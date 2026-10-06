#pragma once

#include "Editor/EditorModuleTypes.h"

namespace Stack::Editor::RawLabInternal {

inline const char* LabToolName(EditorModuleTypes::RawLabTool tool) {
    using Tool = EditorModuleTypes::RawLabTool;
    switch (tool) {
    case Tool::Denoise: return "CFA Denoise";
    case Tool::RgbDenoise: return "RGB Denoise";
    case Tool::Light: return "Camera preparation";
    case Tool::Exposure: return "Exposure";
    case Tool::Zones: return "Local exposure";
    case Tool::Tone: return "Tone curves";
    case Tool::Detail: return "Detail Contrast";
    case Tool::Color: return "Color Warp";
    case Tool::Calibration: return "Calibration";
    case Tool::View: return "View transform";
    case Tool::MultiFrame: return "Bracketing";
    case Tool::Inspect: return "Inspect";
    case Tool::Transform: return "Transform";
    }
    return "Camera";
}

} // namespace Stack::Editor::RawLabInternal
