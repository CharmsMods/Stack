#include "ImGuiExtras.h"
#include <imgui_internal.h>
#include <cmath>

namespace ImGuiExtras {

void DrawSpinner(const char* label, float radius, int thickness, ImU32 color) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(radius * 2.0f, (radius * 2.0f) + ImGui::GetStyle().ItemInnerSpacing.y + ImGui::GetTextLineHeight());
    ImGui::Dummy(size);
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));

    const float time = static_cast<float>(ImGui::GetTime());
    const float start = std::abs(std::sin(time * 1.8f)) * 6.0f;
    const float aMin = IM_PI * 2.0f * (start / 8.0f);
    const float aMax = IM_PI * 2.0f * ((start + 6.0f) / 8.0f);
    const ImVec2 center(bb.Min.x + radius, bb.Min.y + radius);

    window->DrawList->PathClear();
    window->DrawList->PathArcTo(center, radius, aMin, aMax, 24);
    window->DrawList->PathStroke(color, false, static_cast<float>(thickness));

    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 textPos(bb.Min.x + (size.x - textSize.x) * 0.5f, bb.Min.y + radius * 2.0f + ImGui::GetStyle().ItemInnerSpacing.y);
    window->DrawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
}

void RenderBusyOverlay(const char* message) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 min = ImGui::GetWindowPos();
    ImVec2 max = ImVec2(min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y);

    // Draw the overlay background
    drawList->AddRectFilled(min, max, IM_COL32(20, 20, 20, 180));

    // Block interaction for the window by drawing an invisible button
    // We need to push a clip rect to allow the button to be drawn outside of normal flow if necessary, 
    // but SetCursorScreenPos is usually enough.
    ImGui::SetCursorScreenPos(min);
    ImGui::PushID("##BusyOverlayBlocker");
    ImGui::InvisibleButton("##Blocker", ImGui::GetWindowSize());
    ImGui::PopID();

    // Compute center and set cursor for spinner
    float radius = 24.0f;
    float totalHeight = (radius * 2.0f) + ImGui::GetStyle().ItemInnerSpacing.y + ImGui::GetTextLineHeight();
    
    ImVec2 centerPos = ImVec2(
        min.x + (max.x - min.x) * 0.5f - radius,
        min.y + (max.y - min.y) * 0.5f - (totalHeight * 0.5f)
    );

    ImGui::SetCursorScreenPos(centerPos);
    DrawSpinner(message, radius, 4, IM_COL32(255, 255, 255, 240));
}

} // namespace ImGuiExtras
