#pragma once
#include "Color.h"
namespace Stack::Hdr {
// Uncompressed gain-map reconstruction in the SDR base's linear Rec.709 gamut.
// The codec uses identical positive offsets; JPEG/downsizing are export-only.
inline Rgb GainMapPreview(Rgb base,Rgb hdr,float headroom) {
    const float weight=std::clamp(std::log2(std::max(1.f,headroom))/std::log2(MasterHeadroom),0.f,1.f);
    if(weight>=1.f) return hdr;
    if(weight<=0.f) return base;
    for(int c=0;c<3;++c) {
        const float a=std::max(0.f,base[c])+1e-7f,b=std::max(0.f,hdr[c])+1e-7f;
        base[c]=a*std::pow(b/a,weight)-1e-7f;
    }
    return base;
}
inline constexpr char GainMapPreviewShader[]=R"GLSL(
// Unpaired textures have no authored SDR base. Join the shoulder continuously
// at diffuse white, without changing ordinary UI colors below white.
vec3 stackUnpairedHdrPreview(vec3 hdr,float headroom) {
    float y=dot(hdr,vec3(.2126,.7152,.0722));
    if(headroom<1000.0/203.0 && y>1.0) {
        float extent=max(.00001,headroom-1.0), x=min(y,1000.0/203.0)-1.0;
        hdr*=(1.0+x/(1.0+x*(1.0/extent-1.0/(1000.0/203.0-1.0))))/y;
    }
    return hdr;
}
vec3 stackGainMapPreview(vec3 base,vec3 hdr,float headroom) {
    float weight=clamp(log2(max(1.0,headroom))/log2(1000.0/203.0),0.0,1.0);
    if(weight>=1.0) return hdr;
    if(weight<=0.0) return base;
    vec3 a=max(base,vec3(0))+vec3(1e-7), b=max(hdr,vec3(0))+vec3(1e-7);
    return a*pow(b/a,vec3(weight))-vec3(1e-7);
}
)GLSL";
}
