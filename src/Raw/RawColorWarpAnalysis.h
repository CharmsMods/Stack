#pragma once

#include "Raw/RawDevelopmentRecipe.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace Stack::RawRecipe {

struct RawColorWarpAnalysisSample {
    float sourceU = 0.0f;
    float sourceV = 0.0f;
    RawColorWarpCoordinate color;
    bool valid = true;
};

struct RawColorWarpAreaProposal {
    float sourceA = 0.0f;
    float sourceB = 0.0f;
    RawColorWarpEvCurve evCurve;
    std::size_t supportingPixelCount = 0;
};

struct RawColorWarpAreaAnalysisResult {
    std::vector<RawColorWarpAreaProposal> proposals;
    std::size_t sampledPixelCount = 0;
    std::size_t rejectedPixelCount = 0;
    bool capacityLimited = false;
    bool cancelled = false;
};

// Pure, bounded analysis used by the render worker and by deterministic tests.
// The caller owns task scheduling and generation rejection.
RawColorWarpAreaAnalysisResult AnalyzeRawColorWarpArea(
    const std::vector<RawColorWarpAnalysisSample>& samples,
    const RawColorWarpSampleCircle& circle,
    std::size_t availablePinSlots,
    const std::function<bool()>& isCancelled = {});

} // namespace Stack::RawRecipe
