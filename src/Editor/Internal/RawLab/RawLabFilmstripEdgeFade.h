#pragma once

#include <algorithm>
#include <imgui.h>

namespace Stack::Editor::RawLabInternal {

// Composite the entire strip back into its opaque Gallery surface. Unlike
// vertex-alpha edits, this also fades a large image quad only near its edges.
// Fixed geometry: no texture reads, offscreen targets or per-card traversal.
inline void DrawFilmstripEdgeFade(ImDrawList* draw, ImVec2 minimum,
    ImVec2 maximum, float feather, ImVec4 surface) {
    feather = std::min(feather, (maximum.x - minimum.x) * .25f);
    if (feather <= 0.f || maximum.y <= minimum.y) return;
    constexpr int segments = 12;
    const auto color = [&](float t) {
        surface.w = 1.f - t * t * (3.f - 2.f * t);
        return ImGui::GetColorU32(surface);
    };
    draw->PushClipRect(minimum, maximum, true);
    for (int i = 0; i < segments; ++i) {
        const float a = static_cast<float>(i) / segments;
        const float b = static_cast<float>(i + 1) / segments;
        const ImU32 outer = color(a), inner = color(b);
        draw->AddRectFilledMultiColor(
            {minimum.x + feather * a, minimum.y},
            {minimum.x + feather * b, maximum.y}, outer, inner, inner, outer);
        draw->AddRectFilledMultiColor(
            {maximum.x - feather * b, minimum.y},
            {maximum.x - feather * a, maximum.y}, inner, outer, outer, inner);
    }
    draw->PopClipRect();
}

} // namespace Stack::Editor::RawLabInternal
