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
// Output rectangle size and the glass origin within it, in framebuffer pixels.
layout(location=9) uniform vec4 drawMapping;
vec4 sampleValid(vec2 p) {
    vec2 halfTexel=.5/vec2(textureSize(softScene,0));
    return texture(softScene,clamp(p*softExtent,halfTexel,softExtent-halfTexel));
}
vec3 sampleSharp(vec2 p) {
    vec2 halfTexel=.5/vec2(textureSize(sharpScene,0));
    return texture(sharpScene,clamp(p*sharpExtent,halfTexel,sharpExtent-halfTexel)).rgb;
}
float rand(vec2 co) {
    return fract(sin(dot(co,vec2(12.9898,78.233)))*43758.5453);
}
float objectDistance(vec2 point, float r) {
    vec2 q=abs(point-.5*sizePx)-.5*sizePx+r;
    return length(max(q,0.0))+min(max(q.x,q.y),0.0)-r;
}
void main() {
    vec2 point=uv*drawMapping.xy-drawMapping.zw;
    vec2 objectUv=point/sizePx;
    // One symmetric, four-corner rounded rectangle per object. Its uncut
    // silhouette is independent of the output clip and the invisible column.
    float r=min(radiusPx,.5*min(sizePx.x,sizePx.y));
    float silhouette=objectDistance(point,r);
    float coverage=1.0-smoothstep(-.5,.5,silhouette);
    float sigma=max(.75,r*.2);
    float shadowOffset=max(.75,r*.22);
    float shadowDistance=max(0.0,objectDistance(point+vec2(0.0,shadowOffset),r));
    float shadow=.10*exp(-.5*shadowDistance*shadowDistance/(sigma*sigma));
    shadow*=1.0-smoothstep(2.5*sigma,3.0*sigma,shadowDistance);
    if (coverage<=0.0) { color=vec4(vec3(0.0),shadow); return; }

    float bevel=max(.5*min(sizePx.x,sizePx.y),.75);
    vec2 centered=point-.5*sizePx;
    // C1 absolute coordinates prevent the symmetric dome from acquiring a
    // center-axis crease. Outside this small interior band they are exact;
    // coverage and the shadow always use the exact rounded-rectangle SDF.
    vec2 absolute=abs(centered);
    float axisWidth=max(.001,bevel*.15);
    vec2 axisGradient=sign(centered)*min(absolute/axisWidth,vec2(1.0));
    vec2 smoothAbsolute=mix(.5*(absolute*absolute/axisWidth+axisWidth),absolute,
        step(vec2(axisWidth),absolute));
    vec2 q=smoothAbsolute-.5*sizePx+r;
    vec2 corner=max(q,0.0);
    float cornerLength=length(corner);
    float distance=cornerLength+min(max(q.x,q.y),0.0)-r;
    vec2 gradient=cornerLength>0.0 ? corner/cornerLength
        : (q.x>q.y ? vec2(1.0,0.0) : vec2(0.0,1.0));
    // Smooth the nearest-edge junction inside the solid only. This broad
    // bead has a continuous optical normal instead of diagonal edge seams.
    if (max(q.x,q.y)<-.0001) {
        float sum=q.x+q.y;
        float smoothing=-2.0*q.x*q.y/sum;
        vec2 smoothingGradient=-2.0*vec2(q.y*q.y,q.x*q.x)/(sum*sum);
        if (smoothing>bevel*.3) {
            float t=clamp((smoothing-bevel*.3)/(bevel*.2),0.0,1.0);
            smoothing=bevel*(.3+.2*(t-.5*t*t));
            smoothingGradient*=1.0-t;
        }
        float blend=clamp(.5+.5*(q.x-q.y)/smoothing,0.0,1.0);
        distance=mix(q.y,q.x,blend)+smoothing*blend*(1.0-blend)-r;
        gradient=vec2(blend,1.0-blend)+smoothingGradient*blend*(1.0-blend);
    }
    gradient*=axisGradient;
    vec2 edgeNormal=gradient*inversesqrt(max(dot(gradient,gradient),.000001));

    // Rounded convex cross-section with a soft squircle shoulder. A rounder
    // front spreads the lighting across the bevel instead of drawing a hairline.
    // The half-minimum object extent sets its curvature, not the column
    // width, so every button reads as a separate raised bead.
    float depth=max(0.0,-distance);
    float shoulder=clamp(1.0-depth/bevel,0.0,.9999);
    float shoulder3=shoulder*shoulder*shoulder;
    float roundHeight=sqrt(max(1.0-shoulder*shoulder,.0001));
    float softHeight=sqrt(sqrt(max(1.0-shoulder3*shoulder,.0001)));
    float height=mix(roundHeight,softHeight,.25);
    float slope=.9*(.75*shoulder/max(roundHeight,.001)
        +.25*shoulder3/max(softHeight*softHeight*softHeight,.001));
    vec3 normal=normalize(vec3(gradient*slope,1.0));
    vec3 ray=refract(vec3(0.0,0.0,-1.0),normal,1.0/1.45);
    vec2 offset=ray.xy/max(-ray.z,.1)*bevel*(.35+.65*height);
    vec2 coord=sceneOrigin+(objectUv+offset/sizePx)*sceneScale;
    vec3 transmitted=mix(sampleSharp(coord),sampleValid(coord).rgb,.14);

    // Nearby scene color and a quiet directional environment give even a plain
    // backdrop a curved glass surface. This is a screen-space approximation,
    // not a ray-traced room reflection or a constant outline around the object.
    // Stay inside the renderer's existing 16-logical-pixel capture neighborhood.
    float environmentReach=min(bevel*.3,r*.85);
    vec2 outside=objectUv+edgeNormal*(depth+environmentReach)/sizePx;
    vec3 environment=sampleValid(sceneOrigin+outside*sceneScale).rgb;
    vec3 reflected=reflect(vec3(0.0,0.0,-1.0),normal);
    float environmentLight=.72+.26*smoothstep(-.55,.7,reflected.z);
    environment=mix(environment,vec3(.14,.16,.18),.12)*environmentLight;
    float grazing=1.0-normal.z;
    float grazing2=grazing*grazing;
    float fresnel=.035+.965*grazing2*grazing2*grazing;
    vec3 material=mix(transmitted,environment,.36*(fresnel-.035)+.08*grazing2);
    // A restrained face-light gradient makes the convex body legible even
    // over a plain scene, without a flat wash or fixed opaque perimeter.
    material*=1.0+.065*normal.y;
    material=mix(material,tint.rgb,.025*clamp(tint.a,0.0,1.0));
    // Framebuffer UV and this normal use positive Y upward, unlike ImGui.
    // Keep the key light above the material rather than lighting its underside.
    vec3 halfLight=normalize(normalize(vec3(-.25,.75,.65))+vec3(0.0,0.0,1.0));
    float light=max(dot(normal,halfLight),0.0);
    float light2=light*light, light4=light2*light2, light8=light4*light4;
    float glint=(.20*light8+.36*light8*light8*light8*light8)*grazing;
    // A broad overhead source catches the upper shoulder. Keep its crest
    // directional rather than drawing a constant white perimeter stroke.
    float upper=max(normal.y,0.0);
    float upper2=upper*upper;
    float upperReflection=.11*upper2*upper2*(1.0-grazing);
    // A distant broad light reflected at a grazing angle produces a curved
    // edge crest, not a painted perimeter. The lower arc catches more of this
    // environment than the top; the key light still illuminates the upper face.
    float reflectedBand=1.0-smoothstep(.08,.28,abs(reflected.z+.88));
    float edgeReflection=reflectedBand*grazing*(.035+.10*max(normal.x,0.0)
        +.14*max(-normal.y,0.0));
    material+=vec3(.94,.97,1.0)*(glint+upperReflection+edgeReflection);
    // Keep upstream's static dither at sub-code-value strength, not visible frost.
    material+=vec3((rand(gl_FragCoord.xy)-.5)/255.0);
    float alpha=coverage+(1.0-coverage)*shadow;
    color=vec4(material*coverage/max(alpha,.0001),alpha);
})";
}
