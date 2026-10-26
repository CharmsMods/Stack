#pragma once
#include "motion_presets.hpp"
#include "../glass_presets.hpp"
#include <optional>

namespace stack::glass::engine {

enum class Appearance { ClearWater, CalibratedAppleReference };

// Art direction is independent from backdrop brightness. The renderer samples
// one physical-looking material against light, middle-gray and dark scenes.
// This preset is original; it is not an estimate of Apple's private parameters.
inline std::optional<gl::MaterialParameters> clearWater(float halfHeightPx) {
    auto p = presets::clearControlV2(halfHeightPx);
    if (!p) return {};
    auto& m = *p;
    m.neutralContrast = 0;
    m.chromaGamma = 1;
    m.tintAmount = 0;
    m.roughnessLod = 0;
    m.bodyReflection = 1.15f;
    m.rimReflection = 1.20f;
    m.innerShade = 0.19f;
    m.outerContour = 0.12f;
    m.adaptation = 0;
    m.contentReflection = 0;
    m.shadowOpacity = 0.13f;
    m.shadowSigmaPx = std::max(0.25f, 0.22f * halfHeightPx);
    m.shadowOffsetPx = {0, -0.38f * halfHeightPx};
    m.causticGain = 0.009f;
    m.causticWidthPx = 0.12f * halfHeightPx;
    m.lipGain = 0.34f;
    m.lightShape = {0.42f, 0.28f, -0.58f, 0.40f};
    m.lightGain = {2.35f, 0.70f};
    return p;
}

struct Style {
    Appearance appearance = Appearance::ClearWater;
    motion::MotionTuning motion = motion::presets::clearControl();
    SpringTuning placementSpring{7.0, 0.82};
    SpringTuning morphSpring{8.0, 0.84};
    float opacity = 1;
    float restFillOpacity = 0;
    Vec3 restFillLinear{0.96f, 0.96f, 0.97f};
    // Optional material override is in logical units. Engine scales every length
    // by the viewport scale; dimensionless lighting and IOR remain unchanged.
    std::optional<gl::MaterialParameters> materialOverride;
};

namespace styles {
inline Style slider() { return {}; }
inline Style graphPoint() { Style s; s.motion = motion::presets::graphPoint(); return s; }
inline Style button() { Style s; s.motion = motion::presets::button(); return s; }
inline Style selection() { Style s; s.motion = motion::presets::selection(); return s; }
inline Style precision() { Style s; s.motion = motion::presets::precision(); return s; }
inline Style materializingSlider() {
    Style s; s.motion = motion::presets::clearControlMaterialize(); s.restFillOpacity = 1; return s;
}
inline Style appleReference() {
    Style s; s.appearance = Appearance::CalibratedAppleReference; return s;
}
} // namespace styles

} // namespace stack::glass::engine
