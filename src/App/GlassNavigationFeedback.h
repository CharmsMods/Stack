// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "AppHeaderStyle.h"
#include "Animation/ControlMotion.h"
#include "ControlMotionAdapter.h"
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
// Owner-retained motion is independent of ImGui storage and object geometry.
using GlassControlState = Stack::Animation::ControlSnapshot;
inline GlassControlState UpdateGlassControlState(Stack::Animation::ControlMotion& motion, bool selected) {
    return motion.Update(Stack::UiMotion::LastItemInput(selected));
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
    // One restrained response layer preserves the optical surface underneath.
    // Hover remains visible on selected controls; pressure already changes the
    // shared optical elevation and glyph, so it needs only a small ink response.
    wash.w *= std::min(.125f,.06f*hover + .04f*selection + .025f*press);
    draw->AddRectFilled(min,max,ImGui::GetColorU32(wash),.5f*std::min(max.x-min.x,max.y-min.y));
    if (selection > .001f) {
        ImVec4 mark(neutral,neutral,neutral,palette.text.w*.8f*selection);
        const float centerY = (min.y+max.y)*.5f;
        draw->AddLine(ImVec2(rect.Min.x+scale,centerY-5.f*scale),
            ImVec2(rect.Min.x+scale,centerY+5.f*scale),ImGui::GetColorU32(mark),1.5f*scale);
    }
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
