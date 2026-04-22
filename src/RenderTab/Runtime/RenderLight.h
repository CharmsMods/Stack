#pragma once

#include "RenderTab/Runtime/Geometry/RenderMath.h"
#include "RenderTab/Runtime/Geometry/RenderTransform.h"

#include <string>

enum class RenderLightType {
    RectArea = 0,
    Point,
    Spot,
    Sun
};

struct RenderLight {
    int id = -1;
    std::string name;
    RenderLightType type = RenderLightType::Point;
    RenderTransform transform {};
    RenderFloat3 color { 1.0f, 1.0f, 1.0f };
    float intensity = 10.0f;
    RenderFloat2 areaSize { 1.0f, 1.0f };
    float range = 20.0f;
    float innerConeDegrees = 18.0f;
    float outerConeDegrees = 32.0f;
    bool enabled = true;
};

bool NearlyEqual(const RenderLight& a, const RenderLight& b, float epsilon = 0.0001f);
const char* GetRenderLightTypeLabel(RenderLightType type);
RenderFloat3 GetRenderLightDirection(const RenderLight& light);
RenderFloat3 GetRenderLightUp(const RenderLight& light);
RenderFloat3 GetRenderLightRight(const RenderLight& light);
RenderBounds ComputeBounds(const RenderLight& light);
