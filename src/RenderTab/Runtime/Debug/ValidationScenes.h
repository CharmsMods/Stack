#pragma once

#include "RenderTab/Runtime/Geometry/RenderMesh.h"
#include "RenderTab/Runtime/Geometry/RenderSceneGeometry.h"
#include "RenderTab/Runtime/RenderLight.h"
#include "RenderTab/Runtime/Materials/RenderMaterial.h"
#include "RenderTab/Runtime/RenderSceneTypes.h"

#include <vector>

struct RenderValidationSceneOption {
    RenderValidationSceneId id;
    const char* label;
    const char* description;
    RenderBackgroundMode defaultBackground;
    bool defaultEnvironmentEnabled = true;
};

struct RenderValidationSceneTemplate {
    RenderValidationSceneId id {};
    const char* label = "";
    const char* description = "";
    RenderBackgroundMode defaultBackground = RenderBackgroundMode::Gradient;
    bool defaultEnvironmentEnabled = true;
    float defaultEnvironmentIntensity = 1.0f;
    bool defaultFogEnabled = false;
    RenderFloat3 defaultFogColor { 0.82f, 0.88f, 0.96f };
    float defaultFogDensity = 0.0f;
    float defaultFogAnisotropy = 0.0f;
    std::vector<RenderMaterial> materials;
    std::vector<RenderMeshDefinition> meshes;
    std::vector<RenderMeshInstance> meshInstances;
    std::vector<RenderSphere> spheres;
    std::vector<RenderTriangle> triangles;
    std::vector<RenderLight> lights;
};

const std::vector<RenderValidationSceneOption>& GetRenderValidationSceneOptions();
const char* GetRenderValidationSceneLabel(RenderValidationSceneId id);
RenderValidationSceneTemplate BuildRenderValidationScene(RenderValidationSceneId id);
