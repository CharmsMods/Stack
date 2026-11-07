#pragma once
#include "Editor/Internal/RawLab/RawLabCurveEditor.h"

namespace Stack::Editor::RawLabInternal {
// Reversible presentation coordinates. Authored curves stay in their original
// domain; zoom crops one continuous scale instead of compressing its edges.
struct CurveAxisMapping {
    float minimum=0, maximum=1;
    bool brightnessSpacing=false;
    CurveAxisMapping() = default;
    CurveAxisMapping(float minimum,float maximum,bool brightness);
    float ToView(float value) const;
    float FromView(float position) const;
    bool operator==(const CurveAxisMapping& other) const {
        return minimum==other.minimum && maximum==other.maximum &&
            brightnessSpacing==other.brightnessSpacing;
    }
    bool operator!=(const CurveAxisMapping& other) const { return !(*this==other); }
private:
    float spacedMinimum=0,spacedSpan=1;
};
struct CurveCanvasViewOptions {
    bool stableScale=false, brightnessSpacing=false, exposureUnits=false;
    float domainMinimum=0, domainMaximum=1;
    bool exposureAsLinearLabels=false;
    bool operator==(const CurveCanvasViewOptions& other) const {
        return stableScale==other.stableScale && brightnessSpacing==other.brightnessSpacing &&
            exposureUnits==other.exposureUnits && domainMinimum==other.domainMinimum && domainMaximum==other.domainMaximum && exposureAsLinearLabels==other.exposureAsLinearLabels;
    }
};
struct CurveCanvasFraming {
    CurveAxisMapping axis,outputAxis;
    CurveCanvasViewOptions options;
    bool initialized=false;
    // View bounds in the full display scale, independent of authored values.
    RawLabToneGraphViewRange horizontal{},vertical{};
    bool fitPending=false,fitAttempted=false;
    std::uint64_t fitRevision=0;
    std::array<float,kRawLabHistogramBinCount> fitSource{};
    enum class Request { None,Fit,Full } request=Request::None;
    std::array<std::array<char,32>,3> labels{};
};
CurveAxisMapping ResolveCurveCanvasAxis(CurveCanvasFraming&,
    const CurveCanvasViewOptions&,RawLabToneGraphViewRange fallback);
void SetCurveCanvasView(CurveCanvasFraming&,RawLabToneGraphViewRange horizontal,
    RawLabToneGraphViewRange vertical);
void FitCurveCanvasView(CurveCanvasFraming&,const RawLabGraphHistogram&,
    std::uint64_t histogramRevision,const std::vector<RawRecipe::RawBezierCurvePoint>&,
    bool diagonal);
std::array<float,kRawLabHistogramBinCount> MapCurveHistogram(
    const std::array<float,kRawLabHistogramBinCount>&,const CurveAxisMapping&);
}
