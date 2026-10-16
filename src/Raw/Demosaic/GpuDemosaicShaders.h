#pragma once

namespace Raw::Demosaic::Shaders {
// Algorithm bodies follow this common interface. No display transform or
// sensor-range clamp belongs here. Reflection preserves the Bayer phase.
inline constexpr const char* Common = R"GLSL(
#version 430 core
layout(local_size_x=16,local_size_y=16) in;
layout(binding=0) uniform sampler2D mosaic;
layout(binding=1) uniform sampler2D inputA;
layout(binding=2) uniform sampler2D inputB;
layout(binding=3) uniform sampler2D inputC;
layout(OUT_FORMAT,binding=0) writeonly uniform image2D result;
layout(std430,binding=0) buffer Status { uint status; };
shared uint groupStatus;
uniform ivec2 extent;
uniform int pattern, mode, rowOffset;
int reflect(int p,int size) {
    int period=2*(size-1), q=abs(p)%period;
    return q<size?q:period-q;
}
ivec2 mirrored(ivec2 p) { return ivec2(reflect(p.x,extent.x),reflect(p.y,extent.y)); }
int cfa(ivec2 p) {
    int phase=((p.y&1)<<1)|(p.x&1);
    if(pattern==1) return int[4](0,1,1,2)[phase];
    if(pattern==2) return int[4](2,1,1,0)[phase];
    if(pattern==3) return int[4](1,2,0,1)[phase];
    return int[4](1,0,2,1)[phase];
}
float raw(ivec2 p) { return texelFetch(mosaic,mirrored(p),0).r; }
vec4 a(ivec2 p) { return texelFetch(inputA,mirrored(p),0); }
vec4 b(ivec2 p) { return texelFetch(inputB,mirrored(p),0); }
vec4 cc(ivec2 p) { return texelFetch(inputC,mirrored(p),0); }
bool interior(ivec2 p,int margin) { return all(greaterThanEqual(p,ivec2(margin))) && all(lessThan(p,extent-margin)); }
vec3 restoreSample(ivec2 p,vec3 value) { value[cfa(p)]=raw(p); return value; }
vec4 evaluate(ivec2 p);
void main() {
    if(gl_LocalInvocationIndex==0) groupStatus=0;
    barrier();
    ivec2 p=ivec2(gl_GlobalInvocationID.xy)+ivec2(0,rowOffset);
    if(all(lessThan(p,extent))) {
        vec4 value=evaluate(p);
        if(any(isnan(value)) || any(isinf(value))) atomicOr(groupStatus,2u);
        imageStore(result,p,value);
    }
    barrier();
    if(gl_LocalInvocationIndex==0 && groupStatus!=0) atomicOr(status,groupStatus);
}
)GLSL";

inline constexpr const char* DirectionalGreen = R"GLSL(
vec4 evaluate(ivec2 p) {
    float c=raw(p), g=c;
    if(cfa(p)!=1) {
        float l=raw(p+ivec2(-1,0)),r=raw(p+ivec2(1,0));
        float u=raw(p+ivec2(0,-1)),d=raw(p+ivec2(0,1));
        float dh=2*c-raw(p+ivec2(-2,0))-raw(p+ivec2(2,0));
        float dv=2*c-raw(p+ivec2(0,-2))-raw(p+ivec2(0,2));
        float h=0.5*(l+r)+0.25*dh,v=0.5*(u+d)+0.25*dv;
        float gh=abs(l-r)+abs(dh),gv=abs(u-d)+abs(dv);
        if(mode==4) {
            float eps=1e-6*(1+abs(c)),wh=1/(eps+gh),wv=1/(eps+gv);
            g=(wh*h+wv*v)/(wh+wv);
        } else g=gh<gv?h:gv<gh?v:0.5*(h+v);
    }
    return vec4(g,0,0,1);
}
)GLSL";
inline constexpr const char* DirectionalColor = R"GLSL(
vec4 evaluate(ivec2 p) {
    float g=a(p).r;
    vec3 rgb=vec3(0,g,0);
    for(int target=0;target<=2;target+=2) {
        if(cfa(p)==target) { rgb[target]=raw(p); continue; }
        float sum=0,weights=0;
        for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
            ivec2 q=p+ivec2(x,y);
            if(cfa(q)!=target) continue;
            float ng=a(q).r,nc=raw(q),estimate=g+nc-ng;
            float w=mode==4?1/(1e-5+abs(g-ng)):1;
            if(mode==5 && g>0.001 && ng>0.001 && nc>=0) estimate=g*(nc/ng);
            sum+=w*estimate; weights+=w;
        }
        rgb[target]=sum/weights;
    }
    return vec4(restoreSample(p,rgb),1);
}
)GLSL";

inline constexpr const char* RefineChroma = R"GLSL(
vec4 evaluate(ivec2 p) {
    vec3 rgb=a(p).rgb;
    for(int c=0;c<=2;c+=2) {
        if(cfa(p)==c) { rgb[c]=raw(p); continue; }
        float values[9]; int k=0;
        for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
            vec3 v=a(p+ivec2(x,y)).rgb; values[k++]=v[c]-v.g;
        }
        for(int i=1;i<9;++i) { float v=values[i]; int j=i-1;
            while(j>=0 && values[j]>v) { values[j+1]=values[j]; --j; } values[j+1]=v;
        }
        rgb[c]=rgb.g+values[4];
    }
    return vec4(restoreSample(p,rgb),1);
}
)GLSL";

// Malvar, He and Cutler's linear 5x5 filters, also used inside combinations.
inline constexpr const char* Malvar = R"GLSL(
vec3 mhc(ivec2 p) {
    float c=raw(p),l=raw(p+ivec2(-1,0)),r=raw(p+ivec2(1,0));
    float u=raw(p+ivec2(0,-1)),d=raw(p+ivec2(0,1));
    float hh=raw(p+ivec2(-2,0))+raw(p+ivec2(2,0));
    float vv=raw(p+ivec2(0,-2))+raw(p+ivec2(0,2));
    float diag=raw(p+ivec2(-1,-1))+raw(p+ivec2(1,-1))+raw(p+ivec2(-1,1))+raw(p+ivec2(1,1));
    int color=cfa(p); vec3 rgb;
    if(color==1) {
        rgb.g=c;
        rgb[cfa(p+ivec2(1,0))]=(5*c+4*(l+r)-hh-diag+0.5*vv)/8;
        rgb[cfa(p+ivec2(0,1))]=(5*c+4*(u+d)-vv-diag+0.5*hh)/8;
    } else {
        rgb[color]=c;
        rgb.g=(4*c+2*(l+r+u+d)-hh-vv)/8;
        rgb[2-color]=(6*c+2*diag-1.5*(hh+vv))/8;
    }
    return rgb;
}
)GLSL";
inline constexpr const char* Combine = R"GLSL(
float detail(ivec2 p) {
    float energy=0,level=0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        ivec2 q=p+ivec2(x,y);
        energy+=abs(raw(q+ivec2(-2,0))-raw(q+ivec2(2,0)))+abs(raw(q+ivec2(0,-2))-raw(q+ivec2(0,2)));
        level+=abs(raw(q));
    }
    return smoothstep(0.025,0.20,energy/(2*level+0.02));
}
vec4 evaluate(ivec2 p) {
    vec3 smoothColor=mode==10?mhc(p):a(p).rgb;
    vec3 sharp=b(p).rgb,rgb;
    if(mode==14) rgb=vec3(sharp.g+smoothColor.r-smoothColor.g,sharp.g,sharp.g+smoothColor.b-smoothColor.g);
    else {
        float brightness=0.25*(smoothColor.r+2*smoothColor.g+smoothColor.b);
        float weight=mode==13?smoothstep(0.015,0.15,brightness):detail(p);
        if(mode==15) weight*=smoothstep(0.01,0.10,brightness);
        rgb=mix(smoothColor,sharp,weight);
        if(mode==15) rgb=mix(rgb,mhc(p),smoothstep(0.75,1.05,brightness));
    }
    return vec4(restoreSample(p,rgb),1);
}
)GLSL";
}
