#pragma once

namespace Stack::Renderer::LocalToneGuideShaders {

inline constexpr const char* Vertex = R"(
#version 330 core
layout(location=0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)";

inline constexpr const char* Initialize = R"(
#version 330 core
layout(location=0) out vec4 moments;
uniform sampler2D uInput;
uniform ivec2 uSourceSize;
uniform ivec2 uAnalysisSize;
uniform vec3 uLuma;
void main() {
    ivec2 cell = ivec2(gl_FragCoord.xy);
    // Disjoint integer blocks visit every input pixel exactly once. Unlike
    // sparse proxy sampling, this retains small highlights and coverage holes.
    ivec2 first = cell * uSourceSize / uAnalysisSize;
    ivec2 end = (cell + 1) * uSourceSize / uAnalysisSize;
    vec3 sum = vec3(0.0);
    for (int y = first.y; y < end.y; ++y) {
        for (int x = first.x; x < end.x; ++x) {
            vec4 color = texelFetch(uInput, ivec2(x, y), 0);
            if (any(isnan(color)) || any(isinf(color)) || color.a <= 0.0) continue;
            // Signed RGB is meaningful in the working space. Only the scalar
            // luminance must be positive to participate in the logarithm.
            float luminance = dot(color.rgb, uLuma);
            if (!(luminance > 0.0) || isinf(luminance) || isnan(luminance)) continue;
            float ev = log2(luminance) - log2(0.18);
            if (isinf(ev) || isnan(ev)) continue;
            float weight = clamp(color.a, 0.0, 1.0);
            sum += vec3(ev, ev * ev, 1.0) * weight;
        }
    }
    // Keep actual source-pixel counts: odd-sized blocks must not receive the
    // same measurement weight as larger blocks in a neighboring grid cell.
    moments = vec4(sum, float(end.x - first.x) * float(end.y - first.y));
}
)";

inline constexpr const char* Box = R"(
#version 330 core
layout(location=0) out vec4 field;
uniform sampler2D uInput;
uniform ivec2 uDirection;
uniform int uRadius;
uniform int uMakeCoefficients;
uniform float uEpsilon;
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(uInput, 0);
    int coordinate = uDirection.x != 0 ? pixel.x : pixel.y;
    int extent = uDirection.x != 0 ? size.x : size.y;
    int first = max(-uRadius, -coordinate);
    int last = min(uRadius, extent - coordinate - 1);
    vec4 sum = vec4(0.0);
    for (int offset = first; offset <= last; ++offset)
        sum += texelFetch(uInput, pixel + offset * uDirection, 0);
    vec4 mean = sum / float(last - first + 1);
    if (uMakeCoefficients == 0) { field = mean; return; }
    if (!(mean.z > 0.0) || !(mean.w > 0.0)) {
        field = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    float average = mean.x / mean.z;
    float variance = max(0.0, mean.y / mean.z - average * average);
    float a = variance / (variance + uEpsilon);
    float b = (1.0 - a) * average;
    float coverage = clamp(mean.z / mean.w, 0.0, 1.0);
    // Premultiplication also makes the final coefficient box average and
    // bilinear upsampling normalize correctly across missing image coverage.
    field = vec4(a * coverage, b * coverage, coverage, 1.0);
}
)";

} // namespace Stack::Renderer::LocalToneGuideShaders
