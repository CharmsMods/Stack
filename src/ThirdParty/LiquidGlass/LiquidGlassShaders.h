// SPDX-License-Identifier: GPL-3.0-only AND MIT
// Upstream portions retain MIT; Stack modifications are GPL-3.0-only.
// Adapted from OverShifted/LiquidGlass, commit
// 3797fa541c1f026c521a75885ee271f03bdf9f0f.
// Copyright (c) 2026 Sepehr Kalanaki.
// Gaussian blur kernel: Copyright (c) 2015 Jam3, glsl-fast-gaussian-blur (MIT).
// Full notices: legal/third_party/LiquidGlass-MIT.txt,
// legal/third_party/GLSL-Fast-Gaussian-Blur-MIT.txt and LiquidGlass-NOTICE.txt.
// Stack modifications: GLSL4.3 bindings, bounded texture crops, a left-attached
// silhouette, half-lens coordinate mapping and defined descending smoothstep.
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
layout(location=0) uniform vec2 sizePx;
layout(location=2) uniform float radiusPx;
layout(location=5) uniform vec2 softExtent;
layout(location=6) uniform vec2 sceneOrigin;
layout(location=7) uniform vec2 sceneScale;
vec4 sampleValid(vec2 p) {
    vec2 halfTexel=.5/vec2(textureSize(softScene,0));
    return texture(softScene,clamp(p*softExtent,halfTexel,softExtent-halfTexel));
}
float sdSuperellipse(vec2 p, float n, float r) {
    // Take the absolute value of the coordinates, as the formula uses |x| and |y|.
    // This makes the shape symmetrical in all quadrants.
    vec2 p_abs = abs(p);

    // Numerator of the distance formula: |x|^n + |y|^n - r^n
    // This is the implicit equation of the superellipse. Its value is 0 on the
    // boundary, negative inside, and positive outside.
    float numerator = pow(p_abs.x, n) + pow(p_abs.y, n) - pow(r, n);

    // Denominator: n * sqrt(|x|^(2n-2) + |y|^(2n-2))
    // This is the magnitude of the gradient of the implicit function. Dividing by it
    // normalizes the result, making it a better approximation of true Euclidean distance.
    // Note: The pow() function can be computationally expensive. For a fixed, integer 'n'
    // (like n=4), you would get better performance by using direct multiplication.
    float den_x = pow(p_abs.x, 2.0 * n - 2.0);
    float den_y = pow(p_abs.y, 2.0 * n - 2.0);

    // Add a small epsilon to prevent division by zero at the origin (0,0).
    float denominator = n * sqrt(den_x + den_y) + 0.00001;

    // The final signed distance
    return numerator / denominator;
}


const float M_E=2.718281828459045;
// Application defaults from upstream src/LiquidGlass.h, not shader fallback values.
const float u_powerFactor=3.0;
const float u_a=.7, u_b=2.3, u_c=5.2, u_d=6.9;
const float u_fPower=1.0, u_noise=.06;
const float u_glowWeight=.25, u_glowBias=0.0;
const float u_glowEdge0=.5, u_glowEdge1=-.5;
float f(float x) {
    return 1.0-u_b*pow(u_c*M_E,-u_d*x-u_a);
}
float rand(vec2 co) {
    return fract(sin(dot(co,vec2(12.9898,78.233)))*43758.5453);
}
void main() {
    // Show the right half of the source lens; its left half continues outside
    // the viewport, preventing an exposed left rim while retaining radial math.
    vec2 p=vec2(uv.x,uv.y*2.0-1.0);
    float d=sdSuperellipse(p,u_powerFactor,1.0);
    float dist=max(0.0,-d);
    vec2 sampleP=p*pow(f(dist),u_fPower);
    vec2 coord=sceneOrigin+vec2(sampleP.x,sampleP.y*.5+.5)*sceneScale;
    vec4 noise=vec4(vec3(rand(gl_FragCoord.xy*1e-3)-.5),0.0);
    vec4 transmitted=sampleValid(coord)+noise*u_noise;
    float glow=sin(atan(p.y,p.x)-.5);
    // GLSL smoothstep with reversed endpoints is undefined. This expresses
    // upstream's intended descending ramp with defined endpoint ordering.
    float edge=1.0-smoothstep(u_glowEdge1,u_glowEdge0,dist);
    float mul=glow*u_glowWeight*edge+1.0+u_glowBias;

    // Stack's requested flush-left, right-corners-only silhouette is separate
    // from the upstream superellipse used for lensing and angular glow.
    float r=min(radiusPx,.5*min(sizePx.x,sizePx.y));
    vec2 q=vec2(uv.x*sizePx.x,abs((uv.y-.5)*sizePx.y))-vec2(sizePx.x,.5*sizePx.y)+r;
    float maskDistance=length(max(q,0.0))+min(max(q.x,q.y),0.0)-r;
    float coverage=1.0-smoothstep(-1.0,0.0,maskDistance);
    color=vec4(transmitted.rgb*mul,coverage);
})";
}
