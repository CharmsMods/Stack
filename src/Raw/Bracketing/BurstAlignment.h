#pragma once
#include "ProcessingInternal.h"

namespace Raw::Bracketing {
bool BuildCapturePyramid(const PreparedSource&,const std::filesystem::path&,
    const Mfd::Parameters&,const std::function<bool()>&,Mfd::CfaPlanePyramid&,std::string&);
// Admit the normal full-quality refinement footprint before expensive motion
// search. The actual motion field is checked again when refinement starts.
bool PreflightBurstRefinement(const ProcessingRequest&,const PreparedDataset&,
    std::vector<std::string>&,std::string&);
bool RefineBurstAlignment(const ProcessingRequest&,PreparedDataset&,
    std::vector<std::string>&,std::string&);
}
