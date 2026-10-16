#pragma once

#include "Raw/RawImageData.h"
#include <functional>

namespace Stack::RawWorkspace {
inline constexpr int kFastNeutralThumbnailMaxDimension = 320;
inline constexpr int kNeutralThumbnailMaxDimension = 512;

// Shared neutral pixel preparation for Gallery thumbnails and scene matching.
// The standard path averages the same sensor regions before white balance and
// encoding. No file, catalog or render context is needed.
bool BuildRawGalleryThumbnailPixels(const Raw::RawImageData& raw, int maxDimension,
    std::vector<unsigned char>& pixels, int& width, int& height,
    bool fastPreview = false, const std::function<bool()>& shouldCancel = {});
} // namespace Stack::RawWorkspace
