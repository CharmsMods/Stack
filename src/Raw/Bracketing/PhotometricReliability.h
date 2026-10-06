#pragma once
#include "Raw/MultiFrameDenoise/Reliability.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
inline double NoiseCorrectedMismatch(double sum,double sumSquared,unsigned count) {
    if(count<4) return 0;
    const double mean=sum/count;
    const double structure=std::sqrt(std::max(0.,sumSquared/count-1.));
    // The neighborhood contains interpolated measurements. Allow correlation
    // between neighboring taps instead of claiming count independent samples.
    const double coherent=std::abs(mean)*std::sqrt(count/2.);
    return std::max(structure,coherent);
}
bool BuildColorReliability(const Mfd::ReliabilityBuildRequest& request,
    Mfd::ReliabilityMap& result,std::string& error);
}
