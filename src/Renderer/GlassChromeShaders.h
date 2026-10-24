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
    // The shape extends off-screen to the left. Only its exposed right-hand
    // corners have curvature; there is no artificial left bevel or shadow.
    vec2 p = vec2(uv.x * sizePx.x, (uv.y - .5) * sizePx.y);
    float r = min(radiusPx, .5 * min(sizePx.x,sizePx.y));
    vec2 q = vec2(p.x,abs(p.y)) - vec2(sizePx.x,.5*sizePx.y) + r;
    float sd = length(max(q,0.0)) + min(max(q.x,q.y),0.0) - r;
    float coverage = 1.0 - smoothstep(-1.0,0.0,sd);
    vec2 outward = normalize(max(q,vec2(.0001))) * vec2(1.0,sign(p.y));
    if (q.x < 0.0 || q.y < 0.0)
        outward = q.x > q.y ? vec2(1,0) : vec2(0,sign(p.y));

    // A rounded thickness profile supplies a surface normal. Snell refraction
    // and Schlick reflectance motivate this screen-space approximation; there
    // is no claim of tracing a real three-dimensional glass object.
    float depth = max(0.0,-sd);
    float bevel = 13.0 * pixelScale;
    float t = clamp(depth / bevel,0.0,1.0);
    float cap = sqrt(max(.025,1.0-(1.0-t)*(1.0-t)));
    float slope = (1.0-t) / cap;
    vec3 normal = normalize(vec3(outward*slope,1.0));
    vec3 ray = refract(vec3(0,0,-1),normal,1.0/1.46);
    vec2 displacement = -ray.xy / max(.2,-ray.z) * 16.0 * pixelScale;
    vec2 sceneUv = sceneOrigin + (uv + displacement / sizePx) * sceneScale;
    vec3 clear = sampleValid(sharpScene,sceneUv,sharpExtent);
    vec3 scattered = sampleValid(softScene,sceneUv,softExtent);
    vec3 transmitted = mix(clear,scattered,.38 + .16*t);

    float luminance = dot(tint.rgb,vec3(.2126,.7152,.0722));
    vec3 neutral = luminance < .45 ? vec3(.12,.14,.16) : vec3(.94,.96,.98);
    // Keep the backdrop visible. Theme tint is only a light contrast veil,
    // unlike the former opaque dark panel treatment.
    transmitted = mix(transmitted,neutral,.12);
    float fresnel = .035 + .965*pow(1.0-normal.z,5.0);
    vec3 light = normalize(vec3(-.35,.65,.70));
    float specular = pow(max(0.0,dot(normal,light)),18.0);
    float rim = exp(-pow((depth-1.25*pixelScale)/max(.65,pixelScale),2.0));
    float edgeLight = .35 + .65*max(0.0,dot(outward,normalize(vec2(-.4,1.0))));
    vec3 c = transmitted*(1.0-fresnel*.35);
    c += vec3(.88,.94,1.0)*(fresnel*.35 + specular*.18 + rim*edgeLight*.30);
    // A restrained dark underside separates transparent glass from content.
    c *= 1.0 - rim*.12*max(0.0,-outward.y);
    color = vec4(clamp(c,0.0,1.0),coverage);
})";
}
