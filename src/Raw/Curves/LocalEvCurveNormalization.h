#pragma once

#include "Raw/RawDevelopmentRecipe.h"

namespace Stack::RawRecipe {

// Uniform mean over the authored EV domain, independent of histogram/view zoom.
// Points always retain the total correction. Normalization chooses how much of
// its mean is represented as an offset; residual + offset is the same mapping.
float LocalEvCurveOffset(const std::vector<RawBezierCurvePoint>& points, float normalization);
float LocalEvGlobalOffsetEv(const RawLocalRangeRecipe& recipe);

} // namespace Stack::RawRecipe
