#!/usr/bin/env python3
"""Real ImGui interaction with production Library tag-filter drawing.

Extracts the local helper and RenderTagFilters method, using real TagManager
with its storage path unset (no disk writes). --source accepts an older file
for a negative control. No application, GL context, or saved projects required.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def block(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--imgui', type=Path)
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (args.source or root/'src/Library/LibraryModuleTags.cpp').read_text()
    helper = block(source, 'bool LiteralTagCheckbox(') if 'bool LiteralTagCheckbox(' in source else ''
    generated = r'''
#include <imgui.h>
#include <imgui_internal.h>
#include "Library/TagManager.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <stdexcept>
struct Palette { ImVec4 controlSurface,controlSurfaceHovered,controlSurfaceActive; };
namespace StackAppearance { using RuntimeSurfacePalette=Palette; }
ImVec4 BlendColor(const ImVec4& a,const ImVec4&,float){return a;}
class LibraryModule { public:
 bool m_ShowAssets=false,m_FilterNoTag=false;
 std::unordered_set<std::string> m_SelectedAssets,m_SelectedProjects,m_ActiveTagFilters;
 char m_AddTagBuffer[128]={};
 void RenderTagFilters(bool,const Palette&);
};
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Row {ImGuiID checkbox=0;ImRect bounds,box;};
std::vector<Row> rows;
namespace ImGui {
bool RecordCheckbox(const char* label,bool* value){
 bool changed=Checkbox(label,value);
 rows.push_back({GetItemID(),{GetItemRectMin(),GetItemRectMax()},{GetItemRectMin(),GetItemRectMax()}});
 return changed;
}
bool RecordItemAdd(const ImRect& bounds,ImGuiID id){
 bool visible=ItemAdd(bounds,id);
 rows.push_back({id,bounds,{bounds.Min,ImVec2(bounds.Min.x+GetFrameHeight(),bounds.Min.y+GetFrameHeight())}});
 return visible;
}
}
#define Checkbox RecordCheckbox
#define ItemAdd RecordItemAdd
''' + helper + '\n' + block(source, 'void LibraryModule::RenderTagFilters(') + r'''
#undef Checkbox
#undef ItemAdd
int main(){try{
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
 io.DisplaySize={800,600};io.DeltaTime=1.f/60;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
 unsigned char* px;int w,h;io.Fonts->GetTexDataAsRGBA32(&px,&w,&h);
 auto& tags=TagManager::Get();
 tags.AddTag("a","blue###same");tags.AddTag("b","red###same");tags.AddTag("c","travel");
 LibraryModule lib;bool disabled=false,modal=false,openModal=false;
 auto frame=[&](int focus=-1){
  rows.clear();ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({500,500});
  ImGui::Begin("Library");ImGui::BeginDisabled(disabled);
  if(focus>=0)ImGui::SetKeyboardFocusHere(focus);
  lib.RenderTagFilters(false,{});ImGui::EndDisabled();
  if(openModal){ImGui::OpenPopup("Block");openModal=false;}
  ImGui::SetNextWindowPos({600,400});
  if(ImGui::BeginPopupModal("Block",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
   ImGui::TextUnformatted("Decision");if(!modal)ImGui::CloseCurrentPopup();ImGui::EndPopup();
  }
  ImGui::End();ImGui::Render();
 };
 auto point=[&](int row,bool label){const auto r=rows.at(row);return label?
  ImVec2(r.bounds.Max.x-3,(r.bounds.Min.y+r.bounds.Max.y)/2):
  ImVec2(r.box.Min.x+5,(r.box.Min.y+r.box.Max.y)/2);};
 auto click=[&](ImVec2 p){io.AddMousePosEvent(p.x,p.y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();};
 frame();frame();
 // This is the concrete old failure: the earlier same-ID row consumes release.
 click(point(2,false));
 require(lib.m_ActiveTagFilters.count("red###same")==1,"second ### tag checkbox did not toggle independently");
 require(lib.m_ActiveTagFilters.count("blue###same")==0,"click toggled the other tag");
 require(rows[1].checkbox!=rows[2].checkbox,"raw tag IDs collided");
 require(rows.size()==4,"tags must each register exactly one checkbox stop");
 const auto& names=tags.GetAllKnownTags();
 for(std::size_t n=0;n<names.size();++n){
  const float expected=ImGui::GetFrameHeight()+ImGui::GetStyle().ItemInnerSpacing.x+
   ImGui::CalcTextSize(names[n].c_str(),nullptr,false).x;
  require(std::abs(rows[n+1].bounds.GetWidth()-expected)<.1f,"literal label width was truncated");

 }
 click(point(1,true));require(lib.m_ActiveTagFilters.count("blue###same")==1,"literal label click did not toggle");
 click(point(3,false));require(lib.m_ActiveTagFilters.count("travel")==1,"normal checkbox stopped working");
 click(point(3,true));require(lib.m_ActiveTagFilters.count("travel")==0,"normal label stopped working");
 auto crossRow=[&](bool startOnLabel){
  auto start=point(3,startOnLabel),finish=point(3,!startOnLabel);
  io.AddMousePosEvent(start.x,start.y);frame();io.AddMouseButtonEvent(0,true);frame();
  io.AddMousePosEvent(finish.x,finish.y);frame();io.AddMouseButtonEvent(0,false);frame();
 };
 crossRow(false);require(lib.m_ActiveTagFilters.count("travel")==1,"checkbox-to-label release cancelled");
 crossRow(true);require(lib.m_ActiveTagFilters.count("travel")==0,"label-to-checkbox release cancelled");
 auto p=point(1,true);io.AddMousePosEvent(p.x,p.y);frame();io.AddMouseButtonEvent(0,true);frame();
 io.AddMousePosEvent(450,450);frame();io.AddMouseButtonEvent(0,false);frame();
 require(lib.m_ActiveTagFilters.count("blue###same")==1,"dragging off label committed a toggle");
 disabled=true;frame();click(point(1,true));click(point(2,false));
 require(lib.m_ActiveTagFilters.size()==2,"disabled tag changed");disabled=false;frame();
 modal=true;openModal=true;frame();frame();click(point(1,true));click(point(2,false));
 require(lib.m_ActiveTagFilters.size()==2,"popup-blocked tag changed");modal=false;frame();frame();
 // Keyboard focus explicitly targets the normal checkbox after the three
 // preceding checkbox stops. Labels must not enter that navigation sequence.
 io.AddMousePosEvent(700,550);frame(2);frame();
 io.AddKeyEvent(ImGuiKey_Tab,true);frame();io.AddKeyEvent(ImGuiKey_Tab,false);frame();frame();
 io.AddKeyEvent(ImGuiKey_Tab,true);frame();io.AddKeyEvent(ImGuiKey_Tab,false);frame();frame();
 require(GImGui->NavId==rows[3].checkbox,"keyboard focus did not reach the sole checkbox stop");
 io.AddKeyEvent(ImGuiKey_Space,true);frame();io.AddKeyEvent(ImGuiKey_Space,false);frame();
 require(lib.m_ActiveTagFilters.count("travel")==1,"keyboard checkbox activation failed");
 ImGui::DestroyContext();std::cout<<"Library tag filters passed\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
'''
    if args.output:
        args.output.write_text(generated)
    if args.imgui:
        with tempfile.TemporaryDirectory(prefix='stack-library-tags-') as work:
            cpp = Path(work)/'test.cpp'
            exe = Path(work)/'test'
            cpp.write_text(generated)
            subprocess.run([args.cxx, '-std=c++17', '-I'+str(args.imgui), '-I'+str(root/'src'), str(cpp),
                            str(root/'src/Library/TagManager.cpp'),
                            *[str(args.imgui/(name+'.cpp')) for name in ('imgui','imgui_draw','imgui_tables','imgui_widgets')],
                            '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
    elif not args.output:
        parser.error('provide --output or --imgui')


if __name__ == '__main__':
    main()
