#pragma once
#include "SigmoidDisplayMapping.h"
#include "Hdr/Color.h"
#include "Hdr/SdrFinish.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace Raw::ViewTransform {
// The default RAW finish is shared by Stack and standalone file output. It
// consumes scene-linear color; clipping belongs after tone and gamut mapping.
struct Parameters {
    float exposure=0,middleGrey=SigmoidDisplay::MiddleGrey;
    float contrast=1,contrastPivotEv=0,saturation=1;
    bool preserveHue=true,debugFalseColor=false,inputIsRec2020=true;
    bool encodeSrgbOutput=true;
    float hdrHeadroom=1.f;
    Stack::Hdr::SdrTrim sdr{0.f,1.f,1.f};
};
inline constexpr Parameters Defaults{};

// Resolve the pointwise transform once per image, never once per output pixel.
// CPU libm and GLSL may round differently; both implement the same operations.
class PreparedTransform {
public:
    explicit PreparedTransform(const Parameters& parameters=Defaults):p_(parameters),
        sigmoid_(parameters.middleGrey,parameters.contrast,parameters.exposure,parameters.contrastPivotEv), sdr_(parameters.sdr) {}
    float Luma(float input) const {
        const float base=sigmoid_.Evaluate(input);
        if(p_.hdrHeadroom>1.f) return Stack::Hdr::Extend(base,p_.middleGrey,p_.hdrHeadroom);
        if(p_.sdr.highlightDetail==0.f && p_.sdr.brightness==0.f) return base;
        const float master=p_.sdr.highlightDetail>0.f ? Stack::Hdr::Extend(base,p_.middleGrey,Stack::Hdr::MasterHeadroom) : 0.f;
        return sdr_.Apply({base,base,base},{master,master,master})[0];
    }
    std::array<float,3> Rgb(std::array<float,3> rgb) const {
        if(p_.debugFalseColor) {
            const float oldLuma=DisplayLuma(p_.inputIsRec2020?Rec2020ToSrgb(rgb):rgb);
            const float ev=std::log2(std::max(.000001f,oldLuma)/std::max(.000001f,p_.middleGrey));
            if(ev<-4)return {.02f,.05f,.24f};
            if(ev<-2)return {0,.34f,.85f};
            if(ev<0)return {0,.65f,.45f};
            if(ev<2)return {.82f,.72f,.18f};
            if(ev<4)return {1,.42f,.08f};
            return {1,.06f,.04f};
        }
        rgb=sigmoid_.Rgb(rgb,p_.preserveHue);
        const auto sigmoidRgb=rgb;
        if(p_.hdrHeadroom>1.f) {
            for(auto& v:rgb) v=Stack::Hdr::Extend(v,p_.middleGrey,p_.hdrHeadroom);
            if(!p_.inputIsRec2020) rgb=Stack::Hdr::To2020(rgb);
            const float y=std::clamp(.2627f*rgb[0]+.6780f*rgb[1]+.0593f*rgb[2],0.f,p_.hdrHeadroom);
            // Limit saturation in the mastering gamut. Rec.709 transport
            // coordinates can legitimately lie outside its unit RGB cube.
            float chroma=std::max(0.f,p_.saturation);
            for(float v:rgb) {
                if(v>y) chroma=std::min(chroma,(p_.hdrHeadroom-y)/(v-y));
                if(v<y) chroma=std::min(chroma,y/(y-v));
            }
            for(auto& v:rgb) v=y+(v-y)*chroma;
            rgb=Rec2020ToSrgb(rgb);
            if(p_.encodeSrgbOutput) for(auto& v:rgb) v=Stack::Hdr::Srgb(v);
            return rgb;
        }
        if(p_.inputIsRec2020)rgb=Rec2020ToSrgb(rgb);
        CompressGamut(rgb);
        const float luma=DisplayLuma(rgb),saturation=std::max(0.f,p_.saturation);
        for(auto& value:rgb)value=luma*(1.f-saturation)+value*saturation;
        CompressGamut(rgb);
        if(p_.sdr.highlightDetail!=0.f || p_.sdr.brightness!=0.f || p_.sdr.saturation!=1.f) {
            if(p_.sdr.highlightDetail==0.f) {
                rgb=sdr_.Apply(rgb,{});
            } else {
            auto hdr=sigmoidRgb;
            for(float& v:hdr) v=Stack::Hdr::Extend(v,p_.middleGrey,Stack::Hdr::MasterHeadroom);
            if(!p_.inputIsRec2020) hdr=Stack::Hdr::To2020(hdr);
            const float y=std::clamp(.2627f*hdr[0]+.6780f*hdr[1]+.0593f*hdr[2],0.f,Stack::Hdr::MasterHeadroom);
            float chroma=std::max(0.f,p_.saturation);
            for(float v:hdr) {
                if(v>y) chroma=std::min(chroma,(Stack::Hdr::MasterHeadroom-y)/(v-y));
                if(v<y) chroma=std::min(chroma,y/(y-v));
            }
            for(float& v:hdr) v=y+(v-y)*chroma;
            rgb=sdr_.Apply(rgb,Rec2020ToSrgb(hdr));
            }
        }
        for(auto& value:rgb) {
            value=std::clamp(value,0.f,1.f);
            if(p_.encodeSrgbOutput)value=value<.0031308f?value*12.92f:
                1.055f*std::pow(value,1.f/2.4f)-.055f;
        }
        return rgb;
    }
private:
    static std::array<float,3> Rec2020ToSrgb(const std::array<float,3>& rgb) {
        return {
            1.6604910f*rgb[0]-.5876411f*rgb[1]-.0728499f*rgb[2],
            -.1245505f*rgb[0]+1.1328999f*rgb[1]-.0083494f*rgb[2],
            -.0181508f*rgb[0]-.1005789f*rgb[1]+1.1187297f*rgb[2]};
    }
    static float DisplayLuma(const std::array<float,3>& rgb) {
        return std::max(0.f,.2126f*rgb[0]+.7152f*rgb[1]+.0722f*rgb[2]);
    }
    static float Smooth(float low,float high,float value) {
        const float t=std::clamp((value-low)/(high-low),0.f,1.f);
        return t*t*(3.f-2.f*t);
    }
    static void CompressGamut(std::array<float,3>& rgb) {
        const float luma=std::clamp(DisplayLuma(rgb),0.f,1.f);
        const float maximum=std::max(rgb[0],std::max(rgb[1],rgb[2]));
        const float minimum=std::min(rgb[0],std::min(rgb[1],rgb[2]));
        float amount=0;
        if(maximum>1)amount=std::max(amount,(maximum-1)/std::max(.00001f,maximum-luma));
        if(minimum<0)amount=std::max(amount,-minimum/std::max(.00001f,luma-minimum));
        const float highlight=Smooth(.68f,1.f,luma)*Smooth(.92f,1.35f,maximum);
        amount=std::clamp(std::max(amount,highlight*.45f),0.f,1.f);
        for(auto& value:rgb)value=value*(1.f-amount)+luma*amount;
    }
    Parameters p_;
    SigmoidDisplay::PreparedCurve sigmoid_;
    Stack::Hdr::PreparedSdrFinish sdr_;
};

// Actual Stack View Transform shader functions, kept with their CPU reference.
// Callers declare the u* settings and supply one scene-linear RGB sample.
inline const std::string& ShaderFunctions() {
    static const std::string source=std::string(R"GLSL(
float lumaOf(vec3 rgb) {
    return max(0.0, dot(rgb, vec3(0.2126, 0.7152, 0.0722)));
}
vec3 falseColor(float ev) {
    if (ev < -4.0) return vec3(0.02, 0.05, 0.24);
    if (ev < -2.0) return vec3(0.0, 0.34, 0.85);
    if (ev < 0.0) return vec3(0.0, 0.65, 0.45);
    if (ev < 2.0) return vec3(0.82, 0.72, 0.18);
    if (ev < 4.0) return vec3(1.0, 0.42, 0.08);
    return vec3(1.0, 0.06, 0.04);
}
vec3 compressDisplayGamut(vec3 rgb) {
    float luma = clamp(lumaOf(rgb), 0.0, 1.0);
    float maxChannel = max(rgb.r, max(rgb.g, rgb.b));
    float minChannel = min(rgb.r, min(rgb.g, rgb.b));
    float amount = 0.0;
    if (maxChannel > 1.0) {
        amount = max(amount, (maxChannel - 1.0) / max(0.00001, maxChannel - luma));
    }
    if (minChannel < 0.0) {
        amount = max(amount, (0.0 - minChannel) / max(0.00001, luma - minChannel));
    }
    float highlightChroma = smoothstep(0.68, 1.0, luma) * smoothstep(0.92, 1.35, maxChannel);
    amount = clamp(max(amount, highlightChroma * 0.45), 0.0, 1.0);
    return mix(rgb, vec3(luma), amount);
}
vec3 rec2020ToLinearSrgb(vec3 rgb) {
    return mat3(
         1.6604910, -0.1245505, -0.0181508,
        -0.5876411,  1.1328999, -0.1005789,
        -0.0728499, -0.0083494,  1.1187297) * rgb;
}
vec3 linearToSrgb(vec3 rgb) {
    vec3 low = rgb * 12.92;
    vec3 high = 1.055 * pow(max(rgb, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), rgb));
}
)GLSL")+SigmoidDisplay::ShaderFunctions()+Stack::Hdr::SdrFinishShader+R"GLSL(
vec3 hdrViewFinish(vec3 rgb,float headroom) {
    vec3 x=max(rgb-vec3(uMiddleGrey),vec3(0));
    rgb=min(rgb,vec3(uMiddleGrey))+x/(1.0-x*(1.0/(1.0-uMiddleGrey)-1.0/(headroom-uMiddleGrey)));
    if(uInputIsRec2020==0) rgb=mat3(.627404,.069097,.016391,
        .329283,.919540,.088013,.043313,.011363,.895596)*rgb;
    float y=clamp(dot(rgb,vec3(.2627,.6780,.0593)),0.0,headroom);
    float chroma=max(0.0,uSaturation);
    for(int c=0;c<3;++c) {
        if(rgb[c]>y) chroma=min(chroma,(headroom-y)/(rgb[c]-y));
        if(rgb[c]<y) chroma=min(chroma,y/(y-rgb[c]));
    }
    return rec2020ToLinearSrgb(vec3(y)+(rgb-y)*chroma);
}
vec3 applyViewTransform(vec3 inputRgb) {
    if(uDisplayLinearInput!=0) {
        vec3 rgb=uInputIsRec2020!=0 ? rec2020ToLinearSrgb(inputRgb) : inputRgb;
        if(uHdrHeadroom<=1.0) rgb=finishSdrOutput(vec3(0),rgb,vec3(uSdrTrim.x,1.0,uSdrTrim.z));
        return uEncodeSrgbOutput!=0 ? sign(rgb)*linearToSrgb(abs(rgb)) : rgb;
    }
    if (uDebugFalseColor != 0) {
        vec3 displayInput = uInputIsRec2020 != 0 ? rec2020ToLinearSrgb(inputRgb) : inputRgb;
        float oldLuma = lumaOf(displayInput);
        float ev = log2(max(0.000001, oldLuma) / max(0.000001, uMiddleGrey));
        return falseColor(ev);
    }
    vec3 rgb = sigmoidDisplayRgb(inputRgb);
    vec3 sigmoidRgb=rgb;
    if(uHdrHeadroom>1.0) {
        rgb=hdrViewFinish(rgb,uHdrHeadroom);
        return uEncodeSrgbOutput!=0 ? sign(rgb)*linearToSrgb(abs(rgb)) : rgb;
    }
    if (uInputIsRec2020 != 0) rgb = rec2020ToLinearSrgb(rgb);
    rgb = compressDisplayGamut(rgb);
    float luma = lumaOf(rgb);
    rgb = mix(vec3(luma), rgb, max(0.0, uSaturation));
    rgb = compressDisplayGamut(rgb);
    if(uSdrTrim.x!=1.0 || uSdrTrim.y!=0.0 || uSdrTrim.z!=1.0)
        rgb=finishSdrOutput(rgb,uSdrTrim.y>0.0 ? hdrViewFinish(sigmoidRgb,1000.0/203.0) : vec3(0),uSdrTrim);
    rgb = clamp(rgb, 0.0, 1.0);
    if (uEncodeSrgbOutput != 0) rgb = linearToSrgb(rgb);
    return rgb;
}
)GLSL";
    return source;
}
} // namespace Raw::ViewTransform
