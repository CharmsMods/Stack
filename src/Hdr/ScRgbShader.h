#pragma once
namespace Stack::Hdr {
// The composited input is linear light relative to diffuse white. Windows expects
// linear Rec.709/D65, where one scRGB unit is 80 cd/m².
inline constexpr char ScRgbConversion[]=R"(
vec4 toWindowsColor(vec4 s,float whiteScale,float headroom) {
    vec3 linear=s.rgb;
    float luminance=dot(linear,vec3(.2126,.7152,.0722));
    // Photo adaptation happens before UI composition, using its SDR/HDR pair.
    // This final conversion must not tone-map it a second time or change UI white.
    if(luminance>headroom) {
        linear*=headroom/luminance;
    }
    return vec4(linear*whiteScale,s.a);
}
)";
}
