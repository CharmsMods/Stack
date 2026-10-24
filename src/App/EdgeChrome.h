#pragma once

#include "AppHeaderStyle.h"
#include "NavigationRail.h"
#include "GlassNavigationFeedback.h"
#include "Renderer/GlassChrome.h"
#include "imgui_internal.h"
#include <cstring>

namespace Stack::Navigation {

inline constexpr float BarExtent = 52.f;
inline constexpr float ButtonExtent = 38.f;
inline constexpr float BarPadding = 7.f;

// A remembered navigation focus is not ongoing interaction. Renew the reveal
// while navigating, then let the ordinary grace period expire after release.
inline bool NavigationInputActive() {
    return ImGui::GetIO().NavVisible &&
        (ImGui::IsKeyDown(ImGuiKey_Tab) || ImGui::IsKeyDown(ImGuiKey_LeftArrow) ||
         ImGui::IsKeyDown(ImGuiKey_RightArrow) || ImGui::IsKeyDown(ImGuiKey_UpArrow) ||
         ImGui::IsKeyDown(ImGuiKey_DownArrow) || ImGui::IsKeyDown(ImGuiKey_Enter) ||
         ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyDown(ImGuiKey_GamepadDpadLeft) ||
         ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight) || ImGui::IsKeyDown(ImGuiKey_GamepadDpadUp) ||
         ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown) || ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown));
}

inline void UpdateEdgeReveal(float& amount, double& visibleUntil, bool focused,
    bool atEdge, bool hovered, bool interacting, bool popupOpen, bool reducedMotion) {
    const double now = ImGui::GetTime();
    if (focused && (hovered || interacting || popupOpen ||
            (atEdge && !ImGui::IsAnyMouseDown() && !ImGui::IsAnyItemActive())))
        visibleUntil = now + .65;
    const float target = focused && now < visibleUntil ? 1.f : 0.f;
    if (!focused) visibleUntil = 0.0;
    if (reducedMotion) amount = target;
    else Stack::Header::ApproachMotion(amount, target, .24f);
}

inline bool IconButton(const char* label, Glyph glyph, bool selected, bool enabled,
    float scale, const Stack::Header::Palette& palette, const char* disabledReason = nullptr, bool glass = false, Stack::Renderer::GlassChrome* material = nullptr, bool objectOutsideWindow = false) {
    const float size = ButtonExtent * scale;
    ImGui::BeginDisabled(!enabled);
    if (glass) ImGui::PushStyleColor(ImGuiCol_NavCursor,ImVec4(0,0,0,0));
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(size, size), ImGuiButtonFlags_EnableNav);
    if (glass) ImGui::PopStyleColor();
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const auto key = ImGui::GetItemID();
    auto* storage = ImGui::GetStateStorage();
    float amount = storage->GetFloat(key);
    Stack::Header::Approach(amount, selected ? 1.f : ImGui::IsItemHovered() ? .6f : 0.f, 24.f);
    storage->SetFloat(key, amount);
    auto* draw = ImGui::GetWindowDrawList();
    if (!glass && amount > .001f) {
        ImVec4 fill = ImGui::IsItemActive() ? palette.pressed : palette.selected;
        fill.w *= amount;
        draw->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(fill), 9.f * scale);
    }
    if (!glass && ImGui::IsItemFocused()) draw->AddRect(rect.Min, rect.Max,
        ImGui::GetColorU32(palette.focus), 9.f * scale, 0, 1.25f * scale);
    if (glass) GlassObjectBacking(draw,rect.Min,rect.Max,12.f*scale,palette);
    if (glass && material) {
        // A small sibling toggle window clips its glyphs/hit target tightly,
        // but the material may cast its shadow inside the approved column.
        if (objectOutsideWindow) {
            const auto* viewport=ImGui::GetWindowViewport();
            draw->PushClipRect(viewport->Pos,ImVec2(viewport->Pos.x+viewport->Size.x,
                viewport->Pos.y+viewport->Size.y),false);
        }
        material->AddObject(draw,rect.Min,rect.Max,palette.caption,12.f*scale);
        if (objectOutsideWindow) draw->PopClipRect();
    }
    // Feedback, keyboard focus and the single sharp glyph share adaptive ink.
    const bool adaptiveInk = glass && material && material->BeginInk(draw,rect.Min,rect.Max,ImGui::GetItemID());
    const float press = glass ? GlassControlFeedback(rect,selected,scale,palette) : 0.f;
    if (glass) GlassKeyboardFocus(rect,scale,palette);
    const float glyphSize = (glyph == Glyph::Stack ? (glass ? 25.f : 24.f) :
        (glass ? 23.f : 21.f)) * scale * (1.f - .045f*press);
    const ImVec2 glyphMin(rect.Min.x + (size - glyphSize) * .5f,
        rect.Min.y + (size - glyphSize) * .5f);
    if (adaptiveInk) {
        ImVec4 ink=palette.text; ink.w *= selected || ImGui::IsItemHovered() ? 1.f : .92f;
        DrawGlyph(draw,glyph,glyphMin,glyphSize,ImGui::GetColorU32(ink));
        material->EndInk(draw);
    } else if (glass) {
        // On an unavailable material, the solid core supplies known contrast.
        // Keep the original glyph sharp rather than adding an outlined halo.
        DrawGlyph(draw,glyph,glyphMin,glyphSize,ImGui::GetColorU32(palette.text));
    } else {
        DrawGlyph(draw,glyph,glyphMin,glyphSize,
            ImGui::GetColorU32(selected ? palette.text : palette.mutedText));
    }
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
