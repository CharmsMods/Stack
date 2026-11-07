#pragma once
#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Editor/GraphUI/GraphPlotStyle.h"
#include <imgui_internal.h>

namespace Stack::Editor::RawLabInternal {
// Called while the graph is the last submitted item, before authoring gestures.
void NavigateCurveCanvas(CurveCanvasState&,const ImRect&,bool hovered,bool active,
    bool enabled,bool editing,bool diagonal);
void DrawCurveCanvasViewControl(CurveCanvasFraming&,const GraphUI::GraphPlotStyle&);
}
