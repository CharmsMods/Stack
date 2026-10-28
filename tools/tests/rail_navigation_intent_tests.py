#!/usr/bin/env python3
"""Compile actual shell rail queue/dispatch methods against small UI boundary doubles.

Uses production ProjectLoadTransition. No editor, disk saves, or native UI is run.
--rail accepts the old dispatch source for a negative control; queuing stays current.
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
    parser.add_argument('--rail', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    current = (root/'src/App/AppNavigationRail.cpp').read_text()
    dispatch = (args.rail.read_text() if args.rail else current)
    workspaces = (root/'src/App/AppProjectWorkspaces.cpp').read_text()
    # Direct notification/retirement activation must also cancel pending intent.
    activate = block(workspaces, 'bool AppShell::ActivateProjectWorkspace(')
    assert 'm_NavigationRail.requestedDestination = -1;' in activate
    shell = (root/'src/App/AppShell.cpp').read_text()
    assert 'QueueRailNavigation(0);' in shell
    assert 'QueueRailNavigation(item.first);' in current
    assert 'QueueRailNavigation(Library);' in current
    generated = r'''
#include "App/ProjectLoadTransition.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
constexpr int Library=0,Graph=1,RawTab=5,Queue=7;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Workspace {std::uint64_t id; ProjectLoadTransition loadTransition;};
struct AppShell {
 struct Rail {int requestedDestination=-1;std::uint64_t requestedWorkspace=0,requestedLoadGeneration=0,lastEditingWorkspace=2;bool galleryView=false;} m_NavigationRail;
 enum class WorkspaceTabAction {None,Select,Close,Create};
 WorkspaceTabAction m_WorkspaceTabAction=WorkspaceTabAction::None;
 std::uint64_t m_WorkspaceTabActionId=0,m_ActiveProjectWorkspace=2,m_GalleryWorkspaceId=1;
 std::vector<std::unique_ptr<Workspace>> m_ProjectWorkspaces;
 bool switchAllowed=true;int requested=-1,dispatches=0;
 AppShell(){for(auto id:{1,2,3})m_ProjectWorkspaces.push_back(std::make_unique<Workspace>(Workspace{static_cast<std::uint64_t>(id),{}}));}
 Workspace* FindProjectWorkspace(std::uint64_t id){for(auto& w:m_ProjectWorkspaces)if(w->id==id)return w.get();return nullptr;}
 bool CanSwitchProjectWorkspace(){return switchAllowed;}
 void CancelProjectPillPreview(){}
 bool ActivateProjectWorkspace(std::uint64_t id){m_ActiveProjectWorkspace=id;return true;}
 std::uint64_t CreateProjectWorkspace(){return 3;}
 void RequestTabSwitch(int destination){requested=destination;++dispatches;}
 void QueueRailNavigation(int);
 void ApplyRailNavigation();
 void QueueWorkspaceTabAction(WorkspaceTabAction,std::uint64_t);
};
''' + block(current, 'void AppShell::QueueRailNavigation(') + '\n' + block(dispatch, 'void AppShell::ApplyRailNavigation(') + '\n' + block(workspaces, 'void AppShell::QueueWorkspaceTabAction(') + r'''
int main(){
 try {
 AppShell a;auto& load=a.FindProjectWorkspace(2)->loadTransition;
 a.QueueRailNavigation(Queue);a.ApplyRailNavigation();
 require(a.requested==Queue&&a.dispatches==1,"ordinary navigation must remain immediate");
 a.ApplyRailNavigation();require(a.dispatches==1,"navigation dispatched twice");
 load.Reset();load.phase=ProjectLoadPhase::SpinnerFadeIn;
 a.QueueRailNavigation(Queue);a.ApplyRailNavigation();
 require(a.dispatches==1&&a.m_NavigationRail.requestedDestination==Queue,"loading discarded user intent");
 a.QueueRailNavigation(Library);a.ApplyRailNavigation();
 a.RequestTabSwitch(Graph); // Simulate an independent load-owned editor request.
 load.phase=ProjectLoadPhase::None;a.ApplyRailNavigation();
 require(a.requested==Library&&a.dispatches==3,"latest intent lost to load-owned navigation");
 a.ApplyRailNavigation();require(a.dispatches==3,"completion dispatched twice");
 // Failed load reaches None in the same generation: chosen destination still applies.
 load.Reset();load.phase=ProjectLoadPhase::SpinnerFadeOut;load.applySucceeded=false;
 a.QueueRailNavigation(Queue);a.ApplyRailNavigation();load.phase=ProjectLoadPhase::None;a.ApplyRailNavigation();
 require(a.requested==Queue&&a.dispatches==4,"failed load lost user destination");
 // Cancellation/replacement advances generation and must discard old intent.
 load.Reset();load.phase=ProjectLoadPhase::SpinnerFadeIn;a.QueueRailNavigation(Library);
 load.Reset();a.ApplyRailNavigation();require(a.dispatches==4&&a.m_NavigationRail.requestedDestination<0,"canceled load replayed intent");
 load.phase=ProjectLoadPhase::SpinnerFadeIn;a.QueueRailNavigation(Library);
 load.Reset();load.phase=ProjectLoadPhase::SpinnerFadeIn;a.ApplyRailNavigation();
 require(a.m_NavigationRail.requestedDestination<0,"replacement load retained stale intent");
 a.QueueRailNavigation(Queue);a.m_ActiveProjectWorkspace=3;a.ApplyRailNavigation();
 require(a.dispatches==4&&a.m_NavigationRail.requestedDestination<0,"A request navigated B");
 a.m_ActiveProjectWorkspace=2;load.phase=ProjectLoadPhase::None;
 a.QueueRailNavigation(Queue);a.switchAllowed=false;a.ApplyRailNavigation();require(a.dispatches==4,"modal guard bypassed");
 a.switchAllowed=true;a.ApplyRailNavigation();require(a.dispatches==5,"guarded intent was lost");
 // New explicit pill intent cancels old root intent, even the active pill.
 a.QueueRailNavigation(Queue);a.QueueWorkspaceTabAction(AppShell::WorkspaceTabAction::Select,2);a.ApplyRailNavigation();
 require(a.dispatches==5&&a.m_WorkspaceTabActionId==2,"active pill did not supersede rail request");
 a.QueueRailNavigation(Queue);a.QueueWorkspaceTabAction(AppShell::WorkspaceTabAction::Close,2);a.ApplyRailNavigation();
 require(a.dispatches==5,"closing owner replayed intent");
 a.m_ActiveProjectWorkspace=3;a.QueueRailNavigation(Queue);a.m_ProjectWorkspaces.pop_back();a.ApplyRailNavigation();
 require(a.dispatches==5&&a.m_NavigationRail.requestedDestination<0,"removed owner replayed intent");
 std::cout<<"PASS owned rail navigation intent\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
'''
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(generated)
        return
    with tempfile.TemporaryDirectory() as temp:
        source, binary = Path(temp)/'test.cpp', Path(temp)/'test'
        source.write_text(generated)
        subprocess.run([args.cxx, '-std=c++17', '-I'+str(root/'src'), str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
