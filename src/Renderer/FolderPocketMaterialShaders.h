#pragma once

namespace Stack::Renderer::FolderPocketMaterialShaders {

// Same vertex locations and lower-left GL clip convention as Stack's main
// ImGui OpenGL 4.3 backend. No private ImGui renderer state is inspected.
inline constexpr const char* Vertex = R"(#version 430 core
layout(location=0) in vec2 Position;
layout(location=1) in vec2 UV;
layout(location=2) in vec4 Color;
layout(location=1) uniform vec4 displayRect;
out vec2 sourceUv;
out float coverage;
void main() {
    sourceUv = UV;
    coverage = Color.a;
    vec2 p = (Position-displayRect.xy)/displayRect.zw;
    gl_Position = vec4(p.x*2.0-1.0, 1.0-p.y*2.0, 0.0, 1.0);
}
)";

inline constexpr const char* Fragment = R"(#version 430 core
// Texture unit is immutable for this material; do not resend it per pocket.
layout(location=0, binding=0) uniform sampler2D thumbnail;
layout(location=2) uniform vec2 sampleStep;
layout(location=3) uniform vec4 topTint;
layout(location=4) uniform vec4 bottomTint;
layout(location=5) uniform float transmission;
layout(location=6) uniform vec2 gradientUv;
in vec2 sourceUv;
in float coverage;
layout(location=0) out vec4 FragColor;
void main() {
    vec2 extent = vec2(textureSize(thumbnail, 0));
    // Gallery thumbnails already own full mip chains. Sample the mip matching
    // each Gaussian tap's footprint so widely spaced taps cannot leak detail.
    // A valid nonmipped GL_LINEAR texture still uses its base level unchanged.
    float footprint = max(abs(sampleStep.x)*extent.x, abs(sampleStep.y)*extent.y);
    float lod = max(0.0, log2(max(1.0, footprint)));
    const float weights[5] = float[5](1.0, 4.0, 6.0, 4.0, 1.0);
    vec3 blurred = vec3(0.0);
    vec2 edge = .5 / max(extent, vec2(1.0));
    for (int y=0; y<5; ++y) {
        for (int x=0; x<5; ++x) {
            vec2 uv = clamp(sourceUv + vec2(x-2,y-2)*sampleStep, edge, 1.0-edge);
            blurred += textureLod(thumbnail, uv, lod).rgb * (weights[x]*weights[y]/256.0);
        }
    }
    float span = gradientUv.y-gradientUv.x;
    float depth = abs(span)>0.00001 ? clamp((sourceUv.y-gradientUv.x)/span,0.0,1.0) : 0.0;
    vec4 tint = mix(topTint,bottomTint,smoothstep(0.0,1.0,depth));
    // The lower face is nearly milky-solid. Only soft color, never sharp lower
    // file details, transmits through it. This is a UI material in display RGB.
    float light = transmission * mix(1.0,.28,smoothstep(0.0,.85,depth));
    vec3 color = mix(tint.rgb,blurred,light);
    // Tiny stationary surface grain, anchored in face UVs rather than time.
    vec2 grainCell = floor(sourceUv/max(abs(sampleStep),vec2(.00001))*6.0);
    float grain = fract(sin(dot(grainCell,vec2(12.9898,78.233)))*43758.5453)-.5;
    color += grain*(1.5/255.0);
    FragColor = vec4(clamp(color,0.0,1.0),coverage*tint.a);
}
)";

} // namespace Stack::Renderer::FolderPocketMaterialShaders
