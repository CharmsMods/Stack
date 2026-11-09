#pragma once
namespace Stack::Hdr {
inline constexpr char SdrAppearanceShader[]=R"GLSL(
uniform sampler3D uSdrAppearance;
vec3 sdrAppearance(vec3 hdr) {
    vec3 rgb=mat3(.627404,.069097,.016391,
        .329283,.919540,.088013,.043313,.011363,.895596)*hdr;
    float y=max(0.0,dot(rgb,vec3(.2627,.6780,.0593)));
    float low=min(rgb.r,min(rgb.g,rgb.b));
    float chroma=low<0.0 ? y/max(1e-8,y-low) : 1.0;
    rgb=clamp(vec3(y)+(rgb-vec3(y))*chroma,0.0,64.0);
    vec3 position=log2(vec3(1)+4.0*sqrt(rgb))*(128.0/5.04439411936);
    ivec3 p=min(ivec3(position),ivec3(127));
    vec3 f=position-vec3(p);
    ivec3 order=ivec3(0,1,2);
    if(f[order.x]<f[order.y]) order.xy=order.yx;
    if(f[order.y]<f[order.z]) order.yz=order.zy;
    if(f[order.x]<f[order.y]) order.xy=order.yx;
    vec3 a=texelFetch(uSdrAppearance,p,0).rgb; ++p[order.x];
    vec3 b=texelFetch(uSdrAppearance,p,0).rgb; ++p[order.y];
    vec3 c=texelFetch(uSdrAppearance,p,0).rgb; ++p[order.z];
    vec3 d=texelFetch(uSdrAppearance,p,0).rgb;
    vec3 value=a+f[order.x]*(b-a)+f[order.y]*(c-b)+f[order.z]*(d-c);
    return value*value;
}
)GLSL";
}
