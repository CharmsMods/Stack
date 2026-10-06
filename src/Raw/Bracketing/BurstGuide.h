#pragma once
#include "ProcessingInternal.h"
#include "PreparedTileSampler.h"

namespace Raw::Bracketing {
struct BurstGuidePoint {
    std::array<double,4> mean{},variance{};
    std::array<Mfd::RawSignalGradient,4> gradient{};
    bool repeated=false;
};
// A bounded analysis patch, never an image published to RAW. Disjoint capture
// subsets must agree on structure before it can request motion refinement.
bool MeasureBurstGuide(const ProcessingRequest&,const PreparedDataset&,
    PreparedTileSampler&,unsigned,unsigned,BurstGuidePoint&,std::string&);
bool RepeatedBurstStructure(const std::array<double,25>&,
    const std::array<double,25>&,const std::array<double,25>&,
    const std::array<double,25>&,Mfd::RawSignalGradient&,double&);
}
