#pragma once

#include "AppHeaderStyle.h"
#include "NavigationRail.h"
#include "imgui_internal.h"
#include <cstring>

namespace Stack::Navigation {

inline constexpr float BarExtent = 52.f;
inline constexpr float ButtonExtent = 38.f;
inline constexpr float BarPadding = 7.f;

inline void UpdateEdgeReveal(float& amount, double& visibleUntil, bool focused,
    bool atEdge, bool hovered, bool interacting, bool popupOpen, bool reducedMotion) {
    const double now = ImGui::GetTime();
    if (focused && (hovered || interacting || popupOpen ||
            (atEdge && !ImGui::IsAnyMouseDown() && !ImGui::IsAnyItemActive())))
        visibleUntil = now + .65;
    const float target = focused && now < visibleUntil ? 1.f : 0.f;
    if (!focused) visibleUntil = 0.0;
    if (reducedMotion) amount = target;
    else Stack::Header::Approach(amount, target, 18.f);
}

inline bool IconButton(const char* label, Glyph glyph, bool selected, bool enabled,
    float scale, const Stack::Header::Palette& palette, const char* disabledReason = nullptr) {
    const float size = ButtonExtent * scale;
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(size, size), ImGuiButtonFlags_EnableNav);
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const auto key = ImGui::GetItemID();
    auto* storage = ImGui::GetStateStorage();
    float amount = storage->GetFloat(key);
    Stack::Header::Approach(amount, selected ? 1.f : ImGui::IsItemHovered() ? .6f : 0.f, 24.f);
    storage->SetFloat(key, amount);
    auto* draw = ImGui::GetWindowDrawList();
    if (amount > .001f) {
        ImVec4 fill = ImGui::IsItemActive() ? palette.pressed : palette.selected;
        fill.w *= amount;
        draw->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(fill), 9.f * scale);
    }
    if (ImGui::IsItemFocused()) draw->AddRect(rect.Min, rect.Max,
        ImGui::GetColorU32(palette.focus), 9.f * scale, 0, 1.25f * scale);
    const float glyphSize = (glyph == Glyph::Stack ? 24.f : 21.f) * scale;
    DrawGlyph(draw, glyph, ImVec2(rect.Min.x + (size - glyphSize) * .5f,
        rect.Min.y + (size - glyphSize) * .5f), glyphSize,
        ImGui::GetColorU32(selected ? palette.text : palette.mutedText));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (!enabled && disabledReason && *disabledReason) ImGui::SetTooltip("%s", disabledReason);
        else {
            const char* suffix = std::strstr(label, "###");
            ImGui::SetTooltip("%.*s", int(suffix ? suffix - label : std::strlen(label)), label);
        }
    }
    ImGui::EndDisabled();
    return clicked;
}

} // namespace Stack::Navigation
