#!/usr/bin/env python3
"""Exercise the production RAW-node pruning method with counted resource owners.

Graph/snapshot types are real. GPU pipelines and decoded/proxy payloads are
counted stand-ins: this checks ownership, not GL deletion or pixel equivalence.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def method(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def generate(root):
    resources = (root / "src/Renderer/Internal/RenderPipelineResources.cpp").read_text()
    body = method(resources, "void RenderPipeline::PruneObsoleteRawNodeResources(")
    execution = (root / "src/Renderer/Internal/RenderPipelineGraphExecution.cpp").read_text()
    call = execution.index("    PruneObsoleteRawNodeResources(graph, topology);")
    assert execution.index("if (!topology.IsBoundTo(graph) || !topology.valid)") < call
    assert execution.index("if (!hasOutputNode)") < call
    assert execution.rfind("    m_OutputTexture = 0;", 0, call) > execution.index("if (!hasOutputNode)")
    assert execution.index("    m_GraphSourceTexture = 0;") < call
    assert call < execution.index("    GraphExecutionRuntime executionRuntime(")
    builder = (root / "src/Project/GraphSnapshotBuilder.cpp").read_text()
    assert re.search(r"snapshot\.completeNodeSet\s*=\s*stageOutputNodeId\s*<=\s*0\s*;", builder)
    gpu = (root / "src/Raw/RawGpuPipeline.cpp").read_text()
    assert "Clear();" in method(gpu, "RawGpuPipeline::~RawGpuPipeline()")
    assert "glDeleteTextures" in method(gpu, "void RawGpuPipeline::Clear()")
    return r'''
#include "Renderer/Internal/RenderPipelineGraphSchedule.h"
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using ReleaseLog = std::map<std::string, int>;
struct Resource {
    std::string identity;
    ReleaseLog* released;
    Resource(std::string id, ReleaseLog* log):identity(std::move(id)),released(log){}
    Resource(const Resource&) = delete;
    ~Resource(){++(*released)[identity];}
};
struct RenderPipeline {
    ReleaseLog* released;
    std::unordered_map<int, Resource> m_RawPipelines, m_RawDataCache, m_RawPreviewDataCache;
    std::unordered_map<int, std::string> m_RawDataCachePaths, m_RawPreviewDataCacheKeys;
    std::unordered_map<std::string, std::shared_ptr<const int>> m_RawSharedSourceData;
    unsigned int m_OutputTexture=73, m_GraphSourceTexture=74;
    int m_Width=23,m_Height=31;
    explicit RenderPipeline(ReleaseLog& log):released(&log){}
    void Add(int id) {
        m_RawPipelines.try_emplace(id,"gpu"+std::to_string(id),released);
        m_RawDataCache.try_emplace(id,"raw"+std::to_string(id),released);
        m_RawPreviewDataCache.try_emplace(id,"proxy"+std::to_string(id),released);
        m_RawDataCachePaths[id]="original-read-only.raw";
        m_RawPreviewDataCacheKeys[id]="proxy-key";
    }
    void PruneObsoleteRawNodeResources(const RenderGraphSnapshot& graph,
        const Stack::Renderer::GraphExecution::GraphTopologyIndex& topology);
};
''' + body + r'''
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
auto Topology(const RenderGraphSnapshot& graph) {
    Stack::Renderer::GraphExecution::GraphTopologyIndex topology;
    for(const auto& node:graph.nodes)topology.nodes[node.nodeId]=&node;
    return topology;
}
RenderGraphSnapshot Snapshot(bool complete,int id,RenderGraphNodeKind kind) {
    RenderGraphSnapshot graph;
    graph.completeNodeSet=complete;
    graph.nodes.emplace_back();graph.nodes.back().nodeId=id;graph.nodes.back().kind=kind;
    return graph;
}
void CheckUnaffected(const RenderPipeline& pipeline) {
    Require(pipeline.m_OutputTexture==73 && pipeline.m_GraphSourceTexture==74 &&
        pipeline.m_Width==23 && pipeline.m_Height==31,"pruning mutated presentation output");
}
void CompleteAndPartial() {
    Require(!RenderGraphSnapshot{}.completeNodeSet,"ad-hoc snapshots must not imply complete ownership");
    ReleaseLog released;
    RenderPipeline pipeline(released);
    pipeline.Add(10);pipeline.Add(99);
    auto lease=std::make_shared<const int>(61);
    pipeline.m_RawSharedSourceData["source"]=lease;
    auto partial=Snapshot(false,10,RenderGraphNodeKind::RawSource);
    const auto* raw=&pipeline.m_RawDataCache.at(10);
    const auto* proxy=&pipeline.m_RawPreviewDataCache.at(10);
    pipeline.PruneObsoleteRawNodeResources(partial,Topology(partial));
    Require(released["gpu10"]==1 && released["raw10"]==0 && released["proxy10"]==0,
        "known RawSource kind did not prune only its incompatible GPU owner");
    Require(pipeline.m_RawPipelines.count(99) && pipeline.m_RawDataCache.count(99) &&
        pipeline.m_RawPreviewDataCache.count(99),"auxiliary snapshot evicted absent live nodes");
    auto full=partial;full.completeNodeSet=true;
    pipeline.PruneObsoleteRawNodeResources(full,Topology(full));
    Require(!pipeline.m_RawPipelines.count(99) && !pipeline.m_RawDataCache.count(99) &&
        !pipeline.m_RawPreviewDataCache.count(99) && !pipeline.m_RawDataCachePaths.count(99) &&
        !pipeline.m_RawPreviewDataCacheKeys.count(99),"removed node retained resource or metadata");
    Require(released["gpu99"]==1 && released["raw99"]==1 && released["proxy99"]==1,
        "removed resource was not released exactly once");
    Require(raw==&pipeline.m_RawDataCache.at(10) && proxy==&pipeline.m_RawPreviewDataCache.at(10),
        "unchanged resources were recreated instead of retained");
    const auto once=released;
    pipeline.PruneObsoleteRawNodeResources(full,Topology(full));
    Require(released==once,"repeated pruning released an owner again");
    Require(lease.use_count()==2 && *pipeline.m_RawSharedSourceData.at("source")==61,
        "pruning mutated independently shared immutable source data");
    CheckUnaffected(pipeline);
    auto retyped=Snapshot(false,10,RenderGraphNodeKind::Image);
    pipeline.PruneObsoleteRawNodeResources(retyped,Topology(retyped));
    Require(!pipeline.m_RawDataCache.count(10) && !pipeline.m_RawPreviewDataCache.count(10) &&
        released["raw10"]==1 && released["proxy10"]==1,
        "same-ID incompatible replacement retained decoder/proxy resources");
    CheckUnaffected(pipeline);
}
void CompatibleKindsAndIsolation() {
    using Kind=RenderGraphNodeKind;
    for(auto kind:{Kind::RawSource,Kind::RawDevelopment,Kind::RawProjectSourceSet,
                  Kind::RawDecode,Kind::RawDevelop,Kind::Image}) {
        ReleaseLog released,otherReleased;
        RenderPipeline pipeline(released),other(otherReleased);
        pipeline.Add(1);other.Add(1);
        auto graph=Snapshot(true,1,kind);
        pipeline.PruneObsoleteRawNodeResources(graph,Topology(graph));
        const bool gpu=kind==Kind::RawDevelopment || kind==Kind::RawProjectSourceSet ||
            kind==Kind::RawDecode || kind==Kind::RawDevelop;
        const bool raw=kind==Kind::RawSource;
        const bool proxy=raw || kind==Kind::RawDevelopment || kind==Kind::RawProjectSourceSet;
        Require(bool(pipeline.m_RawPipelines.count(1))==gpu,"GPU owner kind contract changed");
        Require(bool(pipeline.m_RawDataCache.count(1))==raw &&
            bool(pipeline.m_RawDataCachePaths.count(1))==raw,"decoder owner kind contract changed");
        Require(bool(pipeline.m_RawPreviewDataCache.count(1))==proxy &&
            bool(pipeline.m_RawPreviewDataCacheKeys.count(1))==proxy,"proxy owner kind contract changed");
        Require(released["gpu1"]==!gpu && released["raw1"]==!raw && released["proxy1"]==!proxy,
            "kind replacement did not release only incompatible owners once");
        Require(otherReleased.empty() && other.m_RawPipelines.size()==1 &&
            other.m_RawDataCache.size()==1 && other.m_RawPreviewDataCache.size()==1,
            "pruning crossed independent pipeline owners");
        CheckUnaffected(pipeline);CheckUnaffected(other);
    }
}
void AlternatingPartialSnapshots() {
    ReleaseLog released;
    RenderPipeline pipeline(released);
    pipeline.m_RawPipelines.try_emplace(1,"gpu1",&released);
    pipeline.m_RawPipelines.try_emplace(2,"gpu2",&released);
    const auto* first=&pipeline.m_RawPipelines.at(1);
    const auto* second=&pipeline.m_RawPipelines.at(2);
    const auto a=Snapshot(false,1,RenderGraphNodeKind::RawDevelopment);
    const auto b=Snapshot(false,2,RenderGraphNodeKind::RawDecode);
    for(const auto* graph:{&a,&b,&a,&b})
        pipeline.PruneObsoleteRawNodeResources(*graph,Topology(*graph));
    Require(released.empty() && first==&pipeline.m_RawPipelines.at(1) &&
        second==&pipeline.m_RawPipelines.at(2),
        "alternating auxiliary snapshots thrashed still-live RAW uploads");
    CheckUnaffected(pipeline);
}
int main()try {
    CompleteAndPartial();CompatibleKindsAndIsolation();AlternatingPartialSnapshots();
    std::cout<<"RAW node resources: complete/partial ownership, retyping, once-only release, reuse and isolation passed.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, help="Generate C++ for the CMake target")
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = generate(root)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(source)
        return
    with tempfile.TemporaryDirectory(prefix="stack-raw-node-lifetime-") as directory:
        cpp, exe = Path(directory) / "test.cpp", Path(directory) / "test"
        cpp.write_text(source)
        if Path(args.cxx).name.lower() in ("cl", "cl.exe", "clang-cl", "clang-cl.exe"):
            exe = exe.with_suffix(".exe")
            command = [args.cxx, "/nologo", "/std:c++17", "/EHsc", "/I" + str(root / "src"),
                       str(cpp), "/Fe:" + str(exe)]
        else:
            command = [args.cxx, "-std=c++17", "-O2", "-I" + str(root / "src"),
                       str(cpp), "-o", str(exe)]
        subprocess.run(command, check=True, cwd=directory)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
