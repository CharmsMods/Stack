#pragma once

#include "ProcessingInternal.h"

namespace Raw::Bracketing {

// Samples an original, unnormalized alignment proxy at a coordinate in the
// fixed output/reference grid. The source's saved transform is applied first.
bool SampleAlignmentProxy(
    const PreparedSource& source,
    Mfd::RawCoordinate referenceRaw,
    double& value);
// Calibration keeps channel agreement observable instead of reducing the
// measurements to luminance before estimating an exposure ratio.
bool SampleAlignmentProxyColor(const PreparedSource&,Mfd::RawCoordinate,
    double& luminance,std::array<double,3>& color);

} // namespace Raw::Bracketing
