#pragma once

#include "Raw/MultiFrameDenoise/SameCfaSampler.h"

namespace Raw::Bracketing {

// A conditional alternative to the strict Keys stencil, used only when sensor
// clipping removes part of its support. All original footprint taps must have
// been read. The positive central-bilinear support must remain measured; this
// operation never fills a clipped center or authorizes rejected geometry.
// Failure means unsupported reconstruction, not a prepared-tile read error.
bool ReconstructPartialCfaFootprint(
    const Mfd::KeysBicubicFootprint& footprint,
    const std::array<Mfd::SameCfaTapInput,Mfd::kSameCfaTapCount>& taps,
    const Mfd::SiteNoiseProfile& noiseProfile,
    double usableCodeSpanDn,
    const Mfd::SameCfaScalarParameters& parameters,
    Mfd::SameCfaSampleResult& result,
    double& headroom);

} // namespace Raw::Bracketing
