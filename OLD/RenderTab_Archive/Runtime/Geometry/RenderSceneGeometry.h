#pragma once

#include "RenderMath.h"
#include "RenderTransform.h"

#include <string>

enum class RenderPrimitiveType {
    Sphere = 0,
    Triangle
};

struct RenderSphere {
    int id = -1;
    std::string name;
    RenderTransform transform {};
    RenderFloat3 localCenter {};
    float radius = 1.0f;
    int materialIndex = 0;
    RenderFloat3 albedoTint { 0.8f, 0.8f, 0.8f };
};

struct RenderTriangle {
    int id = -1;
    std::string name;
    RenderTransform transform {};
    RenderFloat3 localA {};
    RenderFloat3 localB {};
    RenderFloat3 localC {};
    RenderFloat3 localNormalA {};
    RenderFloat3 localNormalB {};
    RenderFloat3 localNormalC {};
    RenderFloat2 uvA {};
    RenderFloat2 uvB { 1.0f, 0.0f };
    RenderFloat2 uvC { 0.0f, 1.0f };
    int materialIndex = 0;
    RenderFloat3 albedoTint { 0.8f, 0.8f, 0.8f };
};

struct RenderResolvedSphere {
    int objectId = -1;
    RenderFloat3 center {};
    float radius = 1.0f;
    int materialIndex = 0;
    RenderFloat3 albedoTint { 0.8f, 0.8f, 0.8f };
};

struct RenderResolvedTriangle {
    int triangleId = -1;
    int meshInstanceId = -1;
    RenderFloat3 a {};
    RenderFloat3 b {};
    RenderFloat3 c {};
    RenderFloat3 normalA {};
    RenderFloat3 normalB {};
    RenderFloat3 normalC {};
    RenderFloat2 uvA {};
    RenderFloat2 uvB {};
    RenderFloat2 uvC {};
    int materialIndex = 0;
    RenderFloat3 albedoTint { 0.8f, 0.8f, 0.8f };
};

struct RenderPrimitiveRef {
    RenderPrimitiveType type = RenderPrimitiveType::Sphere;
    int index = -1;
    RenderBounds bounds {};
};

RenderResolvedSphere ResolveSphere(const RenderSphere& sphere);
RenderResolvedTriangle ResolveTriangle(const RenderTriangle& triangle);
RenderBounds ComputeBounds(const RenderResolvedTriangle& triangle);
RenderBounds ComputeBounds(const RenderSphere& sphere);
RenderBounds ComputeBounds(const RenderTriangle& triangle);
RenderBounds UnionBounds(const RenderBounds& a, const RenderBounds& b);
RenderFloat3 BoundsCentroid(const RenderBounds& bounds);
RenderFloat3 BoundsExtent(const RenderBounds& bounds);
const char* GetRenderPrimitiveTypeLabel(RenderPrimitiveType type);
