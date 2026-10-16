#pragma once
// SPDX-License-Identifier: CDDL-1.0
// Float GPU adaptations of LibRaw 0.22.1 PPG, VNG and AHD.
// Copyright 2019-2025 LibRaw LLC. dcraw copyright 1997-2018 Dave Coffin.
// PPG by Alain Desbiolles. AHD based on Hirakawa, Parks and Paul Lee.
// Distributed under LibRaw's CDDL option. Full notice and source distribution
// details: legal/third_party/LibRaw-Demosaic-NOTICE.txt.
// Stack changes: separate GPU passes, mirrored boundaries, explicit camera
// transform, float arithmetic, preserved signed/HDR values and recorded samples.
namespace Raw::Demosaic::Shaders {
inline constexpr const char* ClassicBilinear = R"GLSL(
int color(ivec2 p) {int c=cfa(p);return mode==1 && c==1 && (p.y&1)!=0?3:c;}
vec4 evaluate(ivec2 p) {
    vec4 rgb=vec4(0);
    for(int c=0;c<(mode==1?4:3);++c) {
        if(color(p)==c) {rgb[c]=raw(p);continue;}
        float sum=0,weights=0;
        for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
            if(color(p+ivec2(x,y))!=c) continue;
            float w=x==0 || y==0?2:1;sum+=w*raw(p+ivec2(x,y));weights+=w;
        }
        rgb[c]=sum/weights;
    }
    return rgb;
}
)GLSL";
inline constexpr const char* PpgGreen = R"GLSL(
vec4 evaluate(ivec2 p) {
    float g=raw(p);
    if(cfa(p)!=1) {
        precise vec2 guess,diff;
        for(int d=0;d<2;++d) {
            ivec2 s=d==0?ivec2(1,0):ivec2(0,1);
            guess[d]=((raw(p-s)+raw(p)+raw(p+s))*2-raw(p-2*s)-raw(p+2*s))/4;
            diff[d]=3*(abs(raw(p-2*s)-raw(p))+abs(raw(p+2*s)-raw(p))+abs(raw(p-s)-raw(p+s)))+
                2*(abs(raw(p+3*s)-raw(p+s))+abs(raw(p-3*s)-raw(p-s)));
        }
        int d=diff.x>diff.y?1:0;ivec2 s=d==0?ivec2(1,0):ivec2(0,1);
        g=clamp(guess[d],min(raw(p-s),raw(p+s)),max(raw(p-s),raw(p+s)));
    }
    return vec4(g,0,0,1);
}
)GLSL";
inline constexpr const char* PpgColor = R"GLSL(
vec4 evaluate(ivec2 p) {
    float g=a(p).r;vec3 rgb=vec3(g);int known=cfa(p);rgb[known]=raw(p);
    if(known==1) {
        for(int d=0;d<2;++d) {
            ivec2 s=d==0?ivec2(1,0):ivec2(0,1);int c=cfa(p+s);
            rgb[c]=(raw(p-s)+raw(p+s)+2*g-a(p-s).r-a(p+s).r)/2;
        }
    } else {
        precise vec2 guess,diff;
        for(int d=0;d<2;++d) {
            ivec2 s=d==0?ivec2(1,1):ivec2(-1,1);
            diff[d]=abs(raw(p-s)-raw(p+s))+abs(a(p-s).r-g)+abs(a(p+s).r-g);
            guess[d]=(raw(p-s)+raw(p+s)+2*g-a(p-s).r-a(p+s).r)/2;
        }
        rgb[2-known]=mode==1 || diff.x==diff.y?(guess.x+guess.y)/2:guess[diff.x>diff.y?1:0];
    }
    return vec4(restoreSample(p,rgb),1);
}
)GLSL";
// The 64 gradient terms are prepended from ClassicDemosaicTerms.h.
inline constexpr const char* Vng = R"GLSL(
int color(ivec2 p) {int c=cfa(p);return mode==1 && c==1 && (p.y&1)!=0?3:c;}
vec4 evaluate(ivec2 p) {
    vec4 rgb=a(p);
    if(interior(p,2)) {
        precise float gradients[8];for(int g=0;g<8;++g) gradients[g]=0;
        for(int t=0;t<64;++t) {
            ivec2 p1=p+ivec2(terms[t*6+1],terms[t*6]),p2=p+ivec2(terms[t*6+3],terms[t*6+2]);
            int c=color(p1);if(color(p2)!=c) continue;
            int diag=color(p+ivec2(1,0))==c && color(p+ivec2(0,1))==c?2:1;
            if(abs(p1.x-p2.x)==diag && abs(p1.y-p2.y)==diag) continue;
            float diff=abs(a(p1)[c]-a(p2)[c])*float(1<<terms[t*6+4]);
            for(int g=0;g<8;++g) if((terms[t*6+5]&(1<<g))!=0) gradients[g]+=diff;
        }
        float low=gradients[0],high=low;
        for(int g=1;g<8;++g) {low=min(low,gradients[g]);high=max(high,gradients[g]);}
        if(high>0) {
            const ivec2 directions[8]=ivec2[8](ivec2(-1,-1),ivec2(0,-1),ivec2(1,-1),ivec2(1,0),
                ivec2(1,1),ivec2(0,1),ivec2(-1,1),ivec2(-1,0));
            precise vec4 sum=vec4(0);float n=0;int known=color(p);
            for(int d=0;d<8;++d) if(gradients[d]<=low+0.5*high) {
                ivec2 s=directions[d];vec4 value=a(p+s);
                if(color(p+s)!=known && color(p+2*s)==known) value[known]=(a(p)[known]+a(p+2*s)[known])/2;
                sum+=value;++n;
            }
            rgb=vec4(raw(p))+(sum-vec4(sum[known]))/n;
        }
    }
    if(mode==1) rgb.g=(rgb.g+rgb.a)/2;
    return vec4(restoreSample(p,rgb.rgb),1);
}
)GLSL";
inline constexpr const char* AhdGreen = R"GLSL(
vec4 evaluate(ivec2 p) {
    float g=raw(p);
    if(cfa(p)!=1) {
        ivec2 s=mode==0?ivec2(1,0):ivec2(0,1);
        g=((raw(p-s)+raw(p)+raw(p+s))*2-raw(p-2*s)-raw(p+2*s))/4;
        g=clamp(g,min(raw(p-s),raw(p+s)),max(raw(p-s),raw(p+s)));
    }
    return vec4(g,0,0,1);
}
)GLSL";
inline constexpr const char* AhdLab = R"GLSL(
uniform mat3 cameraToSrgb;
float labCurve(float v) {return v>0.008856?pow(v,1.0/3.0):7.787*v+16.0/116.0;}
vec4 evaluate(ivec2 p) {
    precise vec3 rgb=cameraToSrgb*a(p).rgb;
    precise vec3 xyz=vec3((0.4124564*rgb.r+0.3575761*rgb.g+0.1804375*rgb.b)/0.95047,
        0.2126729*rgb.r+0.7151522*rgb.g+0.0721750*rgb.b,(0.0193339*rgb.r+0.1191920*rgb.g+0.9503041*rgb.b)/1.08883);
    vec3 f=vec3(labCurve(xyz.x),labCurve(xyz.y),labCurve(xyz.z));
    precise vec3 lab=vec3(116*f.y-16,500*(f.x-f.y),200*(f.y-f.z));
    // AHD's reference homogeneity map uses 1/64 Lab units. Keep that decision
    // precision without its 16-bit range limit or any quantization of RGB.
    return vec4(trunc(lab*64)/64,1);
}
)GLSL";
inline constexpr const char* AhdHomogeneity = R"GLSL(
vec4 evaluate(ivec2 p) {
    const ivec2 offsets[4]=ivec2[4](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1));
    float l[8],chroma[8];
    for(int d=0;d<2;++d) for(int n=0;n<4;++n) {
        vec3 delta=d==0?a(p).rgb-a(p+offsets[n]).rgb:b(p).rgb-b(p+offsets[n]).rgb;
        l[4*d+n]=abs(delta.x);chroma[4*d+n]=dot(delta.yz,delta.yz);
    }
    float leps=min(max(l[0],l[1]),max(l[6],l[7]));
    float ceps=min(max(chroma[0],chroma[1]),max(chroma[6],chroma[7]));
    vec2 count=vec2(0);
    for(int d=0;d<2;++d) for(int n=0;n<4;++n) if(l[4*d+n]<=leps && chroma[4*d+n]<=ceps) ++count[d];
    return vec4(count,0,1);
}
)GLSL";
inline constexpr const char* AhdSelect = R"GLSL(
vec4 evaluate(ivec2 p) {
    vec2 counts=vec2(0);
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) counts+=cc(p+ivec2(x,y)).rg;
    vec3 rgb=counts.x>counts.y?a(p).rgb:counts.y>counts.x?b(p).rgb:0.5*(a(p).rgb+b(p).rgb);
    return vec4(restoreSample(p,rgb),1);
}
)GLSL";
}
