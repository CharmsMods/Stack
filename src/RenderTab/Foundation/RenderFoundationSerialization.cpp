#include "RenderFoundationSerialization.h"

#include "RenderTab/Contracts/RenderContracts.h"
#include "Utils/Base64.h"

#include <algorithm>
#include <utility>

namespace RenderFoundation::Serialization {

namespace {

using json = StackBinaryFormat::json;

constexpr int kSnapshotVersion = 11;

json ToJson(const Vec2& value) {
    return json::array({ value.x, value.y });
}

json ToJson(const Vec3& value) {
    return json::array({ value.x, value.y, value.z });
}

json ToJson(const RenderFloat2& value) {
    return json::array({ value.x, value.y });
}

json ToJson(const RenderFloat3& value) {
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

bool FromJson(const json& value, RenderFloat2& outValue) {
    if (!value.is_array() || value.size() != 2) {
        return false;
    }

    outValue.x = value[0].get<float>();
    outValue.y = value[1].get<float>();
    return true;
}

bool FromJson(const json& value, RenderFloat3& outValue) {
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

json BuildTextureRefJson(const RenderMaterialTextureRef& textureRef) {
    json root = json::object();
    root["textureIndex"] = textureRef.textureIndex;
    root["uvSet"] = textureRef.uvSet;
    return root;
}

bool ParseTextureRef(const json& value, RenderMaterialTextureRef& outTextureRef) {
    if (!value.is_object()) {
        return false;
    }

    outTextureRef.textureIndex = value.value("textureIndex", -1);
    outTextureRef.uvSet = value.value("uvSet", 0);
    return true;
}

json BuildImportedAssetJson(const RenderImportedAsset& asset) {
    json root = json::object();
    root["name"] = asset.name;
    root["sourcePath"] = asset.sourcePath;
    root["binaryContainer"] = asset.binaryContainer;
    return root;
}

bool ParseImportedAsset(const json& value, RenderImportedAsset& outAsset) {
    if (!value.is_object()) {
        return false;
    }

    outAsset.name = value.value("name", "Imported Asset");
    outAsset.sourcePath = value.value("sourcePath", std::string());
    outAsset.binaryContainer = value.value("binaryContainer", false);
    return true;
}

json BuildImportedTextureJson(const RenderImportedTexture& texture) {
    json root = json::object();
    root["name"] = texture.name;
    root["sourcePath"] = texture.sourcePath;
    root["sourceUri"] = texture.sourceUri;
    root["sourceKind"] = static_cast<int>(texture.sourceKind);
    root["semantic"] = static_cast<int>(texture.semantic);
    root["sourceAssetIndex"] = texture.sourceAssetIndex;
    root["sourceImageIndex"] = texture.sourceImageIndex;
    root["width"] = texture.width;
    root["height"] = texture.height;
    root["channels"] = texture.channels;
    root["pixelsBase64"] = Utils::Base64Encode(texture.pixels);
    return root;
}

bool ParseImportedTexture(const json& value, RenderImportedTexture& outTexture) {
    if (!value.is_object()) {
        return false;
    }

    outTexture.name = value.value("name", "Imported Texture");
    outTexture.sourcePath = value.value("sourcePath", std::string());
    outTexture.sourceUri = value.value("sourceUri", std::string());
    outTexture.sourceKind = static_cast<RenderImportedTextureSourceKind>(value.value("sourceKind", 0));
    outTexture.semantic = static_cast<RenderTextureSemantic>(value.value("semantic", 0));
    outTexture.sourceAssetIndex = value.value("sourceAssetIndex", -1);
    outTexture.sourceImageIndex = value.value("sourceImageIndex", -1);
    outTexture.width = value.value("width", 0);
    outTexture.height = value.value("height", 0);
    outTexture.channels = value.value("channels", 4);
    outTexture.pixels = Utils::Base64Decode(value.value("pixelsBase64", std::string()));
    return true;
}

json BuildMeshTriangleJson(const RenderMeshTriangle& triangle) {
    json root = json::object();
    root["name"] = triangle.name;
    root["localA"] = ToJson(triangle.localA);
    root["localB"] = ToJson(triangle.localB);
    root["localC"] = ToJson(triangle.localC);
    root["localNormalA"] = ToJson(triangle.localNormalA);
    root["localNormalB"] = ToJson(triangle.localNormalB);
    root["localNormalC"] = ToJson(triangle.localNormalC);
    root["uvA"] = ToJson(triangle.uvA);
    root["uvB"] = ToJson(triangle.uvB);
    root["uvC"] = ToJson(triangle.uvC);
    root["materialIndex"] = triangle.materialIndex;
    root["albedoTint"] = ToJson(triangle.albedoTint);
    return root;
}

bool ParseMeshTriangle(const json& value, RenderMeshTriangle& outTriangle) {
    if (!value.is_object()) {
        return false;
    }

    outTriangle.name = value.value("name", "Triangle");
    outTriangle.materialIndex = value.value("materialIndex", 0);
    if (!FromJson(value.value("localA", json::array({ 0.0f, 0.0f, 0.0f })), outTriangle.localA) ||
        !FromJson(value.value("localB", json::array({ 0.0f, 0.0f, 0.0f })), outTriangle.localB) ||
        !FromJson(value.value("localC", json::array({ 0.0f, 0.0f, 0.0f })), outTriangle.localC) ||
        !FromJson(value.value("localNormalA", json::array({ 0.0f, 1.0f, 0.0f })), outTriangle.localNormalA) ||
        !FromJson(value.value("localNormalB", json::array({ 0.0f, 1.0f, 0.0f })), outTriangle.localNormalB) ||
        !FromJson(value.value("localNormalC", json::array({ 0.0f, 1.0f, 0.0f })), outTriangle.localNormalC) ||
        !FromJson(value.value("uvA", json::array({ 0.0f, 0.0f })), outTriangle.uvA) ||
        !FromJson(value.value("uvB", json::array({ 1.0f, 0.0f })), outTriangle.uvB) ||
        !FromJson(value.value("uvC", json::array({ 0.0f, 1.0f })), outTriangle.uvC) ||
        !FromJson(value.value("albedoTint", json::array({ 1.0f, 1.0f, 1.0f })), outTriangle.albedoTint)) {
        return false;
    }

    return true;
}

json BuildImportedMeshJson(const RenderMeshDefinition& mesh) {
    json root = json::object();
    root["name"] = mesh.name;
    root["sourceAssetIndex"] = mesh.sourceAssetIndex;
    root["sourceMeshName"] = mesh.sourceMeshName;
    json triangles = json::array();
    for (const RenderMeshTriangle& triangle : mesh.triangles) {
        triangles.push_back(BuildMeshTriangleJson(triangle));
    }
    root["triangles"] = std::move(triangles);
    return root;
}

bool ParseImportedMesh(const json& value, RenderMeshDefinition& outMesh) {
    if (!value.is_object()) {
        return false;
    }

    std::vector<RenderMeshTriangle> triangles;
    const json triangleValues = value.value("triangles", json::array());
    if (!triangleValues.is_array()) {
        return false;
    }
    triangles.reserve(triangleValues.size());
    for (const json& triangleValue : triangleValues) {
        RenderMeshTriangle triangle;
        if (!ParseMeshTriangle(triangleValue, triangle)) {
            return false;
        }
        triangles.push_back(std::move(triangle));
    }

    outMesh = BuildRenderMeshDefinition(value.value("name", "Imported Mesh"), std::move(triangles));
    outMesh.sourceAssetIndex = value.value("sourceAssetIndex", -1);
    outMesh.sourceMeshName = value.value("sourceMeshName", std::string());
    return true;
}

json BuildMaterialJson(const Material& material) {
    json root = json::object();
    root["id"] = material.id;
    root["name"] = material.name;
    root["isTemplate"] = material.isTemplate;
    root["importedSource"] = material.importedSource;
    root["surfacePreset"] = static_cast<int>(material.baseMaterial);
    root["sourceAssetIndex"] = material.sourceAssetIndex;
    root["sourceMaterialName"] = material.sourceMaterialName;
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
    root["baseColorTexture"] = BuildTextureRefJson(material.baseColorTexture);
    root["metallicRoughnessTexture"] = BuildTextureRefJson(material.metallicRoughnessTexture);
    root["emissiveTexture"] = BuildTextureRefJson(material.emissiveTexture);
    root["normalTexture"] = BuildTextureRefJson(material.normalTexture);

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
    outMaterial.isTemplate = value.value("isTemplate", false);
    outMaterial.importedSource = value.value("importedSource", false);
    outMaterial.baseMaterial = static_cast<BaseMaterial>(value.value("surfacePreset", 0));
    outMaterial.sourceAssetIndex = value.value("sourceAssetIndex", -1);
    outMaterial.sourceMaterialName = value.value("sourceMaterialName", std::string());
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

    const json baseColorTexture = value.value("baseColorTexture", json::object());
    if (baseColorTexture.is_object() && !ParseTextureRef(baseColorTexture, outMaterial.baseColorTexture)) {
        return false;
    }
    const json metallicRoughnessTexture = value.value("metallicRoughnessTexture", json::object());
    if (metallicRoughnessTexture.is_object() && !ParseTextureRef(metallicRoughnessTexture, outMaterial.metallicRoughnessTexture)) {
        return false;
    }
    const json emissiveTexture = value.value("emissiveTexture", json::object());
    if (emissiveTexture.is_object() && !ParseTextureRef(emissiveTexture, outMaterial.emissiveTexture)) {
        return false;
    }
    const json normalTexture = value.value("normalTexture", json::object());
    if (normalTexture.is_object() && !ParseTextureRef(normalTexture, outMaterial.normalTexture)) {
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
    root["meshIndex"] = primitive.meshIndex;
    root["importedMaterialMode"] = static_cast<int>(primitive.importedMaterialMode);
    root["importedMaterialBlend"] = primitive.importedMaterialBlend;
    root["colorTint"] = ToJson(primitive.colorTint);
    root["visible"] = primitive.visible;
    root["importedAssetLabel"] = primitive.importedAssetLabel;
    root["importedPartCount"] = primitive.importedPartCount;
    root["importedTriangleCount"] = primitive.importedTriangleCount;
    root["importedMaterialCount"] = primitive.importedMaterialCount;
    root["importedAppliedScale"] = primitive.importedAppliedScale;
    root["importedScaleMode"] = static_cast<int>(primitive.importedScaleMode);
    root["importedLocalBoundsMin"] = ToJson(primitive.importedLocalBoundsMin);
    root["importedLocalBoundsMax"] = ToJson(primitive.importedLocalBoundsMax);
    root["importedWarningText"] = primitive.importedWarningText;
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
    outPrimitive.meshIndex = value.value("meshIndex", -1);
    outPrimitive.importedMaterialMode = static_cast<ImportedMaterialMode>(
        value.value("importedMaterialMode", static_cast<int>(ImportedMaterialMode::UseImported)));
    outPrimitive.importedMaterialBlend = value.value("importedMaterialBlend", 0.5f);
    outPrimitive.visible = value.value("visible", true);
    outPrimitive.importedAssetLabel = value.value("importedAssetLabel", std::string());
    outPrimitive.importedPartCount = value.value("importedPartCount", 0);
    outPrimitive.importedTriangleCount = value.value("importedTriangleCount", 0);
    outPrimitive.importedMaterialCount = value.value("importedMaterialCount", 0);
    outPrimitive.importedAppliedScale = value.value("importedAppliedScale", 1.0f);
    outPrimitive.importedScaleMode = static_cast<ImportedModelScaleMode>(
        value.value("importedScaleMode", static_cast<int>(ImportedModelScaleMode::AutoFit)));
    outPrimitive.importedWarningText = value.value("importedWarningText", std::string());
    if (!FromJson(value.value("transform", json::object()), outPrimitive.transform)) {
        return false;
    }
    if (!FromJson(value.value("colorTint", json::array({ 1.0f, 1.0f, 1.0f })), outPrimitive.colorTint)) {
        return false;
    }
    if (!FromJson(value.value("importedLocalBoundsMin", json::array({ 0.0f, 0.0f, 0.0f })), outPrimitive.importedLocalBoundsMin) ||
        !FromJson(value.value("importedLocalBoundsMax", json::array({ 0.0f, 0.0f, 0.0f })), outPrimitive.importedLocalBoundsMax)) {
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
    root["albedoTint"] = ToJson(primitive.colorTint);
    return root;
}

json BuildLegacyMeshInstanceJson(const Primitive& primitive, const std::vector<Material>& materials) {
    (void)materials;
    json root = json::object();
    root["id"] = primitive.id;
    root["name"] = primitive.name;
    root["meshIndex"] = primitive.meshIndex;
    root["transform"] = ToJson(primitive.transform);
    root["colorTint"] = ToJson(primitive.colorTint);
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
    root["laserWavelengthNm"] = light.laserWavelengthNm;
    root["laserLinewidthNm"] = light.laserLinewidthNm;
    root["laserApertureRadius"] = light.laserApertureRadius;
    root["laserBeamWaistRadius"] = light.laserBeamWaistRadius;
    root["laserBeamQuality"] = light.laserBeamQuality;
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
    outLight.laserWavelengthNm = value.value("laserWavelengthNm", 532.0f);
    outLight.laserLinewidthNm = value.value("laserLinewidthNm", 1.0f);
    outLight.laserApertureRadius = value.value("laserApertureRadius", 0.01f);
    outLight.laserBeamWaistRadius = value.value("laserBeamWaistRadius", 0.002f);
    outLight.laserBeamQuality = value.value("laserBeamQuality", 1.0f);
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

    json importedAssets = json::array();
    for (const RenderImportedAsset& asset : snapshot.importedAssets) {
        importedAssets.push_back(BuildImportedAssetJson(asset));
    }
    root["importedAssets"] = std::move(importedAssets);

    json importedTextures = json::array();
    for (const RenderImportedTexture& texture : snapshot.importedTextures) {
        importedTextures.push_back(BuildImportedTextureJson(texture));
    }
    root["importedTextures"] = std::move(importedTextures);

    json importedMeshes = json::array();
    for (const RenderMeshDefinition& mesh : snapshot.importedMeshes) {
        importedMeshes.push_back(BuildImportedMeshJson(mesh));
    }
    root["importedMeshes"] = std::move(importedMeshes);

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
    camera["whiteBalanceTemperature"] = snapshot.camera.whiteBalanceTemperature;
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

    outSnapshot.importedAssets.clear();
    const json importedAssets = root.value("importedAssets", json::array());
    if (!importedAssets.is_array()) {
        return false;
    }
    for (const json& assetValue : importedAssets) {
        RenderImportedAsset asset;
        if (!ParseImportedAsset(assetValue, asset)) {
            return false;
        }
        outSnapshot.importedAssets.push_back(std::move(asset));
    }

    outSnapshot.importedTextures.clear();
    const json importedTextures = root.value("importedTextures", json::array());
    if (!importedTextures.is_array()) {
        return false;
    }
    for (const json& textureValue : importedTextures) {
        RenderImportedTexture texture;
        if (!ParseImportedTexture(textureValue, texture)) {
            return false;
        }
        outSnapshot.importedTextures.push_back(std::move(texture));
    }

    outSnapshot.importedMeshes.clear();
    const json importedMeshes = root.value("importedMeshes", json::array());
    if (!importedMeshes.is_array()) {
        return false;
    }
    for (const json& meshValue : importedMeshes) {
        RenderMeshDefinition mesh;
        if (!ParseImportedMesh(meshValue, mesh)) {
            return false;
        }
        outSnapshot.importedMeshes.push_back(std::move(mesh));
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
                primitive.materialId = outSnapshot.materials[static_cast<std::size_t>(materialIndex)].id;
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
            primitive.meshIndex = instanceValue.value("meshIndex", -1);
            primitive.visible = instanceValue.value("visible", true);
            if (!FromJson(instanceValue.value("transform", json::object()), primitive.transform) ||
                !FromJson(instanceValue.value("colorTint", json::array({ 1.0f, 1.0f, 1.0f })), primitive.colorTint)) {
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
    outSnapshot.camera.fieldOfViewDegrees = camera.value("fieldOfViewDegrees", 80.0f);
    outSnapshot.camera.focusDistance = camera.value("focusDistance", 6.0f);
    outSnapshot.camera.apertureRadius = camera.value("apertureRadius", 0.0f);
    outSnapshot.camera.exposure = camera.value("exposure", 1.0f);
    outSnapshot.camera.whiteBalanceTemperature = camera.value("whiteBalanceTemperature", 6500.0f);

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
    root["viewportResolutionOverrideEnabled"] = settings.viewportResolutionOverrideEnabled;
    root["viewportResolutionX"] = settings.viewportResolutionX;
    root["viewportResolutionY"] = settings.viewportResolutionY;
    root["viewportPerformanceMode"] = static_cast<int>(settings.viewportPerformanceMode);
    root["integratorMode"] = settings.viewMode == ViewMode::PathTrace ? 1 : 0;
    root["pathTraceTransportMode"] = static_cast<int>(settings.pathTraceTransportMode);
    root["pathTraceDebugMode"] = static_cast<int>(settings.pathTraceDebugMode);
    root["pathTraceDebugPixelX"] = settings.pathTraceDebugPixelX;
    root["pathTraceDebugPixelY"] = settings.pathTraceDebugPixelY;
    root["maxBounceCount"] = settings.maxBounceCount;
    root["pathTraceTerminationMode"] = static_cast<int>(settings.pathTraceTerminationMode);
    root["displayMode"] = 0;
    root["tonemapMode"] = 0;
    root["gizmoMode"] = static_cast<int>(settings.transformMode);
    root["transformSpace"] = static_cast<int>(settings.transformSpace);
    root["debugViewMode"] = static_cast<int>(settings.pathTraceDebugMode);
    root["bvhTraversalEnabled"] = true;

    json denoiser = json::object();
    denoiser["enabled"] = settings.denoiser.enabled;
    denoiser["mode"] = static_cast<int>(settings.denoiser.mode);
    denoiser["debugView"] = static_cast<int>(settings.denoiser.debugView);
    denoiser["fireflyClampEnabled"] = settings.denoiser.fireflyClampEnabled;
    denoiser["fireflyClampThreshold"] = settings.denoiser.fireflyClampThreshold;
    denoiser["bilateralRadius"] = settings.denoiser.bilateralRadius;
    denoiser["bilateralSpatialSigma"] = settings.denoiser.bilateralSpatialSigma;
    denoiser["bilateralColorSigma"] = settings.denoiser.bilateralColorSigma;
    denoiser["bilateralDepthPhi"] = settings.denoiser.bilateralDepthPhi;
    denoiser["bilateralNormalPhi"] = settings.denoiser.bilateralNormalPhi;
    denoiser["bilateralAlbedoPhi"] = settings.denoiser.bilateralAlbedoPhi;
    denoiser["atrousPassCount"] = settings.denoiser.atrousPassCount;
    denoiser["atrousDepthPhi"] = settings.denoiser.atrousDepthPhi;
    denoiser["atrousNormalPhi"] = settings.denoiser.atrousNormalPhi;
    denoiser["atrousAlbedoPhi"] = settings.denoiser.atrousAlbedoPhi;
    denoiser["varianceEnabled"] = settings.denoiser.varianceEnabled;
    denoiser["varianceStrength"] = settings.denoiser.varianceStrength;
    denoiser["exportMode"] = static_cast<int>(settings.denoiser.exportMode);
    root["denoiser"] = std::move(denoiser);

    json cameraPreview = json::object();
    cameraPreview["resolutionX"] = settings.cameraPreview.resolutionX;
    cameraPreview["resolutionY"] = settings.cameraPreview.resolutionY;
    cameraPreview["maxBounceCount"] = settings.cameraPreview.maxBounceCount;
    cameraPreview["terminationMode"] = static_cast<int>(settings.cameraPreview.terminationMode);
    root["cameraPreview"] = std::move(cameraPreview);

    json finalRender = json::object();
    finalRender["resolutionX"] = settings.finalRender.resolutionX;
    finalRender["resolutionY"] = settings.finalRender.resolutionY;
    finalRender["sampleTarget"] = settings.finalRender.sampleTarget;
    finalRender["maxBounceCount"] = settings.finalRender.maxBounceCount;
    finalRender["terminationMode"] = static_cast<int>(settings.finalRender.terminationMode);
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
    settings.viewportResolutionOverrideEnabled = root.value(
        "viewportResolutionOverrideEnabled",
        settings.viewportResolutionOverrideEnabled);
    settings.viewportResolutionX = std::max(1, root.value("viewportResolutionX", settings.viewportResolutionX));
    settings.viewportResolutionY = std::max(1, root.value("viewportResolutionY", settings.viewportResolutionY));
    settings.viewportPerformanceMode = static_cast<ViewportPerformanceMode>(
        root.value("viewportPerformanceMode", static_cast<int>(settings.viewportPerformanceMode)));
    settings.viewMode = root.value("integratorMode", 0) == 1 ? ViewMode::PathTrace : ViewMode::Unlit;
    settings.pathTraceTransportMode = static_cast<PathTraceTransportMode>(
        root.value("pathTraceTransportMode", static_cast<int>(settings.pathTraceTransportMode)));
    settings.pathTraceDebugMode = static_cast<PathTraceDebugMode>(
        root.value("pathTraceDebugMode", root.value("debugViewMode", static_cast<int>(settings.pathTraceDebugMode))));
    settings.pathTraceDebugPixelX = root.value("pathTraceDebugPixelX", settings.pathTraceDebugPixelX);
    settings.pathTraceDebugPixelY = root.value("pathTraceDebugPixelY", settings.pathTraceDebugPixelY);
    settings.maxBounceCount = root.value("maxBounceCount", settings.maxBounceCount);
    settings.pathTraceTerminationMode = static_cast<PathTraceTerminationMode>(
        root.value("pathTraceTerminationMode", static_cast<int>(settings.pathTraceTerminationMode)));
    settings.transformMode = static_cast<TransformMode>(root.value("gizmoMode", static_cast<int>(settings.transformMode)));
    settings.transformSpace = static_cast<TransformSpace>(root.value("transformSpace", static_cast<int>(settings.transformSpace)));

    const json denoiser = root.value("denoiser", json::object());
    if (denoiser.is_object()) {
        settings.denoiser.enabled = denoiser.value("enabled", settings.denoiser.enabled);
        settings.denoiser.mode = static_cast<DenoiserMode>(
            denoiser.value("mode", static_cast<int>(settings.denoiser.mode)));
        settings.denoiser.debugView = static_cast<DenoiserDebugView>(
            denoiser.value("debugView", static_cast<int>(settings.denoiser.debugView)));
        settings.denoiser.fireflyClampEnabled = denoiser.value(
            "fireflyClampEnabled",
            settings.denoiser.fireflyClampEnabled);
        settings.denoiser.fireflyClampThreshold = denoiser.value(
            "fireflyClampThreshold",
            settings.denoiser.fireflyClampThreshold);
        settings.denoiser.bilateralRadius = std::clamp(
            denoiser.value("bilateralRadius", settings.denoiser.bilateralRadius),
            1,
            8);
        settings.denoiser.bilateralSpatialSigma = std::max(
            0.1f,
            denoiser.value("bilateralSpatialSigma", settings.denoiser.bilateralSpatialSigma));
        settings.denoiser.bilateralColorSigma = std::max(
            0.01f,
            denoiser.value("bilateralColorSigma", settings.denoiser.bilateralColorSigma));
        settings.denoiser.bilateralDepthPhi = std::max(
            0.01f,
            denoiser.value("bilateralDepthPhi", settings.denoiser.bilateralDepthPhi));
        settings.denoiser.bilateralNormalPhi = std::max(
            0.01f,
            denoiser.value("bilateralNormalPhi", settings.denoiser.bilateralNormalPhi));
        settings.denoiser.bilateralAlbedoPhi = std::max(
            0.01f,
            denoiser.value("bilateralAlbedoPhi", settings.denoiser.bilateralAlbedoPhi));
        settings.denoiser.atrousPassCount = std::clamp(
            denoiser.value("atrousPassCount", settings.denoiser.atrousPassCount),
            1,
            5);
        settings.denoiser.atrousDepthPhi = std::max(
            0.01f,
            denoiser.value("atrousDepthPhi", settings.denoiser.atrousDepthPhi));
        settings.denoiser.atrousNormalPhi = std::max(
            0.01f,
            denoiser.value("atrousNormalPhi", settings.denoiser.atrousNormalPhi));
        settings.denoiser.atrousAlbedoPhi = std::max(
            0.01f,
            denoiser.value("atrousAlbedoPhi", settings.denoiser.atrousAlbedoPhi));
        settings.denoiser.varianceEnabled = denoiser.value(
            "varianceEnabled",
            settings.denoiser.varianceEnabled);
        settings.denoiser.varianceStrength = std::max(
            0.0f,
            denoiser.value("varianceStrength", settings.denoiser.varianceStrength));
        settings.denoiser.exportMode = static_cast<DenoiserExportMode>(
            denoiser.value("exportMode", static_cast<int>(settings.denoiser.exportMode)));
    }

    const json cameraPreview = root.value("cameraPreview", json::object());
    if (cameraPreview.is_object()) {
        settings.cameraPreview.resolutionX = cameraPreview.value("resolutionX", settings.cameraPreview.resolutionX);
        settings.cameraPreview.resolutionY = cameraPreview.value("resolutionY", settings.cameraPreview.resolutionY);
        settings.cameraPreview.maxBounceCount = cameraPreview.value("maxBounceCount", settings.cameraPreview.maxBounceCount);
        settings.cameraPreview.terminationMode = static_cast<PathTraceTerminationMode>(
            cameraPreview.value("terminationMode", static_cast<int>(settings.cameraPreview.terminationMode)));
    }

    const json finalRender = root.value("finalRender", json::object());
    if (finalRender.is_object()) {
        settings.finalRender.resolutionX = finalRender.value("resolutionX", settings.finalRender.resolutionX);
        settings.finalRender.resolutionY = finalRender.value("resolutionY", settings.finalRender.resolutionY);
        settings.finalRender.sampleTarget = finalRender.value("sampleTarget", settings.finalRender.sampleTarget);
        settings.finalRender.maxBounceCount = finalRender.value("maxBounceCount", settings.finalRender.maxBounceCount);
        settings.finalRender.terminationMode = static_cast<PathTraceTerminationMode>(
            finalRender.value("terminationMode", static_cast<int>(settings.finalRender.terminationMode)));
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
