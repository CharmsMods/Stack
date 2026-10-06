#pragma once
#include "Raw/Bracketing/Processor.h"

namespace Raw::Bracketing {
// Replaces the completed native mosaic with RGB reconstructed from originals.
// The native result supplies only fallbacks and the independent luminance guide.
bool ReconstructSuperResolution(const ProcessingRequest&,BracketingResult&,std::string&);
Preview SuperResolutionDetail(const BracketingResult&,unsigned x,unsigned y,unsigned size,
    const std::function<bool()>& cancel={},std::uint64_t memoryBudgetBytes=1024ull*1024*1024);
}
