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
        if(s.areaSoftness>0) {
            const float dx=b[0]-a[0],dy=b[1]-a[1];
            const float t=std::clamp(((p[0]-a[0])*dx+(p[1]-a[1])*dy)/std::max(dx*dx+dy*dy,1e-12f),0.f,1.f);
            distance=std::min(distance,std::hypot(p[0]-a[0]-t*dx,p[1]-a[1]-t*dy));
        }
        a=b;
    }
    return inside?(s.areaSoftness>0?HslSmooth(distance/s.areaSoftness):1.f):0.f;
}
inline float NeutralRangeWeight(const RawColorWarpCoordinate& c) {
    const auto ab=ColorWarpSelectionCoordinate(c);
    return 1.f-HslSmooth((std::hypot(ab[0],ab[1])-.02f)/.06f);
}
inline float WarpChromaWeight(const HslSelection& s,const RawColorWarpCoordinate& input) {
    if(s.sourceChroma<0)return 1.f;
    const auto ab=ColorWarpSelectionCoordinate(input);
    const float distance=std::abs(std::hypot(ab[0],ab[1])-s.sourceChroma);
    const float feather=std::min(.035f,s.chromaReach);
    return HslSmooth((s.chromaReach-distance)/feather);
}
inline float HslHueWeight(const HslSelection& s,const RawColorWarpCoordinate& input,float hue) {
    if(s.neutralOnly)return NeutralRangeWeight(input);
    if(s.allColors)return 1.f;
    const float distance=std::abs(ColorWarpHueDelta(hue-s.hue));
    const float edge=distance-s.hueWidth;
    if(s.sourceChroma>=0 && s.hueSoftness>0 && s.hueWidth>0)
        return HslSmooth(-edge/std::min(s.hueWidth,s.hueSoftness));
    return edge<=0?1.f:s.hueSoftness>0?1-HslSmooth(edge/s.hueSoftness):0.f;
}
inline float HslSelectionWeight(const HslSelection& s,const RawColorWarpCoordinate& c,std::array<float,2> uv) {
    if(!s.enabled || c.lightness<=0)return 0;
    const float color=HslHueWeight(s,c,ColorWarpHue(c.a,c.b));
    return WarpChromaWeight(s,c)*color*HslBrightnessWeight(s,c.sceneEv)*HslAreaWeight(s,uv);
}
inline ColorWarpHslCoefficients HslOwnCoefficients(const HslSelection& s) {
    ColorWarpHslCoefficients c{};
    const auto& hues=ColorWarpHslAnchorHues();
    for(int i=0;i<8;++i)c[i]={hues[i],s.values[i][0],s.values[i][1],s.values[i][2]};
    return c;
}
struct HslSelectionEvaluation {
    ColorWarpHslCoefficients coefficients{};
    std::array<float,4> bounds{1,1,0,0};
};
inline HslSelectionEvaluation PrepareHslSelectionEvaluation(const HslSelection& selection) {
    HslSelectionEvaluation result;
    if(selection.own[0] || selection.own[1] || selection.own[2])result.coefficients=HslOwnCoefficients(selection);
    for(const auto& point:selection.outline) {
        result.bounds[0]=std::min(result.bounds[0],point[0]);
        result.bounds[1]=std::min(result.bounds[1],point[1]);
        result.bounds[2]=std::max(result.bounds[2],point[0]);
        result.bounds[3]=std::max(result.bounds[3],point[1]);
    }
    return result;
}
inline std::array<float,3> ResolveHslSelections(const RawColorWarpRecipe& recipe,
    const RawColorWarpCoordinate& input,std::array<float,3> global,std::array<float,2> uv,
    const std::vector<HslSelectionEvaluation>* prepared=nullptr,std::array<float,3>* grade=nullptr) {
    auto result=recipe.limitHslToSelections?std::array<float,3>{}:global;
    if(input.lightness<=0)return result;
    // All selection curves use the same immutable anchors. Preserve their raw
    // a/b hue (including near-neutral values), which differs from the guarded
    // normalized hue used by the global family curve.
    const float hue=ColorWarpHue(input.a,input.b);
    ColorWarpHslSegment segment;
    bool hasSegment=false;
    for(std::size_t index=0;index<recipe.hslSelections.size();++index) {
        const auto& s=recipe.hslSelections[index];if(!s.enabled)continue;
        const auto* evaluation=prepared && index<prepared->size()?&(*prepared)[index]:nullptr;
        if(s.areaEnabled && evaluation) {
            const auto& bounds=evaluation->bounds;
            if(uv[0]<bounds[0] || uv[1]<bounds[1] || uv[0]>bounds[2] || uv[1]>bounds[3])continue;
        }
        const float color=HslHueWeight(s,input,hue);
        if(color<=0)continue;
        const float brightness=HslBrightnessWeight(s,input.sceneEv);if(brightness<=0)continue;
        const float w=WarpChromaWeight(s,input)*color*brightness*HslAreaWeight(s,uv);if(w<=0)continue;
        if(grade)for(int channel=0;channel<3;++channel)(*grade)[channel]+=w*s.grade[channel];
        std::array<float,3> own{};
        if(s.own[0] || s.own[1] || s.own[2]) {
            ColorWarpHslCoefficients temporary;
            const auto* coefficients=evaluation?&evaluation->coefficients:nullptr;
            if(!coefficients){temporary=HslOwnCoefficients(s);coefficients=&temporary;}
            if(!hasSegment){segment=FindColorWarpHslSegment(*coefficients,hue);hasSegment=true;}
            own=EvaluateColorWarpHsl(*coefficients,segment);
        }
        for(int channel=0;channel<3;++channel) {
            if(s.independentChannels && !s.own[channel])continue;
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
    for(const auto& s:recipe.hslSelections)if(s.enabled) {
        if(s.grade!=std::array<float,3>{})return true;
        for(int c=0;c<3;++c)if(s.own[c])for(const auto& v:s.values)if(v[c]!=0)return true;
    }
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
        row[1]={s.independentChannels?-1.f:s.influence,s.own[0]?1.f:0.f,s.own[1]?1.f:0.f,s.own[2]?1.f:0.f};
        row[2]={float(s.bands.size()),float(s.excludedBands.size()),s.areaEnabled?float(s.outline.size()):-1.f,s.areaSoftness};
        row[3]={s.neutralOnly?2.f:s.allColors?1.f:0.f,s.grade[0],s.grade[1],s.grade[2]};
        for(int j=0;j<8;++j)row[4+j]={s.values[j][0],s.values[j][1],s.values[j][2],0};
        for(std::size_t j=0;j<s.bands.size();++j)row[12+j]={s.bands[j].low,s.bands[j].high,s.bands[j].softness,0};
        for(std::size_t j=0;j<s.excludedBands.size();++j)row[28+j]={s.excludedBands[j].low,s.excludedBands[j].high,s.excludedBands[j].softness,0};
        for(std::size_t j=0;j<s.outline.size();++j) {
            // Two points per texel.
            row[44+j/2][(j%2)*2]=s.outline[j][0];row[44+j/2][(j%2)*2+1]=s.outline[j][1];
        }
        row[109]={s.sourceChroma,s.chromaReach,0,0};
        row[108]={1,1,0,0};for(const auto& p:s.outline){row[108][0]=std::min(row[108][0],p[0]);row[108][1]=std::min(row[108][1],p[1]);row[108][2]=std::max(row[108][2],p[0]);row[108][3]=std::max(row[108][3],p[1]);}
    }
    return data;
}
}
