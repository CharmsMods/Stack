#pragma once

#include <vector>

namespace Stack::RawRecipe {

struct RawDenoiseBand {
    int renderGap = 1;
    float sourceSupportPixels = 4.0f;
    float normalizedFrequency = 1.0f;
    float normalizedBandwidth = 0.1f;
    float whiteNoiseSigma = 1.0f;
};

std::vector<RawDenoiseBand> BuildRawDenoiseBandSchedule(
    int renderWidth,
    int renderHeight,
    int sourceWidth,
    int sourceHeight,
    float maximumStructureSize);

} // namespace Stack::RawRecipe
