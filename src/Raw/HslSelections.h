#pragma once
#include "Raw/ColorWarpHsl.h"
#include <limits>

namespace Stack::RawRecipe {
inline float HslSmooth(float x) { x=std::clamp(x,0.f,1.f);return x*x*(3-2*x); }
inline float HslBandWeight(const HslBrightnessBand& b,float ev) {
    const float d=std::max(b.low-ev,ev-b.high);
    return d<=0?1.f:b.softness>0?1-HslSmooth(d/b.softness):0.f;
}
inline float HslBrightnessWeight(const HslSelection& s,float ev) {
    float included=s.bands.empty()?1.f:0.f,excluded=0;
    for(const auto& b:s.bands)included=std::max(included,HslBandWeight(b,ev));
    for(const auto& b:s.excludedBands)excluded=std::max(excluded,HslBandWeight(b,ev));
    return included*(1-excluded);
}
inline float HslAreaWeight(const HslSelection& s,std::array<float,2> p) {
    if(!s.areaEnabled)return 1;
    if(s.outline.size()<3)return 0; // Unfinished is not unrestricted.
    bool inside=false;float distance=std::numeric_limits<float>::max();
    auto a=s.outline.back();
    for(const auto& b:s.outline) {
        if((a[1]>p[1])!=(b[1]>p[1]) && p[0]<(b[0]-a[0])*(p[1]-a[1])/(b[1]-a[1])+a[0])inside=!inside;
        const float dx=b[0]-a[0],dy=b[1]-a[1];
        const float t=std::clamp(((p[0]-a[0])*dx+(p[1]-a[1])*dy)/std::max(dx*dx+dy*dy,1e-12f),0.f,1.f);
        distance=std::min(distance,std::hypot(p[0]-a[0]-t*dx,p[1]-a[1]-t*dy));a=b;
    }
    return inside?(s.areaSoftness>0?HslSmooth(distance/s.areaSoftness):1.f):0.f;
}
inline float HslSelectionWeight(const HslSelection& s,const RawColorWarpCoordinate& c,std::array<float,2> uv) {
    if(!s.enabled || c.lightness<=0)return 0;
    const float d=std::abs(ColorWarpHueDelta(ColorWarpHue(c.a,c.b)-s.hue))-s.hueWidth;
    const float color=s.allColors?1.f:d<=0?1.f:s.hueSoftness>0?1-HslSmooth(d/s.hueSoftness):0.f;
    return color*HslBrightnessWeight(s,c.sceneEv)*HslAreaWeight(s,uv);
}
inline ColorWarpHslCoefficients HslOwnCoefficients(const HslSelection& s) {
    ColorWarpHslCoefficients c{};
    const auto& anchors=ColorWarpHslAnchors();
    for(int i=0;i<8;++i)c[i]={ColorWarpHue(anchors[i].sourceA,anchors[i].sourceB),s.values[i][0],s.values[i][1],s.values[i][2]};
    return c;
}
inline std::array<float,3> ResolveHslSelections(const RawColorWarpRecipe& recipe,
    const RawColorWarpCoordinate& input,std::array<float,3> global,std::array<float,2> uv) {
    auto result=recipe.limitHslToSelections?std::array<float,3>{}:global;
    for(const auto& s:recipe.hslSelections) {
        const float w=HslSelectionWeight(s,input,uv);if(w<=0)continue;
        const auto own=s.own[0]||s.own[1]||s.own[2]?EvaluateColorWarpHsl(HslOwnCoefficients(s),ColorWarpHue(input.a,input.b)):std::array<float,3>{};
        for(int channel=0;channel<3;++channel) {
            const float target=s.own[channel]?own[channel]:global[channel]*s.influence;
            result[channel]+=w*(target-result[channel]);
        }
    }
    return result;
}
inline void HslDetachChannel(HslSelection& s,int channel,const RawColorWarpRecipe& recipe) {
    const auto global=PrepareColorWarpHsl(recipe);
    for(int i=0;i<8;++i)s.values[i][channel]=global[i][channel+1]*s.influence;
    s.own[channel]=true;
}
inline bool HslHasOwnAdjustment(const RawColorWarpRecipe& recipe) {
    for(const auto& s:recipe.hslSelections)if(s.enabled)
        for(int c=0;c<3;++c)if(s.own[c])for(const auto& v:s.values)if(v[c]!=0)return true;
    return false;
}
// One compact float texture avoids increasing the GL 3.3 fragment-uniform
// requirement. Prepared once for an actual render request, never UI drawing.
inline constexpr int kHslDataWidth=144;
inline std::vector<std::array<float,4>> PackHslSelections(const RawColorWarpRecipe& recipe) {
    std::vector<std::array<float,4>> data(kHslDataWidth*std::max(std::size_t(1),recipe.hslSelections.size()));
    for(std::size_t i=0;i<recipe.hslSelections.size();++i) {
        const auto& s=recipe.hslSelections[i];auto* row=data.data()+i*kHslDataWidth;
        row[0]={s.enabled?1.f:0.f,s.hue,s.hueWidth,s.hueSoftness};
        row[1]={s.influence,s.own[0]?1.f:0.f,s.own[1]?1.f:0.f,s.own[2]?1.f:0.f};
        row[2]={float(s.bands.size()),float(s.excludedBands.size()),s.areaEnabled?float(s.outline.size()):-1.f,s.areaSoftness};
        row[3]={s.allColors?1.f:0.f,0,0,0};
        for(int j=0;j<8;++j)row[4+j]={s.values[j][0],s.values[j][1],s.values[j][2],0};
        for(std::size_t j=0;j<s.bands.size();++j)row[12+j]={s.bands[j].low,s.bands[j].high,s.bands[j].softness,0};
        for(std::size_t j=0;j<s.excludedBands.size();++j)row[28+j]={s.excludedBands[j].low,s.excludedBands[j].high,s.excludedBands[j].softness,0};
        for(std::size_t j=0;j<s.outline.size();++j) {
            // Two points per texel.
            row[44+j/2][(j%2)*2]=s.outline[j][0];row[44+j/2][(j%2)*2+1]=s.outline[j][1];
        }
        row[108]={1,1,0,0};for(const auto& p:s.outline){row[108][0]=std::min(row[108][0],p[0]);row[108][1]=std::min(row[108][1],p[1]);row[108][2]=std::max(row[108][2],p[0]);row[108][3]=std::max(row[108][3],p[1]);}
    }
    return data;
}
}
