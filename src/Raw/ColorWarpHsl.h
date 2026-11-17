#pragma once
#include "Raw/ColorWarpMath.h"
#include <algorithm>
#include <cmath>

namespace Stack::RawRecipe {
// Canonical anchors are immutable; prepare coefficients on render requests,
// never image analysis or UI frames. XYZ/Oklab coordinates remain version 4.
inline const std::array<RawColorWarpPin,8>& ColorWarpHslAnchors() {
    static const auto anchors=[] {
        std::array<RawColorWarpPin,8> result;
        for(int i=0;i<8;++i)result[i]=MakeColorWarpFamily(i);
        return result;
    }();
    return anchors;
}
inline bool UsesColorWarpHsl(const RawColorWarpRecipe& recipe,const RawColorWarpPin& pin) {
    if(recipe.version<5 || pin.family<0 || pin.family>=8 || pin.moveColors || pin.protectColor ||
       pin.tintA!=0 || pin.tintB!=0 || pin.sampleRadius>0 || !pin.population.empty() ||
       !pin.regionId.empty() || pin.saturationRange.enabled || pin.exposureRange.enabled)return false;
    const auto& anchor=ColorWarpHslAnchors()[pin.family];
    const auto& a=anchor.hueRange;const auto& b=pin.hueRange;
    return pin.sourceA==anchor.sourceA && pin.sourceB==anchor.sourceB &&
        a.enabled==b.enabled && a.low==b.low && a.high==b.high &&
        a.featherLow==b.featherLow && a.featherHigh==b.featherHigh;
}
using ColorWarpHslCoefficients=std::array<std::array<float,4>,8>;
inline ColorWarpHslCoefficients PrepareColorWarpHsl(const RawColorWarpRecipe& recipe) {
    ColorWarpHslCoefficients result{};
    for(int i=0;i<8;++i) {
        const auto& a=ColorWarpHslAnchors()[i];
        result[i][0]=ColorWarpHue(a.sourceA,a.sourceB);
    }
    for(const auto& pin:recipe.pins)if(UsesColorWarpHsl(recipe,pin) && pin.enabled) {
        auto& c=result[pin.family];
        c[1]=pin.hueShift*pin.strength;
        c[2]=pin.saturation*pin.strength;
        c[3]=pin.brightness*pin.strength;
    }
    return result;
}
inline std::array<float,3> EvaluateColorWarpHsl(const ColorWarpHslCoefficients& coefficients,float hue) {
    for(int i=0;i<8;++i) {
        const auto& a=coefficients[i];const auto& b=coefficients[(i+1)%8];
        const auto positive=[](float h){return h-360.f*std::floor(h/360.f);};
        const float span=positive(b[0]-a[0]),distance=positive(hue-a[0]);
        if(distance<=span) {
            float t=distance/std::max(span,.000001f);t=t*t*(3.f-2.f*t);
            // Shortest rotation; exact half-turn ties consistently take +180.
            float delta=ColorWarpHueDelta(b[1]-a[1]);if(delta==-180.f)delta=180.f;
            return {a[1]+t*delta,a[2]+t*(b[2]-a[2]),a[3]+t*(b[3]-a[3])};
        }
    }
    return {};
}
}
