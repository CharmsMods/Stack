#include "ImGuiExtras.h"
#include <imgui_internal.h>
#include <array>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace ImGuiExtras {

namespace {

NodeControlState g_NodeControlState;
float g_RoutedMouseWheel = 0.0f;
bool g_SliderWheelModifierActive = false;
bool g_SliderWheelConsumed = false;
int g_GraphNodeControlScopeDepth = 0;
GraphNodeControlScopeConfig g_GraphNodeControlScopeConfig;

enum class GraphSliderValueKind {
    Float,
    Int
};

enum class GraphSliderInteractionMode {
    Slider,
    TextInput
};

struct GraphSliderScrubState {
    bool active = false;
    ImVec2 anchorScreenPos = ImVec2(0.0f, 0.0f);
    ImVec2 restoreScreenPos = ImVec2(0.0f, 0.0f);
    float dragStartFloat = 0.0f;
    int dragStartInt = 0;
};

struct GraphSliderEditState {
    GraphSliderInteractionMode mode = GraphSliderInteractionMode::Slider;
    GraphSliderValueKind kind = GraphSliderValueKind::Float;
    std::array<char, 96> buffer {};
    bool focusRequested = false;
    int lastTouchedFrame = 0;
    float lastValidFloat = 0.0f;
    int lastValidInt = 0;
    GraphSliderScrubState scrub;
};

std::unordered_map<ImGuiID, GraphSliderEditState> g_GraphSliderEditStates;
CursorCaptureRequest g_PendingCursorCaptureRequest;
bool g_HasPendingCursorCaptureRequest = false;

struct NodeControlLayout {
    float startX = 0.0f;
    float labelWidth = 0.0f;
    float widgetWidth = 0.0f;
    float valueWidth = 0.0f;
    float spacing = 0.0f;
    bool stacked = false;
    ImVec2 screenPos;
};

struct InputTextCallbackUserData {
    std::string* value = nullptr;
};

float GraphNodeControlScale() {
    return g_GraphNodeControlScopeDepth > 0
        ? std::max(0.01f, g_GraphNodeControlScopeConfig.scale)
        : 1.0f;
}

bool GraphNodeSliderTextEntryEnabled() {
    return g_GraphNodeControlScopeDepth > 0 && g_GraphNodeControlScopeConfig.allowSliderTextEntry;
}

bool GraphNodeSliderUsesScrubHandles() {
    return g_GraphNodeControlScopeDepth > 0 && g_GraphNodeControlScopeConfig.useScrubHandles;
}

GraphSliderRangePolicy CurrentGraphSliderRangePolicy() {
    return g_GraphNodeControlScopeDepth > 0
        ? g_GraphNodeControlScopeConfig.rangePolicy
        : GraphSliderRangePolicy::Bounded;
}

float GraphNodeSliderScrubSensitivity() {
    return g_GraphNodeControlScopeDepth > 0
        ? std::max(0.0001f, g_GraphNodeControlScopeConfig.scrubSensitivity)
        : 1.0f;
}

float GraphSliderInputWidth(const NodeControlLayout& layout) {
    if (layout.stacked) {
        return layout.widgetWidth;
    }
    return layout.widgetWidth + layout.spacing + layout.valueWidth;
}

void FormatSliderInputBuffer(GraphSliderEditState& state, float value) {
    std::snprintf(state.buffer.data(), state.buffer.size(), "%.9g", value);
}

void FormatSliderInputBuffer(GraphSliderEditState& state, int value) {
    std::snprintf(state.buffer.data(), state.buffer.size(), "%d", value);
}

GraphSliderEditState& TouchGraphSliderState(ImGuiID id, GraphSliderValueKind kind) {
    GraphSliderEditState& state = g_GraphSliderEditStates[id];
    state.lastTouchedFrame = ImGui::GetFrameCount();
    if (state.kind != kind) {
        state = GraphSliderEditState{};
        state.kind = kind;
        state.lastTouchedFrame = ImGui::GetFrameCount();
    }
    return state;
}

void PruneGraphSliderStates() {
    const int frame = ImGui::GetFrameCount();
    for (auto it = g_GraphSliderEditStates.begin(); it != g_GraphSliderEditStates.end(); ) {
        if (frame - it->second.lastTouchedFrame > 1200) {
            it = g_GraphSliderEditStates.erase(it);
        } else {
            ++it;
        }
    }
}

bool ParseFiniteFloat(const char* text, float& outValue) {
    if (!text) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(text, &end);
    if (end == text) {
        return false;
    }
    while (end && *end != '\0' && std::isspace(static_cast<unsigned char>(*end))) {
        ++end;
    }
    if (errno == ERANGE || (end && *end != '\0') || !std::isfinite(parsed)) {
        return false;
    }
    outValue = parsed;
    return true;
}

bool ParseInt(const char* text, int& outValue) {
    if (!text) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(text, &end, 10);
    if (end == text) {
        return false;
    }
    while (end && *end != '\0' && std::isspace(static_cast<unsigned char>(*end))) {
        ++end;
    }
    if (errno == ERANGE || (end && *end != '\0')) {
        return false;
    }
    if (parsed < static_cast<long long>(std::numeric_limits<int>::min()) ||
        parsed > static_cast<long long>(std::numeric_limits<int>::max())) {
        return false;
    }
    outValue = static_cast<int>(parsed);
    return true;
}

template <typename TValue>
bool AssignIfChanged(TValue* value, TValue nextValue) {
    if (!value || *value == nextValue) {
        return false;
    }
    *value = nextValue;
    return true;
}

NodeControlLayout BuildNodeControlLayout(float controlWidth, bool includeValueLane) {
    NodeControlLayout layout;
    layout.startX = ImGui::GetCursorPosX();
    layout.screenPos = ImGui::GetCursorScreenPos();
    const bool graphScope = g_GraphNodeControlScopeDepth > 0;
    const float graphScale = GraphNodeControlScale();
    layout.spacing = graphScope
        ? std::max(0.25f, ImGui::GetStyle().ItemInnerSpacing.x)
        : std::max(3.0f, ImGui::GetStyle().ItemInnerSpacing.x);

    float availableWidth = controlWidth > 0.0f ? controlWidth : ImGui::CalcItemWidth();
    if (availableWidth <= 0.0f) {
        availableWidth = 200.0f;
    }

    if (graphScope) {
        const float minLabelWidth = std::max(0.5f, 38.0f * graphScale);
        const float minValueWidth = std::max(0.5f, 36.0f * graphScale);
        const float minWidgetWidth = std::max(0.5f, 38.0f * graphScale);
        layout.labelWidth = std::min(g_GraphNodeControlScopeConfig.labelWidth, std::max(minLabelWidth, availableWidth * 0.34f));
        layout.valueWidth = includeValueLane
            ? std::min(g_GraphNodeControlScopeConfig.valueWidth, std::max(minValueWidth, availableWidth * 0.20f))
            : 0.0f;
        const float occupiedSpacing = includeValueLane ? (layout.spacing * 2.0f) : layout.spacing;
        layout.widgetWidth = availableWidth - layout.labelWidth - layout.valueWidth - occupiedSpacing;
        if (includeValueLane && layout.widgetWidth < g_GraphNodeControlScopeConfig.minSliderWidth) {
            layout.stacked = true;
            layout.labelWidth = std::max(minLabelWidth, availableWidth - layout.valueWidth - layout.spacing);
            layout.widgetWidth = std::max(minWidgetWidth, availableWidth);
        } else {
            layout.widgetWidth = std::max(minWidgetWidth, layout.widgetWidth);
        }
        return layout;
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

void CaptureNodeControlItem(bool popupOpen = false, bool rightClickConsumed = false) {
    g_NodeControlState.lastHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    g_NodeControlState.lastActive = ImGui::IsItemActive();
    g_NodeControlState.lastEdited = ImGui::IsItemEdited();
    g_NodeControlState.lastPopupOpen = popupOpen;
    g_NodeControlState.lastRightClickConsumed = rightClickConsumed;
    g_NodeControlState.hovered |= g_NodeControlState.lastHovered;
    g_NodeControlState.active |= g_NodeControlState.lastActive;
    g_NodeControlState.edited |= g_NodeControlState.lastEdited;
    g_NodeControlState.popupOpen |= popupOpen;
    g_NodeControlState.rightClickConsumed |= rightClickConsumed;
    if (g_NodeControlState.lastHovered || g_NodeControlState.lastActive || g_NodeControlState.lastEdited || popupOpen || rightClickConsumed) {
        g_NodeControlState.id = ImGui::GetItemID();
    }
}

float NormalizedSliderValue(float value, float valueMin, float valueMax) {
    const float lo = std::min(valueMin, valueMax);
    const float hi = std::max(valueMin, valueMax);
    const float span = std::max(hi - lo, 0.000001f);
    return std::clamp((value - lo) / span, 0.0f, 1.0f);
}

void DrawGraphNodeSliderVisual(
    const NodeControlLayout& layout,
    const ImRect& trackRect,
    const char* label,
    const char* valueText,
    float normalizedValue,
    bool hovered,
    bool active) {
    (void)normalizedValue;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float graphScale = GraphNodeControlScale();
    const float textHeight = ImGui::GetTextLineHeight();
    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 valueColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 chipColor = ImGui::GetColorU32(active ? ImGuiCol_FrameBgActive : (hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg));
    const ImU32 chipBorder = ImGui::GetColorU32(active ? ImGuiCol_SliderGrabActive : ImGuiCol_Border);
    const ImU32 gripColor = ImGui::GetColorU32(active ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab);
    const ImU32 glowColor = ImGui::GetColorU32(ImGuiCol_CheckMark);

    const ImVec2 labelMin = layout.screenPos;
    const ImVec2 valueSize = ImGui::CalcTextSize(valueText ? valueText : "");
    const ImVec2 valueMin = layout.stacked
        ? ImVec2(layout.screenPos.x + layout.labelWidth + layout.spacing, layout.screenPos.y)
        : ImVec2(trackRect.Max.x + layout.spacing, layout.screenPos.y);
    const ImVec2 labelMax = layout.stacked
        ? ImVec2(std::max(labelMin.x, valueMin.x - layout.spacing), layout.screenPos.y + textHeight + style.FramePadding.y)
        : ImVec2(layout.screenPos.x + layout.labelWidth, layout.screenPos.y + textHeight + style.FramePadding.y);
    drawList->PushClipRect(labelMin, labelMax, true);
    drawList->AddText(labelMin, textColor, label ? label : "");
    drawList->PopClipRect();

    const ImVec2 valueClipMin = valueMin;
    const ImVec2 valueClipMax(valueMin.x + layout.valueWidth, valueMin.y + textHeight + style.FramePadding.y);
    drawList->PushClipRect(valueClipMin, valueClipMax, true);
    drawList->AddText(ImVec2(valueClipMax.x - std::min(layout.valueWidth, valueSize.x), valueMin.y), valueColor, valueText ? valueText : "");
    drawList->PopClipRect();

    const float centerY = (trackRect.Min.y + trackRect.Max.y) * 0.5f;
    const float chipHeight = std::clamp(
        trackRect.GetHeight() * 0.46f,
        std::max(0.40f, 8.0f * graphScale),
        std::max(0.70f, 14.0f * graphScale));
    const float chipWidth = std::clamp(
        trackRect.GetWidth() * 0.18f,
        std::max(0.90f, 18.0f * graphScale),
        std::max(1.10f, 34.0f * graphScale));
    const float chipRounding = chipHeight * 0.5f;
    const float chipCenterX = trackRect.Min.x + trackRect.GetWidth() * 0.5f;
    const ImVec2 chipMin(chipCenterX - chipWidth * 0.5f, centerY - chipHeight * 0.5f);
    const ImVec2 chipMax(chipCenterX + chipWidth * 0.5f, centerY + chipHeight * 0.5f);

    if (active) {
        const ImVec2 glowExpand(std::max(1.0f, 4.0f * graphScale), std::max(1.0f, 3.0f * graphScale));
        const ImVec4 glow = ImGui::ColorConvertU32ToFloat4(glowColor);
        drawList->AddRectFilled(
            ImVec2(chipMin.x - glowExpand.x, chipMin.y - glowExpand.y),
            ImVec2(chipMax.x + glowExpand.x, chipMax.y + glowExpand.y),
            ImGui::GetColorU32(ImVec4(glow.x, glow.y, glow.z, 0.18f)),
            chipRounding + glowExpand.y);
    }

    drawList->AddRectFilled(chipMin, chipMax, chipColor, chipRounding);
    drawList->AddRect(chipMin, chipMax, chipBorder, chipRounding, 0, std::max(1.0f, graphScale));

    const float gripSpacing = std::max(1.0f, 4.0f * graphScale);
    const float gripHalfHeight = std::max(1.0f, chipHeight * 0.24f);
    const float gripHalfWidth = std::max(1.0f, 1.0f * graphScale);
    for (int index = -1; index <= 1; ++index) {
        const float x = chipCenterX + static_cast<float>(index) * gripSpacing;
        drawList->AddRectFilled(
            ImVec2(x - gripHalfWidth, centerY - gripHalfHeight),
            ImVec2(x + gripHalfWidth, centerY + gripHalfHeight),
            gripColor,
            gripHalfWidth);
    }
}

bool ApplyFloatSliderWheel(
    float* v,
    float vMin,
    float vMax,
    GraphSliderRangePolicy rangePolicy,
    bool sliderHovered,
    bool sliderActive,
    ImGuiWindow* currentWindow,
    float scrollYBefore,
    float scrollXBefore) {
    const float wheelDelta = GetSliderWheelDelta();
    if (!sliderHovered || sliderActive || ImGui::GetIO().WantTextInput || wheelDelta == 0.0f) {
        return false;
    }
    g_SliderWheelConsumed = true;
    const float span = std::max(std::abs(vMax - vMin), 0.0001f);
    float step = std::max(span * 0.01f, 0.001f);
    if (ImGui::GetIO().KeyShift) {
        step *= 0.1f;
    } else if (ImGui::GetIO().KeyCtrl) {
        step *= 10.0f;
    }
    float next = *v + (wheelDelta * step);
    if (rangePolicy == GraphSliderRangePolicy::Bounded) {
        next = std::clamp(next, std::min(vMin, vMax), std::max(vMin, vMax));
    }
    if (currentWindow) {
        currentWindow->Scroll.y = scrollYBefore;
        currentWindow->Scroll.x = scrollXBefore;
    }
    if (std::abs(next - *v) <= 0.000001f) {
        return false;
    }
    *v = next;
    return true;
}

bool ApplyIntSliderWheel(
    int* v,
    int vMin,
    int vMax,
    GraphSliderRangePolicy rangePolicy,
    bool sliderHovered,
    bool sliderActive,
    ImGuiWindow* currentWindow,
    float scrollYBefore,
    float scrollXBefore) {
    const float wheelDelta = GetSliderWheelDelta();
    if (!sliderHovered || sliderActive || ImGui::GetIO().WantTextInput || wheelDelta == 0.0f) {
        return false;
    }
    g_SliderWheelConsumed = true;
    const int span = std::max(std::abs(vMax - vMin), 1);
    int step = std::max(1, span / 100);
    if (ImGui::GetIO().KeyShift) {
        step = std::max(1, step / 2);
    } else if (ImGui::GetIO().KeyCtrl) {
        step = std::max(1, step * 5);
    }
    int next = *v + (wheelDelta > 0.0f ? step : -step);
    if (rangePolicy == GraphSliderRangePolicy::Bounded) {
        next = std::clamp(next, std::min(vMin, vMax), std::max(vMin, vMax));
    }
    if (currentWindow) {
        currentWindow->Scroll.y = scrollYBefore;
        currentWindow->Scroll.x = scrollXBefore;
    }
    if (next == *v) {
        return false;
    }
    *v = next;
    return true;
}

bool CommitGraphSliderFloatInput(
    GraphSliderEditState& state,
    float* value,
    float minValue,
    float maxValue,
    GraphSliderRangePolicy rangePolicy) {
    float parsedValue = 0.0f;
    if (!value) {
        return false;
    }
    if (ParseFiniteFloat(state.buffer.data(), parsedValue)) {
        if (rangePolicy == GraphSliderRangePolicy::Bounded) {
            parsedValue = std::clamp(parsedValue, std::min(minValue, maxValue), std::max(minValue, maxValue));
        }
        state.lastValidFloat = parsedValue;
        FormatSliderInputBuffer(state, parsedValue);
        return AssignIfChanged(value, parsedValue);
    }
    FormatSliderInputBuffer(state, state.lastValidFloat);
    return false;
}

bool CommitGraphSliderIntInput(
    GraphSliderEditState& state,
    int* value,
    int minValue,
    int maxValue,
    GraphSliderRangePolicy rangePolicy) {
    int parsedValue = 0;
    if (!value) {
        return false;
    }
    if (ParseInt(state.buffer.data(), parsedValue)) {
        if (rangePolicy == GraphSliderRangePolicy::Bounded) {
            parsedValue = std::clamp(parsedValue, std::min(minValue, maxValue), std::max(minValue, maxValue));
        }
        state.lastValidInt = parsedValue;
        FormatSliderInputBuffer(state, parsedValue);
        return AssignIfChanged(value, parsedValue);
    }
    FormatSliderInputBuffer(state, state.lastValidInt);
    return false;
}

bool RenderGraphNodeSliderFloatLegacy(const char* label, float* v, float v_min, float v_max, const char* format, const NodeControlLayout& layout) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float trackY = layout.stacked
        ? layout.screenPos.y + ImGui::GetTextLineHeight() + style.ItemInnerSpacing.y * 0.45f
        : layout.screenPos.y;
    const float trackX = layout.stacked
        ? layout.screenPos.x
        : layout.screenPos.x + layout.labelWidth + layout.spacing;
    ImGui::SetCursorScreenPos(ImVec2(trackX, trackY));
    ImGui::SetNextItemWidth(layout.widgetWidth);
    ImGuiWindow* currentWindow = ImGui::GetCurrentWindow();
    const float scrollYBefore = currentWindow ? currentWindow->Scroll.y : 0.0f;
    const float scrollXBefore = currentWindow ? currentWindow->Scroll.x : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    bool changed = ImGui::SliderFloat("##track", v, v_min, v_max, "");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(6);
    const bool sliderHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool sliderActive = ImGui::IsItemActive();
    changed |= ApplyFloatSliderWheel(v, v_min, v_max, GraphSliderRangePolicy::Bounded, sliderHovered, sliderActive, currentWindow, scrollYBefore, scrollXBefore);
    CaptureNodeControlItem();
    char valBuf[64];
    std::snprintf(valBuf, sizeof(valBuf), format ? format : "%.2f", *v);
    DrawGraphNodeSliderVisual(layout, ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), label, valBuf, NormalizedSliderValue(*v, v_min, v_max), sliderHovered, sliderActive);
    return changed;
}

bool RenderGraphNodeSliderIntLegacy(const char* label, int* v, int v_min, int v_max, const char* format, const NodeControlLayout& layout) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float trackY = layout.stacked
        ? layout.screenPos.y + ImGui::GetTextLineHeight() + style.ItemInnerSpacing.y * 0.45f
        : layout.screenPos.y;
    const float trackX = layout.stacked
        ? layout.screenPos.x
        : layout.screenPos.x + layout.labelWidth + layout.spacing;
    ImGui::SetCursorScreenPos(ImVec2(trackX, trackY));
    ImGui::SetNextItemWidth(layout.widgetWidth);
    ImGuiWindow* currentWindow = ImGui::GetCurrentWindow();
    const float scrollYBefore = currentWindow ? currentWindow->Scroll.y : 0.0f;
    const float scrollXBefore = currentWindow ? currentWindow->Scroll.x : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    bool changed = ImGui::SliderInt("##track", v, v_min, v_max, "");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(6);
    const bool sliderHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool sliderActive = ImGui::IsItemActive();
    changed |= ApplyIntSliderWheel(v, v_min, v_max, GraphSliderRangePolicy::Bounded, sliderHovered, sliderActive, currentWindow, scrollYBefore, scrollXBefore);
    CaptureNodeControlItem();
    char valBuf[64];
    std::snprintf(valBuf, sizeof(valBuf), format ? format : "%d", *v);
    DrawGraphNodeSliderVisual(layout, ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), label, valBuf, NormalizedSliderValue(static_cast<float>(*v), static_cast<float>(v_min), static_cast<float>(v_max)), sliderHovered, sliderActive);
    return changed;
}

bool RenderGraphNodeSliderFloatTextEditable(const char* label, float* v, float v_min, float v_max, const char* format, const NodeControlLayout& layout) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float trackY = layout.stacked
        ? layout.screenPos.y + ImGui::GetTextLineHeight() + style.ItemInnerSpacing.y * 0.45f
        : layout.screenPos.y;
    const float trackX = layout.stacked
        ? layout.screenPos.x
        : layout.screenPos.x + layout.labelWidth + layout.spacing;
    const GraphSliderRangePolicy rangePolicy = CurrentGraphSliderRangePolicy();
    ImGuiWindow* currentWindow = ImGui::GetCurrentWindow();
    const float scrollYBefore = currentWindow ? currentWindow->Scroll.y : 0.0f;
    const float scrollXBefore = currentWindow ? currentWindow->Scroll.x : 0.0f;
    const ImGuiID sliderId = currentWindow ? currentWindow->GetID("##track") : 0;
    GraphSliderEditState& state = TouchGraphSliderState(sliderId, GraphSliderValueKind::Float);
    state.lastValidFloat = *v;

    if (state.mode == GraphSliderInteractionMode::TextInput) {
        if (state.buffer[0] == '\0') {
            FormatSliderInputBuffer(state, *v);
        }
        RenderNodeControlLabel(label, layout);
        ImGui::SetCursorScreenPos(ImVec2(trackX, trackY));
        if (state.focusRequested) {
            ImGui::SetKeyboardFocusHere();
            state.focusRequested = false;
        }
        ImGui::SetNextItemWidth(GraphSliderInputWidth(layout));
        const bool committedWithEnter = ImGui::InputText(
            "##track_input",
            state.buffer.data(),
            state.buffer.size(),
            ImGuiInputTextFlags_EnterReturnsTrue |
                ImGuiInputTextFlags_AutoSelectAll |
                ImGuiInputTextFlags_CharsScientific);
        const bool inputHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const bool inputActive = ImGui::IsItemActive();
        const bool deactivated = ImGui::IsItemDeactivated();
        const bool escapePressed = inputActive && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        const bool toggledBack = inputHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        bool changed = false;
        if (escapePressed) {
            FormatSliderInputBuffer(state, state.lastValidFloat);
            ImGui::ClearActiveID();
        } else if (committedWithEnter || deactivated || toggledBack) {
            changed |= CommitGraphSliderFloatInput(state, v, v_min, v_max, rangePolicy);
        }
        if (toggledBack) {
            state.mode = GraphSliderInteractionMode::Slider;
            state.focusRequested = false;
            state.scrub.active = false;
            if (ImGui::GetActiveID() == ImGui::GetItemID()) {
                ImGui::ClearActiveID();
            }
        }
        CaptureNodeControlItem(false, toggledBack);
        return changed;
    }

    ImGui::SetCursorScreenPos(ImVec2(trackX, trackY));
    const ImVec2 widgetSize(layout.widgetWidth, ImGui::GetFrameHeight());
    ImGui::InvisibleButton("##track", widgetSize);
    const ImRect itemRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const bool sliderHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool sliderActive = ImGui::IsItemActive() || state.scrub.active;
    const bool justActivated = ImGui::IsItemActivated();
    bool changed = false;
    bool rightClickConsumed = false;

    if (sliderHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        state.mode = GraphSliderInteractionMode::TextInput;
        state.focusRequested = true;
        state.lastValidFloat = *v;
        FormatSliderInputBuffer(state, *v);
        state.scrub.active = false;
        rightClickConsumed = true;
    } else {
        if (justActivated && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            state.scrub.active = true;
            state.scrub.anchorScreenPos = ImGui::GetIO().MousePos;
            state.scrub.restoreScreenPos = ImGui::GetIO().MousePos;
            state.scrub.dragStartFloat = *v;
        }
        if (state.scrub.active) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                state.scrub.active = false;
            } else {
                const float rectWidth = std::max(1.0f, itemRect.GetWidth());
                float stepPerPixel = (std::abs(v_max - v_min) / rectWidth) * GraphNodeSliderScrubSensitivity();
                stepPerPixel = std::max(stepPerPixel, 0.000001f);
                if (ImGui::GetIO().KeyShift) {
                    stepPerPixel *= 0.1f;
                } else if (ImGui::GetIO().KeyCtrl) {
                    stepPerPixel *= 10.0f;
                }
                float nextValue = *v + ((ImGui::GetIO().MousePos.x - state.scrub.anchorScreenPos.x) * stepPerPixel);
                if (rangePolicy == GraphSliderRangePolicy::Bounded) {
                    nextValue = std::clamp(nextValue, std::min(v_min, v_max), std::max(v_min, v_max));
                }
                changed |= AssignIfChanged(v, nextValue);
                state.lastValidFloat = *v;
                SubmitCursorCaptureRequest(CursorCaptureRequest{
                    CursorCaptureMode::LockedScrub,
                    state.scrub.anchorScreenPos,
                    state.scrub.restoreScreenPos
                });
            }
        }
    }

    changed |= ApplyFloatSliderWheel(v, v_min, v_max, rangePolicy, sliderHovered, sliderActive, currentWindow, scrollYBefore, scrollXBefore);
    if (changed) {
        state.lastValidFloat = *v;
    }
    if (sliderHovered || state.scrub.active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    CaptureNodeControlItem(false, rightClickConsumed);
    char valBuf[64];
    std::snprintf(valBuf, sizeof(valBuf), format ? format : "%.2f", *v);
    DrawGraphNodeSliderVisual(layout, itemRect, label, valBuf, NormalizedSliderValue(*v, v_min, v_max), sliderHovered, sliderActive);
    return changed;
}

bool RenderGraphNodeSliderIntTextEditable(const char* label, int* v, int v_min, int v_max, const char* format, const NodeControlLayout& layout) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float trackY = layout.stacked
        ? layout.screenPos.y + ImGui::GetTextLineHeight() + style.ItemInnerSpacing.y * 0.45f
        : layout.screenPos.y;
    const float trackX = layout.stacked
        ? layout.screenPos.x
        : layout.screenPos.x + layout.labelWidth + layout.spacing;
    const GraphSliderRangePolicy rangePolicy = CurrentGraphSliderRangePolicy();
    ImGuiWindow* currentWindow = ImGui::GetCurrentWindow();
    const float scrollYBefore = currentWindow ? currentWindow->Scroll.y : 0.0f;
    const float scrollXBefore = currentWindow ? currentWindow->Scroll.x : 0.0f;
    const ImGuiID sliderId = currentWindow ? currentWindow->GetID("##track") : 0;
    GraphSliderEditState& state = TouchGraphSliderState(sliderId, GraphSliderValueKind::Int);
    state.lastValidInt = *v;

    if (state.mode == GraphSliderInteractionMode::TextInput) {
        if (state.buffer[0] == '\0') {
            FormatSliderInputBuffer(state, *v);
        }
        RenderNodeControlLabel(label, layout);
        ImGui::SetCursorScreenPos(ImVec2(trackX, trackY));
        if (state.focusRequested) {
            ImGui::SetKeyboardFocusHere();
            state.focusRequested = false;
        }
        ImGui::SetNextItemWidth(GraphSliderInputWidth(layout));
        const bool committedWithEnter = ImGui::InputText(
            "##track_input",
            state.buffer.data(),
            state.buffer.size(),
            ImGuiInputTextFlags_EnterReturnsTrue |
                ImGuiInputTextFlags_AutoSelectAll |
                ImGuiInputTextFlags_CharsDecimal);
        const bool inputHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const bool inputActive = ImGui::IsItemActive();
        const bool deactivated = ImGui::IsItemDeactivated();
        const bool escapePressed = inputActive && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        const bool toggledBack = inputHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        bool changed = false;
        if (escapePressed) {
            FormatSliderInputBuffer(state, state.lastValidInt);
            ImGui::ClearActiveID();
        } else if (committedWithEnter || deactivated || toggledBack) {
            changed |= CommitGraphSliderIntInput(state, v, v_min, v_max, rangePolicy);
        }
        if (toggledBack) {
            state.mode = GraphSliderInteractionMode::Slider;
            state.focusRequested = false;
            state.scrub.active = false;
            if (ImGui::GetActiveID() == ImGui::GetItemID()) {
                ImGui::ClearActiveID();
            }
        }
        CaptureNodeControlItem(false, toggledBack);
        return changed;
    }

    ImGui::SetCursorScreenPos(ImVec2(trackX, trackY));
    const ImVec2 widgetSize(layout.widgetWidth, ImGui::GetFrameHeight());
    ImGui::InvisibleButton("##track", widgetSize);
    const ImRect itemRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const bool sliderHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool sliderActive = ImGui::IsItemActive() || state.scrub.active;
    const bool justActivated = ImGui::IsItemActivated();
    bool changed = false;
    bool rightClickConsumed = false;

    if (sliderHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        state.mode = GraphSliderInteractionMode::TextInput;
        state.focusRequested = true;
        state.lastValidInt = *v;
        FormatSliderInputBuffer(state, *v);
        state.scrub.active = false;
        rightClickConsumed = true;
    } else {
        if (justActivated && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            state.scrub.active = true;
            state.scrub.anchorScreenPos = ImGui::GetIO().MousePos;
            state.scrub.restoreScreenPos = ImGui::GetIO().MousePos;
            state.scrub.dragStartInt = *v;
        }
        if (state.scrub.active) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                state.scrub.active = false;
            } else {
                const float rectWidth = std::max(1.0f, itemRect.GetWidth());
                float stepPerPixel = (static_cast<float>(std::abs(v_max - v_min)) / rectWidth) * GraphNodeSliderScrubSensitivity();
                stepPerPixel = std::max(stepPerPixel, 1.0f / rectWidth);
                if (ImGui::GetIO().KeyShift) {
                    stepPerPixel *= 0.1f;
                } else if (ImGui::GetIO().KeyCtrl) {
                    stepPerPixel *= 10.0f;
                }
                float nextValue = static_cast<float>(*v) + ((ImGui::GetIO().MousePos.x - state.scrub.anchorScreenPos.x) * stepPerPixel);
                int quantizedValue = static_cast<int>(std::lround(nextValue));
                if (rangePolicy == GraphSliderRangePolicy::Bounded) {
                    quantizedValue = std::clamp(quantizedValue, std::min(v_min, v_max), std::max(v_min, v_max));
                }
                changed |= AssignIfChanged(v, quantizedValue);
                state.lastValidInt = *v;
                SubmitCursorCaptureRequest(CursorCaptureRequest{
                    CursorCaptureMode::LockedScrub,
                    state.scrub.anchorScreenPos,
                    state.scrub.restoreScreenPos
                });
            }
        }
    }

    changed |= ApplyIntSliderWheel(v, v_min, v_max, rangePolicy, sliderHovered, sliderActive, currentWindow, scrollYBefore, scrollXBefore);
    if (changed) {
        state.lastValidInt = *v;
    }
    if (sliderHovered || state.scrub.active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    CaptureNodeControlItem(false, rightClickConsumed);
    char valBuf[64];
    std::snprintf(valBuf, sizeof(valBuf), format ? format : "%d", *v);
    DrawGraphNodeSliderVisual(layout, itemRect, label, valBuf, NormalizedSliderValue(static_cast<float>(*v), static_cast<float>(v_min), static_cast<float>(v_max)), sliderHovered, sliderActive);
    return changed;
}

bool RenderGraphNodeSliderFloat(const char* label, float* v, float v_min, float v_max, const char* format, const NodeControlLayout& layout) {
    if (!GraphNodeSliderUsesScrubHandles() && !GraphNodeSliderTextEntryEnabled()) {
        return RenderGraphNodeSliderFloatLegacy(label, v, v_min, v_max, format, layout);
    }
    return RenderGraphNodeSliderFloatTextEditable(label, v, v_min, v_max, format, layout);
}

bool RenderGraphNodeSliderInt(const char* label, int* v, int v_min, int v_max, const char* format, const NodeControlLayout& layout) {
    if (!GraphNodeSliderUsesScrubHandles() && !GraphNodeSliderTextEntryEnabled()) {
        return RenderGraphNodeSliderIntLegacy(label, v, v_min, v_max, format, layout);
    }
    return RenderGraphNodeSliderIntTextEditable(label, v, v_min, v_max, format, layout);
}

int ResizeStdStringInputCallback(ImGuiInputTextCallbackData* data) {
    InputTextCallbackUserData* userData = static_cast<InputTextCallbackUserData*>(data->UserData);
    if (!userData || !userData->value) {
        return 0;
    }

    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        std::string& value = *userData->value;
        value.resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = value.data();
    }
    return 0;
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

void BeginFrameInputRouting() {
    ImGuiIO& io = ImGui::GetIO();
    PruneGraphSliderStates();
    g_HasPendingCursorCaptureRequest = false;
    g_PendingCursorCaptureRequest = {};
    g_SliderWheelModifierActive = io.KeyCtrl && !io.WantTextInput;
    g_SliderWheelConsumed = false;
    g_RoutedMouseWheel = io.MouseWheel;
}

bool IsSliderWheelModifierActive() {
    return g_SliderWheelModifierActive;
}

bool IsSliderWheelConsumed() {
    return g_SliderWheelConsumed;
}

float GetSliderWheelDelta() {
    return g_SliderWheelModifierActive ? g_RoutedMouseWheel : 0.0f;
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

void DrawSpinnerOnly(float radius, int thickness, ImU32 color) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(radius * 2.0f, radius * 2.0f);
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
}

void RenderSpinnerOnlyOverlay(float alpha) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (alpha <= 0.001f) {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 min = ImGui::GetWindowPos();
    ImVec2 max = ImVec2(min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y);

    ImGui::SetCursorScreenPos(min);
    ImGui::PushID("##SpinnerOnlyOverlayBlocker");
    ImGui::InvisibleButton("##Blocker", ImGui::GetWindowSize());
    ImGui::PopID();

    const float radius = 24.0f;
    const ImVec2 centerPos(
        min.x + (max.x - min.x) * 0.5f - radius,
        min.y + (max.y - min.y) * 0.5f - radius);

    ImGui::SetCursorScreenPos(centerPos);
    DrawSpinnerOnly(radius, 4, IM_COL32(255, 255, 255, static_cast<int>(240.0f * alpha)));
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

void RenderProgressOverlay(const char* message, float progress) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 windowMin = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const float panelWidth = std::min(340.0f, std::max(220.0f, windowSize.x - 32.0f));
    const float panelHeight = 58.0f;
    const ImVec2 panelMin(
        windowMin.x + windowSize.x - panelWidth - 18.0f,
        windowMin.y + windowSize.y - panelHeight - 18.0f);
    const ImVec2 panelMax(panelMin.x + panelWidth, panelMin.y + panelHeight);
    const float clampedProgress = std::clamp(progress, 0.0f, 1.0f);

    drawList->AddRectFilled(panelMin, panelMax, IM_COL32(9, 23, 28, 220), 8.0f);
    drawList->AddRect(panelMin, panelMax, IM_COL32(110, 176, 190, 120), 8.0f);

    const float padding = 12.0f;
    const ImVec2 textMin(panelMin.x + padding, panelMin.y + 9.0f);
    const ImVec2 textMax(panelMax.x - padding, panelMin.y + 29.0f);
    drawList->PushClipRect(textMin, textMax, true);
    drawList->AddText(textMin, IM_COL32(228, 246, 250, 245), message && message[0] ? message : "Rendering...");
    drawList->PopClipRect();

    char percentText[16];
    std::snprintf(percentText, sizeof(percentText), "%d%%", static_cast<int>(std::round(clampedProgress * 100.0f)));
    const ImVec2 percentSize = ImGui::CalcTextSize(percentText);
    drawList->AddText(
        ImVec2(panelMax.x - padding - percentSize.x, panelMin.y + 9.0f),
        IM_COL32(170, 214, 224, 230),
        percentText);

    const ImVec2 barMin(panelMin.x + padding, panelMax.y - 18.0f);
    const ImVec2 barMax(panelMax.x - padding, panelMax.y - 10.0f);
    drawList->AddRectFilled(barMin, barMax, IM_COL32(42, 70, 78, 245), 4.0f);
    const float fillX = barMin.x + (barMax.x - barMin.x) * clampedProgress;
    if (fillX > barMin.x) {
        drawList->AddRectFilled(barMin, ImVec2(fillX, barMax.y), IM_COL32(88, 192, 211, 245), 4.0f);
    }
}

void ResetNodeControlState() {
    g_NodeControlState = {};
}

const NodeControlState& GetNodeControlState() {
    return g_NodeControlState;
}

void BeginGraphNodeControlScope(const GraphNodeControlScopeConfig& config) {
    if (g_GraphNodeControlScopeDepth == 0) {
        g_GraphNodeControlScopeConfig = config;
    }
    ++g_GraphNodeControlScopeDepth;
}

void EndGraphNodeControlScope() {
    if (g_GraphNodeControlScopeDepth > 0) {
        --g_GraphNodeControlScopeDepth;
    }
}

bool IsGraphNodeControlScopeActive() {
    return g_GraphNodeControlScopeDepth > 0;
}

void SubmitCursorCaptureRequest(const CursorCaptureRequest& request) {
    g_PendingCursorCaptureRequest = request;
    g_HasPendingCursorCaptureRequest = request.mode != CursorCaptureMode::None;
}

bool ConsumeCursorCaptureRequest(CursorCaptureRequest* outRequest) {
    if (!g_HasPendingCursorCaptureRequest) {
        return false;
    }
    if (outRequest) {
        *outRequest = g_PendingCursorCaptureRequest;
    }
    g_HasPendingCursorCaptureRequest = false;
    g_PendingCursorCaptureRequest = {};
    return true;
}

void RichSectionLabel(const char* label, float spacingAfter) {
    ImGui::TextDisabled("%s", label);
    if (spacingAfter > 0.0f) {
        ImGui::Dummy(ImVec2(0.0f, spacingAfter));
    }
}

bool RichFullWidthButton(const char* label, float width, float height) {
    return ImGui::Button(label, ImVec2(std::max(1.0f, width), height));
}

void RichColorSwatchRow(
    const char* swatchId,
    const float color[3],
    float swatchWidth,
    float swatchHeight,
    const char* valueText,
    float totalWidth,
    float spacing) {
    const float clampedSwatchWidth = std::max(18.0f, swatchWidth);
    const float clampedSwatchHeight = std::max(8.0f, swatchHeight);
    const float clampedSpacing = std::max(0.0f, spacing);
    const ImVec2 swatchMin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(swatchId, ImVec2(clampedSwatchWidth, clampedSwatchHeight));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        swatchMin,
        ImGui::GetItemRectMax(),
        ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], 1.0f)),
        std::max(2.0f, clampedSwatchHeight * 0.15f));
    drawList->AddRect(
        swatchMin,
        ImGui::GetItemRectMax(),
        IM_COL32(255, 255, 255, 28),
        std::max(2.0f, clampedSwatchHeight * 0.15f));

    if (valueText && valueText[0] != '\0') {
        const float textWidth = ImGui::CalcTextSize(valueText).x;
        const float sameLineThreshold = clampedSwatchWidth + clampedSpacing + textWidth;
        if (totalWidth >= sameLineThreshold) {
            ImGui::SameLine(0.0f, clampedSpacing);
            ImGui::AlignTextToFramePadding();
        } else {
            ImGui::Dummy(ImVec2(0.0f, std::max(1.0f, clampedSpacing * 0.4f)));
        }
        ImGui::TextDisabled("%s", valueText);
    }
}

bool NodeSliderFloat(const char* label, const char* id, float* v, float v_min, float v_max, const char* format, float controlWidth) {
    ImGui::PushID(id);
    const NodeControlLayout layout = BuildNodeControlLayout(controlWidth, true);
    if (IsGraphNodeControlScopeActive()) {
        const bool changed = RenderGraphNodeSliderFloat(label, v, v_min, v_max, format, layout);
        ImGui::PopID();
        return changed;
    }

    RenderNodeControlLabel(label, layout);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(layout.startX + layout.labelWidth + layout.spacing);
    ImGui::SetNextItemWidth(layout.widgetWidth);
    ImGuiWindow* currentWindow = ImGui::GetCurrentWindow();
    const float scrollYBefore = currentWindow ? currentWindow->Scroll.y : 0.0f;
    const float scrollXBefore = currentWindow ? currentWindow->Scroll.x : 0.0f;
    bool changed = ImGui::SliderFloat("##track", v, v_min, v_max, "");
    const bool sliderHovered = ImGui::IsItemHovered();
    const bool sliderActive = ImGui::IsItemActive();
    changed |= ApplyFloatSliderWheel(v, v_min, v_max, GraphSliderRangePolicy::Bounded, sliderHovered, sliderActive, currentWindow, scrollYBefore, scrollXBefore);
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
    if (IsGraphNodeControlScopeActive()) {
        const bool changed = RenderGraphNodeSliderInt(label, v, v_min, v_max, format, layout);
        ImGui::PopID();
        return changed;
    }

    RenderNodeControlLabel(label, layout);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(layout.startX + layout.labelWidth + layout.spacing);
    ImGui::SetNextItemWidth(layout.widgetWidth);
    ImGuiWindow* currentWindow = ImGui::GetCurrentWindow();
    const float scrollYBefore = currentWindow ? currentWindow->Scroll.y : 0.0f;
    const float scrollXBefore = currentWindow ? currentWindow->Scroll.x : 0.0f;
    bool changed = ImGui::SliderInt("##track", v, v_min, v_max, "");
    const bool sliderHovered = ImGui::IsItemHovered();
    const bool sliderActive = ImGui::IsItemActive();
    changed |= ApplyIntSliderWheel(v, v_min, v_max, GraphSliderRangePolicy::Bounded, sliderHovered, sliderActive, currentWindow, scrollYBefore, scrollXBefore);
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
    const NodeControlLayout layout = BuildNodeControlLayout(
        controlWidth,
        IsGraphNodeControlScopeActive() ? false : true);

    RenderNodeControlLabel(label, layout);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(layout.startX + layout.labelWidth + layout.spacing);
    const bool changed = ImGui::Checkbox("##check", v);
    CaptureNodeControlItem();

    if (!IsGraphNodeControlScopeActive()) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorPosX(layout.startX + layout.labelWidth + layout.spacing + layout.widgetWidth + layout.spacing);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(*v ? "On" : "Off");
    }
    
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

bool NodeTextMultiline(const char* label, const char* id, std::string& value, float controlWidth, int lineCount) {
    ImGui::PushID(id);

    if (label && label[0] != '\0') {
        ImGui::TextDisabled("%s", label);
    }

    const float width = std::max(80.0f, controlWidth > 0.0f ? controlWidth : ImGui::CalcItemWidth());
    const float lines = std::max(2, lineCount);
    const float height =
        ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(lines) +
        ImGui::GetStyle().FramePadding.y * 2.0f;
    ImGui::SetNextItemWidth(width);
    if (value.capacity() < value.size() + 1) {
        value.reserve(value.size() + 32);
    }

    InputTextCallbackUserData userData { &value };
    const bool changed = ImGui::InputTextMultiline(
        "##TextValue",
        value.data(),
        value.capacity() + 1,
        ImVec2(width, height),
        ImGuiInputTextFlags_CallbackResize,
        ResizeStdStringInputCallback,
        &userData);
    CaptureNodeControlItem();

    ImGui::PopID();
    return changed;
}

bool GradeWheel3(const char* label, const char* id, float color[3], float width) {
    ImGui::PushID(id);
    ImGui::BeginGroup();

    if (label && label[0] != '\0') {
        ImGui::TextUnformatted(label);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    const float availableWidth = width > 0.0f
        ? width
        : std::max(170.0f, std::min(ImGui::GetContentRegionAvail().x, 250.0f));
    const float clampedWidth = std::max(96.0f, availableWidth);

    color[0] = std::clamp(color[0], 0.0f, 1.0f);
    color[1] = std::clamp(color[1], 0.0f, 1.0f);
    color[2] = std::clamp(color[2], 0.0f, 1.0f);

    ImGui::SetNextItemWidth(clampedWidth);
    const ImGuiColorEditFlags flags =
        ImGuiColorEditFlags_Float |
        ImGuiColorEditFlags_DisplayRGB |
        ImGuiColorEditFlags_PickerHueWheel |
        ImGuiColorEditFlags_NoSidePreview |
        ImGuiColorEditFlags_NoSmallPreview |
        ImGuiColorEditFlags_NoOptions |
        ImGuiColorEditFlags_NoInputs |
        ImGuiColorEditFlags_NoLabel;
    bool changed = ImGui::ColorPicker3("##GradeWheel", color, flags);
    CaptureNodeControlItem();

    const bool neutral =
        std::abs(color[0] - 1.0f) < 0.0005f &&
        std::abs(color[1] - 1.0f) < 0.0005f &&
        std::abs(color[2] - 1.0f) < 0.0005f;

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    char rgbText[64];
    snprintf(rgbText, sizeof(rgbText), "R %.0f  G %.0f  B %.0f", color[0] * 255.0f, color[1] * 255.0f, color[2] * 255.0f);
    const float swatchWidth = std::max(48.0f, std::min(clampedWidth * 0.32f, 76.0f));
    const float swatchHeight = std::max(12.0f, ImGui::GetFrameHeight() * 0.55f);
    RichColorSwatchRow("##GradeWheelSwatch", color, swatchWidth, swatchHeight, rgbText, clampedWidth, 10.0f);

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::BeginDisabled(neutral);
    if (RichFullWidthButton("Reset", clampedWidth)) {
        color[0] = 1.0f;
        color[1] = 1.0f;
        color[2] = 1.0f;
        changed = true;
    }
    CaptureNodeControlItem();
    ImGui::EndDisabled();

    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

} // namespace ImGuiExtras
