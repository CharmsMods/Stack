#pragma once
#include "imgui_internal.h"

namespace Stack::Editor {
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
