#!/usr/bin/env python3
"""Bounded CPU checks of the production frame-data cache methods.

UI/graph endpoints are counted stand-ins. The real socket revision/filter and
activity-counting bodies are extracted unchanged; no GL, files or app UI run.
"""
import argparse
from pathlib import Path
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
    ui = (root / "src/Editor/NodeGraph/EditorNodeGraphUI.cpp").read_text()
    header = (root / "src/Editor/NodeGraph/EditorNodeGraphUI.h").read_text()
    activity = (root / "src/Editor/Internal/EditorModuleActivity.cpp").read_text()
    counts = activity[activity.index("    int quickAvailable = 0;"):
                      activity.index("    const int sourceCount =")]
    assert "const std::vector<EditorNodeGraph::SocketDefinition>& PresentedSockets(" in header
    assert "const auto& sockets =\n        PresentedSockets(graph, node);" in ui
    assert "mutable std::unordered_map<int, NodeLayoutCache> m_NodeLayoutCache;" in header
    mutation = (root / "src/Editor/NodeGraph/Model/EditorNodeGraphMutation.cpp").read_text()
    for signature in ("bool Graph::TryConnectSockets(",
                      "bool Graph::RemoveLink(int fromNodeId, const std::string& fromSocketId,"):
        assert "TouchStructure();" in method(mutation, signature)
    bodies = "\n".join(method(ui, signature) for signature in (
        "std::uint64_t EditorNodeGraphUI::LogicalNodeInputRevision(",
        "bool EditorNodeGraphUI::IsSocketConnected(",
        "const std::vector<EditorNodeGraph::SocketDefinition>& EditorNodeGraphUI::PresentedSockets("))
    return r'''
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
bool countAllocations=false;
int allocations=0,socketCopies=0;
#if defined(_MSC_VER)
#define CACHE_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define CACHE_NOINLINE __attribute__((noinline))
#else
#define CACHE_NOINLINE
#endif
CACHE_NOINLINE void* operator new(std::size_t size) {
    if(countAllocations)++allocations;
    if(void* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc();
}
CACHE_NOINLINE void operator delete(void* value)noexcept{std::free(value);}
CACHE_NOINLINE void operator delete(void* value,std::size_t)noexcept{std::free(value);}
#undef CACHE_NOINLINE
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
namespace ImGui {
struct Style {float FontSizeBase=13;};
Style style;int fonts[2];void* font=&fonts[0];
Style& GetStyle(){return style;}
void* GetFont(){return font;}
}
void MixRevision(std::uint64_t& seed,std::uint64_t value){
    seed^=value+0x9e3779b97f4a7c15ull+(seed<<6u)+(seed>>2u);
}
namespace EditorNodeGraph {
enum class NodeKind{Layer,RawOperation,MaskGenerator,Other};
enum class SocketDirection{Input,Output};
enum class SocketVisibilityTier{Required,Advanced};
struct Coverage {
    std::string canvas;
    std::string value(const char*,std::string)const{return canvas;}
};
struct Node {
    int id=1,layerType=0,layerIndex=0,role=0,maskKind=0,maskUtilityKind=0;
    int imageToMaskKind=0,imageGeneratorKind=0,scopeKind=0,dataMathMode=0;
    NodeKind kind=NodeKind::Other;
    struct {int kind=0;}rawOperation;
    struct {int sampleCount=1;}imageToMaskSettings;
    struct {int operation=0;}technicalImageSettings;
    bool expanded=false,outputEnabled=true;
    std::string rawLayerOwnerId,title="Node",typeId="builtin";
    Coverage rawCoverage;
};
struct SocketDefinition {
    std::string id,label,semanticRoleKey;
    int nodeId=1;
    SocketDirection direction=SocketDirection::Input;
    SocketVisibilityTier visibilityTier=SocketVisibilityTier::Required;
    bool visible=false;
    std::vector<std::string> metadata;
    SocketDefinition()=default;
    SocketDefinition(std::string name,SocketDirection dir,SocketVisibilityTier tier)
        :id(std::move(name)),label("A semantic socket label"),semanticRoleKey("image.color"),
         direction(dir),visibilityTier(tier),metadata{"red","green","blue"}{}
    SocketDefinition(const SocketDefinition& other)
        :id(other.id),label(other.label),semanticRoleKey(other.semanticRoleKey),nodeId(other.nodeId),
         direction(other.direction),visibilityTier(other.visibilityTier),visible(other.visible),metadata(other.metadata){++socketCopies;}
    SocketDefinition& operator=(const SocketDefinition& other){
        ++socketCopies;id=other.id;label=other.label;semanticRoleKey=other.semanticRoleKey;nodeId=other.nodeId;
        direction=other.direction;visibilityTier=other.visibilityTier;visible=other.visible;metadata=other.metadata;return *this;
    }
    SocketDefinition(SocketDefinition&&)=default;
    SocketDefinition& operator=(SocketDefinition&&)=default;
};
struct Link {int fromNodeId=1,toNodeId=1;std::string fromSocketId,toSocketId;};
struct Graph {
    std::uint64_t revision=1;
    int preview=0;
    mutable int socketReads=0,linkVisits=0;
    std::vector<SocketDefinition> sockets;
    std::vector<Link> links;
    std::uint64_t GetStructureRevision()const{return revision;}
    int GetSocketPreviewIntent(int)const{return preview;}
    std::vector<SocketDefinition> GetSockets(const Node&,bool)const{++socketReads;return sockets;}
    template<class F>void ForEachIncomingLink(int id,F f)const{
        for(const auto& link:links)if(link.toNodeId==id){++linkVisits;f(link);}
    }
    template<class F>void ForEachOutgoingLink(int id,F f)const{
        for(const auto& link:links)if(link.fromNodeId==id){++linkVisits;f(link);}
    }
};
}
struct Editor {std::uint64_t renderRevision=1;std::uint64_t GetRenderRevision()const{return renderRevision;}};
struct EditorNodeGraphUI {
    struct NodeLayoutCache {
        std::vector<EditorNodeGraph::SocketDefinition> declaredSockets,presentedSockets;
        std::uint64_t logicalInputRevision=0,presentedInputRevision=0;
        bool hasLogicalInputs=false,hasPresentedSockets=false;
    };
    mutable std::unordered_map<int,NodeLayoutCache> m_NodeLayoutCache;
    bool m_RenderPreviewOnly=false,m_RenderCaptureOnly=false;
    Editor* m_ActiveEditor=nullptr;
    struct {EditorNodeGraph::Graph* graph=nullptr;std::uint64_t revision=1,documentRevision=1;}m_GraphContext;
    std::unordered_set<int>m_RevealedAdvancedInputNodes,m_RevealedAdvancedOutputNodes;
    struct Spec{int presentation=0,density=0;float preferredWidth=280,maxWidth=400;};
    Spec ResolveLayerSurfaceSpec(const Editor*,int)const{return {};}
    bool ResolveLayerUsesRichNodeSurface(const Editor*,int)const{return false;}
    std::uint64_t LogicalNodeInputRevision(const EditorNodeGraph::Graph&,const EditorNodeGraph::Node&)const;
    bool IsSocketConnected(const EditorNodeGraph::Graph&,const EditorNodeGraph::SocketDefinition&)const;
    const std::vector<EditorNodeGraph::SocketDefinition>& PresentedSockets(const EditorNodeGraph::Graph&,const EditorNodeGraph::Node&)const;
};
''' + bodies + r'''
using namespace EditorNodeGraph;
void Expect(const std::vector<SocketDefinition>& sockets,std::initializer_list<const char*> ids){
    Require(sockets.size()==ids.size(),"presented socket count changed");
    std::size_t i=0;for(const char* id:ids){Require(sockets[i].id==id && sockets[i].visible,"socket order/visibility changed");++i;}
}
void SteadyReuse(EditorNodeGraphUI& ui,Graph& graph,const Node& node){
    const auto* prior=&ui.PresentedSockets(graph,node);
    socketCopies=allocations=0;graph.socketReads=graph.linkVisits=0;
    countAllocations=true;
    for(int i=0;i<20;++i)Require(&ui.PresentedSockets(graph,node)==prior,"socket list not borrowed");
    countAllocations=false;
    Require(socketCopies==0 && allocations==0 && graph.socketReads==0 && graph.linkVisits==0,
        "unchanged presented sockets copied, allocated or rescanned declarations/connections");
}
void SocketCache(){
    Editor editor,otherEditor;Graph graph;Node node;node.kind=NodeKind::RawOperation;
    graph.sockets={{"required",SocketDirection::Input,SocketVisibilityTier::Required},
        {"advancedIn",SocketDirection::Input,SocketVisibilityTier::Advanced},
        {"output",SocketDirection::Output,SocketVisibilityTier::Required},
        {"advancedOut",SocketDirection::Output,SocketVisibilityTier::Advanced},
        {"inputImageOut",SocketDirection::Output,SocketVisibilityTier::Required}};
    EditorNodeGraphUI ui;ui.m_ActiveEditor=&editor;ui.m_GraphContext.graph=&graph;
    Expect(ui.PresentedSockets(graph,node),{"required","output"});SteadyReuse(ui,graph,node);
    auto& layout=ui.m_NodeLayoutCache[node.id];
    layout.declaredSockets=graph.sockets;layout.logicalInputRevision=ui.LogicalNodeInputRevision(graph,node);layout.hasLogicalInputs=true;
    layout.hasPresentedSockets=false;graph.socketReads=0;
    Expect(ui.PresentedSockets(graph,node),{"required","output"});
    Require(graph.socketReads==0,"presented rebuild ignored current declaration cache");
    ++graph.revision;
    graph.links.push_back({1,1,"inputImageOut","advancedIn"});
    Expect(ui.PresentedSockets(graph,node),{"required","advancedIn","output","inputImageOut"});SteadyReuse(ui,graph,node);
    graph.links.clear();++graph.revision;
    Expect(ui.PresentedSockets(graph,node),{"required","output"});
    ui.m_RevealedAdvancedInputNodes.insert(1);
    Expect(ui.PresentedSockets(graph,node),{"required","advancedIn","output"});
    ui.m_RevealedAdvancedOutputNodes.insert(1);
    Expect(ui.PresentedSockets(graph,node),{"required","advancedIn","output","advancedOut"});
    ui.m_RevealedAdvancedInputNodes.clear();ui.m_RevealedAdvancedOutputNodes.clear();
    node.kind=NodeKind::MaskGenerator;
    Expect(ui.PresentedSockets(graph,node),{"required","output","inputImageOut"});
    node.rawCoverage.canvas="project";
    Expect(ui.PresentedSockets(graph,node),{"output","inputImageOut"});
    node.rawCoverage.canvas.clear();
    graph.sockets[0].label="Renamed input";graph.sockets[0].metadata={"new metadata"};++editor.renderRevision;
    const auto& renamed=ui.PresentedSockets(graph,node);
    Require(renamed[0].label=="Renamed input" && renamed[0].metadata==graph.sockets[0].metadata,"metadata edit stale");
    std::swap(graph.sockets[0],graph.sockets[2]);++graph.revision;
    Expect(ui.PresentedSockets(graph,node),{"output","required","inputImageOut"});
    SteadyReuse(ui,graph,node);
    for(bool preview:{false,true}){
        ui.m_RenderPreviewOnly=preview;ui.m_RenderCaptureOnly=!preview;
        graph.socketReads=0;ui.PresentedSockets(graph,node);ui.PresentedSockets(graph,node);
        Require(graph.socketReads==2,"preview/capture inputs were incorrectly retained");
    }
    ui.m_RenderPreviewOnly=ui.m_RenderCaptureOnly=false;
    Graph other=graph;other.sockets[0].label="Other graph";ui.m_GraphContext.graph=&other;
    Require(ui.PresentedSockets(other,node)[0].label=="Other graph","graph owner reuse stale");
    other.sockets[0].label="Other editor";ui.m_ActiveEditor=&otherEditor;
    Require(ui.PresentedSockets(other,node)[0].label=="Other editor","editor owner reuse stale");
    const auto requireRevisionChange=[&](auto edit){
        const auto before=ui.LogicalNodeInputRevision(other,node);edit();
        Require(before!=ui.LogicalNodeInputRevision(other,node),"presentation input missing from revision");
    };
    requireRevisionChange([&]{node.title="New title";});
    requireRevisionChange([&]{node.typeId="New type";});
    requireRevisionChange([&]{node.expanded=!node.expanded;});
    requireRevisionChange([&]{other.preview=1;});
    requireRevisionChange([&]{ImGui::style.FontSizeBase=16;});
    requireRevisionChange([&]{ImGui::font=&ImGui::fonts[1];});
}
namespace Stack::RawWorkspace {enum class ThumbnailStatus{Missing,Valid,Ready};}
struct Source {
    struct {Stack::RawWorkspace::ThumbnailStatus status;} thumbnail,transientThumbnail;
};
struct Sources {
    std::vector<Source> values;
    mutable int visits=0;
    struct Iterator{
        const Sources* owner;std::size_t at;
        const Source& operator*()const{++owner->visits;return owner->values[at];}
        Iterator& operator++(){++at;return *this;}
        bool operator!=(const Iterator& other)const{return at!=other.at;}
    };
    Iterator begin()const{return {this,0};}Iterator end()const{return {this,values.size()};}
};
std::pair<int,int> CountReadiness(const Sources& sources,bool scanBusy,bool thumbnailBusy){
    struct {const Sources& sources;}m_RawWorkspace{sources};
''' + counts + r'''
    return {quickAvailable,standardAvailable};
}
void ActivityCounts(){
    using S=Stack::RawWorkspace::ThumbnailStatus;
    Sources sources{{{{S::Missing},{S::Missing}},{{S::Ready},{S::Missing}},
        {{S::Valid},{S::Missing}},{{S::Missing},{S::Ready}}}};
    Require(CountReadiness(sources,false,false)==std::make_pair(0,0) && sources.visits==0,"idle activity scanned sources");
    for(const auto& state:{std::make_pair(true,false),std::make_pair(false,true),std::make_pair(true,true)}){
        sources.visits=0;
        Require(CountReadiness(sources,state.first,state.second)==std::make_pair(3,2) && sources.visits==4,
            "active readiness output/count changed");
    }
}
int main()try{
    SocketCache();ActivityCounts();
    std::cout<<"UI frame data: unchanged sockets 0 copies/allocations/schema reads/link visits; "
        "connection, reveal, metadata, ownership, preview/capture and readiness checks passed.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    source = generate(Path(__file__).resolve().parents[2])
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(source)
        return
    with tempfile.TemporaryDirectory(prefix="stack-ui-frame-data-") as directory:
        cpp, exe = Path(directory) / "test.cpp", Path(directory) / "test"
        cpp.write_text(source)
        if Path(args.cxx).name.lower() in ("cl", "cl.exe", "clang-cl", "clang-cl.exe"):
            exe = exe.with_suffix(".exe")
            command = [args.cxx, "/nologo", "/std:c++17", "/EHsc", str(cpp), "/Fe:" + str(exe)]
        else:
            command = [args.cxx, "-std=c++17", "-O2", str(cpp), "-o", str(exe)]
        subprocess.run(command, check=True, cwd=directory)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
