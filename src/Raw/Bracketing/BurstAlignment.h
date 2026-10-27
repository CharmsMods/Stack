#pragma once
#include "ProcessingInternal.h"

namespace Raw::Bracketing {
bool BuildCapturePyramid(const PreparedSource&,const std::filesystem::path&,
    const Mfd::Parameters&,const std::function<bool()>&,Mfd::CfaPlanePyramid&,std::string&);
// Conservative host-memory admission, not measured RSS or a GPU reservation.
// No alternate means no pair requirement (requiredBytes=0). Invalid geometry,
// overflow and insufficient budget fail without changing correspondence quality.
bool AdmitLocalAlignmentMemory(const ProcessingRequest&,const PreparedDataset&,
    std::uint64_t& requiredBytes,std::string&);
// Admit the normal full-quality refinement footprint before expensive motion
// search, including the local pair even when fewer than three captures exist.
// The actual motion field is checked again when refinement starts.
bool PreflightBurstRefinement(const ProcessingRequest&,const PreparedDataset&,
    std::vector<std::string>&,std::string&);
bool RefineBurstAlignment(const ProcessingRequest&,PreparedDataset&,
    std::vector<std::string>&,std::string&);
}
