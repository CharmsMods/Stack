#pragma once
#include "CaptureReliability.h"

namespace Raw::Bracketing {
// Bracket-specific annotation on an AlignmentInvalid cell. Local geometry
// remains rejected for registration, depth and super-resolution.
inline constexpr std::uint16_t kNativeGlobalVerified=1u<<6;
inline constexpr double kGlobalCheckRadiusRaw=8;
bool VerifyNativeGlobalSupport(const ProcessingRequest&,const PreparedSource&,
    const Mfd::CfaPlanePyramid&,const Mfd::CfaPlanePyramid&,Mfd::ReliabilityMap&);
}
