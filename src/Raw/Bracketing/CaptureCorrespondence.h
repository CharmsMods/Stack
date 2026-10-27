#pragma once
#include "CaptureSupport.h"
#include "Raw/MultiFrameDenoise/LocalMotion.h"
#include <cstdint>
#include <functional>
#include <string>

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
bool HasCaptureDepthSupport(const PreparedSource&);
// Unknown/missing depth evidence is not rejection. This categorical veto is
// independent of photometric reliability and never applies to the reference.
bool CaptureDepthRejects(const PreparedSource&,Mfd::RawCoordinate,bool reference=false);
// Crops categorical reliability evidence without re-evaluating motion. Missing
// or stale required evidence rejects; cancellation/resource failures are errors.
bool BuildCaptureSupportMask(const PreparedSource&,Mfd::RawCoordinate first,
    Mfd::RawCoordinate last,bool reference,CaptureSupportMask&,
    const std::function<bool()>& shouldCancel,std::string& error);
// Rejected matches retain their coordinates for display fallback, but cannot
// contribute reconstruction support. The fixed reference needs no match.
// When supplied, evaluatedMotion receives the successful local-motion sample;
// jacobian optionally describes the actual returned coordinate map.
// Callers must still check the returned correspondence confidence before use.
CaptureCorrespondence EvaluateCaptureCorrespondence(const PreparedSource&,
    Mfd::RawCoordinate,bool reference=false,Mfd::LocalMotionFieldSample* evaluatedMotion=nullptr,
    Mfd::RawMappingJacobian* jacobian=nullptr);
}
