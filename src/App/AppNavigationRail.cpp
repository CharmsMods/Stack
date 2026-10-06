#include "AppShell.h"
#include "AppHeaderStyle.h"
#include "EdgeChrome.h"
#include "WorkspaceInputScope.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <utility>
#include <array>
#include <cstring>

namespace {
constexpr int Library = 0, Graph = 1, RawTab = 5, Queue = 7;
bool IsRaw(int tab) { return tab == RawTab || tab == 3; }
}

void AppShell::ApplyRailNavigation() {
    auto& rail = m_NavigationRail;
    if (rail.requestedDestination < 0 || !CanSwitchProjectWorkspace()) return;
    const int destination = std::exchange(rail.requestedDestination, -1);
    CancelProjectPillPreview();
    if (destination == Library && rail.galleryView) {
        ActivateProjectWorkspace(m_GalleryWorkspaceId);
        return;
    }
    if ((destination == RawTab || destination == Graph) && m_ActiveProjectWorkspace == m_GalleryWorkspaceId) {
        auto* target = FindProjectWorkspace(rail.lastEditingWorkspace);
        if (!target || target->id == m_GalleryWorkspaceId) {
            for (const auto& workspace : m_ProjectWorkspaces)
                if (workspace->id != m_GalleryWorkspaceId) { target = workspace.get(); break; }
        }
        const auto id = target ? target->id : CreateProjectWorkspace();
        if (!ActivateProjectWorkspace(id)) return;
    }
    RequestTabSwitch(destination);
}

void AppShell::LayoutNavigationRail() {
    auto& rail = m_NavigationRail;
    if (m_Editor->ConsumeRawSettingsPanelRequest()) rail.panelOpen = true;
    auto* viewport = ImGui::GetMainViewport();
    rail.scale = std::max(.75f, ImGui::GetFontSize() / 16.f);
    const bool toggleWasTop = TopBarOwnsPanelToggle();
    LayoutRawMaskToolbar();
    const float width = Stack::Navigation::BarExtent * rail.scale;
    const auto& io = ImGui::GetIO();
    const bool focused = glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) == GLFW_TRUE;
    const bool inHeight = io.MousePos.y >= viewport->Pos.y &&
        io.MousePos.y < viewport->Pos.y + viewport->Size.y;
    const bool atEdge = inHeight && io.MousePos.x >= viewport->Pos.x &&
        io.MousePos.x <= viewport->Pos.x + 2.f * rail.scale;
    const bool overRail = inHeight && rail.revealAmount > .001f &&
        io.MousePos.x >= viewport->Pos.x && io.MousePos.x < viewport->Pos.x + width * rail.revealAmount;
    const auto* context = ImGui::GetCurrentContext();
    const auto belongsToRail = [](const ImGuiWindow* window) {
        return window && std::strcmp(window->RootWindow->Name, "StackNavigationRail") == 0;
    };
    const auto belongsToToggle = [&](const ImGuiWindow* window) {
        return !toggleWasTop && window &&
            std::strcmp(window->RootWindow->Name, "StackPanelToggle") == 0;
    };
    const bool railActive = (context->ActiveId && (belongsToRail(context->ActiveIdWindow) || belongsToToggle(context->ActiveIdWindow))) ||
        (io.NavVisible && (belongsToRail(context->NavWindow) || belongsToToggle(context->NavWindow)));
    const bool railPopup = rail.menuOpen || m_NotificationPresenter.PanelOpen();
    Stack::Navigation::UpdateEdgeReveal(rail.revealAmount, rail.visibleUntil, focused,
        atEdge, overRail, railActive, railPopup, Stack::Notifications::SystemReducedMotion());
    rail.visibleWidth = width * rail.revealAmount;
    rail.position = ImVec2(viewport->Pos.x - width + rail.visibleWidth, viewport->Pos.y);
    const float available = std::max(0.f, viewport->Size.x - rail.visibleWidth);
    const float panelWidth = std::min(360.f * rail.scale, available * .38f);
    const float target = rail.panelOpen ? 1.f : 0.f;
    if (Stack::Notifications::SystemReducedMotion()) rail.panelAmount = target;
    else Stack::Header::Approach(rail.panelAmount, target, 16.f);
    rail.visiblePanelWidth = panelWidth * rail.panelAmount;
    // Each animated column reserves its visible width. Revealing the rail
    // shifts the panel and narrows the workspace without changing panel state.
    const float toolbarHeight = m_RawMaskToolbar.visibleHeight;
    rail.panelPosition = ImVec2(viewport->Pos.x + rail.visibleWidth, viewport->Pos.y + toolbarHeight);
    rail.panelSize = ImVec2(panelWidth, std::max(1.f, viewport->Size.y - toolbarHeight));
    const float top = std::max(toolbarHeight, Stack::Header::CaptionHeight * rail.scale);
    rail.bodyPosition = ImVec2(rail.panelPosition.x + rail.visiblePanelWidth, viewport->Pos.y + top);
    rail.bodySize = ImVec2(std::max(1.f, available - rail.visiblePanelWidth), std::max(1.f, viewport->Size.y - top));
    const ImVec2 toggleOrigin = TopBarOwnsPanelToggle() ? m_RawMaskToolbar.position : rail.position;
    m_RawMaskToolbar.panelTogglePosition = ImVec2(toggleOrigin.x + Stack::Navigation::BarPadding * rail.scale,
        toggleOrigin.y + Stack::Navigation::BarPadding * rail.scale);
}

void AppShell::RenderNavigationRail(bool openFileMenu, bool openSettings, bool openActivity) {
    auto& rail = m_NavigationRail;
    auto* viewport = ImGui::GetMainViewport();
    const float s = rail.scale, width = Stack::Navigation::BarExtent * s, targetSize = Stack::Navigation::ButtonExtent * s;
    const auto palette = Stack::Header::ResolvePalette(m_Appearance ? m_Appearance->GetClearColor()
        : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::SetNextWindowPos(rail.position);
    ImGui::SetNextWindowSize(ImVec2(width, viewport->Size.y));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImVec4 railTint = palette.caption;
    railTint.w = Stack::Header::ResolveSectionTintOpacity(rail.panelAmount);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, railTint);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7.f * s, 7.f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 6.f * s));
    ImGui::Begin("StackNavigationRail", nullptr, ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground |
        (rail.revealAmount <= .001f ? ImGuiWindowFlags_NoInputs : ImGuiWindowFlags_None));
    auto* draw = ImGui::GetWindowDrawList();
    // The top bar owns the overlap's tint. Paint the rail below it once.
    draw->AddRectFilled(ImVec2(rail.position.x, viewport->Pos.y + m_RawMaskToolbar.visibleHeight),
        ImVec2(rail.position.x + width, viewport->Pos.y + viewport->Size.y), ImGui::GetColorU32(railTint));
    const auto button = [&](const char* label, Stack::Navigation::Glyph glyph, bool selected, bool enabled = true) {
        return Stack::Navigation::IconButton(label, glyph, selected, enabled, s, palette);
    };
    using Stack::Navigation::Glyph;
    // Both bars reserve this slot; the shared sibling owns the only hit target.
    ImGui::Dummy(ImVec2(targetSize, targetSize));
    if (button("Stack menu", Glyph::Stack, false) || openFileMenu) {
        CancelProjectPillPreview();
        ImGui::OpenPopup("GlobalFileMenu");
    }
    rail.menuOpen = ImGui::IsPopupOpen("GlobalFileMenu");
    if (rail.menuOpen) ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + width + 6.f * s,
        viewport->Pos.y + 50.f * s), ImGuiCond_Appearing);
    RenderStackMenu();
    if (openSettings) {
        m_SettingsPopupOpen = true;
        m_SettingsPopupOpenedAt = ImGui::GetTime();
    }
    ImGui::Dummy(ImVec2(0, 8.f * s));
    const bool gallery = m_ActiveProjectWorkspace == m_GalleryWorkspaceId && IsRaw(m_CurrentTabId);
    if (gallery) rail.galleryView = true;
    if (m_CurrentTabId == Library) rail.galleryView = false;
    const bool available = CanSwitchProjectWorkspace();
    const std::array<std::pair<int, Glyph>, 4> destinations{{
        {RawTab, Glyph::Raw}, {Graph, Glyph::Graph}, {Library, Glyph::Library}, {Queue, Glyph::Queue}}};
    for (const auto& item : destinations) {
        const char* name = item.first == RawTab ? "RAW" : item.first == Graph ? "Graph" : item.first == Library ? "Library" : "Queue";
        const bool selected = item.first == Library ? m_CurrentTabId == Library || gallery
            : item.first == RawTab ? IsRaw(m_CurrentTabId) && !gallery : m_CurrentTabId == item.first;
        if (button(name, item.second, selected, available)) {
            rail.requestedDestination = item.first;
            CancelProjectPillPreview();
        }
    }
    ImGui::Dummy(ImVec2(0, 10.f * s));
    const ImVec2 pills = ImGui::GetCursorScreenPos();
    const float footerHeight = 48.f * s;
    const ImVec2 pillsSize(targetSize, std::max(1.f, viewport->Pos.y + viewport->Size.y - footerHeight - pills.y));
    RenderProjectPills(pills, pillsSize);
    ImGui::SetCursorScreenPos(ImVec2(rail.position.x + 7.f * s,
        viewport->Pos.y + viewport->Size.y - 41.f * s));
    RenderActivityIndicator(targetSize, 32.f * s, openActivity);
    m_WorkspaceCompositor.KeepChrome(draw);
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
}

void AppShell::RenderLibraryViewSwitch() {
    auto& rail = m_NavigationRail;
    if (m_CurrentTabId != Library && !(m_ActiveProjectWorkspace == m_GalleryWorkspaceId && IsRaw(m_CurrentTabId))) return;
    const bool gallery = m_CurrentTabId != Library;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.f * rail.scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.f * rail.scale, 7.f * rail.scale));
    ImGui::BeginDisabled(!CanSwitchProjectWorkspace());
    for (int view = 0; view < 2; ++view) {
        if (view) ImGui::SameLine(0, 4.f * rail.scale);
        ImGui::PushStyleColor(ImGuiCol_Button, (view == 1) == gallery
            ? ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered) : ImVec4(0, 0, 0, 0));
        if (ImGui::Button(view ? "Gallery" : "Projects")) {
            rail.galleryView = view != 0;
            rail.requestedDestination = Library;
        }
        ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar(3);
    ImGui::Dummy(ImVec2(0, 10.f * rail.scale));
}

void AppShell::RenderWorkspaceSectionPanel(int renderTab) {
    const auto& rail = m_NavigationRail;
    m_Editor->SetGraphCatalogHost(renderTab == Graph ? rail.panelPosition : ImVec2(),
        renderTab == Graph ? rail.panelSize : ImVec2(), rail.panelOpen, rail.visiblePanelWidth);
    if (IsRaw(renderTab)) {
        m_Editor->RenderRawWorkspaceSectionPanel(rail.panelPosition, rail.panelSize, rail.visiblePanelWidth);
        return;
    }
    if (renderTab == Graph || rail.visiblePanelWidth < 1.f) return;
    ImGui::SetNextWindowPos(rail.panelPosition);
    ImGui::SetNextWindowSize(ImVec2(rail.visiblePanelWidth, rail.panelSize.y));
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Stack::Header::ResolvePanelColor(m_Appearance
        ? m_Appearance->GetClearColor() : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg),
        Stack::Header::ResolveSectionTintOpacity(rail.panelAmount)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::Begin("StackWorkspacePanel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.f * rail.scale, 18.f * rail.scale));
    ImGui::BeginChild("Contents", rail.panelSize, ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoBackground);
    if (renderTab == Library) m_Library.RenderSectionPanel();
    else if (renderTab == Queue) m_Queue.RenderSectionPanel();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    // The full-screen workspace must stay behind this sibling window and its
    // children. Change display order without stealing keyboard focus or
    // putting the panel over an open menu/modal.
    if (ImGui::GetCurrentContext()->OpenPopupStack.empty())
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
}
