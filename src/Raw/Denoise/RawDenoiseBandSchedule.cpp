#include "Raw/Denoise/RawDenoiseBandSchedule.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Stack::RawRecipe {

std::vector<RawDenoiseBand> BuildRawDenoiseBandSchedule(
    int renderWidth,
    int renderHeight,
    int sourceWidth,
    int sourceHeight,
    float maximumStructureSize) {
    const int renderLongEdge = std::max(1, std::max(renderWidth, renderHeight));
    const int sourceLongEdge = std::max(
        renderLongEdge,
        std::max(sourceWidth, sourceHeight));
    const float sourcePixelsPerRenderPixel =
        static_cast<float>(sourceLongEdge) /
        static_cast<float>(renderLongEdge);
    const float maximumSupport = std::clamp(
        std::isfinite(maximumStructureSize) ? maximumStructureSize : 128.0f,
        8.0f,
        2048.0f);
    const float frequencyDenominator = std::max(
        1.0f,
        std::log2(maximumSupport / 4.0f));

    // The first eight octave gaps keep the largest separable pass practical
    // on OpenGL 3.3 hardware. The schedule stops when another band would
    // exceed the authored source-pixel support.
    constexpr std::array<float, 8> kUnitNoiseSigma = {
        0.890796310f, 0.200663851f, 0.085507505f, 0.041217444f,
        0.020424967f, 0.010189759f, 0.005092047f, 0.002545669f
    };
    std::vector<RawDenoiseBand> bands;
    bands.reserve(kUnitNoiseSigma.size());
    for (int octave = 0; octave < static_cast<int>(kUnitNoiseSigma.size()); ++octave) {
        const int gap = 1 << octave;
        const float support = 4.0f * static_cast<float>(gap) *
            sourcePixelsPerRenderPixel;
        if (octave >= 3 && support > maximumSupport * 1.01f) {
            break;
        }
        RawDenoiseBand band;
        band.renderGap = gap;
        band.sourceSupportPixels = support;
        band.normalizedFrequency = std::clamp(
            1.0f - std::log2(std::max(4.0f, support) / 4.0f) /
                frequencyDenominator,
            0.0f,
            1.0f);
        band.whiteNoiseSigma = kUnitNoiseSigma[static_cast<std::size_t>(octave)];
        bands.push_back(band);
    }
    if (bands.empty()) {
        bands.push_back({});
    }
    const float bandwidth = bands.size() > 1
        ? 0.70f / static_cast<float>(bands.size() - 1u)
        : 0.35f;
    for (RawDenoiseBand& band : bands) {
        band.normalizedBandwidth = bandwidth;
    }
    return bands;
}

} // namespace Stack::RawRecipe
