#pragma once

namespace Raw::Bracketing { struct BracketingResult; struct BracketingRecipe; }
namespace Stack::Project {
struct RawLayerStackState;
inline constexpr char kBracketToneFitConsidered[] = "hdrStartingCurveConsidered";
// Called on first accepted processing only. The resulting points belong to the
// ordinary editable scene-luminance Tone Curve, with optional local contrast,
// in the project. Existing curves are never replaced by this initialization.
bool FitBracketStartingCurve(RawLayerStackState&, const Raw::Bracketing::BracketingResult&,
    const Raw::Bracketing::BracketingRecipe&);
}
