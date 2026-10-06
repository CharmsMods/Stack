#include "Editor/EditorModule.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cstdio>

void EditorModule::RenderRawFloatingControls(RawWorkspaceEditContext& context,
    const ImVec2& minimum, const ImVec2& size, bool enabled) {
    auto& surface = m_RawFloatingSurface;
    const auto* viewport = ImGui::GetMainViewport();
    const float scale = std::max(.75f, ImGui::GetFontSize() / 16.f);
    const float margin = std::min({12.f * scale, std::max(0.f, size.x) * .05f,
        std::max(0.f, size.y) * .05f});
    const ImVec2 available(std::max(1.f, size.x - margin * 2.f),
        std::max(1.f, size.y - margin * 2.f));
    const int tool = static_cast<int>(m_RawWorkspaceLabUi.activeTool);
    const auto measured = surface.contentHeights.find(tool);
    const float contentHeight = measured == surface.contentHeights.end()
        ? 560.f * scale : measured->second;
    const ImVec2 extent(std::min(400.f * scale, available.x),
        std::min({std::max(80.f * scale, contentHeight), 640.f * scale, available.y}));
    const ImVec2 lower(minimum.x + margin, minimum.y + margin);
    const ImVec2 upper(lower.x + std::max(0.f, available.x - extent.x),
        lower.y + std::max(0.f, available.y - extent.y));
    if (!surface.positioned) {
        surface.verticalOffset = lower.y - viewport->Pos.y;
        surface.positioned = true;
    }
    // Keep horizontal placement relative to the usable area. Rail/panel
    // expansion moves and compresses that area; collapse reverses the move.
    // Do not replace the preferred placement when a narrow layout constrains it.
    surface.horizontalTravel = upper.x - lower.x;
    const ImVec2 position(lower.x + surface.horizontalTravel * surface.horizontalPlacement,
        std::clamp(viewport->Pos.y + surface.verticalOffset, lower.y, upper.y));
    surface.minimum = position;
    surface.maximum = ImVec2(position.x + extent.x, position.y + extent.y);
    surface.frame = ImGui::GetFrameCount();
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(extent);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f * scale, 8.f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
        ImGui::GetStyle().Alpha * m_RawWorkspaceLabEditingSurfaceReveal);
    char name[80];
    std::snprintf(name, sizeof(name), "##RawFloatingControls_%p", static_cast<void*>(this));
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin(name, nullptr, flags)) {
        ImGui::BeginDisabled(!enabled);
        RenderRawWorkspaceActiveControls(context);
        ImGui::EndDisabled();
    }
    if (ImGui::GetCurrentContext()->OpenPopupStack.empty())
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
}

void EditorModule::RenderRawFloatingSurfaceHandle(const char* label) {
    const ImVec2 text = ImGui::CalcTextSize(label);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, ImVec2(text.x + 18.f, ImGui::GetFrameHeight()));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        auto& surface = m_RawFloatingSurface;
        const auto& delta = ImGui::GetIO().MouseDelta;
        if (surface.horizontalTravel > 0.f)
            surface.horizontalPlacement = std::clamp(surface.horizontalPlacement +
                delta.x / surface.horizontalTravel, 0.f, 1.f);
        surface.verticalOffset = surface.minimum.y - ImGui::GetMainViewport()->Pos.y + delta.y;
    }
    auto* draw = ImGui::GetWindowDrawList();
    const float centerY = start.y + ImGui::GetFrameHeight() * .5f;
    const ImU32 grip = ImGui::GetColorU32(hovered || active ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    for (int column = 0; column < 2; ++column)
        for (int row = 0; row < 3; ++row)
            draw->AddCircleFilled(ImVec2(start.x + 3.f + column * 4.f,
                centerY - 4.f + row * 4.f), 1.f, grip);
    draw->AddText(ImVec2(start.x + 18.f, centerY - text.y * .5f),
        ImGui::GetColorU32(ImGuiCol_Text), label);
    if (hovered) ImGui::SetTooltip("Drag to move the editing tools.");
}

bool EditorModule::RawFloatingSurfaceOwnsPointer() const {
    const auto& surface = m_RawFloatingSurface;
    auto& gui = *ImGui::GetCurrentContext();
    char name[80];
    std::snprintf(name, sizeof(name), "##RawWorkspaceSection_%p", static_cast<const void*>(this));
    auto* section = ImGui::FindWindowByID(ImHashStr(name));
    const auto belongsToSection = [section](ImGuiWindow* window) {
        return section && section->Active && window && ImGui::IsWindowChildOf(window, section, true, true);
    };
    if ((gui.ActiveId && belongsToSection(gui.ActiveIdWindow)) ||
        belongsToSection(gui.HoveredWindow)) return true;
    if (surface.frame != ImGui::GetFrameCount()) return false;
    std::snprintf(name, sizeof(name), "##RawFloatingControls_%p", static_cast<const void*>(this));
    auto* controls = ImGui::FindWindowByID(ImHashStr(name));
    const auto belongsToControls = [controls](ImGuiWindow* window) {
        return controls && window && ImGui::IsWindowChildOf(window, controls, true, true);
    };
    // Menus can extend beyond the panel. Retain ownership during a drag and
    // over descendant popups so direct canvas polling cannot consume it too.
    if ((gui.ActiveId && belongsToControls(gui.ActiveIdWindow)) ||
        belongsToControls(gui.HoveredWindow)) return true;
    return ImRect(surface.minimum, surface.maximum).Contains(ImGui::GetIO().MousePos);
}
