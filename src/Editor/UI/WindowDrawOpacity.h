#pragma once
#include "imgui_internal.h"

namespace Stack::Editor {
inline void TranslateWindowDraw(ImGuiWindow* window,ImVec2 offset) {
    if(!window || (offset.x==0 && offset.y==0))return;
    for(auto& vertex:window->DrawList->VtxBuffer) {
        vertex.pos.x+=offset.x;vertex.pos.y+=offset.y;
    }
    for(auto* child:window->DC.ChildWindows)
        if(child->Active)TranslateWindowDraw(child,offset);
}
// Fade completed window geometry once so custom explicit-color plots and
// ordinary widgets share one opacity. Caller leaves style alpha unchanged.
inline void ApplyWindowDrawOpacity(ImGuiWindow* window, float opacity) {
    if (!window || opacity >= 1.f) return;
    for (auto& vertex : window->DrawList->VtxBuffer) {
        auto& color = vertex.col;
        color = (color & ~IM_COL32_A_MASK) |
            (static_cast<ImU32>((color >> IM_COL32_A_SHIFT)*opacity) << IM_COL32_A_SHIFT);
    }
    for (auto* child : window->DC.ChildWindows)
        if (child->Active) ApplyWindowDrawOpacity(child,opacity);
}
}
