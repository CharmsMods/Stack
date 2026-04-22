#include "RenderSceneSerialization.h"

#include "ThirdParty/json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <utility>

namespace {

using json = nlohmann::json;

constexpr int kRenderSceneSnapshotVersion = 8;

RenderFloat3 BuildLegacyOrbitPosition(float yawDegrees, float pitchDegrees, float focusDistance) {
    const float yawRadians = yawDegrees * 0.01745329252f;
    const float pitchRadians = pitchDegrees * 0.01745329252f;
    const RenderFloat3 forward = Normalize(MakeRenderFloat3(
        std::cos(pitchRadians) * std::cos(yawRadians),
        std::sin(pitchRadians),
        std::cos(pitchRadians) * std::sin(yawRadians)));
    return Subtract(
        MakeRenderFloat3(0.0f, 0.75f, 0.0f),
        Scale(forward, std::max(focusDistance, 0.5f)));
}

std::string EncodeBase64(const std::vector<unsigned char>& data) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((data.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const unsigned int octetA = data[i];
        const unsigned int octetB = i + 1 < data.size() ? data[i + 1] : 0;
        const unsigned int octetC = i + 2 < data.size() ? data[i + 2] : 0;
        const unsigned int triple = (octetA << 16) | (octetB << 8) | octetC;
        encoded.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        encoded.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        encoded.push_back(i + 1 < data.size() ? kAlphabet[(triple >> 6) & 0x3F] : '=');
        encoded.push_back(i + 2 < data.size() ? kAlphabet[triple & 0x3F] : '=');
    }
    return encoded;
}

int DecodeBase64Char(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

bool DecodeBase64(const std::string& encoded, std::vector<unsigned char>& output) {
    output.clear();
    int buffer = 0;
    int bits = 0;
    for (char ch : encoded) {
        if (ch == '=') {
            break;
        }
        const int value = DecodeBase64Char(ch);
        if (value < 0) {
            continue;
        }
        buffer = (buffer << 6) | value;
        bits += 6;
        while (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<unsigned char>((buffer >> bits) & 0xFF));
        }
    }
    return encoded.empty() || !output.empty();
}

json ToJson(const RenderFloat3& value) {
    return json::array({ value.x, value.y, value.z });
}

bool FromJson(const json& value, RenderFloat3& result) {
    if (!value.is_array() || value.size() != 3) {
        return false;
    }

    result.x = value[0].get<float>();
    result.y = value[1].get<float>();
    result.z = value[2].get<float>();
    return true;
}

json ToJson(const RenderFloat2& value) {
    return json::array({ value.x, value.y });
}

bool FromJson(const json& value, RenderFloat2& result) {
    if (!value.is_array() || value.size() != 2) {
        return false;
    }

    result.x = value[0].get<float>();
    result.y = value[1].get<float>();
    return true;
}

json ToJson(const RenderMaterialTextureRef& value) {
    json result = json::object();
    result["textureIndex"] = value.textureIndex;
    result["uvSet"] = value.uvSet;
    return result;
}

bool FromJson(const json& value, RenderMaterialTextureRef& result) {
    if (!value.is_object()) {
        return false;
    }

    result.textureIndex = value.value("textureIndex", -1);
    result.uvSet = value.value("uvSet", 0);
    return true;
}

json ToJson(const RenderImportedAsset& asset) {
    json value = json::object();
    value["name"] = asset.name;
    value["sourcePath"] = asset.sourcePath;
    value["binaryContainer"] = asset.binaryContainer;
    return value;
}

bool FromJson(const json& value, RenderImportedAsset& result) {
    if (!value.is_object()) {
        return false;
    }

    result.name = value.value("name", "Imported Asset");
    result.sourcePath = value.value("sourcePath", "");
    result.binaryContainer = value.value("binaryContainer", false);
    return true;
}

json ToJson(const RenderImportedTexture& texture) {
    json value = json::object();
    value["name"] = texture.name;
    value["sourcePath"] = texture.sourcePath;
    value["sourceUri"] = texture.sourceUri;
    value["sourceKind"] = static_cast<int>(texture.sourceKind);
    value["semantic"] = static_cast<int>(texture.semantic);
    value["sourceAssetIndex"] = texture.sourceAssetIndex;
    value["sourceImageIndex"] = texture.sourceImageIndex;
    value["width"] = texture.width;
    value["height"] = texture.height;
    value["channels"] = texture.channels;
    value["pixelsBase64"] = EncodeBase64(texture.pixels);
    return value;
}

bool FromJson(const json& value, RenderImportedTexture& result) {
    if (!value.is_object()) {
        return false;
    }

    result.name = value.value("name", "Imported Texture");
    result.sourcePath = value.value("sourcePath", "");
    result.sourceUri = value.value("sourceUri", "");
    result.sourceKind = static_cast<RenderImportedTextureSourceKind>(value.value("sourceKind", 0));
    result.semantic = static_cast<RenderTextureSemantic>(value.value("semantic", 0));
    result.sourceAssetIndex = value.value("sourceAssetIndex", -1);
    result.sourceImageIndex = value.value("sourceImageIndex", -1);
    result.width = value.value("width", 0);
    result.height = value.value("height", 0);
    result.channels = value.value("channels", 4);
    return DecodeBase64(value.value("pixelsBase64", ""), result.pixels);
}

json ToJson(const RenderTransform& transform) {
    json value = json::object();
    value["translation"] = ToJson(transform.translation);
    value["rotationDegrees"] = ToJson(transform.rotationDegrees);
    value["scale"] = ToJson(transform.scale);
    return value;
}

bool FromJson(const json& value, RenderTransform& result) {
    if (!value.is_object()) {
        return false;
    }

    RenderFloat3 translation {};
    RenderFloat3 rotationDegrees {};
    RenderFloat3 scale {};
    if (!FromJson(value.value("translation", json::array()), translation) ||
        !FromJson(value.value("rotationDegrees", json::array()), rotationDegrees) ||
        !FromJson(value.value("scale", json::array()), scale)) {
        return false;
    }

    result.translation = translation;
    result.rotationDegrees = rotationDegrees;
    result.scale = scale;
    return true;
}

json ToJson(const RenderMeshTriangle& triangle) {
    json value = json::object();
    value["name"] = triangle.name;
    value["localA"] = ToJson(triangle.localA);
    value["localB"] = ToJson(triangle.localB);
    value["localC"] = ToJson(triangle.localC);
    value["localNormalA"] = ToJson(triangle.localNormalA);
    value["localNormalB"] = ToJson(triangle.localNormalB);
    value["localNormalC"] = ToJson(triangle.localNormalC);
    value["uvA"] = ToJson(triangle.uvA);
    value["uvB"] = ToJson(triangle.uvB);
    value["uvC"] = ToJson(triangle.uvC);
    value["materialIndex"] = triangle.materialIndex;
    value["albedoTint"] = ToJson(triangle.albedoTint);
    return value;
}

bool FromJson(const json& value, RenderMeshTriangle& result) {
    if (!value.is_object()) {
        return false;
    }

    result.name = value.value("name", "Mesh Triangle");
    result.materialIndex = value.value("materialIndex", 0);
    const json albedoTintJson = value.contains("albedoTint")
        ? value["albedoTint"]
        : value.value("color", json::array({ 0.8f, 0.8f, 0.8f }));
    return FromJson(value.value("localA", json::array()), result.localA) &&
        FromJson(value.value("localB", json::array()), result.localB) &&
        FromJson(value.value("localC", json::array()), result.localC) &&
        FromJson(value.value("localNormalA", json::array({ 0.0f, 0.0f, 0.0f })), result.localNormalA) &&
        FromJson(value.value("localNormalB", json::array({ 0.0f, 0.0f, 0.0f })), result.localNormalB) &&
        FromJson(value.value("localNormalC", json::array({ 0.0f, 0.0f, 0.0f })), result.localNormalC) &&
        FromJson(value.value("uvA", json::array({ 0.0f, 0.0f })), result.uvA) &&
        FromJson(value.value("uvB", json::array({ 1.0f, 0.0f })), result.uvB) &&
        FromJson(value.value("uvC", json::array({ 0.0f, 1.0f })), result.uvC) &&
        FromJson(albedoTintJson, result.albedoTint);
}

json ToJson(const RenderMaterial& material) {
    json value = json::object();
    value["name"] = material.name;
    value["sourceAssetIndex"] = material.sourceAssetIndex;
    value["sourceMaterialName"] = material.sourceMaterialName;
    value["surfacePreset"] = static_cast<int>(material.surfacePreset);
    value["baseColor"] = ToJson(material.baseColor);
    value["emissionColor"] = ToJson(material.emissionColor);
    value["emissionStrength"] = material.emissionStrength;
    value["roughness"] = material.roughness;
    value["metallic"] = material.metallic;
    value["transmission"] = material.transmission;
    value["ior"] = material.ior;
    value["thinWalled"] = material.thinWalled;
    value["absorptionColor"] = ToJson(material.absorptionColor);
    value["absorptionDistance"] = material.absorptionDistance;
    value["transmissionRoughness"] = material.transmissionRoughness;
    value["baseColorTexture"] = ToJson(material.baseColorTexture);
    value["metallicRoughnessTexture"] = ToJson(material.metallicRoughnessTexture);
    value["emissiveTexture"] = ToJson(material.emissiveTexture);
    value["normalTexture"] = ToJson(material.normalTexture);
    return value;
}

bool FromJson(const json& value, RenderMaterial& result) {
    if (!value.is_object()) {
        return false;
    }

    result.name = value.value("name", "Material");
    result.sourceAssetIndex = value.value("sourceAssetIndex", -1);
    result.sourceMaterialName = value.value("sourceMaterialName", "");
    result.surfacePreset = static_cast<RenderSurfacePreset>(value.value("surfacePreset", static_cast<int>(RenderSurfacePreset::Diffuse)));
    result.emissionStrength = value.value("emissionStrength", 0.0f);
    result.roughness = value.value("roughness", 1.0f);
    result.metallic = value.value("metallic", 0.0f);
    result.transmission = value.value("transmission", 0.0f);
    result.ior = value.value("ior", 1.5f);
    result.thinWalled = value.value("thinWalled", false);
    result.absorptionDistance = value.value("absorptionDistance", 1.0f);
    result.transmissionRoughness = value.value("transmissionRoughness", 0.0f);
    return FromJson(value.value("baseColor", json::array()), result.baseColor) &&
        FromJson(value.value("emissionColor", json::array({ 1.0f, 1.0f, 1.0f })), result.emissionColor) &&
        FromJson(value.value("absorptionColor", json::array({ 1.0f, 1.0f, 1.0f })), result.absorptionColor) &&
        FromJson(value.value("baseColorTexture", json::object({ { "textureIndex", -1 }, { "uvSet", 0 } })), result.baseColorTexture) &&
        FromJson(value.value("metallicRoughnessTexture", json::object({ { "textureIndex", -1 }, { "uvSet", 0 } })), result.metallicRoughnessTexture) &&
        FromJson(value.value("emissiveTexture", json::object({ { "textureIndex", -1 }, { "uvSet", 0 } })), result.emissiveTexture) &&
        FromJson(value.value("normalTexture", json::object({ { "textureIndex", -1 }, { "uvSet", 0 } })), result.normalTexture);
}

json ToJson(const RenderMeshDefinition& mesh) {
    json triangles = json::array();
    for (const RenderMeshTriangle& triangle : mesh.triangles) {
        triangles.push_back(ToJson(triangle));
    }

    json value = json::object();
    value["name"] = mesh.name;
    value["sourceAssetIndex"] = mesh.sourceAssetIndex;
    value["sourceMeshName"] = mesh.sourceMeshName;
    value["triangles"] = std::move(triangles);
    return value;
}

bool FromJson(const json& value, RenderMeshDefinition& result) {
    if (!value.is_object()) {
        return false;
    }

    std::vector<RenderMeshTriangle> triangles;
    const json triangleArray = value.value("triangles", json::array());
    if (!triangleArray.is_array()) {
        return false;
    }

    triangles.reserve(triangleArray.size());
    for (const json& triangleValue : triangleArray) {
        RenderMeshTriangle triangle;
        if (!FromJson(triangleValue, triangle)) {
            return false;
        }
        triangles.push_back(std::move(triangle));
    }

    result = BuildRenderMeshDefinition(value.value("name", "Mesh"), std::move(triangles));
    result.sourceAssetIndex = value.value("sourceAssetIndex", -1);
    result.sourceMeshName = value.value("sourceMeshName", result.name);
    return true;
}

json ToJson(const RenderMeshInstance& meshInstance) {
    json value = json::object();
    value["id"] = meshInstance.id;
    value["name"] = meshInstance.name;
    value["meshIndex"] = meshInstance.meshIndex;
    value["transform"] = ToJson(meshInstance.transform);
    value["colorTint"] = ToJson(meshInstance.colorTint);
    return value;
}

bool FromJson(const json& value, RenderMeshInstance& result) {
    if (!value.is_object()) {
        return false;
    }

    result.name = value.value("name", "Mesh Instance");
    result.id = value.value("id", -1);
    result.meshIndex = value.value("meshIndex", -1);
    return FromJson(value.value("transform", json::object()), result.transform) &&
        FromJson(value.value("colorTint", json::array()), result.colorTint);
}

json ToJson(const RenderSphere& sphere) {
    json value = json::object();
    value["id"] = sphere.id;
    value["name"] = sphere.name;
    value["transform"] = ToJson(sphere.transform);
    value["localCenter"] = ToJson(sphere.localCenter);
    value["radius"] = sphere.radius;
    value["materialIndex"] = sphere.materialIndex;
    value["albedoTint"] = ToJson(sphere.albedoTint);
    return value;
}

bool FromJson(const json& value, RenderSphere& result) {
    if (!value.is_object()) {
        return false;
    }

    result.name = value.value("name", "Sphere");
    result.id = value.value("id", -1);
    result.radius = value.value("radius", 1.0f);
    result.materialIndex = value.value("materialIndex", 0);
    const json albedoTintJson = value.contains("albedoTint")
        ? value["albedoTint"]
        : value.value("color", json::array({ 0.8f, 0.8f, 0.8f }));
    return FromJson(value.value("transform", json::object()), result.transform) &&
        FromJson(value.value("localCenter", json::array()), result.localCenter) &&
        FromJson(albedoTintJson, result.albedoTint);
}

json ToJson(const RenderTriangle& triangle) {
    json value = json::object();
    value["id"] = triangle.id;
    value["name"] = triangle.name;
    value["transform"] = ToJson(triangle.transform);
    value["localA"] = ToJson(triangle.localA);
    value["localB"] = ToJson(triangle.localB);
    value["localC"] = ToJson(triangle.localC);
    value["localNormalA"] = ToJson(triangle.localNormalA);
    value["localNormalB"] = ToJson(triangle.localNormalB);
    value["localNormalC"] = ToJson(triangle.localNormalC);
    value["uvA"] = ToJson(triangle.uvA);
    value["uvB"] = ToJson(triangle.uvB);
    value["uvC"] = ToJson(triangle.uvC);
    value["materialIndex"] = triangle.materialIndex;
    value["albedoTint"] = ToJson(triangle.albedoTint);
    return value;
}

bool FromJson(const json& value, RenderTriangle& result) {
    if (!value.is_object()) {
        return false;
    }

    result.name = value.value("name", "Triangle");
    result.id = value.value("id", -1);
    result.materialIndex = value.value("materialIndex", 0);
    const json albedoTintJson = value.contains("albedoTint")
        ? value["albedoTint"]
        : value.value("color", json::array({ 0.8f, 0.8f, 0.8f }));
    return FromJson(value.value("transform", json::object()), result.transform) &&
        FromJson(value.value("localA", json::array()), result.localA) &&
        FromJson(value.value("localB", json::array()), result.localB) &&
        FromJson(value.value("localC", json::array()), result.localC) &&
        FromJson(value.value("localNormalA", json::array({ 0.0f, 0.0f, 0.0f })), result.localNormalA) &&
        FromJson(value.value("localNormalB", json::array({ 0.0f, 0.0f, 0.0f })), result.localNormalB) &&
        FromJson(value.value("localNormalC", json::array({ 0.0f, 0.0f, 0.0f })), result.localNormalC) &&
        FromJson(value.value("uvA", json::array({ 0.0f, 0.0f })), result.uvA) &&
        FromJson(value.value("uvB", json::array({ 1.0f, 0.0f })), result.uvB) &&
        FromJson(value.value("uvC", json::array({ 0.0f, 1.0f })), result.uvC) &&
        FromJson(albedoTintJson, result.albedoTint);
}

json ToJson(const RenderLight& light) {
    json value = json::object();
    value["id"] = light.id;
    value["name"] = light.name;
    value["type"] = static_cast<int>(light.type);
    value["transform"] = ToJson(light.transform);
    value["color"] = ToJson(light.color);
    value["intensity"] = light.intensity;
    value["areaSize"] = ToJson(light.areaSize);
    value["range"] = light.range;
    value["innerConeDegrees"] = light.innerConeDegrees;
    value["outerConeDegrees"] = light.outerConeDegrees;
    value["enabled"] = light.enabled;
    return value;
}

bool FromJson(const json& value, RenderLight& result) {
    if (!value.is_object()) {
        return false;
    }

    result.id = value.value("id", -1);
    result.name = value.value("name", "Light");
    result.type = static_cast<RenderLightType>(value.value("type", static_cast<int>(RenderLightType::Point)));
    result.intensity = value.value("intensity", 10.0f);
    result.range = value.value("range", 20.0f);
    result.innerConeDegrees = value.value("innerConeDegrees", 18.0f);
    result.outerConeDegrees = value.value("outerConeDegrees", 32.0f);
    result.enabled = value.value("enabled", true);
    return FromJson(value.value("transform", json::object()), result.transform) &&
        FromJson(value.value("color", json::array({ 1.0f, 1.0f, 1.0f })), result.color) &&
        FromJson(value.value("areaSize", json::array({ 1.0f, 1.0f })), result.areaSize);
}

template <typename T>
json ToJsonArray(const std::vector<T>& items) {
    json array = json::array();
    for (const T& item : items) {
        array.push_back(ToJson(item));
    }
    return array;
}

template <typename T>
bool FromJsonArray(const json& value, std::vector<T>& items) {
    if (!value.is_array()) {
        return false;
    }

    items.clear();
    items.reserve(value.size());
    for (const json& itemValue : value) {
        T item {};
        if (!FromJson(itemValue, item)) {
            return false;
        }
        items.push_back(std::move(item));
    }
    return true;
}

} // namespace

namespace RenderSceneSerialization {

nlohmann::json BuildSnapshotJson(const RenderScene& scene, const RenderCamera& camera) {
    std::vector<RenderImportedAsset> importedAssets;
    importedAssets.reserve(static_cast<std::size_t>(scene.GetImportedAssetCount()));
    for (int i = 0; i < scene.GetImportedAssetCount(); ++i) {
        importedAssets.push_back(scene.GetImportedAsset(i));
    }

    std::vector<RenderImportedTexture> importedTextures;
    importedTextures.reserve(static_cast<std::size_t>(scene.GetImportedTextureCount()));
    for (int i = 0; i < scene.GetImportedTextureCount(); ++i) {
        importedTextures.push_back(scene.GetImportedTexture(i));
    }

    std::vector<RenderMaterial> materials;
    materials.reserve(static_cast<std::size_t>(scene.GetMaterialCount()));
    for (int i = 0; i < scene.GetMaterialCount(); ++i) {
        materials.push_back(scene.GetMaterial(i));
    }

    std::vector<RenderMeshDefinition> meshes;
    meshes.reserve(static_cast<std::size_t>(scene.GetMeshDefinitionCount()));
    for (int i = 0; i < scene.GetMeshDefinitionCount(); ++i) {
        meshes.push_back(scene.GetMeshDefinition(i));
    }

    std::vector<RenderMeshInstance> meshInstances;
    meshInstances.reserve(static_cast<std::size_t>(scene.GetMeshInstanceCount()));
    for (int i = 0; i < scene.GetMeshInstanceCount(); ++i) {
        meshInstances.push_back(scene.GetMeshInstance(i));
    }

    std::vector<RenderSphere> spheres;
    spheres.reserve(static_cast<std::size_t>(scene.GetSphereCount()));
    for (int i = 0; i < scene.GetSphereCount(); ++i) {
        spheres.push_back(scene.GetSphere(i));
    }

    std::vector<RenderTriangle> triangles;
    triangles.reserve(static_cast<std::size_t>(scene.GetTriangleCount()));
    for (int i = 0; i < scene.GetTriangleCount(); ++i) {
        triangles.push_back(scene.GetTriangle(i));
    }

    std::vector<RenderLight> lights;
    lights.reserve(static_cast<std::size_t>(scene.GetLightCount()));
    for (int i = 0; i < scene.GetLightCount(); ++i) {
        lights.push_back(scene.GetLight(i));
    }

    json root = json::object();
    root["version"] = kRenderSceneSnapshotVersion;
    root["label"] = scene.GetValidationSceneLabel();
    root["description"] = scene.GetValidationSceneDescription();
    root["backgroundMode"] = static_cast<int>(scene.GetBackgroundMode());
    root["environmentEnabled"] = scene.IsEnvironmentEnabled();
    root["environmentIntensity"] = scene.GetEnvironmentIntensity();
    root["fogEnabled"] = scene.IsFogEnabled();
    root["fogColor"] = ToJson(scene.GetFogColor());
    root["fogDensity"] = scene.GetFogDensity();
    root["fogAnisotropy"] = scene.GetFogAnisotropy();
    root["importedAssets"] = ToJsonArray(importedAssets);
    root["importedTextures"] = ToJsonArray(importedTextures);
    root["materials"] = ToJsonArray(materials);
    root["meshes"] = ToJsonArray(meshes);
    root["meshInstances"] = ToJsonArray(meshInstances);
    root["spheres"] = ToJsonArray(spheres);
    root["triangles"] = ToJsonArray(triangles);
    root["lights"] = ToJsonArray(lights);

    json cameraJson = json::object();
    cameraJson["position"] = ToJson(camera.GetPosition());
    cameraJson["yawDegrees"] = camera.GetYawDegrees();
    cameraJson["pitchDegrees"] = camera.GetPitchDegrees();
    cameraJson["fieldOfViewDegrees"] = camera.GetFieldOfViewDegrees();
    cameraJson["focusDistance"] = camera.GetFocusDistance();
    cameraJson["apertureRadius"] = camera.GetApertureRadius();
    cameraJson["exposure"] = camera.GetExposure();
    root["camera"] = std::move(cameraJson);
    return root;
}

bool ParseSnapshotJson(const nlohmann::json& root, RenderSceneSnapshotDocument& document) {
    if (!root.is_object()) {
        return false;
    }

    document.label = root.value("label", "Custom Scene");
    document.description = root.value("description", "Render scene snapshot.");
    document.backgroundMode = static_cast<RenderBackgroundMode>(root.value("backgroundMode", 0));
    document.environmentEnabled = root.value("environmentEnabled", true);
    document.environmentIntensity = root.value("environmentIntensity", 1.0f);
    document.fogEnabled = root.value("fogEnabled", false);
    document.fogDensity = root.value("fogDensity", 0.0f);
    document.fogAnisotropy = root.value("fogAnisotropy", 0.0f);
    if (!FromJson(root.value("fogColor", json::array({ 0.82f, 0.88f, 0.96f })), document.fogColor)) {
        return false;
    }

    if (!FromJsonArray(root.value("importedAssets", json::array()), document.importedAssets) ||
        !FromJsonArray(root.value("importedTextures", json::array()), document.importedTextures) ||
        !FromJsonArray(root.value("materials", json::array()), document.materials) ||
        !FromJsonArray(root.value("meshes", json::array()), document.meshes) ||
        !FromJsonArray(root.value("meshInstances", json::array()), document.meshInstances) ||
        !FromJsonArray(root.value("spheres", json::array()), document.spheres) ||
        !FromJsonArray(root.value("triangles", json::array()), document.triangles) ||
        !FromJsonArray(root.value("lights", json::array()), document.lights)) {
        return false;
    }

    const json cameraJson = root.value("camera", json::object());
    if (!cameraJson.is_object()) {
        return false;
    }

    document.cameraYawDegrees = cameraJson.value("yawDegrees", 18.0f);
    document.cameraPitchDegrees = cameraJson.value("pitchDegrees", -12.0f);
    document.cameraFieldOfViewDegrees = cameraJson.value("fieldOfViewDegrees", 50.0f);
    document.cameraFocusDistance = cameraJson.value("focusDistance", 6.0f);
    document.cameraApertureRadius = cameraJson.value("apertureRadius", 0.0f);
    document.cameraExposure = cameraJson.value("exposure", 1.0f);
    const json cameraPositionJson = cameraJson.value("position", json());
    if (!cameraPositionJson.is_null()) {
        if (!FromJson(cameraPositionJson, document.cameraPosition)) {
            return false;
        }
    } else {
        document.cameraPosition = BuildLegacyOrbitPosition(
            document.cameraYawDegrees,
            document.cameraPitchDegrees,
            document.cameraFocusDistance);
    }
    return true;
}

bool WriteSnapshotFile(
    const std::filesystem::path& path,
    const RenderScene& scene,
    const RenderCamera& camera,
    std::string& errorMessage) {
    errorMessage.clear();

    try {
        if (path.empty()) {
            errorMessage = "No snapshot file path was provided.";
            return false;
        }

        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            errorMessage = "Failed to open the snapshot file for writing.";
            return false;
        }

        file << BuildSnapshotJson(scene, camera).dump(2);
        return true;
    } catch (const std::exception& exception) {
        errorMessage = exception.what();
        return false;
    } catch (...) {
        errorMessage = "Unknown error while saving the render scene snapshot.";
        return false;
    }
}

bool ReadSnapshotFile(
    const std::filesystem::path& path,
    RenderSceneSnapshotDocument& document,
    std::string& errorMessage) {
    errorMessage.clear();

    try {
        if (path.empty() || !std::filesystem::exists(path)) {
            errorMessage = "The selected render scene snapshot file does not exist.";
            return false;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            errorMessage = "Failed to open the snapshot file for reading.";
            return false;
        }

        json root = json::parse(file);
        if (!ParseSnapshotJson(root, document)) {
            errorMessage = "The render scene snapshot format is invalid.";
            return false;
        }

        return true;
    } catch (const std::exception& exception) {
        errorMessage = exception.what();
        return false;
    } catch (...) {
        errorMessage = "Unknown error while loading the render scene snapshot.";
        return false;
    }
}

} // namespace RenderSceneSerialization
