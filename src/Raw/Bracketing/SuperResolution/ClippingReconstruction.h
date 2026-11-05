#pragma once

#include "Kernel.h"
#include "../LocalAffineFit.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace Raw::Bracketing::Sr::Clipping {

// Only the broad reconstruction may regain support lost to a distant clipped
// sample. All colors share admission, while each fit uses its own measured CFA
// samples. Keep the matching GLSL in ClippingReconstructionShader.h in sync.
struct BroadCorrection {
    std::array<float,3> coefficients{};
    float mass=0;
    float modelVariance=0;
};

inline float Smooth(float low,float high,float value) {
    const float t=std::clamp((value-low)/(high-low),0.f,1.f);
    return t*t*(3-2*t);
}

inline float CoreInfluence(float dx,float dy) {
    // The four nearest CFA sites are within sqrt(2) raw pixels. A missing
    // measurement there cannot be supplied by a fitted plane. The outer fade
    // prevents a moving clipped sample from abruptly re-enabling a capture.
    return 1-Smooth(2.25f,6.25f,dx*dx+dy*dy);
}

inline float SpatialWeight(const Vec4& kernel,float dx,float dy) {
    return std::exp(-.5f*(kernel.x*dx*dx+2*kernel.y*dx*dy+kernel.z*dy*dy));
}

inline float SensorVariance(const Vec4& sample,float pilot) {
    return std::max(1e-10f,sample.y*std::max(0.f,pilot)+sample.z);
}

inline float FitWeight(const Vec4& sample,float spatial,float variance,float risk) {
    return 1e-5f*spatial*std::clamp(sample.w,0.f,1.f)/(variance+risk);
}

inline bool Finite(const Vec4& sample) {
    return std::isfinite(sample.x)&&std::isfinite(sample.y)&&
        std::isfinite(sample.z)&&std::isfinite(sample.w);
}

inline float CoreHeadroom(const FrameRegion& frame,const Vec4& mapping) {
    const int cx=int(std::floor(mapping.x+.5f)),cy=int(std::floor(mapping.y+.5f));
    float headroom=1;
    for(int y=cy-3;y<=cy+3;++y)for(int x=cx-3;x<=cx+3;++x) {
        const float influence=CoreInfluence(x-mapping.x,y-mapping.y);
        if(influence<=0)continue;
        if(x<frame.left||y<frame.top||x>=frame.left+frame.width||y>=frame.top+frame.height) {
            headroom=std::min(headroom,1-influence);continue;
        }
        const auto& sample=frame.samples[std::size_t(y-frame.top)*frame.width+x-frame.left];
        const float safety=Finite(sample)?std::clamp(sample.w,0.f,1.f):0;
        headroom=std::min(headroom,1-influence*(1-safety));
    }
    return headroom;
}

inline std::array<BroadCorrection,3> Reconstruct(
    const FrameRegion& frame,const Vec4& mapping,const Vec4& kernel,
    float pilot,float recoveredHeadroom) {
    std::array<BroadCorrection,3> result{};
    if(!(recoveredHeadroom>0))return result;
    const int cx=int(std::floor(mapping.x+.5f)),cy=int(std::floor(mapping.y+.5f));
    const float risk=std::max(0.f,mapping.w);
    float confidence=1;
    for(unsigned color=0;color<3;++color) {
        LocalAffineMoments<float> moments;
        float fullSpatial=0,validSpatial=0,softCount=0;
        for(int y=cy-3;y<=cy+3;++y)for(int x=cx-3;x<=cx+3;++x) {
            if(x<frame.left||y<frame.top||x>=frame.left+frame.width||y>=frame.top+frame.height||
                unsigned(frame.colors[((y&1)<<1)|(x&1)])!=color)continue;
            const auto& sample=frame.samples[std::size_t(y-frame.top)*frame.width+x-frame.left];
            if(sample.w==-2)return {};
            const float dx=x-mapping.x,dy=y-mapping.y;
            const float spatial=SpatialWeight(kernel,dx,dy);
            fullSpatial+=spatial;
            if(sample.w<=0)continue;
            if(!Finite(sample))return {};
            const float variance=SensorVariance(sample,pilot);
            const float weight=FitWeight(sample,spatial,variance,risk);
            validSpatial+=spatial*std::clamp(sample.w,0.f,1.f);
            softCount+=Smooth(0,.05f,sample.w);
            moments.Add(dx,dy,sample.x,weight);
        }
        LocalAffineFit<float> fit;
        if(!(fullSpatial>0)||!moments.Fit(fit))return {};
        const float xx=moments.xx/fit.weight-fit.meanX*fit.meanX;
        const float xy=moments.xy/fit.weight-fit.meanX*fit.meanY;
        const float yy=moments.yy/fit.weight-fit.meanY*fit.meanY;
        const float condition=(xx*yy-xy*xy)/((xx+yy)*(xx+yy));
        const float leverage=fit.meanX*(fit.inverseXX*fit.meanX+fit.inverseXY*fit.meanY)+
            fit.meanY*(fit.inverseXY*fit.meanX+fit.inverseYY*fit.meanY);
        if(!std::isfinite(condition)||!std::isfinite(leverage)||leverage<0)return {};
        confidence=std::min({confidence,
            Smooth(.5f,.85f,validSpatial/fullSpatial),Smooth(4,6,softCount),
            Smooth(.001f,.01f,condition),1-Smooth(.25f,1,leverage)});
        if(!(confidence>0))return {};

        float variance=0,gradientXVariance=0,gradientYVariance=0,meanVariance=0;
        float residual=0,expectedNoise=0,standardizedResidual=0,coefficientNorm=0;
        for(int y=cy-3;y<=cy+3;++y)for(int x=cx-3;x<=cx+3;++x) {
            if(x<frame.left||y<frame.top||x>=frame.left+frame.width||y>=frame.top+frame.height||
                unsigned(frame.colors[((y&1)<<1)|(x&1)])!=color)continue;
            const auto& sample=frame.samples[std::size_t(y-frame.top)*frame.width+x-frame.left];
            if(sample.w<=0)continue;
            const float dx=x-mapping.x,dy=y-mapping.y,v=SensorVariance(sample,pilot);
            const float weight=FitWeight(sample,SpatialWeight(kernel,dx,dy),v,risk);
            const auto coefficient=fit.Coefficients(dx,dy,weight);
            const float normalized=weight/fit.weight;
            const float difference=sample.x-(fit.value+fit.slopeX*dx+fit.slopeY*dy);
            variance+=coefficient[0]*coefficient[0]*v;
            gradientXVariance+=coefficient[1]*coefficient[1]*v;
            gradientYVariance+=coefficient[2]*coefficient[2]*v;
            meanVariance+=normalized*normalized*v;
            residual+=normalized*difference*difference;expectedNoise+=normalized*v;
            standardizedResidual+=normalized*difference*difference/v;
            coefficientNorm+=std::abs(coefficient[0]);
        }
        if(!std::isfinite(variance)||!std::isfinite(gradientXVariance)||
            !std::isfinite(gradientYVariance)||!std::isfinite(meanVariance)||
            !std::isfinite(residual)||!std::isfinite(expectedNoise)||
            !std::isfinite(standardizedResidual)||!std::isfinite(coefficientNorm))return {};
        const float modelVariance=std::max(0.f,residual-expectedNoise);
        const float noiseAmplification=(variance+modelVariance)/std::max(1e-20f,meanVariance);
        confidence=std::min({confidence,1-Smooth(1.25f,2,coefficientNorm),
            1-Smooth(3,9,standardizedResidual),1-Smooth(4,8,noiseAmplification),
            std::min(1.f,(meanVariance+risk)/(variance+modelVariance+risk))});
        if(!std::isfinite(variance)||!std::isfinite(modelVariance)||!(confidence>0))return {};

        // A dark, locally flat set of survivors must not predict through an
        // unseen bright step. Clipped observations supply lower bounds only.
        // Fade this constraint in throughout the shoulder, before h reaches 0.
        // Cauchy's bound includes value/gradient correlation without assuming
        // that uncertainty at a distant clipped location equals that at center.
        for(int y=cy-3;y<=cy+3;++y)for(int x=cx-3;x<=cx+3;++x) {
            if(x<frame.left||y<frame.top||x>=frame.left+frame.width||y>=frame.top+frame.height||
                unsigned(frame.colors[((y&1)<<1)|(x&1)])!=color)continue;
            const auto& sample=frame.samples[std::size_t(y-frame.top)*frame.width+x-frame.left];
            if(sample.w<0||sample.w>=1)continue;
            if(!Finite(sample))return {};
            const float dx=x-mapping.x,dy=y-mapping.y;
            const float prediction=fit.value+fit.slopeX*dx+fit.slopeY*dy;
            if(!std::isfinite(prediction))return {};
            const float deficit=std::max(0.f,sample.x-prediction);
            const float fitSigma=std::sqrt(variance)+std::abs(dx)*std::sqrt(gradientXVariance)+
                std::abs(dy)*std::sqrt(gradientYVariance)+std::sqrt(modelVariance);
            const float sigma=fitSigma+std::sqrt(SensorVariance(sample,std::max(pilot,sample.x))+risk);
            if(!std::isfinite(sigma))return {};
            confidence=std::min(confidence,
                1-(1-sample.w)*Smooth(2,4,deficit/std::max(1e-10f,sigma)));
        }
        if(!(confidence>0))return {};
        // Equivalent coefficients can be signed. Their sum is fit.weight here;
        // the caller combines them with its old broad coefficients BEFORE
        // propagating noise, cross-kernel covariance and capture support.
        result[color].coefficients={
            1+leverage,
            -(fit.meanX*fit.inverseXX+fit.meanY*fit.inverseXY),
            -(fit.meanX*fit.inverseXY+fit.meanY*fit.inverseYY)};
        result[color].mass=fit.weight;
        result[color].modelVariance=modelVariance;
    }
    const float scale=mapping.z*recoveredHeadroom*confidence;
    for(auto& color:result) {
        for(auto& coefficient:color.coefficients)coefficient*=scale;
        color.mass*=scale;
    }
    return result;
}

} // namespace Raw::Bracketing::Sr::Clipping
