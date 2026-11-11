#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Stack::RawRecipe {
struct LightBands {
    std::array<float, 5> amounts{}; // -100..100; each contributes at most four stops.
    bool automatic = true;
    float lowEv = -8.f;
    float highEv = 6.f;
};
struct LightRange {
    float lowEv = -8.f, highEv = 6.f;
    bool valid = false;
};
inline float LightBandWeight(float sceneEv, LightRange range, int band) {
    const float x = std::clamp(4.f * (sceneEv-range.lowEv) /
        std::max(.1f,range.highEv-range.lowEv),0.f,4.f);
    const float distance = std::abs(x-float(band));
    return distance < 1.f ? .5f+.5f*std::cos(distance*3.14159265358979323846f) : 0.f;
}
inline float LightBandDelta(const LightBands& bands, LightRange range, float sceneEv) {
    if (!std::isfinite(sceneEv)) return 0.f;
    float result=0.f;
    for(int i=0;i<5;++i) result+=.04f*bands.amounts[i]*LightBandWeight(sceneEv,range,i);
    return result;
}
// Shared by CPU analysis and the GPU histogram readback. Bins span -32..32 EV.
inline LightRange FitLightRange(const std::array<std::uint32_t,1024>& bins) {
    std::uint64_t total=0; for(auto count:bins) total+=count;
    if(!total) return {-8.f,6.f,true}; // Empty/black input: deterministic neutral targeting.
    const auto percentile=[&](double fraction) {
        const auto target=std::max<std::uint64_t>(1,std::uint64_t(std::ceil(total*fraction)));
        std::uint64_t sum=0;
        for(int i=0;i<1024;++i) {sum+=bins[i];if(sum>=target)return -32.f+(i+.5f)/16.f;}
        return 32.f;
    };
    const float low=percentile(.01),high=percentile(.99),center=(low+high)*.5f;
    const float half=std::max(2.f,(high-low)*.5f);
    return {center-half,center+half,true};
}
inline LightRange AnalyzeLightRange(const std::vector<float>& rgba, const std::array<float,3>& luma, float middleGrey=.18f) {
    std::array<std::uint32_t,1024> bins{};
    for(std::size_t i=0;i+3<rgba.size();i+=4) {
        if(!std::isfinite(rgba[i]) || !std::isfinite(rgba[i+1]) || !std::isfinite(rgba[i+2]) ||
            !std::isfinite(rgba[i+3]) || rgba[i+3]<=0.f)continue;
        const float y=rgba[i]*luma[0]+rgba[i+1]*luma[1]+rgba[i+2]*luma[2];
        if(!(y>0.f) || !std::isfinite(y))continue;
        const float ev=std::log2(y)-std::log2(std::max(middleGrey,1e-6f));
        ++bins[std::clamp(int(std::floor((ev+32.f)*16.f)),0,1023)];
    }
    return FitLightRange(bins);
}

}
