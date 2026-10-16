#pragma once
#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Editor/Layers/ToneLayers.h"
#include <array>
#include <memory>

namespace Stack::Editor {
struct ToneCurveEditorState {
    int component=0, selected=-1, dragging=-1;
    float minimum=-16, maximum=16;
    std::array<RawLabInternal::CurveCanvasState,5> canvas;
    std::array<RawLabInternal::RawLabToneGraphViewRange,5> fitted;
    std::array<std::uint64_t,5> fittedRevision {};
    std::array<bool,5> fittedValid {};
    float zoom=0;
    std::shared_ptr<ToneCurveLayer> advanced;
    nlohmann::json appliedAdvanced;
};
}
