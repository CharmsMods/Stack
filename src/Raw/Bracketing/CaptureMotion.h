#pragma once
#include "Raw/MultiFrameDenoise/LocalMotion.h"
#include <memory>

namespace Raw::Bracketing {
struct PreparedSource;
struct MotionCorrection {
    Mfd::RawCoordinate delta;
    Mfd::SymmetricRawCovariance covariance;
    float confidence=0;
};
// Corrections augment the original field. Empty cells contribute exactly zero;
// an unsupported original warp can never be resurrected by interpolation.
struct MotionRefinementField {
    unsigned width=0,height=0;
    double spacing=0;
    std::vector<MotionCorrection> cells;
};
bool EvaluateCaptureMotion(const PreparedSource&,Mfd::RawCoordinate,
    const Mfd::LocalMotionOptions&,Mfd::LocalMotionFieldSample&,double* refinementConfidence=nullptr);
}
