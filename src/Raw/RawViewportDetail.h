#pragma once
#include "Raw/RawViewportRegion.h"

namespace Raw {
// Compare coverage in image coordinates, independent of the render's proxy grid.
inline bool ViewportDetailContains(const ViewportRegion& outer, const ViewportRegion& inner) {
    if (!outer.Valid()) return true;
    if (!inner.Valid()) return !outer.Partial();
    const auto atMost = [](int a, int aFull, int b, int bFull) {
        return std::int64_t(a) * bFull <= std::int64_t(b) * aFull;
    };
    return atMost(outer.x, outer.fullWidth, inner.x, inner.fullWidth) &&
        atMost(outer.y, outer.fullHeight, inner.y, inner.fullHeight) &&
        atMost(inner.x + inner.width, inner.fullWidth, outer.x + outer.width, outer.fullWidth) &&
        atMost(inner.y + inner.height, inner.fullHeight, outer.y + outer.height, outer.fullHeight);
}

inline bool ViewportDetailCoversNativeRegion(const ViewportRegion& rendered, int width, int height,
    const ViewportRegion& visible) {
    if (!visible.Valid() || width <= 0 || height <= 0) return false;
    if (!rendered.Valid()) return width == visible.fullWidth && height == visible.fullHeight;
    return rendered.fullWidth == visible.fullWidth && rendered.fullHeight == visible.fullHeight &&
        width == rendered.width && height == rendered.height && ViewportDetailContains(rendered, visible);
}

inline bool ViewportDetailCoversDisplayRegion(const ViewportRegion& rendered, int width, int height,
    const ViewportRegion& visible, int physicalWidth, int physicalHeight) {
    if (!visible.Valid() || width <= 0 || height <= 0 || physicalWidth <= 0 || physicalHeight <= 0 ||
        !ViewportDetailContains(rendered, visible)) return false;
    if (ViewportDetailCoversNativeRegion(rendered, width, height, visible)) return true;
    // Compare each axis in source coordinates. Merely containing the requested
    // crop does not prove that a proxy has enough samples for its display size.
    const double samplesX = rendered.Valid() ? double(width) * rendered.fullWidth / rendered.width : width;
    const double samplesY = rendered.Valid() ? double(height) * rendered.fullHeight / rendered.height : height;
    return samplesX * visible.width / visible.fullWidth >= physicalWidth &&
        samplesY * visible.height / visible.fullHeight >= physicalHeight;
}

// Pixel density in the full image's normalized coordinate system. Region
// dimensions describe the rendered raster, which may itself be a proxy.
inline double ViewportDetailDensity(const ViewportRegion& region, int width, int height) {
    if (width <= 0 || height <= 0) return 0;
    if (!region.Valid()) return std::max(width, height);
    return std::max(double(width) * region.fullWidth / region.width,
        double(height) * region.fullHeight / region.height);
}
inline bool RetainViewportDetail(std::size_t previousContent, std::size_t nextContent,
    const ViewportRegion& previous, int previousWidth, int previousHeight,
    const ViewportRegion& next, int nextWidth, int nextHeight) {
    if (!previousContent || previousContent != nextContent || previousWidth <= 0 || previousHeight <= 0) return false;
    const double oldDensity = ViewportDetailDensity(previous, previousWidth, previousHeight);
    const double newDensity = ViewportDetailDensity(next, nextWidth, nextHeight);
    return oldDensity > newDensity * 1.01 ||
        (oldDensity >= newDensity * 0.99 && !ViewportDetailContains(next, previous));
}

inline bool PreferViewportDetail(const ViewportRegion& candidate, int candidateWidth, int candidateHeight,
    const ViewportRegion& retained, int retainedWidth, int retainedHeight) {
    const double candidateDensity = ViewportDetailDensity(candidate, candidateWidth, candidateHeight);
    const double retainedDensity = ViewportDetailDensity(retained, retainedWidth, retainedHeight);
    if (candidateDensity > retainedDensity * 1.01) return true;
    if (candidateDensity < retainedDensity * 0.99) return false;
    const auto area = [](const ViewportRegion& region) {
        return region.Valid() ? double(region.width) * region.height / (double(region.fullWidth) * region.fullHeight) : 1.0;
    };
    return area(candidate) > area(retained);
}
}
