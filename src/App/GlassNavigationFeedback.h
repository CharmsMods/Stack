// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "AppHeaderStyle.h"
#include "imgui_internal.h"

namespace Stack::Navigation {
// A small opaque core remains readable if the optional GPU material fails.
// It is queued after backdrop capture and underneath fully opaque glass, not a
// CPU implementation of the effect. Inset avoids doubling the material's AA rim.
inline void GlassObjectBacking(ImDrawList* draw, ImVec2 min, ImVec2 max,
    float radius, const Stack::Header::Palette& palette) {
    const auto scale=ImGui::GetIO().DisplayFramebufferScale;
    const float inset=2.f/std::max(.5f,std::min(scale.x,scale.y));
    if (max.x-min.x<=2.f*inset || max.y-min.y<=2.f*inset) return;
    ImVec4 fill=palette.caption; fill.w=1.f;
    draw->AddRectFilled(ImVec2(min.x+inset,min.y+inset),ImVec2(max.x-inset,max.y-inset),
        ImGui::GetColorU32(fill),std::max(0.f,radius-inset));
}
// Decorates the existing item, never adds a hit target or delays its action.
// Window-local ImGui storage keeps animation attached to the item's stable ID.
struct GlassControlState {
    float hover=0.f, press=0.f, selection=0.f;
    // Lower the same optical body on press, without changing its hitbox or shape.
    float Elevation() const { return 1.f-.8f*press; }
};
// Evaluate once after the item, before queuing its material. Drawing the ink
// later consumes this snapshot rather than advancing animation a second time.
inline GlassControlState UpdateGlassControlState(bool selected) {
    const ImGuiID id = ImGui::GetItemID();
    auto* storage = ImGui::GetStateStorage();
    const auto hoverKey = ImHashStr("glass-hover",0,id);
    const auto pressKey = ImHashStr("glass-press",0,id);
    const auto selectKey = ImHashStr("glass-selected",0,id);
    const auto frameKey = ImHashStr("glass-frame",0,id);
    const int previousFrame=storage->GetInt(frameKey,-2);
    if (previousFrame==ImGui::GetFrameCount())
        return {storage->GetFloat(hoverKey),storage->GetFloat(pressKey),storage->GetFloat(selectKey)};
    const bool continuous = previousFrame == ImGui::GetFrameCount()-1;
    storage->SetInt(frameKey,ImGui::GetFrameCount());
    float hover = continuous ? storage->GetFloat(hoverKey) : 0.f;
    float press = continuous ? storage->GetFloat(pressKey) : 0.f;
    float selection = continuous ? storage->GetFloat(selectKey) : (selected ? 1.f : 0.f);
    Stack::Header::Approach(hover,ImGui::IsItemHovered() ? 1.f : 0.f,26.f);
    // Press feedback starts immediately; only its release is eased.
    const bool mouseActive=ImGui::GetCurrentContext()->ActiveIdSource==ImGuiInputSource_Mouse;
    const bool held=ImGui::IsItemActive() && (!mouseActive ||
        ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride));
    if (held) press = 1.f;
    else Stack::Header::Approach(press,0.f,30.f);
    Stack::Header::Approach(selection,selected ? 1.f : 0.f,22.f);
    storage->SetFloat(hoverKey,hover); storage->SetFloat(pressKey,press);
    storage->SetFloat(selectKey,selection);
    return {hover,press,selection};
}
inline void DrawGlassControlFeedback(const ImRect& rect, const GlassControlState& state,
    float scale, const Stack::Header::Palette& palette) {
    const float hover=state.hover, press=state.press, selection=state.selection;
    const float intensity = std::max(hover,selection);
    if (intensity <= .001f && press <= .001f) return;
    const float inset = (1.f + press * 1.25f) * scale;
    ImVec2 min(rect.Min.x+inset,rect.Min.y+inset), max(rect.Max.x-inset,rect.Max.y-inset);
    auto* draw = ImGui::GetWindowDrawList();
    // Quiet feedback sits above each independent optical object.
    const float neutral = palette.text.x*.2126f + palette.text.y*.7152f + palette.text.z*.0722f;
    ImVec4 wash(neutral,neutral,neutral,palette.text.w);
    wash.w *= .09f*hover + .12f*selection + .065f*press;
    // A low-opacity outer shoulder softens the hover without another material
    // pass. The visual compresses on press; the original item rectangle does not.
    ImVec4 shoulder = wash; shoulder.w *= .25f;
    draw->AddRectFilled(ImVec2(min.x-scale,min.y-scale),
        ImVec2(max.x+scale,max.y+scale),ImGui::GetColorU32(shoulder),.5f*std::min(max.x-min.x+2.f*scale,max.y-min.y+2.f*scale));
    draw->AddRectFilled(min,max,ImGui::GetColorU32(wash),.5f*std::min(max.x-min.x,max.y-min.y));
    if (selection > .001f) {
        ImVec4 mark(neutral,neutral,neutral,palette.text.w*.8f*selection);
        const float centerY = (min.y+max.y)*.5f;
        draw->AddLine(ImVec2(rect.Min.x+scale,centerY-5.f*scale),
            ImVec2(rect.Min.x+scale,centerY+5.f*scale),ImGui::GetColorU32(mark),1.5f*scale);
    }
}
// Compatibility helper for consumers needing foreground feedback only.
inline float GlassControlFeedback(const ImRect& rect, bool selected, float scale,
    const Stack::Header::Palette& palette) {
    const auto state=UpdateGlassControlState(selected);
    DrawGlassControlFeedback(rect,state,scale,palette);
    return state.press;
}
// Keyboard focus is separate from the persistent left-side selection mark.
// A neutral bottom dash avoids a colored outline after mouse use or Escape.
inline void GlassKeyboardFocus(const ImRect& rect, float scale,
    const Stack::Header::Palette& palette) {
    if (!ImGui::IsItemFocused() || !ImGui::GetIO().NavVisible) return;
    ImVec4 ink=palette.text; ink.w *= .85f;
    const float center=(rect.Min.x+rect.Max.x)*.5f;
    const float half=std::min(5.f*scale,(rect.Max.x-rect.Min.x)*.2f);
    const float y=rect.Max.y-3.f*scale;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(center-half,y),ImVec2(center+half,y),
        ImGui::GetColorU32(ink),1.5f*scale);
}

}
