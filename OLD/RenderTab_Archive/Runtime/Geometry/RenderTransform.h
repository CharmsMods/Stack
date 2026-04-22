#pragma once

#include "RenderMath.h"

struct RenderTransform {
    RenderFloat3 translation {};
    RenderFloat3 rotationDegrees {};
    RenderFloat3 scale { 1.0f, 1.0f, 1.0f };
};

bool NearlyEqual(const RenderTransform& a, const RenderTransform& b, float epsilon = 0.0001f);
RenderFloat3 TransformPoint(const RenderTransform& transform, const RenderFloat3& point);
RenderFloat3 TransformDirection(const RenderTransform& transform, const RenderFloat3& direction);
