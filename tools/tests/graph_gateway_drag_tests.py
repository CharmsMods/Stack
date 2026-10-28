#!/usr/bin/env python3
"""Check pending-wire and hover-ring geometry using whole production draw methods.

Copies RenderPendingOutputLinkDrag and RenderPendingInputLinkDrag unchanged.
ImGui drawing, node socket positions and connection eligibility are explicit
boundary doubles. This checks endpoint selection, not native input delivery,
full wire tessellation, GPU rendering, or actual graph connection validation.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path)
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (root / 'src/Editor/NodeGraph/UI/EditorNodeGraphUILinksAndGroups.cpp').read_text()
    start = source.index('void EditorNodeGraphUI::RenderPendingOutputLinkDrag(')
    methods = source[start:source.index('void EditorNodeGraphUI::RenderGroups(', start)]
    generated = r'''
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
struct ImVec2 { float x=0,y=0; ImVec2()=default; ImVec2(float x_,float y_):x(x_),y(y_){} };
using ImU32=unsigned;
#define IM_COL32(...) 0u
struct ImDrawList {
    std::vector<ImVec2> rings, filled;
    template<class...T> void AddCircle(ImVec2 p,T...){rings.push_back(p);}
    template<class...T> void AddCircleFilled(ImVec2 p,T...){filled.push_back(p);}
};
namespace ImGui {
ImDrawList draw;
ImVec2 mouse{900,700};
ImDrawList* GetWindowDrawList(){return &draw;}
ImVec2 GetMousePos(){return mouse;}
double GetTime(){return 0;}
}
namespace StackAppearance {
struct AppearanceManager {bool GetSeamlessSurfaceStylingEnabled()const{return false;}};
}
struct EditorModule {const StackAppearance::AppearanceManager* GetAppearance()const{return nullptr;}};
namespace EditorNodeGraph {
struct Node {int id;};
struct Graph {
    std::vector<Node> nodes{{1},{2}};
    const Node* FindNode(int id)const{for(const auto& n:nodes)if(n.id==id)return &n;return nullptr;}
};
}
struct SocketHit {int nodeId=0;std::string socketId;bool IsValid()const{return nodeId>0&&!socketId.empty();}};
struct LinkVisualStyle {bool dotted=false;};
struct GraphStyleTokens {bool enabled=false;int selected=0;};
ImVec2 ToImVec2(ImVec2 p){return p;}
bool GraphReducedMotion(){return true;}
bool GraphDottedMaskLinksEnabled(EditorModule*){return false;}
bool GraphStraightLinksEnabled(EditorModule*){return false;}
GraphStyleTokens BuildGraphStyleTokens(EditorModule*){return {};}
float ResolveLinkHandle(ImVec2,ImVec2,bool){return 40;}
unsigned ColorToU32(unsigned c){return c;}
unsigned LinkColorVec(LinkVisualStyle,GraphStyleTokens){return 0;}
unsigned LinkColorClassic(LinkVisualStyle,bool){return 0;}
unsigned ColorWithAlpha(int,float){return 0;}
unsigned ApplyStyleAlpha(unsigned c){return c;}
float NodeUiScaleFromZoom(float){return 1;}
struct Stroke {ImVec2 first,last;};
std::vector<Stroke> strokes;
template<class...T> void DrawLinkStrokeWithEdgeFade(ImDrawList*,ImVec2 first,ImVec2,ImVec2,ImVec2 last,T...){strokes.push_back({first,last});}
struct EditorNodeGraphUI {
    struct Row {int source=1,target=2;std::string sourceSocket="out",socket="in";ImVec2 input{400,200},output{560,200};};
    int m_DragOutputNodeId=1,m_DragInputNodeId=2;
    std::string m_DragOutputSocketId="out",m_DragInputSocketId="in";
    int m_CompoundEntryDragInput=-1,m_CompoundEntryDragOutput=-1;
    std::vector<Row> m_CompoundEntryRows{Row{}};
    ImVec2 m_CanvasMin{0,0},m_CanvasMax{1000,800};
    float m_Zoom=1;
    bool connectable=true;
    template<class...T> bool CanPreviewConnection(T...)const{return connectable;}
    template<class...T> LinkVisualStyle ResolveLinkVisualStyle(T...)const{return {};}
    ImVec2 OutputPinScreenPos(const EditorNodeGraph::Node&,const std::string&)const{return {100,100};}
    ImVec2 InputPinScreenPos(const EditorNodeGraph::Node&,const std::string&)const{return {700,400};}
    float NodePinRadius()const{return 4;}
    void RenderPendingOutputLinkDrag(EditorModule*,const EditorNodeGraph::Graph&,const SocketHit&);
    void RenderPendingInputLinkDrag(EditorModule*,const EditorNodeGraph::Graph&,const SocketHit&);
};
''' + methods + r'''
void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
bool same(ImVec2 a,ImVec2 b){return a.x==b.x&&a.y==b.y;}
void run(bool backwards,int rowIndex,bool allowed,bool mismatchNode,bool mismatchSocket) {
    EditorNodeGraphUI ui;EditorNodeGraph::Graph graph;EditorModule editor;
    ui.connectable=allowed;
    auto& row=ui.m_CompoundEntryRows[0];
    if(backwards){ui.m_CompoundEntryDragOutput=rowIndex;}
    else{ui.m_CompoundEntryDragInput=rowIndex;}
    if(mismatchNode){row.source=3;row.target=3;}
    if(mismatchSocket){row.sourceSocket="other";row.socket="other";}
    ImGui::draw={};strokes.clear();
    if(backwards)ui.RenderPendingInputLinkDrag(&editor,graph,{1,"out"});
    else ui.RenderPendingOutputLinkDrag(&editor,graph,{2,"in"});
    const bool gateway=rowIndex==0&&!mismatchNode&&!mismatchSocket;
    const auto expected= !allowed ? ImGui::mouse : gateway ? (backwards?row.output:row.input) :
        backwards ? ImVec2{100,100} : ImVec2{700,400};
    check(strokes.size()==1,"one pending stroke");
    check(same(backwards?strokes[0].first:strokes[0].last,expected),"pending wire must end at hovered visual socket");
    check(same(backwards?strokes[0].last:strokes[0].first,backwards?ImVec2{700,400}:ImVec2{100,100}),"drag origin remains unchanged");
    check(ImGui::draw.rings.size()==(allowed?1u:0u),"only compatible socket has hover ring");
    if(allowed)check(same(ImGui::draw.rings[0],expected),"hover ring must match pending wire endpoint");
}
int main(){int failures=0;
    for(bool backwards:{false,true}) {try{
        run(backwards,-1,true,false,false); // Ordinary socket.
        run(backwards,99,true,false,false); // Stale row index.
        run(backwards,0,true,true,false); // Unrelated node.
        run(backwards,0,true,false,true); // Other socket of same node.
        run(backwards,0,false,false,false); // Rejected gateway hover follows cursor.
        run(backwards,0,true,false,false); // Matching gateway wire + ring.
    }catch(const std::exception& e){++failures;std::cerr<<(backwards?"input drag: ":"output drag: ")<<e.what()<<'\n';}}
    if(failures)return 1;
    std::cout<<"gateway pending drag endpoints and rings passed\n";
    return 0;
}
'''
    if args.output:
        args.output.write_text(generated)
    else:
        with tempfile.TemporaryDirectory(prefix='stack-gateway-drag-') as directory:
            cpp = Path(directory) / 'test.cpp'
            binary = Path(directory) / 'test'
            cpp.write_text(generated)
            subprocess.run([args.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
