#!/usr/bin/env python3
"""Generate a bounded graph pointer-lifetime component check.

The production entry cleanup, pan methods, socket start/drop branches and
marquee branches are copied unchanged and in their existing order. Unrelated
rendering, hit testing and graph edits are boundary doubles. This verifies the
retained state when an input edge is missed, not native event routing or pixels.

Run with --output <file.cpp>, then compile that file with a C++17 compiler.
No other source files, include directories, libraries, GPU or downloads are
required. The source dependencies are EditorNodeGraphUIInteraction.cpp,
EditorNodeGraphUIDragLayout.cpp, EditorNodeGraphUILinksAndGroups.cpp,
EditorNodeGraphUICamera.cpp and EditorNodeGraphUICanvas.cpp in
src/Editor/NodeGraph/UI/, plus src/Utils/ImGuiExtras.cpp,
src/Editor/Internal/EditorModuleView.cpp and src/Editor/EditorModuleTypes.h.
The node update method is copied whole; layout application is a boundary double.
Camera checks also copy the pan/zoom methods, easing and canvas advance block;
input and appearance services remain boundary doubles. The autofocus fixture
extracts the preparation failure/success handoff and actual autofocus state/math;
layout preparation is a boundary double.
Group rename checks copy the render prelude and interaction gate; missing-group
lookup is a boundary double. Native text input is not covered by this target.
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
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
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
struct IO { float DeltaTime=1.f/60; bool KeyCtrl=false,KeyAlt=false; ImVec2 MouseDelta; double MouseClickedTime[5]{}; };
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
    int layoutEnds=0;bool layoutCancelled=false;
    void EndGraphLayoutGesture(bool cancel){++layoutEnds;layoutCancelled=cancel;}
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
    bool m_ExpansionMotion=false,m_SmoothZoomActive=false;
    float m_Zoom=1.f,m_ZoomTarget=1.f;
    ImVec2 m_CanvasOrigin,m_SmoothZoomFocusScreen,m_SmoothZoomFocusGraph;
    void AdvanceZoom();
    void ZoomAtMouse(float);

    int m_MiddlePanSuppressDeltaFrames=0,m_MiddlePanLastUpdateFrame=-1;
    int m_DragGroupId=-1,m_ResizingGroupId=-1,m_DragOutputNodeId=-1,m_DragInputNodeId=-1;
    double m_LeftPointerGestureStartedAt=-1.,m_GroupPointerGestureStartedAt=-1.;
    int m_HoveredInputNodeId=-1,m_HoveredOutputNodeId=-1,m_NodeBrowserDragFromNodeId=-1,m_NodeBrowserDragToNodeId=-1;
    std::string m_DragOutputSocketId,m_DragInputSocketId,m_HoveredInputSocketId,m_HoveredOutputSocketId;
    std::string m_NodeBrowserDragFromSocketId,m_NodeBrowserDragToSocketId;
    ImVec2 m_MiddlePanAnchorScreenPos,m_MiddlePanRestoreScreenPos,m_Pan,m_BoxSelectStart,m_BoxSelectCurrent;
    int connections=0,browsers=0,wireFrames=0;
    void StopMiddlePanCapture();
    bool UpdateMiddlePanCapture(EditorModule*,bool,bool);
    void Step(EditorModule*,GraphMouseOwner,bool=true);
    void TouchNodeFront(int){}
    ImVec2 ScreenToGraph(const ImVec2& value) const;
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
int RunPointerTests() try {
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
    for(bool resize:{false,true})for(bool changedPress:{false,true}) {
        EditorModule editor;EditorNodeGraphUI ui;
        ImGui::Next(false,false);ImGui::Next(true,false);
        ui.m_GroupPointerGestureStartedAt=ImGui::GetIO().MouseClickedTime[0];
        if(resize)ui.m_ResizingGroupId=7;else ui.m_DragGroupId=7;
        if(changedPress){ImGui::Next(false,false);ImGui::Next(true,false);ImGui::Next(true,false);}
        ui.Step(&editor,Owner::None,false);
        Check(editor.layoutEnds==(changedPress?1:0) && !editor.layoutCancelled &&
            (resize?ui.m_ResizingGroupId:ui.m_DragGroupId)==(changedPress?-1:7),
            "Group dragging/resizing must retain only its original press");
    }
    if(failures)return 1;
    std::cout<<"Graph wire/marquee missed-release, pan takeover, owner hide/reentry and ordinary release checks passed. Component only.\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
'''

NODE_PREAMBLE = r'''
namespace NodeDragCheck {
using std::string;
struct Vec2 {float x=0,y=0;};
struct Graph {};
namespace Stack {namespace GraphLayout {struct PlacementEdit{};} namespace UiPerformance {enum class Counter{LayoutSolve};void Add(Counter){}}}
enum {ImGuiMouseButton_Left, ImGuiKey_Escape};
namespace ImGui {
struct IO {double MouseClickedTime[1]={1};Vec2 MouseDelta{10,10};}; IO io;
bool down=true,released=false; double now=1.1;
IO& GetIO(){return io;} bool IsMouseDown(int){return down;} bool IsMouseReleased(int){return released;}
double GetTime(){return now;} bool IsKeyPressed(int,bool){return false;}
Vec2 GetMouseDragDelta(int){return {10,10};} Vec2 GetMousePos(){return {100,100};}
}
enum class UiNotificationSeverity{Error};
struct EditorModule {int ends=0;bool cancelled=false;void EndGraphLayoutGesture(bool cancel){++ends;cancelled=cancel;} void ShowUiNotification(UiNotificationSeverity,const string&,const char*){}};
int moves=0;
struct Room {bool Move(Vec2,Stack::GraphLayout::PlacementEdit&,string&){++moves;return true;}};
struct Context {string documentId="A",graphId="graph";int documentRevision=1;};
struct State {string documentId="A",graphId="graph",lastError;int revision=1;double pressStartedAt=1,lastFrame=1;bool moved=true;Vec2 press;Room room;};
struct EditorNodeGraphUI {
std::unique_ptr<State> m_DragLayout=std::make_unique<State>(); Context m_GraphContext;
int m_DragNodeId=1,m_EditingGroupId=-1,m_DragGroupId=-1,m_ResizingGroupId=-1,m_DragOutputNodeId=-1,m_DragInputNodeId=-1;
bool m_MiddlePanCaptureActive=false;
bool HasDrawerOpen(){return false;} Vec2 ScreenToGraph(Vec2 p){return p;}
void EndDragLayout(EditorModule* e,bool cancel){m_DragLayout.reset();m_DragNodeId=-1;e->EndGraphLayoutGesture(cancel);}
bool ApplyAnimatedPlacement(EditorModule*,const Stack::GraphLayout::PlacementEdit&,bool,string&){return true;}
void UpdateActiveNodeDrag(EditorModule*,Graph&);

};

'''

NODE_TESTS = r'''
int Run(){
 Graph graph;int failures=0;
 const char* names[]={"new press", "missed release", "ordinary release", "held press", "stationary held press"};
 for(int scenario=0;scenario<5;++scenario){
  EditorModule e;EditorNodeGraphUI ui;moves=0;
  ImGui::io.MouseClickedTime[0]=scenario==0?2:1;
  ImGui::io.MouseDelta=scenario==4?Vec2{}:Vec2{10,10};
  ImGui::down=scenario==0 || scenario>=3;ImGui::released=scenario==2;
  ui.UpdateActiveNodeDrag(&e,graph);
  bool ok=moves==((scenario==2||scenario==3)?1:0) &&
      e.ends==(scenario>=3?0:1) && !e.cancelled;
  std::cout<<"Node "<<names[scenario]<<": "<<(ok?"PASS":"FAIL")<<" moves="<<moves<<"\n";failures+=!ok;
 }
 return failures?1:0;
}

} // namespace NodeDragCheck

'''


RENAME_PREAMBLE = r'''
namespace RenameGateCheck {
struct Graph {
    bool exists = true;
    const Graph* FindGroup(int id) const { return exists && id == 7 ? this : nullptr; }
};
struct EditorNodeGraphUI {
    enum class GraphMouseOwner { None, Popup };
    int m_EditingGroupId = 7;
    bool m_GraphInteractionBlocked = false, m_BoxSelecting = false;
    GraphMouseOwner m_MouseOwner = GraphMouseOwner::None;
    void StopMiddlePanCapture() {}
    void RenderPrelude(const Graph& graph);
    void Interaction();
};
constexpr int ImGuiKey_Escape = 1;
'''

RENAME_TESTS = r'''
int Run() {
    const int before = failures;
    Graph graph; EditorNodeGraphUI ui;
    ui.RenderPrelude(graph); ui.Interaction();
    Check(ui.m_EditingGroupId == 7 && ui.m_GraphInteractionBlocked,
        "A live group must retain its rename gate");
    // Hidden canvas frames omit both drawing and deactivation. Undo removes
    // the group before that same owner's canvas becomes visible again.
    graph.exists = false;
    ui.m_GraphInteractionBlocked = false;
    ui.RenderPrelude(graph); ui.Interaction();
    Check(ui.m_EditingGroupId < 0 && !ui.m_GraphInteractionBlocked,
        "A removed group's rename must not block the returning canvas");
    graph.exists = true;
    ui.RenderPrelude(graph); ui.Interaction();
    Check(ui.m_EditingGroupId < 0 && !ui.m_GraphInteractionBlocked,
        "A later group must not revive a cancelled rename");
    return failures != before;
}
} // namespace RenameGateCheck
int main(){return RunPointerTests() | RunPanZoomTests() | NodeDragCheck::Run() | DragAutoFocusCheck::Run() | RenameGateCheck::Run();}
'''


CAMERA_TESTS = r'''int RunPanZoomTests() {
    const int before=failures;
    EditorModule editor;
    for(int wheelOrder:{0,1,2}) {
        EditorNodeGraphUI ui;
        ImGui::popup=false;ImGui::io.MouseDelta={};ImGui::mouse={180,130};
        ImGui::Next(false,false);
        ui.ZoomAtMouse(2.f);ui.AdvanceZoom();
        Check(ui.m_SmoothZoomActive && ui.m_Zoom>1.f,"Wheel zoom fixture did not ease");
        // Plain wheel advances before a newly accepted pan; Ctrl-wheel fallback
        // can run after acceptance. Also exercise takeover before the advance.
        // None may snap or overwrite the pan.
        ImGui::Next(false,true);
        if(wheelOrder<2) ui.ZoomAtMouse(1.f);
        if(wheelOrder==0) ui.AdvanceZoom();
        const auto pressPan=ui.m_Pan;const float pressZoom=ui.m_Zoom;
        ui.UpdateMiddlePanCapture(&editor,true,true);
        if(wheelOrder==2)ui.ZoomAtMouse(1.f);
        ui.AdvanceZoom();
        Check(!ui.m_SmoothZoomActive && ui.m_ZoomTarget==pressZoom && ui.m_Zoom==pressZoom &&
            ui.m_Pan.x==pressPan.x && ui.m_Pan.y==pressPan.y,"Pan takeover snapped or retained stale zoom");
        ImGui::Next(false,true);ui.UpdateMiddlePanCapture(&editor,true,false); // Warp suppression.
        ImGui::Next(false,true);ImGui::io.MouseDelta={40,20};
        ui.UpdateMiddlePanCapture(&editor,false,false); // Capture continues outside canvas.
        ui.ZoomAtMouse(-2.f);ui.AdvanceZoom();
        Check(std::abs(ui.m_Pan.x-pressPan.x-22.f)<.001f &&
            std::abs(ui.m_Pan.y-pressPan.y-11.f)<.001f && ui.m_Zoom==pressZoom,
            "Wheel easing overwrote captured pan delta");
        ui.UpdateMiddlePanCapture(&editor,false,false);
        Check(std::abs(ui.m_Pan.x-pressPan.x-22.f)<.001f,"Repeated update doubled pan delta");
        ImGui::Next(false,false);ui.UpdateMiddlePanCapture(&editor,false,false);
        const auto focus=ui.ScreenToGraph(ImGui::mouse);const float zoom=ui.m_Zoom;
        ui.ZoomAtMouse(1.f);ui.AdvanceZoom();const auto after=ui.ScreenToGraph(ImGui::mouse);
        Check(ui.m_SmoothZoomActive && ui.m_Zoom>zoom &&
            std::abs(after.x-focus.x)<.001f && std::abs(after.y-focus.y)<.001f,
            "Fresh wheel after pan release lost easing or focus");
    }
    for(bool popup:{false,true}) {
        EditorNodeGraphUI ui;ImGui::Next(false,false);ImGui::popup=popup;
        ImGui::Next(false,true);ui.ZoomAtMouse(1.f);
        ui.UpdateMiddlePanCapture(&editor,!popup,false); // Another owner forbids starting.
        Check(!ui.m_MiddlePanCaptureActive && ui.m_SmoothZoomActive,"Blocked pan cancelled camera input");
        ImGui::popup=false;ui.UpdateMiddlePanCapture(&editor,true,true);
        ImGui::popup=true;ui.UpdateMiddlePanCapture(&editor,true,false);
        Check(!ui.m_MiddlePanCaptureActive && !ui.m_SmoothZoomActive,"Cancelled pan revived old zoom");
        ImGui::popup=false;ImGui::io.MouseDelta={};ImGui::Next(false,false);
    }
    return failures!=before;
}
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
    camera = (root / "src/Editor/NodeGraph/UI/EditorNodeGraphUICamera.cpp").read_text()
    canvas = (root / "src/Editor/NodeGraph/UI/EditorNodeGraphUICanvas.cpp").read_text()
    extras = (root / "src/Utils/ImGuiExtras.cpp").read_text()
    generated = PREAMBLE + "\nnamespace ImGuiExtras {\n" + block(extras, "float AnimateTowards(") + "\n}\n"
    generated += "bool GraphReducedMotion(){return false;}\n"
    generated += block(camera, "EditorNodeGraph::Vec2 EditorNodeGraphUI::ScreenToGraph(")
    generated += block(camera, "void EditorNodeGraphUI::ZoomAtMouse(")
    generated += "void EditorNodeGraphUI::AdvanceZoom(){const struct{bool interactive=true;}options;\n"
    generated += block(canvas, "    if (options.interactive && m_SmoothZoomActive) {") + "\n}\n"
    generated += block(source, "void EditorNodeGraphUI::StopMiddlePanCapture()")
    generated += "\n" + block(source, "bool EditorNodeGraphUI::UpdateMiddlePanCapture(")
    generated += "\nvoid EditorNodeGraphUI::Step(EditorModule* editor,GraphMouseOwner hit,bool graphHovered) {\n"
    generated += prelude + STEP_SETUP + branches[0] + STEP_HITS + "\n".join(branches[1:]) + "\n}\n" + TESTS + CAMERA_TESTS
    drag_source = (root / "src/Editor/NodeGraph/UI/EditorNodeGraphUIDragLayout.cpp").read_text(encoding="utf-8")
    generated += NODE_PREAMBLE + block(drag_source, "void EditorNodeGraphUI::UpdateActiveNodeDrag(") + NODE_TESTS
    view = (root / "src/Editor/Internal/EditorModuleView.cpp").read_text()
    types = (root / "src/Editor/EditorModuleTypes.h").read_text()
    focus = (root / "tools/tests/graph_drag_autofocus_fixture.cpp").read_text()
    focus = focus.replace('// PRODUCTION_AUTOFOCUS_STATE', block(types, 'struct GraphAutoFocusState {') + ';')
    constants = '\n'.join(line for line in view.splitlines() if line.startswith('constexpr ') and 'kGraphAutoFocus' in line)
    helpers = constants + '\n' + block(view, 'float ComputeGraphAutoFocusZoom(') + '\n' + block(view, 'void ComputeGraphAutoFocusPan(')
    helpers += '\nnamespace ImGuiExtras {\n' + block(extras, 'float EaseOutCubic(') + '\n}\n'
    focus = focus.replace('// PRODUCTION_AUTOFOCUS_HELPERS', helpers)
    focus = focus.replace('// PRODUCTION_AUTOFOCUS_METHODS', '\n'.join(block(view, 'void EditorModule::' + name + '(')
        for name in ('ApplyGraphAutoFocusFrame', 'RequestGraphNodeAutoFocus', 'CancelGraphAutoFocusTracking')))
    begin_drag = block(drag_source, 'bool EditorNodeGraphUI::BeginDragLayout(')
    handoff = begin_drag[begin_drag.index('    if(!drag->room.Begin('):begin_drag.rfind('}')]
    group_start = interaction.index('    // Intercept Canvas clicks')
    group_end = interaction.index('        if (ImGui::IsMouseDoubleClicked', group_start)
    group_handoff = interaction[group_start:group_end] + '    }\n'
    focus = focus.replace('// PRODUCTION_GROUP_HANDOFF', group_handoff)
    generated += focus.replace('// PRODUCTION_DRAG_HANDOFF', handoff)
    groups_source = (root / "src/Editor/NodeGraph/UI/EditorNodeGraphUILinksAndGroups.cpp").read_text(encoding="utf-8")
    render_groups = block(groups_source, "void EditorNodeGraphUI::RenderGroups(")
    rename_prelude = render_groups[render_groups.index("{") + 1:render_groups.index("    ImDrawList* drawList")]
    generated += RENAME_PREAMBLE
    generated += "\nvoid EditorNodeGraphUI::RenderPrelude(const Graph& graph) {\n" + rename_prelude + "\n}\n"
    generated += "void EditorNodeGraphUI::Interaction() {\n" + block(interaction, "    if (m_EditingGroupId > 0) {") + "\n}\n" + RENAME_TESTS
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generated, encoding="utf-8")


if __name__ == "__main__":
    main()
