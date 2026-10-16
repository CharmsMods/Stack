#pragma once
#include "Processor.h"

namespace Raw::Bracketing {
// Returns the full-resolution, active-area camera CFA with the origin's
// exact prepared comparison gain applied once. No WB, demosaic, clipping,
// exposure or color transform is applied. Preserves signed/HDR measurements.
// Must run in the engine with a live prepared analysis. Throws on failure.
std::vector<float> BuildOutputMosaic(const BracketingResult&,
    const std::function<bool()>& shouldCancel,
    const std::function<void(double)>& reportProgress = {});
}
