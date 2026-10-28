#pragma once

#include "RawLabGalleryLayout.h"
#include <imgui.h>
#include <algorithm>

namespace Stack::Editor::RawLabInternal {

// Keep the complete content extent without submitting one invisible widget per
// offscreen photo. Motion/anchor geometry remains owned by the caller and is
// recorded separately, including destinations outside these visible rows.
template <typename DrawTile>
void SubmitGalleryGrid(int count, int columns, ImVec2 tileSize, float gap,
    GalleryVisibleRange rows, const DrawTile& drawTile) {
    if (count <= 0 || columns <= 0) return;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const int rowCount = (count + columns - 1) / columns;
    // SameLine supplied the horizontal gap; row advancement used the host's
    // style spacing (native/MFD gallery hosts need not use the grid's 16px).
    const float rowGap = ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 extent(
        std::min(count, columns) * tileSize.x + (std::min(count, columns) - 1) * gap,
        rowCount * tileSize.y + (rowCount - 1) * rowGap);
    const int firstRow = std::clamp(rows.first, 0, rowCount);
    const int lastRow = std::clamp(rows.lastExclusive, firstRow, rowCount);
    for (int row = firstRow; row < lastRow; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int index = row * columns + column;
            if (index >= count) break;
            ImGui::SetCursorScreenPos(ImVec2(origin.x + column * (tileSize.x + gap),
                origin.y + row * (tileSize.y + rowGap)));
            drawTile(index);
        }
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(extent);
}

}
