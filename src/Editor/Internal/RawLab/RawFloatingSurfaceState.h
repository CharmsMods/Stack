#pragma once

#include "imgui.h"
#include <map>

namespace Stack::Editor::RawLabInternal {

// One placement per editor, shared by all tools. This is presentation state,
// independent of the image recipe, project history and saved RAW settings.
struct FloatingSurfaceState {
    bool positioned = false;
    float horizontalPlacement = 0.f; // Fraction of available horizontal travel, from 0 to 1.
    float horizontalTravel = 0.f;
    float verticalOffset = 0.f;
    ImVec2 minimum{}, maximum{};
    int frame = -2;
    std::map<int, float> contentHeights;
};

} // namespace Stack::Editor::RawLabInternal
