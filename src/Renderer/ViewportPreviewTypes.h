#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>

namespace Stack::Renderer {

// Bounds use top-left pixel corners on a complete image sample grid. A
// resident crop keeps that grid's dimensions, even though its texture is small.
struct ImageRegion {
    int fullWidth = 0, fullHeight = 0;
    int x = 0, y = 0, width = 0, height = 0;

    bool Valid() const {
        return fullWidth > 0 && fullHeight > 0 && width > 0 && height > 0 &&
            x >= 0 && y >= 0 && width <= fullWidth - x && height <= fullHeight - y;
    }
    bool Partial() const {
        return Valid() && (x || y || width != fullWidth || height != fullHeight);
    }
    bool operator==(const ImageRegion& b) const {
        return fullWidth == b.fullWidth && fullHeight == b.fullHeight &&
            x == b.x && y == b.y && width == b.width && height == b.height;
    }
    bool operator!=(const ImageRegion& b) const { return !(*this == b); }
    std::size_t Fingerprint() const {
        // Preserve the former RAW region cache identity without importing its
        // recipe, stage-cache, or JSON dependencies into image geometry.
        std::size_t key = 1;
        for (int v : {fullWidth, fullHeight, x, y, width, height}) {
            key ^= std::hash<int>{}(v) + static_cast<std::size_t>(0x9e3779b97f4a7c15ull) +
                (key << 6u) + (key >> 2u);
        }
        return key;
    }
};

inline ImageRegion WholeImageRegion(int width, int height) {
    return width > 0 && height > 0 ? ImageRegion{width, height, 0, 0, width, height} : ImageRegion{};
}

inline ImageRegion ScaleImageRegion(const ImageRegion& region, int width, int height) {
    if (!region.Valid() || width <= 0 || height <= 0) return {};
    const auto lower = [](int origin, int extent, int full) {
        return static_cast<int>(static_cast<std::int64_t>(origin) * extent / full);
    };
    const auto upper = [](int end, int extent, int full) {
        return static_cast<int>((static_cast<std::int64_t>(end) * extent + full - 1) / full);
    };
    const int left = std::clamp(lower(region.x, width, region.fullWidth), 0, width - 1);
    const int top = std::clamp(lower(region.y, height, region.fullHeight), 0, height - 1);
    const int right = std::clamp(upper(region.x + region.width, width, region.fullWidth), left + 1, width);
    const int bottom = std::clamp(upper(region.y + region.height, height, region.fullHeight), top + 1, height);
    return {width, height, left, top, right - left, bottom - top};
}

inline bool ContainsImageRegion(const ImageRegion& outer, const ImageRegion& inner) {
    if (!outer.Valid() || !inner.Valid()) return false;
    // Exact rational comparisons distinguish missing coverage from rounding
    // noise in normalized floating-point coordinates.
    const auto contains = [](int a, int lengthA, int fullA, int b, int lengthB, int fullB) {
        return static_cast<std::int64_t>(a) * fullB <= static_cast<std::int64_t>(b) * fullA &&
            static_cast<std::int64_t>(a + lengthA) * fullB >=
                static_cast<std::int64_t>(b + lengthB) * fullA;
    };
    return contains(outer.x, outer.width, outer.fullWidth, inner.x, inner.width, inner.fullWidth) &&
        contains(outer.y, outer.height, outer.fullHeight, inner.y, inner.height, inner.fullHeight);
}

// UV bounds use bottom-left texture coordinates. Invalid coverage stays
// unavailable; a missing region is never implicitly treated as a full image.
struct ImageRegionUv {
    double x = 0, y = 0, width = 0, height = 0;
    bool valid = false;
};

inline ImageRegionUv MapImageRegionUv(const ImageRegion& resident, const ImageRegion& target) {
    if (!ContainsImageRegion(resident, target)) return {};
    const double sourceX = static_cast<double>(resident.x) / resident.fullWidth;
    const double sourceY = static_cast<double>(resident.y) / resident.fullHeight;
    const double sourceWidth = static_cast<double>(resident.width) / resident.fullWidth;
    const double sourceHeight = static_cast<double>(resident.height) / resident.fullHeight;
    const double targetX = static_cast<double>(target.x) / target.fullWidth;
    const double targetY = static_cast<double>(target.y) / target.fullHeight;
    const double targetWidth = static_cast<double>(target.width) / target.fullWidth;
    const double targetHeight = static_cast<double>(target.height) / target.fullHeight;
    const double left = std::clamp((targetX - sourceX) / sourceWidth, 0.0, 1.0);
    const double bottom = std::clamp(1.0 - (targetY + targetHeight - sourceY) / sourceHeight, 0.0, 1.0);
    return {left, bottom, std::min(targetWidth / sourceWidth, 1.0 - left),
        std::min(targetHeight / sourceHeight, 1.0 - bottom), true};
}

struct ViewportPreviewRequest {
    ImageRegion visible;
    double samplingScale = 1.0;
    std::uint64_t generation = 0;
    bool enabled = false;
    // Enabled with empty=true requests no detail. Enabled with an invalid
    // visible rectangle and empty=false explicitly retains full-image demand.
    bool empty = false;
};

// Logical dimensions describe the final zero-origin output canvas. Semantic
// full/data windows and source transforms remain with the image descriptor.
// The resident rectangle describes the selected complete sample grid; each
// stored texel stays on that grid rather than being fitted to the crop again.
struct ViewportPreviewGeometry {
    int logicalWidth = 0, logicalHeight = 0;
    ImageRegion resident;
    int storageWidth = 0, storageHeight = 0;

    bool Valid() const {
        return logicalWidth > 0 && logicalHeight > 0 && resident.Valid() &&
            storageWidth == resident.width && storageHeight == resident.height;
    }
    double SampleSpacingX() const {
        return Valid() ? static_cast<double>(logicalWidth) / resident.fullWidth : 0.0;
    }
    double SampleSpacingY() const {
        return Valid() ? static_cast<double>(logicalHeight) / resident.fullHeight : 0.0;
    }
    double SampleOriginX() const { return resident.x * SampleSpacingX(); }
    double SampleOriginY() const { return resident.y * SampleSpacingY(); }
    bool NativeSamples() const {
        return Valid() && resident.fullWidth == logicalWidth && resident.fullHeight == logicalHeight;
    }
};

} // namespace Stack::Renderer
