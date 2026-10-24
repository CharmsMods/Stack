#pragma once

#include "Utils/PixelBufferUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Stack::EditorRendering {

struct CompositeRasterSize {
    int width = 0;
    int height = 0;
    std::size_t bytes = 0;
};

inline bool TryRoundCompositeDimension(double value, int& result) {
    result = 0;
    if (!std::isfinite(value)) return false;
    const double rounded = std::round(std::max(1.0, value));
    if (rounded > static_cast<double>(std::numeric_limits<int>::max())) return false;
    result = static_cast<int>(rounded);
    return true;
}

inline bool TryResolveCompositePairedDimension(
    int knownDimension, double aspectRatio, bool widthIsKnown, int& result) {
    result = 0;
    if (!std::isfinite(aspectRatio) || aspectRatio <= 0.0 ||
        aspectRatio > std::numeric_limits<float>::max()) return false;
    // Preserve the editor's existing float arithmetic/rounding for ordinary
    // settings; validate the rounded result before any integer conversion.
    const float ratio = std::max(0.0001f, static_cast<float>(aspectRatio));
    const float known = static_cast<float>(std::max(1, knownDimension));
    if (!std::isfinite(ratio)) return false;
    return TryRoundCompositeDimension(widthIsKnown ? known / ratio : known * ratio, result);
}

inline bool TryPlanCompositeRaster(
    int requestedWidth, double boundsWidth, double boundsHeight, CompositeRasterSize& result) {
    result = {};
    if (!std::isfinite(boundsWidth) || !std::isfinite(boundsHeight) ||
        boundsWidth <= 0.0 || boundsHeight <= 0.0) return false;
    CompositeRasterSize candidate;
    candidate.width = std::max(1, requestedWidth);
    const double preciseRatio = std::max(1.0, boundsWidth) / std::max(1.0, boundsHeight);
    if (preciseRatio > std::numeric_limits<float>::max()) return false;
    const float ratio = static_cast<float>(preciseRatio);
    if (!TryResolveCompositePairedDimension(candidate.width, ratio, true, candidate.height) ||
        !PixelBuffer::TryComputePixelByteCount(candidate.width, candidate.height, 4, candidate.bytes) ||
        candidate.bytes > std::vector<unsigned char>().max_size()) return false;
    result = candidate;
    return true;
}

inline bool TryAllocateCompositeRaster(
    const CompositeRasterSize& size, std::vector<unsigned char>& pixels, int& width, int& height) {
    pixels.clear();
    width = height = 0;
    std::size_t required = 0;
    if (!PixelBuffer::TryComputePixelByteCount(size.width, size.height, 4, required) ||
        required != size.bytes || required > pixels.max_size()) return false;
    try {
        pixels.assign(required, 0);
    } catch (const std::bad_alloc&) {
        pixels.clear();
        return false;
    } catch (const std::length_error&) {
        pixels.clear();
        return false;
    }
    width = size.width;
    height = size.height;
    return true;
}

// Coordinates belong to a previously validated complete RGBA raster.
inline std::size_t CompositeRgbaOffset(int width, int x, int y) {
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
        static_cast<std::size_t>(x)) * 4u;
}

inline bool TryCompositeRasterSpan(
    double minimum, double maximum, double origin, double extent, int pixels,
    int& first, int& last) {
    first = 0;
    last = -1;
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || !std::isfinite(origin) ||
        !std::isfinite(extent) || maximum < minimum || extent <= 0.0 || pixels <= 0) return false;
    const double scale = static_cast<double>(pixels) / std::max(0.0001, extent);
    const double lower = std::floor((minimum - origin) * scale);
    const double upper = std::ceil((maximum - origin) * scale);
    if (!std::isfinite(lower) || !std::isfinite(upper)) return false;
    // Clamp before conversion, including wholly off-canvas items.
    first = static_cast<int>(std::clamp(lower, 0.0, static_cast<double>(pixels)));
    last = static_cast<int>(std::clamp(upper, -1.0, static_cast<double>(pixels - 1)));
    return first <= last;
}

} // namespace Stack::EditorRendering
