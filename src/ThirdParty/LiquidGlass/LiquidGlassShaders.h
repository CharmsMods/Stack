// SPDX-License-Identifier: GPL-3.0-only AND MIT
// Upstream portions retain MIT; Stack modifications are GPL-3.0-only.
// Adapted from OverShifted/LiquidGlass, commit
// 3797fa541c1f026c521a75885ee271f03bdf9f0f.
// Copyright (c) 2026 Sepehr Kalanaki.
// Gaussian blur kernel: Copyright (c) 2015 Jam3, glsl-fast-gaussian-blur (MIT).
// Full notices: legal/third_party/LiquidGlass-MIT.txt,
// legal/third_party/GLSL-Fast-Gaussian-Blur-MIT.txt and LiquidGlass-NOTICE.txt.
// Stack modifications: GLSL4.3 bindings, bounded texture crops and a pixel-space
// rounded-object optics, coverage and directional environment lighting.
#pragma once
namespace Stack::ThirdParty::LiquidGlass {
inline constexpr const char* Blur = R"(#version 430 core
in vec2 uv; out vec4 color;
layout(binding=0) uniform sampler2D source;
layout(location=0) uniform vec2 stepUv;
layout(location=1) uniform vec2 extent;
vec4 sampleValid(sampler2D image, vec2 p) {
    vec2 halfTexel=.5/vec2(textureSize(image,0));
    return texture(image,clamp(p,halfTexel,extent-halfTexel));
}
vec4 blur13(sampler2D image, vec2 uv, vec2 resolution, vec2 direction) {
	vec4 color = vec4(0.0);
	vec2 off1 = vec2(1.411764705882353) * direction;
	vec2 off2 = vec2(3.2941176470588234) * direction;
	vec2 off3 = vec2(5.176470588235294) * direction;
	color += sampleValid(image, uv) * 0.1964825501511404;
	color += sampleValid(image, uv + (off1 / resolution)) * 0.2969069646728344;
	color += sampleValid(image, uv - (off1 / resolution)) * 0.2969069646728344;
	color += sampleValid(image, uv + (off2 / resolution)) * 0.09447039785044732;
	color += sampleValid(image, uv - (off2 / resolution)) * 0.09447039785044732;
	color += sampleValid(image, uv + (off3 / resolution)) * 0.010381362401148057;
	color += sampleValid(image, uv - (off3 / resolution)) * 0.010381362401148057;
	return color;
}


void main() {
    // stepUv is upstream direction * radius / resolution.
    color=blur13(source,uv*extent,vec2(1.0),stepUv);
})";
// Original Stack optics: exact rounded-rectangle swept-ellipsoid
// volume, two dielectric interfaces and a finite reflected rectangular emitter.
// This is not Apple's implementation. The small elevation shadow is the
// existing stylized screen-space approximation, not a traced area-light shadow.
inline constexpr const char* Material = R"(#version 430 core
in vec2 uv; out vec4 color;
layout(binding=0) uniform sampler2D softScene;
layout(binding=1) uniform sampler2D sharpScene;
layout(location=0) uniform vec2 sizePx;
layout(location=2) uniform float radiusPx;
layout(location=3) uniform vec4 tint;
layout(location=5) uniform vec2 softExtent;
layout(location=6) uniform vec2 sceneOrigin;
layout(location=7) uniform vec2 sceneScale;
layout(location=8) uniform vec2 sharpExtent;
layout(location=9) uniform vec4 drawMapping;
vec3 sampleSoft(vec2 p) {
    vec2 halfTexel=.5/vec2(textureSize(softScene,0));
    return texture(softScene,clamp(p*softExtent,halfTexel,softExtent-halfTexel)).rgb;
}
vec3 sampleSharp(vec2 p) {
    vec2 halfTexel=.5/vec2(textureSize(sharpScene,0));
    return texture(sharpScene,clamp(p*sharpExtent,halfTexel,sharpExtent-halfTexel)).rgb;
}
vec3 toLinear(vec3 v) {
    return mix(v/12.92,pow((v+.055)/1.055,vec3(2.4)),step(vec3(.04045),v));
}
vec3 toDisplay(vec3 v) {
    v=max(v,vec3(0.0));
    return mix(v*12.92,1.055*pow(v,vec3(1.0/2.4))-.055,step(vec3(.0031308),v));
}
float rand(vec2 co) { return fract(sin(dot(co,vec2(12.9898,78.233)))*43758.5453); }
float objectDistance(vec2 point,float r) {
    vec2 q=abs(point-.5*sizePx)-.5*sizePx+r;
    return length(max(q,0.0))+min(max(q.x,q.y),0.0)-r;
}
float farRoot(vec3 origin,vec3 direction) {
    float a=dot(direction,direction);
    if (a<1e-14) return -1.0; // Parallel to this infinite patch's generator.
    float b=dot(origin,direction);
    float c=dot(origin,origin)-1.0;
    float disc=b*b-a*c;
    float tolerance=1e-6*max(b*b+abs(a*c),1e-12);
    if (disc < -tolerance) return -1.0;
    float root=sqrt(max(0.0,disc));
    // Stable quadratic roots; the greater root has outward-facing derivative.
    float q=-b-(b>=0.0 ? root : -root);
    if (abs(q)<1e-12) return -b/a;
    return max(q/a,c/q);
}
vec3 volumeNormal(vec3 p,vec2 core,float r,float h) {
    vec2 radial=p.xy-clamp(p.xy,-core,core);
    // Inverse-transpose of the ellipsoid scaling, not a sphere normal.
    return normalize(vec3(radial/(r*r),p.z/(h*h)));
}
bool inRegion(float coordinate,float extent,int region,float tolerance) {
    return region==0 ? abs(coordinate)<=extent+tolerance
        : float(region)*coordinate>=extent-tolerance;
}
void offerHit(float t,vec3 origin,vec3 direction,vec2 core,float r,float h,
    int regionX,int regionY,inout float best,inout vec3 normal) {
    float tolerance=1e-4*max(r,h);
    if (t<=tolerance || t>=best || isnan(t) || isinf(t)) return;
    vec3 p=origin+t*direction;
    if (!inRegion(p.x,core.x,regionX,tolerance) ||
        !inRegion(p.y,core.y,regionY,tolerance)) return;
    vec3 n=volumeNormal(p,core,r,h);
    if (dot(direction,n)<=1e-7) return;
    best=t; normal=n;
}
bool exitVolume(vec3 origin,vec3 direction,vec2 core,float r,float h,
    out vec3 point,out vec3 normal,out float lengthInside) {
    float best=1e20;
    normal=vec3(0.0,0.0,-1.0);
    // Eight exact patches: four elliptical cylinders and four ellipsoids.
    // A degenerate core extent naturally produces the capsule/sphere cases.
    for (int x=-1;x<=1;++x) for (int y=-1;y<=1;++y) {
        if (x==0 && y==0) continue;
        vec3 o=vec3(x==0 ? 0.0 : (origin.x-float(x)*core.x)/r,
                    y==0 ? 0.0 : (origin.y-float(y)*core.y)/r,origin.z/h);
        vec3 d=vec3(x==0 ? 0.0 : direction.x/r,
                    y==0 ? 0.0 : direction.y/r,direction.z/h);
        offerHit(farRoot(o,d),origin,direction,core,r,h,x,y,best,normal);
    }
    // Both flat caps close the volume; the positive-facing candidate is rejected
    // naturally when it is behind the refracted downward ray.
    if (abs(direction.z)>1e-8) {
        offerHit(( h-origin.z)/direction.z,origin,direction,core,r,h,0,0,best,normal);
        offerHit((-h-origin.z)/direction.z,origin,direction,core,r,h,0,0,best,normal);
    }
    lengthInside=best;
    point=origin+min(best,1e6)*direction;
    return best<1e19;
}
float fresnel(float cosine,float incidentIndex,float transmittedIndex) {
    cosine=clamp(cosine,0.0,1.0);
    float eta=incidentIndex/transmittedIndex;
    float sin2=eta*eta*max(0.0,1.0-cosine*cosine);
    if (sin2>=1.0) return 1.0;
    float transmittedCosine=sqrt(max(0.0,1.0-sin2));
    float rs=(incidentIndex*cosine-transmittedIndex*transmittedCosine)/
        max(incidentIndex*cosine+transmittedIndex*transmittedCosine,1e-8);
    float rp=(transmittedIndex*cosine-incidentIndex*transmittedCosine)/
        max(transmittedIndex*cosine+incidentIndex*transmittedCosine,1e-8);
    return .5*(rs*rs+rp*rp);
}
vec3 environment(vec3 point,vec3 direction,vec3 ambient,vec2 capturePixels) {
    // One shared finite overhead emitter in capture/world coordinates, not a
    // stripe painted in object UV. Its footprint follows the reflected ray.
    float unit=max(capturePixels.y,64.0);
    float lightZ=.9*unit;
    vec2 lightCenter=vec2(-.35,2.4)*unit;
    vec2 lightHalfSize=vec2(.65,.45)*unit;
    float lamp=0.0;
    if (direction.z>1e-5) {
        float t=(lightZ-point.z)/direction.z;
        vec2 p=point.xy+t*direction.xy;
        vec2 outside=abs(p-lightCenter)-lightHalfSize;
        float d=max(outside.x,outside.y);
        // Bounded angular roughness approximation. No screen derivatives inside
        // direction/TIR branches, where neighboring rays may be divergent.
        float softness=.015*unit;
        lamp=t>0.0 ? 1.0-smoothstep(-softness,softness,d) : 0.0;
    }
    // A low-frequency local-scene environment is an explicit screen-space
    // approximation. Only the rectangle is the modeled overhead light source.
    return ambient*.94+vec3(.006)+vec3(2.5,2.45,2.35)*lamp;
}
void main() {
    vec2 point=uv*drawMapping.xy-drawMapping.zw;
    float r=max(.01,min(radiusPx,.5*min(sizePx.x,sizePx.y)));
    float silhouette=objectDistance(point,r);
    float coverage=1.0-smoothstep(-.5,.5,silhouette);
    // Unchanged bounded stylized shadow, for an isolated material comparison.
    // The whole-curved icon comparison changes the footprint, not shadow
    // strength/spread: retain the prior 12/38 radius-based sigma and offset.
    float shadowRadius=abs(sizePx.x-sizePx.y)<.001*min(sizePx.x,sizePx.y)
        ? min(r,(12.0/38.0)*min(sizePx.x,sizePx.y)) : r;
    float sigma=max(.75,shadowRadius*.2);
    float shadowOffset=max(.75,shadowRadius*.22);
    float shadowDistance=max(0.0,objectDistance(point+vec2(0.0,shadowOffset),r));
    float shadow=.10*exp(-.5*shadowDistance*shadowDistance/(sigma*sigma));
    shadow*=1.0-smoothstep(2.5*sigma,3.0*sigma,shadowDistance);
    if (coverage<=0.0) { color=vec4(vec3(0.0),shadow); return; }

    float h=max(.25,.32*min(sizePx.x,sizePx.y));
    float gap=.05*min(sizePx.x,sizePx.y);
    float centerZ=h+gap;
    vec2 core=max(.5*sizePx-vec2(r),vec2(0.0));
    vec2 local=point-.5*sizePx;
    vec2 closest=clamp(local,-core,core);
    vec2 radial=local-closest;
    // Clamp only the half-pixel AA fringe to its true geometric entry point.
    radial/=max(1.0,length(radial)/r);
    local=closest+radial;
    float cap=sqrt(max(0.0,1.0-dot(radial,radial)/(r*r)));
    vec3 entry=vec3(local,h*cap);
    vec3 entryNormal=volumeNormal(entry,core,r,h);
    vec3 incoming=vec3(0.0,0.0,-1.0);
    const float ior=1.45;
    vec3 inside=refract(incoming,entryNormal,1.0/ior);
    float entryFresnel=fresnel(-dot(incoming,entryNormal),1.0,ior);
    vec2 capturePixels=sharpExtent*vec2(textureSize(sharpScene,0));
    vec2 objectCenter=sceneOrigin*capturePixels+.5*sizePx;
    vec3 entryWorld=vec3(objectCenter+entry.xy,centerZ+entry.z);
    vec3 ambient=toLinear(sampleSoft(sceneOrigin+point/sizePx*sceneScale));
    vec3 reflection=environment(entryWorld,reflect(incoming,entryNormal),ambient,capturePixels);
    vec3 exitPoint,exitNormal;
    float pathLength=0.0;
    bool found=exitVolume(entry,inside,core,r,h,exitPoint,exitNormal,pathLength);
    vec3 transmitted=reflection;
    float transmission=0.0;
    bool internallyReflected=false;
    if (found) {
        float exitFresnel=fresnel(dot(inside,exitNormal),ior,1.0);
        // Outward normal is negated at the glass-to-air interface.
        vec3 outgoing=refract(inside,-exitNormal,ior);
        // A single transmitted ray is the deliberate budget. All untraced
        // exit-reflection energy uses the same environment on BOTH sides of
        // the critical angle. No new high-energy bounce appears only at TIR.
        internallyReflected=dot(outgoing,outgoing)<1e-10;
        if (!internallyReflected) {
            transmission=(1.0-entryFresnel)*(1.0-exitFresnel);
            vec3 exitWorld=vec3(objectCenter+exitPoint.xy,centerZ+exitPoint.z);
            transmitted=environment(exitWorld,outgoing,ambient,capturePixels);
            if (outgoing.z < -1e-5) {
                float t=-exitWorld.z/outgoing.z;
                vec2 scenePoint=exitWorld.xy+t*outgoing.xy;
                vec2 margin=min(scenePoint,capturePixels-scenePoint);
                float available=smoothstep(0.0,1.5,min(margin.x,margin.y));
                // No uninitialized capacity samples or long clamped streaks:
                // rays outside the captured plane fall back to environment.
                if (available>0.0) transmitted=mix(transmitted,
                    toLinear(sampleSharp(scenePoint/capturePixels)),available);
            }
            float absorption=.006*clamp(tint.a,0.0,1.0)*min(pathLength/(2.0*h),2.0);
            transmitted*=mix(vec3(1.0),clamp(tint.rgb,vec3(0.0),vec3(1.0)),absorption);
        }
    }
    vec3 material=toDisplay(mix(reflection,transmitted,transmission));

    // Preserve the upstream static, sub-code-value dithering convention.
    material+=vec3((rand(gl_FragCoord.xy)-.5)/255.0);
    float alpha=coverage+(1.0-coverage)*shadow;
    color=vec4(material*coverage/max(alpha,.0001),alpha);
})";
}
