#pragma once
#include "ProcessingInternal.h"

namespace Raw::Bracketing {
bool BuildCapturePyramid(const PreparedSource&,const std::filesystem::path&,
    const Mfd::Parameters&,const std::function<bool()>&,Mfd::CfaPlanePyramid&,std::string&);
bool RefineBurstAlignment(const ProcessingRequest&,PreparedDataset&,
    std::vector<std::string>&,std::string&);
}
