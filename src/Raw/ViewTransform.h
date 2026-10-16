#pragma once
#include "HdrDisplayMapping.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace Raw::ViewTransform {
// The default RAW finish is shared by Stack and standalone file output. It
// consumes scene-linear color; clipping belongs after tone and gamut mapping.
struct Parameters {
    float exposure=0,blackEv=-8,whiteEv=4,middleGrey=.18f;
    float shoulder=.45f,toe=.18f,contrast=1,contrastPivotEv=0,saturation=1;
    bool preserveHue=true,debugFalseColor=false,inputIsRec2020=true;
    bool encodeSrgbOutput=true,photographicHdr=false;
};
inline constexpr Parameters Defaults{};

// Resolve the pointwise transform once per image, never once per output pixel.
// CPU libm and GLSL may round differently; both implement the same operations.
class PreparedTransform {
public:
    explicit PreparedTransform(const Parameters& parameters=Defaults):p_(parameters) {
        black_=p_.middleGrey*std::exp2(p_.blackEv);
        const float white=p_.middleGrey*std::exp2(p_.whiteEv);
        range_=std::max(.000001f,white-black_);
        exposure_=std::exp2(p_.exposure);
        contrast_=std::max(.05f,p_.contrast);
        pivot_=std::max(.000001f,(p_.middleGrey*std::exp2(p_.contrastPivotEv)-black_)/range_);
        toe_=std::clamp(p_.toe,0.f,1.f);
        shoulder_=std::max(.001f,p_.shoulder);
        whiteMapped_=1.f/(1.f+shoulder_);
    }
    float Luma(float input) const {
        if(p_.photographicHdr)return HdrDisplay::Evaluate(input,{p_.exposure,p_.blackEv,
            p_.whiteEv,p_.middleGrey,p_.contrast,p_.contrastPivotEv});
        float normalized=std::max(0.f,input*exposure_-black_)/range_;
        normalized=pivot_*std::pow(std::max(0.f,normalized)/pivot_,contrast_);
        const float toeMapped=(normalized+toe_*normalized/(normalized+.18f))/(1.f+toe_);
        normalized=normalized*(1.f-toe_)+toeMapped*toe_;
        const float mapped=normalized/(normalized+shoulder_);
        return std::clamp(mapped/std::max(.0001f,whiteMapped_),0.f,1.f);
    }
    std::array<float,3> Rgb(std::array<float,3> rgb) const {
        if(p_.inputIsRec2020)rgb={
            1.6604910f*rgb[0]-.5876411f*rgb[1]-.0728499f*rgb[2],
            -.1245505f*rgb[0]+1.1328999f*rgb[1]-.0083494f*rgb[2],
            -.0181508f*rgb[0]-.1005789f*rgb[1]+1.1187297f*rgb[2]};
        const float oldLuma=DisplayLuma(rgb);
        if(p_.debugFalseColor) {
            const float ev=std::log2(std::max(.000001f,oldLuma)/std::max(.000001f,p_.middleGrey));
            if(ev<-4)return {.02f,.05f,.24f};
            if(ev<-2)return {0,.34f,.85f};
            if(ev<0)return {0,.65f,.45f};
            if(ev<2)return {.82f,.72f,.18f};
            if(ev<4)return {1,.42f,.08f};
            return {1,.06f,.04f};
        }
        const float newLuma=Luma(oldLuma);
        for(auto& value:rgb)value=p_.preserveHue
            ?(oldLuma>.00001f?value*(newLuma/oldLuma):newLuma):Luma(value);
        CompressGamut(rgb);
        const float luma=DisplayLuma(rgb),saturation=std::max(0.f,p_.saturation);
        for(auto& value:rgb)value=luma*(1.f-saturation)+value*saturation;
        CompressGamut(rgb);
        for(auto& value:rgb) {
            value=std::clamp(value,0.f,1.f);
            if(p_.encodeSrgbOutput)value=value<.0031308f?value*12.92f:
                1.055f*std::pow(value,1.f/2.4f)-.055f;
        }
        return rgb;
    }
private:
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
    float black_=0,range_=1,exposure_=1,contrast_=1,pivot_=1,toe_=0,shoulder_=1,whiteMapped_=1;
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
float filmicCurve(float x) {
    float black = uMiddleGrey * exp2(uBlackEv);
    float white = uMiddleGrey * exp2(uWhiteEv);
    x = max(0.0, x * exp2(uExposure) - black);
    float norm = x / max(0.000001, white - black);
    float safeContrast = max(0.05, uContrast);
    float pivotInput = uMiddleGrey * exp2(uContrastPivotEv);
    float pivotNorm = max(
        0.000001,
        (pivotInput - black) / max(0.000001, white - black));
    norm = pivotNorm * pow(max(0.0, norm) / pivotNorm, safeContrast);
    float toe = clamp(uToe, 0.0, 1.0);
    norm = mix(norm, (norm + toe * norm / (norm + 0.18)) / (1.0 + toe), toe);
    float shoulder = max(0.001, uShoulder);
    float mapped = norm / (norm + shoulder);
    float whiteMapped = 1.0 / (1.0 + shoulder);
    return clamp(mapped / max(0.0001, whiteMapped), 0.0, 1.0);
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
)GLSL")+HdrDisplay::ShaderFunction()+R"GLSL(
float displayCurve(float x) {
    return uPhotographicHdr != 0 ? photographicHdrCurve(x) : filmicCurve(x);
}
vec3 applyViewTransform(vec3 inputRgb) {
    vec3 rgb = uInputIsRec2020 != 0 ? rec2020ToLinearSrgb(inputRgb) : inputRgb;
    float oldLuma = lumaOf(rgb);
    if (uDebugFalseColor != 0) {
        float ev = log2(max(0.000001, oldLuma) / max(0.000001, uMiddleGrey));
        return falseColor(ev);
    }
    float newLuma = displayCurve(oldLuma);
    if (uPreserveHue != 0) {
        rgb = oldLuma > 0.00001 ? rgb * (newLuma / oldLuma) : vec3(newLuma);
    } else {
        rgb = vec3(displayCurve(rgb.r), displayCurve(rgb.g), displayCurve(rgb.b));
    }
    rgb = compressDisplayGamut(rgb);
    float luma = lumaOf(rgb);
    rgb = mix(vec3(luma), rgb, max(0.0, uSaturation));
    rgb = compressDisplayGamut(rgb);
    rgb = clamp(rgb, 0.0, 1.0);
    if (uEncodeSrgbOutput != 0) rgb = linearToSrgb(rgb);
    return rgb;
}
)GLSL";
    return source;
}
} // namespace Raw::ViewTransform
