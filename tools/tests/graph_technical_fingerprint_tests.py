#!/usr/bin/env python3
"""Check the production TechnicalImage key under the real graph schedule.

Extracts the fingerprint branch and regional-demand hash unchanged. Input
fingerprints and cache slots are CPU stand-ins; this does not execute image
passes, validate pixels, or measure GPU rendering time.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def between(source, start, end):
    begin = source.index(start) + len(start)
    return source[begin:source.index(end, begin)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    renderer = root / "src/Renderer/Internal"
    source = (renderer / "RenderPipelineGraphExecutionFingerprintImage.cpp").read_text()
    branch = between(source,
        '} else if (node.kind == RenderGraphNodeKind::TechnicalImage) {',
        '} else if (node.kind == RenderGraphNodeKind::Reformat) {')
    if 'HashViewportDemand(fingerprint, nodeId, socketId);' not in source:
        raise AssertionError("Image fingerprint no longer retains regional demand")
    viewport = (renderer / "RenderPipelineGraphViewport.cpp").read_text()
    demand = between(viewport,
        'void GraphExecutionRuntime::HashViewportDemand(',
        '\nvoid GraphExecutionRuntime::RecordViewportGeometry(')
    hashes = between((renderer / "RenderPipelineGraphExecutionHelpers.h").read_text(),
        'inline void HashCombine(', '\nstd::size_t HashBytes(')
    generated = r'''
#include "Renderer/Internal/RenderPipelineGraphSchedule.h"
#include "Renderer/RenderTiling.h"
#include "Editor/NodeGraph/NodeGraphTypes.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
using namespace Stack::Renderer::GraphExecution;
inline void HashCombine(''' + hashes + r'''
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Harness {
    struct Pipeline { int m_Width=1024,m_Height=512; } pipeline;
    RenderGraphSnapshot graph;
    RenderGraphViewportPlan viewportPlan;
    std::size_t imageIdentity=123,scalarIdentity=456;
    void HashViewportDemand(''' + demand + r'''
    std::size_t Fingerprint(const RenderGraphNode& node)const {
        const int nodeId=node.nodeId;
        std::size_t fingerprint=17;
        const auto findInputLink=[&](int id,const std::string& socket)->const RenderGraphLink* {
            for(const auto& link:graph.links)
                if(link.toNodeId==id && link.toSocketId==socket)return &link;
            return nullptr;
        };
        const auto fingerprintImage=[&](int,const std::string&){return imageIdentity;};
        const auto fingerprintScalar=[&](int,const std::string&){return scalarIdentity;};
''' + branch + r'''
        HashViewportDemand(fingerprint,nodeId,"imageOut");
        return fingerprint;
    }
};
int main()try{
    Harness h;
    for(int id=1;id<=5;++id){RenderGraphNode n;n.nodeId=id;h.graph.nodes.push_back(std::move(n));}
    auto& operation=h.graph.nodes[2];operation.kind=RenderGraphNodeKind::TechnicalImage;
    operation.technicalImageOperation=Stack::NodeMath::TechnicalImageOperation::Exposure;
    h.graph.nodes[3].kind=RenderGraphNodeKind::Reformat;
    h.graph.nodes[4].kind=RenderGraphNodeKind::Mix;
    // A valid authored connection order puts the independent framing branch
    // immediately before TechnicalImage in the breadth/topological schedule.
    h.graph.links={{4,"imageOut",5,"imageB"},{3,"imageOut",5,"imageA"},
        {1,"imageOut",3,"imageIn"},{2,"imageOut",4,"imageIn"}};
    h.graph.outputNodeId=5;
    const auto schedule=BuildGraphEvaluationSchedule(h.graph,{5,"imageOut"});
    Check(schedule.valid,"real graph schedule failed");
    std::vector<int> order;for(const auto& item:schedule.outputs)order.push_back(item.nodeId);
    Check(order==std::vector<int>({2,1,4,3,5}),"fixture no longer places independent framing before TechnicalImage");
    std::unordered_map<std::size_t,int> warmedCache;
    int misses=0,hits=0;
    const auto evaluate=[&](int frameWidth,int frameHeight){
        for(const auto& item:schedule.outputs){
            if(item.nodeId==1)h.pipeline={1024,512};
            if(item.nodeId==2)h.pipeline={640,480};
            if(item.nodeId==4)h.pipeline={frameWidth,frameHeight};
            if(item.nodeId==3){
                const auto key=h.Fingerprint(operation);
                if(warmedCache.emplace(key,1).second)++misses;else ++hits;
                h.pipeline={1024,512};
            }
        }
    };
    evaluate(600,400);evaluate(900,600);
    Check(misses==1 && hits==1,"unrelated framing extent invalidated TechnicalImage cache");
    const auto original=h.Fingerprint(operation);
    ++h.imageIdentity;Check(h.Fingerprint(operation)!=original,"real input identity change was not invalidating");
    --h.imageIdentity;
    operation.technicalExposureValue=1.f;Check(h.Fingerprint(operation)!=original,"manual exposure change was not invalidating");
    operation.technicalExposureValue=0.f;
    operation.technicalImageOperation=Stack::NodeMath::TechnicalImageOperation::Premultiply;
    Check(h.Fingerprint(operation)!=original,"operation change was not invalidating");
    operation.technicalImageOperation=Stack::NodeMath::TechnicalImageOperation::Exposure;
    h.graph.links.push_back({6,"valueOut",3,"evIn"});
    const auto driven=h.Fingerprint(operation);
    ++h.scalarIdentity;Check(h.Fingerprint(operation)!=driven,"driven exposure change was not invalidating");
    h.graph.links.pop_back();
    h.viewportPlan.valid=true;
    auto& stage=h.viewportPlan.stages[{3,"imageOut"}];stage.regionalExecutable=true;
    stage.renderWidth=512;stage.renderHeight=256;stage.demand={1024,512,0,0,500,500};
    const auto regional=h.Fingerprint(operation);
    ++stage.demand.x;Check(h.Fingerprint(operation)!=regional,"changed regional origin reused pixels");
    --stage.demand.x;++stage.renderWidth;
    Check(h.Fingerprint(operation)!=regional,"changed regional sample grid reused pixels");
    h.viewportPlan.valid=false;
    h.graph.links.erase(std::remove_if(h.graph.links.begin(),h.graph.links.end(),
        [](const auto& link){return link.toNodeId==3 && link.toSocketId=="imageIn";}),h.graph.links.end());
    h.pipeline={640,480};const auto unlinked=h.Fingerprint(operation);
    h.pipeline={800,600};Check(h.Fingerprint(operation)!=unlinked,"unlinked fallback lost extent dependency");
    std::cout<<"Real schedule/TechnicalImage fingerprint cache eligibility checks passed (no image passes).\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
'''
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(generated)
        return
    with tempfile.TemporaryDirectory(prefix="stack-technical-fingerprint-") as temp:
        cpp=Path(temp)/"test.cpp"
        exe=Path(temp)/"test"
        cpp.write_text(generated)
        subprocess.run([args.cxx,"-std=c++17","-O2","-I"+str(root/"src"),str(cpp),
            str(renderer/"RenderPipelineGraphSchedule.cpp"),"-o",str(exe)],check=True)
        subprocess.run([str(exe)],check=True)


if __name__ == "__main__":
    main()
