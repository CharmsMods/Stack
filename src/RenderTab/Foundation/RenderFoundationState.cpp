#include "RenderFoundationState.h"

#include "RenderTab/Contracts/RenderContracts.h"

#include <algorithm>
#include <utility>

namespace RenderFoundation {

namespace {

Material BuildDefaultMaterial(Id id, BaseMaterial preset, const std::string& name, const Vec3& color) {
    Material material;
    material.id = id;
    material.name = name;
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
    m_Settings = Settings {};
    m_Camera = Camera {};
    m_Selection = Selection {};
    m_Materials.clear();
    m_Primitives.clear();
    m_Lights.clear();
    m_ProjectName.clear();
    m_ProjectFileName.clear();
    m_AccumulationManager.ResetForScene(m_Settings);
    m_HasUnsavedChanges = false;
    m_LastChangeReason = "Default foundation scene loaded.";
    SeedDefaultScene();
}

Snapshot State::BuildSnapshot() const {
    Snapshot snapshot;
    snapshot.cameraId = RenderContracts::kCameraObjectId;
    snapshot.metadata = m_Metadata;
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
    m_Materials = std::move(snapshot.materials);
    m_Primitives = std::move(snapshot.primitives);
    m_Lights = std::move(snapshot.lights);
    m_Camera = snapshot.camera;
    m_Settings = settings;
    m_Selection = Selection {};
    RefreshNextId();
    m_ProjectName = projectName;
    m_ProjectFileName = projectFileName;
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
            BuildDefaultMaterial(AllocateId(), BaseMaterial::Diffuse, "Fallback Diffuse", { 0.8f, 0.8f, 0.8f }));
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
    const Id clayId = AddMaterial(BuildDefaultMaterial(AllocateId(), BaseMaterial::Diffuse, "Clay", { 0.82f, 0.72f, 0.62f }));
    const Id metalId = AddMaterial(BuildDefaultMaterial(AllocateId(), BaseMaterial::Metal, "Brushed Metal", { 0.83f, 0.85f, 0.88f }));
    const Id glassId = AddMaterial(BuildDefaultMaterial(AllocateId(), BaseMaterial::Glass, "Studio Glass", { 0.94f, 0.97f, 1.0f }));
    const Id emitId = AddMaterial(BuildDefaultMaterial(AllocateId(), BaseMaterial::Emissive, "Warm Emitter", { 1.0f, 0.87f, 0.68f }));
    (void)emitId;

    Primitive floor = BuildPrimitive(AllocateId(), PrimitiveType::Plane, clayId, "Ground Plane");
    floor.transform.translation = { 0.0f, -0.9f, 0.0f };
    floor.transform.scale = { 8.0f, 1.0f, 8.0f };
    m_Primitives.push_back(floor);

    Primitive heroSphere = BuildPrimitive(AllocateId(), PrimitiveType::Sphere, glassId, "Hero Sphere");
    heroSphere.transform.translation = { -1.4f, 0.3f, 0.2f };
    heroSphere.transform.scale = { 1.5f, 1.5f, 1.5f };
    m_Primitives.push_back(heroSphere);

    Primitive cube = BuildPrimitive(AllocateId(), PrimitiveType::Cube, metalId, "Reference Cube");
    cube.transform.translation = { 1.45f, 0.1f, 0.7f };
    cube.transform.rotationDegrees = { 0.0f, 28.0f, 0.0f };
    cube.transform.scale = { 1.2f, 1.2f, 1.2f };
    m_Primitives.push_back(cube);

    Light keyLight = BuildLight(AllocateId(), LightType::Directional, "Key Directional");
    keyLight.transform.rotationDegrees = { -36.0f, 28.0f, 0.0f };
    keyLight.intensity = 5.5f;
    m_Lights.push_back(keyLight);

    Light pointLight = BuildLight(AllocateId(), LightType::Point, "Fill Point");
    pointLight.transform.translation = { 0.0f, 2.8f, 2.1f };
    pointLight.intensity = 28.0f;
    pointLight.color = { 1.0f, 0.88f, 0.76f };
    m_Lights.push_back(pointLight);

    SelectPrimitive(heroSphere.id);
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

void State::ApplyChangeSet(const RenderContracts::SceneChangeSet& changeSet) {
    m_AccumulationManager.ApplyChange(changeSet, m_Settings);
}

} // namespace RenderFoundation
