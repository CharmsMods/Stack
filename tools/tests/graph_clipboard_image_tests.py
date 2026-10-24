#!/usr/bin/env python3
"""Generate a CPU harness from the actual clipboard/export image branches.

Compile the generated C++ with the real image codec, color metadata and UUID
sources. This deliberately does not pretend to exercise the ImGui UI or an asset
store: portability means the pasted payload needs neither source project/store.
"""
import argparse
from pathlib import Path


def between(text, start, end):
    assert text.count(start) == 1, start
    return text.split(start, 1)[1].split(end, 1)[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    export = (root / 'src/Editor/NodeGraph/EditorNodeGraphSelectionExport.cpp').read_text()
    clipboard = (root / 'src/Editor/NodeGraph/UI/EditorNodeGraphUIClipboard.cpp').read_text()
    serializer = (root / 'src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp').read_text()
    detach = between(export, 'EditorNodeGraph::Node nodeCopy = includeState ? *sourceNode : MakeTreeOnlyNode(*sourceNode);',
                     '        if (nodeCopy.kind == EditorNodeGraph::NodeKind::Layer)')
    validation = between(clipboard, '    // A clipboard has no project asset store',
                         '    std::vector<int> syntheticOutputNodeIds;')
    validation = '    // A clipboard has no project asset store' + validation
    assert clipboard.index(validation) < clipboard.index('    auto targetGraph = *context.graph;')
    assert 'nodeCopy.instanceUuid = Stack::NodeMath::GenerateCanonicalUuid();' in clipboard
    assert 'CopySelectedNodes(editor);' in clipboard and 'PasteNodes(editor);' in clipboard
    helper = 'std::vector<unsigned char> BuildImagePayloadStoragePngBytes' + between(
        serializer, 'std::vector<unsigned char> BuildImagePayloadStoragePngBytes', '\n\n} // namespace')
    write = '            item["label"] = node.image.label;' + between(serializer,
        '            item["label"] = node.image.label;', '            auto metadata')
    read = '            node.kind = NodeKind::Image;' + between(serializer,
        '            node.kind = NodeKind::Image;', '            node.image.originalChannels =')
    generated = r'''
#include "Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.h"
#include "NodeMath/Uuid.h"
#include "Library/LibraryManager.h"
#include "Utils/PixelBufferUtils.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ThirdParty/stb_image_write.h"
#include <iostream>
#include <stdexcept>
// Match the library's thin row-flip adapter without linking its UI services.
void LibraryManager::FlipImageRowsInPlace(std::vector<unsigned char>& pixels, int w, int h, int c) {
    (void)Stack::PixelBuffer::FlipInterleavedRowsInPlace(pixels, w, h, c);
}
using namespace EditorNodeGraph;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
''' + helper + r'''
Node PrepareCopy(const Node& source, bool includeState = true) {
    Node nodeCopy = source;
''' + detach + r'''
    return nodeCopy;
}
nlohmann::json WriteImage(const Node& node) { nlohmann::json item;
''' + write + r'''
    return item;
}
Node ReadImage(const nlohmann::json& item) { Node node;
''' + read + r'''
    return node;
}
struct ScratchGraph { std::vector<Node> nodes; auto& EditNodes() { return nodes; } };
bool Validate(ScratchGraph& tempGraph, std::string* outSummary) {
''' + validation + r'''
    return true;
}
bool Paste(const nlohmann::json& item, std::vector<Node>& destination, std::string& error) {
    ScratchGraph graph; graph.nodes.push_back(ReadImage(item));
    if (!Validate(graph, &error)) return false;
    Node nodeCopy = graph.nodes.front();
    nodeCopy.instanceUuid = Stack::NodeMath::GenerateCanonicalUuid();
    destination.push_back(std::move(nodeCopy)); return true;
}
int main() {
    Node source; source.kind = NodeKind::Image; source.instanceUuid = "source-uuid";
    source.image.width = source.image.height = 2; source.image.channels = 4;
    source.image.pixels = {255,0,0,255,0,255,0,255,0,0,255,255,255,255,0,255};
    source.image.pngBytes = EncodeImagePayloadPngForStorage(source.image.pixels,2,2,4);
    Check(!source.image.pngBytes.empty(), "fixture encode");
    source.image.managedAssetId = "source-project-asset";
    source.image.projectAssetPath = "Assets/source.png";
    const auto payload = WriteImage(PrepareCopy(source));
    Check(payload.contains("pngBytes") && !payload.contains("managedAssetId"), "portable managed export");
    std::string error;
    std::vector<Node> sameProject {source}, otherProject;
    Check(Paste(payload, sameProject, error), "managed duplicate");
    Check(Paste(payload, otherProject, error), "cross-project paste");
    for (const auto* pasted : {&sameProject.back(), &otherProject.back()}) {
        Check(pasted->image.pixels == source.image.pixels, "pixel preservation");
        Check(pasted->image.width == 2 && pasted->image.height == 2, "extent preservation");
        Check(pasted->image.managedAssetId.empty() && pasted->image.projectAssetPath.empty(), "destination asset independence");
        Check(pasted->instanceUuid != source.instanceUuid, "fresh identity");
    }
    Check(sameProject.back().instanceUuid != otherProject.back().instanceUuid, "independent paste identities");
    Check(source.image.managedAssetId == "source-project-asset" && source.image.projectAssetPath == "Assets/source.png", "no live source mutation");
    sameProject.back().image.pixels[0] = 0;
    Check(source.image.pixels[0] == 255 && otherProject.back().image.pixels[0] == 255, "pixel isolation");
    Node shared = source; shared.image.pngBytes.clear();
    shared.image.sharedPixels = std::make_shared<const std::vector<unsigned char>>(shared.image.pixels);
    shared.image.pixels.clear();
    Check(Paste(WriteImage(PrepareCopy(shared)), otherProject, error), "shared pixel export");
    Check(otherProject.back().image.pixels == source.image.pixels, "shared pixel preservation");
    Node embedded = source; embedded.image.managedAssetId.clear(); embedded.image.projectAssetPath.clear();
    Check(Paste(WriteImage(PrepareCopy(embedded)), otherProject, error), "ordinary embedded image");
    auto olderPayload = payload; olderPayload["managedAssetId"] = "foreign-asset";
    olderPayload["projectAssetPath"] = "foreign/path";
    Check(Paste(olderPayload, otherProject, error), "older embedded managed image");
    Check(otherProject.back().image.managedAssetId.empty(), "older identity detached");
    auto missing = olderPayload; missing.erase("pngBytes");
    const auto before = otherProject.size();
    Check(!Paste(missing, otherProject, error) && otherProject.size() == before, "reference-only rejected atomically");
    Check(error.find("embedded image data") != std::string::npos, "actionable rejection");
    Node referenceOnly = source; referenceOnly.image.pixels.clear(); referenceOnly.image.pngBytes.clear();
    Check(!Paste(WriteImage(PrepareCopy(referenceOnly)), otherProject, error), "unhydrated export remains rejectable");
    auto corrupt = olderPayload; corrupt["pngBytes"] = nlohmann::json::binary({1,2,3});
    Check(!Paste(corrupt, otherProject, error), "undecodable reference rejected");
    Node placeholder; placeholder.kind = NodeKind::Image; placeholder.image.isLoading = true;
    Check(Paste(WriteImage(PrepareCopy(placeholder)), otherProject, error), "async placeholder unchanged");
    Node tree = source; tree.image = {};
    Check(Paste(WriteImage(PrepareCopy(tree, false)), otherProject, error), "tree-only image unchanged");
    std::cout << "graph clipboard image tests passed\n";
}
'''
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generated)


if __name__ == '__main__':
    main()
