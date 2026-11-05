#pragma once
#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Editor/Layers/ToneLayers.h"
#include <array>
#include <memory>

namespace Stack::Editor {
struct ToneCurveEditorState {
    void EndInteraction() {
        dragging=-1;
        for (auto& value:canvas) value.EndInteraction();
        if (advanced) {advanced->ClearViewportProbe();advanced->EndViewportTargetDrag();}
    }
    int photoChannel=0; // White selects luminance. This is presentation state only.
    int component=0, selected=-1, dragging=-1;
    float minimum=-16, maximum=16;
    std::array<RawLabInternal::CurveCanvasState,5> canvas;
    std::shared_ptr<ToneCurveLayer> advanced;
    nlohmann::json appliedAdvanced;
};
}
