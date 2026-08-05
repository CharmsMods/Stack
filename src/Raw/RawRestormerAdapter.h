#pragma once

#include "Raw/RawDevelopmentRecipe.h"

#include <string>
#include <vector>

namespace Stack::RawRestormer {

struct AdapterResult {
    bool ok = false;
    std::string error;
    float inputExposureGain = 1.0f;
};

// Builds the display-referred RGB proxy expected by the frozen Restormer
// checkpoints. The source remains untouched and is expected to contain
// interleaved scene-linear RGBA float pixels.
AdapterResult BuildInputProxy(
    const std::vector<float>& sourceRgba,
    int width,
    int height,
    Raw::RawWorkingSpace workingSpace,
    std::vector<float>& outSrgbProxy);

// Applies a neutral Restormer output back to the original scene-linear image.
// The model output is interleaved sRGB-encoded RGB in the same proxy domain
// returned by BuildInputProxy. Alpha and the original scene-linear headroom are
// preserved.
AdapterResult ApplyOutput(
    const std::vector<float>& sourceRgba,
    const std::vector<float>& inputSrgbProxy,
    const std::vector<float>& modelOutputSrgbProxy,
    int width,
    int height,
    Raw::RawWorkingSpace workingSpace,
    const RawRecipe::RawRgbDenoiseRecipe& settings,
    std::vector<float>& outRgba);

} // namespace Stack::RawRestormer
