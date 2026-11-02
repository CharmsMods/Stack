#pragma once
#include "Raw/MultiFrameDenoise/SameCfaSampler.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
// Headroom is a reliability in [0,1]. Use the magnitude of each
// interpolation coefficient so negative cubic lobes cannot create headroom.
inline double FootprintHeadroom(const Mfd::KeysBicubicFootprint& footprint,
    const std::array<Mfd::SameCfaTapInput,Mfd::kSameCfaTapCount>& taps) {
    double sum=0, total=0;
    for(std::size_t i=0;i<taps.size();++i) {
        const double weight=std::abs(footprint.coefficients[i]);
        const double h=std::clamp((.995-taps[i].normalizedSample)/.045,0.,1.);
        sum+=weight*h*h*(3-2*h);
        total+=weight;
    }
    return sum/total;
}

struct CaptureFallback {
    double value=0;
    bool finite=false,clipped=false;
};

// A shorter exposure does not make rejected geometry a better display source.
// Preserve an available fallback whose registration has not been contradicted;
// only compare exposures within the same registration-evidence class.
inline bool PreferCaptureFallback(bool haveCurrent,bool currentRejected,double currentExposure,
    bool candidateRejected,double candidateExposure) {
    return !haveCurrent||(currentRejected&&!candidateRejected)||
        (currentRejected==candidateRejected&&candidateExposure<currentExposure);
}

// A display fallback retains the same registered footprint even when clipping
// disqualifies it as a measurement. Switching only a clipped color to nearest
// sampling changes its edge position relative to the other colors.
inline CaptureFallback InterpolateCaptureFallback(const Mfd::KeysBicubicFootprint& footprint,
    const std::array<Mfd::SameCfaTapInput,Mfd::kSameCfaTapCount>& taps,double scale) {
    CaptureFallback out;
    if(!std::isfinite(scale)||scale<=0)return out;
    for(std::size_t i=0;i<taps.size();++i) {
        const auto weight=footprint.coefficients[i];
        if(std::abs(weight)<=1e-12)continue;
        const auto& tap=taps[i];
        if(!std::isfinite(weight)||!std::isfinite(tap.normalizedSample)||
            !std::isfinite(tap.comparisonGain)||tap.comparisonGain<=0||
            Mfd::HasSampleFlag(tap.sampleFlags,Mfd::PreparedSampleFlag::Defective)||
            Mfd::HasSampleFlag(tap.sampleFlags,Mfd::PreparedSampleFlag::DecoderRepaired))return {};
        out.value+=weight*tap.normalizedSample*tap.comparisonGain*scale;
        out.clipped|=Mfd::HasSampleFlag(tap.sampleFlags,Mfd::PreparedSampleFlag::Saturated)||
            Mfd::HasSampleFlag(tap.sampleFlags,Mfd::PreparedSampleFlag::ExplicitDecoderClip);
    }
    out.finite=std::isfinite(out.value);
    return out;
}
}
