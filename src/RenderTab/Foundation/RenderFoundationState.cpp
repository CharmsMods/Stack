#include "RenderFoundationState.h"

#include "RenderFoundationModelImport.h"
#include "RenderTab/Contracts/RenderContracts.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace RenderFoundation {

namespace {

Material BuildDefaultMaterial(Id id, BaseMaterial preset, const std::string& name, const Vec3& color, bool isTemplate = false) {
    Material material;
    material.id = id;
    material.name = name;
    material.isTemplate = isTemplate;
    material.baseMaterial = preset;
    material.baseColor = color;

    switch (preset) {
        case BaseMaterial::Diffuse:
            material.roughness = 0.65f;
            break;
        case BaseMaterial::Metal:
            material.roughness = 0.18f;
            material.metallic = 1.0f;
            break;
        case BaseMaterial::Glass:
            material.roughness = 0.02f;
            material.transmission = 1.0f;
            material.ior = 1.52f;
            material.absorptionDistance = 2.0f;
            break;
        case BaseMaterial::Emissive:
            material.emissionStrength = 12.0f;
            material.emissionColor = color;
            material.roughness = 0.1f;
            break;
    }

    SyncMaterialLayersFromLegacy(material);

    return material;
}

Primitive BuildPrimitive(Id id, PrimitiveType type, Id materialId, const std::string& name) {
    Primitive primitive;
    primitive.id = id;
    primitive.type = type;
    primitive.materialId = materialId;
    primitive.name = name;
    return primitive;
}

Light BuildLight(Id id, LightType type, const std::string& name) {
    Light light;
    light.id = id;
    light.type = type;
    light.name = name;
    return light;
}

} // namespace

State::State() {
    ResetToDefaultScene();
}

void State::ResetToDefaultScene() {
    m_NextId = 1;
    m_Metadata = SceneMetadata {};
    m_Metadata.label = "Cornell Box";
    m_Metadata.description = "Warm Cornell box baseline with an emissive ceiling panel and diffuse, metal, and glass starter materials.";
    m_Metadata.backgroundMode = 3; // Black
    m_Metadata.environmentEnabled = false;
    m_Metadata.environmentIntensity = 1.0f;
    m_Metadata.fogEnabled = false;
    m_Settings = Settings {};
    m_Camera = Camera {};
    m_Selection = Selection {};
    m_ImportedAssets.clear();
    m_ImportedTextures.clear();
    m_ImportedMeshes.clear();
    m_Materials.clear();
    m_Primitives.clear();
    m_Lights.clear();
    m_ProjectName.clear();
    m_ProjectFileName.clear();
    m_AccumulationManager.ResetForScene(m_Settings);
    m_SceneRevision += 1;
    m_HasUnsavedChanges = false;
    m_LastChangeReason = "Cornell box baseline loaded.";
    SeedDefaultScene();
}

Snapshot State::BuildSnapshot() const {
    Snapshot snapshot;
    snapshot.cameraId = RenderContracts::kCameraObjectId;
    snapshot.metadata = m_Metadata;
    snapshot.importedAssets = m_ImportedAssets;
    snapshot.importedTextures = m_ImportedTextures;
    snapshot.importedMeshes = m_ImportedMeshes;
    snapshot.materials = m_Materials;
    snapshot.primitives = m_Primitives;
    snapshot.lights = m_Lights;
    snapshot.camera = m_Camera;
    return snapshot;
}

void State::ApplyLoadedState(
    Snapshot snapshot,
    Settings settings,
    const std::string& projectName,
    const std::string& projectFileName) {

    m_Metadata = std::move(snapshot.metadata);
    m_ImportedAssets = std::move(snapshot.importedAssets);
    m_ImportedTextures = std::move(snapshot.importedTextures);
    m_ImportedMeshes = std::move(snapshot.importedMeshes);
    m_Materials = std::move(snapshot.materials);
    m_Primitives = std::move(snapshot.primitives);
    m_Lights = std::move(snapshot.lights);
    m_Camera = snapshot.camera;
    m_Settings = settings;
    m_Selection = Selection {};
    RefreshNextId();
    m_ProjectName = projectName;
    m_ProjectFileName = projectFileName;
    m_SceneRevision += 1;
    m_HasUnsavedChanges = false;
    m_AccumulationManager.ResetForScene(m_Settings);
    ApplyChangeSet({ RenderContracts::DirtyFlags::SceneStructure, RenderContracts::ResetClass::FullAccumulation, "Render project loaded." });
    m_LastChangeReason = "Render project loaded.";

    for (Material& material : m_Materials) {
        if (material.layers.empty()) {
            SyncMaterialLayersFromLegacy(material);
        } else {
            SyncLegacyMaterialFromLayers(material);
        }
    }

    if (m_Materials.empty()) {
        const Id fallbackMaterial = AddMaterial(
            BuildDefaultMaterial(AllocateId(), BaseMaterial::Diffuse, "Warm Diffuse", { 0.86f, 0.78f, 0.64f }, true));
        for (Primitive& primitive : m_Primitives) {
            primitive.materialId = fallbackMaterial;
        }
    }
}

void State::SelectNone() {
    m_Selection = Selection {};
}

void State::SelectPrimitive(Id id) {
    m_Selection.type = SelectionType::Primitive;
    m_Selection.id = id;
}

void State::SelectLight(Id id) {
    m_Selection.type = SelectionType::Light;
    m_Selection.id = id;
}

void State::SelectCamera() {
    m_Selection.type = SelectionType::Camera;
    m_Selection.id = RenderContracts::kCameraObjectId;
}

Material* State::FindMaterial(Id id) {
    auto it = std::find_if(m_Materials.begin(), m_Materials.end(), [id](const Material& material) {
        return material.id == id;
    });
    return it == m_Materials.end() ? nullptr : &(*it);
}

const Material* State::FindMaterial(Id id) const {
    auto it = std::find_if(m_Materials.begin(), m_Materials.end(), [id](const Material& material) {
        return material.id == id;
    });
    return it == m_Materials.end() ? nullptr : &(*it);
}

Primitive* State::FindPrimitive(Id id) {
    auto it = std::find_if(m_Primitives.begin(), m_Primitives.end(), [id](const Primitive& primitive) {
        return primitive.id == id;
    });
    return it == m_Primitives.end() ? nullptr : &(*it);
}

const Primitive* State::FindPrimitive(Id id) const {
    auto it = std::find_if(m_Primitives.begin(), m_Primitives.end(), [id](const Primitive& primitive) {
        return primitive.id == id;
    });
    return it == m_Primitives.end() ? nullptr : &(*it);
}

Light* State::FindLight(Id id) {
    auto it = std::find_if(m_Lights.begin(), m_Lights.end(), [id](const Light& light) {
        return light.id == id;
    });
    return it == m_Lights.end() ? nullptr : &(*it);
}

const Light* State::FindLight(Id id) const {
    auto it = std::find_if(m_Lights.begin(), m_Lights.end(), [id](const Light& light) {
        return light.id == id;
    });
    return it == m_Lights.end() ? nullptr : &(*it);
}

int State::CountMaterialUsers(Id materialId) const {
    return static_cast<int>(std::count_if(m_Primitives.begin(), m_Primitives.end(), [materialId](const Primitive& primitive) {
        return primitive.materialId == materialId;
    }));
}

Material* State::DuplicateMaterialForPrimitive(Id primitiveId, const std::string& requestedName) {
    Primitive* primitive = FindPrimitive(primitiveId);
    if (primitive == nullptr) {
        return nullptr;
    }

    Material* sourceMaterial = FindMaterial(primitive->materialId);
    if (sourceMaterial == nullptr) {
        Material fallback = BuildDefaultMaterial(
            0,
            BaseMaterial::Diffuse,
            MakeUniqueMaterialName(primitive->name.empty() ? "Material" : primitive->name + " Material"),
            { 0.8f, 0.8f, 0.8f },
            false);
        const Id fallbackId = AddMaterial(std::move(fallback));
        primitive->materialId = fallbackId;
        return FindMaterial(fallbackId);
    }

    Material materialCopy = *sourceMaterial;
    materialCopy.id = 0;
    materialCopy.isTemplate = false;
    materialCopy.name = MakeUniqueMaterialName(
        requestedName.empty()
            ? (primitive->name.empty() ? sourceMaterial->name + " Copy" : primitive->name + " Material")
            : requestedName);

    const Id duplicatedId = AddMaterial(std::move(materialCopy));
    primitive->materialId = duplicatedId;
    return FindMaterial(duplicatedId);
}

Material* State::EnsureEditableMaterialForPrimitive(Id primitiveId) {
    Primitive* primitive = FindPrimitive(primitiveId);
    if (primitive == nullptr) {
        return nullptr;
    }

    Material* material = FindMaterial(primitive->materialId);
    if (material == nullptr) {
        return DuplicateMaterialForPrimitive(primitiveId);
    }

    if (!material->isTemplate && CountMaterialUsers(material->id) <= 1) {
        return material;
    }

    return DuplicateMaterialForPrimitive(primitiveId);
}

bool State::ImportModelFromFile(
    const std::string& filePath,
    const ImportedModelOptions& options,
    ImportedModelResult& outResult,
    std::string& errorMessage) const {
    outResult = {};
    return ImportGltfModel(filePath, options, outResult, errorMessage);
}

bool State::ApplyImportedModelResult(
    ImportedModelResult result,
    Id* outPrimitiveId,
    ImportedModelDiagnostics* outDiagnostics) {
    if (outPrimitiveId != nullptr) {
        *outPrimitiveId = 0;
    }
    if (outDiagnostics != nullptr) {
        *outDiagnostics = result.diagnostics;
    }

    ImportedModelPayload& payload = result.payload;
    if (payload.importedMeshes.empty() || payload.primitives.empty()) {
        return false;
    }

    const int assetBaseIndex = static_cast<int>(m_ImportedAssets.size());
    const int textureBaseIndex = static_cast<int>(m_ImportedTextures.size());
    const int meshBaseIndex = static_cast<int>(m_ImportedMeshes.size());
    const int materialBaseIndex = static_cast<int>(m_Materials.size());

    for (RenderImportedAsset& asset : payload.importedAssets) {
        m_ImportedAssets.push_back(std::move(asset));
    }

    for (RenderImportedTexture& texture : payload.importedTextures) {
        if (texture.sourceAssetIndex >= 0) {
            texture.sourceAssetIndex += assetBaseIndex;
        }
        m_ImportedTextures.push_back(std::move(texture));
    }

    std::vector<Id> importedMaterialIds;
    importedMaterialIds.reserve(payload.materials.size());
    for (Material& material : payload.materials) {
        if (material.sourceAssetIndex >= 0) {
            material.sourceAssetIndex += assetBaseIndex;
        }
        if (material.baseColorTexture.textureIndex >= 0) {
            material.baseColorTexture.textureIndex += textureBaseIndex;
        }
        if (material.metallicRoughnessTexture.textureIndex >= 0) {
            material.metallicRoughnessTexture.textureIndex += textureBaseIndex;
        }
        if (material.emissiveTexture.textureIndex >= 0) {
            material.emissiveTexture.textureIndex += textureBaseIndex;
        }
        if (material.normalTexture.textureIndex >= 0) {
            material.normalTexture.textureIndex += textureBaseIndex;
        }
        importedMaterialIds.push_back(AddMaterial(std::move(material)));
    }

    const Id defaultImportedMaterialId = !importedMaterialIds.empty()
        ? importedMaterialIds.front()
        : ResolveMaterialId(0);

    for (RenderMeshDefinition& mesh : payload.importedMeshes) {
        if (mesh.sourceAssetIndex >= 0) {
            mesh.sourceAssetIndex += assetBaseIndex;
        }
        for (RenderMeshTriangle& triangle : mesh.triangles) {
            triangle.materialIndex = std::max(triangle.materialIndex + materialBaseIndex, 0);
        }
        m_ImportedMeshes.push_back(std::move(mesh));
    }

    Id firstImportedPrimitiveId = 0;
    for (Primitive& primitive : payload.primitives) {
        primitive.id = AllocateId();
        primitive.materialId = defaultImportedMaterialId;
        if (primitive.meshIndex >= 0) {
            primitive.meshIndex += meshBaseIndex;
        }
        m_Primitives.push_back(std::move(primitive));
        if (firstImportedPrimitiveId == 0) {
            firstImportedPrimitiveId = m_Primitives.back().id;
        }
    }

    if (firstImportedPrimitiveId != 0) {
        SelectPrimitive(firstImportedPrimitiveId);
    }

    if (outPrimitiveId != nullptr) {
        *outPrimitiveId = firstImportedPrimitiveId;
    }

    MarkTransportDirty(
        "Model imported.",
        RenderContracts::ResetClass::FullAccumulation,
        RenderContracts::DirtyFlags::SceneStructure | RenderContracts::DirtyFlags::SceneContent | RenderContracts::DirtyFlags::Viewport);
    return true;
}

Id State::AddPrimitive(PrimitiveType type) {
    const Id primitiveId = AllocateId();
    const Id defaultMaterialId = ResolveMaterialId(m_Materials.empty() ? 0 : m_Materials.front().id);

    Primitive primitive;
    switch (type) {
        case PrimitiveType::Sphere:
            primitive = BuildPrimitive(primitiveId, type, defaultMaterialId, "Sphere");
            primitive.transform.translation = { -1.2f, 0.35f, 0.0f };
            primitive.transform.scale = { 1.3f, 1.3f, 1.3f };
            break;
        case PrimitiveType::Cube:
            primitive = BuildPrimitive(primitiveId, type, defaultMaterialId, "Cube");
            primitive.transform.translation = { 1.5f, 0.25f, 0.6f };
            primitive.transform.rotationDegrees = { 0.0f, 25.0f, 0.0f };
            primitive.transform.scale = { 1.1f, 1.1f, 1.1f };
            break;
        case PrimitiveType::Plane:
            primitive = BuildPrimitive(primitiveId, type, defaultMaterialId, "Plane");
            primitive.transform.translation = { 0.0f, -0.9f, 0.0f };
            primitive.transform.scale = { 8.0f, 1.0f, 8.0f };
            break;
        case PrimitiveType::ImportedMesh:
            primitive = BuildPrimitive(primitiveId, PrimitiveType::Cube, defaultMaterialId, "Imported Mesh");
            break;
    }

    m_Primitives.push_back(primitive);
    SelectPrimitive(primitiveId);
    MarkTransportDirty("Primitive added.");
    return primitiveId;
}

Id State::AddLight(LightType type) {
    const Id lightId = AllocateId();
    Light light;
    switch (type) {
        case LightType::Point:
            light = BuildLight(lightId, type, "Point Light");
            light.transform.translation = { 0.0f, 2.8f, 1.8f };
            light.intensity = 35.0f;
            break;
        case LightType::Spot:
            light = BuildLight(lightId, type, "Spot Light");
            light.transform.translation = { -2.6f, 3.0f, 2.8f };
            light.transform.rotationDegrees = { -45.0f, 20.0f, 0.0f };
            light.intensity = 45.0f;
            break;
        case LightType::Area:
            light = BuildLight(lightId, type, "Area Light");
            light.transform.translation = { 0.0f, 3.4f, 0.0f };
            light.areaSize = { 1.75f, 1.2f };
            light.intensity = 22.0f;
            break;
        case LightType::Directional:
            light = BuildLight(lightId, type, "Directional Light");
            light.transform.rotationDegrees = { -38.0f, 24.0f, 0.0f };
            light.intensity = 6.0f;
            break;
        case LightType::Laser:
            light = BuildLight(lightId, type, "Laser");
            light.transform.translation = { -2.8f, 1.35f, -2.4f };
            light.transform.rotationDegrees = { 0.0f, -90.0f, 0.0f };
            light.color = { 0.18f, 1.0f, 0.24f };
            light.intensity = 12.0f;
            light.range = 20.0f;
            light.laserWavelengthNm = 532.0f;
            light.laserLinewidthNm = 1.0f;
            light.laserApertureRadius = 0.015f;
            light.laserBeamWaistRadius = 0.0025f;
            light.laserBeamQuality = 1.1f;
            break;
    }

    m_Lights.push_back(light);
    SelectLight(lightId);
    MarkTransportDirty("Light added.");
    return lightId;
}

bool State::DeleteSelection() {
    if (m_Selection.type == SelectionType::Primitive) {
        const auto oldSize = m_Primitives.size();
        m_Primitives.erase(
            std::remove_if(m_Primitives.begin(), m_Primitives.end(), [this](const Primitive& primitive) {
                return primitive.id == m_Selection.id;
            }),
            m_Primitives.end());
        if (m_Primitives.size() != oldSize) {
            SelectNone();
            MarkTransportDirty("Primitive deleted.");
            return true;
        }
    } else if (m_Selection.type == SelectionType::Light) {
        const auto oldSize = m_Lights.size();
        m_Lights.erase(
            std::remove_if(m_Lights.begin(), m_Lights.end(), [this](const Light& light) {
                return light.id == m_Selection.id;
            }),
            m_Lights.end());
        if (m_Lights.size() != oldSize) {
            SelectNone();
            MarkTransportDirty("Light deleted.");
            return true;
        }
    }

    return false;
}

void State::MarkTransportDirty(
    const std::string& reason,
    RenderContracts::ResetClass resetClass,
    RenderContracts::DirtyFlags dirtyFlags) {

    m_HasUnsavedChanges = true;
    m_LastChangeReason = reason;
    m_SceneRevision += 1;
    ApplyChangeSet({
        dirtyFlags,
        resetClass,
        reason
    });
}

void State::MarkDisplayDirty(const std::string& reason) {
    m_HasUnsavedChanges = true;
    m_LastChangeReason = reason;
    ApplyChangeSet({
        RenderContracts::DirtyFlags::Display,
        RenderContracts::ResetClass::DisplayOnly,
        reason
    });
}

void State::TickFrame() {
    m_AccumulationManager.TickFrame(m_Settings);
}

void State::ApplyExternalChange(const RenderContracts::SceneChangeSet& changeSet) {
    if (changeSet.resetClass == RenderContracts::ResetClass::None) {
        return;
    }

    m_HasUnsavedChanges = true;
    if (!changeSet.reason.empty()) {
        m_LastChangeReason = changeSet.reason;
    }
    if (changeSet.resetClass != RenderContracts::ResetClass::DisplayOnly) {
        m_SceneRevision += 1;
    }
    ApplyChangeSet(changeSet);
}

void State::MarkSaved(const std::string& projectName, const std::string& projectFileName) {
    m_ProjectName = projectName;
    m_ProjectFileName = projectFileName;
    m_HasUnsavedChanges = false;
    m_LastChangeReason = "Render project saved.";
}

Id State::AllocateId() {
    return m_NextId++;
}

Id State::AddMaterial(Material material) {
    if (material.layers.empty()) {
        SyncMaterialLayersFromLegacy(material);
    } else {
        SyncLegacyMaterialFromLayers(material);
    }
    if (material.id == 0) {
        material.id = AllocateId();
    }
    const Id id = material.id;
    m_Materials.push_back(std::move(material));
    return id;
}

void State::SeedDefaultScene() {
    const Id warmDiffuseId = AddMaterial(BuildDefaultMaterial(AllocateId(), BaseMaterial::Diffuse, "Warm Diffuse", { 0.86f, 0.78f, 0.64f }, true));
    const Id brassMetalId = AddMaterial(BuildDefaultMaterial(AllocateId(), BaseMaterial::Metal, "Brass Metal", { 0.92f, 0.80f, 0.48f }, true));
    const Id amberGlassId = AddMaterial(BuildDefaultMaterial(AllocateId(), BaseMaterial::Glass, "Amber Glass", { 0.95f, 0.92f, 0.78f }, true));
    Material ceilingLight = BuildDefaultMaterial(AllocateId(), BaseMaterial::Emissive, "Ceiling Light", { 1.0f, 0.91f, 0.74f }, true);
    ceilingLight.emissionStrength = 15.0f;
    const Id ceilingLightId = AddMaterial(std::move(ceilingLight));
    (void)ceilingLightId;

    auto addPlane = [&](const std::string& name, Id materialId, const Vec3& translation, const Vec3& rotationDegrees, const Vec3& scale) {
        Primitive primitive = BuildPrimitive(AllocateId(), PrimitiveType::Plane, materialId, name);
        primitive.transform.translation = translation;
        primitive.transform.rotationDegrees = rotationDegrees;
        primitive.transform.scale = scale;
        m_Primitives.push_back(std::move(primitive));
    };

    // Cornell-style room baseline. Only the top emitter is a light source.
    addPlane("Floor", warmDiffuseId, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 5.5f, 1.0f, 5.5f });
    addPlane("Ceiling", warmDiffuseId, { 0.0f, 5.5f, 0.0f }, { 180.0f, 0.0f, 0.0f }, { 5.5f, 1.0f, 5.5f });
    addPlane("Back Wall", warmDiffuseId, { 0.0f, 2.75f, 2.75f }, { -90.0f, 0.0f, 0.0f }, { 5.5f, 1.0f, 5.5f });
    addPlane("Left Wall", warmDiffuseId, { -2.75f, 2.75f, 0.0f }, { 0.0f, 0.0f, -90.0f }, { 5.5f, 1.0f, 5.5f });
    addPlane("Right Wall", warmDiffuseId, { 2.75f, 2.75f, 0.0f }, { 0.0f, 0.0f, 90.0f }, { 5.5f, 1.0f, 5.5f });
    addPlane("Ceiling Light", ceilingLightId, { 0.0f, 5.49f, 0.0f }, { 180.0f, 0.0f, 0.0f }, { 1.35f, 1.0f, 1.1f });

    Primitive metalCube = BuildPrimitive(AllocateId(), PrimitiveType::Cube, brassMetalId, "Metal Cube");
    metalCube.transform.translation = { -1.05f, 1.65f, 0.80f };
    metalCube.transform.rotationDegrees = { 0.0f, 15.0f, 0.0f };
    metalCube.transform.scale = { 1.0f, 3.3f, 1.0f };
    m_Primitives.push_back(metalCube);

    Primitive glassSphere = BuildPrimitive(AllocateId(), PrimitiveType::Sphere, amberGlassId, "Glass Sphere");
    glassSphere.transform.translation = { 1.15f, 0.80f, -0.65f };
    glassSphere.transform.scale = { 1.6f, 1.6f, 1.6f };
    m_Primitives.push_back(glassSphere);

    m_Camera.position = { 0.0f, 2.6f, -8.2f };
    m_Camera.yawDegrees = 90.0f;
    m_Camera.pitchDegrees = -6.0f;
    m_Camera.fieldOfViewDegrees = 80.0f;
    m_Camera.focusDistance = 8.2f;
    m_Camera.apertureRadius = 0.0f;

    SelectCamera();
}

void State::RefreshNextId() {
    Id maxId = 0;
    for (const Material& material : m_Materials) {
        maxId = std::max(maxId, material.id);
    }
    for (const Primitive& primitive : m_Primitives) {
        maxId = std::max(maxId, primitive.id);
    }
    for (const Light& light : m_Lights) {
        maxId = std::max(maxId, light.id);
    }
    m_NextId = maxId + 1;
}

Id State::ResolveMaterialId(Id requestedId) const {
    if (requestedId != 0) {
        const Material* material = FindMaterial(requestedId);
        if (material != nullptr) {
            return requestedId;
        }
    }

    return m_Materials.empty() ? 0 : m_Materials.front().id;
}

std::string State::MakeUniqueMaterialName(const std::string& requestedName) const {
    const std::string baseName = requestedName.empty() ? "Material" : requestedName;
    auto nameExists = [this](const std::string& candidate) {
        return std::any_of(m_Materials.begin(), m_Materials.end(), [&candidate](const Material& material) {
            return material.name == candidate;
        });
    };

    if (!nameExists(baseName)) {
        return baseName;
    }

    for (int suffix = 2; suffix < 10000; ++suffix) {
        std::ostringstream builder;
        builder << baseName << " " << suffix;
        if (!nameExists(builder.str())) {
            return builder.str();
        }
    }

    return baseName;
}

void State::ApplyChangeSet(const RenderContracts::SceneChangeSet& changeSet) {
    m_AccumulationManager.ApplyChange(changeSet, m_Settings);
}

} // namespace RenderFoundation
