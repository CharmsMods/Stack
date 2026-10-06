#pragma once
#include "Raw/RawViewportRegion.h"
#include <imgui.h>

namespace Raw {
inline void DrawViewportDetail(ImDrawList& list, unsigned int texture, const ViewportRegion& region,
    ImVec2 minimum, ImVec2 maximum) {
    const ImVec2 size(maximum.x - minimum.x, maximum.y - minimum.y);
    const ImVec2 left = region.Valid() ? ImVec2(minimum.x + size.x * region.x / region.fullWidth,
        minimum.y + size.y * region.y / region.fullHeight) : minimum;
    const ImVec2 right = region.Valid() ? ImVec2(minimum.x + size.x * (region.x + region.width) / region.fullWidth,
        minimum.y + size.y * (region.y + region.height) / region.fullHeight) : maximum;
    list.AddImage((ImTextureID)(intptr_t)texture, left, right, ImVec2(0, 1), ImVec2(1, 0));
}
}
