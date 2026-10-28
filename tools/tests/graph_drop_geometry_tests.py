#!/usr/bin/env python3
"""Bounded CPU regression for canvas catalog-drop timing.

Runs the production acceptance/delivery blocks and coordinate conversion with
small ImGui, graph-edit and layout stand-ins. Covers delivery while the canvas
origin/camera changes, not native OS drag delivery or full graph processing.
Use --canvas with an older source file for the negative control.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def block(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--canvas', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    ui = root / 'src/Editor/NodeGraph/UI'
    source = (args.canvas or ui / 'EditorNodeGraphUICanvas.cpp').read_text()
    capture = block(source, 'if (options.allowDropTarget && ImGui::BeginDragDropTarget())')
    late = 'if (deliveredEntry)' in source
    delivery = block(source, 'if (deliveredEntry)') if late else ''
    # Preserve the real preparation/delivery ordering, before node widgets can
    # queue commands or retain node pointers. The old acceptance block mutates
    # immediately, before the current camera is installed in the harness.
    if late:
        prepare = block(source, 'const auto prepareNodeProjection =') + ';'
        assert source.index('m_CanvasOrigin = ToGraphVec2(canvasMin)', source.index('void EditorNodeGraphUI::RenderGraphCanvas')) < source.index('const auto prepareNodeProjection =')
        assert source.index('prepareNodeProjection();') < source.index('if (deliveredEntry)') < source.index('const std::vector<int>& nodeRenderOrder')
    else:
        prepare = 'const auto prepareNodeProjection = [&] { ++preparations; projected = true; };'
    camera = (ui / 'EditorNodeGraphUICamera.cpp').read_text()
    conversion = block(camera, 'EditorNodeGraph::Vec2 EditorNodeGraphUI::ScreenToGraph(').replace('EditorNodeGraphUI::', '')
    generated = r'''
#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
struct ImVec2 { float x=0,y=0; };
namespace EditorNodeGraph { using Vec2=ImVec2; struct Link { int fromNodeId=0,toNodeId=0; }; }
struct NodeBrowserEntry { std::string label; int value=0; };
struct ImGuiPayload { void* Data=nullptr; int DataSize=0; bool delivery=true; bool IsDelivery() const {return delivery;} };
namespace ImGui {
struct IO { ImVec2 MousePos; } io;
ImGuiPayload payload;
bool BeginDragDropTarget(){return true;}
const ImGuiPayload* AcceptDragDropPayload(const char*){return &payload;}
void EndDragDropTarget(){}
IO& GetIO(){return io;}
}
namespace Stack::UiPerformance {
enum class Stage { LogicalLayout, Projection };
struct Span { explicit Span(Stage) {} };
}
struct Node {int id=1; ImVec2 position;};
struct Graph {std::vector<Node> nodes{{}}; auto& GetNodes(){return nodes;}};
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Fixture {
ImVec2 m_CanvasOrigin{0,0},m_Pan{0,0}; float m_Zoom=1;
bool m_DragLayout=false,m_ExpansionMotion=false,projected=false,gateways=false;
std::vector<int> m_LinkLabelHitRects{1}; int m_LinkHitTestFrame=99;
int m_NodeLayoutCache[8]{},preparations=0,adds=0,inserts=0,nextId=3,pickedFrom=0,pickedTo=0;
bool onWire=true,insertSucceeds=true;
ImVec2 insertedAt; std::string insertedLabel;
Graph graph; void* editor=nullptr;
static ImVec2 ToGraphVec2(ImVec2 p){return p;}
static ImVec2 ClampGraphPosition(ImVec2 p){return p;}
void RefreshLogicalNodeLayoutCache(Graph&,Node&,int&){}
void ClampPanToContent(Graph&){++preparations;}
void ProjectNodeLayoutCache(const Node&,int&){projected=true;}
void PrepareCompoundEntries(Graph&){require(projected,"gateway before current projection");gateways=true;}
void PrepareWireRoutes(Graph&){require(gateways,"routing before gateway preparation");}
EditorNodeGraph::Link FindLinkAt(Graph&,ImVec2){if(!m_LinkLabelHitRects.empty()||m_LinkHitTestFrame>=0)return {8,9};return onWire&&gateways?EditorNodeGraph::Link{1,2}:EditorNodeGraph::Link{};}
int AddNodeFromBrowserEntry(void*,const NodeBrowserEntry& entry,ImVec2 p){++adds;insertedAt=p;insertedLabel=entry.label;projected=false;graph.nodes.push_back({3,{}});return nextId;}
bool InsertNewNodeOnExistingLink(Fixture*,void*,const EditorNodeGraph::Link& link,int){pickedFrom=link.fromNodeId;pickedTo=link.toNodeId;++inserts;return insertSucceeds;}
''' + conversion + r'''
void run(bool deliver=true,bool validPayload=true,bool presentEntry=true) {
struct {bool allowDropTarget=true;} options;
NodeBrowserEntry entry{"owned catalog entry",7}; const NodeBrowserEntry* pointer=presentEntry?&entry:nullptr;
ImGui::payload={&pointer,validPayload?int(sizeof(pointer)):0,deliver}; ImGui::io.MousePos={350,260};
std::optional<NodeBrowserEntry> deliveredEntry; ImVec2 deliveredPosition{};
''' + capture + r'''
// A slide/autofocus/smooth zoom updates the transform after item acceptance.
m_CanvasOrigin={100,40};m_Pan={50,20};m_Zoom=2;
entry.label="catalog changed"; // Delivery must own its payload, not borrow it.
''' + prepare + '\nprepareNodeProjection();\n' + delivery + r'''
if (!deliver || !validPayload || !presentEntry) {require(adds==0&&inserts==0&&preparations==1,"preview performed a drop");return;}
require(adds==1,"drop was lost or repeated");
require(insertedAt.x==100&&insertedAt.y==100,"drop used the previous canvas/camera transform");
require(insertedLabel=="owned catalog entry","drop borrowed catalog storage");
require(inserts==(onWire&&nextId>0?1:0) && (!inserts || (pickedFrom==1&&pickedTo==2)),"drop did not pick the current wire");
require(projected&&preparations==2,"new topology was not projected before rendering");
}
};
int main(){try {
Fixture{}.run();
Fixture blank;blank.onWire=false;blank.run();
Fixture failedInsertion;failedInsertion.insertSucceeds=false;failedInsertion.run();
Fixture failedAdd;failedAdd.nextId=-1;failedAdd.run();
Fixture{}.run(false);
Fixture{}.run(true,false);
Fixture{}.run(true,true,false);
std::cout<<"PASS catalog drop current camera, owned payload, wire/blank/failure and post-edit projection\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
'''
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(generated)
        return
    with tempfile.TemporaryDirectory(prefix='stack-drop-geometry-') as temp:
        cpp, exe = Path(temp)/'test.cpp', Path(temp)/'test'
        cpp.write_text(generated)
        subprocess.run([args.cxx,'-std=c++17','-O1',str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    main()
