#include "AppShell.h"
#include "AppHeaderStyle.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <string>

void AppShell::CancelProjectPillPreview() {
    auto& rail = m_NavigationRail;
    rail.suppressedPreview = rail.hoveredProject;
    rail.hoveredProject = rail.previewProject = 0;
    rail.previewRootView = -1;
    rail.previewAmount = 0.f;
    rail.hoverStarted = 0;
}

void AppShell::UpdateProjectPillPreview(std::uint64_t hovered) {
    auto& rail = m_NavigationRail;
    const bool blocked = !CanSwitchProjectWorkspace() || m_SettingsPopupOpen || rail.menuOpen ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) ||
        ImGui::IsAnyMouseDown() || ImGui::IsAnyItemActive() ||
        glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) != GLFW_TRUE;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        rail.suppressedPreview = hovered;
        hovered = 0;
    }
    if (hovered != rail.suppressedPreview && hovered != 0) rail.suppressedPreview = 0;
    if (hovered == 0 && !ImGui::IsKeyPressed(ImGuiKey_Escape, false)) rail.suppressedPreview = 0;
    if (blocked || hovered == rail.suppressedPreview || hovered == m_ActiveProjectWorkspace) hovered = 0;
    if (rail.hoveredProject != hovered) {
        rail.hoveredProject = hovered;
        rail.hoverStarted = ImGui::GetTime();
    }
    const auto* workspace = hovered ? FindProjectWorkspace(hovered) : nullptr;
    const bool ready = workspace && ImGui::GetTime() - rail.hoverStarted >= .22;
    if (ready && rail.previewProject != hovered) {
        // Fade out the old view before showing another project, never substitute
        // another texture halfway through an existing crossfade.
        if (rail.previewAmount <= .001f) {
            rail.previewProject = hovered;
            rail.previewRootView = workspace->rootTab;
        }
    }
    const bool showing = ready && rail.previewProject == hovered;
    const float target = showing ? 1.f : 0.f;
    if (Stack::Notifications::SystemReducedMotion()) rail.previewAmount = target;
    else Stack::Header::Approach(rail.previewAmount, target, 24.f);
    if (blocked) rail.previewAmount = 0.f;
    if (rail.previewAmount <= .001f && !showing) {
        rail.previewProject = 0;
        rail.previewRootView = -1;
    }
}

void AppShell::RenderProjectPills(const ImVec2& position, const ImVec2& size) {
    const auto& rail = m_NavigationRail;
    const auto palette = Stack::Header::ResolvePalette(m_Appearance ? m_Appearance->GetClearColor()
        : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 3.f * rail.scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 3.f * rail.scale);
    ImGui::BeginChild("OpenProjectPills", size, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoBackground);
    const bool enabled = CanSwitchProjectWorkspace();
    std::uint64_t hovered = 0;
    ImGui::BeginDisabled(!enabled);
    for (const auto& workspace : m_ProjectWorkspaces) {
        if (workspace->id == m_GalleryWorkspaceId) continue;
        const auto id = workspace->id;
        ImGui::PushID(static_cast<int>(id));
        const bool selected = id == m_ActiveProjectWorkspace;
        std::string name = workspace->editor->GetCurrentProjectName();
        if (name.empty()) name = "Untitled project";
        const bool dirty = workspace->editor->NeedsWorkspaceSaveBeforeTransition();
        const bool clicked = ImGui::InvisibleButton(name.c_str(),
            ImVec2(size.x - 4.f * rail.scale, 24.f * rail.scale), ImGuiButtonFlags_EnableNav);
        const bool focused = ImGui::IsItemFocused();
        const bool over = ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride);
        // Preview follows pointer hover only. Navigation focus can persist
        // after opening a document, which would otherwise keep its input blocked.
        if (over) hovered = id;
        if (clicked) { CancelProjectPillPreview(); QueueWorkspaceTabAction(WorkspaceTabAction::Select, id); hovered = 0; }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) {
            CancelProjectPillPreview(); QueueWorkspaceTabAction(WorkspaceTabAction::Close, id); hovered = 0;
        }
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        const ImVec2 pillMin(min.x + 4.f * rail.scale, min.y + 5.f * rail.scale);
        const ImVec2 pillMax(max.x - 4.f * rail.scale, max.y - 5.f * rail.scale);
        ImVec4 fill = Stack::Header::Blend(palette.caption, palette.text, selected ? .56f : over ? .28f : .16f);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(pillMin, pillMax, ImGui::GetColorU32(fill), 8.f * rail.scale);
        if (dirty || focused) draw->AddRect(pillMin, pillMax,
            ImGui::GetColorU32(focused ? palette.focus : palette.mutedText), 8.f * rail.scale, 0, 1.f);
        if (over || focused) ImGui::SetTooltip("%s%s\nClick to activate. Middle-click to close.",
            name.c_str(), dirty ? " (unsaved changes)" : "");
        if (ImGui::BeginPopupContextItem("ProjectActions")) {
            ImGui::TextUnformatted(name.c_str());
            if (dirty) ImGui::TextDisabled("Unsaved changes");
            if (ImGui::MenuItem("Open workspace")) QueueWorkspaceTabAction(WorkspaceTabAction::Select, id);
            if (ImGui::MenuItem("Close project")) QueueWorkspaceTabAction(WorkspaceTabAction::Close, id);
            ImGui::EndPopup();
        }
        if (selected && m_ScrollActiveProjectTabIntoView) ImGui::SetScrollHereY(.5f);
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    m_ScrollActiveProjectTabIntoView = false;
    m_WorkspaceCompositor.KeepChrome(ImGui::GetWindowDrawList());
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    UpdateProjectPillPreview(hovered);
}

void AppShell::RenderProjectPreviewFallback() {
    const auto& rail = m_NavigationRail;
    if (!rail.previewProject || rail.previewAmount <= .001f ||
        m_WorkspaceCompositor.HasProjectPreview(rail.previewProject, rail.previewRootView)) return;
    const auto* workspace = FindProjectWorkspace(rail.previewProject);
    if (!workspace) return;
    auto* viewport = ImGui::GetMainViewport();
    auto* draw = ImGui::GetForegroundDrawList(viewport);
    ImVec4 fill = m_Appearance ? m_Appearance->GetClearColor() : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    fill.w = rail.previewAmount;
    draw->AddRectFilled(rail.panelPosition,
        ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), ImGui::GetColorU32(fill));
    ImVec4 ink = ImGui::GetStyleColorVec4(ImGuiCol_Text); ink.w *= rail.previewAmount;
    const std::string name = workspace->editor->GetCurrentProjectName();
    draw->AddText(ImVec2(rail.bodyPosition.x + 28.f, rail.bodyPosition.y + 28.f),
        ImGui::GetColorU32(ink), name.empty() ? "Untitled project" : name.c_str());
    draw->AddText(ImVec2(rail.bodyPosition.x + 28.f, rail.bodyPosition.y + 58.f),
        ImGui::GetColorU32(ink), "No recent view available. Click the pill to open this workspace.");
}
