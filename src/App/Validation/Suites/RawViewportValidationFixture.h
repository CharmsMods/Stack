#pragma once
#include "Raw/RawImageData.h"
#include <memory>
namespace Stack::Validation {
inline std::shared_ptr<Raw::RawImageData> MakeViewportValidationRaw(int width = 512, int height = 384) {
    auto raw = std::make_shared<Raw::RawImageData>();
    auto& metadata = raw->metadata;
    metadata.sourcePath = "viewport-region-validation";
    metadata.rawWidth = metadata.visibleWidth = width;
    metadata.rawHeight = metadata.visibleHeight = height;
    metadata.bitDepth = 14;
    metadata.cfaPattern = Raw::CfaPattern::RGGB;
    metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    metadata.mosaiced = true;
    metadata.isDng = true;
    metadata.blackLevel = metadata.rawMinimum = 512;
    metadata.whiteLevel = metadata.rawMaximum = 16383;
    metadata.cameraWhiteBalance = metadata.daylightWhiteBalance = {1, 1, 1, 1};
    raw->rawBuffer.resize(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        raw->rawBuffer[static_cast<std::size_t>(y) * width + x] = static_cast<std::uint16_t>(
            900 + 9 * (x * 512 / width) + 11 * (y * 384 / height) + ((x * 17 + y * 37) % 97));
    return raw;
}
}
