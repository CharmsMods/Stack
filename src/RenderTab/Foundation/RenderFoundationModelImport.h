#pragma once

#include "RenderFoundationTypes.h"

#include <string>
#include <vector>

namespace RenderFoundation {

struct ImportedModelOptions {
    ImportedModelScaleMode scaleMode = ImportedModelScaleMode::AutoFit;
    float autoFitTargetExtent = 2.0f;
};

struct ImportedModelPayload {
    std::string assetLabel;
    std::vector<RenderImportedAsset> importedAssets;
    std::vector<RenderImportedTexture> importedTextures;
    std::vector<Material> materials;
    std::vector<RenderMeshDefinition> importedMeshes;
    std::vector<Primitive> primitives;
};

struct ImportedModelResult {
    ImportedModelPayload payload;
    ImportedModelDiagnostics diagnostics;
};

bool ImportGltfModel(
    const std::string& filePath,
    const ImportedModelOptions& options,
    ImportedModelResult& outResult,
    std::string& errorMessage);

} // namespace RenderFoundation
