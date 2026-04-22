#include "RenderMaterial.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

RenderFloat3 Multiply(const RenderFloat3& a, const RenderFloat3& b) {
    return MakeRenderFloat3(a.x * b.x, a.y * b.y, a.z * b.z);
}

RenderFloat3 ScaleAndClamp(const RenderFloat3& value, float scalar) {
    return MakeRenderFloat3(
        std::max(value.x * scalar, 0.0f),
        std::max(value.y * scalar, 0.0f),
        std::max(value.z * scalar, 0.0f));
}

} // namespace

RenderMaterial BuildRenderMaterial(
    std::string name,
    const RenderFloat3& baseColor,
    float emissionStrength,
    const RenderFloat3& emissionColor,
    float roughness,
    float metallic,
    float transmission,
    float ior) {
    RenderMaterial material;
    material.name = std::move(name);
    material.baseColor = baseColor;
    material.emissionColor = emissionColor;
    material.emissionStrength = emissionStrength;
    material.roughness = std::clamp(roughness, 0.0f, 1.0f);
    material.metallic = std::clamp(metallic, 0.0f, 1.0f);
    material.transmission = std::clamp(transmission, 0.0f, 1.0f);
    material.ior = std::clamp(ior, 1.0f, 3.0f);
    ApplyRenderSurfacePreset(material, material.transmission > 0.0001f
        ? RenderSurfacePreset::Glass
        : (emissionStrength > 0.0001f ? RenderSurfacePreset::Emissive : (metallic > 0.5f ? RenderSurfacePreset::Metal : RenderSurfacePreset::Diffuse)));
    return material;
}

bool NearlyEqual(const RenderMaterial& a, const RenderMaterial& b, float epsilon) {
    return a.name == b.name &&
        a.sourceAssetIndex == b.sourceAssetIndex &&
        a.sourceMaterialName == b.sourceMaterialName &&
        a.surfacePreset == b.surfacePreset &&
        NearlyEqual(a.baseColor, b.baseColor, epsilon) &&
        NearlyEqual(a.emissionColor, b.emissionColor, epsilon) &&
        NearlyEqual(a.emissionStrength, b.emissionStrength, epsilon) &&
        NearlyEqual(a.roughness, b.roughness, epsilon) &&
        NearlyEqual(a.metallic, b.metallic, epsilon) &&
        NearlyEqual(a.transmission, b.transmission, epsilon) &&
        NearlyEqual(a.ior, b.ior, epsilon) &&
        a.thinWalled == b.thinWalled &&
        NearlyEqual(a.absorptionColor, b.absorptionColor, epsilon) &&
        NearlyEqual(a.absorptionDistance, b.absorptionDistance, epsilon) &&
        NearlyEqual(a.transmissionRoughness, b.transmissionRoughness, epsilon) &&
        a.baseColorTexture.textureIndex == b.baseColorTexture.textureIndex &&
        a.baseColorTexture.uvSet == b.baseColorTexture.uvSet &&
        a.metallicRoughnessTexture.textureIndex == b.metallicRoughnessTexture.textureIndex &&
        a.metallicRoughnessTexture.uvSet == b.metallicRoughnessTexture.uvSet &&
        a.emissiveTexture.textureIndex == b.emissiveTexture.textureIndex &&
        a.emissiveTexture.uvSet == b.emissiveTexture.uvSet &&
        a.normalTexture.textureIndex == b.normalTexture.textureIndex &&
        a.normalTexture.uvSet == b.normalTexture.uvSet;
}

bool IsEmissive(const RenderMaterial& material, float epsilon) {
    return material.emissionStrength > epsilon &&
        (material.emissionColor.x > epsilon ||
            material.emissionColor.y > epsilon ||
            material.emissionColor.z > epsilon);
}

bool IsSmoothDielectric(const RenderMaterial& material, float epsilon) {
    return material.transmission > epsilon;
}

const char* GetRenderSurfacePresetLabel(RenderSurfacePreset preset) {
    switch (preset) {
    case RenderSurfacePreset::Diffuse:
        return "Diffuse";
    case RenderSurfacePreset::Metal:
        return "Metal";
    case RenderSurfacePreset::Glass:
        return "Glass";
    case RenderSurfacePreset::Emissive:
        return "Emissive";
    }

    return "Unknown";
}

void ApplyRenderSurfacePreset(RenderMaterial& material, RenderSurfacePreset preset) {
    material.surfacePreset = preset;
    switch (preset) {
    case RenderSurfacePreset::Diffuse:
        material.metallic = 0.0f;
        material.transmission = 0.0f;
        material.thinWalled = false;
        material.transmissionRoughness = 0.0f;
        material.emissionStrength = 0.0f;
        material.ior = std::clamp(material.ior, 1.0f, 3.0f);
        break;
    case RenderSurfacePreset::Metal:
        material.metallic = 1.0f;
        material.transmission = 0.0f;
        material.thinWalled = false;
        material.transmissionRoughness = 0.0f;
        material.emissionStrength = 0.0f;
        material.ior = std::clamp(material.ior, 1.0f, 3.0f);
        material.roughness = std::clamp(material.roughness, 0.02f, 1.0f);
        break;
    case RenderSurfacePreset::Glass:
        material.metallic = 0.0f;
        material.transmission = std::max(material.transmission, 0.85f);
        material.emissionStrength = 0.0f;
        material.roughness = std::clamp(material.roughness, 0.0f, 1.0f);
        material.transmissionRoughness = std::clamp(material.transmissionRoughness, 0.0f, 1.0f);
        material.ior = std::clamp(material.ior, 1.0f, 3.0f);
        material.absorptionDistance = std::max(material.absorptionDistance, 0.01f);
        break;
    case RenderSurfacePreset::Emissive:
        material.metallic = 0.0f;
        material.transmission = 0.0f;
        material.thinWalled = false;
        material.transmissionRoughness = 0.0f;
        material.emissionStrength = material.emissionStrength <= 0.0f ? 4.0f : material.emissionStrength;
        break;
    }
}

RenderResolvedMaterial ResolveMaterial(const RenderMaterial& material, const RenderFloat3& tint) {
    RenderResolvedMaterial resolved;
    resolved.albedo = Multiply(material.baseColor, tint);
    resolved.emission = ScaleAndClamp(Multiply(material.emissionColor, tint), material.emissionStrength);
    resolved.roughness = std::clamp(material.roughness, 0.0f, 1.0f);
    resolved.metallic = std::clamp(material.metallic, 0.0f, 1.0f);
    resolved.transmission = std::clamp(material.transmission, 0.0f, 1.0f);
    resolved.ior = std::clamp(material.ior, 1.0f, 3.0f);
    resolved.thinWalled = material.thinWalled;
    resolved.absorptionColor = material.absorptionColor;
    resolved.absorptionDistance = std::max(material.absorptionDistance, 0.01f);
    resolved.transmissionRoughness = std::clamp(material.transmissionRoughness, 0.0f, 1.0f);
    resolved.baseColorTexture = material.baseColorTexture;
    resolved.metallicRoughnessTexture = material.metallicRoughnessTexture;
    resolved.emissiveTexture = material.emissiveTexture;
    resolved.normalTexture = material.normalTexture;
    return resolved;
}
