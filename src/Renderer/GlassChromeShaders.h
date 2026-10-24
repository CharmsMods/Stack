// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Independently authored Stack shaders. Generic separable filtering and an
// analytic rounded-rectangle distance field; no reference implementation code.
namespace Stack::Renderer::GlassChromeShaders {
inline constexpr const char* Vertex = R"(#version 430 core
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
inline constexpr const char* Blur = R"(#version 430 core
in vec2 uv; out vec4 color;
layout(binding=0) uniform sampler2D source;
layout(location=0) uniform vec2 stepUv;
layout(location=1) uniform vec2 extent;
vec3 tap(vec2 p) {
    vec2 halfTexel = .5 / vec2(textureSize(source,0));
    return texture(source,clamp(p,halfTexel,extent-halfTexel)).rgb;
}
void main() {
    vec2 p = uv * extent;
    vec3 c = tap(p) * .227027;
    c += (tap(p + stepUv * 1.384615) + tap(p - stepUv * 1.384615)) * .316216;
    c += (tap(p + stepUv * 3.230769) + tap(p - stepUv * 3.230769)) * .070270;
    color = vec4(c,1.0);
})";
inline constexpr const char* Material = R"(#version 430 core
in vec2 uv; out vec4 color;
layout(binding=0) uniform sampler2D softScene;
layout(binding=1) uniform sampler2D sharpScene;
layout(location=0) uniform vec2 sizePx;
layout(location=1) uniform vec4 tint;
layout(location=2) uniform float radiusPx;
layout(location=3) uniform float pixelScale;
layout(location=4) uniform vec2 sharpExtent;
layout(location=5) uniform vec2 softExtent;
layout(location=6) uniform vec2 sceneOrigin;
layout(location=7) uniform vec2 sceneScale;
vec3 sampleValid(sampler2D image, vec2 p, vec2 extent) {
    vec2 halfTexel = .5 / vec2(textureSize(image,0));
    return texture(image,clamp(p*extent,halfTexel,extent-halfTexel)).rgb;
}
void main() {
    vec2 p = (uv - .5) * sizePx;
    float r = min(radiusPx, .5 * min(sizePx.x,sizePx.y));
    vec2 q = abs(p) - .5 * sizePx + r;
    float sd = length(max(q,0.0)) + min(max(q.x,q.y),0.0) - r;
    float coverage = 1.0 - smoothstep(-1.0,0.0,sd);
    vec2 n = normalize(max(q,vec2(.001))) * sign(p);
    if (q.x < 0.0 || q.y < 0.0) n = q.x > q.y ? vec2(sign(p.x),0) : vec2(0,sign(p.y));
    float rim = exp(-abs(sd + 1.5 * pixelScale) / max(1.0,1.2 * pixelScale));
    float bend = exp(-max(0.0,-sd) / max(1.0,9.0 * pixelScale));
    vec2 refracted = sceneOrigin + (uv + n * bend * 3.5 * pixelScale / sizePx) * sceneScale;
    vec3 scene = mix(sampleValid(softScene,refracted,softExtent),sampleValid(sharpScene,refracted,sharpExtent),.10);
    // Strong neutral tint protects the legibility of small navigation glyphs.
    vec3 c = mix(scene,tint.rgb,mix(.62,.84,clamp(tint.a,0.0,1.0)));
    float light = dot(n,normalize(vec2(-.6,1.0)));
    c += vec3(.13) * rim * max(light,0.0);
    c -= vec3(.06) * rim * max(-light,0.0);
    color = vec4(clamp(c,0.0,1.0),coverage);
})";
}
