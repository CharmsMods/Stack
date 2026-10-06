#pragma once
#include "Raw/MultiFrameDenoise/SameCfaSampler.h"
#include <cmath>

namespace Raw::Bracketing {
struct CaptureFallback {
    double value=0;
    bool finite=false,clipped=false;
};

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
