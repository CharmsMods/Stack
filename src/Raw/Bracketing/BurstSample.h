#pragma once
#include "ProcessingInternal.h"
#include "PreparedTileSampler.h"

namespace Raw::Bracketing {
// Shared original-capture reader for burst guides and local spectral evidence.
// The reference and identity captures are not interpolated.
Observation ReadBurstSample(const PreparedDataset&,const PreparedSource&,
    PreparedTileSampler&,unsigned,unsigned,double,std::string&,
    const Mfd::RawSignalGradient* sceneGradient=nullptr);
}
