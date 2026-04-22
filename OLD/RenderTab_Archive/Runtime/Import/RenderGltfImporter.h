#pragma once

#include "RenderTab/Runtime/Assets/RenderImportedAsset.h"
#include "RenderTab/Runtime/Geometry/RenderMesh.h"
#include "RenderTab/Runtime/Geometry/RenderSceneGeometry.h"
#include "RenderTab/Runtime/Materials/RenderMaterial.h"
#include "RenderTab/Runtime/RenderSceneTypes.h"

#include <filesystem>
#include <string>
#include <vector>

struct RenderImportResult {
    std::string label;
    std::string description;
    RenderBackgroundMode backgroundMode = RenderBackgroundMode::Black;
    std::vector<RenderImportedAsset> importedAssets;
    std::vector<RenderImportedTexture> importedTextures;
    std::vector<RenderMaterial> materials;
    std::vector<RenderMeshDefinition> meshes;
    std::vector<RenderMeshInstance> meshInstances;
    std::vector<RenderSphere> spheres;
    std::vector<RenderTriangle> triangles;
};

namespace RenderGltfImporter {

bool ImportScene(
    const std::filesystem::path& path,
    RenderImportResult& result,
    std::string& errorMessage);

} // namespace RenderGltfImporter
