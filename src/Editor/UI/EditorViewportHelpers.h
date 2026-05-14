#pragma once

#include "Editor/EditorModule.h"
#include <array>
#include <imgui.h>

namespace EditorViewportHelpers {

struct AffineTransform2D {
    float m00 = 1.0f;
    float m01 = 0.0f;
    float m02 = 0.0f;
    float m10 = 0.0f;
    float m11 = 1.0f;
    float m12 = 0.0f;
};

AffineTransform2D Inverse(const AffineTransform2D& matrix);
AffineTransform2D BuildLocalTransform(const EditorModule::CompositeSceneItem& item);
ImVec2 TransformPoint(const AffineTransform2D& matrix, const ImVec2& point);
std::array<ImVec2, 4> ComputeSceneQuadWorld(const EditorModule::CompositeSceneItem& item);
ImVec2 WorldToScreen(const ImVec2& canvasCenter, float zoom, float panX, float panY, const ImVec2& worldPoint);
ImVec2 ScreenToWorld(const ImVec2& canvasCenter, float zoom, float panX, float panY, const ImVec2& screenPoint);
bool PointInQuad(const std::array<ImVec2, 4>& quad, const ImVec2& point);
EditorModule::CompositeFloatRect QuadBounds(const std::array<ImVec2, 4>& quad);
EditorModule::CompositeFloatRect MakeNormalizedRect(float x1, float y1, float x2, float y2);
float DistanceToPoint(const ImVec2& a, const ImVec2& b);
ImVec2 Midpoint(const ImVec2& a, const ImVec2& b);
ImVec2 NormalizeOrZero(const ImVec2& value);

} // namespace EditorViewportHelpers
