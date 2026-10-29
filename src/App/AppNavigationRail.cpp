#include "AppShell.h"
#include "UiPerformanceTrace.h"
#include "UiPerformanceProbe.h"
#include "AppHeaderStyle.h"
#include "EdgeChrome.h"
#include "WorkspaceInputScope.h"
#include "WorkspacePresentation.h"
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

void AppShell::QueueRailNavigation(int destination) {
    auto& rail = m_NavigationRail;
    m_GalleryNavigationMotion->NewIntent();
    const auto* owner = FindProjectWorkspace(m_ActiveProjectWorkspace);
    rail.requestedDestination = owner ? destination : -1;
    rail.requestedWorkspace = m_ActiveProjectWorkspace;
    rail.requestedLoadGeneration = owner ? owner->loadTransition.generation : 0;
}

void AppShell::ApplyRailNavigation() {
    auto& rail = m_NavigationRail;
    if (rail.requestedDestination < 0) return;
    const auto* owner = FindProjectWorkspace(rail.requestedWorkspace);
    if (!owner || owner->id != m_ActiveProjectWorkspace ||
        owner->loadTransition.generation != rail.requestedLoadGeneration) {
        rail.requestedDestination = -1;
        return;
    }
    // Keep the latest user destination separate from load-owned Graph/View
    // requests. A canceled or replaced load cannot replay its old navigation.
    if (!CanSwitchProjectWorkspace() ||
        (owner->loadTransition.phase != ProjectLoadPhase::None &&
         !(rail.requestedDestination==Library && m_GalleryNavigationMotion->Owns(owner->id)))) return;
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
    const bool processing = m_Editor->IsBracketingPresentationActive() || m_Editor->IsAutoBracketWorkspace();
    if (m_CurrentTabId == Graph) m_NavigationRail.panelOpen = m_Editor->WorkspacePanelOpen();
    auto& rail = m_NavigationRail;
    if (IsRaw(m_CurrentTabId) && !m_Editor->IsRawWorkspaceGalleryWorkspaceOpen())
        rail.panelOpen=m_Editor->ViewSectionPanelOpen();
    if (m_Editor->ConsumeRawSettingsPanelRequest()) {
        rail.panelOpen = true;
        if (m_CurrentTabId == Graph) m_Editor->SetWorkspacePanelOpen(true);
        else if (IsRaw(m_CurrentTabId)) m_Editor->SetViewSectionPanelOpen(true);
    }
    if (m_Editor->IsReturningToGraph()) rail.panelOpen = false;
    auto* viewport = ImGui::GetMainViewport();
    rail.scale = std::max(.75f, ImGui::GetFontSize() / 16.f);
    const bool toggleWasTop = TopBarOwnsPanelToggle();
    LayoutRawMaskToolbar();
    const float width = Stack::Navigation::BarExtent * rail.scale;
    const auto& io = ImGui::GetIO();
    const bool focused = m_UiPerformanceCapture || glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) == GLFW_TRUE;
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
        (Stack::Navigation::NavigationInputActive() && (belongsToRail(context->NavWindow) || belongsToToggle(context->NavWindow)));
    const bool railPopup = rail.menuOpen || m_NotificationPresenter.PanelOpen();
    Stack::Navigation::UpdateEdgeReveal(rail.revealAmount, rail.visibleUntil, focused,
        atEdge, overRail, railActive, railPopup, Stack::Notifications::SystemReducedMotion());
    rail.visibleWidth = width * rail.revealAmount;
    rail.position = ImVec2(viewport->Pos.x - width + rail.visibleWidth,
        viewport->Pos.y);
    const float available = std::max(0.f, viewport->Size.x - rail.visibleWidth);
    const bool gallery = m_ActiveProjectWorkspace == m_GalleryWorkspaceId && IsRaw(m_CurrentTabId);
    const bool queue = m_CurrentTabId == Queue;
    if (rail.galleryResizeActive && ((rail.galleryResizeForQueue ? !queue : !gallery) ||
            !rail.panelOpen || !io.MouseDown[0] || Stack::Workspace::IsPreview()))
        Stack::Navigation::FinishGalleryNavigatorResize(rail);
    const float panelWidth = std::min((gallery ? rail.galleryPanelWidth : queue ? rail.queuePanelWidth : 360.f) * rail.scale, available * .38f);
    const float target = !processing && rail.panelOpen ? 1.f : 0.f;
    if (processing || Stack::Notifications::SystemReducedMotion()) rail.panelAmount = target;
    else Stack::Header::ApproachMotion(rail.panelAmount, target, .28f);
    rail.visiblePanelWidth = panelWidth * rail.panelAmount;
    // Each animated column reserves its visible width. Revealing the rail
    // shifts the panel and narrows the workspace without changing panel state.
    const float toolbarHeight = m_RawMaskToolbar.visibleHeight;
    rail.panelPosition = ImVec2(viewport->Pos.x + rail.visibleWidth, viewport->Pos.y + toolbarHeight);
    rail.panelSize = ImVec2(panelWidth, std::max(1.f, viewport->Size.y - toolbarHeight));
    const bool graphBody = m_CurrentTabId == Graph && !m_Editor->IsViewMode();
    const float top = processing ? 0.f : graphBody ? toolbarHeight : std::max(toolbarHeight, Stack::Header::CaptionHeight * rail.scale);
    rail.bodyPosition = ImVec2(rail.panelPosition.x + rail.visiblePanelWidth, viewport->Pos.y + top);
    rail.bodySize = ImVec2(std::max(1.f, available - rail.visiblePanelWidth), std::max(1.f, viewport->Size.y - top));
    m_GalleryNavigationMotion->Viewport(m_ActiveProjectWorkspace,
        {rail.bodyPosition,{rail.bodyPosition.x+rail.bodySize.x,rail.bodyPosition.y+rail.bodySize.y}});
    m_Editor->SetGraphReturnBounds(
        ImVec2(viewport->Pos.x + rail.visibleWidth, viewport->Pos.y + toolbarHeight),
        ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y));
    // Blend the two reveal paths at their shared corner. Switching the input
    // owner must not teleport the button between horizontal and vertical paths.
    const float left = rail.revealAmount, topReveal = m_RawMaskToolbar.revealAmount;
    const float total = left + topReveal;
    const float hidden = width * (1.f - std::max(left, topReveal));
    const float leftWeight = total > 0.f ? left / total : 1.f;
    m_RawMaskToolbar.panelTogglePosition = ImVec2(
        viewport->Pos.x - (hidden * leftWeight) + Stack::Navigation::BarPadding * rail.scale,
        viewport->Pos.y - hidden * (1.f - leftWeight) + Stack::Navigation::BarPadding * rail.scale);
}

void AppShell::RenderNavigationRail(bool openFileMenu, bool openSettings, bool openActivity) {
    Stack::UiPerformance::Span timing(Stack::UiPerformance::Stage::NavigationBars);
    Stack::UiPerformance::Probe::Drawn(Stack::UiPerformance::Probe::Visual::Rail,
        Stack::UiPerformance::Probe::Key(m_NavigationRail.revealAmount));
    auto& rail = m_NavigationRail;
    auto* viewport = ImGui::GetMainViewport();
    const float s = rail.scale, width = Stack::Navigation::BarExtent * s, targetSize = Stack::Navigation::ButtonExtent * s;
    const auto palette = Stack::Header::ResolvePalette(m_Editor ? m_Editor->GetWorkspaceBaseColor()
        : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::SetNextWindowPos(rail.position);
    const float drawnWidth = width;
    ImGui::SetNextWindowSize(ImVec2(std::max(1.f,drawnWidth), viewport->Size.y));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImVec4 railTint = palette.caption;
    railTint.w = 1.f;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, railTint);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7.f * s, 7.f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 6.f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.f,1.f));
    ImGui::Begin("StackNavigationRail", nullptr, ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground |
        (rail.revealAmount <= .001f ? ImGuiWindowFlags_NoInputs : ImGuiWindowFlags_None));
    auto* draw = ImGui::GetWindowDrawList();
    // The sidebar is a visible native surface, independent of optional point optics.
    draw->AddRectFilled(ImVec2(rail.position.x,viewport->Pos.y+m_RawMaskToolbar.visibleHeight),
        ImVec2(rail.position.x+drawnWidth,viewport->Pos.y+viewport->Size.y),ImGui::GetColorU32(railTint));
    const auto button = [&](const char* label, Stack::Navigation::Glyph glyph, bool selected, Stack::Animation::ControlMotion& motion, bool enabled = true) {
        return Stack::Navigation::IconButton(label, glyph, selected, enabled, s, palette, motion);
    };
    using Stack::Navigation::Glyph;
    // Both bars reserve this slot; the shared sibling owns the only hit target.
    ImGui::Dummy(ImVec2(targetSize, targetSize));
    if (button("Stack menu", Glyph::Stack, false, rail.stackMenuMotion) || openFileMenu) {
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
    const std::array<std::pair<int, Glyph>, 2> destinations{{
        {Library, Glyph::Library}, {Queue, Glyph::Queue}}};
    for (const auto& item : destinations) {
        const char* name = item.first == RawTab ? "RAW" : item.first == Graph ? "Graph" : item.first == Library ? "Library" : "Queue";
        const bool selected = item.first == Library ? m_CurrentTabId == Library || gallery
            : item.first == RawTab ? IsRaw(m_CurrentTabId) && !gallery : m_CurrentTabId == item.first;
        if (button(name, item.second, selected, item.first == Library ? rail.libraryMotion : rail.queueMotion, available)) {
            QueueRailNavigation(item.first);
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
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor();
}

void AppShell::RenderLibraryViewSwitch() {
    auto& rail = m_NavigationRail;
    if (m_CurrentTabId != Library && !(m_ActiveProjectWorkspace == m_GalleryWorkspaceId && IsRaw(m_CurrentTabId))) return;
    const bool gallery = m_CurrentTabId != Library;
    if (gallery) ImGui::PushFont(nullptr, 14.f * rail.scale);
    const int view = Stack::LibraryUi::ViewSwitch(gallery, rail.libraryViewMotion,
        CanSwitchProjectWorkspace(), gallery ? rail.scale : 0.f);
    if (gallery) ImGui::PopFont();
    if (view >= 0 && (view != 0) != gallery) {
        rail.galleryView = view != 0;
        QueueRailNavigation(Library);
    }
    ImGui::Dummy(ImVec2(0, (gallery ? 16.f : 10.f) * rail.scale));
}

void AppShell::RenderWorkspaceSectionPanel(int renderTab) {
    if (m_Editor->IsBracketingPresentationActive() || m_Editor->IsAutoBracketWorkspace()) return;
    const auto& rail = m_NavigationRail;
    Stack::UiPerformance::Probe::Drawn(Stack::UiPerformance::Probe::Visual::Panel,
        Stack::UiPerformance::Probe::Key(rail.visiblePanelWidth));
    const bool graphMode = renderTab == Graph && !m_Editor->IsViewMode();
    m_Editor->SetGraphCatalogHost(graphMode ? rail.panelPosition : ImVec2(),
        graphMode ? rail.panelSize : ImVec2(), rail.panelOpen, rail.visiblePanelWidth);
    if (IsRaw(renderTab) || (renderTab == Graph && m_Editor->IsViewMode())) {
        m_Editor->RenderRawWorkspaceSectionPanel(rail.panelPosition, rail.panelSize, rail.visiblePanelWidth,
            [this] { RenderLibraryViewSwitch(); });
        if (m_ActiveProjectWorkspace == m_GalleryWorkspaceId && rail.panelAmount == 1.f &&
            !Stack::Workspace::IsPreview()) Stack::Navigation::RenderGalleryNavigatorResize(m_NavigationRail);
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
    const bool queue = renderTab == Queue;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
        queue ? ImVec2(12.f * rail.scale, 16.f * rail.scale) : ImVec2(16.f * rail.scale, 18.f * rail.scale));
    if (queue) ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
    if (ImGui::BeginChild("Contents", queue ? ImVec2(0, 0) : rail.panelSize, ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoBackground | (queue ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0))) {
        if (renderTab == Library) {
            RenderLibraryViewSwitch();
            m_Library.RenderSectionPanel();
        }
        else if (queue) m_Queue.RenderSectionPanel();
    }
    ImGui::EndChild();
    if (queue) ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    // The full-screen workspace must stay behind this sibling window and its
    // children. Change display order without stealing keyboard focus or
    // putting the panel over an open menu/modal.
    if (ImGui::GetCurrentContext()->OpenPopupStack.empty())
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
    if (queue && rail.panelAmount == 1.f && !Stack::Workspace::IsPreview())
        Stack::Navigation::RenderQueueNavigatorResize(m_NavigationRail);
}
