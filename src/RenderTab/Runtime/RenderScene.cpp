#include "RenderScene.h"

#include "RenderTab/Runtime/Debug/ValidationScenes.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace {

RenderMaterial BuildFallbackMaterial() {
    return BuildRenderMaterial("Default Diffuse", MakeRenderFloat3(0.8f, 0.8f, 0.8f));
}

void SanitizeMaterial(RenderMaterial& material) {
    if (material.name.empty()) {
        material.name = "Material";
    }
    material.roughness = std::clamp(material.roughness, 0.0f, 1.0f);
    material.metallic = std::clamp(material.metallic, 0.0f, 1.0f);
    material.transmission = std::clamp(material.transmission, 0.0f, 1.0f);
    material.ior = std::clamp(material.ior, 1.0f, 3.0f);
    material.emissionStrength = std::max(material.emissionStrength, 0.0f);
    material.transmissionRoughness = std::clamp(material.transmissionRoughness, 0.0f, 1.0f);
    material.absorptionDistance = std::max(material.absorptionDistance, 0.01f);
    material.absorptionColor.x = std::clamp(material.absorptionColor.x, 0.0f, 1.0f);
    material.absorptionColor.y = std::clamp(material.absorptionColor.y, 0.0f, 1.0f);
    material.absorptionColor.z = std::clamp(material.absorptionColor.z, 0.0f, 1.0f);
    ApplyRenderSurfacePreset(material, material.surfacePreset);
}

void EnsureMaterialLibrary(std::vector<RenderMaterial>& materials) {
    if (materials.empty()) {
        materials.push_back(BuildFallbackMaterial());
    }

    for (RenderMaterial& material : materials) {
        SanitizeMaterial(material);
    }
}

int SanitizeMaterialIndex(int materialIndex, int materialCount) {
    if (materialCount <= 0) {
        return 0;
    }

    if (materialIndex < 0) {
        return 0;
    }

    if (materialIndex >= materialCount) {
        return materialCount - 1;
    }

    return materialIndex;
}

bool MeshInstanceChanged(const RenderMeshInstance& left, const RenderMeshInstance& right) {
    return !NearlyEqual(left, right);
}

bool MaterialChanged(const RenderMaterial& left, const RenderMaterial& right) {
    return !NearlyEqual(left, right);
}

bool SphereChanged(const RenderSphere& left, const RenderSphere& right) {
    return left.name != right.name ||
        !NearlyEqual(left.transform, right.transform) ||
        !NearlyEqual(left.localCenter, right.localCenter) ||
        !NearlyEqual(left.radius, right.radius) ||
        left.materialIndex != right.materialIndex ||
        !NearlyEqual(left.albedoTint, right.albedoTint);
}

bool TriangleChanged(const RenderTriangle& left, const RenderTriangle& right) {
    return left.name != right.name ||
        !NearlyEqual(left.transform, right.transform) ||
        !NearlyEqual(left.localA, right.localA) ||
        !NearlyEqual(left.localB, right.localB) ||
        !NearlyEqual(left.localC, right.localC) ||
        !NearlyEqual(left.localNormalA, right.localNormalA) ||
        !NearlyEqual(left.localNormalB, right.localNormalB) ||
        !NearlyEqual(left.localNormalC, right.localNormalC) ||
        !NearlyEqual(left.uvA, right.uvA) ||
        !NearlyEqual(left.uvB, right.uvB) ||
        !NearlyEqual(left.uvC, right.uvC) ||
        left.materialIndex != right.materialIndex ||
        !NearlyEqual(left.albedoTint, right.albedoTint);
}

bool LightChanged(const RenderLight& left, const RenderLight& right) {
    return !NearlyEqual(left, right);
}

RenderMeshDefinition BuildBuiltInCubeMesh(int materialIndex) {
    const std::vector<RenderMeshTriangle> triangles = {
        { "Cube +X A", MakeRenderFloat3(0.5f, -0.5f, -0.5f), MakeRenderFloat3(0.5f, 0.5f, -0.5f), MakeRenderFloat3(0.5f, -0.5f, 0.5f), {}, {}, {}, {}, {}, {}, materialIndex, MakeRenderFloat3(1.0f, 1.0f, 1.0f) },
        { "Cube +X B", MakeRenderFloat3(0.5f, -0.5f, 0.5f), MakeRenderFloat3(0.5f, 0.5f, -0.5f), MakeRenderFloat3(0.5f, 0.5f, 0.5f), {}, {}, {}, {}, {}, {}, materialIndex, MakeRenderFloat3(1.0f, 1.0f, 1.0f) },
        { "Cube -X A", MakeRenderFloat3(-0.5f, -0.5f, 0.5f), MakeRenderFloat3(-0.5f, 0.5f, 0.5f), MakeRenderFloat3(-0.5f, -0.5f, -0.5f), {}, {}, {}, {}, {}, {}, materialIndex, MakeRenderFloat3(1.0f, 1.0f, 1.0f) },
        { "Cube -X B", MakeRenderFloat3(-0.5f, -0.5f, -0.5f), MakeRenderFloat3(-0.5f, 0.5f, 0.5f), MakeRenderFloat3(-0.5f, 0.5f, -0.5f), {}, {}, {}, {}, {}, {}, materialIndex, MakeRenderFloat3(1.0f, 1.0f, 1.0f) },
        { "Cube +Y A", MakeRenderFloat3(-0.5f, 0.5f, -0.5f), MakeRenderFloat3(-0.5f, 0.5f, 0.5f), MakeRenderFloat3(0.5f, 0.5f, -0.5f), {}, {}, {}, {}, {}, {}, materialIndex, MakeRenderFloat3(1.0f, 1.0f, 1.0f) },
        { "Cube +Y B", MakeRenderFloat3(0.5f, 0.5f, -0.5f), MakeRenderFloat3(-0.5f, 0.5f, 0.5f), MakeRenderFloat3(0.5f, 0.5f, 0.5f), {}, {}, {}, {}, {}, {}, materialIndex, MakeRenderFloat3(1.0f, 1.0f, 1.0f) },
        { "Cube -Y A", MakeRenderFloat3(-0.5f, -0.5f, 0.5f), MakeRenderFloat3(-0.5f, -0.5f, -0.5f), MakeRenderFloat3(0.5f, -0.5f, -0.5f), {}, {}, {}, {}, {}, {}, materialIndex, MakeRenderFloat3(1.0f, 1.0f, 1.0f) },
        { "Cube -Y B", MakeRenderFloat3(-0.5f, -0.5f, 0.5f), MakeRenderFloat3(0.5f, -0.5f, -0.5f), MakeRenderFloat3(0.5f, -0.5f, 0.5f), {}, {}, {}, {}, {}, {}, materialIndex, MakeRenderFloat3(1.0f, 1.0f, 1.0f) },
        { "Cube +Z A", MakeRenderFloat3(-0.5f, -0.5f, 0.5f), MakeRenderFloat3(0.5f, -0.5f, 0.5f), MakeRenderFloat3(-0.5f, 0.5f, 0.5f), {}, {}, {}, {}, {}, {}, materialIndex, MakeRenderFloat3(1.0f, 1.0f, 1.0f) },
        { "Cube +Z B", MakeRenderFloat3(0.5f, -0.5f, 0.5f), MakeRenderFloat3(0.5f, 0.5f, 0.5f), MakeRenderFloat3(-0.5f, 0.5f, 0.5f), {}, {}, {}, {}, {}, {}, materialIndex, MakeRenderFloat3(1.0f, 1.0f, 1.0f) },
        { "Cube -Z A", MakeRenderFloat3(0.5f, -0.5f, -0.5f), MakeRenderFloat3(-0.5f, -0.5f, -0.5f), MakeRenderFloat3(-0.5f, 0.5f, -0.5f), {}, {}, {}, {}, {}, {}, materialIndex, MakeRenderFloat3(1.0f, 1.0f, 1.0f) },
        { "Cube -Z B", MakeRenderFloat3(0.5f, -0.5f, -0.5f), MakeRenderFloat3(-0.5f, 0.5f, -0.5f), MakeRenderFloat3(0.5f, 0.5f, -0.5f), {}, {}, {}, {}, {}, {}, materialIndex, MakeRenderFloat3(1.0f, 1.0f, 1.0f) }
    };
    return BuildRenderMeshDefinition("Built-In Cube", triangles);
}

RenderMeshDefinition BuildBuiltInPlaneMesh(int materialIndex) {
    const std::vector<RenderMeshTriangle> triangles = {
        { "Plane A", MakeRenderFloat3(-0.5f, 0.0f, -0.5f), MakeRenderFloat3(-0.5f, 0.0f, 0.5f), MakeRenderFloat3(0.5f, 0.0f, -0.5f), {}, {}, {}, MakeRenderFloat2(0.0f, 0.0f), MakeRenderFloat2(0.0f, 1.0f), MakeRenderFloat2(1.0f, 0.0f), materialIndex, MakeRenderFloat3(1.0f, 1.0f, 1.0f) },
        { "Plane B", MakeRenderFloat3(0.5f, 0.0f, -0.5f), MakeRenderFloat3(-0.5f, 0.0f, 0.5f), MakeRenderFloat3(0.5f, 0.0f, 0.5f), {}, {}, {}, MakeRenderFloat2(1.0f, 0.0f), MakeRenderFloat2(0.0f, 1.0f), MakeRenderFloat2(1.0f, 1.0f), materialIndex, MakeRenderFloat3(1.0f, 1.0f, 1.0f) }
    };
    return BuildRenderMeshDefinition("Built-In Plane", triangles);
}

} // namespace

RenderScene::RenderScene()
    : m_Name("Render Scene")
    , m_BackgroundMode(RenderBackgroundMode::Gradient)
    , m_ValidationSceneId(RenderValidationSceneId::MixedDebug)
    , m_EnvironmentEnabled(true)
    , m_EnvironmentIntensity(1.0f)
    , m_FogEnabled(false)
    , m_FogColor(MakeRenderFloat3(0.82f, 0.88f, 0.96f))
    , m_FogDensity(0.0f)
    , m_FogAnisotropy(0.0f)
    , m_Revision(1)
    , m_LastChangeReason("Initial scene state.") {
    LoadValidationScene(RenderValidationSceneId::MixedDebug);
}

int RenderScene::AllocateObjectId() {
    return m_NextObjectId++;
}

bool RenderScene::SetSceneLabel(const std::string& label) {
    const std::string resolved = label.empty() ? "Untitled Render Scene" : label;
    if (m_ValidationSceneLabel == resolved) {
        return false;
    }

    m_ValidationSceneLabel = resolved;
    Touch("Scene label changed.");
    return true;
}

void RenderScene::EnsureObjectIds() {
    std::unordered_set<int> usedIds;
    int highestId = 0;

    auto claimId = [&](int& id) {
        if (id > 0 && usedIds.insert(id).second) {
            highestId = std::max(highestId, id);
            return;
        }

        do {
            id = AllocateObjectId();
        } while (!usedIds.insert(id).second);
        highestId = std::max(highestId, id);
    };

    for (RenderMeshInstance& meshInstance : m_MeshInstances) {
        claimId(meshInstance.id);
    }
    for (RenderSphere& sphere : m_Spheres) {
        claimId(sphere.id);
    }
    for (RenderTriangle& triangle : m_Triangles) {
        claimId(triangle.id);
    }
    for (RenderLight& light : m_Lights) {
        claimId(light.id);
    }

    m_NextObjectId = std::max(m_NextObjectId, highestId + 1);
}

bool RenderScene::SetBackgroundMode(RenderBackgroundMode mode) {
    if (m_BackgroundMode == mode) {
        return false;
    }

    m_BackgroundMode = mode;
    Touch("Scene background mode changed.");
    return true;
}

bool RenderScene::SetValidationScene(RenderValidationSceneId id) {
    if (m_ValidationSceneId == id) {
        return false;
    }

    LoadValidationScene(id);
    Touch(std::string("Validation scene changed to ") + m_ValidationSceneLabel + ".");
    return true;
}

bool RenderScene::SetEnvironmentEnabled(bool enabled) {
    if (m_EnvironmentEnabled == enabled) {
        return false;
    }

    m_EnvironmentEnabled = enabled;
    Touch(enabled ? "Scene environment enabled." : "Scene environment disabled.");
    return true;
}

bool RenderScene::SetEnvironmentIntensity(float intensity) {
    const float clampedIntensity = std::clamp(intensity, 0.0f, 16.0f);
    if (NearlyEqual(m_EnvironmentIntensity, clampedIntensity)) {
        return false;
    }

    m_EnvironmentIntensity = clampedIntensity;
    Touch("Scene environment intensity changed.");
    return true;
}

bool RenderScene::SetFogEnabled(bool enabled) {
    if (m_FogEnabled == enabled) {
        return false;
    }

    m_FogEnabled = enabled;
    Touch(enabled ? "Scene fog enabled." : "Scene fog disabled.");
    return true;
}

bool RenderScene::SetFogColor(const RenderFloat3& color) {
    const RenderFloat3 clamped = MakeRenderFloat3(
        std::clamp(color.x, 0.0f, 1.0f),
        std::clamp(color.y, 0.0f, 1.0f),
        std::clamp(color.z, 0.0f, 1.0f));
    if (NearlyEqual(m_FogColor, clamped)) {
        return false;
    }

    m_FogColor = clamped;
    Touch("Scene fog color changed.");
    return true;
}

bool RenderScene::SetFogDensity(float density) {
    const float clampedDensity = std::clamp(density, 0.0f, 4.0f);
    if (NearlyEqual(m_FogDensity, clampedDensity)) {
        return false;
    }

    m_FogDensity = clampedDensity;
    Touch("Scene fog density changed.");
    return true;
}

bool RenderScene::SetFogAnisotropy(float anisotropy) {
    const float clampedAnisotropy = std::clamp(anisotropy, -0.95f, 0.95f);
    if (NearlyEqual(m_FogAnisotropy, clampedAnisotropy)) {
        return false;
    }

    m_FogAnisotropy = clampedAnisotropy;
    Touch("Scene fog anisotropy changed.");
    return true;
}

bool RenderScene::UpdateMaterial(int index, const RenderMaterial& material) {
    if (!IsMaterialIndexValid(index)) {
        return false;
    }

    RenderMaterial sanitized = material;
    SanitizeMaterial(sanitized);

    RenderMaterial& current = m_Materials[static_cast<std::size_t>(index)];
    if (!MaterialChanged(current, sanitized)) {
        return false;
    }

    current = sanitized;
    Touch(current.name + " material updated.");
    return true;
}

bool RenderScene::UpdateSphere(int index, const RenderSphere& sphere) {
    if (!IsSphereIndexValid(index)) {
        return false;
    }

    RenderSphere sanitized = sphere;
    sanitized.materialIndex = SanitizeMaterialIndex(sanitized.materialIndex, GetMaterialCount());

    RenderSphere& current = m_Spheres[static_cast<std::size_t>(index)];
    if (!SphereChanged(current, sanitized)) {
        return false;
    }

    current = sanitized;
    RebuildSpatialData();
    Touch(current.name + " updated.");
    return true;
}

bool RenderScene::UpdateTriangle(int index, const RenderTriangle& triangle) {
    if (!IsTriangleIndexValid(index)) {
        return false;
    }

    RenderTriangle sanitized = triangle;
    sanitized.materialIndex = SanitizeMaterialIndex(sanitized.materialIndex, GetMaterialCount());

    RenderTriangle& current = m_Triangles[static_cast<std::size_t>(index)];
    if (!TriangleChanged(current, sanitized)) {
        return false;
    }

    current = sanitized;
    RebuildSpatialData();
    Touch(current.name + " updated.");
    return true;
}

bool RenderScene::UpdateMeshInstance(int index, const RenderMeshInstance& meshInstance) {
    if (!IsMeshInstanceIndexValid(index)) {
        return false;
    }

    RenderMeshInstance& current = m_MeshInstances[static_cast<std::size_t>(index)];
    if (!MeshInstanceChanged(current, meshInstance)) {
        return false;
    }

    current = meshInstance;
    RebuildSpatialData();
    Touch(current.name + " updated.");
    return true;
}

bool RenderScene::UpdateLight(int index, const RenderLight& light) {
    if (!IsLightIndexValid(index)) {
        return false;
    }

    RenderLight& current = m_Lights[static_cast<std::size_t>(index)];
    if (!LightChanged(current, light)) {
        return false;
    }

    current = light;
    Touch(current.name + " updated.");
    return true;
}

int RenderScene::AddMaterial(const RenderMaterial& material) {
    RenderMaterial resolved = material;
    SanitizeMaterial(resolved);
    m_Materials.push_back(resolved);
    Touch(resolved.name + " added.");
    return static_cast<int>(m_Materials.size()) - 1;
}

bool RenderScene::DuplicateMaterial(int materialIndex, int& newMaterialIndex) {
    if (!IsMaterialIndexValid(materialIndex)) {
        return false;
    }

    RenderMaterial copy = m_Materials[static_cast<std::size_t>(materialIndex)];
    copy.name += " Copy";
    m_Materials.push_back(copy);
    newMaterialIndex = static_cast<int>(m_Materials.size()) - 1;
    Touch(copy.name + " duplicated.");
    return true;
}

bool RenderScene::DeleteMaterial(int materialIndex) {
    if (!IsMaterialIndexValid(materialIndex) || m_Materials.size() <= 1) {
        return false;
    }

    const std::string name = m_Materials[static_cast<std::size_t>(materialIndex)].name;
    m_Materials.erase(m_Materials.begin() + materialIndex);

    const auto remapIndex = [materialIndex](int& index) {
        if (index == materialIndex) {
            index = 0;
        } else if (index > materialIndex) {
            --index;
        }
    };

    for (RenderSphere& sphere : m_Spheres) {
        remapIndex(sphere.materialIndex);
    }
    for (RenderTriangle& triangle : m_Triangles) {
        remapIndex(triangle.materialIndex);
    }
    for (RenderMeshDefinition& mesh : m_MeshDefinitions) {
        for (RenderMeshTriangle& triangle : mesh.triangles) {
            remapIndex(triangle.materialIndex);
        }
    }

    RebuildSpatialData();
    Touch(name + " deleted.");
    return true;
}

bool RenderScene::AddSphere(const RenderSphere& sphere, int& newObjectId) {
    RenderSphere instance = sphere;
    instance.id = AllocateObjectId();
    if (instance.name.empty()) {
        instance.name = "Sphere";
    }
    instance.materialIndex = SanitizeMaterialIndex(instance.materialIndex, GetMaterialCount());
    newObjectId = instance.id;
    m_Spheres.push_back(instance);
    RebuildSpatialData();
    Touch(instance.name + " added.");
    return true;
}

bool RenderScene::AddMeshInstance(const RenderMeshInstance& meshInstance, int& newObjectId) {
    if (!IsMeshDefinitionIndexValid(meshInstance.meshIndex)) {
        return false;
    }

    RenderMeshInstance instance = meshInstance;
    instance.id = AllocateObjectId();
    if (instance.name.empty()) {
        instance.name = "Mesh";
    }
    newObjectId = instance.id;
    m_MeshInstances.push_back(instance);
    RebuildSpatialData();
    Touch(instance.name + " added.");
    return true;
}

bool RenderScene::AddLight(const RenderLight& light, int& newObjectId) {
    RenderLight instance = light;
    instance.id = AllocateObjectId();
    if (instance.name.empty()) {
        instance.name = std::string(GetRenderLightTypeLabel(instance.type)) + " Light";
    }
    newObjectId = instance.id;
    m_Lights.push_back(instance);
    Touch(instance.name + " added.");
    return true;
}

bool RenderScene::AddBuiltInCube(int materialIndex, int& newObjectId) {
    const int resolvedMaterialIndex = SanitizeMaterialIndex(materialIndex, GetMaterialCount());
    RenderMeshDefinition mesh = BuildBuiltInCubeMesh(resolvedMaterialIndex);
    const int meshIndex = static_cast<int>(m_MeshDefinitions.size());
    m_MeshDefinitions.push_back(std::move(mesh));

    RenderMeshInstance instance;
    instance.name = "Cube";
    instance.meshIndex = meshIndex;
    const float offset = static_cast<float>(meshIndex % 5) * 0.2f;
    instance.transform.translation = MakeRenderFloat3(offset, 0.5f, offset);
    instance.colorTint = MakeRenderFloat3(1.0f, 1.0f, 1.0f);
    return AddMeshInstance(instance, newObjectId);
}

bool RenderScene::AddBuiltInPlane(int materialIndex, int& newObjectId) {
    const int resolvedMaterialIndex = SanitizeMaterialIndex(materialIndex, GetMaterialCount());
    RenderMeshDefinition mesh = BuildBuiltInPlaneMesh(resolvedMaterialIndex);
    const int meshIndex = static_cast<int>(m_MeshDefinitions.size());
    m_MeshDefinitions.push_back(std::move(mesh));

    RenderMeshInstance instance;
    instance.name = "Plane";
    instance.meshIndex = meshIndex;
    const float offset = static_cast<float>(meshIndex % 3) * 0.1f;
    instance.transform.translation = MakeRenderFloat3(offset, -offset, offset);
    instance.transform.scale = MakeRenderFloat3(4.0f, 1.0f, 4.0f);
    instance.colorTint = MakeRenderFloat3(1.0f, 1.0f, 1.0f);
    return AddMeshInstance(instance, newObjectId);
}

bool RenderScene::AssignMaterialToMeshInstance(int objectId, int materialIndex) {
    const int meshInstanceIndex = FindMeshInstanceIndexById(objectId);
    if (!IsMeshInstanceIndexValid(meshInstanceIndex) || !IsMaterialIndexValid(materialIndex)) {
        return false;
    }

    RenderMeshInstance& meshInstance = m_MeshInstances[static_cast<std::size_t>(meshInstanceIndex)];
    if (!IsMeshDefinitionIndexValid(meshInstance.meshIndex)) {
        return false;
    }

    RenderMeshDefinition meshDefinition = m_MeshDefinitions[static_cast<std::size_t>(meshInstance.meshIndex)];
    bool changed = false;
    for (RenderMeshTriangle& triangle : meshDefinition.triangles) {
        if (triangle.materialIndex != materialIndex) {
            triangle.materialIndex = materialIndex;
            changed = true;
        }
    }
    if (!changed) {
        return false;
    }

    meshDefinition.name += " Variant";
    m_MeshDefinitions.push_back(std::move(meshDefinition));
    meshInstance.meshIndex = static_cast<int>(m_MeshDefinitions.size()) - 1;
    RebuildSpatialData();
    Touch("Mesh instance material reassigned.");
    return true;
}

void RenderScene::CreateEmptyScene(const std::string& label) {
    m_ValidationSceneId = RenderValidationSceneId::Custom;
    m_ValidationSceneLabel = label.empty() ? "Untitled Render Scene" : label;
    m_ValidationSceneDescription = "Authorable render scene.";
    m_BackgroundMode = RenderBackgroundMode::Black;
    m_EnvironmentEnabled = false;
    m_EnvironmentIntensity = 1.0f;
    m_FogEnabled = false;
    m_FogColor = MakeRenderFloat3(0.82f, 0.88f, 0.96f);
    m_FogDensity = 0.0f;
    m_FogAnisotropy = 0.0f;
    m_ImportedAssets.clear();
    m_ImportedTextures.clear();
    m_Materials = { BuildFallbackMaterial() };
    m_MeshDefinitions.clear();
    m_MeshInstances.clear();
    m_Spheres.clear();
    m_Triangles.clear();
    m_Lights.clear();
    RebuildSpatialData();
    Touch("Created new empty render scene.");
}

void RenderScene::ApplySceneSnapshot(
    std::string label,
    std::string description,
    RenderBackgroundMode backgroundMode,
    bool environmentEnabled,
    float environmentIntensity,
    bool fogEnabled,
    RenderFloat3 fogColor,
    float fogDensity,
    float fogAnisotropy,
    std::vector<RenderImportedAsset> importedAssets,
    std::vector<RenderImportedTexture> importedTextures,
    std::vector<RenderMaterial> materials,
    std::vector<RenderMeshDefinition> meshes,
    std::vector<RenderMeshInstance> meshInstances,
    std::vector<RenderSphere> spheres,
    std::vector<RenderTriangle> triangles,
    std::vector<RenderLight> lights,
    const std::string& reason) {
    m_ValidationSceneId = RenderValidationSceneId::Custom;
    m_ValidationSceneLabel = label.empty() ? "Custom Scene" : std::move(label);
    m_ValidationSceneDescription = description.empty() ? "Render scene snapshot." : std::move(description);
    m_BackgroundMode = backgroundMode;
    m_EnvironmentEnabled = environmentEnabled;
    m_EnvironmentIntensity = std::clamp(environmentIntensity, 0.0f, 16.0f);
    m_FogEnabled = fogEnabled;
    m_FogColor = MakeRenderFloat3(
        std::clamp(fogColor.x, 0.0f, 1.0f),
        std::clamp(fogColor.y, 0.0f, 1.0f),
        std::clamp(fogColor.z, 0.0f, 1.0f));
    m_FogDensity = std::clamp(fogDensity, 0.0f, 4.0f);
    m_FogAnisotropy = std::clamp(fogAnisotropy, -0.95f, 0.95f);
    m_ImportedAssets = std::move(importedAssets);
    m_ImportedTextures = std::move(importedTextures);
    m_Materials = std::move(materials);
    m_MeshDefinitions = std::move(meshes);
    m_MeshInstances = std::move(meshInstances);
    m_Spheres = std::move(spheres);
    m_Triangles = std::move(triangles);
    m_Lights = std::move(lights);
    RebuildSpatialData();
    Touch(reason.empty() ? std::string("Scene snapshot applied.") : reason);
}

bool RenderScene::MergeImportedScene(
    std::string label,
    std::string description,
    std::vector<RenderImportedAsset> importedAssets,
    std::vector<RenderImportedTexture> importedTextures,
    std::vector<RenderMaterial> materials,
    std::vector<RenderMeshDefinition> meshes,
    std::vector<RenderMeshInstance> meshInstances,
    std::vector<RenderSphere> spheres,
    std::vector<RenderTriangle> triangles,
    std::vector<RenderLight> lights,
    const std::string& reason) {
    const int assetIndexOffset = GetImportedAssetCount();
    const int textureIndexOffset = GetImportedTextureCount();
    const int materialIndexOffset = GetMaterialCount();
    const int meshIndexOffset = GetMeshDefinitionCount();

    auto remapTextureRef = [textureIndexOffset](RenderMaterialTextureRef& ref) {
        if (ref.textureIndex >= 0) {
            ref.textureIndex += textureIndexOffset;
        }
    };

    for (RenderImportedTexture& texture : importedTextures) {
        if (texture.sourceAssetIndex >= 0) {
            texture.sourceAssetIndex += assetIndexOffset;
        }
    }
    for (RenderMaterial& material : materials) {
        if (material.sourceAssetIndex >= 0) {
            material.sourceAssetIndex += assetIndexOffset;
        }
        remapTextureRef(material.baseColorTexture);
        remapTextureRef(material.metallicRoughnessTexture);
        remapTextureRef(material.emissiveTexture);
        remapTextureRef(material.normalTexture);
        SanitizeMaterial(material);
    }
    for (RenderMeshDefinition& mesh : meshes) {
        if (mesh.sourceAssetIndex >= 0) {
            mesh.sourceAssetIndex += assetIndexOffset;
        }
        for (RenderMeshTriangle& triangle : mesh.triangles) {
            triangle.materialIndex += materialIndexOffset;
        }
    }
    for (RenderMeshInstance& meshInstance : meshInstances) {
        meshInstance.id = AllocateObjectId();
        meshInstance.meshIndex += meshIndexOffset;
    }
    for (RenderSphere& sphere : spheres) {
        sphere.id = AllocateObjectId();
        sphere.materialIndex += materialIndexOffset;
    }
    for (RenderTriangle& triangle : triangles) {
        triangle.id = AllocateObjectId();
        triangle.materialIndex += materialIndexOffset;
    }
    for (RenderLight& light : lights) {
        light.id = AllocateObjectId();
    }

    if (m_ValidationSceneId != RenderValidationSceneId::Custom) {
        m_ValidationSceneId = RenderValidationSceneId::Custom;
        m_ValidationSceneDescription = "Authorable render scene.";
    }
    if (!label.empty() && m_ValidationSceneLabel.empty()) {
        m_ValidationSceneLabel = std::move(label);
    }
    if (!description.empty() && m_ValidationSceneDescription.empty()) {
        m_ValidationSceneDescription = std::move(description);
    }

    m_ImportedAssets.insert(
        m_ImportedAssets.end(),
        std::make_move_iterator(importedAssets.begin()),
        std::make_move_iterator(importedAssets.end()));
    m_ImportedTextures.insert(
        m_ImportedTextures.end(),
        std::make_move_iterator(importedTextures.begin()),
        std::make_move_iterator(importedTextures.end()));
    m_Materials.insert(
        m_Materials.end(),
        std::make_move_iterator(materials.begin()),
        std::make_move_iterator(materials.end()));
    m_MeshDefinitions.insert(
        m_MeshDefinitions.end(),
        std::make_move_iterator(meshes.begin()),
        std::make_move_iterator(meshes.end()));
    m_MeshInstances.insert(
        m_MeshInstances.end(),
        std::make_move_iterator(meshInstances.begin()),
        std::make_move_iterator(meshInstances.end()));
    m_Spheres.insert(
        m_Spheres.end(),
        std::make_move_iterator(spheres.begin()),
        std::make_move_iterator(spheres.end()));
    m_Triangles.insert(
        m_Triangles.end(),
        std::make_move_iterator(triangles.begin()),
        std::make_move_iterator(triangles.end()));
    m_Lights.insert(
        m_Lights.end(),
        std::make_move_iterator(lights.begin()),
        std::make_move_iterator(lights.end()));

    RebuildSpatialData();
    Touch(reason.empty() ? std::string("Imported scene merged.") : reason);
    return true;
}

void RenderScene::MarkDirty(const std::string& reason) {
    RebuildSpatialData();
    Touch(reason.empty() ? std::string("Scene changed.") : reason);
}

bool RenderScene::UpdateImportedTexturePixels(
    int index,
    std::vector<unsigned char> pixels,
    int width,
    int height,
    int channels,
    const std::string& reason) {
    if (!IsImportedTextureIndexValid(index)) {
        return false;
    }

    RenderImportedTexture& texture = m_ImportedTextures[static_cast<std::size_t>(index)];
    if (texture.width == width &&
        texture.height == height &&
        texture.channels == channels &&
        texture.pixels == pixels) {
        return false;
    }

    texture.width = width;
    texture.height = height;
    texture.channels = channels;
    texture.pixels = std::move(pixels);
    Touch(reason.empty() ? std::string("Imported texture reloaded.") : reason);
    return true;
}

bool RenderScene::IsImportedAssetIndexValid(int index) const {
    return index >= 0 && index < static_cast<int>(m_ImportedAssets.size());
}

bool RenderScene::IsImportedTextureIndexValid(int index) const {
    return index >= 0 && index < static_cast<int>(m_ImportedTextures.size());
}

bool RenderScene::IsMaterialIndexValid(int index) const {
    return index >= 0 && index < static_cast<int>(m_Materials.size());
}

bool RenderScene::IsMeshDefinitionIndexValid(int index) const {
    return index >= 0 && index < static_cast<int>(m_MeshDefinitions.size());
}

bool RenderScene::IsMeshInstanceIndexValid(int index) const {
    return index >= 0 && index < static_cast<int>(m_MeshInstances.size());
}

bool RenderScene::IsSphereIndexValid(int index) const {
    return index >= 0 && index < static_cast<int>(m_Spheres.size());
}

bool RenderScene::IsTriangleIndexValid(int index) const {
    return index >= 0 && index < static_cast<int>(m_Triangles.size());
}

bool RenderScene::IsLightIndexValid(int index) const {
    return index >= 0 && index < static_cast<int>(m_Lights.size());
}

int RenderScene::FindMeshInstanceIndexById(int objectId) const {
    for (int i = 0; i < static_cast<int>(m_MeshInstances.size()); ++i) {
        if (m_MeshInstances[static_cast<std::size_t>(i)].id == objectId) {
            return i;
        }
    }
    return -1;
}

int RenderScene::FindSphereIndexById(int objectId) const {
    for (int i = 0; i < static_cast<int>(m_Spheres.size()); ++i) {
        if (m_Spheres[static_cast<std::size_t>(i)].id == objectId) {
            return i;
        }
    }
    return -1;
}

int RenderScene::FindTriangleIndexById(int objectId) const {
    for (int i = 0; i < static_cast<int>(m_Triangles.size()); ++i) {
        if (m_Triangles[static_cast<std::size_t>(i)].id == objectId) {
            return i;
        }
    }
    return -1;
}

int RenderScene::FindLightIndexById(int objectId) const {
    for (int i = 0; i < static_cast<int>(m_Lights.size()); ++i) {
        if (m_Lights[static_cast<std::size_t>(i)].id == objectId) {
            return i;
        }
    }
    return -1;
}

int RenderScene::GetEmissiveMaterialCount() const {
    int count = 0;
    for (const RenderMaterial& material : m_Materials) {
        if (IsEmissive(material)) {
            ++count;
        }
    }
    return count;
}

const RenderImportedAsset& RenderScene::GetImportedAsset(int index) const {
    if (!IsImportedAssetIndexValid(index)) {
        throw std::out_of_range("Render imported asset index out of range.");
    }
    return m_ImportedAssets[static_cast<std::size_t>(index)];
}

const RenderImportedTexture& RenderScene::GetImportedTexture(int index) const {
    if (!IsImportedTextureIndexValid(index)) {
        throw std::out_of_range("Render imported texture index out of range.");
    }
    return m_ImportedTextures[static_cast<std::size_t>(index)];
}

const RenderMaterial& RenderScene::GetMaterial(int index) const {
    if (!IsMaterialIndexValid(index)) {
        throw std::out_of_range("Render material index out of range.");
    }
    return m_Materials[static_cast<std::size_t>(index)];
}

const RenderMeshDefinition& RenderScene::GetMeshDefinition(int index) const {
    if (!IsMeshDefinitionIndexValid(index)) {
        throw std::out_of_range("Render mesh definition index out of range.");
    }
    return m_MeshDefinitions[static_cast<std::size_t>(index)];
}

const RenderMeshInstance& RenderScene::GetMeshInstance(int index) const {
    if (!IsMeshInstanceIndexValid(index)) {
        throw std::out_of_range("Render mesh instance index out of range.");
    }
    return m_MeshInstances[static_cast<std::size_t>(index)];
}

const RenderSphere& RenderScene::GetSphere(int index) const {
    if (!IsSphereIndexValid(index)) {
        throw std::out_of_range("Render sphere index out of range.");
    }
    return m_Spheres[static_cast<std::size_t>(index)];
}

const RenderTriangle& RenderScene::GetTriangle(int index) const {
    if (!IsTriangleIndexValid(index)) {
        throw std::out_of_range("Render triangle index out of range.");
    }
    return m_Triangles[static_cast<std::size_t>(index)];
}

const RenderLight& RenderScene::GetLight(int index) const {
    if (!IsLightIndexValid(index)) {
        throw std::out_of_range("Render light index out of range.");
    }
    return m_Lights[static_cast<std::size_t>(index)];
}

const RenderResolvedTriangle& RenderScene::GetResolvedTriangle(int index) const {
    if (index < 0 || index >= static_cast<int>(m_ResolvedTriangles.size())) {
        throw std::out_of_range("Resolved render triangle index out of range.");
    }
    return m_ResolvedTriangles[static_cast<std::size_t>(index)];
}

bool RenderScene::DuplicateMeshInstance(int objectId, int& newObjectId) {
    const int index = FindMeshInstanceIndexById(objectId);
    if (!IsMeshInstanceIndexValid(index)) {
        return false;
    }

    RenderMeshInstance copy = m_MeshInstances[static_cast<std::size_t>(index)];
    copy.id = AllocateObjectId();
    copy.name += " Copy";
    copy.transform.translation = Add(copy.transform.translation, MakeRenderFloat3(0.35f, 0.0f, 0.35f));
    newObjectId = copy.id;
    m_MeshInstances.push_back(copy);
    RebuildSpatialData();
    Touch(copy.name + " duplicated.");
    return true;
}

bool RenderScene::DuplicateSphere(int objectId, int& newObjectId) {
    const int index = FindSphereIndexById(objectId);
    if (!IsSphereIndexValid(index)) {
        return false;
    }

    RenderSphere copy = m_Spheres[static_cast<std::size_t>(index)];
    copy.id = AllocateObjectId();
    copy.name += " Copy";
    copy.transform.translation = Add(copy.transform.translation, MakeRenderFloat3(0.35f, 0.0f, 0.35f));
    newObjectId = copy.id;
    m_Spheres.push_back(copy);
    RebuildSpatialData();
    Touch(copy.name + " duplicated.");
    return true;
}

bool RenderScene::DuplicateTriangle(int objectId, int& newObjectId) {
    const int index = FindTriangleIndexById(objectId);
    if (!IsTriangleIndexValid(index)) {
        return false;
    }

    RenderTriangle copy = m_Triangles[static_cast<std::size_t>(index)];
    copy.id = AllocateObjectId();
    copy.name += " Copy";
    copy.transform.translation = Add(copy.transform.translation, MakeRenderFloat3(0.35f, 0.0f, 0.35f));
    newObjectId = copy.id;
    m_Triangles.push_back(copy);
    RebuildSpatialData();
    Touch(copy.name + " duplicated.");
    return true;
}

bool RenderScene::DuplicateLight(int objectId, int& newObjectId) {
    const int index = FindLightIndexById(objectId);
    if (!IsLightIndexValid(index)) {
        return false;
    }

    RenderLight copy = m_Lights[static_cast<std::size_t>(index)];
    copy.id = AllocateObjectId();
    copy.name += " Copy";
    copy.transform.translation = Add(copy.transform.translation, MakeRenderFloat3(0.35f, 0.35f, 0.35f));
    newObjectId = copy.id;
    m_Lights.push_back(copy);
    Touch(copy.name + " duplicated.");
    return true;
}

bool RenderScene::DeleteMeshInstance(int objectId) {
    const int index = FindMeshInstanceIndexById(objectId);
    if (!IsMeshInstanceIndexValid(index)) {
        return false;
    }

    const std::string name = m_MeshInstances[static_cast<std::size_t>(index)].name;
    m_MeshInstances.erase(m_MeshInstances.begin() + index);
    RebuildSpatialData();
    Touch(name + " deleted.");
    return true;
}

bool RenderScene::DeleteSphere(int objectId) {
    const int index = FindSphereIndexById(objectId);
    if (!IsSphereIndexValid(index)) {
        return false;
    }

    const std::string name = m_Spheres[static_cast<std::size_t>(index)].name;
    m_Spheres.erase(m_Spheres.begin() + index);
    RebuildSpatialData();
    Touch(name + " deleted.");
    return true;
}

bool RenderScene::DeleteTriangle(int objectId) {
    const int index = FindTriangleIndexById(objectId);
    if (!IsTriangleIndexValid(index)) {
        return false;
    }

    const std::string name = m_Triangles[static_cast<std::size_t>(index)].name;
    m_Triangles.erase(m_Triangles.begin() + index);
    RebuildSpatialData();
    Touch(name + " deleted.");
    return true;
}

bool RenderScene::DeleteLight(int objectId) {
    const int index = FindLightIndexById(objectId);
    if (!IsLightIndexValid(index)) {
        return false;
    }

    const std::string name = m_Lights[static_cast<std::size_t>(index)].name;
    m_Lights.erase(m_Lights.begin() + index);
    Touch(name + " deleted.");
    return true;
}

void RenderScene::LoadValidationScene(RenderValidationSceneId id) {
    const RenderValidationSceneTemplate sceneTemplate = BuildRenderValidationScene(id);
    m_ValidationSceneId = id;
    m_ValidationSceneLabel = sceneTemplate.label;
    m_ValidationSceneDescription = sceneTemplate.description;
    m_BackgroundMode = sceneTemplate.defaultBackground;
    m_EnvironmentEnabled = sceneTemplate.defaultEnvironmentEnabled;
    m_EnvironmentIntensity = sceneTemplate.defaultEnvironmentIntensity;
    m_FogEnabled = sceneTemplate.defaultFogEnabled;
    m_FogColor = sceneTemplate.defaultFogColor;
    m_FogDensity = sceneTemplate.defaultFogDensity;
    m_FogAnisotropy = sceneTemplate.defaultFogAnisotropy;
    m_ImportedAssets.clear();
    m_ImportedTextures.clear();
    m_Materials = sceneTemplate.materials;
    m_MeshDefinitions = sceneTemplate.meshes;
    m_MeshInstances = sceneTemplate.meshInstances;
    m_Spheres = sceneTemplate.spheres;
    m_Triangles = sceneTemplate.triangles;
    m_Lights = sceneTemplate.lights;
    EnsureObjectIds();
    RebuildSpatialData();
}

void RenderScene::RebuildSpatialData() {
    EnsureObjectIds();
    EnsureMaterialLibrary(m_Materials);
    const int materialCount = GetMaterialCount();
    for (RenderSphere& sphere : m_Spheres) {
        sphere.materialIndex = SanitizeMaterialIndex(sphere.materialIndex, materialCount);
    }
    for (RenderTriangle& triangle : m_Triangles) {
        triangle.materialIndex = SanitizeMaterialIndex(triangle.materialIndex, materialCount);
    }
    for (RenderMeshDefinition& mesh : m_MeshDefinitions) {
        for (RenderMeshTriangle& triangle : mesh.triangles) {
            triangle.materialIndex = SanitizeMaterialIndex(triangle.materialIndex, materialCount);
        }
    }

    std::size_t resolvedTriangleCount = m_Triangles.size();
    for (const RenderMeshInstance& meshInstance : m_MeshInstances) {
        if (!IsMeshDefinitionIndexValid(meshInstance.meshIndex)) {
            continue;
        }
        resolvedTriangleCount += m_MeshDefinitions[static_cast<std::size_t>(meshInstance.meshIndex)].triangles.size();
    }

    m_ResolvedTriangles.clear();
    m_ResolvedTriangles.reserve(resolvedTriangleCount);

    m_PrimitiveRefs.clear();
    m_PrimitiveRefs.reserve(m_Spheres.size() + resolvedTriangleCount);

    for (std::size_t i = 0; i < m_Spheres.size(); ++i) {
        RenderPrimitiveRef ref;
        ref.type = RenderPrimitiveType::Sphere;
        ref.index = static_cast<int>(i);
        ref.bounds = ComputeBounds(m_Spheres[i]);
        m_PrimitiveRefs.push_back(ref);
    }

    for (std::size_t i = 0; i < m_Triangles.size(); ++i) {
        m_ResolvedTriangles.push_back(ResolveTriangle(m_Triangles[i]));
        RenderPrimitiveRef ref;
        ref.type = RenderPrimitiveType::Triangle;
        ref.index = static_cast<int>(m_ResolvedTriangles.size() - 1);
        ref.bounds = ComputeBounds(m_ResolvedTriangles.back());
        m_PrimitiveRefs.push_back(ref);
    }

    for (const RenderMeshInstance& meshInstance : m_MeshInstances) {
        if (!IsMeshDefinitionIndexValid(meshInstance.meshIndex)) {
            continue;
        }

        const RenderMeshDefinition& mesh = m_MeshDefinitions[static_cast<std::size_t>(meshInstance.meshIndex)];
        for (const RenderMeshTriangle& triangle : mesh.triangles) {
            m_ResolvedTriangles.push_back(ResolveMeshTriangle(triangle, meshInstance));

            RenderPrimitiveRef ref;
            ref.type = RenderPrimitiveType::Triangle;
            ref.index = static_cast<int>(m_ResolvedTriangles.size() - 1);
            ref.bounds = ComputeBounds(m_ResolvedTriangles.back());
            m_PrimitiveRefs.push_back(ref);
        }
    }

    m_BvhNodes = BuildRenderBvh(m_PrimitiveRefs);
}

void RenderScene::Touch(const std::string& reason) {
    ++m_Revision;
    m_LastChangeReason = reason;
}
