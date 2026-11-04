#pragma once
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.h"
#include "Hdr/ImageFile.h"
#include "Project/GraphImagePayload.h"
#include <algorithm>
#include <stdexcept>

namespace Stack::Validation {
// Exercise the production serializer with the files produced by the native
// export check. Keep only one editor-side full-resolution master at a time.
inline void CheckHdrFileReopen(const std::vector<unsigned char>& file, const Hdr::Image& upright) {
    const auto check=[&](const EditorNodeGraph::ImagePayload& loaded) {
        const auto render=Project::BuildRenderImagePayload(loaded);
        if(!render.hdrPixels || render.width!=upright.width || render.height!=upright.height ||
            render.hdrPixels->size()!=upright.rgba.size() || loaded.pixels.size()!=upright.rgba.size() ||
            loaded.pngBytes!=file || render.sourceDescriptor.reference.value!=NodeMath::ReferenceState::Display ||
            render.sourceDescriptor.transfer.value.kind!=NodeMath::TransferKind::Linear ||
            render.sourceDescriptor.color.value.identity!="srgb-d65" || !render.sourceDescriptor.range.value.allowsAboveNominal)
            throw std::runtime_error("HDR image lost its original file, display rendition, or declared color domain.");
        for(int y:{0,upright.height/2,upright.height-1}) for(int x:{0,upright.width/2,upright.width-1}) {
            const size_t src=(size_t(y)*upright.width+x)*4;
            const size_t dst=(size_t(upright.height-y-1)*upright.width+x)*4;
            if(!std::equal(upright.rgba.begin()+src,upright.rgba.begin()+src+4,render.hdrPixels->begin()+dst))
                throw std::runtime_error("HDR image rows or straight-alpha samples changed on reopen.");
        }
    };
    nlohmann::json stored;
    int id=-1;
    {
        EditorNodeGraph::Graph graph; graph.ResetFromLayers(0,true);
        id=graph.GetActiveImageNodeId();
        auto* node=graph.FindNode(id);
        if(!node || !EditorNodeGraph::DecodeImagePayloadPngBytes(file,node->image))
            throw std::runtime_error("Editor could not decode the exported image.");
        check(node->image);
        stored=EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(),graph);
    }
    EditorNodeGraph::Graph restored;
    EditorNodeGraph::DeserializeGraphPayload(stored,restored,0);
    auto* node=restored.FindNode(id);
    if(!node) throw std::runtime_error("Project reopen lost the HDR Image node.");
    check(node->image);
}
}
