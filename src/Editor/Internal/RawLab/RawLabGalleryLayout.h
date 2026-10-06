#pragma once

#include <algorithm>
#include <cstddef>
#include <cmath>

namespace Stack::Editor::RawLabInternal {

struct GalleryVisibleRange {
    int first = 0;
    int lastExclusive = 0;
};

struct FilmstripHorizontalLayout {
    float contentWidth = 0.0f;
    float leadingOffset = 0.0f;
    bool centered = false;
};

inline FilmstripHorizontalLayout ComputeFilmstripHorizontalLayout(
    std::size_t itemCount,
    float itemWidth,
    float gap,
    float availableWidth) {
    FilmstripHorizontalLayout layout;
    if (itemCount == 0u || !std::isfinite(itemWidth) ||
        !std::isfinite(gap) || !std::isfinite(availableWidth) ||
        itemWidth <= 0.0f || availableWidth <= 0.0f) {
        return layout;
    }
    layout.contentWidth = itemWidth * static_cast<float>(itemCount) +
        std::max(0.0f, gap) * static_cast<float>(itemCount - 1u);
    layout.centered = layout.contentWidth <= availableWidth;
    if (layout.centered) {
        layout.leadingOffset =
            std::max(0.0f, (availableWidth - layout.contentWidth) * 0.5f);
    }
    return layout;
}

inline bool ShouldUseSideFilmstripPerforations(
    int imageWidth,
    int imageHeight,
    float minimumPortraitTallness = 1.12f) {
    return imageWidth > 0 && imageHeight > 0 &&
        std::isfinite(minimumPortraitTallness) &&
        minimumPortraitTallness > 1.0f &&
        static_cast<float>(imageHeight) >=
            static_cast<float>(imageWidth) * minimumPortraitTallness;
}

// Returns the item interval whose rectangles overlap the viewport expanded by
// prefetchPixels. Items are laid out at contentOrigin + index * stridePixels.
// The extra item at the upper boundary is intentional: it keeps fractional
// scrolling and animated layouts from exposing a one-frame blank edge.
inline GalleryVisibleRange ComputeGalleryVisibleRange(
    int itemCount,
    float itemExtentPixels,
    float gapPixels,
    float contentOriginPixels,
    float viewportMinPixels,
    float viewportMaxPixels,
    float prefetchPixels) {
    if (itemCount <= 0 || !std::isfinite(itemExtentPixels) ||
        !std::isfinite(gapPixels) || itemExtentPixels <= 0.0f) {
        return {};
    }

    const float stride = std::max(1.0f, itemExtentPixels + gapPixels);
    const float expandedMin = std::min(viewportMinPixels, viewportMaxPixels) -
        std::max(0.0f, prefetchPixels);
    const float expandedMax = std::max(viewportMinPixels, viewportMaxPixels) +
        std::max(0.0f, prefetchPixels);
    const int first = std::clamp(
        static_cast<int>(std::floor((expandedMin - contentOriginPixels) / stride)),
        0,
        itemCount);
    const int last = std::clamp(
        static_cast<int>(std::ceil((expandedMax - contentOriginPixels) / stride)) + 1,
        first,
        itemCount);
    return { first, last };
}

} // namespace Stack::Editor::RawLabInternal
