#pragma once

namespace Stack::Renderer::ToolDrawerShaders {
// Half-resolution, separable Gaussian with contiguous taps. Adjacent weights
// are paired into bilinear samples, never spread apart to enlarge the blur.
// The 24-texel support bounds each pass to at most 25 texture samples.
inline constexpr const char* Blur = R"(#version 430 core
in vec2 uv; out vec4 color;
uniform sampler2D scene;
uniform vec2 direction;
uniform vec4 bounds;
uniform vec2 feather;
uniform float sigma;
void main() {
    if (any(lessThanEqual(uv, bounds.xy)) || any(greaterThanEqual(uv, bounds.zw))) {
        color = texture(scene, uv); return;
    }
    vec2 edge = min(uv - bounds.xy, bounds.zw - uv) / max(feather, vec2(.000001));
    float coverage = smoothstep(0.0, 1.0, min(edge.x, edge.y));
    float localSigma = max(.45, sigma * coverage);
    float inverseVariance = .5 / (localSigma * localSigma);
    float support = min(24.0, ceil(3.0 * localSigma));
    vec4 sum = texture(scene, uv);
    float total = 1.0;
    // Blur taps stay within the controls. Image colors cannot bleed into
    // their blurred edges when the viewport directly adjoins the controls.
    vec2 inset = min(.5 / vec2(textureSize(scene, 0)), (bounds.zw - bounds.xy) * .5);
    vec2 sampleMin = bounds.xy + inset, sampleMax = bounds.zw - inset;
    for (int i = 1; i < 24; i += 2) {
        float a = float(i), b = a + 1.0;
        if (a > support) break;
        float wa = exp(-a * a * inverseVariance);
        float wb = b <= support ? exp(-b * b * inverseVariance) : 0.0;
        float weight = wa + wb;
        vec2 offset = direction * (a + wb / weight);
        sum += (texture(scene, clamp(uv + offset, sampleMin, sampleMax)) +
                texture(scene, clamp(uv - offset, sampleMin, sampleMax))) * weight;
        total += 2.0 * weight;
    }
    color = sum / total;
})";

inline constexpr const char* Composite = R"(#version 430 core
in vec2 uv; out vec4 color;
uniform sampler2D scene, softScene;
uniform vec4 bounds;
uniform vec2 feather;
uniform float amount;
void main() {
    vec4 sharp = texture(scene, uv);
    if (any(lessThanEqual(uv, bounds.xy)) || any(greaterThanEqual(uv, bounds.zw))) {
        color = sharp; return;
    }
    vec2 edge = min(uv - bounds.xy, bounds.zw - uv) / max(feather, vec2(.000001));
    float coverage = smoothstep(0.0, 1.0, min(edge.x, edge.y));
    // The sharp contribution increases continuously toward the feathered
    // edge. Outside the drawer the original full-resolution scene is exact.
    color = mix(sharp, texture(softScene, uv), coverage * amount);
})";
}
