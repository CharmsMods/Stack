#pragma once
// Shared RAW and graph Color Warp pass; mirrors Raw/ColorWarpMath and ApplyPreparedColorWarp.
namespace Stack::Renderer::ColorWarp {
inline constexpr const char* Fragment=R"GLSL(
#version 330 core
in vec2 vUV;
layout(location=0) out vec4 FragColor;
uniform sampler2D uInputTex;
uniform int uInputIsRec2020, uPinCount, uUseSpatialMasks, uBlendOverlaps;
uniform float uStrength;
uniform vec4 uPinParams[16], uPinAdjust[16], uPinFieldFlags[16], uPinMove[16];
uniform vec4 uPinSelection[16];
uniform vec4 uPopulation[96]; // Six joint chroma/EV centers per selection.
uniform vec4 uHueRange[16], uSaturationRange[16], uExposureRange[16];
uniform sampler2DArray uGateMask, uSupportMask, uBoundaryMask;
uniform sampler2D uMaskGuide;
uniform float uPinSpatialSupport[16];
float signedCbrt(float x) {
    return sign(x) * pow(abs(x), 1.0 / 3.0);
}

vec3 workingToXyz(vec3 rgb) {
    if (uInputIsRec2020 != 0) {
        return vec3(
            dot(rgb, vec3(0.6369580, 0.1446169, 0.1688809)),
            dot(rgb, vec3(0.2627002, 0.6779981, 0.0593017)),
            dot(rgb, vec3(0.0, 0.0280727, 1.0609851)));
    }
    return vec3(
        dot(rgb, vec3(0.4124564, 0.3575761, 0.1804375)),
        dot(rgb, vec3(0.2126729, 0.7151522, 0.0721750)),
        dot(rgb, vec3(0.0193339, 0.1191920, 0.9503041)));
}

vec3 xyzToWorking(vec3 xyz) {
    if (uInputIsRec2020 != 0) {
        return vec3(
            dot(xyz, vec3(1.7166512, -0.3556708, -0.2533663)),
            dot(xyz, vec3(-0.6666844, 1.6164812, 0.0157685)),
            dot(xyz, vec3(0.0176399, -0.0427706, 0.9421031)));
    }
    return vec3(
        dot(xyz, vec3(3.2404542, -1.5371385, -0.4985314)),
        dot(xyz, vec3(-0.9692660, 1.8760108, 0.0415560)),
        dot(xyz, vec3(0.0556434, -0.2040259, 1.0572252)));
}

vec3 xyzToOklab(vec3 xyz) {
    vec3 lms = vec3(
        dot(xyz, vec3(0.8190224380, 0.3619062601, -0.1288737815)),
        dot(xyz, vec3(0.0329836539, 0.9292868616, 0.0361446664)),
        dot(xyz, vec3(0.0481771894, 0.2642395318, 0.6335478285)));
    lms = vec3(signedCbrt(lms.x), signedCbrt(lms.y), signedCbrt(lms.z));
    return vec3(
        dot(lms, vec3(0.2104542553, 0.7936177850, -0.0040720468)),
        dot(lms, vec3(1.9779984951, -2.4285922050, 0.4505937099)),
        dot(lms, vec3(0.0259040371, 0.7827717662, -0.8086757660)));
}

vec3 oklabToXyz(vec3 lab) {
    vec3 roots = vec3(
        lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z,
        lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z,
        lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z);
    vec3 lms = roots * roots * roots;
    return vec3(
        dot(lms, vec3(1.2268798734, -0.5578149966, 0.2813910502)),
        dot(lms, vec3(-0.0405757626, 1.1122868294, -0.0717110667)),
        dot(lms, vec3(-0.0763729497, -0.4214933240, 1.5869240244)));
}


float rangeWeight(vec4 r,float x) {
    if(x<r.y) return r.y>r.x ? smoothstep(r.x,r.y,x):0.0;
    if(x>r.z) return r.w>r.z ? 1.0-smoothstep(r.z,r.w,x):0.0;
    return 1.0;
}
float hueDelta(float h) { return h-360.0*floor((h+180.0)/360.0); }
void main() {
    vec4 inputColor=texture(uInputTex,vUV);
    vec3 xyz=workingToXyz(inputColor.rgb),lab=xyzToOklab(xyz);
    if(inputColor.a<=0.0 || lab.x<=0.0) {FragColor=inputColor;return;}
    vec2 normalized=lab.yz/max(abs(lab.x),0.000001);
    float chroma=length(normalized);
    float hue=chroma>0.000001 ? degrees(atan(normalized.y,normalized.x)):0.0;
    float neutral=smoothstep(0.02,0.10,chroma);
    float ev=log2(max(xyz.y,0.000001)/0.18);
    vec3 adjustment=vec3(0.0);vec2 movement=vec2(0.0);
    float total=0.0,protection=0.0;
    for(int index=0;index<16;++index) {
        if(index>=uPinCount)break;
        vec4 p=uPinParams[index];
        if(p.y<=0.0 || p.w<0.5)continue;
        float h=hueDelta(hue-p.x);
        float colorWeight=neutral*max(rangeWeight(uHueRange[index],h),
            max(rangeWeight(uHueRange[index],h-360.0),rangeWeight(uHueRange[index],h+360.0)))*rangeWeight(uSaturationRange[index],chroma);
        if(uPinSelection[index].z>0.0) {
            float x=length(normalized-uPinSelection[index].xy)/uPinSelection[index].z;
            colorWeight=(1.0-smoothstep(0.25,1.0,x))*max(rangeWeight(uHueRange[index],h),
                max(rangeWeight(uHueRange[index],h-360.0),rangeWeight(uHueRange[index],h+360.0)))*rangeWeight(uSaturationRange[index],chroma);
        }
        if(uPinSelection[index].w>0.0) {
            float joint=0.0;
            for(int j=0;j<6;++j) {
                if(float(j)>=uPinSelection[index].w)break;
                vec4 point=uPopulation[index*6+j];
                float dc=length(normalized-point.xy)/(uPinSelection[index].z>0.0?uPinSelection[index].z:max(.04,length(point.xy)*.4));
                float de=(ev-point.z)/max(.1,uPinFieldFlags[index].z);
                joint=max(joint,1.0-smoothstep(.25,1.0,length(vec2(dc,de))));
            }
            colorWeight=joint*max(rangeWeight(uHueRange[index],h),
                max(rangeWeight(uHueRange[index],h-360.0),rangeWeight(uHueRange[index],h+360.0)))*rangeWeight(uSaturationRange[index],chroma);
        }
        colorWeight*=smoothstep(-16.0,-12.0,ev);
        float direct=colorWeight*rangeWeight(uExposureRange[index],ev);
        float gate=1.0,support=0.0,boundary=1.0;
        if(uUseSpatialMasks!=0 && uPinAdjust[index].w>0.5) {
            vec3 maskUv=vec3(vUV,uPinAdjust[index].w-1.0);
            vec3 guide=texture(uMaskGuide,vUV).rgb;
            float guided=clamp(exp(-(length(lab.yz-guide.xy)*36.0+abs(ev-guide.z)*0.28)),0.04,1.0);
            guided=uPinFieldFlags[index].y>0.5?guided:1.0;
            gate=texture(uGateMask,maskUv).r*guided;
            support=texture(uSupportMask,maskUv).r*guided;
            if(uPinFieldFlags[index].x>0.5)direct=smoothstep(-16.0,-12.0,ev);
            boundary=texture(uBoundaryMask,maskUv).r;
        }
        float weight=min(1.0,direct*max(gate,support*uPinSpatialSupport[index]))*boundary*p.y;
        if(p.z>0.5) { protection=max(protection,clamp(weight,0.0,1.0));continue; }
        total+=weight;adjustment+=uPinAdjust[index].xyz*weight;movement+=uPinMove[index].xy*weight;
    }
    adjustment*=uStrength*(1.0-protection)/(uBlendOverlaps!=0?max(1.0,total):1.0);
    movement*=uStrength*(1.0-protection)/(uBlendOverlaps!=0?max(1.0,total):1.0);
    if(all(equal(adjustment,vec3(0.0))) && all(equal(movement,vec2(0.0)))) { FragColor=inputColor;return; }
    vec3 rgb=inputColor.rgb;
    if(adjustment.x!=0.0 || adjustment.y!=0.0 || any(notEqual(movement,vec2(0.0)))) {
        float angle=radians(adjustment.x),c=cos(angle),s=sin(angle);
        lab.yz=vec2(c*lab.y-s*lab.z,s*lab.y+c*lab.z)*max(0.0,1.0+adjustment.y);
        lab.yz+=movement*max(abs(lab.x),0.000001);
        rgb=xyzToWorking(oklabToXyz(lab));
    }
    float lightness=max(0.0,lab.x);
    float ratio=1.0+clamp(adjustment.z,-1.0,1.0)/(1.0+lightness);
    float gain=lightness>0.0 ? ratio*ratio*ratio : 1.0;
    FragColor=vec4(rgb*gain,inputColor.a);
}
)GLSL";
}
