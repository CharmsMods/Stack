#pragma once

#include "imgui.h"
#include "Utils/FloatingSurfaceInput.h"
#include "Editor/Internal/RawLab/RawFloatingInputTrace.h"
#include "Editor/Internal/RawLab/RawLabHistogramCache.h"
#include <map>

namespace Stack::Editor::RawLabInternal {

// One placement per editor, shared by all tools. This is presentation state,
// independent of the image recipe, project history and saved RAW settings.
struct FloatingSurfaceState {
    bool positioned = false;
    float horizontalPlacement = 0.f; // Fraction of available horizontal travel, from 0 to 1.
    float horizontalTravel = 0.f;
    float verticalOffset = 0.f;
    ImVec2 pointerOffset{}; // Retain the grabbed point throughout a move.
    ImVec2 minimum{}, maximum{};
    int frame = -2;
    bool imageNavigationMenuOpen = false;
    std::map<std::string, float> contentHeights;
    ImGuiExtras::FloatingSurfaceInput input;
    RawFloatingTraceState trace;
    RawLabHistogramCache histogram;
};

} // namespace Stack::Editor::RawLabInternal
