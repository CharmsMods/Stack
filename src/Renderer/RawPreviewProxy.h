#pragma once

#include "Raw/RawImageData.h"

#include <cstddef>
#include <string>
#include <utility>

namespace Stack::Renderer::RawPreviewProxy {

struct Summary {
    bool usedProxy = false;
    int rawWidth = 0;
    int rawHeight = 0;
    int visibleWidth = 0;
    int visibleHeight = 0;
    std::size_t rawSampleCount = 0;
    std::size_t linearUInt16SampleCount = 0;
    std::size_t linearFloatSampleCount = 0;
    std::size_t normalizedMosaicSampleCount = 0;
    int dngGainMapCount = 0;
};

bool HasPixels(const Raw::RawImageData& rawData);
// Match RAW proxy sizing without decoding or rendering the image pixels.
std::pair<int,int> FitPreviewExtent(int width, int height, int previewMaxDimension);
bool BuildPreviewRawData(
    const Raw::RawImageData& source,
    int previewMaxDimension,
    Raw::RawImageData& preview);
std::string BuildCacheKey(
    const std::string& sourceCacheKey,
    const Raw::RawImageData& rawData,
    int previewMaxDimension);
Summary Summarize(const Raw::RawImageData& rawData, bool usedProxy);

} // namespace Stack::Renderer::RawPreviewProxy
