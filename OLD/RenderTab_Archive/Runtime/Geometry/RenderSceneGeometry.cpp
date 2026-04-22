#include "RenderSceneGeometry.h"

#include <algorithm>
#include <cmath>

namespace {

float MaxAbsScaleComponent(const RenderFloat3& scale) {
    return std::max({ std::fabs(scale.x), std::fabs(scale.y), std::fabs(scale.z) });
}

RenderFloat3 ComputeFaceNormal(const RenderFloat3& a, const RenderFloat3& b, const RenderFloat3& c) {
    return Normalize(Cross(Subtract(b, a), Subtract(c, a)));
}

RenderFloat3 ResolveVertexNormal(
    const RenderTransform& transform,
    const RenderFloat3& localNormal,
    const RenderFloat3& fallbackNormal) {
    if (Length(localNormal) > 0.0001f) {
        return TransformDirection(transform, localNormal);
    }
    return fallbackNormal;
}

} // namespace

RenderResolvedSphere ResolveSphere(const RenderSphere& sphere) {
    RenderResolvedSphere resolved;
    resolved.objectId = sphere.id;
    resolved.center = TransformPoint(sphere.transform, sphere.localCenter);
    resolved.radius = sphere.radius * MaxAbsScaleComponent(sphere.transform.scale);
    resolved.materialIndex = sphere.materialIndex;
    resolved.albedoTint = sphere.albedoTint;
    return resolved;
}

RenderResolvedTriangle ResolveTriangle(const RenderTriangle& triangle) {
    RenderResolvedTriangle resolved;
    resolved.triangleId = triangle.id;
    resolved.a = TransformPoint(triangle.transform, triangle.localA);
    resolved.b = TransformPoint(triangle.transform, triangle.localB);
    resolved.c = TransformPoint(triangle.transform, triangle.localC);
    const RenderFloat3 fallbackNormal = ComputeFaceNormal(resolved.a, resolved.b, resolved.c);
    resolved.normalA = ResolveVertexNormal(triangle.transform, triangle.localNormalA, fallbackNormal);
    resolved.normalB = ResolveVertexNormal(triangle.transform, triangle.localNormalB, fallbackNormal);
    resolved.normalC = ResolveVertexNormal(triangle.transform, triangle.localNormalC, fallbackNormal);
    resolved.uvA = triangle.uvA;
    resolved.uvB = triangle.uvB;
    resolved.uvC = triangle.uvC;
    resolved.materialIndex = triangle.materialIndex;
    resolved.albedoTint = triangle.albedoTint;
    return resolved;
}

RenderBounds ComputeBounds(const RenderResolvedTriangle& triangle) {
    const float minX = std::min({ triangle.a.x, triangle.b.x, triangle.c.x });
    const float minY = std::min({ triangle.a.y, triangle.b.y, triangle.c.y });
    const float minZ = std::min({ triangle.a.z, triangle.b.z, triangle.c.z });
    const float maxX = std::max({ triangle.a.x, triangle.b.x, triangle.c.x });
    const float maxY = std::max({ triangle.a.y, triangle.b.y, triangle.c.y });
    const float maxZ = std::max({ triangle.a.z, triangle.b.z, triangle.c.z });

    return RenderBounds {
        MakeRenderFloat3(minX, minY, minZ),
        MakeRenderFloat3(maxX, maxY, maxZ)
    };
}

RenderBounds ComputeBounds(const RenderSphere& sphere) {
    const RenderResolvedSphere resolved = ResolveSphere(sphere);
    return RenderBounds {
        MakeRenderFloat3(
            resolved.center.x - resolved.radius,
            resolved.center.y - resolved.radius,
            resolved.center.z - resolved.radius),
        MakeRenderFloat3(
            resolved.center.x + resolved.radius,
            resolved.center.y + resolved.radius,
            resolved.center.z + resolved.radius)
    };
}

RenderBounds ComputeBounds(const RenderTriangle& triangle) {
    return ComputeBounds(ResolveTriangle(triangle));
}

RenderBounds UnionBounds(const RenderBounds& a, const RenderBounds& b) {
    return RenderBounds {
        MakeRenderFloat3(
            std::min(a.min.x, b.min.x),
            std::min(a.min.y, b.min.y),
            std::min(a.min.z, b.min.z)),
        MakeRenderFloat3(
            std::max(a.max.x, b.max.x),
            std::max(a.max.y, b.max.y),
            std::max(a.max.z, b.max.z))
    };
}

RenderFloat3 BoundsCentroid(const RenderBounds& bounds) {
    return MakeRenderFloat3(
        (bounds.min.x + bounds.max.x) * 0.5f,
        (bounds.min.y + bounds.max.y) * 0.5f,
        (bounds.min.z + bounds.max.z) * 0.5f);
}

RenderFloat3 BoundsExtent(const RenderBounds& bounds) {
    return MakeRenderFloat3(
        bounds.max.x - bounds.min.x,
        bounds.max.y - bounds.min.y,
        bounds.max.z - bounds.min.z);
}

const char* GetRenderPrimitiveTypeLabel(RenderPrimitiveType type) {
    switch (type) {
    case RenderPrimitiveType::Sphere:
        return "Sphere";
    case RenderPrimitiveType::Triangle:
        return "Triangle";
    }

    return "Unknown";
}
