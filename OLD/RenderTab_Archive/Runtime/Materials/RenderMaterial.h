#pragma once

#include "RenderTab/Runtime/Assets/RenderImportedAsset.h"
#include "RenderTab/Runtime/Geometry/RenderMath.h"

#include <string>

enum class RenderSurfacePreset {
    Diffuse = 0,
    Metal,
    Glass,
    Emissive
};

struct RenderMaterial {
    std::string name;
    int sourceAssetIndex = -1;
    std::string sourceMaterialName;
    RenderSurfacePreset surfacePreset = RenderSurfacePreset::Diffuse;
    RenderFloat3 baseColor { 0.8f, 0.8f, 0.8f };
    RenderFloat3 emissionColor { 1.0f, 1.0f, 1.0f };
    float emissionStrength = 0.0f;
    float roughness = 1.0f;
    float metallic = 0.0f;
    float transmission = 0.0f;
    float ior = 1.5f;
    bool thinWalled = false;
    RenderFloat3 absorptionColor { 1.0f, 1.0f, 1.0f };
    float absorptionDistance = 1.0f;
    float transmissionRoughness = 0.0f;
    RenderMaterialTextureRef baseColorTexture {};
    RenderMaterialTextureRef metallicRoughnessTexture {};
    RenderMaterialTextureRef emissiveTexture {};
    RenderMaterialTextureRef normalTexture {};
};

struct RenderResolvedMaterial {
    RenderFloat3 albedo { 0.8f, 0.8f, 0.8f };
    RenderFloat3 emission { 0.0f, 0.0f, 0.0f };
    float roughness = 1.0f;
    float metallic = 0.0f;
    float transmission = 0.0f;
    float ior = 1.5f;
    bool thinWalled = false;
    RenderFloat3 absorptionColor { 1.0f, 1.0f, 1.0f };
    float absorptionDistance = 1.0f;
    float transmissionRoughness = 0.0f;
    RenderMaterialTextureRef baseColorTexture {};
    RenderMaterialTextureRef metallicRoughnessTexture {};
    RenderMaterialTextureRef emissiveTexture {};
    RenderMaterialTextureRef normalTexture {};
};

RenderMaterial BuildRenderMaterial(
    std::string name,
    const RenderFloat3& baseColor,
    float emissionStrength = 0.0f,
    const RenderFloat3& emissionColor = RenderFloat3 { 1.0f, 1.0f, 1.0f },
    float roughness = 1.0f,
    float metallic = 0.0f,
    float transmission = 0.0f,
    float ior = 1.5f);
bool NearlyEqual(const RenderMaterial& a, const RenderMaterial& b, float epsilon = 0.0001f);
bool IsEmissive(const RenderMaterial& material, float epsilon = 0.0001f);
bool IsSmoothDielectric(const RenderMaterial& material, float epsilon = 0.0001f);
const char* GetRenderSurfacePresetLabel(RenderSurfacePreset preset);
void ApplyRenderSurfacePreset(RenderMaterial& material, RenderSurfacePreset preset);
RenderResolvedMaterial ResolveMaterial(
    const RenderMaterial& material,
    const RenderFloat3& tint = RenderFloat3 { 1.0f, 1.0f, 1.0f });
