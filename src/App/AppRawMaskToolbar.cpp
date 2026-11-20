#include "AppShell.h"
#include "Utils/HoverTextScope.h"
#include "UiPerformanceTrace.h"
#include "UiPerformanceProbe.h"
#include "EdgeChrome.h"
#include "WorkspacePresentation.h"
#include "WorkspaceInputScope.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>

namespace {
constexpr const char* ToolbarWindow = "StackRawMaskToolbar";
constexpr const char* ToggleWindow = "StackPanelToggle";

bool BelongsTo(const ImGuiWindow* window, const char* name) {
    return window && std::strcmp(window->RootWindow->Name, name) == 0;
}

} // namespace

void AppShell::ToggleWorkspaceSectionPanel() {
    if(m_Editor->IsFocusedNodeEditor())return;
    if (m_Editor->IsBracketingPresentationActive() || m_Editor->IsAutoBracketWorkspace()) return;
    m_NavigationRail.panelOpen = !m_NavigationRail.panelOpen;
    // Buttons render after layout; arm at acceptance so the next layout can
    // already move, without charging time spent stalled before this click.
    m_NavigationRail.panelMotion.Update(m_NavigationRail.panelAmount,m_NavigationRail.panelOpen?1.f:0.f,.28f,
        Stack::Animation::FrameTween::ClockSeconds(),ImGui::GetFrameCount(),Stack::Notifications::SystemReducedMotion());
    if (m_CurrentTabId == 1) m_Editor->SetWorkspacePanelOpen(m_NavigationRail.panelOpen);
    else if (m_CurrentTabId==3 || m_CurrentTabId==5) m_Editor->SetViewSectionPanelOpen(m_NavigationRail.panelOpen);
    CancelProjectPillPreview();
}

void AppShell::LayoutRawMaskToolbar() {
    auto& t=m_RawMaskToolbar;
    const auto* v=ImGui::GetMainViewport();const auto& io=ImGui::GetIO();
    const auto scale=m_NavigationRail.scale;
    t.enabled=((m_CurrentTabId==1 && m_Editor->IsViewMode()) || m_CurrentTabId==3 || m_CurrentTabId==5) &&
        m_ActiveProjectWorkspace!=m_GalleryWorkspaceId && !m_Editor->IsRawWorkspaceGalleryWorkspaceOpen() &&
        !m_Editor->IsFocusedNodeEditor() && !m_Editor->IsBracketingPresentationActive() && !m_Editor->IsAutoBracketWorkspace();
    if(!t.enabled || io.AppFocusLost) {
        if(m_Editor->MaskAuthoringActive())m_Editor->CancelMaskAuthoring();
        t.revealAmount=0;t.revealMotion.Reset(0);t.menuOpen=false;return;
    }
    if(m_Editor->MaskAuthoringActive() && ImGui::IsKeyPressed(ImGuiKey_Escape,false) &&
        !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)) {
        m_Editor->CancelMaskAuthoring();
        ImGui::SetKeyOwner(ImGuiKey_Escape,ImHashStr("ViewMaskAuthoring"),ImGuiInputFlags_LockThisFrame);
    }
    const float right=v->Pos.x+v->Size.x;
    const float top=v->Pos.y+Stack::Header::CaptionHeight*scale+5*scale;
    const float width=44*scale;
    t.height=std::max(1.f,std::min(8*39.f*scale+12*scale,v->Size.y-(top-v->Pos.y)-8*scale));
    const bool safeY=io.MousePos.y>=top && io.MousePos.y<v->Pos.y+v->Size.y-6*scale;
    const bool edge=safeY && io.MousePos.x>=right-9*scale && io.MousePos.x<=right && !ImGui::IsAnyMouseDown();
    const bool hovered=t.revealAmount>.001f && io.MousePos.x>=right-width*t.revealAmount-5*scale &&
        io.MousePos.x<=right && io.MousePos.y>=top && io.MousePos.y<=top+t.height;
    Stack::Navigation::UpdateEdgeReveal(t.revealAmount,t.revealMotion,t.visibleUntil,true,edge,hovered,false,t.menuOpen,
        Stack::Notifications::SystemReducedMotion());
    t.position={right-(width+5*scale)*t.revealAmount,top};
}

void AppShell::RenderRawMaskToolbar() {
    auto& t=m_RawMaskToolbar;
    if(!t.enabled || t.revealAmount<=.001f)return;
    const float scale=m_NavigationRail.scale;
    ImGui::SetNextWindowPos(t.position);ImGui::SetNextWindowSize({44*scale,t.height});
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{3*scale,4*scale});
    if(ImGui::Begin(ToolbarWindow,nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::BeginDisabled(ActiveProjectLoadPhase()!=LibraryToEditorProjectLoadPhase::None || m_RootTabBodyFadeActive ||
            m_Editor->IsNodeOpenTransitionActive() || m_NavigationRail.previewAmount>.001f || Stack::Workspace::IsPreview());
        m_Editor->RenderMaskToolRail(scale);
        ImGui::EndDisabled();
        t.menuOpen=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        m_WorkspaceCompositor.KeepChrome(ImGui::GetWindowDrawList());
    }
    ImGui::End();ImGui::PopStyleVar();
}

void AppShell::RenderSharedPanelToggle() {
    if(m_Editor->IsFocusedNodeEditor())return;
    ImGuiExtras::HoverTextScope noHoverText;
    if (m_Editor->IsBracketingPresentationActive() || m_Editor->IsAutoBracketWorkspace()) return;
    const float amount = m_NavigationRail.revealAmount;
    const float scale = m_NavigationRail.scale;
    const float toggleWidth = Stack::Navigation::ButtonExtent*scale;
    const auto palette = Stack::Header::ResolvePalette(m_Editor ? m_Editor->GetWorkspaceBaseColor()
        : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::SetNextWindowPos(m_RawMaskToolbar.panelTogglePosition);
    ImGui::SetNextWindowSize(ImVec2(std::max(1.f,toggleWidth), Stack::Navigation::ButtonExtent * scale));
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::Begin(ToggleWindow, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing |
        (amount <= .001f || m_Editor->IsReturningToGraph() ? ImGuiWindowFlags_NoInputs : ImGuiWindowFlags_None));
    Stack::Workspace::InputScope transitionInput(m_Editor->IsReturningToGraph(), false);
    if (Stack::Navigation::IconButton(m_NavigationRail.panelOpen ? "Close side panel###PanelToggle" : "Open side panel###PanelToggle",
            Stack::Navigation::Glyph::Panel, m_NavigationRail.pinnedOpen, true, scale, palette, m_NavigationRail.panelToggleMotion, nullptr,
            false, nullptr, false, {}))
        ToggleWorkspaceSectionPanel();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        m_NavigationRail.pinnedOpen = !m_NavigationRail.pinnedOpen;
        ImGui::MarkIniSettingsDirty();
        if (m_NavigationRail.pinnedOpen && !m_NavigationRail.panelOpen)
            ToggleWorkspaceSectionPanel();
    }
    if (ImGui::IsItemHovered()) CancelProjectPillPreview();
    m_WorkspaceCompositor.KeepChrome(ImGui::GetWindowDrawList());
    ImGui::End();
    ImGui::PopStyleVar(2);
}

bool AppShell::RawMaskToolbarOwnsInput() const {
    const auto& toolbar = m_RawMaskToolbar;
    const auto* context = ImGui::GetCurrentContext();
    // The shared toggle also suppresses canvas polling when the left bar owns
    // it and a held pointer has moved beyond that bar's rectangle.
    if (context->ActiveId && BelongsTo(context->ActiveIdWindow, ToggleWindow)) return true;
    if (!toolbar.enabled) return false;
    if(toolbar.menuOpen)return true;
    const ImRect area(toolbar.position,{toolbar.position.x+44*m_NavigationRail.scale,toolbar.position.y+toolbar.height});
    return (toolbar.revealAmount>.001f && area.Contains(ImGui::GetIO().MousePos)) ||
        (context->ActiveId && BelongsTo(context->ActiveIdWindow,ToolbarWindow));
}
