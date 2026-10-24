#!/usr/bin/env python3
"""Generate a real-ImGui check of shell-owned file shortcut dispatch.

Copies the shell's command routing and any editor-local save predicates
unchanged. Native dialogs and saves are counters: no project or file is opened.
The Save As double queues the key releases/focus events a modal dialog can
produce, without advancing the current ImGui frame.
"""
import argparse
from pathlib import Path
import re

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
args = parser.parse_args()
def read(path):
    return (args.root / 'src' / path).read_text()
shell = read('App/AppShell.cpp')
shell = shell.split('    const bool globalShortcutAllowed =', 1)[1].split('\n    if (!loadTransitionActive', 1)[0]
shell = '    const bool globalShortcutAllowed =' + shell
editor = read('Editor/EditorModule.cpp')
command = editor.split('bool EditorModule::CanConsumeEditorCommandKeys() const {', 1)[1].split('\n}', 1)[0]
legacy = editor.split('    // Timeline presentation and playback are retired', 1)[1].split('    const float timelineHeight', 1)[0]
graph = read('Editor/Internal/EditorModuleGraphCatalog.cpp')
pattern = re.compile(r'    if \((?=[^{};]*CanConsumeEditorCommandKeys\(\))[^{};]*ImGui::IsKeyPressed\(ImGuiKey_S, false\)\)(?:\s*\{)?\s*RequestSaveCurrentProject\([^;]+;\s*(?:\})?')
# Refuse to silently omit a changed local save dispatch form.
def local_save(source):
    matches = pattern.findall(source)
    assert source.count('RequestSaveCurrentProject(') == len(matches)
    return '\n'.join(matches)
text = r'''
#include "imgui.h"
#include "imgui_internal.h"
#include "App/WorkspacePresentation.h"
#include "App/WorkspaceInputScope.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
struct EditorModule {
    struct Project { std::string name="Fixture"; };
    std::unique_ptr<Project> m_Project=std::make_unique<Project>();
    bool m_GraphCaptureWindowOpen=false;
    int saves=0;
    struct Context { bool canOpen=true; };
    Context GetProjectFileCommandContext() const { return {}; }
    void RequestSaveCurrentProject(const std::string&) { ++saves; }
    bool CanConsumeEditorCommandKeys() const { @COMMAND@ }
    void Graph() { @GRAPH@ }
    void Legacy() { @LEGACY@ }
};
struct AppShell {
    EditorModule* m_Editor;
    struct Startup { bool enabled=true; bool AllowsInput() const {return enabled;} } m_StartupRevealVisual;
    struct Tool { bool visible=false; bool Visible() const {return visible;} } m_ToolSwitcher;
    struct Rail { float previewAmount=0; } m_NavigationRail;
    bool m_ShowOpenProjectPrompt=false;
    unsigned long long m_ActiveProjectWorkspace=1;
    enum class PendingFileAction { NewEditorProject };
    enum class WorkspaceTabAction { Close };
    int saveAs=0;
    void QueueFileAction(PendingFileAction) {}
    void QueueWorkspaceTabAction(WorkspaceTabAction, unsigned long long) {}
    void RequestFileMenuSave() { m_Editor->RequestSaveCurrentProject("Fixture"); }
    void RequestFileMenuSaveAs() {
        ++saveAs;
        // Cancel the synthetic native dialog. Backend events are only queued;
        // the shell continues the same frame after GetSaveFileName returns.
        auto& io=ImGui::GetIO();
        io.AddFocusEvent(false); io.AddKeyEvent(ImGuiKey_S,false);
        io.AddKeyEvent(ImGuiMod_Shift,false); io.AddKeyEvent(ImGuiMod_Ctrl,false);
        io.AddFocusEvent(true);
    }
    void Dispatch() { auto& io=ImGui::GetIO(); @SHELL@ }
};
int main() {
    int failures=0;
    for (int host=0;host<2;++host) for (int scenario=0;scenario<7;++scenario) {
        ImGui::CreateContext();
        auto& io=ImGui::GetIO(); io.IniFilename=nullptr;
        io.DisplaySize={600,400}; io.DeltaTime=1.f/60.f;
        unsigned char* pixels=nullptr;int width=0,height=0;
        io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        ImGui::NewFrame(); ImGui::EndFrame();
        io.AddKeyEvent(ImGuiMod_Ctrl,true);
        if (scenario==0) io.AddKeyEvent(ImGuiMod_Shift,true);
        if (scenario==6) io.AddKeyEvent(ImGuiMod_Alt,true);
        io.AddKeyEvent(ImGuiKey_S,true);
        ImGui::NewFrame(); ImGui::Begin("Shortcut fixture");
        EditorModule editor; AppShell app{&editor};
        if (scenario==2) ImGui::OpenPopup("Owned popup");
        if (scenario==3) io.WantTextInput=true;
        if (scenario==4) app.m_NavigationRail.previewAmount=1;
        if (scenario==5) app.m_ToolSwitcher.visible=true;
        {
            Stack::Workspace::InputScope toolInput(scenario==5,false);
            app.Dispatch();
            Stack::Workspace::InputScope previewInput(scenario==4,false);
            if (host==0) editor.Graph(); else editor.Legacy();
        }
        const int expectedSaveAs=scenario==0 ? 1:0;
        const int expectedSave=scenario==1 || scenario==6 ? 1:0;
        const bool ok=app.saveAs==expectedSaveAs && editor.saves==expectedSave;
        const char* names[]={"Save As canceled","plain Save","popup owns input","text entry","project preview","tool switcher","Ctrl+Alt+S shell behavior"};
        std::cout<<(ok?"PASS ":"FAIL ")<<(host==0?"Graph ":"legacy ")<<names[scenario]
            <<" saveAs="<<app.saveAs<<" saves="<<editor.saves<<'\n';
        failures+=!ok;
        ImGui::End(); ImGui::EndFrame(); ImGui::DestroyContext();
    }
    std::cout<<"ImGui "<<IMGUI_VERSION<<"; real key dispatch only, no dialog/save I/O\n";
    return failures?1:0;
}
'''
for marker, value in {'COMMAND':command,'GRAPH':local_save(graph),'LEGACY':local_save(legacy),'SHELL':shell}.items():
    text=text.replace('@'+marker+'@', value)
args.output.write_text(text)
