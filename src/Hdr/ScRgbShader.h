#pragma once
namespace Stack::Hdr {
// The composited input is linear light relative to diffuse white. Windows expects
// linear Rec.709/D65, where one scRGB unit is 80 cd/m².
inline constexpr char ScRgbConversion[]=R"(
vec4 toWindowsColor(vec4 s,float whiteScale,float headroom) {
    vec3 linear=s.rgb;
    float luminance=dot(linear,vec3(.2126,.7152,.0722));
    const float master=1000./203.;
    if(headroom<master && luminance>1.) {
        float extent=max(.00001,headroom-1.);
        float x=min(luminance,master)-1.;
        linear*=(1.+x/(1.+x*(1./extent-1./(master-1.))))/luminance;
    } else if(luminance>headroom) {
        linear*=headroom/luminance;
    }
    return vec4(linear*whiteScale,s.a);
}
)";
}
