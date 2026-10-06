#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include "AppShell.h"
#include "WorkspaceSwitcherDrawing.h"
#include "WorkspaceInputScope.h"
#include "imgui_internal.h"
#include <algorithm>
#include <GLFW/glfw3.h>

namespace {
GLFWwindow* FocusedStackWindow(ImGuiID* id) {
    for(auto* v : ImGui::GetPlatformIO().Viewports) {
        auto* window=static_cast<GLFWwindow*>(v->PlatformHandle);
        if(window && glfwGetWindowAttrib(window,GLFW_FOCUSED)) { *id=v->ID; return window; }
    }
    return nullptr;
}
bool Down(GLFWwindow* window, int key) {
#if defined(_WIN32)
    int vk=0;
    switch(key) { case GLFW_KEY_LEFT_ALT: vk=VK_LMENU; break; case GLFW_KEY_RIGHT_ALT: vk=VK_RMENU; break;
        case GLFW_KEY_LEFT_CONTROL: vk=VK_LCONTROL; break; case GLFW_KEY_TAB: vk=VK_TAB; break;
        case GLFW_KEY_F4: vk=VK_F4; break; case GLFW_KEY_ESCAPE: vk=VK_ESCAPE; break; }
    if(vk) return (GetAsyncKeyState(vk)&0x8000)!=0;
#endif
    return window && glfwGetKey(window,key)==GLFW_PRESS;
}
}
void AppShell::TickWorkspaceSwitcher() {
    if(m_Editor->IsAutoBracketWorkspace() || m_Editor->IsBracketingPresentationActive() || m_ToolSwitcher.Visible())return;
    if (TickDiagnosticWorkspaceSwitcher()) return;
    auto& sw=m_WorkspaceSwitcher; auto& io=ImGui::GetIO();
    auto* main=ImGui::GetMainViewport();
    sw.scale=std::max(.2f, std::min({main->Size.x/620.f, main->Size.y/460.f, main->DpiScale}));
    ImGuiID focusedId=0;
    auto* focused=FocusedStackWindow(&focusedId);
    const Stack::Workspace::HoldInput input{
        Down(focused,GLFW_KEY_LEFT_ALT), Down(focused,GLFW_KEY_RIGHT_ALT),
        Down(focused,GLFW_KEY_LEFT_CONTROL), Down(focused,GLFW_KEY_TAB),
        Down(focused,GLFW_KEY_F4), Down(focused,GLFW_KEY_ESCAPE), focused && !m_CloseRequested};
    const bool alt=input.Alt(), altGr=input.AltGr(), systemChord=input.SystemChord();
    const auto action=Stack::Workspace::ResolveHoldAction(input,sw.held,
        m_WorkspaceAltWasDown || m_WorkspaceAltSuppressed);
    m_WorkspaceFinishing=false;
    if(!alt) m_WorkspaceAltSuppressed=false;
    if(focused==m_Window && !sw.Visible() && !m_WorkspaceFinishPending)
        m_WorkspaceMainCursor=io.MousePos;
    if(sw.held) {
        if(action==Stack::Workspace::HoldAction::CancelWithoutFocus) EndWorkspaceSwitcher(false,false);
        else if(action==Stack::Workspace::HoldAction::Cancel) EndWorkspaceSwitcher(false);
        else {
            double x,y; glfwGetCursorPos(m_Window,&x,&y);
            sw.Move(static_cast<float>(x-m_WorkspacePointerX),static_cast<float>(y-m_WorkspacePointerY));
            m_WorkspacePointerX=x; m_WorkspacePointerY=y;
            if(action==Stack::Workspace::HoldAction::Commit) EndWorkspaceSwitcher(true);
        }
    } else if(m_WorkspaceFinishPending) {
        m_WorkspaceFinishPending=false;
        // A field that retained keyboard ownership did not accept Enter.
        const auto& g=*ImGui::GetCurrentContext();
        const bool editingText=g.ActiveId && g.InputTextState.ID==g.ActiveId;
        if(alt && focused && !systemChord && !altGr && !editingText && m_Editor->FinishWorkspaceInteraction()) {
            ImGui::ClearActiveID();
            Stack::Workspace::CloseOrdinaryPopups();
            m_SettingsPopupOpen=false; m_SettingsPopupOpenedAt=0;
            ReleaseLockedScrubCursor(false);
            glfwFocusWindow(m_Window);
            sw.Begin(m_CurrentTabId);
            glfwSetInputMode(m_Window,GLFW_CURSOR,GLFW_CURSOR_DISABLED);
            if(glfwRawMouseMotionSupported()) glfwSetInputMode(m_Window,GLFW_RAW_MOUSE_MOTION,GLFW_TRUE);
            glfwGetCursorPos(m_Window,&m_WorkspacePointerX,&m_WorkspacePointerY);
            m_WorkspaceDiscardMouse=true;
        } else m_WorkspaceAltSuppressed=alt;
    } else if(action==Stack::Workspace::HoldAction::Open) {
        const bool blocked=m_CloseRequested || !m_StartupRevealVisual.AllowsInput() || m_ShowLegalGateReview ||
            (m_LegalManager && !m_LegalManager->IsAccepted()) ||
            ActiveProjectLoadPhase()!=LibraryToEditorProjectLoadPhase::None || m_RootTabBodyFadeActive ||
            m_ShowEditorSavePrompt || m_ShowEditorNamePrompt || m_ShowRawWorkspaceSwitchPrompt ||
            m_ShowUnnamedEditorClosePrompt || m_ShowOpenProjectPrompt || m_ShowFileDispositionPrompt ||
            ImGui::GetTopMostPopupModal()!=nullptr;
        if(blocked) m_WorkspaceAltSuppressed=true;
        else {
            Stack::Workspace::CloseOrdinaryPopups();
            m_SettingsPopupOpen=false; m_SettingsPopupOpenedAt=0;
            m_WorkspaceOriginViewport=focusedId; m_WorkspaceOriginCursor=io.MousePos;
            m_WorkspaceFinishPending=true; m_WorkspaceFinishing=true;
        }
    }
    m_WorkspaceAltWasDown=alt;
    sw.Tick(io.DeltaTime);
    if (sw.Visible()) m_Editor->BeginWorkspacePreview();
    else m_Editor->EndWorkspacePreview();
    if(m_WorkspaceDiscardMouse && !sw.Visible() && !alt) {
        bool any=false;
        for(int i=0;i<3;++i) any |= glfwGetMouseButton(m_Window,i)==GLFW_PRESS || io.MouseReleased[i];
        if(!any) m_WorkspaceDiscardMouse=false;
    }
}
void AppShell::EndWorkspaceSwitcher(bool commit, bool restoreFocus) {
    auto& sw=m_WorkspaceSwitcher;
    if(!sw.held) return;
    commit = commit && sw.selected >= 0;
    const int destination=sw.End(commit);
    m_Editor->EndWorkspacePreview();
    if(commit && destination!=m_CurrentTabId) {
        if(CanChangeRootTab(m_CurrentTabId,destination)) {
            OnTabChanged(m_CurrentTabId,destination); m_CurrentTabId=destination;
        } else { commit=false; sw.preview=m_CurrentTabId; }
    }
    m_WorkspaceAltSuppressed=true;
    if(glfwRawMouseMotionSupported()) glfwSetInputMode(m_Window,GLFW_RAW_MOUSE_MOTION,GLFW_FALSE);
    glfwSetInputMode(m_Window,GLFW_CURSOR,GLFW_CURSOR_NORMAL);
    if(!restoreFocus) return;
    auto* origin=ImGui::FindViewportByID(m_WorkspaceOriginViewport);
    auto* main=ImGui::GetMainViewport();
    auto* target=(!commit && origin) ? origin : main;
    auto* window=static_cast<GLFWwindow*>(target->PlatformHandle);
    if(!window) return;
    ImVec2 p=(!commit || m_WorkspaceOriginViewport==main->ID) ? m_WorkspaceOriginCursor : m_WorkspaceMainCursor;
    p.x=std::clamp(p.x,target->Pos.x,target->Pos.x+std::max(1.f,target->Size.x)-1);
    p.y=std::clamp(p.y,target->Pos.y,target->Pos.y+std::max(1.f,target->Size.y)-1);
    glfwFocusWindow(window); glfwSetCursorPos(window,p.x-target->Pos.x,p.y-target->Pos.y);
}
void AppShell::DrawWorkspaceSwitcher() {
    if (auto* list=Stack::Workspace::DrawSwitcher(m_WorkspaceSwitcher,ImGui::GetMainViewport()))
        m_WorkspaceCompositor.KeepFixed(list);
}
