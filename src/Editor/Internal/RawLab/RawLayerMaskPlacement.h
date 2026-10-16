#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
// Negative states do not own a gesture. Nonnegative states are active handles
// or the initial placement drag, all committed by the RAW document owner.
inline constexpr int MaskAwaitingPlacement = -2;
inline constexpr int MaskPlacementDrag = 5;

inline void PlaceLayerMask(EditorNodeGraph::MaskGeneratorSettings& settings,
    EditorNodeGraph::MaskGeneratorKind kind, ImVec2 start, ImVec2 point) {
    constexpr float degrees = 180.f / 3.14159265358979323846f;
    const float dx = point.x-start.x, dy = point.y-start.y;
    if (kind == EditorNodeGraph::MaskGeneratorKind::LinearGradient) {
        // The press is zero coverage and the release is full coverage in the
        // generator shader: dot(uv-.5, direction)*scale + .5 + offset.
        settings.angle = std::atan2(dy,dx)*degrees;
        settings.scale = std::clamp(1.f/std::max(.01f,std::hypot(dx,dy)),.1f,100.f);
        const float angle = settings.angle/degrees;
        settings.offset = -(((start.x+point.x)*.5f-.5f)*std::cos(angle)+
            ((start.y+point.y)*.5f-.5f)*std::sin(angle))*settings.scale;
    } else {
        settings.centerX = std::clamp(start.x,-2.f,3.f);
        settings.centerY = std::clamp(start.y,-2.f,3.f);
        settings.angle = 0.f;
        settings.radius = std::clamp(std::abs(dx),.001f,4.f);
        settings.radiusY = std::clamp(std::abs(dy),.001f,4.f);
    }
}
}
