#pragma once
#include "Raw/ReconstructionDepth.h"
#include "../Processor.h"

namespace Raw::Bracketing::Depth {
bool Estimate(const ProcessingRequest&,PreparedDataset&,std::vector<std::string>&,std::string&);
std::vector<DepthMask> ExtractMasks(const ReconstructionDepth&,const RawImageData&,
    const std::function<bool()>& shouldCancel);
bool Finish(const ProcessingRequest&,BracketingResult&,std::string&);
Preview ReadResultDetail(const ProcessingRequest&,const BracketingResult&,unsigned x,unsigned y,unsigned size);
}
