// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "AppHeaderStyle.h"
#include "imgui_internal.h"

namespace Stack::Navigation {
// Decorates the existing item, never adds a hit target or delays its action.
// Window-local ImGui storage keeps animation attached to the item's stable ID.
// Returns the press amount for optional foreground-only compression.
inline float GlassControlFeedback(const ImRect& rect, bool selected, float scale,
    const Stack::Header::Palette& palette) {
    const ImGuiID id = ImGui::GetItemID();
    auto* storage = ImGui::GetStateStorage();
    const auto hoverKey = ImHashStr("glass-hover",0,id);
    const auto pressKey = ImHashStr("glass-press",0,id);
    const auto selectKey = ImHashStr("glass-selected",0,id);
    float hover = storage->GetFloat(hoverKey);
    float press = storage->GetFloat(pressKey);
    float selection = storage->GetFloat(selectKey);
    Stack::Header::Approach(hover,ImGui::IsItemHovered() ? 1.f : 0.f,26.f);
    // Press feedback starts immediately; only its release is eased.
    if (ImGui::IsItemActive()) press = 1.f;
    else Stack::Header::Approach(press,0.f,30.f);
    Stack::Header::Approach(selection,selected ? 1.f : 0.f,22.f);
    storage->SetFloat(hoverKey,hover); storage->SetFloat(pressKey,press);
    storage->SetFloat(selectKey,selection);
    const float intensity = std::max(hover,selection);
    if (intensity <= .001f && press <= .001f) return press;
    const float inset = (1.f + press * 1.25f) * scale;
    ImVec2 min(rect.Min.x+inset,rect.Min.y+inset), max(rect.Max.x-inset,rect.Max.y-inset);
    auto* draw = ImGui::GetWindowDrawList();
    // A light control wash sits above the one continuous glass surface.
    // No bevel or enclosing rim: controls are not little glass plaques.
    const float neutral = palette.text.x*.2126f + palette.text.y*.7152f + palette.text.z*.0722f;
    ImVec4 wash(neutral,neutral,neutral,palette.text.w);
    wash.w *= .09f*hover + .12f*selection + .065f*press;
    // A low-opacity outer shoulder softens the hover without another material
    // pass. The visual compresses on press; the original item rectangle does not.
    ImVec4 shoulder = wash; shoulder.w *= .25f;
    draw->AddRectFilled(ImVec2(min.x-scale,min.y-scale),
        ImVec2(max.x+scale,max.y+scale),ImGui::GetColorU32(shoulder),10.f*scale);
    draw->AddRectFilled(min,max,ImGui::GetColorU32(wash),9.f*scale);
    if (selection > .001f) {
        ImVec4 mark(neutral,neutral,neutral,palette.text.w*.8f*selection);
        const float centerY = (min.y+max.y)*.5f;
        draw->AddLine(ImVec2(rect.Min.x+scale,centerY-5.f*scale),
            ImVec2(rect.Min.x+scale,centerY+5.f*scale),ImGui::GetColorU32(mark),1.5f*scale);
    }
    return press;
}
}
