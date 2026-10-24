// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "AppHeaderStyle.h"
#include "imgui_internal.h"

namespace Stack::Navigation {
// Decorates the existing item, never adds a hit target or delays its action.
// Window-local ImGui storage keeps animation attached to the item's stable ID.
inline void GlassControlFeedback(const ImRect& rect, bool selected, float scale,
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
    if (intensity <= .001f && press <= .001f) return;
    const float inset = (1.f + press * 1.25f) * scale;
    ImVec2 min(rect.Min.x+inset,rect.Min.y+inset), max(rect.Max.x-inset,rect.Max.y-inset);
    auto* draw = ImGui::GetWindowDrawList();
    ImVec4 rim = palette.text; rim.w *= (.12f*hover+.22f*selection+.12f*press);
    draw->AddRect(min,max,ImGui::GetColorU32(rim),7.f*scale,0,scale);
    ImVec4 sheen = palette.text; sheen.w *= .20f*hover*(1.f-.6f*press);
    draw->AddLine(ImVec2(min.x+7.f*scale,min.y),ImVec2(max.x-7.f*scale,min.y),
        ImGui::GetColorU32(sheen),scale);
}
}
