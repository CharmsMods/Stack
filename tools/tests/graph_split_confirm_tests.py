#!/usr/bin/env python3
"""Real ImGui input with production split-confirm drawing, gating and command eligibility.

Graph selection/connection validity and split operations are named boundary doubles.
No graph documents are changed. --source accepts pre-fix interaction source.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def block(source, marker):
    start = source.index(marker)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--imgui', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (args.source or root/'src/Editor/NodeGraph/UI/EditorNodeGraphUIInteraction.cpp').read_text()
    editor = (root/'src/Editor/EditorModule.cpp').read_text()
    plain_start = source.index('    const bool plainGPressed =')
    plain = source[plain_start:source.index('    const auto& selectedNodeIds', plain_start)]
    generated = r'''
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace Stack::Workspace {bool preview=false;bool IsPreview(){return preview;}}
namespace EditorNodeGraph {
enum class NodeKind {Layer,DataMath};
enum class DataMathMode {ImageAverage};
struct Node {NodeKind kind=NodeKind::Layer;DataMathMode dataMathMode=DataMathMode::ImageAverage;};
struct Graph {Node node;bool present=true;std::vector<int> ids{1};
    const Node* FindNode(int id)const{return present&&id==1?&node:nullptr;}};
}
struct EditorModule {
    EditorNodeGraph::Graph graph;int splits=0;bool m_GraphCaptureWindowOpen=false;
    auto& GetNodeGraph(){return graph;}
    bool CanConsumeEditorCommandKeys()const;
    bool SplitLayerNodeIntoChannels(int){++splits;return true;}
    bool SplitImageAverageNodeIntoChannelAverages(int){++splits;return true;}
};
''' + block(editor, 'bool EditorModule::CanConsumeEditorCommandKeys() const') + r'''
enum class UiNotificationSeverity {Success,Error};
template<class...T>void PostNodeGraphNotification(T...){}
struct Rect {
    ImVec2 min{},max{};
    bool IsValid()const{return max.x>min.x;}
    bool Contains(ImVec2 p)const{return p.x>=min.x&&p.y>=min.y&&p.x<=max.x&&p.y<=max.y;}
};
struct EditorNodeGraphUI {
    int m_ChannelSplitConfirmNodeId=1;double m_ChannelSplitConfirmStartTime=0;
    Rect m_ChannelSplitConfirmRect;ImVec2 m_CanvasMin{0,0},m_CanvasMax{600,400};
    bool blocked=false,valid=true;
    void CancelChannelSplitConfirm(){m_ChannelSplitConfirmNodeId=-1;}
    bool CanOpenChannelSplitConfirm(const EditorNodeGraph::Graph& graph,int id){return valid&&graph.FindNode(id);}
    void RenderChannelSplitConfirmPrompt(EditorModule*);
    void Step(EditorModule* editor){
        auto& graph=editor->graph;
        const bool graphWindowFocused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const bool anyPopupOpen=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);
        const bool graphHotkeysBlockedByNodeControls=blocked;
''' + plain + r'''
        const auto& selectedNodeIds=graph.ids;
''' + block(source, 'if (m_ChannelSplitConfirmNodeId > 0) {') + r'''
    }
};
''' + block(source, 'void EditorNodeGraphUI::RenderChannelSplitConfirmPrompt(') + r'''
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Run(int scenario){
    ImGui::CreateContext();
    struct Cleanup{~Cleanup(){ImGui::DestroyContext();Stack::Workspace::preview=false;}}cleanup;
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={600,400};io.DeltaTime=1.f/60;
    unsigned char*p;int w,h;io.Fonts->GetTexDataAsRGBA32(&p,&w,&h);
    EditorModule editor;EditorNodeGraphUI ui;char text[32]="1";bool committed=false,modal=false;
    auto frame=[&]{
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({600,400});
        ImGui::Begin("Graph",nullptr,ImGuiWindowFlags_NoDecoration);
        ImGui::SetCursorScreenPos({210,45});ImGui::SetNextItemWidth(150);
        committed=ImGui::InputText("Value",text,sizeof(text),ImGuiInputTextFlags_EnterReturnsTrue);
        ui.blocked=ImGui::IsItemActive()||ImGui::IsItemEdited()||scenario==5;
        if(modal)ImGui::OpenPopup("Modal");
        ui.Step(&editor);ui.RenderChannelSplitConfirmPrompt(&editor);
        if(ImGui::BeginPopupModal("Modal")){ImGui::TextUnformatted("Blocked");ImGui::EndPopup();}
        ImGui::End();ImGui::Render();
    };
    frame();frame();
    if(scenario==0){
        // The actual foreground prompt is not a blocking window: the node's
        // input underneath can receive this click without outside cancellation.
        io.AddMousePosEvent(230,50);frame();io.AddMouseButtonEvent(0,true);frame();
        io.AddMouseButtonEvent(0,false);frame();
        Check(ui.m_ChannelSplitConfirmNodeId==1&&ImGui::GetActiveID()!=0,"Prompt/input overlap not reached");
        io.AddInputCharactersUTF8("9");frame();
    }
    if(scenario==4)io.AddKeyEvent(ImGuiMod_Alt,true);
    if(scenario==6)modal=true;
    if(scenario==7)editor.graph.ids={2};
    if(scenario==8)editor.graph.present=false;
    if(scenario==9)ui.valid=false;
    if(scenario==10)Stack::Workspace::preview=true;
    if(scenario==11)editor.m_GraphCaptureWindowOpen=true;
    const auto key=scenario==2?ImGuiKey_G:scenario==3?ImGuiKey_Escape:ImGuiKey_Enter;
    io.AddKeyEvent(key,true);frame();
    if(scenario==0)Check(committed&&io.WantTextInput,"Input did not consume Enter");
    Check(editor.splits==((scenario==1||scenario==2)?1:0),"Split confirmation stole an ineligible key");
    if(scenario==3||(scenario>=6&&scenario<=9))Check(ui.m_ChannelSplitConfirmNodeId<0,"Invalid confirmation was not cancelled");
    io.AddKeyEvent(key,false);frame();
    Check(editor.splits==((scenario==1||scenario==2)?1:0),"Released key replayed confirmation");
}
int main(){try{for(int scenario=0;scenario<12;++scenario)Run(scenario);
    std::cout<<"Graph split confirmation input ownership passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
'''
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(generated)
        return
    if not args.imgui:
        parser.error('--imgui is required without --output')
    with tempfile.TemporaryDirectory(prefix='stack-split-confirm-') as directory:
        cpp = Path(directory)/'test.cpp'
        exe = Path(directory)/'test'
        cpp.write_text(generated)
        subprocess.run(['g++', '-std=c++17', '-O2', '-DNDEBUG', '-I'+str(args.imgui), str(cpp),
                        *[str(args.imgui/(name+'.cpp')) for name in ('imgui','imgui_draw','imgui_tables','imgui_widgets')],
                        '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
