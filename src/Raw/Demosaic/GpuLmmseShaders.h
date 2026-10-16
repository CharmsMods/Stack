#pragma once
// GPU adaptation of the BSD-2-Clause Zhang-Wu implementation used by
// LmmseDemosaic.cpp. Copyright (c) 2010-2011 Pascal Getreuer.
// Full notice: legal/third_party/ZhangWu-BSD-2-Clause.txt.
namespace Raw::Demosaic::Shaders {
inline constexpr const char* LmmseDifference = R"GLSL(
vec4 evaluate(ivec2 p) {
    const float f[5]=float[5](-0.25,0.5,0.5,0.5,-0.25);
    vec2 v=vec2(0);
    for(int k=-2;k<=2;++k) v+=f[k+2]*vec2(raw(p+ivec2(k,0)),raw(p+ivec2(0,k)));
    return vec4((cfa(p)==1?-1:1)*(v-raw(p)),0,1);
}
)GLSL";
inline constexpr const char* LmmseSmooth = R"GLSL(
vec4 evaluate(ivec2 p) {
    const float f[9]=float[9](0.03125,0.0703125,0.1171875,0.1796875,0.203125,0.1796875,0.1171875,0.0703125,0.03125);
    vec2 v=vec2(0);
    for(int k=-4;k<=4;++k) v+=f[k+4]*vec2(a(p+ivec2(k,0)).r,a(p+ivec2(0,k)).g);
    return vec4(v,0,1);
}
)GLSL";
inline constexpr const char* LmmseGreen = R"GLSL(
vec4 evaluate(ivec2 p) {
    float green=raw(p);
    if(cfa(p)!=1) {
        vec2 estimate,variance;
        for(int d=0;d<2;++d) {
            // Centered accumulation avoids cancellation in E[x*x]-E[x]^2.
            float mean=0;
            for(int k=-4;k<=4;++k) mean+=b(p+(d==0?ivec2(k,0):ivec2(0,k)))[d];
            mean/=9;
            float squared=0,noise=0;
            for(int k=-4;k<=4;++k) {
                ivec2 q=p+(d==0?ivec2(k,0):ivec2(0,k));
                float v=b(q)[d],delta=v-mean,residual=v-a(q)[d];
                squared+=delta*delta; noise+=residual*residual;
            }
            const float epsilon=0.1/(255*255);
            float prior=squared/8,r=noise/9+epsilon,gain=prior/(prior+r);
            estimate[d]=mean+gain*(a(p)[d]-mean);
            variance[d]=(1-gain)*prior+epsilon;
        }
        green+=(variance.y*estimate.x+variance.x*estimate.y)/(variance.x+variance.y);
    }
    return vec4(green,0,0,1);
}
)GLSL";
inline constexpr const char* LmmseChroma = R"GLSL(
vec2 chroma(ivec2 p) {
    vec2 v=vec2(0); int c=cfa(p);
    if(c!=1) v[c/2]=a(p).r-raw(p);
    return v;
}
vec4 evaluate(ivec2 p) {
    vec2 v=chroma(p);
    if(cfa(p)!=1) {
        int opposite=1-cfa(p)/2;
        v[opposite]=0.25*(chroma(p+ivec2(-1,-1))[opposite]+chroma(p+ivec2(1,-1))[opposite]+
            chroma(p+ivec2(-1,1))[opposite]+chroma(p+ivec2(1,1))[opposite]);
    }
    return vec4(v,0,1);
}
)GLSL";
inline constexpr const char* LmmseColor = R"GLSL(
vec4 evaluate(ivec2 p) {
    vec2 diff=b(p).rg;
    if(cfa(p)==1) diff=0.25*(b(p+ivec2(-1,0)).rg+b(p+ivec2(1,0)).rg+b(p+ivec2(0,-1)).rg+b(p+ivec2(0,1)).rg);
    float g=a(p).r;
    return vec4(restoreSample(p,vec3(g-diff.x,g,g-diff.y)),1);
}
)GLSL";
}
