#!/usr/bin/env python3
"""Generate a bounded graph pointer-lifetime component check.

The production entry cleanup, pan methods, socket start/drop branches and
marquee branches are copied unchanged and in their existing order. Unrelated
rendering, hit testing and graph edits are boundary doubles. This verifies the
retained state when an input edge is missed, not native event routing or pixels.

Run with --output <file.cpp>, then compile that file with a C++17 compiler.
No other source files, include directories, libraries, GPU or downloads are
required. The only source dependency is
src/Editor/NodeGraph/UI/EditorNodeGraphUIInteraction.cpp.
"""
import argparse
from pathlib import Path


def block(source, marker):
    if source.count(marker) != 1:
        raise ValueError(f"Expected one production block: {marker}")
    start = source.index(marker)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


PREAMBLE = r'''
#include <cmath>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
struct ImVec2 { float x=0,y=0; ImVec2()=default; ImVec2(float a,float b):x(a),y(b){} };
enum { ImGuiMouseButton_Left=0, ImGuiMouseButton_Right=1, ImGuiMouseButton_Middle=2,
       ImGuiPopupFlags_AnyPopupId=0, ImGuiKey_G=0 };
#define IM_COL32(...) 0
struct ImDrawList { template<class...T> void AddRect(T...){} template<class...T> void AddRectFilled(T...){} };
namespace ImGui {
struct IO { bool KeyCtrl=false,KeyAlt=false; ImVec2 MouseDelta; double MouseClickedTime[5]{}; };
inline IO io;
inline bool down[3]{},clicked[3]{},released[3]{},popup=false;
inline int frame=0;
inline ImVec2 mouse{20,20};
inline ImDrawList draw;
inline bool IsMouseDown(int button){return down[button];}
inline bool IsMouseClicked(int button){return clicked[button];}
inline bool IsMouseReleased(int button){return released[button];}
inline bool IsPopupOpen(const char*,int){return popup;}
inline bool IsKeyPressed(int,bool){return false;}
inline IO& GetIO(){return io;}
inline int GetFrameCount(){return frame;}
inline ImVec2 GetMousePos(){return mouse;}
inline ImDrawList* GetWindowDrawList(){return &draw;}
void Next(bool left, bool middle) {
    ++frame;
    for(int button:{0,2}) {
        const bool next=button==0?left:middle;
        clicked[button]=next&&!down[button]; released[button]=!next&&down[button]; down[button]=next;
        if(clicked[button])io.MouseClickedTime[button]=double(frame)/60.;
    }
}
}
namespace StackAppearance { struct AppearanceManager { float GetGraphPanSensitivity()const{return .55f;} }; }
namespace ImGuiExtras {
enum class CursorCaptureMode { LockedPan };
struct CursorCaptureRequest { CursorCaptureMode mode; ImVec2 anchorScreenPos,restoreScreenPos; };
inline void SubmitCursorCaptureRequest(const CursorCaptureRequest&){}
}
namespace EditorNodeGraph {
using Vec2=ImVec2;
struct Node {};
struct GraphRect { ImVec2 min,max; };
struct Graph {
    int selections=0;
    void ClearSelection(){}
    template<class F> void SelectNodesInBounds(ImVec2,ImVec2,F,bool){++selections;}
};
}
inline ImVec2 ToImVec2(ImVec2 v){return v;}
inline ImVec2 ToGraphVec2(ImVec2 v){return v;}
enum class UiNotificationSeverity { Error };
struct EditorModule {
    EditorNodeGraph::Graph graph;
    EditorNodeGraph::Graph& GetNodeGraph(){return graph;}
    void EndGraphLayoutGesture(bool){}
    void CancelGraphAutoFocusTracking(){}
    const StackAppearance::AppearanceManager* GetAppearance(){return nullptr;}
};
inline void PostNodeGraphNotification(EditorModule*,UiNotificationSeverity,const std::string&,const char*){}
class EditorNodeGraphUI {
public:
    enum class GraphMouseOwner { None,Canvas,OutputPin,InputPin,NodeHeader };
    enum class NodeBrowserMode { ConnectFromOutput,ConnectFromInput };
    struct SocketHit {int nodeId=-1;std::string socketId;bool IsValid()const{return nodeId>0;}};
    GraphMouseOwner m_MouseOwner=GraphMouseOwner::None;
    bool m_MiddlePanCaptureActive=false,m_GraphInteractionBlocked=false,m_BoxSelecting=false,m_DebugInteractionOverlay=false;
    bool m_ExpansionMotion=false;
    int m_MiddlePanSuppressDeltaFrames=0,m_MiddlePanLastUpdateFrame=-1;
    int m_DragGroupId=-1,m_ResizingGroupId=-1,m_DragOutputNodeId=-1,m_DragInputNodeId=-1;
    double m_LeftPointerGestureStartedAt=-1.;
    int m_HoveredInputNodeId=-1,m_HoveredOutputNodeId=-1,m_NodeBrowserDragFromNodeId=-1,m_NodeBrowserDragToNodeId=-1;
    std::string m_DragOutputSocketId,m_DragInputSocketId,m_HoveredInputSocketId,m_HoveredOutputSocketId;
    std::string m_NodeBrowserDragFromSocketId,m_NodeBrowserDragToSocketId;
    ImVec2 m_MiddlePanAnchorScreenPos,m_MiddlePanRestoreScreenPos,m_Pan,m_BoxSelectStart,m_BoxSelectCurrent;
    int connections=0,browsers=0,wireFrames=0;
    void StopMiddlePanCapture();
    bool UpdateMiddlePanCapture(EditorModule*,bool,bool);
    void Step(EditorModule*,GraphMouseOwner,bool=true);
    void TouchNodeFront(int){}
    ImVec2 ScreenToGraph(ImVec2 value){return value;}
    ImVec2 GraphToScreen(ImVec2 value){return value;}
    ImVec2 NodeSize(const EditorNodeGraph::Node&){return {100,100};}
    ImVec2 DisplayNodePosition(const EditorNodeGraph::Node&){return {};}
    void RenderPendingOutputLinkDrag(EditorModule*,const EditorNodeGraph::Graph&,const SocketHit&){++wireFrames;}
    void RenderPendingInputLinkDrag(EditorModule*,const EditorNodeGraph::Graph&,const SocketHit&){++wireFrames;}
    bool ConnectWithLayout(EditorModule*,int,const std::string&,int,const std::string&,std::string*){++connections;return true;}
    bool ConnectOutputToBestInput(EditorModule*,int,const std::string&,int){++connections;return true;}
    bool ConnectBestOutputToInput(EditorModule*,int,int,const std::string&){++connections;return true;}
    void OpenNodeBrowser(NodeBrowserMode,ImVec2){++browsers;}
};
'''

STEP_SETUP = r'''
    const auto& graph=editor->GetNodeGraph();
    const bool graphWindowFocused=true,ownerIsContent=false,additiveSelect=false;
    const SocketHit hoveredInput=hit==GraphMouseOwner::InputPin?SocketHit{2,"in"}:SocketHit{};
    const SocketHit hoveredOutput=hit==GraphMouseOwner::OutputPin?SocketHit{1,"out"}:SocketHit{};
    const int hoveredNodeId=hit==GraphMouseOwner::NodeHeader?3:-1;
'''

STEP_HITS = r'''
    m_MouseOwner=hit;
    m_HoveredInputNodeId=hoveredInput.nodeId;m_HoveredInputSocketId=hoveredInput.socketId;
    m_HoveredOutputNodeId=hoveredOutput.nodeId;m_HoveredOutputSocketId=hoveredOutput.socketId;
'''

TESTS = r'''
int failures=0;
void Check(bool ok,const char* message){if(!ok){++failures;std::cerr<<message<<'\n';}}
int main() try {
    using Owner=EditorNodeGraphUI::GraphMouseOwner;
    for(bool output:{true,false})for(int interrupted=0;interrupted<4;++interrupted) {
        EditorModule editor;EditorNodeGraphUI ui;
        const auto origin=output?Owner::OutputPin:Owner::InputPin;
        const auto destination=output?Owner::InputPin:Owner::OutputPin;
        ImGui::Next(false,false);ui.Step(&editor,Owner::Canvas);
        ImGui::Next(true,false);ui.Step(&editor,origin);
        ui.Step(&editor,Owner::None,false); // A second draw of the same input frame.
        Check(ui.m_DragOutputNodeId>0 || ui.m_DragInputNodeId>0,
            "Repeating the press frame must preserve the same wire gesture");
        ImGui::Next(true,false);ui.Step(&editor,Owner::Canvas);
        Check(ui.wireFrames>=2,"A held wire must remain active across frames");
        if(interrupted>0) {
            // Retain this owner's UI while another workspace receives release.
            ImGui::Next(false,false);
            ImGui::Next(false,false);
            if(interrupted>=2)ImGui::Next(true,false);
            if(interrupted==3)ImGui::Next(true,false); // New press edge was hidden too.
        } else {
            ImGui::Next(true,true);ui.Step(&editor,Owner::Canvas);
            Check(ui.m_MiddlePanCaptureActive,"Fixture did not start production pan capture");
            ImGui::Next(false,true);ui.Step(&editor,Owner::Canvas);
            ImGui::Next(false,true);ui.Step(&editor,Owner::Canvas);
            ImGui::Next(false,false);
        }
        ui.Step(&editor,Owner::None,false); // Project reentry can precede release.
        Check(ui.m_DragOutputNodeId<0 && ui.m_DragInputNodeId<0 &&
            ui.m_DragOutputSocketId.empty() && ui.m_DragInputSocketId.empty(),
            "A missed release revived a stale wire");
        ImGui::Next(true,false);ui.Step(&editor,Owner::NodeHeader);
        ImGui::Next(false,false);ui.Step(&editor,Owner::NodeHeader);
        Check(editor.graph.selections==0 && ui.connections==0 && ui.browsers==0,
            "An unrelated later click completed the abandoned gesture");
        const int previousConnections=ui.connections,previousBrowsers=ui.browsers;
        ImGui::Next(true,false);ui.Step(&editor,origin);
        ImGui::Next(true,false);ui.Step(&editor,destination);
        ImGui::Next(false,false);ui.Step(&editor,destination);
        Check(ui.connections==previousConnections+1 && ui.browsers==previousBrowsers,
            "Normal release must still connect");
        ImGui::Next(true,false);ui.Step(&editor,origin);
        ImGui::Next(false,false);ui.Step(&editor,Owner::Canvas);
        Check(ui.connections==previousConnections+1 && ui.browsers==previousBrowsers+1,
            "Normal canvas release must still open its browser");
    }
    for(int reentry=0;reentry<3;++reentry) {
        EditorModule editor;EditorNodeGraphUI ui;
        ImGui::Next(false,false);ui.Step(&editor,Owner::Canvas);
        ImGui::Next(true,false);ui.Step(&editor,Owner::Canvas);
        Check(ui.m_BoxSelecting,"A canvas press must start marquee selection");
        ui.Step(&editor,Owner::NodeHeader);
        Check(ui.m_BoxSelecting,"Repeating the press frame must preserve the same marquee");
        ImGui::Next(true,false);ui.Step(&editor,Owner::NodeHeader);
        Check(ui.m_BoxSelecting,"Held marquee must survive over a node");
        ImGui::Next(false,false); // This owner is not drawn during release.
        ImGui::Next(false,false);
        if(reentry>=1)ImGui::Next(true,false);
        if(reentry==2)ImGui::Next(true,false);
        ui.Step(&editor,Owner::None,false);
        Check(!ui.m_BoxSelecting,"A hidden owner's missed release revived its marquee");
        ImGui::Next(true,false);ui.Step(&editor,Owner::NodeHeader);
        ImGui::Next(false,false);ui.Step(&editor,Owner::NodeHeader);
        Check(editor.graph.selections==0,"A later node click completed an abandoned marquee");
        const int previousSelections=editor.graph.selections;
        ImGui::Next(true,false);ui.Step(&editor,Owner::Canvas);
        ImGui::Next(false,false);ui.Step(&editor,Owner::NodeHeader);
        Check(editor.graph.selections==previousSelections+1 && !ui.m_BoxSelecting,
            "Normal marquee release must still select");
    }
    if(failures)return 1;
    std::cout<<"Graph wire/marquee missed-release, pan takeover, owner hide/reentry and ordinary release checks passed. Component only.\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (root / "src/Editor/NodeGraph/UI/EditorNodeGraphUIInteraction.cpp").read_text(encoding="utf-8")
    interaction = block(source, "void EditorNodeGraphUI::RenderInteraction(")
    prelude = interaction[interaction.index("{") + 1:interaction.index("    if ((m_NodeNotes.Revealed()")]
    markers = [
        "    if (m_MiddlePanCaptureActive) {",
        "    if (UpdateMiddlePanCapture(editor, graphHovered && !ownerIsContent, true)) {",
        "    if (m_MouseOwner == GraphMouseOwner::OutputPin && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {",
        "    if (m_MouseOwner == GraphMouseOwner::InputPin && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {",
        "    if (m_DragOutputNodeId > 0) {",
        "    if (m_DragInputNodeId > 0) {",
        "    if (m_MouseOwner == GraphMouseOwner::Canvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {",
        "    if (m_BoxSelecting) {",
    ]
    offsets = [interaction.index(marker) for marker in markers]
    if offsets != sorted(offsets):
        raise ValueError("Production pointer branch order changed; review component extraction")
    branches = [block(interaction, marker) for marker in markers]
    generated = PREAMBLE + "\n" + block(source, "void EditorNodeGraphUI::StopMiddlePanCapture()")
    generated += "\n" + block(source, "bool EditorNodeGraphUI::UpdateMiddlePanCapture(")
    generated += "\nvoid EditorNodeGraphUI::Step(EditorModule* editor,GraphMouseOwner hit,bool graphHovered) {\n"
    generated += prelude + STEP_SETUP + branches[0] + STEP_HITS + "\n".join(branches[1:]) + "\n}\n" + TESTS
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generated, encoding="utf-8")


if __name__ == "__main__":
    main()
