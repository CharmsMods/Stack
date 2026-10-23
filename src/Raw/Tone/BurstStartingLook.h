#pragma once
#include <algorithm>
#include <cmath>

namespace Stack::RawRecipe {
// A conservative starting look, not a reconstruction/calibration operation.
// Inputs are complete-image scene-linear luminance percentiles BEFORE the
// default View Transform and BEFORE any creative node edits. No pixels or
// display-encoded evidence enter this planner. The caller owns eligibility,
// identities, one-time application and history.
struct BurstStartingLook {
    bool valid = false;
    float exposureEv = 0;
    float shoulder = 0;
    float OutputEv(float inputEv) const {
        if (inputEv <= 1 || shoulder <= 0) return inputEv;
        return 1 + std::log1p(shoulder * (inputEv - 1)) / shoulder;
    }
};
inline BurstStartingLook PlanBurstStartingLook(float medianLuma, float highlightLuma) {
    BurstStartingLook plan;
    if (!std::isfinite(medianLuma) || !std::isfinite(highlightLuma) ||
        medianLuma <= 1e-6f || highlightLuma < medianLuma) return plan;
    // Do not force every night/high-key scene to middle grey. Half of a
    // bounded median correction leaves the capture's exposure character.
    plan.exposureEv = .5f * std::clamp((std::log2(.18f) - std::log2(medianLuma)), -1.f, 1.f);
    const float highEv = (std::log2(highlightLuma) - std::log2(.18f)) + plan.exposureEv;
    // The existing standard View Transform reaches white at +4 EV. Leave
    // half a stop of margin at P99.9; small speculars may still reach white.
    // Compress only the upper range; keep midtone slopes and shadows intact.
    if (highEv > 3.5f) {
        float low = 0, high = 64;
        for (int i = 0; i < 32; ++i) {
            const float k = (low + high) * .5f;
            const float mapped = 1 + std::log1p(k * (highEv - 1)) / k;
            if (mapped > 3.5f) low = k; else high = k;
        }
        plan.shoulder = high;
    }
    plan.valid = true;
    return plan;
}
} // namespace Stack::RawRecipe
