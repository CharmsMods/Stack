#pragma once
#include "RawImageBackdropNoiseAnalysisShaders.h"
#include <string>

namespace Stack::Renderer::RawImageBackdropNoiseShaders {
inline const std::string Synthesis = std::string(BandStatistics) + R"(
mat3x2 blurNoiseRetention(float deviation) {
    if (deviation<.0001) return mat3x2(vec2(1),vec2(1),vec2(1));
    float variance=deviation*deviation;
    vec3 covariance=vec3(1),energy=vec3(0);
    if (deviation>2.0) {
        energy=vec3(1);
        for (int lag=1;lag<=4;++lag) {
            vec3 r=2.0*vec3(0,grainCorrelation(1,lag),grainCorrelation(2,lag));
            float d=float(lag*lag);
            covariance+=r*exp(-.5*d/variance);energy+=r*exp(-.25*d/variance);
        }
        covariance=covariance*covariance/(6.2831853072*variance);
        energy=energy*energy/(12.5663706144*variance);
    } else {
        // All bands share the same blur kernel; evaluate it once per pixel.
        float weights[7];weights[0]=1.0;float total=1.0;
        for (int i=1;i<=6;++i) {weights[i]=exp(-.5*float(i*i)/variance);total+=2.0*weights[i];}
        for (int lag=1;lag<=4;++lag)
            covariance+=2.0*vec3(0,grainCorrelation(1,lag),grainCorrelation(2,lag))*weights[lag];
        for (int i=-6;i<=6;++i) {
            float a=weights[abs(i)];energy+=vec3(a*a);
            for (int lag=1;lag<=4;++lag)
                if (i+lag<=6) energy+=2.0*vec3(0,grainCorrelation(1,lag),grainCorrelation(2,lag))*a*weights[abs(i+lag)];
        }
        covariance/=total;energy/=total*total;
        covariance*=covariance;energy*=energy;
    }
    return mat3x2(vec2(covariance.x,energy.x),vec2(covariance.y,energy.y),vec2(covariance.z,energy.z));
}
uint grainHash(uint x) {x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
vec2 grainAddress(vec2 coordinate,ivec2 cell) {
    uint h=grainHash(uint(cell.x)^grainHash(uint(cell.y)+0x9e3779b9u));
    vec2 q=coordinate;
    if ((h&1u)!=0u) q.x=-q.x;
    if ((h&2u)!=0u) q.y=-q.y;
    if ((h&4u)!=0u) q=q.yx;
    vec2 offset=vec2(float((h>>3)&511u),float((h>>12)&511u));
    return (q+offset)/vec2(textureSize(noiseGrain,0).xy);
}
vec3 sampleGrain(vec2 coordinate,float lod,int band) {
    vec2 grid=coordinate/256.0;
    ivec2 cell=ivec2(floor(grid));
    vec2 f=smoothstep(vec2(0),vec2(1),fract(grid));
    vec3 result=vec3(0);float energy=0.0;
    for (int y=0;y<2;++y) for (int x=0;x<2;++x) {
        float weight=(x==0 ? 1.0-f.x : f.x)*(y==0 ? 1.0-f.y : f.y);
        vec2 p=grainAddress(coordinate,cell+ivec2(x,y));
        result+=weight*textureLod(noiseGrain,vec3(p,float(band)),lod).rgb;
        energy+=weight*weight;
    }
    return result*inversesqrt(max(.000001,energy));
}
vec3 colorNoise(vec3 independent,vec3 variance,vec3 covariance) {
    vec3 sigma=sqrt(max(vec3(0),variance));
    vec3 r=clamp(covariance/max(sigma.xxy*sigma.yzz,vec3(1e-20)),vec3(-1),vec3(1));
    // Cholesky in correlation space retains near-monochrome/rank-one noise.
    // Limit only roundoff outside the positive-semidefinite correlation cone.
    float y=sqrt(max(0.0,1.0-r.x*r.x));
    float zLimit=sqrt(max(0.0,1.0-r.y*r.y));
    float zy=y>1e-6 ? clamp((r.z-r.x*r.y)/y,-zLimit,zLimit) : 0.0;
    float zz=sqrt(max(0.0,1.0-r.y*r.y-zy*zy));
    return sigma*vec3(independent.x,r.x*independent.x+y*independent.y,
        r.y*independent.x+zy*independent.y+zz*independent.z);
}
vec3 matchedNoise(float blurredWeight,float sigma,vec3 visibleColor) {
    if (!noiseEnabled || blurredWeight<=0.0) return vec3(0);
    vec2 photoSpan=max(photo.zw-photo.xy,vec2(.000001));
    vec2 imagePoint=(uv-photo.xy)/photoSpan;
    vec2 profilePoint=clamp(imagePoint,vec2(0),vec2(1));
    vec2 outside=max(max(-imagePoint,imagePoint-1.0),vec2(0));
    vec2 outsideCells=outside*vec2(textureSize(noiseVariance,0).xy);
    float profileLod=log2(1.0+.5*length(outsideCells));
    float brightness=clamp(dot(visibleColor,vec3(.2126,.7152,.0722)),0.0,1.0);
    float bins=float(textureSize(noiseBrightnessVariance,0).x);
    vec2 brightnessPoint=vec2((brightness*(bins-1.0)+.5)/bins,.5);
    vec2 photoSize=photoSpan*screenSize;
    float distance=length(outside*photoSize)/max(.000001,min(photoSize.x,photoSize.y));
    float brightnessWeight=smoothstep(0.0,.35,distance);
    // Grain remains anchored in photo coordinates while panning and zooming.
    // Each independent spatial band has its own correctly filtered mip chain.
    vec2 coordinate=imagePoint*noiseNativeExtent;
    vec2 footprint=noiseNativeExtent/max(photoSpan*noisePhysicalSize,vec2(.000001));
    float reduction=max(1.0,max(footprint.x,footprint.y)),lod=log2(reduction);
    vec2 nativePerLogical=noiseNativeExtent/max(photoSpan*screenSize,vec2(.000001));
    float deviation=sigma*max(nativePerLogical.x,nativePerLogical.y)/reduction;
    mat3x2 retention=blurNoiseRetention(deviation);
    float sharpWeight=1.0-blurredWeight;
    vec3 components=vec3(0);
    for (int band=0;band<3;++band) {
        vec3 localPoint=vec3(profilePoint,float(band)),curvePoint=vec3(brightnessPoint,float(band));
        // At either end of the transition only one profile contributes.
        // Avoid doubling profile reads across the near and far surround.
        vec4 local=vec4(0,0,0,-1),curve=vec4(0,0,0,-1);
        if (brightnessWeight<1.0) local=textureLod(noiseVariance,localPoint,profileLod);
        float blend=local.a<0.0 ? 1.0 : brightnessWeight;
        if (blend>0.0) curve=textureLod(noiseBrightnessVariance,curvePoint,0.0);
        if (local.a<0.0 && curve.a<0.0) continue;
        vec3 variance=mix(local.rgb,curve.rgb,blend);
        if (max(variance.x,max(variance.y,variance.z))<1e-16) continue;
        vec3 localCovariance=blend<1.0 ? textureLod(noiseCovariance,localPoint,profileLod).rgb : vec3(0);
        vec3 curveCovariance=blend>0.0 ? textureLod(noiseBrightnessCovariance,curvePoint,0.0).rgb : vec3(0);
        vec3 covariance=mix(localCovariance,curveCovariance,blend);
        vec2 retained=retention[band];
        float remaining=sharpWeight*sharpWeight+2.0*sharpWeight*blurredWeight*retained.x+
            blurredWeight*blurredWeight*retained.y;
        float replacement=sqrt(clamp(1.0-remaining,0.0,1.0));
        components+=colorNoise(sampleGrain(coordinate,lod,band),variance,covariance)*replacement;
    }
    // This is a quiet display surround, not a reconstruction of all measured
    // photo texture. Keep its synthesized grain below the source amplitude.
    return .25*vec3(components.x*.5773502692+components.y*.7071067812+components.z*.4082482905,
                components.x*.5773502692-components.z*.8164965809,
                components.x*.5773502692-components.y*.7071067812+components.z*.4082482905);
}
)";
}
