#include "AppHeaderStyle.h"
#include "Notifications/NotificationPresenter.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>

namespace Stack::Header {
ImVec4 Blend(ImVec4 from, ImVec4 to, float amount) {
    return ImLerp(from, to, std::clamp(amount, 0.0f, 1.0f));
}

float ResolveSectionTintOpacity(float expandedAmount) {
    return SectionTintOpacity * std::clamp(expandedAmount, 0.0f, 1.0f);
}

ImVec4 ResolvePanelColor(ImVec4 workspace, float opacity) {
    workspace.w = 1.0f;
    const float luminance = workspace.x * .2126f + workspace.y * .7152f + workspace.z * .0722f;
    ImVec4 panel = Blend(workspace, luminance < .5f
        ? ImVec4(0, 0, 0, 1) : ImVec4(1, 1, 1, 1), .13f);
    panel.w = std::clamp(opacity, 0.0f, 1.0f);
    return panel;
}

Palette ResolvePalette(ImVec4 workspace, bool windowFocused) {
    Palette palette;
    workspace.w = 1.0f;
    palette.activeTab = workspace;
    palette.text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    palette.caption = ResolvePanelColor(workspace);
    palette.mutedText = Blend(palette.text, palette.caption, .28f);
    palette.hover = Blend(palette.caption, palette.text, .10f);
    palette.pressed = Blend(palette.caption, palette.text, .16f);
    palette.focus = ImGui::GetStyleColorVec4(ImGuiCol_NavCursor);
    palette.selected = Blend(palette.caption, palette.text, .14f);
    if (!windowFocused) {
        palette.text = Blend(palette.text, palette.caption, .25f);
        palette.mutedText = Blend(palette.mutedText, palette.caption, .18f);
    }
    return palette;
}

void Approach(float& value, float target, float speed, float settleTolerance) {
    if (value == target) return;
    if (Stack::Notifications::SystemReducedMotion()) { value = target; return; }
    const float response = 1.0f - std::exp(-std::clamp(ImGui::GetIO().DeltaTime, 0.0f, .05f) * speed);
    value += (target - value) * response;
    if (std::abs(value - target) < settleTolerance) value = target;
}

void ApproachMotion(float& value, float target, float duration) {
    if (value == target) return;
    if (Stack::Notifications::SystemReducedMotion()) { value = target; return; }
    const float distance = std::abs(target - value);
    const float remaining = std::max(0.f, std::cbrt(distance) -
        std::max(0.f, ImGui::GetIO().DeltaTime) / std::max(.001f, duration));
    value = target + (value < target ? -1.f : 1.f) * remaining * remaining * remaining;
}

ImRect AlignToPixels(const ImRect& rect) {
    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    const auto snap = [](float value, float factor) {
        factor = std::max(1.0f, factor);
        return std::round(value * factor) / factor;
    };
    return ImRect(snap(rect.Min.x, scale.x), snap(rect.Min.y, scale.y),
        snap(rect.Max.x, scale.x), snap(rect.Max.y, scale.y));
}

void DrawActiveTab(ImDrawList* draw, const ImRect& rect, ImU32 color) {
    const float x = rect.Min.x, y = rect.Min.y, right = rect.Max.x, bottom = rect.Max.y;
    const float radius = std::min(10.0f, rect.GetWidth() / 6.0f);
    const float foot = radius;
    constexpr float curve = .55228475f;
    draw->PathLineTo(ImVec2(x, bottom));
    draw->PathBezierCubicCurveTo(ImVec2(x + foot * curve, bottom),
        ImVec2(x + foot, bottom - foot * (1 - curve)), ImVec2(x + foot, bottom - foot));
    draw->PathLineTo(ImVec2(x + foot, y + radius));
    draw->PathBezierCubicCurveTo(ImVec2(x + foot, y + radius * (1 - curve)),
        ImVec2(x + foot + radius * (1 - curve), y), ImVec2(x + foot + radius, y));
    draw->PathLineTo(ImVec2(right - foot - radius, y));
    draw->PathBezierCubicCurveTo(ImVec2(right - foot - radius * (1 - curve), y),
        ImVec2(right - foot, y + radius * (1 - curve)), ImVec2(right - foot, y + radius));
    draw->PathLineTo(ImVec2(right - foot, bottom - foot));
    draw->PathBezierCubicCurveTo(ImVec2(right - foot, bottom - foot * (1 - curve)),
        ImVec2(right - foot * curve, bottom), ImVec2(right, bottom));
    draw->PathFillConcave(color);
}

void DrawControlFeedback(const ImRect& rect, const Palette& palette,
    float selected, bool hovered, bool held, bool focused) {
    auto* storage = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetItemID();
    float hover = storage->GetFloat(ImHashStr("header-hover", 0, id));
    Approach(hover, hovered ? 1.0f : 0.0f, hovered ? 24.0f : 16.0f);
    storage->SetFloat(ImHashStr("header-hover", 0, id), hover);
    float selection = storage->GetFloat(ImHashStr("header-selected", 0, id));
    Approach(selection, selected);
    storage->SetFloat(ImHashStr("header-selected", 0, id), selection);
    ImVec4 fill = Blend(palette.hover, palette.selected, selection);
    if (held && hovered) fill = palette.pressed;
    fill.w = held && hovered ? 1.0f : std::max(hover, selection);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(fill), 8.0f);
    if (focused && ImGui::GetCurrentContext()->NavCursorVisible)
        draw->AddRect(ImVec2(rect.Min.x + 1, rect.Min.y + 1),
            ImVec2(rect.Max.x - 1, rect.Max.y - 1), ImGui::GetColorU32(palette.focus), 7.0f, 0, 1.5f);
}

void DrawProjectPill(ImDrawList* draw, ImVec2 minimum, ImVec2 maximum,
    float scale, const Palette& palette, ProjectPillState state) {
    const float amount = state.automatic
        ? (state.selected ? .31f : state.hovered ? .18f : .08f)
        : (state.selected ? .56f : state.hovered ? .28f : .16f);
    const ImVec4 fill = Blend(palette.caption, palette.text, amount);
    draw->AddRectFilled(minimum, maximum, ImGui::GetColorU32(fill), 8.f*scale);
    if (state.automatic) {
        ImVec4 stripe = palette.text; stripe.w *= .32f;
        const float inset = 5.f*scale, step = 6.f*scale;
        for (float x=minimum.x+inset; x+step<=maximum.x-inset; x+=step)
            draw->AddLine({x,maximum.y-2.f*scale}, {x+step,minimum.y+2.f*scale},
                ImGui::GetColorU32(stripe), scale);
    }
    if (state.dirty || state.focused)
        draw->AddRect(minimum, maximum, ImGui::GetColorU32(state.focused ? palette.focus : palette.mutedText),
            8.f*scale, 0, scale);
}
}
