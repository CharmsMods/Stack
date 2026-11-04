#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace Stack::Hdr {
using Rgb = std::array<float,3>;
inline constexpr float ReferenceWhite = 203.f, PeakNits = 1000.f;
inline constexpr float MasterHeadroom = PeakNits / ReferenceWhite;
inline float Extend(float y,float grey,float headroom) {
    if (y <= grey || headroom <= 1.f) return y;
    const float x=y-grey;
    return grey+x/(1.f-x*(1.f/(1.f-grey)-1.f/(headroom-grey)));
}
inline float Srgb(float v) {
    const float a=std::abs(v);
    return std::copysign(a<=.0031308f ? 12.92f*a : 1.055f*std::pow(a,1.f/2.4f)-.055f,v);
}
inline float Linear(float v) {
    const float a=std::abs(v);
    return std::copysign(a<=.04045f ? a/12.92f : std::pow((a+.055f)/1.055f,2.4f),v);
}
inline Rgb To2020(Rgb v) {
    return {.627404f*v[0]+.329283f*v[1]+.043313f*v[2],
            .069097f*v[0]+.919540f*v[1]+.011363f*v[2],
            .016391f*v[0]+.088013f*v[1]+.895596f*v[2]};
}
inline Rgb To709(Rgb v) {
    return {1.660491f*v[0]-.587641f*v[1]-.072850f*v[2],
            -.124551f*v[0]+1.132900f*v[1]-.008349f*v[2],
            -.018151f*v[0]-.100579f*v[1]+1.118730f*v[2]};
}
inline float Pq(float nits) {
    const double p=std::pow(std::clamp(double(nits)/10000.,0.,1.),2610./16384.);
    return float(std::pow((3424./4096.+2413./128.*p)/(1.+2392./128.*p),2523./32.));
}
inline float Nits(float pq) {
    const double p=std::pow(std::clamp(double(pq),0.,1.),32./2523.);
    return float(10000.*std::pow(std::max(p-3424./4096.,0.)/(2413./128.-2392./128.*p),16384./2610.));
}
}
