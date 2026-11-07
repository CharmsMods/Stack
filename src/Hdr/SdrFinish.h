#pragma once
#include "Color.h"

namespace Stack::Hdr {
// Output-only trim, in linear Rec.709 relative to diffuse white. No image
// statistics or display settings enter this portable finish.
struct SdrTrim {
    float brightness = 0.f;
    float highlightDetail = 0.f;
    float saturation = 1.f;
};
inline float SdrShoulder(float y) {
    constexpr float knee = .1845f, shape = 12.f;
    if (y <= knee) return y;
    return knee + (1.f-knee)*std::log1p(shape*(y-knee)/(MasterHeadroom-knee))/std::log1p(shape);
}
inline Rgb FitSdrGamut(Rgb rgb, float saturation = 1.f) {
    const float y=std::clamp(.2126f*rgb[0]+.7152f*rgb[1]+.0722f*rgb[2],0.f,1.f);
    float chroma=std::max(0.f,saturation);
    for(float v:rgb) {
        if(v>y) chroma=std::min(chroma,(1.f-y)/(v-y));
        if(v<y) chroma=std::min(chroma,y/(y-v));
    }
    for(float& v:rgb) v=std::clamp(y+(v-y)*chroma,0.f,1.f);
    return rgb;
}
inline Rgb FinishSdr(Rgb base, Rgb hdr, const SdrTrim& trim) {
    const float amount=std::clamp(trim.highlightDetail,0.f,1.f);
    const float exposure=std::exp2(std::clamp(trim.brightness,-3.f,3.f));
    if(amount==0.f) {
        for(float& v:base) v*=exposure;
        return FitSdrGamut(base,trim.saturation);
    }
    const float y=std::max(0.f,.2126f*hdr[0]+.7152f*hdr[1]+.0722f*hdr[2]);
    const float gain=y>1e-8f ? SdrShoulder(y)/y : 1.f;
    for(float& v:hdr) v*=gain;
    hdr=FitSdrGamut(hdr);
    for(int c=0;c<3;++c) base[c]=(base[c]+amount*(hdr[c]-base[c]))*exposure;
    return FitSdrGamut(base,trim.saturation);
}
inline constexpr char SdrFinishShader[]=R"GLSL(
vec3 fitSdrOutputGamut(vec3 rgb,float saturation) {
    float y=clamp(dot(rgb,vec3(.2126,.7152,.0722)),0.0,1.0);
    float chroma=max(0.0,saturation);
    for(int c=0;c<3;++c) {
        if(rgb[c]>y) chroma=min(chroma,(1.0-y)/(rgb[c]-y));
        if(rgb[c]<y) chroma=min(chroma,y/(y-rgb[c]));
    }
    return clamp(vec3(y)+(rgb-vec3(y))*chroma,0.0,1.0);
}
vec3 finishSdrOutput(vec3 base,vec3 hdr,vec3 trim) {
    if(trim.y<=0.0) return fitSdrOutputGamut(base*exp2(clamp(trim.x,-3.0,3.0)),trim.z);
    float y=max(0.0,dot(hdr,vec3(.2126,.7152,.0722)));
    float mapped=y<=.1845 ? y : .1845+.8155*log(1.0+12.0*(y-.1845)/(1000.0/203.0-.1845))/log(13.0);
    hdr=fitSdrOutputGamut(hdr*(y>1e-8 ? mapped/y : 1.0),1.0);
    return fitSdrOutputGamut(mix(base,hdr,clamp(trim.y,0.0,1.0))*exp2(clamp(trim.x,-3.0,3.0)),trim.z);
}
)GLSL";
}
