#pragma once
#include "Editor/Internal/RawLab/RawLabCurveEditor.h"

namespace Stack::Editor::RawLabInternal {
// Reversible presentation coordinates. Authored curves stay in their original
// domain; the quiet tails remain reachable instead of being cropped away.
struct CurveAxisMapping {
    float minimum=0, maximum=1;
    float focusMinimum=0, focusMaximum=1;
    bool brightnessSpacing=false;
    CurveAxisMapping() = default;
    CurveAxisMapping(float minimum,float maximum,float focusMinimum,float focusMaximum,bool brightness);
    float ToView(float value) const;
    float FromView(float position) const;
    bool operator==(const CurveAxisMapping& other) const {
        return minimum==other.minimum && maximum==other.maximum &&
            focusMinimum==other.focusMinimum && focusMaximum==other.focusMaximum &&
            brightnessSpacing==other.brightnessSpacing;
    }
    bool operator!=(const CurveAxisMapping& other) const { return !(*this==other); }
private:
    float focusStart=0,focusEnd=1,viewStart=0,viewEnd=1;
};
struct CurveCanvasViewOptions {
    bool automatic=false, brightnessSpacing=false, exposureUnits=false;
    float domainMinimum=0, domainMaximum=1;
    bool operator==(const CurveCanvasViewOptions& other) const {
        return automatic==other.automatic && brightnessSpacing==other.brightnessSpacing &&
            exposureUnits==other.exposureUnits && domainMinimum==other.domainMinimum && domainMaximum==other.domainMaximum;
    }
};
struct CurveCanvasFraming {
    CurveAxisMapping axis;
    CurveCanvasViewOptions options;
    std::array<float,kRawLabHistogramBinCount> source{};
    std::uint64_t revision=0;
    bool initialized=false, histogramValid=false;
    std::array<std::array<char,32>,3> labels{};
};
CurveAxisMapping ResolveCurveCanvasAxis(CurveCanvasFraming&,
    const CurveCanvasViewOptions&,const RawLabGraphHistogram&,
    std::uint64_t revision,bool interacting,RawLabToneGraphViewRange fallback);
std::array<float,kRawLabHistogramBinCount> MapCurveHistogram(
    const std::array<float,kRawLabHistogramBinCount>&,const CurveAxisMapping&);
}
