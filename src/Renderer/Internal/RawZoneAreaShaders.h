#pragma once
namespace Stack::Renderer::ZoneAreaShaders {
inline constexpr const char* Gain = R"GLSL(
#version 430 core
layout(local_size_x=16, local_size_y=16) in;
layout(binding=2) uniform sampler2D maskImage;
uniform int hasMask;
layout(r32f, binding=1) uniform image2D gainImage;
layout(binding=0) uniform sampler2D referenceImage;
layout(binding=1) uniform sampler2D curveImage;
layout(std430, binding=0) buffer Stats { uint low; uint high; uint black; uint count; uint bins[256]; };
uniform int firstArea;
uniform int measure;
uniform int enabled;
uniform int rec2020;
uniform float offsetEv;
uniform float strength;
uniform int histogramStride;
shared uint lows[256];
shared uint highs[256];
shared uint blacks[256];
shared uint counts[256];
uint ordered(float f) { uint u=floatBitsToUint(f); return u ^ ((u & 0x80000000u)!=0u ? 0xffffffffu : 0x80000000u); }
void main() {
    ivec2 p=ivec2(gl_GlobalInvocationID.xy);
    ivec2 size=imageSize(gainImage);
    uint i=gl_LocalInvocationIndex;
    lows[i]=0xffffffffu; highs[i]=0u; blacks[i]=0u; counts[i]=0u;
    if (all(lessThan(p,size))) {
        float m=hasMask!=0 ? clamp(texture(maskImage,(vec2(p)+.5)/vec2(size)).r,0.0,1.0) : 0.0;
        vec3 rgb=texelFetch(referenceImage,p,0).rgb;
        float l=dot(rgb,rec2020!=0 ? vec3(.2627002,.6779981,.0593017) : vec3(.2126729,.7151522,.0721750));
        float ev=l>0.0 ? log2(l/.18) : -32.0;
        if (isnan(ev) || isinf(ev)) ev=-32.0;
        float x=(clamp((ev+32.0)/64.0,0.0,1.0)*4096.0+.5)/4097.0;
        float delta=enabled!=0 ? m*(offsetEv+texture(curveImage,vec2(x,.5)).r)*strength : 0.0;
        float previous=firstArea!=0 ? 0.0 : imageLoad(gainImage,p).r;
        imageStore(gainImage,p,vec4(previous+delta));
        if (measure!=0 && m>0.0) {
            lows[i]=ordered(ev); highs[i]=ordered(ev); blacks[i]=l<=0.0 ? 1u:0u; counts[i]=1u;
            if (p.x%histogramStride==0 && p.y%histogramStride==0) {
                int bin=clamp(int((ev+32.0)*4.0),0,255);
                // Bounded histogram sampling; extrema above cover every pixel.
                atomicAdd(bins[bin],uint(round(m*65535.0)));
            }
        }
    }
    barrier();
    for (uint step=128u;step>0u;step/=2u) {
        if(i<step) { lows[i]=min(lows[i],lows[i+step]); highs[i]=max(highs[i],highs[i+step]);
            blacks[i]|=blacks[i+step]; counts[i]+=counts[i+step]; }
        barrier();
    }
    if(i==0u && measure!=0 && counts[0]>0u) {
        atomicMin(low,lows[0]); atomicMax(high,highs[0]); atomicOr(black,blacks[0]); atomicAdd(count,counts[0]);
    }
}
)GLSL";
inline constexpr const char* Apply = R"GLSL(
#version 430 core
layout(local_size_x=16,local_size_y=16) in;
layout(binding=0) uniform sampler2D inputImage;
layout(r32f,binding=0) readonly uniform image2D gainImage;
layout(rgba32f,binding=1) writeonly uniform image2D outputImage;
void main() {
    ivec2 p=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(p,imageSize(outputImage)))) return;
    vec4 c=texelFetch(inputImage,p,0);
    float gain=imageLoad(gainImage,p).r;
    if (gain==0.0) { imageStore(outputImage,p,c); return; }
    // Only the representable floating-point range bounds accumulated EV.
    vec3 rgb=c.rgb*exp2(clamp(gain,-120.0,120.0));
    if (any(isinf(rgb)) || abs(gain)>120.0) {
        rgb=sign(c.rgb)*exp2(clamp(log2(max(abs(c.rgb),vec3(1e-38)))+gain,vec3(-126),vec3(127)));
        rgb=mix(rgb,vec3(0),equal(c.rgb,vec3(0)));
    }
    imageStore(outputImage,p,vec4(rgb,c.a));
}
)GLSL";
}
