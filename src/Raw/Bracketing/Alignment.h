#pragma once

#include "ProcessingInternal.h"

namespace Raw::Bracketing {

// Samples an original, unnormalized alignment proxy at a coordinate in the
// fixed output/reference grid. The source's saved transform is applied first.
bool SampleAlignmentProxy(
    const PreparedSource& source,
    Mfd::RawCoordinate referenceRaw,
    double& value);

} // namespace Raw::Bracketing
