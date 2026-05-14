#include "ImGuiExtras.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace ImGuiExtras {

namespace {

NodeControlState g_NodeControlState;

struct NodeControlLayout {
    float startX = 0.0f;
    float labelWidth = 0.0f;
    float widgetWidth = 0.0f;
    float valueWidth = 0.0f;
    float spacing = 0.0f;
    ImVec2 screenPos;
};

NodeControlLayout BuildNodeControlLayout(float controlWidth, bool includeValueLane) {
    NodeControlLayout layout;
    layout.startX = ImGui::GetCursorPosX();
    layout.screenPos = ImGui::GetCursorScreenPos();
    layout.spacing = ImGui::GetStyle().ItemInnerSpacing.x;

    float availableWidth = controlWidth > 0.0f ? controlWidth : ImGui::CalcItemWidth();
    if (availableWidth <= 0.0f) {
        availableWidth = 200.0f;
    }

    layout.labelWidth = std::min(120.0f, availableWidth * 0.45f);
    layout.valueWidth = includeValueLane
        ? std::min(58.0f, std::max(42.0f, availableWidth * 0.20f))
        : 0.0f;
    const float occupiedSpacing = includeValueLane ? (layout.spacing * 2.0f) : layout.spacing;
    layout.widgetWidth = std::max(10.0f, availableWidth - layout.labelWidth - layout.valueWidth - occupiedSpacing);
    return layout;
}

void RenderNodeControlLabel(const char* label, const NodeControlLayout& layout) {
    ImGui::AlignTextToFramePadding();
    ImGui::PushClipRect(
        layout.screenPos,
        ImVec2(
            layout.screenPos.x + layout.labelWidth,
            layout.screenPos.y + ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 2.0f),
        true);
    ImGui::TextDisabled("%s", label);
    ImGui::PopClipRect();
}

void CaptureNodeControlItem(bool popupOpen = false) {
    g_NodeControlState.hovered |= ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    g_NodeControlState.active |= ImGui::IsItemActive();
    g_NodeControlState.edited |= ImGui::IsItemEdited();
    g_NodeControlState.popupOpen |= popupOpen;
    if (g_NodeControlState.hovered || g_NodeControlState.active || g_NodeControlState.edited || popupOpen) {
        g_NodeControlState.id = ImGui::GetItemID();
    }
}

} // namespace

float AnimateTowards(float current, float target, float deltaTime, float speed) {
    if (deltaTime <= 0.0f) {
        return target;
    }

    const float factor = 1.0f - std::exp(-std::max(speed, 0.0f) * deltaTime);
    current += (target - current) * factor;

    if (std::abs(current - target) < 0.0005f) {
        return target;
    }

    return current;
}

float EaseOutCubic(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    const float inverse = 1.0f - value;
    return 1.0f - (inverse * inverse * inverse);
}

void DrawSpinner(const char* label, float radius, int thickness, ImU32 color) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(radius * 2.0f, (radius * 2.0f) + ImGui::GetStyle().ItemInnerSpacing.y + ImGui::GetTextLineHeight());
    ImGui::Dummy(size);
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));

    const float time = static_cast<float>(ImGui::GetTime());
    const float pulse = 0.94f + (0.06f * std::sin(time * 2.35f));
    const float start = std::abs(std::sin(time * 1.8f)) * 6.0f;
    const float aMin = IM_PI * 2.0f * (start / 8.0f);
    const float aMax = IM_PI * 2.0f * ((start + 6.0f) / 8.0f);
    const ImVec2 center(bb.Min.x + radius, bb.Min.y + radius);

    window->DrawList->PathClear();
    window->DrawList->PathArcTo(center, radius * pulse, aMin, aMax, 24);
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

void ResetNodeControlState() {
    g_NodeControlState = {};
}

const NodeControlState& GetNodeControlState() {
    return g_NodeControlState;
}

bool NodeSliderFloat(const char* label, const char* id, float* v, float v_min, float v_max, const char* format, float controlWidth) {
    ImGui::PushID(id);
    const NodeControlLayout layout = BuildNodeControlLayout(controlWidth, true);

    RenderNodeControlLabel(label, layout);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(layout.startX + layout.labelWidth + layout.spacing);
    ImGui::SetNextItemWidth(layout.widgetWidth);
    const bool changed = ImGui::SliderFloat("##track", v, v_min, v_max, "");
    CaptureNodeControlItem();

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(layout.startX + layout.labelWidth + layout.spacing + layout.widgetWidth + layout.spacing);
    ImGui::AlignTextToFramePadding();
    char valBuf[64];
    snprintf(valBuf, sizeof(valBuf), format ? format : "%.2f", *v);
    ImGui::TextUnformatted(valBuf);
    
    ImGui::PopID();
    return changed;
}

bool NodeSliderInt(const char* label, const char* id, int* v, int v_min, int v_max, const char* format, float controlWidth) {
    ImGui::PushID(id);
    const NodeControlLayout layout = BuildNodeControlLayout(controlWidth, true);

    RenderNodeControlLabel(label, layout);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(layout.startX + layout.labelWidth + layout.spacing);
    ImGui::SetNextItemWidth(layout.widgetWidth);
    const bool changed = ImGui::SliderInt("##track", v, v_min, v_max, "");
    CaptureNodeControlItem();

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(layout.startX + layout.labelWidth + layout.spacing + layout.widgetWidth + layout.spacing);
    ImGui::AlignTextToFramePadding();
    char valBuf[64];
    snprintf(valBuf, sizeof(valBuf), format ? format : "%d", *v);
    ImGui::TextUnformatted(valBuf);
    
    ImGui::PopID();
    return changed;
}

bool NodeCheckbox(const char* label, const char* id, bool* v, float controlWidth) {
    ImGui::PushID(id);
    const NodeControlLayout layout = BuildNodeControlLayout(controlWidth, true);

    RenderNodeControlLabel(label, layout);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(layout.startX + layout.labelWidth + layout.spacing);
    const bool changed = ImGui::Checkbox("##check", v);
    CaptureNodeControlItem();

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(layout.startX + layout.labelWidth + layout.spacing + layout.widgetWidth + layout.spacing);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(*v ? "On" : "Off");
    
    ImGui::PopID();
    return changed;
}

bool NodeCombo(const char* label, const char* id, int* current_item, const char* const items[], int items_count, float controlWidth) {
    ImGui::PushID(id);
    const NodeControlLayout layout = BuildNodeControlLayout(controlWidth, false);

    RenderNodeControlLabel(label, layout);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(layout.startX + layout.labelWidth + layout.spacing);
    ImGui::SetNextItemWidth(layout.widgetWidth);
    const bool changed = ImGui::Combo("##combo", current_item, items, items_count);
    CaptureNodeControlItem(ImGui::IsPopupOpen("##combo"));

    ImGui::PopID();
    return changed;
}

bool NodeColorEdit3(const char* label, const char* id, float color[3], ImGuiColorEditFlags flags, float controlWidth) {
    ImGui::PushID(id);
    const NodeControlLayout layout = BuildNodeControlLayout(controlWidth, false);

    RenderNodeControlLabel(label, layout);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(layout.startX + layout.labelWidth + layout.spacing);
    ImGui::SetNextItemWidth(layout.widgetWidth);
    const bool changed = ImGui::ColorEdit3("##color", color, flags | ImGuiColorEditFlags_NoLabel);
    CaptureNodeControlItem(ImGui::IsPopupOpen("picker"));

    ImGui::PopID();
    return changed;
}

bool NodeColorEdit4(const char* label, const char* id, float color[4], ImGuiColorEditFlags flags, float controlWidth) {
    ImGui::PushID(id);
    const NodeControlLayout layout = BuildNodeControlLayout(controlWidth, false);

    RenderNodeControlLabel(label, layout);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(layout.startX + layout.labelWidth + layout.spacing);
    ImGui::SetNextItemWidth(layout.widgetWidth);
    const bool changed = ImGui::ColorEdit4("##color", color, flags | ImGuiColorEditFlags_NoLabel);
    CaptureNodeControlItem(ImGui::IsPopupOpen("picker"));

    ImGui::PopID();
    return changed;
}

bool NodeInputFloat(const char* label, const char* id, float* v, float step, float step_fast, const char* format, float controlWidth) {
    ImGui::PushID(id);
    const NodeControlLayout layout = BuildNodeControlLayout(controlWidth, false);

    RenderNodeControlLabel(label, layout);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(layout.startX + layout.labelWidth + layout.spacing);
    ImGui::SetNextItemWidth(layout.widgetWidth);
    const bool changed = ImGui::InputFloat("##input", v, step, step_fast, format ? format : "%.3f");
    CaptureNodeControlItem();

    ImGui::PopID();
    return changed;
}

bool NodeInputInt(const char* label, const char* id, int* v, int step, int step_fast, float controlWidth) {
    ImGui::PushID(id);
    const NodeControlLayout layout = BuildNodeControlLayout(controlWidth, false);

    RenderNodeControlLabel(label, layout);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(layout.startX + layout.labelWidth + layout.spacing);
    ImGui::SetNextItemWidth(layout.widgetWidth);
    const bool changed = ImGui::InputInt("##input", v, step, step_fast);
    CaptureNodeControlItem();

    ImGui::PopID();
    return changed;
}

} // namespace ImGuiExtras
