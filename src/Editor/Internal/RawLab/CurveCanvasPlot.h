#pragma once
#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Editor/GraphUI/GraphPlotStyle.h"
#include "Animation/ControlMotion.h"
#include <imgui_internal.h>

namespace Stack::Editor::RawLabInternal {
// Same-frame presentation snapshot: native input is captured when the curve
// item is submitted, while geometry is resolved after all authoring controls.
struct CurveCanvasPlotContext {
    ImRect box;
    GraphUI::GraphPlotStyle style;
    RawLabToneGraphViewRange view;
    Animation::ControlInput input;
    ImVec2 mouse;
    float offset=0,yMinimum=0,yMaximum=1;
    bool diagonal=false,amountDriven=false,hovered=false;
    int hoveredPoint=-1;
    bool geometryChanged=false;
};
void DrawCurveCanvasPlot(const CurveCanvasPlotContext&,
    const std::vector<RawRecipe::RawBezierCurvePoint>&,
    const RawRecipe::CurveAmountState&,CurveCanvasState&,
    const RawLabGraphHistogram&,std::uint64_t histogramRevision);
}
