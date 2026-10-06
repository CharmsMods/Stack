#pragma once
#include <algorithm>
#include <cmath>

namespace Raw::HdrDisplay {
inline constexpr const char* Photographic = "photographic-hdr-v1";
struct Parameters {
    float exposure=0, blackEv=-8, whiteEv=4, middleGrey=.18f, contrast=1, pivotEv=0;
};

// Extended Reinhard luminance mapping with an analytically anchored middle grey.
// The white point is expressed before the contrast adjustment, just as the
// black and pivot markers are. This maps the chosen white to 1 without letting
// a wider highlight range pull down the midtones. No spatial detail operation.
inline float Evaluate(float input,const Parameters& p) {
    const float grey=std::clamp(p.middleGrey,.01f,.5f);
    const float black=grey*std::exp2(p.blackEv);
    const float pivot=std::max(1e-6f,grey*std::exp2(p.pivotEv)-black);
    const float contrast=std::clamp(p.contrast,.25f,2.5f);
    const float white=std::max(pivot*1.01f,grey*std::exp2(p.whiteEv)-black);
    const float whiteRatio=std::pow(white/pivot,contrast);
    // A positive monotone curve through both anchors requires W/P > 1/sqrt(grey).
    // If markers cross or contrast is very low, retain the middle-grey anchor
    // and move only the effective white outward to this feasible boundary.
    const float w=std::max(whiteRatio,1.01f/std::sqrt(grey));
    const float x=std::pow(std::max(0.f,input*std::exp2(p.exposure)-black)/pivot,contrast);
    const float a=(grey-1.f/(w*w))/(1.f-grey);
    return std::clamp((a*x+(x/w)*(x/w))/(1.f+a*x),0.f,1.f);
}

// Use current full-image input statistics. Keep the user's exposure, contrast,
// pivot and black point. Half a stop above P99.9 leaves room for small highlights.
inline float FitWhite(float p999,float exposure,float grey,float pivotEv) {
    if(!std::isfinite(p999)||p999<=0)return std::max(4.f,pivotEv+2.f);
    return std::clamp(std::max(pivotEv+2.f,
        std::log2(p999/std::max(.01f,grey))+exposure+.5f),0.f,16.f);
}

// Same arithmetic as Evaluate, used by the active RAW View Transform shader.
inline const char* ShaderFunction() {return R"(
float photographicHdrCurve(float inputValue) {
    float grey=clamp(uMiddleGrey,0.01,0.5);
    float black=grey*exp2(uBlackEv);
    float pivot=max(0.000001,grey*exp2(uContrastPivotEv)-black);
    float contrast=clamp(uContrast,0.25,2.5);
    float white=max(pivot*1.01,grey*exp2(uWhiteEv)-black);
    float w=max(pow(white/pivot,contrast),1.01/sqrt(grey));
    float x=pow(max(0.0,inputValue*exp2(uExposure)-black)/pivot,contrast);
    float a=(grey-1.0/(w*w))/(1.0-grey);
    return clamp((a*x+(x/w)*(x/w))/(1.0+a*x),0.0,1.0);
}
)";}
}
