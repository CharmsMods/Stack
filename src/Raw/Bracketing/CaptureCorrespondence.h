#pragma once
#include "Raw/MultiFrameDenoise/LocalMotion.h"
#include <cstdint>

namespace Raw::Bracketing {
struct PreparedSource;
inline constexpr double kUncertainCaptureReliability=.20;
struct CaptureCorrespondence {
    Mfd::RawCoordinate sourceRaw;
    Mfd::SymmetricRawCovariance covarianceRaw;
    double confidence=0;
};
bool HardCaptureRejection(std::uint16_t bits);
void StampCaptureReliability(PreparedSource&);
bool HasCurrentCaptureReliability(const PreparedSource&);
// Rejected matches retain their coordinates for display fallback, but cannot
// contribute reconstruction support. The fixed reference needs no match.
// When supplied, evaluatedMotion receives the successful local-motion sample;
// callers must still check the returned correspondence confidence before use.
CaptureCorrespondence EvaluateCaptureCorrespondence(const PreparedSource&,
    Mfd::RawCoordinate,bool reference=false,Mfd::LocalMotionFieldSample* evaluatedMotion=nullptr);
}
