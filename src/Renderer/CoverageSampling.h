#pragma once

namespace Stack::Renderer {
inline constexpr const char* CoverageSamplingGlsl = R"GLSL(
vec4 sampleCovered(sampler2D source, vec2 uv) {
    ivec2 size = textureSize(source, 0);
    vec2 position = uv * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(position));
    vec2 fraction = fract(position);
    vec3 color = vec3(0.0);
    float coverage = 0.0;
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        float weight = (x == 0 ? 1.0-fraction.x : fraction.x) *
                       (y == 0 ? 1.0-fraction.y : fraction.y);
        vec4 value = texelFetch(source, clamp(base+ivec2(x,y), ivec2(0), size-1), 0);
        coverage += weight * value.a;
        color += weight * value.a * value.rgb;
    }
    return vec4(coverage > 0.0 ? color / coverage : vec3(0.0), coverage);
}
)GLSL";
}
