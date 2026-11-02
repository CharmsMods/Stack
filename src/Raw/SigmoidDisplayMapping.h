#pragma once

// Log-logistic curve and hue/energy correction adapted from darktable's
// src/iop/sigmoid.c, Copyright (C) 2020-2026 darktable developers.
// SPDX-License-Identifier: GPL-3.0-or-later
// Stack's zero-skew SDR implementation prepares coefficients analytically,
// shares them with GLSL, and leaves exposure placement to its existing owner.
#include <algorithm>
#include <array>
#include <cmath>

namespace Raw::SigmoidDisplay {
inline constexpr const char* Id = "sigmoid-v1";
inline constexpr float MiddleGrey = .1845f;
inline constexpr float Black = .000152f;

class PreparedCurve {
public:
    PreparedCurve(float grey=MiddleGrey, float contrast=1.f,
                  float exposure=0.f, float pivotEv=0.f) {
        grey=std::clamp(grey,.01f,.5f);
        power_=1.5f*std::clamp(contrast,.25f,2.5f);
        const double relation=1.0/double(grey)-1.0;
        const double greyRoot=std::pow(relation,1.0/double(power_));
        const double blackRoot=std::pow(1.0/double(Black)-1.0,1.0/double(power_));
        fog_=float(double(grey)*greyRoot/(blackRoot-greyRoot));
        paper_=float(std::pow(double(fog_)+grey,double(power_))*relation);
        scale_=std::exp2(exposure-pivotEv);
    }
    float Evaluate(float input) const {
        const float response=std::pow(fog_+std::max(0.f,input*scale_),power_);
        // Reciprocal form remains stable near white, including overflow.
        return 1.f/(1.f+paper_/response);
    }
    std::array<float,4> Coefficients() const {return {power_,fog_,paper_,scale_};}
    std::array<float,3> Rgb(std::array<float,3> input, bool preserveHue) const {
        const float average=std::max(0.f,(input[0]+input[1]+input[2])/3.f);
        const float minimum=std::min({input[0],input[1],input[2]});
        if(minimum<0.f) {
            const float saturation=average/(average-minimum);
            for(auto& value:input)value=average+saturation*(value-average);
        }
        std::array<float,3> output{Evaluate(input[0]),Evaluate(input[1]),Evaluate(input[2])};
        if(!preserveHue)return output;
        int low=0,high=0;
        for(int c=1;c<3;++c) {
            if(input[c]<input[low])low=c;
            if(input[c]>=input[high])high=c;
        }
        if(low==high)return output;
        const int mid=3-low-high;
        const float chroma=input[high]-input[low];
        if(chroma==0.f)return output; // Neutral pixels need no hue/energy correction.
        const float position=chroma>0.f?(input[mid]-input[low])/chroma:0.f;
        const float corrected=output[low]+position*(output[high]-output[low]);
        const float sum=input[low]+input[mid];
        const float blend=sum>0.f?2.f*input[low]/sum:0.f;
        const float energy=blend*(output[0]+output[1]+output[2])+
            (1.f-blend)*(output[low]+corrected+output[high]);
        if(corrected<=output[mid]) {
            output[mid]=(position*output[high]+(1.f-position)*(energy-output[high]))/(2.f-position);
            output[low]=energy-output[high]-output[mid];
        } else {
            output[mid]=(output[low]*(1.f-position)+position*(energy-output[low]))/(1.f+position);
            output[high]=energy-output[low]-output[mid];
        }
        return output;
    }
private:
    float power_=1.5f,fog_=0,paper_=1,scale_=1;
};

// Coefficients are prepared once per image submission, outside the pixel shader.
inline const char* ShaderFunctions() {return R"GLSL(
float sigmoidDisplayCurve(float inputValue) {
    float response=pow(uSigmoidParameters.y+max(0.0,inputValue*uSigmoidParameters.w),uSigmoidParameters.x);
    return 1.0/(1.0+uSigmoidParameters.z/response);
}
vec3 sigmoidDisplayRgb(vec3 inputRgb) {
    float average=max(0.0,(inputRgb.r+inputRgb.g+inputRgb.b)/3.0);
    float minimum=min(inputRgb.r,min(inputRgb.g,inputRgb.b));
    if(minimum<0.0)inputRgb=vec3(average)+(inputRgb-vec3(average))*(average/(average-minimum));
    vec3 outputRgb=vec3(sigmoidDisplayCurve(inputRgb.r),sigmoidDisplayCurve(inputRgb.g),sigmoidDisplayCurve(inputRgb.b));
    if(uPreserveHue==0)return outputRgb;
    int low=0,high=0;
    for(int c=1;c<3;++c) {
        if(inputRgb[c]<inputRgb[low])low=c;
        if(inputRgb[c]>=inputRgb[high])high=c;
    }
    if(low==high)return outputRgb;
    int mid=3-low-high;
    float chroma=inputRgb[high]-inputRgb[low];
    if(chroma==0.0)return outputRgb;
    float position=chroma>0.0?(inputRgb[mid]-inputRgb[low])/chroma:0.0;
    float corrected=mix(outputRgb[low],outputRgb[high],position);
    float sum=inputRgb[low]+inputRgb[mid];
    float blend=sum>0.0?2.0*inputRgb[low]/sum:0.0;
    float energy=blend*(outputRgb.r+outputRgb.g+outputRgb.b)+(1.0-blend)*(outputRgb[low]+corrected+outputRgb[high]);
    if(corrected<=outputRgb[mid]) {
        outputRgb[mid]=(position*outputRgb[high]+(1.0-position)*(energy-outputRgb[high]))/(2.0-position);
        outputRgb[low]=energy-outputRgb[high]-outputRgb[mid];
    } else {
        outputRgb[mid]=(outputRgb[low]*(1.0-position)+position*(energy-outputRgb[low]))/(1.0+position);
        outputRgb[high]=energy-outputRgb[low]-outputRgb[mid];
    }
    return outputRgb;
}
)GLSL";}
} // namespace Raw::SigmoidDisplay
