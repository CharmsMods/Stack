#!/usr/bin/env python3
"""Compile unchanged production socket pickers against explicit layout boundaries.
Checks z-order ownership, not native event delivery, GPU rendering or projection.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

PREAMBLE = r'''
#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>
struct ImVec2 {float x=0,y=0;};
namespace EditorNodeGraph {
using Vec2=ImVec2;
enum class SocketDirection {Input,Output};
struct Graph {};
}
ImVec2 ToImVec2(ImVec2 p){return p;}
struct EditorNodeGraphUI {
 using Direction=EditorNodeGraph::SocketDirection;
 struct Rect {
  ImVec2 min,max;
  bool IsValid()const{return max.x>min.x&&max.y>min.y;}
  bool Contains(ImVec2 p)const{return p.x>=min.x&&p.x<=max.x&&p.y>=min.y&&p.y<=max.y;}
 };
 struct SocketAnchor {std::string socketId; Direction direction; ImVec2 screenPos;};
 struct NodeLayoutCache {Rect frameRect,interactionRect;std::vector<SocketAnchor> socketAnchors;};
 struct SocketHit {int nodeId=-1;std::string socketId;bool IsValid()const{return nodeId>0;}};
 struct Row {int group=1,target=3,source=4; std::string socket="in",sourceSocket="out";ImVec2 input{100,100},output{100,100};};
 std::vector<Row> m_CompoundEntryRows;
 int m_DragInputNodeId=-1,m_DragOutputNodeId=-1,m_CompoundEntryDragInput=-1,m_CompoundEntryDragOutput=-1;
 std::map<int,NodeLayoutCache> layouts;
 std::vector<int> order{2,1};
 int hiddenNode=-1;bool groupVisible=true,socketsVisible=true;
 float NodePinRadius()const{return 4;}
 float NodeContentScale()const{return 1;}
 bool LayoutGroupVisible(int)const{return groupVisible;}
 bool LayoutNodeInteractive(int id)const{return id!=hiddenNode;}
 const std::vector<int>& GetNodeHitTestOrder(const EditorNodeGraph::Graph&){return order;}
 const NodeLayoutCache* FindNodeLayoutCache(int id)const{auto it=layouts.find(id);return it==layouts.end()?nullptr:&it->second;}
 bool IsSocketVisibleDuringDrag(const EditorNodeGraph::Graph&,int,const std::string&,Direction)const{return socketsVisible;}
 SocketHit FindInputPinAt(const EditorNodeGraph::Graph&,const EditorNodeGraph::Vec2&);
 SocketHit FindOutputPinAt(const EditorNodeGraph::Graph&,const EditorNodeGraph::Vec2&);
};
'''
TESTS = r'''
int failures=0;
void check(bool ok,const char* text){if(!ok){++failures;std::cerr<<text<<'\n';}}
int main(){
 EditorNodeGraph::Graph graph;
 for(auto direction:{EditorNodeGraph::SocketDirection::Input,EditorNodeGraph::SocketDirection::Output}){
  EditorNodeGraphUI ui;
  auto pick=[&](ImVec2 p){return direction==EditorNodeGraph::SocketDirection::Input?ui.FindInputPinAt(graph,p):ui.FindOutputPinAt(graph,p);};
  // Rear socket is on its node boundary, not an invented interior anchor.
  const bool input=direction==EditorNodeGraph::SocketDirection::Input;
  ui.layouts[1]={input?EditorNodeGraphUI::Rect{{100,50},{200,150}}:EditorNodeGraphUI::Rect{{0,50},{100,150}},
                 {{-20,30},{220,170}},{{"socket",direction,{100,100}}}};
  ui.layouts[2]={{{80,80},{180,180}},{{60,60},{200,200}},{}};
  check(!pick({100,100}).IsValid(),"hidden back socket must not take front node body input");
  ui.order={1,2};
  check(pick({100,100}).nodeId==1,"front socket remains pickable after bringing its node forward");
  ui.order={2,1};ui.layouts[2].socketAnchors={{"front",direction,{100,100}}};
  check(pick({100,100}).nodeId==2,"front node's own socket remains pickable");
  ui.layouts[2].socketAnchors.front().screenPos=input?ImVec2{80,100}:ImVec2{180,100};
  check(pick(ui.layouts[2].socketAnchors.front().screenPos).nodeId==2,"front node boundary socket remains pickable");
  ui.layouts[2].socketAnchors.clear();ui.layouts[2].frameRect={{120,120},{180,180}};
  check(pick(input?ImVec2{92,100}:ImVec2{108,100}).nodeId==1,"socket hit halo outside its own frame remains pickable");
  check(!pick({100,200}).IsValid(),"point outside socket radius and frames remains empty");
  check(pick({100,100}).nodeId==1,"interaction padding alone must not occlude rear socket");
  ui.layouts[2].frameRect={{80,80},{180,180}};ui.hiddenNode=2;
  check(pick({100,100}).nodeId==1,"noninteractive node does not intercept socket");
  ui.hiddenNode=-1;ui.layouts[1].socketAnchors.clear();ui.m_CompoundEntryRows.push_back({});
  check(!pick({100,100}).IsValid(),"gateway hidden beneath node body must not take input");
  ui.layouts.clear();
  check(pick({100,100}).IsValid(),"visible gateway remains pickable outside node bounds");
  ui.m_CompoundEntryDragInput=-1;ui.m_CompoundEntryDragOutput=-1;
  pick({100,100});
  check((input?ui.m_CompoundEntryDragInput:ui.m_CompoundEntryDragOutput)==0,"gateway retains its row for pending-wire geometry");
  ui.groupVisible=false;
  check(!pick({100,100}).IsValid(),"hidden gateway remains noninteractive");
 }
 return failures?1:0;
}
'''
def main():
 parser=argparse.ArgumentParser()
 parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2])
 parser.add_argument('--output',type=Path)
 parser.add_argument('--cxx',default='g++')
 args=parser.parse_args()
 source=(args.root/'src/Editor/NodeGraph/UI/EditorNodeGraphUIHitTesting.cpp').read_text()
 methods=source[source.index('EditorNodeGraphUI::SocketHit EditorNodeGraphUI::FindInputPinAt('):source.index('int EditorNodeGraphUI::FindNodeAt(')]
 generated=PREAMBLE+methods+TESTS
 if args.output:
  args.output.write_text(generated);return
 with tempfile.TemporaryDirectory(prefix='graph-socket-occlusion-') as directory:
  cpp=Path(directory)/'test.cpp';binary=Path(directory)/'test';cpp.write_text(generated)
  subprocess.run([args.cxx,'-std=c++17','-Wall','-Wextra',str(cpp),'-o',str(binary)],check=True)
  subprocess.run([str(binary)],check=True)
if __name__=='__main__':main()
