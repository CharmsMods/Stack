#pragma once

namespace Stack::Renderer::RawImageBackdropNoiseShaders {
inline constexpr const char* Filter = R"(#version 430 core
layout(local_size_x=8,local_size_y=8) in;
layout(rgba32f,binding=0) writeonly uniform image2DArray varianceOutput;
layout(rgba32f,binding=1) writeonly uniform image2DArray covarianceOutput;
uniform sampler2DArray varianceInput,covarianceInput;
uniform ivec2 direction;
uniform bool rejectOutliers;
vec3 median9(inout vec3 values[9]) {
    for (int i=1;i<9;++i) for (int j=i;j>0;--j) {
        vec3 lo=min(values[j-1],values[j]),hi=max(values[j-1],values[j]);
        values[j-1]=lo;values[j]=hi;
    }
    return values[4];
}
void main() {
    ivec3 p=ivec3(gl_GlobalInvocationID),size=imageSize(varianceOutput);
    if (any(greaterThanEqual(p,size))) return;
    vec4 center=texelFetch(varianceInput,p,0),crossCenter=texelFetch(covarianceInput,p,0);
    vec3 variance=center.rgb,covariance=crossCenter.rgb;
    if (center.a>=0.0 && rejectOutliers) {
        vec3 values[9];
        for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x) {
            ivec3 q=ivec3(clamp(p.xy+ivec2(x,y),ivec2(0),size.xy-1),p.z);
            vec4 value=texelFetch(varianceInput,q,0);
            values[(y+1)*3+x+1]=value.a>=0.0 && abs(value.a-center.a)<=.08 ? value.rgb : center.rgb;
        }
        variance=median9(values);
        // Change strength while retaining one valid correlation matrix.
        // Independently median-filtering six entries can make it indefinite.
        vec3 ratio=sqrt(max(vec3(0),variance)/max(center.rgb,vec3(1e-20)));
        covariance*=ratio.xxy*ratio.yzz;
    } else if (center.a>=0.0) {
        const float weights[4]=float[4](.27068215,.21674532,.11128076,.03663285);
        variance=vec3(0);covariance=vec3(0);float total=0.0;
        for (int i=-3;i<=3;++i) {
            ivec3 q=ivec3(clamp(p.xy+direction*i,ivec2(0),size.xy-1),p.z);
            vec4 value=texelFetch(varianceInput,q,0);
            if (value.a<0.0) continue;
            float difference=(value.a-center.a)/.08;
            float weight=weights[abs(i)]*exp(-.5*difference*difference);
            variance+=weight*value.rgb;covariance+=weight*texelFetch(covarianceInput,q,0).rgb;total+=weight;
        }
        variance/=max(total,1e-8);covariance/=max(total,1e-8);
    }
    imageStore(varianceOutput,p,vec4(variance,center.a));
    imageStore(covarianceOutput,p,vec4(covariance,center.a));
}
)";

inline constexpr const char* BrightnessProfile = R"(#version 430 core
layout(local_size_x=8,local_size_y=8) in;
layout(rgba32f,binding=0) writeonly uniform image2DArray varianceOutput;
layout(rgba32f,binding=1) writeonly uniform image2DArray covarianceOutput;
uniform sampler2DArray varianceInput,covarianceInput;
void main() {
    ivec3 p=ivec3(gl_GlobalInvocationID),outputSize=imageSize(varianceOutput);
    if (any(greaterThanEqual(p,outputSize))) return;
    float brightness=float(p.x)/float(max(1,outputSize.x-1));
    ivec2 size=textureSize(varianceInput,0).xy;
    float nearestSquared=1.0;
    for (int y=0;y<size.y;++y) for (int x=0;x<size.x;++x) {
        vec4 value=texelFetch(varianceInput,ivec3(x,y,p.z),0);
        if (value.a<0.0) continue;
        float delta=value.a-brightness;nearestSquared=min(nearestSquared,delta*delta);
    }
    vec3 variance=vec3(0),covariance=vec3(0);float total=0.0;
    for (int y=0;y<size.y;++y) for (int x=0;x<size.x;++x) {
        ivec3 q=ivec3(x,y,p.z);vec4 value=texelFetch(varianceInput,q,0);
        if (value.a<0.0) continue;
        float delta=value.a-brightness;
        float weight=exp(-.5*max(0.0,delta*delta-nearestSquared)/(.045*.045));
        variance+=weight*value.rgb;covariance+=weight*texelFetch(covarianceInput,q,0).rgb;total+=weight;
    }
    // Positive, common weights preserve covariance and therefore noise hue.
    imageStore(varianceOutput,p,vec4(variance/max(total,1e-8),total>0.0 ? 1.0 : -1.0));
    imageStore(covarianceOutput,p,vec4(covariance/max(total,1e-8),total>0.0 ? 1.0 : -1.0));
}
)";

inline constexpr const char* Seed = R"(#version 430 core
layout(local_size_x=8,local_size_y=8) in;
layout(rgba16f,binding=0) writeonly uniform image2DArray grainOutput;
uint hash(uint x) {x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
vec3 gaussian(ivec2 point,uint seed) {
    ivec2 p=(point%512+512)%512;
    float signValue=p.x<256 ? 1.0 : -1.0;p.x%=256;
    uint h=hash(uint(p.x)+uint(p.y)*512u+seed);
    uvec3 channels=uvec3(h,h^0x68bc21ebu,h^0x02e5be93u);
    vec3 a,b;
    for (int i=0;i<3;++i) {
        a[i]=(float(hash(channels[i])>>8)+.5)/16777216.0;
        b[i]=(float(hash(channels[i]^0x967a889bu)>>8)+.5)/16777216.0;
    }
    return signValue*sqrt(-2.0*log(a))*cos(6.2831853072*b);
}
void main() {
    ivec3 p=ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(p,imageSize(grainOutput)))) return;
    vec3 value=vec3(0);
    if (p.z==0) value=gaussian(p.xy,0x243f6a88u);
    else if (p.z==1) {
        for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x)
            value+=gaussian(p.xy+ivec2(x,y),0x9e3779b9u)*float((x==0?2:1)*(y==0?2:1));
        value/=6.0;
    } else {
        const float kernel[3]=float[3](6,4,1);
        for (int y=-2;y<=2;++y) for (int x=-2;x<=2;++x)
            value+=gaussian(p.xy+ivec2(x,y),0xb7e15162u)*kernel[abs(x)]*kernel[abs(y)];
        value/=70.0;
    }
    imageStore(grainOutput,p,vec4(value,1));
}
)";
}
