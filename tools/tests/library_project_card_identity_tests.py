#!/usr/bin/env python3
"""Real ImGui project-card scope, context-popup and Open action regression.

Extracts the shipping Grid scope and Cards right-click/popup/action prefix.
Thumbnail/layout drawing and RequestOpenEditorProject are boundary doubles;
selection and popup ownership use production code. No project files are opened.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--grid-source', type=Path)
    parser.add_argument('--imgui', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    grid = (args.grid_source or root/'src/Library/LibraryModuleGrid.cpp').read_text()
    cards = (root/'src/Library/LibraryModuleCards.cpp').read_text()
    end = grid.index('            if (RenderProjectCard(*project, editor))')
    start = grid.rfind('            ImGui::SetCursorScreenPos(', 0, end)
    scope = grid[grid.index('\n', start)+1:end]
    start = cards.index('    if (isRightClicked) {')
    end = cards.index('            if (project.needsAttention)', start)
    menu = cards[start:end] + '''        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(); ImGui::PopStyleVar(4);
'''
    generated = r'''
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
struct Project {std::string fileName;bool needsAttention=false;std::string projectKind="raw";};
std::set<std::string> m_SelectedProjects;
bool m_BlockLibraryGridContextMenuThisFrame=false;
std::string opened;
void RequestOpenEditorProject(const std::string& path){opened=path;}
std::vector<ImVec2> menuPoints;
namespace ImGui {
bool RecordMenuItem(const char* label){
    const bool clicked=MenuItem(label);
    const auto p=GetItemRectMin();menuPoints.push_back({p.x+15,p.y+8});
    return clicked;
}
}
#define MenuItem RecordMenuItem
void DrawCard(Project& project){
    ImGui::InvisibleButton("##hitbox",{140,50});
    const bool isRightClicked=ImGui::IsItemClicked(ImGuiMouseButton_Right);
    const bool isSelected=m_SelectedProjects.count(project.fileName)>0;
''' + menu + r'''
}
#undef MenuItem
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Run(bool special,bool reversed,int target,bool reorder){
    ImGui::CreateContext();struct Cleanup{~Cleanup(){ImGui::DestroyContext();}}cleanup;
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={800,600};io.DeltaTime=1.f/60;
    unsigned char*p;int w,h;io.Fonts->GetTexDataAsRGBA32(&p,&w,&h);
    std::vector<Project> projects= special
        ? std::vector<Project>{{"C:/Projects/A###same.stack"},{"C:/Projects/B###same.stack"}}
        : std::vector<Project>{{"C:/Projects/ordinaryA.stack"},{"C:/Projects/ordinaryB.stack"}};
    if(reversed)std::reverse(projects.begin(),projects.end());
    const auto expected=projects[target].fileName;
    m_SelectedProjects.clear();opened.clear();
    std::vector<ImGuiID> ids;
    auto frame=[&]{
        ids.clear();menuPoints.clear();ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({800,600});
        ImGui::Begin("Library",nullptr,ImGuiWindowFlags_NoDecoration);
        for(int i=0;i<2;++i){
            auto* project=&projects[i];ImGui::SetCursorScreenPos({20.f+i*180.f,30});
            const auto ordinaryScope=ImGui::GetID(project->fileName.c_str());
''' + scope + r'''
            if(!special)Check(ImGui::GetCurrentWindow()->IDStack.back()==ordinaryScope,
                "Ordinary project path identity changed");
            ids.push_back(ImGui::GetID("##hitbox"));DrawCard(*project);ImGui::PopID();
        }
        ImGui::End();ImGui::Render();
    };
    frame();frame();const auto initialIds=ids;
    io.AddMousePosEvent(40.f+target*180.f,50);frame();
    io.AddMouseButtonEvent(1,true);frame();io.AddMouseButtonEvent(1,false);frame();
    Check(m_SelectedProjects==std::set<std::string>{expected},"Right-click selected the wrong project");
    if(reorder){std::reverse(projects.begin(),projects.end());frame();
        Check(ids[0]==initialIds[1]&&ids[1]==initialIds[0],"Project input identity followed its slot");}
    frame();Check(!menuPoints.empty(),"Project context popup did not open");
    const auto action=menuPoints.front();
    io.AddMousePosEvent(action.x,action.y);frame();io.AddMouseButtonEvent(0,true);frame();
    // Reordering while the menu action is held must preserve its path owner.
    if(reorder){std::reverse(projects.begin(),projects.end());frame();}
    io.AddMouseButtonEvent(0,false);frame();
    Check(opened==expected,"Open in Editor used another project's popup action");
    Check(m_SelectedProjects==std::set<std::string>{expected},"Popup action changed project selection");
    Check(ids[0]!=ids[1],"Different project paths shared an ImGui identity");
}
int main(){try{
    for(bool special:{false,true})for(bool reversed:{false,true})
        for(int target:{1,0})for(bool reorder:{false,true})Run(special,reversed,target,reorder);
    std::cout<<"Library project card popup identity passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
'''
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(generated)
        return
    if not args.imgui:
        parser.error('--imgui required without --output')
    with tempfile.TemporaryDirectory(prefix='stack-project-card-') as directory:
        cpp = Path(directory)/'test.cpp'
        exe = Path(directory)/'test'
        cpp.write_text(generated)
        subprocess.run(['g++', '-std=c++17', '-O2', '-DNDEBUG', '-I'+str(args.imgui), str(cpp),
                        *[str(args.imgui/(name+'.cpp')) for name in ('imgui','imgui_draw','imgui_tables','imgui_widgets')],
                        '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
