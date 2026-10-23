#pragma once
#include "Raw/MultiFrameDenoise/Reliability.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
inline double ClippedReferenceContradiction(double referenceLowerBound,double alternateValue,double variance) {
    // Values share the reference exposure domain. A shorter exposure that
    // actually recovers the highlight must meet this bound after scaling.
    return variance>0?std::max(0.,referenceLowerBound-alternateValue)/std::sqrt(variance):0;
}
inline double NoiseCorrectedMismatch(double sum,double sumSquared,unsigned count) {
    if(count<4) return 0;
    const double mean=sum/count;
    const double structure=std::sqrt(std::max(0.,sumSquared/count-1.));
    // The neighborhood contains interpolated measurements. Allow correlation
    // between neighboring taps instead of claiming count independent samples.
    const double coherent=std::abs(mean)*std::sqrt(count/2.);
    return std::max(structure,coherent);
}
using CaptureMotionEvaluator=std::function<bool(Mfd::RawCoordinate,Mfd::LocalMotionFieldSample&)>;
inline double GreenDifferenceResidual(double residual0,double variance0,double residual1,double variance1) {
    // Compare the *change* in the two green measurements across captures.
    // Do not force spatially separated greens to have the same scene value.
    const double variance=variance0+variance1;
    return variance>0?(residual0-residual1)/std::sqrt(variance):0;
}
bool BuildColorReliability(const Mfd::ReliabilityBuildRequest& request,
    Mfd::ReliabilityMap& result,std::string& error,const CaptureMotionEvaluator& evaluateMotion={});
}
