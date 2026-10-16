#pragma once
#include "ProcessingInternal.h"

namespace Raw::Bracketing {
Mfd::Parameters CaptureAlignmentParameters(const PreparedDataset&);
// Build against source's final motion and calibration, then replace evidence
// and diagnostics together. The caller may stage a source before publishing it.
bool RefreshCaptureReliability(const ProcessingRequest&,const PreparedSource& reference,
    PreparedSource& source,const Mfd::Parameters&,const Mfd::CfaPlanePyramid& referencePyramid,
    const Mfd::CfaPlanePyramid& sourcePyramid,const std::function<void(double)>&,std::string&);
}
