#pragma once
#include "Color.h"
#include "SdrAppearance.h"
#include "SdrAppearanceShader.h"
#include <string>

namespace Stack::Hdr {
// Output-only trim, in linear Rec.709 relative to diffuse white. No image
// statistics or display settings enter this portable finish.
struct SdrTrim {
    float brightness = 0.f;
    float highlightDetail = 0.f;
    float saturation = 1.f;
};
inline float SdrShoulder(float y) {
    // The perceptual table uses this neutral curve: HDR reference white at .55,
    // leaving room for neutral highlights. The master peak lands at .98, retaining
    // a little color room. A C1 tail handles imported content above the master
    // peak without clipping it or deriving an image-dependent exposure.
    constexpr float white = .55f, peak = .98f;
    constexpr float offset = MasterHeadroom*(peak-white)/(white*MasterHeadroom-peak);
    constexpr float scale = peak*(MasterHeadroom+offset)/MasterHeadroom;
    constexpr float slope = scale*offset/((MasterHeadroom+offset)*(MasterHeadroom+offset));
    y=std::max(0.f,y);
    if(y<=MasterHeadroom) return scale*y/(y+offset);
    return 1.f-(1.f-peak)/(1.f+(y-MasterHeadroom)*slope/(1.f-peak));
}
inline Rgb FitSdrGamut(Rgb rgb, float saturation = 1.f) {
    const float y=std::clamp(.2126f*rgb[0]+.7152f*rgb[1]+.0722f*rgb[2],0.f,1.f);
    float chroma=std::max(0.f,saturation);
    for(float v:rgb) {
        if(v<y) chroma=std::min(chroma,y/(y-v));
    }
    // Remove unrepresentable negative channels toward neutral, but resolve
    // the upper boundary by lowering the whole color. Holding luminance while
    // pulling an overbright color toward gray turns bright paint nearly white.
    for(float& v:rgb) v=std::max(0.f,y+(v-y)*chroma);
    const float peak=std::max({1.f,rgb[0],rgb[1],rgb[2]});
    for(float& v:rgb) v/=peak;
    return rgb;
}
class PreparedSdrFinish {
public:
    explicit PreparedSdrFinish(const SdrTrim& trim={0.f,1.f,1.f}) :
        exposure_(std::exp2(std::clamp(trim.brightness,-3.f,3.f))),
        amount_(std::clamp(trim.highlightDetail,0.f,1.f)),
        saturation_(std::max(0.f,trim.saturation)) {}
    Rgb Apply(Rgb base, Rgb hdr) const {
        if(amount_>0.f) {
            const auto mapped=SdrAppearance(hdr);
            for(int c=0;c<3;++c) base[c]=base[c]*(1.f-amount_)+mapped[c]*amount_;
        }
        const float peak=std::clamp(std::max({base[0],base[1],base[2]}),0.f,1.f);
        // Exposure near black, smoothly diminishing toward white. Negative
        // brightness reserves highlight range instead of lowering peak white.
        const float gain=exposure_/(1.f+(exposure_-1.f)*peak);
        for(float& v:base) v*=gain;
        return FitSdrGamut(base,saturation_);
    }
    Rgb Uniforms() const { return {exposure_,amount_,saturation_}; }
private:
    float exposure_,amount_,saturation_;
};
inline Rgb FinishSdr(Rgb base, Rgb hdr, const SdrTrim& trim) {
    return PreparedSdrFinish(trim).Apply(base,hdr);
}
inline Rgb DefaultSdrFromHdr(Rgb hdr) {
    static const PreparedSdrFinish finish;
    return finish.Apply({},hdr);
}
inline const std::string SdrFinishShader=std::string(SdrAppearanceShader)+R"GLSL(
vec3 fitSdrOutputGamut(vec3 rgb,float saturation) {
    float y=clamp(dot(rgb,vec3(.2126,.7152,.0722)),0.0,1.0);
    float chroma=max(0.0,saturation);
    for(int c=0;c<3;++c) {
        if(rgb[c]<y) chroma=min(chroma,y/(y-rgb[c]));
    }
    rgb=max(vec3(0),vec3(y)+(rgb-vec3(y))*chroma);
    return rgb/max(1.0,max(rgb.r,max(rgb.g,rgb.b)));
}
vec3 finishSdrOutput(vec3 base,vec3 hdr,vec3 trim) {
    if(trim.y>0.0) {
        base=mix(base,sdrAppearance(hdr),trim.y);
    }
    float maximum=clamp(max(base.r,max(base.g,base.b)),0.0,1.0);
    return fitSdrOutputGamut(base*(trim.x/(1.0+(trim.x-1.0)*maximum)),trim.z);
}
)GLSL";
}
