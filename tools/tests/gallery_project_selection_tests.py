#!/usr/bin/env python3
"""Real ImGui list clicks using extracted production Gallery selection/row code.

Catalog data and editor side effects are small named doubles; this does not
instantiate EditorModule, read projects or test image rendering. --gallery can
point at an older production file for the regression's negative control.
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
    parser.add_argument('--gallery', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--imgui', type=Path)
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (args.gallery or root/'src/Editor/Internal/RawLab/EditorModuleRawLabGallery.cpp').read_text()
    selection = source[source.index('    const auto sameGalleryProjectPath ='):source.index('    const auto selectSource =')]
    rows = block(source, 'if (projectsOnly && !presentation.projects.empty())')
    literal_row = (block(source, 'bool GalleryListSelectable(')
                   if 'bool GalleryListSelectable(' in source else '')
    generated = r'''
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
''' + literal_row + r'''
namespace Stack::RawWorkspace {
enum class GalleryDisplayMode { Grid, List };
enum class GalleryContentMode { Projects, Bracket };
struct GalleryProjectView {
 std::filesystem::path projectPath; std::string projectId,projectName;
 bool multiFrameProject=false; std::size_t frameCount=1;
};
}
using Project=Stack::RawWorkspace::GalleryProjectView;
using Display=Stack::RawWorkspace::GalleryDisplayMode;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct GalleryFixture {
 struct {std::vector<Project> projects;} presentation;
 struct Family {std::vector<std::size_t> projectIndices;};
 std::vector<Family> families{{{0,1}},{{2,3}},{{4}}};
 const std::vector<Family>* projectStacks=&families;
 bool compactFilmstrip=false,projectsOnly=true;
 Display m_RawWorkspaceGalleryDisplayMode=Display::List;
 Stack::RawWorkspace::GalleryContentMode m_RawWorkspaceGalleryContentMode=Stack::RawWorkspace::GalleryContentMode::Projects;
 std::filesystem::path m_RawWorkspaceLabFocusedProjectPath,m_RawWorkspaceLabProjectSelectionAnchor;
 std::string m_RawWorkspaceLabFocusedProjectName;
 std::vector<std::filesystem::path> m_RawWorkspaceLabSelectedProjectPaths;
 std::unordered_map<std::string,std::string> m_RawWorkspaceGalleryProjectCoverIds;
 struct {std::vector<std::string> selectedSourceKeys;std::string selectedSourceKey;} m_RawWorkspace;
 int invalidations=0,opens=0;
 void InvalidateRawWorkspaceGalleryPresentation(bool){++invalidations;}
 void RequestOpenRawWorkspaceProjectFromGallery(const std::filesystem::path&){++opens;}
 void RenderRawGalleryContextActions(const std::filesystem::path&){}
 void LabTooltip(const std::string&){}
 GalleryFixture(){for(const char* id:{"a0","a1","b0","b1","c0"})presentation.projects.push_back({std::filesystem::path(id),id,id});}
 void draw(int direct=-1,bool toggle=false,bool range=false) {
''' + selection + r'''
 if(direct>=0){const auto& p=presentation.projects[direct];selectProject(p.projectPath,p.projectName,toggle,range);return;}
''' + rows + r'''
 }
 void expect(std::initializer_list<const char*> expected,const char* message) {
  require(m_RawWorkspaceLabSelectedProjectPaths.size()==expected.size(),message);
  for(const auto* path:expected)require(std::find(m_RawWorkspaceLabSelectedProjectPaths.begin(),m_RawWorkspaceLabSelectedProjectPaths.end(),std::filesystem::path(path))!=m_RawWorkspaceLabSelectedProjectPaths.end(),message);
 }
};
int main(){
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
 io.DisplaySize={600,400};io.DeltaTime=1.f/60.f;
 unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
 try {
  GalleryFixture* active=nullptr;
  float rowHeight=0,textHeight=0;
  const auto frame=[&](int direct=-1,bool toggle=false,bool range=false){
   ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({600,400});
   ImGui::Begin("Gallery selection",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
   ImGui::SetCursorScreenPos({20,30});rowHeight=ImGui::GetTextLineHeightWithSpacing();textHeight=ImGui::GetTextLineHeight();
   active->draw(direct,toggle,range);ImGui::Dummy({0,0});ImGui::End();ImGui::Render();
  };
  const auto click=[&](int row,bool shift=false,bool ctrl=false){
   io.AddKeyEvent(ImGuiMod_Shift,shift);io.AddKeyEvent(ImGuiMod_Ctrl,ctrl);
   io.AddMousePosEvent(60,30+(row+1)*rowHeight+textHeight*.5f);frame();
   io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();frame();
   io.AddKeyEvent(ImGuiMod_Shift,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();
  };
  GalleryFixture siblings;active=&siblings;frame();frame();
  click(0);siblings.expect({"a0"},"ordinary list click did not select its row");
  click(1,true);siblings.expect({"a0","a1"},"Shift-list range omitted a sibling version");
  require(siblings.opens==0,"Shift-list range opened an editor");
  click(0);click(4,true);siblings.expect({"a0","a1","b0","b1","c0"},"cross-family list range omitted visible rows");
  click(4);click(1,true);siblings.expect({"a1","b0","b1","c0"},"reverse list range did not follow visible order");
  click(4);click(0,false,true);click(3,true,true);siblings.expect({"a0","a1","b0","b1","c0"},"Ctrl-Shift range lost additive selection");
  for(bool filmstrip:{false,true}) {
   GalleryFixture grouped;active=&grouped;grouped.compactFilmstrip=filmstrip;
   grouped.m_RawWorkspaceGalleryDisplayMode=filmstrip?Display::List:Display::Grid;
   frame(1);frame(4,false,true);grouped.expect({"a1","b0","c0"},"grid/filmstrip range stopped using one family cover");
  }
  std::cout<<"PASS real ImGui project-list clicks: sibling, cross-family, reverse, additive Shift ranges; grouped controls unchanged\n";
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';ImGui::DestroyContext();return 1;}
 ImGui::DestroyContext();
}
'''
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(generated)
        return
    if not args.imgui:
        parser.error('--imgui is required for standalone compilation (or use --output)')
    with tempfile.TemporaryDirectory(prefix='stack-gallery-selection-') as temp:
        cpp, exe = Path(temp)/'test.cpp', Path(temp)/'test'
        cpp.write_text(generated)
        subprocess.run([args.cxx,'-std=c++17','-O2','-DNDEBUG','-I',str(args.imgui),str(cpp),
            *[str(args.imgui/name) for name in ['imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp']],'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    main()
