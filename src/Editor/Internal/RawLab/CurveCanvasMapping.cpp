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
CurveAxisMapping::CurveAxisMapping(float min,float max,float focusMin,float focusMax,bool brightness)
    :minimum(min),maximum(max),focusMinimum(focusMin),focusMaximum(focusMax),brightnessSpacing(brightness) {
    const float span=std::max(.0001f,maximum-minimum);
    focusStart=Space((focusMinimum-minimum)/span,brightnessSpacing);
    focusEnd=Space((focusMaximum-minimum)/span,brightnessSpacing);
    // Never compress the useful interval. Allocate at most ten percent to
    // either tail, retaining the complete authored domain and its endpoints.
    viewStart=std::min(.1f,focusStart);viewEnd=1.f-std::min(.1f,1.f-focusEnd);
}
float CurveAxisMapping::ToView(float value) const {
    const float normalized=Space((value-minimum)/std::max(.0001f,maximum-minimum),brightnessSpacing);
    if(normalized<focusStart && focusStart>0) return normalized*viewStart/focusStart;
    if(normalized>focusEnd && focusEnd<1) return viewEnd+(normalized-focusEnd)*(1-viewEnd)/(1-focusEnd);
    return viewStart+(normalized-focusStart)*(viewEnd-viewStart)/std::max(.0001f,focusEnd-focusStart);
}
float CurveAxisMapping::FromView(float position) const {
    float normalized;
    if(position<viewStart && viewStart>0) normalized=position*focusStart/viewStart;
    else if(position>viewEnd && viewEnd<1) normalized=focusEnd+(position-viewEnd)*(1-focusEnd)/(1-viewEnd);
    else normalized=focusStart+(position-viewStart)*(focusEnd-focusStart)/std::max(.0001f,viewEnd-viewStart);
    return minimum+Unspace(normalized,brightnessSpacing)*(maximum-minimum);
}
CurveAxisMapping ResolveCurveCanvasAxis(CurveCanvasFraming& state,
    const CurveCanvasViewOptions& options,const RawLabGraphHistogram& histogram,
    std::uint64_t revision,bool interacting,RawLabToneGraphViewRange fallback) {
    if(!options.automatic) return {fallback.minimum,fallback.maximum,fallback.minimum,fallback.maximum,false};
    const bool sameOptions=state.initialized && state.options==options;
    if(sameOptions && interacting) return state.axis;
    // An unavailable replacement is not a new measurement. Keep the last fit
    // while the owning analysis lane prepares the next accepted input packet.
    if(sameOptions && !histogram.valid) return state.axis;
    if(sameOptions && state.histogramValid==histogram.valid && state.revision==revision &&
        (revision || state.source==histogram.luma)) return state.axis;
    state.options=options;state.revision=revision;state.initialized=true;
    state.histogramValid=histogram.valid;state.source=histogram.luma;
    auto fitted=BuildRawLabToneGraphViewRange(histogram,0,1);
    const float padding=.08f*(fitted.maximum-fitted.minimum);
    fitted.minimum=std::max(0.f,fitted.minimum-padding);
    fitted.maximum=std::min(1.f,fitted.maximum+padding);
    state.axis={0,1,fitted.minimum,fitted.maximum,options.brightnessSpacing};
    for(int i=0;i<3;++i) {
        const float coordinate=state.axis.FromView(.1f+.4f*i);
        const float value=options.domainMinimum+coordinate*(options.domainMaximum-options.domainMinimum);
        std::snprintf(state.labels[i].data(),state.labels[i].size(),options.exposureUnits?"%+.1f EV":"%.3g",value);
    }
    return state.axis;
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
