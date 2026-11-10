#!/usr/bin/env python3
"""Compile the production project-load transition against readiness boundaries.

Checks acknowledgement, immediate readiness and owner isolation without loading
files, creating a window, or measuring real rendering performance.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

from rail_navigation_intent_tests import block


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--transition', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (args.transition or root/'src/App/AppShellLibraryTransition.cpp').read_text()
    generated = r'''
#include "App/ProjectLoadTransition.h"
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace ImGui { double now=10.; double GetTime(){return now;} }
namespace Stack::Notifications { bool SystemReducedMotion(){return false;} }
namespace Stack::Project { struct LoadedProjectData {}; }
constexpr int RootTabEditor=1,RootTabLibrary=0;
// Retain the former values only for --transition regression comparison.
constexpr double kLibraryLoadFadeOutSeconds=.48,kLibraryLoadSpinnerFadeInSeconds=.26,
    kLibraryLoadSpinnerMinVisibleSeconds=.85,kLibraryLoadSpinnerFadeOutSeconds=.32,
    kLibraryLoadEditorRevealSeconds=1.90;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Editor {
 bool beginOk=true,coreReady=false,imageReady=false,failed=false;
 int beginCount=0,reveals=0,pumps=0;
 std::size_t thumbnails=3;
 std::string status="Preparing image",loadStatus,project="project.stack";
 std::filesystem::path folder;
 void PumpNonRenderingWork(double,bool){++pumps;}
 bool BeginDeferredLoadedProjectApply(std::shared_ptr<Stack::Project::LoadedProjectData>){++beginCount;return beginOk;}
 const std::string& GetDeferredLoadedProjectStatusText(){return status;}
 const std::string& GetProjectLoadStatusText(){return loadStatus;}
 bool HasDeferredLoadedProjectApplyCoreFinished(){return coreReady;}
 bool HasDeferredLoadedProjectFirstRenderReady(){return imageReady;}
 bool HasDeferredLoadedProjectApplyFailed(){return failed;}
 std::size_t GetPendingNodeBrowserThumbnailWarmCount(){return thumbnails;}
 std::size_t GetPendingNodeBrowserThumbnailGenerationCount(){return thumbnails;}
 const std::string& GetCurrentProjectFileName(){return project;}
 std::filesystem::path ResolveRawWorkspaceSourceFolder(const std::string&){return "gallery-owner";}
 void SetRawWorkspaceFolder(const std::filesystem::path& path){folder=path;}
 void BeginLibraryLoadReveal(){++reveals;}
};
struct LibraryManager {
 int completions=0;bool success=false;
 static LibraryManager& Get(){static LibraryManager value;return value;}
 void SetProjectLoadApplyingStatus(Editor* editor,const std::string& status){editor->loadStatus=status;}
 void FinishDeferredProjectLoad(Editor* editor,bool ok,const std::string& status){++completions;success=ok;editor->loadStatus=status;}
};
struct ProjectWorkspace {
 std::uint64_t id=0;int rootTab=0;
 std::unique_ptr<Editor> editor=std::make_unique<Editor>();
 ProjectLoadTransition loadTransition;
 std::filesystem::path sourceWorkspaceRoot;
};
struct AppShell {
 using LibraryToEditorProjectLoadPhase=ProjectLoadPhase;
 std::uint64_t m_ActiveProjectWorkspace=2,m_GalleryWorkspaceId=1;
 int m_CurrentTabId=0,decodeRequests=0,tabChanges=0;
 bool m_LibraryWindowOpen=true,m_LibraryWindowFocusRequested=true;
 struct Library {void DismissPreviewsForProjectLoad(){}} m_Library;
 std::vector<std::unique_ptr<ProjectWorkspace>> m_ProjectWorkspaces;
 AppShell(){for(auto id:{1,2,3}){auto item=std::make_unique<ProjectWorkspace>();item->id=id;m_ProjectWorkspaces.push_back(std::move(item));}}
 ProjectWorkspace* FindProjectWorkspace(std::uint64_t id){for(auto& item:m_ProjectWorkspaces)if(item->id==id)return item.get();return nullptr;}
 void OnTabChanged(int,int){++tabChanges;}
 void TraceLibraryLoadTransition(ProjectWorkspace&,const std::string&){}
 void RequestDeferredLibraryProjectLoad(ProjectWorkspace& workspace){workspace.loadTransition.loadRequested=true;++decodeRequests;}
 void SetLibraryToEditorProjectLoadPhase(ProjectWorkspace&,LibraryToEditorProjectLoadPhase);
 void TickLibraryToEditorProjectLoadTransition(ProjectWorkspace&);
 void OnFramePresented();
};
''' + '\n'.join(block(source, marker) for marker in [
        'void AppShell::SetLibraryToEditorProjectLoadPhase(',
        'void AppShell::TickLibraryToEditorProjectLoadTransition(ProjectWorkspace&',
        'void AppShell::OnFramePresented()',
    ]) + r'''
void begin(AppShell& app,ProjectWorkspace& owner){
 owner.loadTransition.Reset();owner.editor=std::make_unique<Editor>();
 owner.loadTransition.ownerLease=std::make_shared<int>(7);
 owner.loadTransition.projectFileName="project.stack";
 owner.loadTransition.decodedProject=std::make_shared<Stack::Project::LoadedProjectData>();
 owner.loadTransition.dismissLibraryPreviewsPending=true;
 app.SetLibraryToEditorProjectLoadPhase(owner,ProjectLoadPhase::SpinnerFadeIn);
}
int main(){try{
 AppShell app;auto& owner=*app.FindProjectWorkspace(2);auto& load=owner.loadTransition;
 begin(app,owner);
 app.TickLibraryToEditorProjectLoadTransition(owner);
 require(app.decodeRequests==0,"foreground decode started before acknowledgement frame");
 app.OnFramePresented();app.OnFramePresented();
 require(app.decodeRequests==1,"acknowledged load must start exactly once");
 load.decodeReady=load.decodeSucceeded=true;
 ImGui::now+=.001;app.TickLibraryToEditorProjectLoadTransition(owner);
 require(owner.editor->beginCount==1&&load.phase==ProjectLoadPhase::WaitForEditorReady,
     "ready decode waited for an artificial spinner dwell");
 owner.editor->coreReady=true;app.TickLibraryToEditorProjectLoadTransition(owner);
 require(load.phase==ProjectLoadPhase::WaitForEditorReady&&LibraryManager::Get().completions==0,
     "load completed before a usable first image");
 const auto generation=load.generation;owner.editor->imageReady=true;
 app.TickLibraryToEditorProjectLoadTransition(owner);
 require(load.phase==ProjectLoadPhase::None&&app.m_CurrentTabId==RootTabEditor&&owner.editor->reveals==1,
     "ready image remained input-blocked behind a reveal");
 require(!load.ownerLease&&!load.decodedProject&&load.generation==generation,
     "terminal load retained resources or invalidated navigation intent");
 require(LibraryManager::Get().completions==1&&LibraryManager::Get().success&&owner.editor->thumbnails>0,
     "optional thumbnail work blocked successful readiness");
 app.TickLibraryToEditorProjectLoadTransition(owner);
 require(LibraryManager::Get().completions==1,"completed load replayed publication");
 begin(app,owner);load.decodeReady=true;load.decodeSucceeded=false;
 app.TickLibraryToEditorProjectLoadTransition(owner);
 require(load.phase==ProjectLoadPhase::None&&app.m_CurrentTabId==RootTabLibrary,
     "failed decode remained blocked behind presentation timing");
 require(owner.editor->beginCount==0&&!load.ownerLease&&!load.AcceptsCompletion(load.generation),
     "failed load applied data or accepted a late completion");
 begin(app,owner);load.decodeReady=load.decodeSucceeded=true;owner.editor->beginOk=false;
 app.TickLibraryToEditorProjectLoadTransition(owner);
 require(load.phase==ProjectLoadPhase::None&&!LibraryManager::Get().success,
     "failed apply start was hidden by a success or presentation wait");
 begin(app,owner);load.phase=ProjectLoadPhase::WaitForEditorReady;owner.editor->failed=true;
 app.TickLibraryToEditorProjectLoadTransition(owner);
 require(load.phase==ProjectLoadPhase::None&&!LibraryManager::Get().success,
     "delayed apply failure did not finish promptly");
 auto& hidden=*app.FindProjectWorkspace(3);begin(app,hidden);
 const int tab=app.m_CurrentTabId,changes=app.tabChanges;
 app.TickLibraryToEditorProjectLoadTransition(hidden);
 require(hidden.loadTransition.loadRequested,"hidden owner waited for an impossible presented frame");
 hidden.loadTransition.decodeReady=hidden.loadTransition.decodeSucceeded=true;
 app.TickLibraryToEditorProjectLoadTransition(hidden);
 hidden.editor->coreReady=hidden.editor->imageReady=true;
 app.TickLibraryToEditorProjectLoadTransition(hidden);
 require(hidden.loadTransition.phase==ProjectLoadPhase::None&&hidden.rootTab==RootTabEditor&&hidden.editor->reveals==0,
     "hidden project failed to complete independently");
 require(app.m_CurrentTabId==tab&&app.tabChanges==changes,"hidden completion stole foreground navigation");
 require(owner.editor->pumps==0&&hidden.editor->pumps==0,
     "load presentation bypassed the shell's budgeted workspace pump");
 std::cout<<"PASS acknowledged project loading, readiness, failures and owner isolation\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
'''
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(generated)
        return
    with tempfile.TemporaryDirectory() as directory:
        source_path, binary = Path(directory)/'test.cpp', Path(directory)/'test'
        source_path.write_text(generated)
        subprocess.run([args.cxx, '-std=c++17', '-I'+str(root/'src'),
                        str(source_path), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
