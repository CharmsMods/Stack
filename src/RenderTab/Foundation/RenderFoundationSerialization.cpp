#include "RenderFoundationSerialization.h"

#include "RenderTab/Contracts/RenderContracts.h"

#include <algorithm>
#include <utility>

namespace RenderFoundation::Serialization {

namespace {

using json = StackBinaryFormat::json;

constexpr int kSnapshotVersion = 4;

json ToJson(const Vec2& value) {
    return json::array({ value.x, value.y });
}

json ToJson(const Vec3& value) {
    return json::array({ value.x, value.y, value.z });
}

json ToJson(const Transform& value) {
    json root = json::object();
    root["translation"] = ToJson(value.translation);
    root["rotationDegrees"] = ToJson(value.rotationDegrees);
    root["scale"] = ToJson(value.scale);
    return root;
}

bool FromJson(const json& value, Vec2& outValue) {
    if (!value.is_array() || value.size() != 2) {
        return false;
    }

    outValue.x = value[0].get<float>();
    outValue.y = value[1].get<float>();
    return true;
}

bool FromJson(const json& value, Vec3& outValue) {
    if (!value.is_array() || value.size() != 3) {
        return false;
    }

    outValue.x = value[0].get<float>();
    outValue.y = value[1].get<float>();
    outValue.z = value[2].get<float>();
    return true;
}

bool FromJson(const json& value, Transform& outValue) {
    if (!value.is_object()) {
        return false;
    }

    Vec3 translation {};
    Vec3 rotationDegrees {};
    Vec3 scale {};
    if (!FromJson(value.value("translation", json::array({ 0.0f, 0.0f, 0.0f })), translation) ||
        !FromJson(value.value("rotationDegrees", json::array({ 0.0f, 0.0f, 0.0f })), rotationDegrees) ||
        !FromJson(value.value("scale", json::array({ 1.0f, 1.0f, 1.0f })), scale)) {
        return false;
    }

    outValue.translation = translation;
    outValue.rotationDegrees = rotationDegrees;
    outValue.scale = scale;
    return true;
}

json BuildMaterialJson(const Material& material) {
    json root = json::object();
    root["id"] = material.id;
    root["name"] = material.name;
    root["surfacePreset"] = static_cast<int>(material.baseMaterial);
    root["baseColor"] = ToJson(material.baseColor);
    root["emissionColor"] = ToJson(material.emissionColor);
    root["emissionStrength"] = material.emissionStrength;
    root["roughness"] = material.roughness;
    root["metallic"] = material.metallic;
    root["transmission"] = material.transmission;
    root["ior"] = material.ior;
    root["thinWalled"] = material.thinWalled;
    root["absorptionColor"] = ToJson(material.absorptionColor);
    root["absorptionDistance"] = material.absorptionDistance;
    root["transmissionRoughness"] = material.transmissionRoughness;
    root["clearCoat"] = material.clearCoat;
    root["thinFilm"] = material.thinFilm;
    root["subsurface"] = material.subsurface;

    json layers = json::array();
    for (const Material::Layer& layer : material.layers) {
        json layerJson = json::object();
        layerJson["type"] = static_cast<int>(layer.type);
        layerJson["color"] = ToJson(layer.color);
        layerJson["weight"] = layer.weight;
        layerJson["roughness"] = layer.roughness;
        layerJson["metallic"] = layer.metallic;
        layerJson["transmission"] = layer.transmission;
        layerJson["ior"] = layer.ior;
        layerJson["transmissionRoughness"] = layer.transmissionRoughness;
        layerJson["thinWalled"] = layer.thinWalled;
        layers.push_back(std::move(layerJson));
    }
    root["layers"] = std::move(layers);
    return root;
}

bool ParseMaterial(const json& value, Material& outMaterial) {
    if (!value.is_object()) {
        return false;
    }

    outMaterial.id = value.value("id", 0ull);
    outMaterial.name = value.value("name", "Material");
    outMaterial.baseMaterial = static_cast<BaseMaterial>(value.value("surfacePreset", 0));
    outMaterial.emissionStrength = value.value("emissionStrength", 0.0f);
    outMaterial.roughness = value.value("roughness", 0.3f);
    outMaterial.metallic = value.value("metallic", 0.0f);
    outMaterial.transmission = value.value("transmission", 0.0f);
    outMaterial.ior = value.value("ior", 1.5f);
    outMaterial.thinWalled = value.value("thinWalled", false);
    outMaterial.absorptionDistance = value.value("absorptionDistance", 1.0f);
    outMaterial.transmissionRoughness = value.value("transmissionRoughness", 0.0f);
    outMaterial.clearCoat = value.value("clearCoat", 0.0f);
    outMaterial.thinFilm = value.value("thinFilm", 0.0f);
    outMaterial.subsurface = value.value("subsurface", 0.0f);
    outMaterial.layers.clear();

    if (!FromJson(value.value("baseColor", json::array({ 0.8f, 0.8f, 0.8f })), outMaterial.baseColor) ||
        !FromJson(value.value("emissionColor", json::array({ 1.0f, 1.0f, 1.0f })), outMaterial.emissionColor) ||
        !FromJson(value.value("absorptionColor", json::array({ 1.0f, 1.0f, 1.0f })), outMaterial.absorptionColor)) {
        return false;
    }

    const json layers = value.value("layers", json::array());
    if (layers.is_array() && !layers.empty()) {
        for (const json& layerValue : layers) {
            if (!layerValue.is_object()) {
                return false;
            }

            Material::Layer layer;
            layer.type = static_cast<MaterialLayerType>(layerValue.value("type", static_cast<int>(MaterialLayerType::BaseDiffuse)));
            layer.weight = layerValue.value("weight", 1.0f);
            layer.roughness = layerValue.value("roughness", 0.3f);
            layer.metallic = layerValue.value("metallic", 0.0f);
            layer.transmission = layerValue.value("transmission", 0.0f);
            layer.ior = layerValue.value("ior", 1.5f);
            layer.transmissionRoughness = layerValue.value("transmissionRoughness", 0.0f);
            layer.thinWalled = layerValue.value("thinWalled", false);
            if (!FromJson(layerValue.value("color", json::array({ 1.0f, 1.0f, 1.0f })), layer.color)) {
                return false;
            }
            outMaterial.layers.push_back(std::move(layer));
        }
        SyncLegacyMaterialFromLayers(outMaterial);
    } else {
        if (outMaterial.emissionStrength > 0.001f) {
            outMaterial.baseMaterial = BaseMaterial::Emissive;
        } else if (outMaterial.transmission > 0.05f) {
            outMaterial.baseMaterial = BaseMaterial::Glass;
        } else if (outMaterial.metallic > 0.5f) {
            outMaterial.baseMaterial = BaseMaterial::Metal;
        } else {
            outMaterial.baseMaterial = BaseMaterial::Diffuse;
        }
        SyncMaterialLayersFromLegacy(outMaterial);
    }

    return true;
}

json BuildPrimitiveJson(const Primitive& primitive) {
    json root = json::object();
    root["id"] = primitive.id;
    root["name"] = primitive.name;
    root["primitiveType"] = static_cast<int>(primitive.type);
    root["transform"] = ToJson(primitive.transform);
    root["materialId"] = primitive.materialId;
    root["visible"] = primitive.visible;
    return root;
}

bool ParsePrimitive(const json& value, Primitive& outPrimitive, const std::vector<Material>& materials) {
    if (!value.is_object()) {
        return false;
    }

    outPrimitive.id = value.value("id", 0ull);
    outPrimitive.name = value.value("name", "Primitive");
    outPrimitive.type = static_cast<PrimitiveType>(value.value("primitiveType", 0));
    outPrimitive.materialId = value.value("materialId", 0ull);
    outPrimitive.visible = value.value("visible", true);
    if (!FromJson(value.value("transform", json::object()), outPrimitive.transform)) {
        return false;
    }

    const bool materialExists = std::any_of(materials.begin(), materials.end(), [&outPrimitive](const Material& material) {
        return material.id == outPrimitive.materialId;
    });
    if (!materialExists && !materials.empty()) {
        outPrimitive.materialId = materials.front().id;
    }
    return true;
}

json BuildLegacySphereJson(const Primitive& primitive, const std::vector<Material>& materials) {
    int materialIndex = 0;
    for (std::size_t i = 0; i < materials.size(); ++i) {
        if (materials[i].id == primitive.materialId) {
            materialIndex = static_cast<int>(i);
            break;
        }
    }

    json root = json::object();
    root["id"] = primitive.id;
    root["name"] = primitive.name;
    root["transform"] = ToJson(primitive.transform);
    root["localCenter"] = ToJson(Vec3 {});
    root["radius"] = std::max(primitive.transform.scale.x, std::max(primitive.transform.scale.y, primitive.transform.scale.z)) * 0.5f;
    root["materialIndex"] = materialIndex;
    const Material* material = nullptr;
    for (const Material& candidate : materials) {
        if (candidate.id == primitive.materialId) {
            material = &candidate;
            break;
        }
    }
    root["albedoTint"] = ToJson(material != nullptr ? material->baseColor : Vec3 { 0.8f, 0.8f, 0.8f });
    return root;
}

json BuildLegacyMeshInstanceJson(const Primitive& primitive, const std::vector<Material>& materials) {
    json root = json::object();
    root["id"] = primitive.id;
    root["name"] = primitive.name;
    root["meshIndex"] = -1;
    root["transform"] = ToJson(primitive.transform);
    const Material* material = nullptr;
    for (const Material& candidate : materials) {
        if (candidate.id == primitive.materialId) {
            material = &candidate;
            break;
        }
    }
    root["colorTint"] = ToJson(material != nullptr ? material->baseColor : Vec3 { 0.8f, 0.8f, 0.8f });
    root["primitiveType"] = static_cast<int>(primitive.type);
    root["materialId"] = primitive.materialId;
    root["visible"] = primitive.visible;
    return root;
}

json BuildLightJson(const Light& light) {
    json root = json::object();
    root["id"] = light.id;
    root["name"] = light.name;
    root["type"] = static_cast<int>(light.type);
    root["transform"] = ToJson(light.transform);
    root["color"] = ToJson(light.color);
    root["intensity"] = light.intensity;
    root["areaSize"] = ToJson(light.areaSize);
    root["range"] = light.range;
    root["innerConeDegrees"] = light.innerConeDegrees;
    root["outerConeDegrees"] = light.outerConeDegrees;
    root["enabled"] = light.enabled;
    return root;
}

bool ParseLight(const json& value, Light& outLight) {
    if (!value.is_object()) {
        return false;
    }

    outLight.id = value.value("id", 0ull);
    outLight.name = value.value("name", "Light");
    outLight.type = static_cast<LightType>(value.value("type", 0));
    outLight.intensity = value.value("intensity", 10.0f);
    outLight.range = value.value("range", 20.0f);
    outLight.innerConeDegrees = value.value("innerConeDegrees", 18.0f);
    outLight.outerConeDegrees = value.value("outerConeDegrees", 32.0f);
    outLight.enabled = value.value("enabled", true);
    if (!FromJson(value.value("transform", json::object()), outLight.transform) ||
        !FromJson(value.value("color", json::array({ 1.0f, 1.0f, 1.0f })), outLight.color) ||
        !FromJson(value.value("areaSize", json::array({ 1.0f, 1.0f })), outLight.areaSize)) {
        return false;
    }
    return true;
}

json BuildSnapshotJson(const Snapshot& snapshot) {
    json root = json::object();
    root["version"] = kSnapshotVersion;
    root["cameraObjectId"] = snapshot.cameraId;
    root["label"] = snapshot.metadata.label;
    root["description"] = snapshot.metadata.description;
    root["backgroundMode"] = snapshot.metadata.backgroundMode;
    root["environmentEnabled"] = snapshot.metadata.environmentEnabled;
    root["environmentIntensity"] = snapshot.metadata.environmentIntensity;
    root["fogEnabled"] = snapshot.metadata.fogEnabled;
    root["fogColor"] = ToJson(snapshot.metadata.fogColor);
    root["fogDensity"] = snapshot.metadata.fogDensity;
    root["fogAnisotropy"] = snapshot.metadata.fogAnisotropy;
    root["importedAssets"] = json::array();
    root["importedTextures"] = json::array();

    json materials = json::array();
    for (const Material& material : snapshot.materials) {
        materials.push_back(BuildMaterialJson(material));
    }
    root["materials"] = std::move(materials);

    json primitives = json::array();
    json spheres = json::array();
    json meshInstances = json::array();
    for (const Primitive& primitive : snapshot.primitives) {
        primitives.push_back(BuildPrimitiveJson(primitive));
        if (primitive.type == PrimitiveType::Sphere) {
            spheres.push_back(BuildLegacySphereJson(primitive, snapshot.materials));
        } else {
            meshInstances.push_back(BuildLegacyMeshInstanceJson(primitive, snapshot.materials));
        }
    }
    root["primitives"] = std::move(primitives);
    root["spheres"] = std::move(spheres);
    root["meshInstances"] = std::move(meshInstances);
    root["meshes"] = json::array();
    root["triangles"] = json::array();

    json lights = json::array();
    for (const Light& light : snapshot.lights) {
        lights.push_back(BuildLightJson(light));
    }
    root["lights"] = std::move(lights);

    json camera = json::object();
    camera["position"] = ToJson(snapshot.camera.position);
    camera["yawDegrees"] = snapshot.camera.yawDegrees;
    camera["pitchDegrees"] = snapshot.camera.pitchDegrees;
    camera["fieldOfViewDegrees"] = snapshot.camera.fieldOfViewDegrees;
    camera["focusDistance"] = snapshot.camera.focusDistance;
    camera["apertureRadius"] = snapshot.camera.apertureRadius;
    camera["exposure"] = snapshot.camera.exposure;
    root["camera"] = std::move(camera);
    return root;
}

bool ParseSnapshotJson(const json& root, Snapshot& outSnapshot) {
    if (!root.is_object()) {
        return false;
    }

    outSnapshot.cameraId = root.value("cameraObjectId", RenderContracts::kCameraObjectId);
    outSnapshot.metadata.label = root.value("label", "Loaded Render Scene");
    outSnapshot.metadata.description = root.value("description", "Loaded render scene.");
    outSnapshot.metadata.backgroundMode = root.value("backgroundMode", 0);
    outSnapshot.metadata.environmentEnabled = root.value("environmentEnabled", true);
    outSnapshot.metadata.environmentIntensity = root.value("environmentIntensity", 1.0f);
    outSnapshot.metadata.fogEnabled = root.value("fogEnabled", false);
    outSnapshot.metadata.fogDensity = root.value("fogDensity", 0.0f);
    outSnapshot.metadata.fogAnisotropy = root.value("fogAnisotropy", 0.0f);
    if (!FromJson(root.value("fogColor", json::array({ 0.82f, 0.88f, 0.96f })), outSnapshot.metadata.fogColor)) {
        return false;
    }

    outSnapshot.materials.clear();
    const json materials = root.value("materials", json::array());
    if (!materials.is_array()) {
        return false;
    }
    for (const json& materialValue : materials) {
        Material material;
        if (!ParseMaterial(materialValue, material)) {
            return false;
        }
        outSnapshot.materials.push_back(std::move(material));
    }

    outSnapshot.primitives.clear();
    const json primitives = root.value("primitives", json());
    if (primitives.is_array()) {
        for (const json& primitiveValue : primitives) {
            Primitive primitive;
            if (!ParsePrimitive(primitiveValue, primitive, outSnapshot.materials)) {
                return false;
            }
            outSnapshot.primitives.push_back(std::move(primitive));
        }
    } else {
        const json spheres = root.value("spheres", json::array());
        if (!spheres.is_array()) {
            return false;
        }
        for (const json& sphereValue : spheres) {
            Primitive primitive;
            primitive.id = sphereValue.value("id", 0ull);
            primitive.name = sphereValue.value("name", "Sphere");
            primitive.type = PrimitiveType::Sphere;
            primitive.visible = true;
            if (!FromJson(sphereValue.value("transform", json::object()), primitive.transform)) {
                return false;
            }
            const int materialIndex = sphereValue.value("materialIndex", 0);
            if (materialIndex >= 0 && materialIndex < static_cast<int>(outSnapshot.materials.size())) {
                primitive.materialId = outSnapshot.materials[materialIndex].id;
            } else if (!outSnapshot.materials.empty()) {
                primitive.materialId = outSnapshot.materials.front().id;
            }
            outSnapshot.primitives.push_back(std::move(primitive));
        }

        const json meshInstances = root.value("meshInstances", json::array());
        if (!meshInstances.is_array()) {
            return false;
        }
        for (const json& instanceValue : meshInstances) {
            Primitive primitive;
            primitive.id = instanceValue.value("id", 0ull);
            primitive.name = instanceValue.value("name", "Mesh Instance");
            primitive.type = static_cast<PrimitiveType>(instanceValue.value("primitiveType", static_cast<int>(PrimitiveType::Cube)));
            primitive.materialId = instanceValue.value("materialId", !outSnapshot.materials.empty() ? outSnapshot.materials.front().id : 0ull);
            primitive.visible = instanceValue.value("visible", true);
            if (!FromJson(instanceValue.value("transform", json::object()), primitive.transform)) {
                return false;
            }
            outSnapshot.primitives.push_back(std::move(primitive));
        }
    }

    outSnapshot.lights.clear();
    const json lights = root.value("lights", json::array());
    if (!lights.is_array()) {
        return false;
    }
    for (const json& lightValue : lights) {
        Light light;
        if (!ParseLight(lightValue, light)) {
            return false;
        }
        outSnapshot.lights.push_back(std::move(light));
    }

    const json camera = root.value("camera", json::object());
    if (!camera.is_object()) {
        return false;
    }

    outSnapshot.camera.yawDegrees = camera.value("yawDegrees", -34.0f);
    outSnapshot.camera.pitchDegrees = camera.value("pitchDegrees", -16.0f);
    outSnapshot.camera.fieldOfViewDegrees = camera.value("fieldOfViewDegrees", 50.0f);
    outSnapshot.camera.focusDistance = camera.value("focusDistance", 6.0f);
    outSnapshot.camera.apertureRadius = camera.value("apertureRadius", 0.0f);
    outSnapshot.camera.exposure = camera.value("exposure", 1.0f);

    const json position = camera.value("position", json());
    if (position.is_array()) {
        if (!FromJson(position, outSnapshot.camera.position)) {
            return false;
        }
    } else {
        const Vec3 forward = ForwardFromYawPitch(outSnapshot.camera.yawDegrees, outSnapshot.camera.pitchDegrees);
        outSnapshot.camera.position = Vec3 { 0.0f, 0.75f, 0.0f } - (forward * std::max(outSnapshot.camera.focusDistance, 0.5f));
    }

    return true;
}

json BuildSettingsJson(const Settings& settings) {
    json root = json::object();
    root["resolutionX"] = settings.resolutionX;
    root["resolutionY"] = settings.resolutionY;
    root["previewSampleTarget"] = settings.previewSampleTarget;
    root["accumulationEnabled"] = settings.accumulationEnabled;
    root["integratorMode"] = settings.viewMode == ViewMode::PathTrace ? 1 : 0;
    root["pathTraceDebugMode"] = static_cast<int>(settings.pathTraceDebugMode);
    root["pathTraceDebugPixelX"] = settings.pathTraceDebugPixelX;
    root["pathTraceDebugPixelY"] = settings.pathTraceDebugPixelY;
    root["maxBounceCount"] = settings.maxBounceCount;
    root["displayMode"] = 0;
    root["tonemapMode"] = 0;
    root["gizmoMode"] = static_cast<int>(settings.transformMode);
    root["transformSpace"] = static_cast<int>(settings.transformSpace);
    root["debugViewMode"] = static_cast<int>(settings.pathTraceDebugMode);
    root["bvhTraversalEnabled"] = true;

    json finalRender = json::object();
    finalRender["resolutionX"] = settings.finalRender.resolutionX;
    finalRender["resolutionY"] = settings.finalRender.resolutionY;
    finalRender["sampleTarget"] = settings.finalRender.sampleTarget;
    finalRender["maxBounceCount"] = settings.finalRender.maxBounceCount;
    finalRender["outputName"] = settings.finalRender.outputName;
    root["finalRender"] = std::move(finalRender);
    return root;
}

void ParseSettingsJson(const json& root, Settings& settings) {
    if (!root.is_object()) {
        return;
    }

    settings.resolutionX = root.value("resolutionX", settings.resolutionX);
    settings.resolutionY = root.value("resolutionY", settings.resolutionY);
    settings.previewSampleTarget = root.value("previewSampleTarget", settings.previewSampleTarget);
    settings.accumulationEnabled = root.value("accumulationEnabled", settings.accumulationEnabled);
    settings.viewMode = root.value("integratorMode", 0) == 1 ? ViewMode::PathTrace : ViewMode::Unlit;
    settings.pathTraceDebugMode = static_cast<PathTraceDebugMode>(
        root.value("pathTraceDebugMode", root.value("debugViewMode", static_cast<int>(settings.pathTraceDebugMode))));
    settings.pathTraceDebugPixelX = root.value("pathTraceDebugPixelX", settings.pathTraceDebugPixelX);
    settings.pathTraceDebugPixelY = root.value("pathTraceDebugPixelY", settings.pathTraceDebugPixelY);
    settings.maxBounceCount = root.value("maxBounceCount", settings.maxBounceCount);
    settings.transformMode = static_cast<TransformMode>(root.value("gizmoMode", static_cast<int>(settings.transformMode)));
    settings.transformSpace = static_cast<TransformSpace>(root.value("transformSpace", static_cast<int>(settings.transformSpace)));

    const json finalRender = root.value("finalRender", json::object());
    if (finalRender.is_object()) {
        settings.finalRender.resolutionX = finalRender.value("resolutionX", settings.finalRender.resolutionX);
        settings.finalRender.resolutionY = finalRender.value("resolutionY", settings.finalRender.resolutionY);
        settings.finalRender.sampleTarget = finalRender.value("sampleTarget", settings.finalRender.sampleTarget);
        settings.finalRender.maxBounceCount = finalRender.value("maxBounceCount", settings.finalRender.maxBounceCount);
        settings.finalRender.outputName = finalRender.value("outputName", settings.finalRender.outputName);
    }
}

} // namespace

StackBinaryFormat::json BuildPayload(const State& state, const std::string& latestFinalAssetFileName) {
    json root = json::object();
    root["snapshot"] = BuildSnapshotJson(state.BuildSnapshot());
    root["settings"] = BuildSettingsJson(state.GetSettings());
    root["latestFinalAssetFileName"] = latestFinalAssetFileName;
    return root;
}

bool ApplyPayload(
    const StackBinaryFormat::json& payload,
    State& state,
    const std::string& projectName,
    const std::string& projectFileName,
    std::string& latestFinalAssetFileName,
    std::string& errorMessage) {

    errorMessage.clear();
    latestFinalAssetFileName.clear();

    const json snapshotJson = payload.contains("snapshot") ? payload["snapshot"] : payload;
    Snapshot snapshot;
    if (!ParseSnapshotJson(snapshotJson, snapshot)) {
        errorMessage = "The saved render project payload is invalid.";
        return false;
    }

    Settings settings = state.GetSettings();
    ParseSettingsJson(payload.value("settings", json::object()), settings);
    latestFinalAssetFileName = payload.value("latestFinalAssetFileName", std::string());
    state.ApplyLoadedState(std::move(snapshot), settings, projectName, projectFileName);
    return true;
}

} // namespace RenderFoundation::Serialization
