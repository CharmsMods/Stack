#!/usr/bin/env python3
"""Generate a CPU Output-card ownership regression.

Copies the shipping card readout/Export and context-menu Export branches,
single-output resolver, and
accepted-presentation readiness checks unchanged. Completed graph chains,
semantic descriptors, RAW source identity/hash accessors, ImGui, GPU statistics
and native geometry are boundary doubles. No files are exported and no GPU/image
decoding is performed.
"""
import argparse
from pathlib import Path


def block(source, marker):
    if source.count(marker) != 1:
        raise ValueError(f"Expected one source block: {marker}")
    start = source.index(marker)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


PREAMBLE = r'''
#include "Editor/NodeGraph/UI/OutputCardPresentation.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include "Editor/RenderPresentationState.h"
#include "Renderer/OutputTextureStatsCache.h"
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
struct ImVec2 { float x,y; ImVec2(float a,float b):x(a),y(b){} };
struct ImVec4 { ImVec4(float,float,float,float){} };
constexpr int ImGuiHoveredFlags_AllowWhenDisabled=1;
namespace ImGui {
std::vector<std::string> text;
bool disabled=false,click=false,exportEnabled=false;
void Record(const char* format,va_list args) { char buffer[1024];std::vsnprintf(buffer,sizeof(buffer),format,args);text.emplace_back(buffer); }
void TextDisabled(const char* format,...) {va_list args;va_start(args,format);Record(format,args);va_end(args);}
void TextWrapped(const char* format,...) {va_list args;va_start(args,format);Record(format,args);va_end(args);}
void TextColored(ImVec4,const char* format,...) {va_list args;va_start(args,format);Record(format,args);va_end(args);}
void SetTooltip(const char* format,...) {va_list args;va_start(args,format);Record(format,args);va_end(args);}
void Dummy(ImVec2){}
void BeginDisabled(bool value){disabled=value;}
void EndDisabled(){disabled=false;}
bool Button(const char*,ImVec2){exportEnabled=!disabled;return click&&!disabled;}
bool MenuItem(const char*){exportEnabled=!disabled;return click&&!disabled;}
bool IsItemHovered(int){return true;}
bool Has(const std::string& part){return std::any_of(text.begin(),text.end(),[&](const auto& line){return line.find(part)!=std::string::npos;});}
}
namespace FileDialogs {int opens=0;std::string SavePngFileDialog(const char*,const char*){++opens;return "not-written.png";}}
namespace Stack::NodeMath {
// Formatting is not under test; retain the real descriptor value type.
std::string CompactDescriptorLabel(const ValueDescriptor& value) {
    return value.logicalType==LogicalValueType::Channel ? "Channel" : "Image";
}
}
struct RenderTextureStats {bool valid=true;float minRgb=0,maxRgb=1,hdrPixelPercent=0,displayClipPercent=0;};
struct Pipeline {
    int reads=0,queries=0;
    Stack::Renderer::OutputTextureStatsCache<RenderTextureStats> cache;
    RenderTextureStats GetOutputTextureStats(){++queries;return cache.Get(1,1,1,[&](bool& completed){++reads;completed=true;return RenderTextureStats{};});}
};
struct FixtureGraph {
    int m_OutputNodeId=1;
    std::vector<EditorNodeGraph::CompletedChainInfo> completed;
    const auto& GetCompletedChains()const{return completed;}
    std::vector<int> GetConnectedOutputNodeIds()const {std::vector<int> ids;for(const auto& c:completed)ids.push_back(c.outputNodeId);return ids;}
    bool IsOutputConnected()const;
    int ResolvePreviewOutputNodeId()const;
};
struct Project {FixtureGraph graph;};
struct EditorModule {
    Project project;Project* m_Project=&project;
    Pipeline pipeline;
    bool rawProjection=false,composite=false,maskPreview=false,busy=false,native=true,exportBusy=false;
    bool m_RenderDirty=false,m_RenderPending=false;
    std::uint64_t m_ViewportOutputRenderGeneration=1,m_RenderGeneration=1,m_LatestRawPresentationGeneration=0;
    std::size_t m_LatestRawPresentationRecipeRevision=0;
    std::string rawSourceIdentity="raw-source-A";
    std::uint64_t rawSourceHash=41;
    Stack::EditorRendering::RenderPresentationState m_RenderPresentation;
    Stack::GraphRendering::RequestTag current;
    std::map<int,EditorNodeGraph::GraphOutputDescription> descriptions;
    std::vector<Stack::NodeMath::Diagnostic> diagnostics;
    Stack::NodeMath::ValueDescriptor m_LastGraphOutputSemanticDescriptor;
    std::string m_LastGraphOutputSemanticDescriptorIdentity="global";
    int exports=0,lastDescriptorNode=-1;
    bool IsEditingRawLayerMaskGraph()const{return rawProjection;}
    bool IsCompositeViewportMode()const{return composite;}
    bool IsAutoGainMaskPreviewActive()const{return maskPreview;}
    bool IsAnyRenderBackendBusy()const{return busy;}
    bool HasCompleteNativeViewportOutput()const{return native;}
    bool IsExportBusy()const{return exportBusy;}
    Pipeline& GetPipeline(){return pipeline;}
    FixtureGraph& GetNodeGraph(){return project.graph;}
    bool OutputPathNeedsViewTransform(int)const{return false;}
    const auto& GetGraphSemanticDiagnostics()const{return diagnostics;}
    const EditorNodeGraph::GraphOutputDescription* GetGraphOutputDescription(int id,const std::string& socket){
        if(socket!=EditorNodeGraph::kImageOutputSocketId)throw std::runtime_error("wrong descriptor endpoint");
        lastDescriptorNode=id;const auto it=descriptions.find(id);return it==descriptions.end()?nullptr:&it->second;
    }
    bool TryGetGraphOutputSemanticDescriptor(Stack::NodeMath::ValueDescriptor& descriptor)const;
    bool RequestExportImage(const std::string&){++exports;return true;}
    auto CurrentGraphRenderTag()const{return current;}
    std::string GetActiveRawWorkspacePreviewIdentity()const{return rawSourceIdentity;}
    std::uint64_t RawViewportSourceHash()const{return rawSourceHash;}
    bool IsPresentedRenderComplete()const;
    READINESS
    void Accept(){m_RenderPresentation.graphRequest=current;m_RenderPresentation.generation=m_ViewportOutputRenderGeneration;
        m_RenderPresentation.denoisePending=false;m_RenderPresentation.failed=false;pipeline.cache.Invalidate();}
    // Model an accepted packet's boundary data; adoption itself is not under test.
    void AttachRawSource(){m_RenderPresentation.sourceKey=rawSourceIdentity;m_RenderPresentation.sourceHash=rawSourceHash;
        m_RenderPresentation.recipeRevision=m_LatestRawPresentationRecipeRevision=4;
        m_LatestRawPresentationGeneration=m_RenderPresentation.generation;}
    void AcceptExplicit(){Accept();m_RenderPresentation.hasExplicitDocumentContext=true;
        m_RenderPresentation.explicitDocumentContext=current;m_RenderPresentation.graphRequest={};}
    void Complete(std::initializer_list<int> ids){
        project.graph.completed.clear();for(int id:ids){EditorNodeGraph::CompletedChainInfo c;c.outputNodeId=id;project.graph.completed.push_back(c);}
        current.outputNodeId=project.graph.ResolvePreviewOutputNodeId();
    }
    EditorModule(){current.enabled=true;current.revision=7;current.structureRevision=3;current.sourceIdentity=9;Complete({1});Accept();
        descriptions[1].descriptor.logicalType=Stack::NodeMath::LogicalValueType::ColorImage;
        descriptions[2].descriptor.logicalType=Stack::NodeMath::LogicalValueType::Channel;
        m_LastGraphOutputSemanticDescriptor=descriptions[1].descriptor;}
};
struct Card {int id=1;bool outputEnabled=true;};
void Draw(EditorModule* editor,Card node,bool outputUsesChannel=false) {
    auto& graph=editor->project.graph;
    struct Metrics {float itemGap=1;} metrics;
    const float uiScale=1,controlWidth=100;
    ImGui::text.clear();ImGui::disabled=false;ImGui::exportEnabled=false;
    CARD
}
void DrawContext(EditorModule* editor, Card card, bool outputUsesChannel=false) {
    Card* node=&card;
    ImGui::text.clear();ImGui::disabled=false;ImGui::exportEnabled=false;
    CONTEXT_EXPORT
}
int failures=0;
void Require(bool condition,const char* message){if(!condition){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
int main(){
    for(int scenario=0;scenario<9;++scenario){EditorModule e;Card card;
        switch(scenario){
            case 1:card.id=2;break;
            case 2:card.outputEnabled=false;break;
            case 3:e.Complete({1,2});break;
            case 4:e.rawProjection=true;break;
            case 5:e.composite=true;break;
            case 6:e.maskPreview=true;break;
            case 7:e.exportBusy=true;break;
        }
        const int opensBefore=FileDialogs::opens;
        ImGui::click=true;DrawContext(&e,card,scenario==8);ImGui::click=false;
        Require(ImGui::exportEnabled==(scenario==0),"context export enabled for wrong endpoint/mode");
        Require(e.exports==(scenario==0) && FileDialogs::opens-opensBefore==(scenario==0),
            "context menu opened export dialog or requested export for wrong endpoint/mode");
    }

    {EditorModule e;Draw(&e,{1,true});Require(ImGui::exportEnabled&&ImGui::Has("Connected to output chain"),"sole A export/connectivity");
        Require(ImGui::Has("Direct result: Image")&&e.lastDescriptorNode==1,"sole A endpoint descriptor");
        for(int frame=0;frame<100;++frame){Draw(&e,{1,true});}Require(e.pipeline.reads==1,"unchanged draws repeated stats readback");}
    {EditorModule e;ImGui::click=true;Draw(&e,{2,true});ImGui::click=false;
        Require(ImGui::Has("Output is not connected"),"disconnected B borrowed connected A");
        Require(!ImGui::exportEnabled&&e.exports==0,"disconnected B exported A");
        Require(e.pipeline.queries==0,"disconnected B queried A statistics");
        Require(e.lastDescriptorNode==2&&ImGui::Has("Direct result: Channel"),"B borrowed A descriptor");}
    {EditorModule e;ImGui::click=true;Draw(&e,{2,false},true);ImGui::click=false;
        Require(ImGui::Has("deactivated")&&!ImGui::exportEnabled&&e.exports==0,"disabled B command/status");
        Require(e.lastDescriptorNode==2&&ImGui::Has("Direct result: Channel")&&e.pipeline.queries==0,"disabled B presentation identity");}
    for(bool composite:{false,true}){EditorModule e;e.Complete({1,2});e.composite=composite;e.m_LastGraphOutputSemanticDescriptorIdentity.clear();
        for(int id:{1,2}){Draw(&e,{id,true});Require(ImGui::Has("Connected to output chain")&&!ImGui::exportEnabled,"multi-output status/command");
            Require(e.lastDescriptorNode==id&&ImGui::Has(id==1?"Direct result: Image":"Direct result: Channel"),"multi-output endpoint description");}
        Require(e.pipeline.queries==0,"multi-output borrowed main stats");}
    {EditorModule e;e.Complete({2});Draw(&e,{2,true});Require(e.pipeline.queries==0,"new output borrowed old accepted stats");
        e.Accept();Draw(&e,{2,true});Require(e.pipeline.queries==1,"current accepted output statistics hidden");
        ImGui::click=true;Draw(&e,{2,true},true);ImGui::click=false;Require(!ImGui::exportEnabled&&e.exports==0,"channel output export enabled");}
    for(int change=0;change<13;++change){EditorModule e;
        switch(change){case 0:++e.current.revision;break;case 1:++e.current.sourceIdentity;break;case 2:++e.current.structureRevision;break;
            case 3:++e.m_ViewportOutputRenderGeneration;break;case 4:e.m_RenderPresentation.denoisePending=true;break;
            case 5:e.m_RenderPresentation.failed=true;break;case 6:e.m_RenderDirty=true;break;case 7:e.m_RenderPending=true;break;
            case 8:e.busy=true;break;case 9:e.native=false;break;case 10:e.m_RenderPresentation.generation=0;break;
            case 11:e.current.inspectionNodeId=5;break;case 12:e.current.composite=true;break;}
        Draw(&e,{1,true});Require(e.pipeline.queries==0,"stale/pending/partial presentation statistics leaked");}
    // The current RAW identity/hash check applies to both Graph and explicit packets.
    for(bool explicitPacket:{false,true}){for(int change=0;change<5;++change){EditorModule e;
        if(explicitPacket)e.AcceptExplicit();
        e.AttachRawSource();Draw(&e,{1,true});Require(e.pipeline.queries==1,"matching RAW identity statistics hidden");
        switch(change){case 0:e.rawSourceIdentity="raw-source-B";break;case 1:++e.rawSourceHash;break;
            case 2:e.m_RenderPresentation.sourceHash=0;break;case 3:e.rawSourceHash=0;break;
            case 4:e.rawSourceIdentity.clear();break;}
        Draw(&e,{1,true});Require(e.pipeline.queries==1&&ImGui::Has("statistics unavailable"),"changed RAW identity/hash statistics leaked");}}
    // Explicit packets have a disabled emitted Graph tag but retain the accepted document fence.
    for(bool rawSource:{false,true}){for(int change=0;change<7;++change){EditorModule e;e.AcceptExplicit();
        if(rawSource)e.AttachRawSource();
        Draw(&e,{1,true});Require(e.pipeline.queries==1,"matching explicit document statistics hidden");
        switch(change){case 0:++e.current.revision;break;case 1:++e.current.sourceIdentity;break;
            case 2:++e.current.structureRevision;break;case 3:++e.current.outputNodeId;break;
            case 4:++e.current.inspectionNodeId;break;case 5:e.current.composite=true;break;case 6:e.current.enabled=false;break;}
        Draw(&e,{1,true});Require(e.pipeline.queries==1&&ImGui::Has("statistics unavailable"),"stale explicit document statistics leaked");}}
    for(bool rawSource:{false,true}){for(int change=0;change<2;++change){EditorModule e;e.current.enabled=false;e.AcceptExplicit();
        if(rawSource)e.AttachRawSource();
        Draw(&e,{1,true});Require(e.pipeline.queries==1,"matching disabled-Graph explicit document statistics hidden");
        if(change==0)++e.current.revision;else e.current.enabled=true;
        Draw(&e,{1,true});Require(e.pipeline.queries==1,"disabled-Graph explicit document fence lost");}}
    for(int change=0;change<3;++change){EditorModule e;e.AcceptExplicit();if(change<2)e.AttachRawSource();
        if(change==0)++e.m_LatestRawPresentationGeneration;
        else if(change==1)++e.m_LatestRawPresentationRecipeRevision;
        else ++e.m_RenderGeneration;
        Draw(&e,{1,true});Require(e.pipeline.queries==0,"outdated explicit presentation statistics leaked");}
    for(int mode=0;mode<3;++mode){EditorModule e;e.rawProjection=mode==0;e.composite=mode==1;e.maskPreview=mode==2;
        ImGui::click=true;Draw(&e,{1,true});ImGui::click=false;
        Require(!ImGui::exportEnabled&&e.exports==0&&e.pipeline.queries==0,"other graph/composite/inspection borrowed command or stats");}
    {EditorModule a,b;b.descriptions[1].descriptor.logicalType=Stack::NodeMath::LogicalValueType::Channel;
        Draw(&a,{1,true});Require(ImGui::Has("Direct result: Image"),"owner A descriptor");Draw(&b,{1,true});Require(ImGui::Has("Direct result: Channel"),"owner B same-node descriptor");}
    {EditorModule e;ImGui::click=true;Draw(&e,{1,true});ImGui::click=false;Require(e.exports==1,"sole image command lost");
        e.exportBusy=true;ImGui::click=true;Draw(&e,{1,true});ImGui::click=false;Require(e.exports==1,"busy export enabled");}
    if(failures)return 1;
    std::cout<<"Output card/context: per-node descriptors/connections, command target, accepted stats identity and unchanged draw checks passed.\n";
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--ui-source', type=Path, help='Optional pre-fix UI source for regression proof')
    parser.add_argument('--context-source', type=Path, help='Optional pre-fix context-menu source for regression proof')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    ui = (args.ui_source or root / 'src/Editor/NodeGraph/UI/EditorNodeGraphUINodes.cpp').read_text(encoding="utf-8")
    start = ui.index('        const auto outputCard =') if '        const auto outputCard =' in ui else ui.index('        if (!node.outputEnabled) {')
    end = ui.index('        if (ImGui::Button(node.outputEnabled ?', start)
    card = ui[start:end]
    context = (args.context_source or root / 'src/Editor/NodeGraph/UI/EditorNodeGraphUIContextMenu.cpp').read_text(encoding="utf-8")
    output_start = context.index('            if (node->kind == EditorNodeGraph::NodeKind::Output) {')
    export_end = context.index('                ImGui::EndDisabled();', output_start) + len('                ImGui::EndDisabled();')
    export_start = context.index('                ImGui::BeginDisabled(', output_start)
    if '                const auto outputCard =' in context[output_start:export_start]:
        export_start = context.index('                const auto outputCard =', output_start)
    context_export = context[export_start:export_end]
    header = (root / 'src/Editor/EditorModule.h').read_text(encoding="utf-8")
    readiness = block(header, 'bool IsRenderSettledForFullQualityExport() const')
    presentation = block((root / 'src/Editor/Internal/EditorModuleRenderPresentationState.cpp').read_text(encoding="utf-8"), 'bool EditorModule::IsPresentedRenderComplete() const')
    descriptor = block((root / 'src/Editor/Internal/EditorModuleGraphSnapshot.cpp').read_text(encoding="utf-8"), 'bool EditorModule::TryGetGraphOutputSemanticDescriptor(')
    resolver = block((root / 'src/Editor/NodeGraph/Model/EditorNodeGraphTraversal.cpp').read_text(encoding="utf-8"), 'int Graph::ResolvePreviewOutputNodeId() const').replace('Graph::', 'FixtureGraph::')
    connected = block((root / 'src/Editor/NodeGraph/Model/EditorNodeGraphMutation.cpp').read_text(encoding="utf-8"), 'bool Graph::IsOutputConnected() const').replace('Graph::', 'FixtureGraph::')
    text = PREAMBLE.replace('    READINESS', '    '+readiness).replace('    CARD', card).replace('    CONTEXT_EXPORT', context_export)
    # Definitions must precede main but can follow the class declarations.
    insertion = text.index('struct Card {')
    text = text[:insertion] + '\n'.join((presentation,descriptor,resolver,connected)) + '\n' + text[insertion:]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding="utf-8")


if __name__ == '__main__':
    main()
