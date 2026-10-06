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
    float topInset = 0.0f) {
    if (drawList == nullptr) {
        return;
    }

    // Gallery selection is deliberately communicated through a neutral tonal
    // change. Keep the ordinal in the tile's metadata instead of decorating
    // the image with an accent badge or outline.
    (void)focused;
    (void)selectionOrdinal;
    constexpr float rounding = 6.0f;
    if ((!filmstripPerforations && selected) || hovered) {
        // Selected tiles are a little darker than hover-only tiles. Using a
        // neutral black wash keeps this state independent of the accent color
        // and avoids the old outline/infill treatment.
        const ImU32 shade = IM_COL32(
            0,
            0,
            0,
            (!filmstripPerforations && selected) ? 92 : 42);
        drawList->AddRectFilled(
            ImVec2(minimum.x, minimum.y + std::max(0.0f, topInset)),
            maximum,
            shade,
            rounding);
    }
    if (filmstripPerforations && selected) {
        constexpr float squareSize = 3.0f;
        constexpr float squareGap = 4.0f;
        constexpr float horizontalInset = 7.0f;
        constexpr float verticalInset = 4.0f;
        const ImU32 color = ImGui::GetColorU32(
            ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive));
        if (sideFilmstripPerforations) {
            const float availableHeight = std::max(
                0.0f, maximum.y - minimum.y - horizontalInset * 2.0f);
            const int squareCount = std::max(
                1,
                static_cast<int>(
                    (availableHeight + squareGap) /
                    (squareSize + squareGap)));
            const float stripHeight =
                squareCount * squareSize +
                std::max(0, squareCount - 1) * squareGap;
            const float startY = minimum.y +
                (maximum.y - minimum.y - stripHeight) * 0.5f;
            for (int index = 0; index < squareCount; ++index) {
                const float y = startY +
                    static_cast<float>(index) * (squareSize + squareGap);
                drawList->AddRectFilled(
                    ImVec2(minimum.x + verticalInset, y),
                    ImVec2(minimum.x + verticalInset + squareSize,
                           y + squareSize),
                    color,
                    0.6f);
                drawList->AddRectFilled(
                    ImVec2(maximum.x - verticalInset - squareSize, y),
                    ImVec2(maximum.x - verticalInset, y + squareSize),
                    color,
                    0.6f);
            }
        } else {
            const float availableWidth = std::max(
                0.0f, maximum.x - minimum.x - horizontalInset * 2.0f);
            const int squareCount = std::max(
                1,
                static_cast<int>(
                    (availableWidth + squareGap) /
                    (squareSize + squareGap)));
            const float stripWidth =
                squareCount * squareSize +
                std::max(0, squareCount - 1) * squareGap;
            const float startX = minimum.x +
                (maximum.x - minimum.x - stripWidth) * 0.5f;
            for (int index = 0; index < squareCount; ++index) {
                const float x = startX +
                    static_cast<float>(index) * (squareSize + squareGap);
                drawList->AddRectFilled(
                    ImVec2(x, minimum.y + verticalInset),
                    ImVec2(x + squareSize,
                           minimum.y + verticalInset + squareSize),
                    color,
                    0.6f);
                drawList->AddRectFilled(
                    ImVec2(x, maximum.y - verticalInset - squareSize),
                    ImVec2(x + squareSize, maximum.y - verticalInset),
                    color,
                    0.6f);
            }
        }
    }
}

} // namespace Stack::RawGallerySelectionVisuals
