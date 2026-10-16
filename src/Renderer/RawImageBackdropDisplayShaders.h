#pragma once

namespace Stack::Renderer::RawImageBackdropDisplayShaders {
// These are window RGB values after the authored View, not scene-linear RAW.
// The SDR window displays those numeric values directly. Decode its sRGB
// transfer only for surround convolution, then encode once for presentation.
inline constexpr const char* Functions = R"(
vec3 backdropDisplayRange(vec3 rgb) {
    for (int i=0; i<3; ++i)
        if (isnan(rgb[i]) || isinf(rgb[i])) rgb[i]=0.0;
    return clamp(rgb,vec3(0),vec3(1));
}
vec3 backdropDisplayDecode(vec3 rgb) {
    vec3 a=abs(rgb);
    return sign(rgb)*mix(a/12.92,pow((a+.055)/1.055,vec3(2.4)),step(vec3(.04045),a));
}
vec3 backdropDisplayEncode(vec3 rgb) {
    vec3 a=abs(rgb);
    return sign(rgb)*mix(12.92*a,1.055*pow(a,vec3(1.0/2.4))-.055,step(vec3(.0031308),a));
}
vec4 backdropVisibleTexel(sampler2D source, ivec2 point) {
    vec4 value=texelFetch(source,point,0);
    value.rgb=backdropDisplayRange(value.rgb);
    return value;
}
vec4 backdropVisibleSample(sampler2D source, vec2 uv) {
    // Clamp individual display texels BEFORE resampling. Clamping a hardware
    // filtered HDR/signed sample lets invisible channel values change its hue.
    ivec2 extent=textureSize(source,0);
    vec2 point=clamp(uv*vec2(extent)-.5,vec2(0),vec2(extent-1));
    ivec2 lo=ivec2(floor(point)), hi=min(lo+1,extent-1);
    vec2 f=fract(point);
    return mix(mix(backdropVisibleTexel(source,lo),backdropVisibleTexel(source,ivec2(hi.x,lo.y)),f.x),
               mix(backdropVisibleTexel(source,ivec2(lo.x,hi.y)),backdropVisibleTexel(source,hi),f.x),f.y);
}
)";
}
