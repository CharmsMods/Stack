#!/usr/bin/env python3
"""Generate actual CaptureProjectGraphState CPU coverage without UI/GL setup.

The generated method is unchanged production code. Link real graph analysis,
history and byte-storage code; only the editor/project shell is reduced.
"""
import argparse
from pathlib import Path


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    def read(path): return (root / ('src/'+path)).read_text()
    history=read('Editor/Internal/EditorModuleGraphHistory.cpp')
    signature='Stack::Project::ProjectGraphState EditorModule::CaptureProjectGraphState() const {'
    assert history.count(signature)==1
    method=signature+history.split(signature,1)[1].split('\nvoid EditorModule::BeginProjectGraphHistory()',1)[0]
    for kind,field in [('png','pngBytes'),('preview','previewPixels')]:
        assert f'node.instanceUuid+"/{kind}",node.image.{field},node.image.{kind}Storage)' in method
        assert f'node.image.{kind}Storage.Seed(storage.{kind});' in history
    image=read('Editor/Internal/EditorModuleGraphImageNodes.cpp')
    persistence=read('Editor/Internal/EditorModulePersistence.cpp')
    assert 'node->image.pngStorage.Invalidate();\n            node->image.pngBytes = std::move(pngBytes);' in image
    assert 'imageNode->image.pngStorage.Invalidate();\n                imageNode->image.pngBytes = std::move(pngBytes);' in persistence
    assert image.index('node->image.pngStorage.Invalidate();\n    node->image.previewStorage.Invalidate();') < image.index('node->image.pngBytes.clear();')
    decoder=read('Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.cpp')
    assert decoder.index('payload.pngBytes.swap(encodedBytes);') < decoder.index('InvalidateImagePayloadRuntime(payload);') < decoder.index('return true;',decoder.index('InvalidateImagePayloadRuntime(payload);'))
    graph=read('Editor/NodeGraph/EditorNodeGraphCompound.cpp')
    clone=graph.split('Node CloneImageNodeForCompoundExpansion(const Node& source) {',1)[1].split('\n}\n',1)[0]
    assert 'pngStorage' not in clone and 'previewStorage' not in clone and 'pngBytes' not in clone and 'previewPixels' not in clone
    generated=r'''
#include "Project/ProjectGraphHistory.h"
#include "Project/GraphImagePayload.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
struct TestProject {
    EditorNodeGraph::Graph graph;
    Stack::Timeline::TimelineAnimationState timeline;
    Stack::Project::ProjectGraphHistory graphHistory;
};
class EditorModule {
public:
    std::unique_ptr<TestProject> m_Project=std::make_unique<TestProject>();
    Stack::Project::ProjectGraphState CaptureProjectGraphState() const;
};
'''+method+r'''
int main() {
    EditorModule editor;
    for(int i=0;i<2;++i) {
        EditorNodeGraph::Node node;node.id=i+1;node.instanceUuid="image"+std::to_string(i);
        node.kind=EditorNodeGraph::NodeKind::Image;
        node.image.width=node.image.height=1;node.image.channels=4;node.image.pixels={1,2,3,4};
        node.image.pngBytes.assign(32u*1024u*1024u,71);
        editor.m_Project->graph.EditNodes().push_back(std::move(node));
    }
    editor.m_Project->graphHistory.Begin(editor.CaptureProjectGraphState());
    auto run=[&] {
        const auto start=std::chrono::steady_clock::now();
        for(int frame=0;frame<60;++frame) {
            editor.m_Project->graph.EditNodes()[0].position.x+=1;
            editor.m_Project->graphHistory.Observe(editor.CaptureProjectGraphState(),true);
        }
        editor.m_Project->graphHistory.EndGesture();
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    };
    const auto conservative=run();
    for(auto& node:editor.m_Project->graph.EditNodes())node.image.pngStorage.Invalidate();
    auto warm=editor.CaptureProjectGraphState();
    const auto owner=warm.images.at("image0").png;
    const auto certified=run();
    auto after=editor.CaptureProjectGraphState();
    if(after.images.at("image0").png!=owner)throw std::runtime_error("capture lost shared owner");
    for(const auto& node:after.graph.GetNodes())
        if(!node.image.pngBytes.empty() || !node.image.previewPixels.empty())
            throw std::runtime_error("analysis clone retained byte vectors");
    auto& live=editor.m_Project->graph.EditNodes()[0].image;
    live.pngStorage.Invalidate();live.pngBytes[0]=99;
    auto changed=editor.CaptureProjectGraphState();
    if((*changed.images.at("image0").png)[0]!=99 || (*owner)[0]!=71)
        throw std::runtime_error("capture reused changed bytes");
    std::cout<<"Actual capture + history gesture, 60 iterations, 2x32 MiB PNG byte fixtures: conservative "
        <<conservative<<" ms; certified "<<certified<<" ms. CPU component only.\n";
}
'''
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(generated)


if __name__=='__main__': main()
