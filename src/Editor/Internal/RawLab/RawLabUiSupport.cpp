#include "Editor/Internal/RawLab/RawLabUiSupport.h"

#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
namespace {

constexpr float kDrawerResponseSeconds = 0.11f;

void PushBareButtonColors(bool active) {
    const ImVec4 transparent(0.0f, 0.0f, 0.0f, 0.0f);
    const ImVec4 selected = active
        ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
        : transparent;
    ImGui::PushStyleColor(ImGuiCol_Button, selected);
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
    ImGui::PushStyleColor(
        ImGuiCol_ButtonActive,
        ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
}

void PopBareButtonColors() {
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

} // namespace

float AnimateRawLabDrawerHeight(
    float current,
    float target,
    float deltaTimeSeconds) {
    const float delta = std::clamp(deltaTimeSeconds, 0.0f, 0.05f);
    const float response = 1.0f - std::exp(-delta / kDrawerResponseSeconds);
    const float next = current + (target - current) * response;
    return std::abs(next - target) <= 0.25f ? target : next;
}

void LabTooltip(const char* text, ImGuiHoveredFlags flags) {
    if (text != nullptr && text[0] != '\0' && ImGui::IsItemHovered(flags)) {
        ImGui::SetTooltip("%s", text);
    }
}

bool FramelessTextButton(
    const char* label,
    bool active,
    bool enabled) {
    const ImVec4 transparent(0.0f, 0.0f, 0.0f, 0.0f);
    const ImVec4 textColor = active
        ? ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive)
        : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImGui::PushStyleColor(ImGuiCol_Button, transparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, transparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, transparent);
    ImGui::PushStyleColor(ImGuiCol_Text, textColor);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::Button(label);
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return pressed;
}

bool BareTextButton(
    const char* label,
    bool active,
    bool enabled,
    const ImVec2& size) {
    PushBareButtonColors(active);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::Button(label, size);
    ImGui::EndDisabled();
    PopBareButtonColors();
    return pressed;
}

bool BypassEyeButton(
    const char* id,
    bool enabled,
    const char* enabledTooltip,
    const char* bypassedTooltip) {
    const ImVec2 eyeMinimum = ImGui::GetCursorScreenPos();
    const ImVec2 eyeSize(27.0f, 20.0f);
    const bool pressed = ImGui::InvisibleButton(id, eyeSize);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 color = enabled
        ? ImGui::GetColorU32(ImGuiCol_Text)
        : ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImVec2 eyeLeft(eyeMinimum.x + 3.0f, eyeMinimum.y + 10.0f);
    const ImVec2 eyeRight(eyeMinimum.x + 24.0f, eyeMinimum.y + 10.0f);
    drawList->AddBezierCubic(
        eyeLeft,
        ImVec2(eyeMinimum.x + 8.0f, eyeMinimum.y + 2.5f),
        ImVec2(eyeMinimum.x + 19.0f, eyeMinimum.y + 2.5f),
        eyeRight,
        color,
        1.6f);
    drawList->AddBezierCubic(
        eyeLeft,
        ImVec2(eyeMinimum.x + 8.0f, eyeMinimum.y + 17.5f),
        ImVec2(eyeMinimum.x + 19.0f, eyeMinimum.y + 17.5f),
        eyeRight,
        color,
        1.6f);
    if (enabled) {
        drawList->AddCircleFilled(
            ImVec2(eyeMinimum.x + 13.5f, eyeMinimum.y + 10.0f),
            3.2f,
            color);
    } else {
        drawList->AddLine(
            ImVec2(eyeMinimum.x + 4.0f, eyeMinimum.y + 17.0f),
            ImVec2(eyeMinimum.x + 23.0f, eyeMinimum.y + 3.0f),
            color,
            1.8f);
    }
    LabTooltip(enabled ? enabledTooltip : bypassedTooltip);
    return pressed;
}

void ContinueRawLabControlRow(const char* label, float spacing) {
    const float width = ImGui::CalcTextSize(label, nullptr, true).x +
        ImGui::GetStyle().FramePadding.x * 2.0f;
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (ImGui::GetItemRectMax().x + spacing + width <= right)
        ImGui::SameLine(0.0f, spacing);
}

bool BareSliderFloat(
    const char* label,
    const char* id,
    float* value,
    float minimum,
    float maximum,
    const char* format,
    float width) {
    ImGui::PushID(id);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (width > 0.0f || ImGui::GetWindowWidth() >= ImGui::GetFontSize() * 24.0f)
        ImGui::SameLine();
    ImGui::SetNextItemWidth(width > 0.0f ? width : -1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgHovered,
        ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgActive,
        ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    const bool changed =
        ImGui::SliderFloat("##value", value, minimum, maximum, format);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    ImGui::PopID();
    return changed;
}

bool BareSliderInt(
    const char* label,
    const char* id,
    int* value,
    int minimum,
    int maximum,
    const char* format,
    float width) {
    ImGui::PushID(id);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (width > 0.0f || ImGui::GetWindowWidth() >= ImGui::GetFontSize() * 24.0f)
        ImGui::SameLine();
    ImGui::SetNextItemWidth(width > 0.0f ? width : -1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgHovered,
        ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgActive,
        ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    const bool changed =
        ImGui::SliderInt("##value", value, minimum, maximum, format);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    ImGui::PopID();
    return changed;
}

ImVec2 CompactLabGraphSize(
    float reservedHeight,
    float minimumHeight,
    float maximumHeight) {
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float width = std::max(1.0f, available.x);
    const float desiredHeight =
        std::clamp(width * 0.75f, minimumHeight, maximumHeight);
    const float usableHeight =
        std::max(minimumHeight, available.y - reservedHeight);
    return ImVec2(width, std::min(desiredHeight, usableHeight));
}

bool BareToolIslandButton(
    const char* label,
    bool active,
    float horizontalPadding) {
    const ImVec4 transparent(0.0f, 0.0f, 0.0f, 0.0f);
    const ImVec4 textColor = active
        ? ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive)
        : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImGui::PushStyleColor(ImGuiCol_Button, transparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, transparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, transparent);
    ImGui::PushStyleColor(ImGuiCol_Text, textColor);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(horizontalPadding, 5.0f));
    const bool pressed = ImGui::Button(label);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    return pressed;
}

ImVec2 LabPopupAnchorBelowButtonText(
    const ImRect& buttonBounds,
    const char* label) {
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const float textOffsetX = std::max(
        0.0f,
        (buttonBounds.GetWidth() - textSize.x) * 0.5f);
    const float textOffsetY = std::max(
        0.0f,
        (buttonBounds.GetHeight() - textSize.y) * 0.5f);
    return ImVec2(
        buttonBounds.Min.x + textOffsetX,
        buttonBounds.Min.y + textOffsetY + textSize.y + 1.0f);
}

ImVec2 FitLabImage(float width, float height, const ImVec2& bounds) {
    if (width <= 0.0f ||
        height <= 0.0f ||
        bounds.x <= 0.0f ||
        bounds.y <= 0.0f) {
        return ImVec2(0.0f, 0.0f);
    }
    const float scale = std::min(bounds.x / width, bounds.y / height);
    return ImVec2(width * scale, height * scale);
}

} // namespace Stack::Editor::RawLabInternal
