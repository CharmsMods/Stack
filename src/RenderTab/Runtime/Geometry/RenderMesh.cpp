#include "RenderMesh.h"

#include <algorithm>
#include <utility>

namespace {

RenderFloat3 MultiplyColor(const RenderFloat3& a, const RenderFloat3& b) {
    return MakeRenderFloat3(a.x * b.x, a.y * b.y, a.z * b.z);
}

RenderFloat3 ComputeFaceNormal(const RenderFloat3& a, const RenderFloat3& b, const RenderFloat3& c) {
    return Normalize(Cross(Subtract(b, a), Subtract(c, a)));
}

RenderFloat3 ResolveVertexNormal(
    const RenderMeshTriangle& triangle,
    const RenderMeshInstance& instance,
    const RenderFloat3& fallbackNormal,
    int vertexIndex) {
    const RenderFloat3* normal = nullptr;
    switch (vertexIndex) {
    case 0:
        normal = &triangle.localNormalA;
        break;
    case 1:
        normal = &triangle.localNormalB;
        break;
    case 2:
        normal = &triangle.localNormalC;
        break;
    }

    if (normal != nullptr && Length(*normal) > 0.0001f) {
        return TransformDirection(instance.transform, *normal);
    }
    return fallbackNormal;
}

RenderBounds ComputeLocalTriangleBounds(const RenderMeshTriangle& triangle) {
    const float minX = std::min({ triangle.localA.x, triangle.localB.x, triangle.localC.x });
    const float minY = std::min({ triangle.localA.y, triangle.localB.y, triangle.localC.y });
    const float minZ = std::min({ triangle.localA.z, triangle.localB.z, triangle.localC.z });
    const float maxX = std::max({ triangle.localA.x, triangle.localB.x, triangle.localC.x });
    const float maxY = std::max({ triangle.localA.y, triangle.localB.y, triangle.localC.y });
    const float maxZ = std::max({ triangle.localA.z, triangle.localB.z, triangle.localC.z });

    return RenderBounds {
        MakeRenderFloat3(minX, minY, minZ),
        MakeRenderFloat3(maxX, maxY, maxZ)
    };
}

} // namespace

bool NearlyEqual(const RenderMeshInstance& a, const RenderMeshInstance& b, float epsilon) {
    return a.name == b.name &&
        a.meshIndex == b.meshIndex &&
        NearlyEqual(a.transform, b.transform, epsilon) &&
        NearlyEqual(a.colorTint, b.colorTint, epsilon);
}

RenderMeshDefinition BuildRenderMeshDefinition(std::string name, std::vector<RenderMeshTriangle> triangles) {
    RenderMeshDefinition mesh;
    mesh.name = std::move(name);
    mesh.triangles = std::move(triangles);
    mesh.localBounds = ComputeBounds(mesh);
    return mesh;
}

RenderResolvedTriangle ResolveMeshTriangle(const RenderMeshTriangle& triangle, const RenderMeshInstance& instance) {
    RenderResolvedTriangle resolved;
    resolved.meshInstanceId = instance.id;
    resolved.a = TransformPoint(instance.transform, triangle.localA);
    resolved.b = TransformPoint(instance.transform, triangle.localB);
    resolved.c = TransformPoint(instance.transform, triangle.localC);
    const RenderFloat3 fallbackNormal = ComputeFaceNormal(resolved.a, resolved.b, resolved.c);
    resolved.normalA = ResolveVertexNormal(triangle, instance, fallbackNormal, 0);
    resolved.normalB = ResolveVertexNormal(triangle, instance, fallbackNormal, 1);
    resolved.normalC = ResolveVertexNormal(triangle, instance, fallbackNormal, 2);
    resolved.uvA = triangle.uvA;
    resolved.uvB = triangle.uvB;
    resolved.uvC = triangle.uvC;
    resolved.materialIndex = triangle.materialIndex;
    resolved.albedoTint = MultiplyColor(triangle.albedoTint, instance.colorTint);
    return resolved;
}

RenderBounds ComputeBounds(const RenderMeshDefinition& mesh) {
    if (mesh.triangles.empty()) {
        return RenderBounds {
            MakeRenderFloat3(0.0f, 0.0f, 0.0f),
            MakeRenderFloat3(0.0f, 0.0f, 0.0f)
        };
    }

    RenderBounds bounds = ComputeLocalTriangleBounds(mesh.triangles.front());
    for (std::size_t i = 1; i < mesh.triangles.size(); ++i) {
        bounds = UnionBounds(bounds, ComputeLocalTriangleBounds(mesh.triangles[i]));
    }
    return bounds;
}

RenderBounds ComputeBounds(const RenderMeshInstance& instance, const RenderMeshDefinition& mesh) {
    if (mesh.triangles.empty()) {
        const RenderFloat3 worldOrigin = TransformPoint(instance.transform, MakeRenderFloat3(0.0f, 0.0f, 0.0f));
        return RenderBounds { worldOrigin, worldOrigin };
    }

    RenderBounds bounds = ComputeBounds(ResolveMeshTriangle(mesh.triangles.front(), instance));
    for (std::size_t i = 1; i < mesh.triangles.size(); ++i) {
        bounds = UnionBounds(bounds, ComputeBounds(ResolveMeshTriangle(mesh.triangles[i], instance)));
    }
    return bounds;
}
