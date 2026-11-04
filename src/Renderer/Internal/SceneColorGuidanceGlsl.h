#pragma once

namespace Stack::Renderer {

// Shared by moment construction and full-resolution coefficient evaluation.
// The features are bounded and exposure invariant, including for signed RGB.
inline constexpr const char* SceneColorGuidanceGlsl = R"(
bool sceneColorChroma(vec3 rgb, vec3 luma, out vec2 chroma) {
    chroma=vec2(0.0);
    if (any(isnan(rgb)) || any(isinf(rgb))) return false;
    float scale=max(max(abs(rgb.r),abs(rgb.g)),abs(rgb.b));
    if (!(scale>0.0)) return false;
    vec3 normalized=rgb/scale;
    if (!(dot(normalized,luma)>0.0)) return false;
    chroma=vec2(normalized.r-normalized.g,normalized.b-normalized.g)/
        dot(abs(normalized),vec3(1.0));
    return !any(isnan(chroma)) && !any(isinf(chroma));
}

vec3 sampleSceneColorChroma(sampler2D colors, sampler2D confidence, vec2 uv, vec3 luma) {
    ivec2 size=textureSize(colors,0);
    vec2 position=clamp(uv*vec2(size)-vec2(0.5),vec2(0.0),vec2(size-1));
    ivec2 first=ivec2(floor(position));
    vec2 fraction=fract(position);
    vec2 sum=vec2(0.0);
    float weight=0.0;
    for (int y=0;y<2;++y) for (int x=0;x<2;++x) {
        ivec2 p=min(first+ivec2(x,y),size-1);
        float c=texelFetch(confidence,p,0).r;
        if (isnan(c) || isinf(c) || !(c>0.0)) continue;
        vec2 chroma;
        if (!sceneColorChroma(texelFetch(colors,p,0).rgb,luma,chroma)) continue;
        // Normalize features before interpolation: a brighter evidence texel
        // must not dominate a neighboring hue merely because of its exposure.
        float w=(x==0 ? 1.0-fraction.x : fraction.x)*
            (y==0 ? 1.0-fraction.y : fraction.y)*clamp(c,0.0,1.0);
        sum+=w*chroma;
        weight+=w;
    }
    return weight>1e-8 ? vec3(sum/weight,weight) : vec3(0.0);
}
)";

} // namespace Stack::Renderer
