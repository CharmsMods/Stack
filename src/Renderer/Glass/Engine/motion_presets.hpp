#pragma once
#include "motion.hpp"

namespace stack::glass::motion::presets {

// Editable, authored starting points. No frequency, damping, or size here is a
// recovered Apple constant. A UI control opts in to one family explicitly.
inline MotionTuning clearControl() { return {}; }
inline MotionTuning clearControlMaterialize() {
    MotionTuning t;
    t.idleMaterialActivation = 0;
    t.hoverMaterialActivation = 0.15f;
    t.pressMaterialActivation = 1;
    return t;
}
inline MotionTuning precision() {
    MotionTuning t;
    t.hoverGrowth = 0.009f;
    t.pressGrowth = 0.045f;
    t.maximumLogStrain = 0.045f;
    t.complianceUnits = 0.35f;
    t.mouseIntensity = 0.72f;
    t.deformationSpring = {10.0, 0.96};
    t.releaseSpring = {8.5, 0.90};
    return t;
}
inline MotionTuning graphPoint() {
    MotionTuning t;
    t.pressGrowth = 0.22f;
    // Authored for visible diagonal deformation at ordinary editor drag speeds;
    // this deliberately expressive graph-point family is not an Apple fit.
    t.maximumLogStrain = 0.22f;
    t.strainSpeed = 360;
    t.complianceUnits = 0.65f;
    t.hoverGrowth = 0.025f;
    return t;
}
inline MotionTuning button() {
    MotionTuning t;
    t.pressGrowth = -0.025f;
    t.hoverGrowth = 0.012f;
    t.maximumLogStrain = 0.015f;
    t.complianceUnits = 0.30f;
    t.maximumTilt = 0.035f;
    t.pressElevation = 0.18f;
    t.pressContactGain = 0.055f;
    t.mouseIntensity = 0.8f;
    t.touchIntensity = 1.0f;
    return t;
}
inline MotionTuning selection() {
    MotionTuning t;
    t.pressGrowth = 0.024f;
    t.maximumLogStrain = 0.030f;
    t.complianceUnits = 0.45f;
    t.deformationSpring = {10.0, 0.96};
    t.pressElevation = 0.25f;
    return t;
}
inline MotionTuning panel() {
    MotionTuning t;
    t.hoverGrowth = t.pressGrowth = t.maximumLogStrain = 0;
    t.complianceUnits = t.maximumTilt = t.pointerTilt = 0;
    t.hoverContactGain = t.pressContactGain = 0;
    t.hoverElevation = t.pressElevation = 0;
    t.pressSpring = t.releaseSpring = {10.0, 1.0};
    return t;
}

} // namespace stack::glass::motion::presets
