#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace RenderFoundation {

using Id = std::uint64_t;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Transform {
    Vec3 translation {};
    Vec3 rotationDegrees {};
    Vec3 scale { 1.0f, 1.0f, 1.0f };
};

enum class ViewMode : std::uint8_t {
    Unlit = 0,
    PathTrace = 1
};

enum class PathTraceDebugMode : std::uint8_t {
    None = 0,
    SelectedRayLog = 1,
    RefractedSourceClass = 2,
    SelfHitHeatmap = 3
};

enum class TransformMode : std::uint8_t {
    Translate = 0,
    Rotate = 1,
    Scale = 2
};

enum class TransformSpace : std::uint8_t {
    Local = 0,
    World = 1
};

enum class BaseMaterial : std::uint8_t {
    Diffuse = 0,
    Metal = 1,
    Glass = 2,
    Emissive = 3
};

enum class MaterialLayerType : std::uint8_t {
    BaseDiffuse = 0,
    BaseMetal = 1,
    BaseDielectric = 2,
    ClearCoat = 3
};

enum class PrimitiveType : std::uint8_t {
    Sphere = 0,
    Cube = 1,
    Plane = 2
};

enum class LightType : std::uint8_t {
    Point = 0,
    Spot = 1,
    Area = 2,
    Directional = 3
};

enum class SelectionType : std::uint8_t {
    None = 0,
    Primitive = 1,
    Light = 2,
    Camera = 3
};

struct Material {
    Id id = 0;
    std::string name = "Material";
    BaseMaterial baseMaterial = BaseMaterial::Diffuse;
    Vec3 baseColor { 0.8f, 0.8f, 0.8f };
    Vec3 emissionColor { 1.0f, 1.0f, 1.0f };
    float emissionStrength = 0.0f;
    float roughness = 0.3f;
    float metallic = 0.0f;
    float transmission = 0.0f;
    float ior = 1.5f;
    bool thinWalled = false;
    Vec3 absorptionColor { 1.0f, 1.0f, 1.0f };
    float absorptionDistance = 1.0f;
    float transmissionRoughness = 0.0f;
    float clearCoat = 0.0f;
    float thinFilm = 0.0f;
    float subsurface = 0.0f;
    struct Layer {
        MaterialLayerType type = MaterialLayerType::BaseDiffuse;
        Vec3 color { 1.0f, 1.0f, 1.0f };
        float weight = 1.0f;
        float roughness = 0.3f;
        float metallic = 0.0f;
        float transmission = 0.0f;
        float ior = 1.5f;
        float transmissionRoughness = 0.0f;
        bool thinWalled = false;
    };
    std::vector<Layer> layers;
};

struct Primitive {
    Id id = 0;
    std::string name = "Primitive";
    PrimitiveType type = PrimitiveType::Sphere;
    Transform transform {};
    Id materialId = 0;
    bool visible = true;
};

struct Light {
    Id id = 0;
    std::string name = "Light";
    LightType type = LightType::Point;
    Transform transform {};
    Vec3 color { 1.0f, 1.0f, 1.0f };
    float intensity = 10.0f;
    Vec2 areaSize { 1.0f, 1.0f };
    float range = 20.0f;
    float innerConeDegrees = 18.0f;
    float outerConeDegrees = 32.0f;
    bool enabled = true;
};

struct Camera {
    Vec3 position { -4.5f, 2.25f, 6.5f };
    float yawDegrees = -34.0f;
    float pitchDegrees = -16.0f;
    float fieldOfViewDegrees = 50.0f;
    float focusDistance = 6.0f;
    float apertureRadius = 0.0f;
    float exposure = 1.0f;
    float movementSpeed = 5.0f;
};

struct FinalRenderSettings {
    int resolutionX = 1280;
    int resolutionY = 720;
    int sampleTarget = 256;
    int maxBounceCount = 8;
    std::string outputName = "Final Render";
};

struct Settings {
    int resolutionX = 1280;
    int resolutionY = 720;
    int previewSampleTarget = 128;
    bool accumulationEnabled = true;
    ViewMode viewMode = ViewMode::Unlit;
    PathTraceDebugMode pathTraceDebugMode = PathTraceDebugMode::None;
    int pathTraceDebugPixelX = -1;
    int pathTraceDebugPixelY = -1;
    int maxBounceCount = 4;
    TransformMode transformMode = TransformMode::Translate;
    TransformSpace transformSpace = TransformSpace::Local;
    FinalRenderSettings finalRender {};
};

struct SceneMetadata {
    std::string label = "Render Foundation Scene";
    std::string description =
        "Phase 1 foundation scene anchored to the Redefining Rendering specifications.";
    int backgroundMode = 0;
    bool environmentEnabled = true;
    float environmentIntensity = 1.0f;
    bool fogEnabled = false;
    Vec3 fogColor { 0.82f, 0.88f, 0.96f };
    float fogDensity = 0.0f;
    float fogAnisotropy = 0.0f;
};

struct Selection {
    SelectionType type = SelectionType::None;
    Id id = 0;
};

struct Snapshot {
    Id cameraId = 0;
    SceneMetadata metadata {};
    std::vector<Material> materials;
    std::vector<Primitive> primitives;
    std::vector<Light> lights;
    Camera camera {};
};

inline Vec3 MakeVec3(float x, float y, float z) {
    return { x, y, z };
}

inline Vec3 operator+(const Vec3& a, const Vec3& b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

inline Vec3 operator-(const Vec3& a, const Vec3& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline Vec3 operator*(const Vec3& value, float scalar) {
    return { value.x * scalar, value.y * scalar, value.z * scalar };
}

inline Vec3 operator/(const Vec3& value, float scalar) {
    const float safe = scalar == 0.0f ? 1.0f : scalar;
    return { value.x / safe, value.y / safe, value.z / safe };
}

inline float Dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline float Length(const Vec3& value) {
    return std::sqrt(std::max(0.0f, Dot(value, value)));
}

inline Vec3 Normalize(const Vec3& value) {
    const float length = Length(value);
    if (length <= 1e-6f) {
        return {};
    }
    return value / length;
}

inline float ClampFloat(float value, float low, float high) {
    return std::clamp(value, low, high);
}

inline float Clamp01(float value) {
    return ClampFloat(value, 0.0f, 1.0f);
}

inline Vec3 Clamp01(const Vec3& value) {
    return { Clamp01(value.x), Clamp01(value.y), Clamp01(value.z) };
}

inline const char* MaterialLayerTypeLabel(MaterialLayerType type) {
    switch (type) {
        case MaterialLayerType::BaseDiffuse:
            return "Base Diffuse";
        case MaterialLayerType::BaseMetal:
            return "Base Metal";
        case MaterialLayerType::BaseDielectric:
            return "Base Dielectric";
        case MaterialLayerType::ClearCoat:
            return "Clear Coat";
    }
    return "Layer";
}

inline bool IsBaseMaterialLayerType(MaterialLayerType type) {
    return type == MaterialLayerType::BaseDiffuse ||
        type == MaterialLayerType::BaseMetal ||
        type == MaterialLayerType::BaseDielectric;
}

inline Material::Layer MakeDefaultClearCoatLayer(float weight = 1.0f) {
    Material::Layer layer;
    layer.type = MaterialLayerType::ClearCoat;
    layer.color = { 1.0f, 1.0f, 1.0f };
    layer.weight = Clamp01(weight);
    layer.roughness = 0.03f;
    layer.metallic = 0.0f;
    layer.transmission = 0.0f;
    layer.ior = 1.5f;
    layer.transmissionRoughness = 0.0f;
    layer.thinWalled = false;
    return layer;
}

inline Material::Layer MakeBaseLayerFromMaterial(const Material& material) {
    Material::Layer layer;
    layer.color = Clamp01(material.baseColor);
    layer.weight = 1.0f;
    layer.roughness = Clamp01(material.roughness);
    layer.metallic = Clamp01(material.metallic);
    layer.transmission = Clamp01(material.transmission);
    layer.ior = std::max(material.ior, 1.0f);
    layer.transmissionRoughness = Clamp01(material.transmissionRoughness);
    layer.thinWalled = material.thinWalled;

    switch (material.baseMaterial) {
        case BaseMaterial::Metal:
            layer.type = MaterialLayerType::BaseMetal;
            layer.metallic = 1.0f;
            layer.transmission = 0.0f;
            layer.thinWalled = false;
            layer.transmissionRoughness = 0.0f;
            break;
        case BaseMaterial::Glass:
            layer.type = MaterialLayerType::BaseDielectric;
            layer.metallic = 0.0f;
            layer.transmission = std::max(layer.transmission, 0.85f);
            layer.ior = std::max(layer.ior, 1.0f);
            break;
        case BaseMaterial::Emissive:
        case BaseMaterial::Diffuse:
        default:
            layer.type = MaterialLayerType::BaseDiffuse;
            layer.metallic = 0.0f;
            layer.transmission = 0.0f;
            layer.thinWalled = false;
            layer.transmissionRoughness = 0.0f;
            break;
    }

    return layer;
}

inline Material::Layer* FindMaterialLayer(Material& material, MaterialLayerType type) {
    auto it = std::find_if(material.layers.begin(), material.layers.end(), [type](const Material::Layer& layer) {
        return layer.type == type;
    });
    return it == material.layers.end() ? nullptr : &(*it);
}

inline const Material::Layer* FindMaterialLayer(const Material& material, MaterialLayerType type) {
    auto it = std::find_if(material.layers.begin(), material.layers.end(), [type](const Material::Layer& layer) {
        return layer.type == type;
    });
    return it == material.layers.end() ? nullptr : &(*it);
}

inline Material::Layer* FindBaseMaterialLayer(Material& material) {
    auto it = std::find_if(material.layers.begin(), material.layers.end(), [](const Material::Layer& layer) {
        return IsBaseMaterialLayerType(layer.type);
    });
    return it == material.layers.end() ? nullptr : &(*it);
}

inline const Material::Layer* FindBaseMaterialLayer(const Material& material) {
    auto it = std::find_if(material.layers.begin(), material.layers.end(), [](const Material::Layer& layer) {
        return IsBaseMaterialLayerType(layer.type);
    });
    return it == material.layers.end() ? nullptr : &(*it);
}

inline void NormalizeMaterialLayerStack(Material& material) {
    Material::Layer baseLayer = MakeBaseLayerFromMaterial(material);
    if (const Material::Layer* existingBase = FindBaseMaterialLayer(material)) {
        baseLayer = *existingBase;
    }
    Material::Layer clearCoatLayer = MakeDefaultClearCoatLayer();
    const bool hasClearCoatLayer = [&material, &clearCoatLayer]() {
        if (const Material::Layer* existingClearCoat = FindMaterialLayer(material, MaterialLayerType::ClearCoat)) {
            clearCoatLayer = *existingClearCoat;
            return true;
        }
        return false;
    }();

    material.layers.clear();

    if (hasClearCoatLayer) {
        Material::Layer normalizedCoat = clearCoatLayer;
        normalizedCoat.type = MaterialLayerType::ClearCoat;
        normalizedCoat.color = { 1.0f, 1.0f, 1.0f };
        normalizedCoat.weight = Clamp01(normalizedCoat.weight);
        normalizedCoat.roughness = Clamp01(normalizedCoat.roughness);
        normalizedCoat.metallic = 0.0f;
        normalizedCoat.transmission = 0.0f;
        normalizedCoat.ior = std::max(normalizedCoat.ior, 1.0f);
        normalizedCoat.transmissionRoughness = 0.0f;
        normalizedCoat.thinWalled = false;
        if (normalizedCoat.weight > 0.0001f) {
            material.layers.push_back(normalizedCoat);
        }
    }

    baseLayer.weight = 1.0f;
    baseLayer.color = Clamp01(baseLayer.color);
    baseLayer.roughness = Clamp01(baseLayer.roughness);
    baseLayer.metallic = Clamp01(baseLayer.metallic);
    baseLayer.transmission = Clamp01(baseLayer.transmission);
    baseLayer.ior = std::max(baseLayer.ior, 1.0f);
    baseLayer.transmissionRoughness = Clamp01(baseLayer.transmissionRoughness);

    switch (baseLayer.type) {
        case MaterialLayerType::BaseMetal:
            baseLayer.metallic = 1.0f;
            baseLayer.transmission = 0.0f;
            baseLayer.thinWalled = false;
            baseLayer.transmissionRoughness = 0.0f;
            break;
        case MaterialLayerType::BaseDielectric:
            baseLayer.metallic = 0.0f;
            baseLayer.transmission = std::max(baseLayer.transmission, 0.85f);
            break;
        case MaterialLayerType::BaseDiffuse:
        default:
            baseLayer.metallic = 0.0f;
            baseLayer.transmission = 0.0f;
            baseLayer.thinWalled = false;
            baseLayer.transmissionRoughness = 0.0f;
            break;
    }

    material.layers.push_back(baseLayer);
}

inline void SyncMaterialLayersFromLegacy(Material& material) {
    material.layers.clear();
    if (material.clearCoat > 0.0001f) {
        material.layers.push_back(MakeDefaultClearCoatLayer(material.clearCoat));
    }
    material.layers.push_back(MakeBaseLayerFromMaterial(material));
    NormalizeMaterialLayerStack(material);
}

inline void UpdateLegacyMaterialFieldsFromLayerStack(Material& material) {
    if (material.layers.empty()) {
        SyncMaterialLayersFromLegacy(material);
    }

    const Material::Layer* baseLayer = FindBaseMaterialLayer(material);
    if (baseLayer == nullptr) {
        return;
    }

    material.baseColor = Clamp01(baseLayer->color);
    material.roughness = Clamp01(baseLayer->roughness);
    material.metallic = Clamp01(baseLayer->metallic);
    material.transmission = Clamp01(baseLayer->transmission);
    material.ior = std::max(baseLayer->ior, 1.0f);
    material.thinWalled = baseLayer->thinWalled;
    material.transmissionRoughness = Clamp01(baseLayer->transmissionRoughness);

    switch (baseLayer->type) {
        case MaterialLayerType::BaseMetal:
            material.baseMaterial = BaseMaterial::Metal;
            material.metallic = 1.0f;
            material.transmission = 0.0f;
            material.thinWalled = false;
            material.transmissionRoughness = 0.0f;
            break;
        case MaterialLayerType::BaseDielectric:
            material.baseMaterial = BaseMaterial::Glass;
            material.metallic = 0.0f;
            material.transmission = std::max(material.transmission, 0.85f);
            break;
        case MaterialLayerType::BaseDiffuse:
        default:
            material.baseMaterial = material.emissionStrength > 0.0001f ? BaseMaterial::Emissive : BaseMaterial::Diffuse;
            material.metallic = 0.0f;
            material.transmission = 0.0f;
            material.thinWalled = false;
            material.transmissionRoughness = 0.0f;
            break;
    }

    const Material::Layer* clearCoatLayer = FindMaterialLayer(material, MaterialLayerType::ClearCoat);
    material.clearCoat = clearCoatLayer != nullptr ? Clamp01(clearCoatLayer->weight) : 0.0f;
}

inline void SyncLegacyMaterialFromLayers(Material& material) {
    if (material.layers.empty()) {
        SyncMaterialLayersFromLegacy(material);
    } else {
        NormalizeMaterialLayerStack(material);
    }
    UpdateLegacyMaterialFieldsFromLayerStack(material);
}

inline float MaxComponent(const Vec3& value) {
    return std::max(value.x, std::max(value.y, value.z));
}

inline float DegreesToRadians(float degrees) {
    return degrees * 0.01745329252f;
}

inline Vec3 ForwardFromYawPitch(float yawDegrees, float pitchDegrees) {
    const float yawRadians = DegreesToRadians(yawDegrees);
    const float pitchRadians = DegreesToRadians(pitchDegrees);
    return Normalize({
        std::cos(pitchRadians) * std::cos(yawRadians),
        std::sin(pitchRadians),
        std::cos(pitchRadians) * std::sin(yawRadians)
    });
}

inline Vec3 RightFromForward(const Vec3& forward) {
    const Vec3 worldUp { 0.0f, 1.0f, 0.0f };
    const Vec3 right = Cross(forward, worldUp);
    if (Length(right) <= 1e-6f) {
        return { 1.0f, 0.0f, 0.0f };
    }
    return Normalize(right);
}

inline Vec3 UpFromBasis(const Vec3& right, const Vec3& forward) {
    return Normalize(Cross(right, forward));
}

} // namespace RenderFoundation
