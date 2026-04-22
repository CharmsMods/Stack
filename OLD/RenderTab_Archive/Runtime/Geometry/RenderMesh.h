#pragma once

#include "RenderSceneGeometry.h"

#include <string>
#include <vector>

struct RenderMeshTriangle {
    std::string name;
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

struct RenderMeshDefinition {
    std::string name;
    int sourceAssetIndex = -1;
    std::string sourceMeshName;
    std::vector<RenderMeshTriangle> triangles;
    RenderBounds localBounds {};
};

struct RenderMeshInstance {
    int id = -1;
    std::string name;
    int meshIndex = -1;
    RenderTransform transform {};
    RenderFloat3 colorTint { 1.0f, 1.0f, 1.0f };
};

bool NearlyEqual(const RenderMeshInstance& a, const RenderMeshInstance& b, float epsilon = 0.0001f);
RenderMeshDefinition BuildRenderMeshDefinition(std::string name, std::vector<RenderMeshTriangle> triangles);
RenderResolvedTriangle ResolveMeshTriangle(const RenderMeshTriangle& triangle, const RenderMeshInstance& instance);
RenderBounds ComputeBounds(const RenderMeshDefinition& mesh);
RenderBounds ComputeBounds(const RenderMeshInstance& instance, const RenderMeshDefinition& mesh);
