#pragma once
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include "Raw/RawImageData.h"

namespace Stack::Editor::RawLabInternal {
// Display coordinates only. Recipes and qualification retain scene-linear OKLab.
inline constexpr float ColorDiscExtent = 0.45f;
inline constexpr float ColorDiscShoulder = 0.12f;
inline std::array<float, 3> ColorDiscDisplayRgb(const std::array<float, 3>& rgb, Raw::RawWorkingSpace space) {
    if (space != Raw::RawWorkingSpace::LinearRec2020D65) return rgb;
    return {1.6604910f*rgb[0]-0.5876411f*rgb[1]-0.0728499f*rgb[2],
        -0.1245505f*rgb[0]+1.1328999f*rgb[1]-0.0083494f*rgb[2],
        -0.0181508f*rgb[0]-0.1005789f*rgb[1]+1.1187297f*rgb[2]};
}
inline ImVec2 ProjectColorDisc(float a, float b) {
    const float radius = std::hypot(a, b);
    const float scale = ColorDiscExtent / (ColorDiscShoulder + radius);
    return ImVec2(a * scale, b * scale);
}
inline ImVec2 UnprojectColorDisc(float a, float b) {
    const float radius = std::hypot(a, b);
    const float scale = ColorDiscShoulder / std::max(0.000001f, ColorDiscExtent - radius);
    return ImVec2(a * scale, b * scale);
}
}
