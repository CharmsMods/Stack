#pragma once

#include "ThirdParty/json.hpp"

#include "RenderScene.h"
#include "RenderCamera.h"

#include <filesystem>
#include <string>
#include <vector>

struct RenderSceneSnapshotDocument {
    std::string label;
    std::string description;
    RenderBackgroundMode backgroundMode = RenderBackgroundMode::Gradient;
    bool environmentEnabled = true;
    float environmentIntensity = 1.0f;
    bool fogEnabled = false;
    RenderFloat3 fogColor { 0.82f, 0.88f, 0.96f };
    float fogDensity = 0.0f;
    float fogAnisotropy = 0.0f;
    std::vector<RenderImportedAsset> importedAssets;
    std::vector<RenderImportedTexture> importedTextures;
    std::vector<RenderMaterial> materials;
    std::vector<RenderMeshDefinition> meshes;
    std::vector<RenderMeshInstance> meshInstances;
    std::vector<RenderSphere> spheres;
    std::vector<RenderTriangle> triangles;
    std::vector<RenderLight> lights;
    RenderFloat3 cameraPosition {};
    float cameraYawDegrees = 18.0f;
    float cameraPitchDegrees = -12.0f;
    float cameraFieldOfViewDegrees = 50.0f;
    float cameraFocusDistance = 6.0f;
    float cameraApertureRadius = 0.0f;
    float cameraExposure = 1.0f;
};

namespace RenderSceneSerialization {

nlohmann::json BuildSnapshotJson(const RenderScene& scene, const RenderCamera& camera);
bool ParseSnapshotJson(const nlohmann::json& root, RenderSceneSnapshotDocument& document);

bool WriteSnapshotFile(
    const std::filesystem::path& path,
    const RenderScene& scene,
    const RenderCamera& camera,
    std::string& errorMessage);

bool ReadSnapshotFile(
    const std::filesystem::path& path,
    RenderSceneSnapshotDocument& document,
    std::string& errorMessage);

} // namespace RenderSceneSerialization
