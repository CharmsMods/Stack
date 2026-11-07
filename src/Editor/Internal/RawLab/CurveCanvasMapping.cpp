#include "CurveCanvasMapping.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Stack::Editor::RawLabInternal {
namespace {
constexpr float brightnessStrength=31.f;
constexpr float brightnessLog=3.4657359028f; // log(32)
float Space(float value,bool brightness) {
    // Signed extension also keeps extrapolated Bezier handles well-defined.
    return brightness ? std::copysign(std::log1p(brightnessStrength*std::abs(value))/brightnessLog,value) : value;
}
float Unspace(float value,bool brightness) {
    return brightness ? std::copysign(std::expm1(brightnessLog*std::abs(value))/brightnessStrength,value) : value;
}
}
CurveAxisMapping::CurveAxisMapping(float min,float max,bool brightness)
    :minimum(min),maximum(max),brightnessSpacing(brightness),
    spacedMinimum(Space(min,brightness)),
    spacedSpan(std::max(.000001f,Space(max,brightness)-spacedMinimum)) {}
float CurveAxisMapping::ToView(float value) const {
    return (Space(value,brightnessSpacing)-spacedMinimum)/spacedSpan;
}
float CurveAxisMapping::FromView(float position) const {
    return Unspace(spacedMinimum+position*spacedSpan,brightnessSpacing);
}
CurveAxisMapping ResolveCurveCanvasAxis(CurveCanvasFraming& state,
    const CurveCanvasViewOptions& options,RawLabToneGraphViewRange fallback) {
    if(!options.stableScale) return {fallback.minimum,fallback.maximum,false};
    // A changed domain starts fully zoomed out. Histogram publications refresh
    // the distribution without moving the user's viewport.
    if(state.initialized && state.options==options) return state.axis;
    state={};state.options=options;state.initialized=true;
    SetCurveCanvasView(state,{},{});
    return state.axis;
}
void SetCurveCanvasView(CurveCanvasFraming& state,RawLabToneGraphViewRange horizontal,
    RawLabToneGraphViewRange vertical) {
    const auto bounded=[](RawLabToneGraphViewRange range) {
        const float span=std::clamp(range.maximum-range.minimum,1.f/256,1.f);
        const float minimum=std::clamp(range.minimum,0.f,1.f-span);
        return RawLabToneGraphViewRange{minimum,minimum+span};
    };
    horizontal=bounded(horizontal);vertical=bounded(vertical);
    if(state.labels[0][0] && horizontal.minimum==state.horizontal.minimum && horizontal.maximum==state.horizontal.maximum &&
        vertical.minimum==state.vertical.minimum && vertical.maximum==state.vertical.maximum)return;
    state.horizontal=horizontal;state.vertical=vertical;
    const auto& options=state.options;
    const CurveAxisMapping full{0,1,options.brightnessSpacing};
    state.axis={full.FromView(state.horizontal.minimum),full.FromView(state.horizontal.maximum),options.brightnessSpacing};
    state.outputAxis={full.FromView(state.vertical.minimum),full.FromView(state.vertical.maximum),options.brightnessSpacing};
    for(int i=0;i<3;++i) {
        const float coordinate=state.axis.FromView(.1f+.4f*i);
        float value=options.domainMinimum+coordinate*(options.domainMaximum-options.domainMinimum);
        if(options.exposureAsLinearLabels)value=.18f*std::exp2(value);
        std::snprintf(state.labels[i].data(),state.labels[i].size(),options.exposureUnits?"%+.1f EV":"%.3g",value);
    }
}
void FitCurveCanvasView(CurveCanvasFraming& state,const RawLabGraphHistogram& histogram,
    std::uint64_t revision,const std::vector<RawRecipe::RawBezierCurvePoint>& points,bool diagonal) {
    if(!state.fitPending || !histogram.valid) return;
    if(state.fitAttempted && state.fitRevision==revision &&
        (revision || state.fitSource==histogram.luma)) return;
    state.fitAttempted=true;state.fitRevision=revision;
    if(!revision)state.fitSource=histogram.luma;
    if(std::none_of(histogram.luma.begin(),histogram.luma.end(),[](float v){return v>0 && std::isfinite(v);}))return;
    const auto fitted=BuildRawLabToneGraphViewRange(histogram,0,1.f);
    const CurveAxisMapping full{0,1,state.options.brightnessSpacing};
    RawLabToneGraphViewRange horizontal{full.ToView(fitted.minimum),full.ToView(fitted.maximum)};
    auto vertical=horizontal;
    if(diagonal) {
        // Keep an already edited curve visible over the fitted input interval.
        for(int i=0;i<=64;++i) {
            const float x=fitted.minimum+(fitted.maximum-fitted.minimum)*i/64.f;
            const float y=full.ToView(RawRecipe::EvaluateRawBezierCurve(points,x));
            vertical.minimum=std::min(vertical.minimum,y);vertical.maximum=std::max(vertical.maximum,y);
        }
        for(const auto& p:points)if(p.x>=fitted.minimum && p.x<=fitted.maximum) {
            const float y=full.ToView(p.y);
            vertical.minimum=std::min(vertical.minimum,y);vertical.maximum=std::max(vertical.maximum,y);
        }
    }
    const auto padded=[](RawLabToneGraphViewRange range) {
        const float padding=(range.maximum-range.minimum)*.08f;
        return RawLabToneGraphViewRange{std::max(0.f,range.minimum-padding),std::min(1.f,range.maximum+padding)};
    };
    SetCurveCanvasView(state,padded(horizontal),padded(vertical));
    state.fitPending=false;
}
std::array<float,kRawLabHistogramBinCount> MapCurveHistogram(
    const std::array<float,kRawLabHistogramBinCount>& source,const CurveAxisMapping& axis) {
    std::array<float,kRawLabHistogramBinCount> result{};
    constexpr int count=int(kRawLabHistogramBinCount);
    // Integrate each source bin over the inverse-mapped destination intervals.
    // Counts remain counts even when equal screen widths cover unequal values.
    std::array<float,count+1> boundaries{};
    for(int i=0;i<=count;++i) boundaries[i]=axis.FromView(float(i)/count);
    for(int i=0;i<count;++i) {
        if(!(source[i]>0)) continue;
        const float low=float(i)/count,high=float(i+1)/count;
        const int first=std::clamp(int(std::floor(axis.ToView(low)*count)),0,count-1);
        const int last=std::clamp(int(std::ceil(axis.ToView(high)*count))-1,0,count-1);
        for(int j=first;j<=last;++j) {
            const float overlap=std::max(0.f,std::min(high,boundaries[j+1])-std::max(low,boundaries[j]));
            result[j]+=source[i]*overlap*count;
        }
    }
    return result;
}
}
