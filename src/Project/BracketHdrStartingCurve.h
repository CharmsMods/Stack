#pragma once

namespace Raw::Bracketing { struct BracketingResult; struct BracketingRecipe; }
namespace Stack::Project {
struct RawLayerStackState;
inline constexpr char kBracketToneFitConsidered[] = "hdrStartingCurveConsidered";
// Seed editable Local EV and scene-luminance Tone Curve on first acceptance.
bool FitBracketStartingAdjustments(RawLayerStackState&, const Raw::Bracketing::BracketingResult&,
    const Raw::Bracketing::BracketingRecipe&);
}
