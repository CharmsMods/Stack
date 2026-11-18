#pragma once

#include "Raw/MultiFrameDenoise/Contracts.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Raw::Mfd {

// Registration currently admits four levels including level zero. The bounded
// implementation accounts for the longest composed filter at level three.
inline constexpr std::uint32_t kMaximumPyramidVarianceLevel = 3u;
inline constexpr std::uint64_t kPyramidVarianceScratchBytes = 32u * 1024u;

// Propagate independent level-zero sample variances through the *complete*
// repeated five-tap, factor-two signal filter. Intermediate pyramid pixels share
// native samples, so recursively filtering their marginal variances is wrong.
// This computes exact output marginals, not independence between output pixels.
//
// levelVariance must already have the target extent's size; neither input nor
// output storage is allocated or copied. Reflection follows every intermediate
// level's actual extent, including odd dimensions. Repeated references to one
// native sample are combined before its coefficient is squared. This arithmetic
// helper does not replace sample-validity propagation: the owner must reject any
// footprint containing invalid native input and retain those inherited masks.
// Arithmetic on invalid variance payloads carries no usable noise guarantee;
// the owner also checks corrected output variance for finiteness/nonnegativity.
bool PropagatePyramidLevelVariance(
    const std::vector<double>& baseVariance,
    PixelExtent baseExtent,
    std::uint32_t level,
    const std::array<double, 5>& kernel,
    std::vector<double>& levelVariance,
    std::string* error = nullptr);

} // namespace Raw::Mfd
