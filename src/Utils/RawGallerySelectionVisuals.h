#pragma once

#include <imgui.h>

#include <algorithm>
#include <cstddef>

namespace Stack::RawGallerySelectionVisuals {

inline void DrawTileSelection(
    ImDrawList* drawList,
    const ImVec2& minimum,
    const ImVec2& maximum,
    bool selected,
    bool focused,
    bool hovered,
    std::size_t selectionOrdinal,
    bool filmstripPerforations = false,
    bool sideFilmstripPerforations = false,
    float topInset = 0.0f,
    const ImVec2* imageMinimum = nullptr,
    const ImVec2* imageMaximum = nullptr) {
    if (drawList == nullptr) {
        return;
    }

    (void)focused;
    (void)selectionOrdinal;
    (void)filmstripPerforations;
    (void)sideFilmstripPerforations;
    const ImVec2 lo = imageMinimum ? *imageMinimum :
        ImVec2(minimum.x, minimum.y + std::max(0.0f, topInset));
    const ImVec2 hi = imageMaximum ? *imageMaximum : maximum;
    if (selected) {
        // A narrow light stroke with a dark keyline remains legible against
        // both the photograph and every workspace shade. Never dim the photo.
        const float scale = std::max(.75f, ImGui::GetFontSize() / 16.f);
        drawList->AddRect(lo, hi, IM_COL32(12, 12, 12, 235), 4.f * scale, 0, 4.f * scale);
        drawList->AddRect(lo, hi, IM_COL32(242, 242, 242, 255), 4.f * scale, 0, 2.f * scale);
    } else if (hovered) {
        drawList->AddRectFilled(lo, hi, IM_COL32(0, 0, 0, 42), 6.f);
    }
}

} // namespace Stack::RawGallerySelectionVisuals
