#include "RenderTab/Contracts/SceneCompiler.h"

#include "RenderTab/Runtime/Bvh/RenderBvh.h"
#include "RenderTab/Runtime/Geometry/RenderMesh.h"
#include "RenderTab/Runtime/Geometry/RenderSceneGeometry.h"
#include "RenderTab/Runtime/Materials/RenderMaterial.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace RenderContracts {

namespace {

using namespace RenderFoundation;

RenderFloat3 ToRuntime(const Vec3& value) {
    return MakeRenderFloat3(value.x, value.y, value.z);
}

RenderFloat2 ToRuntime(const Vec2& value) {
    return MakeRenderFloat2(value.x, value.y);
}

RenderTransform ToRuntimeTransform(const Transform& transform) {
    RenderTransform runtime;
    runtime.translation = ToRuntime(transform.translation);
    runtime.rotationDegrees = ToRuntime(transform.rotationDegrees);
    runtime.scale = ToRuntime(transform.scale);
    return runtime;
}

RenderBackgroundMode ToRuntimeBackgroundMode(int backgroundMode) {
    switch (backgroundMode) {
        case 1:
            return RenderBackgroundMode::Checker;
        case 2:
            return RenderBackgroundMode::Grid;
        case 3:
            return RenderBackgroundMode::Black;
        default:
            return RenderBackgroundMode::Gradient;
    }
}

RenderIntegratorMode ToRuntimeIntegratorMode(ViewMode viewMode) {
    return viewMode == ViewMode::Unlit
        ? RenderIntegratorMode::RasterPreview
        : RenderIntegratorMode::PathTracePreview;
}

RenderGizmoMode ToRuntimeGizmoMode(TransformMode mode) {
    switch (mode) {
        case TransformMode::Rotate:
            return RenderGizmoMode::Rotate;
        case TransformMode::Scale:
            return RenderGizmoMode::Scale;
        case TransformMode::Translate:
        default:
            return RenderGizmoMode::Translate;
    }
}

RenderTransformSpace ToRuntimeTransformSpace(TransformSpace space) {
    return space == TransformSpace::Local ? RenderTransformSpace::Local : RenderTransformSpace::World;
}

RenderSurfacePreset ToRuntimeSurfacePreset(BaseMaterial material) {
    switch (material) {
        case BaseMaterial::Metal:
            return RenderSurfacePreset::Metal;
        case BaseMaterial::Glass:
            return RenderSurfacePreset::Glass;
        case BaseMaterial::Emissive:
            return RenderSurfacePreset::Emissive;
        case BaseMaterial::Diffuse:
        default:
            return RenderSurfacePreset::Diffuse;
    }
}

RenderLightType ToRuntimeLightType(LightType type) {
    switch (type) {
        case LightType::Area:
            return RenderLightType::RectArea;
        case LightType::Spot:
            return RenderLightType::Spot;
        case LightType::Directional:
            return RenderLightType::Sun;
        case LightType::Point:
        default:
            return RenderLightType::Point;
    }
}

RenderMaterial CompileMaterial(const Material& material) {
    RenderMaterial runtime;
    runtime.name = material.name;
    runtime.surfacePreset = ToRuntimeSurfacePreset(material.baseMaterial);
    runtime.baseColor = ToRuntime(material.baseColor);
    runtime.emissionColor = ToRuntime(material.emissionColor);
    runtime.emissionStrength = material.emissionStrength;
    runtime.roughness = material.roughness;
    runtime.metallic = material.metallic;
    runtime.transmission = material.transmission;
    runtime.ior = material.ior;
    runtime.thinWalled = material.thinWalled;
    runtime.absorptionColor = ToRuntime(material.absorptionColor);
    runtime.absorptionDistance = material.absorptionDistance;
    runtime.transmissionRoughness = material.transmissionRoughness;
    return runtime;
}

std::vector<RenderMeshTriangle> BuildCubeTriangles(int materialIndex) {
    return {
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
}

std::vector<RenderMeshTriangle> BuildPlaneTriangles(int materialIndex) {
    return {
        { "Plane A", MakeRenderFloat3(-0.5f, 0.0f, -0.5f), MakeRenderFloat3(-0.5f, 0.0f, 0.5f), MakeRenderFloat3(0.5f, 0.0f, -0.5f), {}, {}, {}, MakeRenderFloat2(0.0f, 0.0f), MakeRenderFloat2(0.0f, 1.0f), MakeRenderFloat2(1.0f, 0.0f), materialIndex, MakeRenderFloat3(1.0f, 1.0f, 1.0f) },
        { "Plane B", MakeRenderFloat3(0.5f, 0.0f, -0.5f), MakeRenderFloat3(-0.5f, 0.0f, 0.5f), MakeRenderFloat3(0.5f, 0.0f, 0.5f), {}, {}, {}, MakeRenderFloat2(1.0f, 0.0f), MakeRenderFloat2(0.0f, 1.0f), MakeRenderFloat2(1.0f, 1.0f), materialIndex, MakeRenderFloat3(1.0f, 1.0f, 1.0f) }
    };
}

int ResolveMaterialIndex(const std::unordered_map<Id, int>& materialIndices, Id materialId) {
    const auto it = materialIndices.find(materialId);
    return it == materialIndices.end() ? 0 : it->second;
}

enum class PtThinWallPolicy : std::uint8_t {
    Solid = 0,
    ThinSheet = 1
};

struct PtMaterialKey {
    int authorMaterialIndex = 0;
    PtThinWallPolicy thinWallPolicy = PtThinWallPolicy::Solid;

    bool operator==(const PtMaterialKey& other) const {
        return authorMaterialIndex == other.authorMaterialIndex &&
            thinWallPolicy == other.thinWallPolicy;
    }
};

struct PtMaterialKeyHash {
    std::size_t operator()(const PtMaterialKey& key) const {
        return (static_cast<std::size_t>(key.authorMaterialIndex) << 1u) ^
            static_cast<std::size_t>(key.thinWallPolicy);
    }
};

RenderPathTrace::SpectralBasisCoefficients BuildSpectralBasis(const Vec3& rgb) {
    RenderPathTrace::SpectralBasisCoefficients coefficients;
    coefficients.coefficients[0] = ClampFloat(rgb.x, 0.0f, 32.0f);
    coefficients.coefficients[1] = ClampFloat(rgb.y, 0.0f, 32.0f);
    coefficients.coefficients[2] = ClampFloat(rgb.z, 0.0f, 32.0f);
    coefficients.coefficients[3] = ClampFloat((rgb.x * 0.2126f) + (rgb.y * 0.7152f) + (rgb.z * 0.0722f), 0.0f, 32.0f);
    return coefficients;
}

void StoreCoefficients(float target[4], const RenderPathTrace::SpectralBasisCoefficients& coefficients) {
    std::memcpy(target, coefficients.coefficients.data(), sizeof(float) * 4);
}

float EstimateDielectricDispersionScale(float baseIor) {
    return std::max(0.0f, (baseIor - 1.0f) * 0.004f);
}

std::uint64_t HashMix(std::uint64_t hash, std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
    return hash;
}

std::uint64_t HashBytes(std::uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= static_cast<std::uint64_t>(bytes[index]);
        hash *= 1099511628211ull;
    }
    return hash;
}

template <typename T>
std::uint64_t HashPodVector(std::uint64_t hash, const std::vector<T>& values) {
    hash = HashMix(hash, static_cast<std::uint64_t>(values.size()));
    if (!values.empty()) {
        hash = HashBytes(hash, values.data(), values.size() * sizeof(T));
    }
    return hash;
}

std::uint64_t HashStructure(const SceneSnapshot& snapshot) {
    std::uint64_t hash = 1469598103934665603ull;
    hash = HashMix(hash, snapshot.cameraId);
    hash = HashMix(hash, static_cast<std::uint64_t>(snapshot.primitives.size()));
    hash = HashMix(hash, static_cast<std::uint64_t>(snapshot.lights.size()));
    hash = HashMix(hash, static_cast<std::uint64_t>(snapshot.materials.size()));
    for (const Primitive& primitive : snapshot.primitives) {
        hash = HashMix(hash, primitive.id);
        hash = HashMix(hash, static_cast<std::uint64_t>(primitive.type));
    }
    for (const Light& light : snapshot.lights) {
        hash = HashMix(hash, light.id);
        hash = HashMix(hash, static_cast<std::uint64_t>(light.type));
    }
    for (const Material& material : snapshot.materials) {
        hash = HashMix(hash, material.id);
        hash = HashMix(hash, static_cast<std::uint64_t>(material.baseMaterial));
        hash = HashMix(hash, static_cast<std::uint64_t>(material.layers.size()));
        for (const Material::Layer& layer : material.layers) {
            hash = HashMix(hash, static_cast<std::uint64_t>(layer.type));
        }
    }
    return hash;
}

std::uint64_t HashPathTraceUpload(const RenderPathTrace::CompiledPathTraceScene& scene) {
    std::uint64_t hash = 1469598103934665603ull;
    hash = HashMix(hash, scene.structuralHash);
    hash = HashMix(hash, static_cast<std::uint64_t>(scene.featureMask));
    hash = HashPodVector(hash, scene.materials);
    hash = HashPodVector(hash, scene.materialLayers);
    hash = HashPodVector(hash, scene.spheres);
    hash = HashPodVector(hash, scene.triangles);
    hash = HashPodVector(hash, scene.primitiveRefs);
    hash = HashPodVector(hash, scene.bvhNodes);
    hash = HashPodVector(hash, scene.lights);
    hash = HashBytes(hash, &scene.environment, sizeof(scene.environment));
    return hash;
}

RenderPathTrace::GpuMaterial ToGpuMaterialHeader(
    const RenderMaterial& material,
    int layerOffset,
    int layerCount,
    std::uint32_t localFeatureMask) {
    RenderPathTrace::GpuMaterial gpu {};
    StoreCoefficients(gpu.baseCoefficients, BuildSpectralBasis({ material.baseColor.x, material.baseColor.y, material.baseColor.z }));
    const RenderFloat3 emissionColor = Scale(material.emissionColor, material.emissionStrength);
    StoreCoefficients(gpu.emissionCoefficients, BuildSpectralBasis({ emissionColor.x, emissionColor.y, emissionColor.z }));
    StoreCoefficients(gpu.absorptionCoefficients, BuildSpectralBasis({ material.absorptionColor.x, material.absorptionColor.y, material.absorptionColor.z }));
    gpu.params0[0] = material.roughness;
    gpu.params0[1] = material.metallic;
    gpu.params0[2] = material.transmission;
    gpu.params0[3] = material.ior;
    gpu.params1[0] = material.absorptionDistance;
    gpu.params1[1] = material.thinWalled ? 1.0f : 0.0f;
    gpu.params1[2] = static_cast<float>(material.surfacePreset);
    gpu.params1[3] = material.transmissionRoughness;
    gpu.params2[0] = EstimateDielectricDispersionScale(material.ior);
    gpu.params2[1] = 0.0f;
    gpu.params2[2] = 0.0f;
    gpu.params2[3] = 0.0f;
    gpu.layerInfo[0] = layerOffset;
    gpu.layerInfo[1] = layerCount;
    gpu.layerInfo[2] = static_cast<int>(localFeatureMask);
    gpu.layerInfo[3] = 0;
    return gpu;
}

RenderPathTrace::GpuMaterialLayer ToGpuMaterialLayer(const Material::Layer& layer) {
    RenderPathTrace::GpuMaterialLayer gpu {};
    StoreCoefficients(gpu.coefficients, BuildSpectralBasis({ layer.color.x, layer.color.y, layer.color.z }));
    gpu.params0[0] = static_cast<float>(layer.type);
    gpu.params0[1] = layer.weight;
    gpu.params0[2] = layer.roughness;
    gpu.params0[3] = layer.metallic;
    gpu.params1[0] = layer.transmission;
    gpu.params1[1] = layer.ior;
    gpu.params1[2] = layer.transmissionRoughness;
    gpu.params1[3] = layer.thinWalled ? 1.0f : 0.0f;
    return gpu;
}

bool IsPathTraceThinSheetGlass(const Material::Layer* baseLayer) {
    return baseLayer != nullptr &&
        baseLayer->type == MaterialLayerType::BaseDielectric &&
        baseLayer->thinWalled &&
        baseLayer->transmission > 0.001f;
}

Material BuildSpecializedPathTraceMaterial(const Material& material, PtThinWallPolicy thinWallPolicy) {
    Material specialized = material;
    if (specialized.layers.empty()) {
        SyncMaterialLayersFromLegacy(specialized);
    } else {
        SyncLegacyMaterialFromLayers(specialized);
    }

    if (Material::Layer* baseLayer = FindBaseMaterialLayer(specialized)) {
        if (baseLayer->type == MaterialLayerType::BaseDielectric) {
            baseLayer->thinWalled = thinWallPolicy == PtThinWallPolicy::ThinSheet;
        }
    }

    SyncLegacyMaterialFromLayers(specialized);
    return specialized;
}

RenderPathTrace::PathTraceFeatureMask AccumulatePathTraceMaterialFeatures(
    const Material& material,
    const RenderMaterial& runtimeMaterial) {

    RenderPathTrace::PathTraceFeatureMask featureMask = RenderPathTrace::PathTraceFeatureMask::None;

    if (IsEmissive(runtimeMaterial)) {
        featureMask |= RenderPathTrace::PathTraceFeatureMask::EmissiveGeometry;
    }
    const Material::Layer* baseLayer = FindBaseMaterialLayer(material);
    if (baseLayer != nullptr &&
        baseLayer->type == MaterialLayerType::BaseDielectric &&
        baseLayer->transmission > 0.001f) {
        featureMask |= RenderPathTrace::PathTraceFeatureMask::SpectralDielectricEta;
        if (IsPathTraceThinSheetGlass(baseLayer)) {
            featureMask |= RenderPathTrace::PathTraceFeatureMask::ThinWalledGlass;
        }
        if (baseLayer->transmissionRoughness > 0.001f) {
            featureMask |= RenderPathTrace::PathTraceFeatureMask::RoughDielectricGlass;
        }
    }

    const Material::Layer* clearCoatLayer = FindMaterialLayer(material, MaterialLayerType::ClearCoat);
    if (clearCoatLayer != nullptr && clearCoatLayer->weight > 0.0001f) {
        featureMask |= RenderPathTrace::PathTraceFeatureMask::ClearCoatLayering;
        if (baseLayer != nullptr &&
            baseLayer->type == MaterialLayerType::BaseDielectric &&
            baseLayer->transmissionRoughness > 0.001f) {
            featureMask |= RenderPathTrace::PathTraceFeatureMask::PolishedFrostedLayering;
        }
    }

    if (material.thinFilm > 0.0001f) {
        featureMask |= RenderPathTrace::PathTraceFeatureMask::ThinFilmDeferred;
    }

    return featureMask;
}

int ResolvePathTraceMaterialIndex(
    int authorMaterialIndex,
    PtThinWallPolicy thinWallPolicy,
    const SceneSnapshot& snapshot,
    const RenderScene& scene,
    RenderPathTrace::CompiledPathTraceScene& pathTraceScene,
    std::unordered_map<PtMaterialKey, int, PtMaterialKeyHash>& materialIndexCache) {

    const int resolvedAuthorMaterialIndex =
        std::clamp(authorMaterialIndex, 0, std::max(scene.GetMaterialCount() - 1, 0));
    const PtMaterialKey key { resolvedAuthorMaterialIndex, thinWallPolicy };
    const auto cached = materialIndexCache.find(key);
    if (cached != materialIndexCache.end()) {
        return cached->second;
    }

    Material specializedMaterial =
        resolvedAuthorMaterialIndex >= 0 && resolvedAuthorMaterialIndex < static_cast<int>(snapshot.materials.size())
            ? BuildSpecializedPathTraceMaterial(snapshot.materials[static_cast<std::size_t>(resolvedAuthorMaterialIndex)], thinWallPolicy)
            : Material {};
    const RenderMaterial runtimeMaterial = CompileMaterial(specializedMaterial);
    const RenderPathTrace::PathTraceFeatureMask localFeatureMask =
        AccumulatePathTraceMaterialFeatures(specializedMaterial, runtimeMaterial);
    const int layerOffset = static_cast<int>(pathTraceScene.materialLayers.size());
    for (const Material::Layer& layer : specializedMaterial.layers) {
        pathTraceScene.materialLayers.push_back(ToGpuMaterialLayer(layer));
    }
    const int specializedIndex = static_cast<int>(pathTraceScene.materials.size());
    pathTraceScene.materials.push_back(ToGpuMaterialHeader(
        runtimeMaterial,
        layerOffset,
        static_cast<int>(specializedMaterial.layers.size()),
        static_cast<std::uint32_t>(localFeatureMask)));
    pathTraceScene.featureMask |= localFeatureMask;
    materialIndexCache.emplace(key, specializedIndex);
    return specializedIndex;
}

RenderPathTrace::GpuSphere ToGpuSphere(const RenderResolvedSphere& sphere) {
    RenderPathTrace::GpuSphere gpu {};
    gpu.centerRadius[0] = sphere.center.x;
    gpu.centerRadius[1] = sphere.center.y;
    gpu.centerRadius[2] = sphere.center.z;
    gpu.centerRadius[3] = sphere.radius;
    gpu.surface[0] = static_cast<float>(sphere.materialIndex);
    gpu.surface[1] = static_cast<float>(sphere.objectId);
    gpu.surface[2] = 0.0f;
    gpu.surface[3] = 0.0f;
    return gpu;
}

RenderPathTrace::GpuTriangle ToGpuTriangle(const RenderResolvedTriangle& triangle) {
    RenderPathTrace::GpuTriangle gpu {};
    gpu.a[0] = triangle.a.x; gpu.a[1] = triangle.a.y; gpu.a[2] = triangle.a.z; gpu.a[3] = 1.0f;
    gpu.b[0] = triangle.b.x; gpu.b[1] = triangle.b.y; gpu.b[2] = triangle.b.z; gpu.b[3] = 1.0f;
    gpu.c[0] = triangle.c.x; gpu.c[1] = triangle.c.y; gpu.c[2] = triangle.c.z; gpu.c[3] = 1.0f;
    gpu.normalA[0] = triangle.normalA.x; gpu.normalA[1] = triangle.normalA.y; gpu.normalA[2] = triangle.normalA.z;
    gpu.normalB[0] = triangle.normalB.x; gpu.normalB[1] = triangle.normalB.y; gpu.normalB[2] = triangle.normalB.z;
    gpu.normalC[0] = triangle.normalC.x; gpu.normalC[1] = triangle.normalC.y; gpu.normalC[2] = triangle.normalC.z;
    gpu.uvAuvB[0] = triangle.uvA.x; gpu.uvAuvB[1] = triangle.uvA.y; gpu.uvAuvB[2] = triangle.uvB.x; gpu.uvAuvB[3] = triangle.uvB.y;
    gpu.uvC[0] = triangle.uvC.x; gpu.uvC[1] = triangle.uvC.y;
    gpu.surface[0] = static_cast<float>(triangle.materialIndex);
    gpu.surface[1] = static_cast<float>(triangle.meshInstanceId > 0 ? triangle.meshInstanceId : triangle.triangleId);
    gpu.surface[2] = static_cast<float>(triangle.triangleId);
    gpu.surface[3] = 0.0f;
    return gpu;
}

RenderPathTrace::GpuPrimitiveRef ToGpuPrimitiveRef(const RenderPrimitiveRef& primitiveRef) {
    RenderPathTrace::GpuPrimitiveRef gpu {};
    gpu.meta[0] = primitiveRef.type == RenderPrimitiveType::Sphere ? 0 : 1;
    gpu.meta[1] = primitiveRef.index;
    gpu.meta[2] = 0;
    gpu.meta[3] = 0;
    return gpu;
}

RenderPathTrace::GpuBvhNode ToGpuBvhNode(const RenderBvhNode& node) {
    RenderPathTrace::GpuBvhNode gpu {};
    gpu.minBounds[0] = node.bounds.min.x;
    gpu.minBounds[1] = node.bounds.min.y;
    gpu.minBounds[2] = node.bounds.min.z;
    gpu.maxBounds[0] = node.bounds.max.x;
    gpu.maxBounds[1] = node.bounds.max.y;
    gpu.maxBounds[2] = node.bounds.max.z;
    gpu.meta0[0] = node.leftChild;
    gpu.meta0[1] = node.rightChild;
    gpu.meta0[2] = node.firstPrimitive;
    gpu.meta0[3] = node.primitiveCount;
    gpu.meta1[0] = node.depth;
    gpu.meta1[1] = 0;
    gpu.meta1[2] = 0;
    gpu.meta1[3] = 0;
    return gpu;
}

RenderPathTrace::GpuLight ToGpuLight(const RenderLight& light) {
    RenderPathTrace::GpuLight gpu {};
    const RenderFloat3 direction = GetRenderLightDirection(light);
    const RenderFloat3 right = GetRenderLightRight(light);
    const RenderFloat3 up = GetRenderLightUp(light);
    gpu.positionType[0] = light.transform.translation.x;
    gpu.positionType[1] = light.transform.translation.y;
    gpu.positionType[2] = light.transform.translation.z;
    gpu.positionType[3] = static_cast<float>(light.type);
    gpu.directionRange[0] = direction.x;
    gpu.directionRange[1] = direction.y;
    gpu.directionRange[2] = direction.z;
    gpu.directionRange[3] = light.range;
    StoreCoefficients(gpu.colorCoefficients, BuildSpectralBasis({ light.color.x, light.color.y, light.color.z }));
    gpu.params[0] = light.areaSize.x;
    gpu.params[1] = light.areaSize.y;
    gpu.params[2] = light.innerConeDegrees;
    gpu.params[3] = light.outerConeDegrees;
    gpu.basis0[0] = right.x;
    gpu.basis0[1] = right.y;
    gpu.basis0[2] = right.z;
    gpu.basis0[3] = light.enabled ? 1.0f : 0.0f;
    gpu.basis1[0] = up.x;
    gpu.basis1[1] = up.y;
    gpu.basis1[2] = up.z;
    gpu.basis1[3] = light.intensity;
    return gpu;
}

RenderPathTrace::GpuEnvironment BuildEnvironment(const SceneMetadata& metadata) {
    RenderPathTrace::GpuEnvironment environment {};
    StoreCoefficients(environment.zenithCoefficients, BuildSpectralBasis({ 0.18f, 0.28f, 0.54f }));
    StoreCoefficients(environment.horizonCoefficients, BuildSpectralBasis({ 0.78f, 0.84f, 0.92f }));
    StoreCoefficients(environment.groundCoefficients, BuildSpectralBasis({ 0.05f, 0.06f, 0.08f }));
    environment.params[0] = metadata.environmentEnabled ? 1.0f : 0.0f;
    environment.params[1] = metadata.environmentIntensity;
    return environment;
}

RenderPathTrace::CompiledPathTraceScene BuildPathTraceScene(
    const SceneSnapshot& snapshot,
    const CompiledScene& compiledScene) {

    RenderPathTrace::CompiledPathTraceScene pathTraceScene;
    pathTraceScene.metadata = snapshot.metadata;
    pathTraceScene.camera = snapshot.camera;
    pathTraceScene.structuralHash = HashStructure(snapshot);
    pathTraceScene.environment = BuildEnvironment(snapshot.metadata);

    const RenderScene& scene = compiledScene.scene;
    std::unordered_map<int, PrimitiveType> primitiveTypesByObjectId;
    primitiveTypesByObjectId.reserve(snapshot.primitives.size());
    for (const Primitive& primitive : snapshot.primitives) {
        primitiveTypesByObjectId.emplace(static_cast<int>(primitive.id), primitive.type);
    }

    std::unordered_map<PtMaterialKey, int, PtMaterialKeyHash> ptMaterialIndices;
    ptMaterialIndices.reserve(static_cast<std::size_t>(std::max(scene.GetMaterialCount() * 2, 1)));
    pathTraceScene.materials.reserve(static_cast<std::size_t>(std::max(scene.GetMaterialCount() * 2, 1)));
    pathTraceScene.materialLayers.reserve(static_cast<std::size_t>(std::max(scene.GetMaterialCount() * 2, 1)));

    pathTraceScene.spheres.reserve(static_cast<std::size_t>(scene.GetSphereCount()));
    for (int sphereIndex = 0; sphereIndex < scene.GetSphereCount(); ++sphereIndex) {
        RenderResolvedSphere sphere = ResolveSphere(scene.GetSphere(sphereIndex));
        sphere.materialIndex = ResolvePathTraceMaterialIndex(
            sphere.materialIndex,
            PtThinWallPolicy::Solid,
            snapshot,
            scene,
            pathTraceScene,
            ptMaterialIndices);
        pathTraceScene.spheres.push_back(ToGpuSphere(sphere));
    }

    pathTraceScene.triangles.reserve(static_cast<std::size_t>(scene.GetResolvedTriangleCount()));
    for (int triangleIndex = 0; triangleIndex < scene.GetResolvedTriangleCount(); ++triangleIndex) {
        RenderResolvedTriangle triangle = scene.GetResolvedTriangle(triangleIndex);
        PtThinWallPolicy thinWallPolicy = PtThinWallPolicy::Solid;
        const auto primitiveType = primitiveTypesByObjectId.find(triangle.meshInstanceId);
        if (primitiveType != primitiveTypesByObjectId.end() &&
            primitiveType->second == PrimitiveType::Plane &&
            scene.GetMaterial(std::clamp(triangle.materialIndex, 0, std::max(scene.GetMaterialCount() - 1, 0))).thinWalled) {
            thinWallPolicy = PtThinWallPolicy::ThinSheet;
        }
        triangle.materialIndex = ResolvePathTraceMaterialIndex(
            triangle.materialIndex,
            thinWallPolicy,
            snapshot,
            scene,
            pathTraceScene,
            ptMaterialIndices);
        pathTraceScene.triangles.push_back(ToGpuTriangle(triangle));
    }

    pathTraceScene.primitiveRefs.reserve(scene.GetPrimitiveRefs().size());
    for (const RenderPrimitiveRef& primitiveRef : scene.GetPrimitiveRefs()) {
        pathTraceScene.primitiveRefs.push_back(ToGpuPrimitiveRef(primitiveRef));
    }

    pathTraceScene.bvhNodes.reserve(scene.GetBvhNodes().size());
    for (const RenderBvhNode& node : scene.GetBvhNodes()) {
        pathTraceScene.bvhNodes.push_back(ToGpuBvhNode(node));
    }

    pathTraceScene.lights.reserve(static_cast<std::size_t>(scene.GetLightCount()));
    for (int lightIndex = 0; lightIndex < scene.GetLightCount(); ++lightIndex) {
        const RenderLight& light = scene.GetLight(lightIndex);
        if (light.type == RenderLightType::RectArea) {
            pathTraceScene.featureMask |= RenderPathTrace::PathTraceFeatureMask::AreaLights;
        }
        pathTraceScene.lights.push_back(ToGpuLight(light));
    }

    if (snapshot.metadata.environmentEnabled) {
        pathTraceScene.featureMask |= RenderPathTrace::PathTraceFeatureMask::Environment;
    }
    pathTraceScene.featureMask |= RenderPathTrace::PathTraceFeatureMask::ThinFilmDeferred;

    pathTraceScene.uploadHash = HashPathTraceUpload(pathTraceScene);
    pathTraceScene.valid = true;
    return pathTraceScene;
}

} // namespace

bool SceneCompiler::Compile(
    const SceneSnapshot& snapshot,
    const RenderFoundation::Settings& settings,
    CompiledScene& outCompiledScene,
    std::string& errorMessage) const {

    errorMessage.clear();

    std::vector<RenderImportedAsset> importedAssets;
    std::vector<RenderImportedTexture> importedTextures;
    std::vector<RenderMaterial> materials;
    std::vector<RenderMeshDefinition> meshes;
    std::vector<RenderMeshInstance> meshInstances;
    std::vector<RenderSphere> spheres;
    std::vector<RenderTriangle> triangles;
    std::vector<RenderLight> lights;
    std::unordered_map<Id, int> materialIndices;

    materials.reserve(snapshot.materials.size());
    for (std::size_t index = 0; index < snapshot.materials.size(); ++index) {
        materials.push_back(CompileMaterial(snapshot.materials[index]));
        materialIndices[snapshot.materials[index].id] = static_cast<int>(index);
    }
    if (materials.empty()) {
        materials.push_back(BuildRenderMaterial("Fallback Diffuse", MakeRenderFloat3(0.8f, 0.8f, 0.8f)));
    }

    spheres.reserve(snapshot.primitives.size());
    meshInstances.reserve(snapshot.primitives.size());
    meshes.reserve(snapshot.primitives.size());
    for (const Primitive& primitive : snapshot.primitives) {
        if (!primitive.visible) {
            continue;
        }

        const int materialIndex = ResolveMaterialIndex(materialIndices, primitive.materialId);
        if (primitive.type == PrimitiveType::Sphere) {
            RenderSphere sphere;
            sphere.id = static_cast<int>(primitive.id);
            sphere.name = primitive.name;
            sphere.transform = ToRuntimeTransform(primitive.transform);
            sphere.localCenter = MakeRenderFloat3(0.0f, 0.0f, 0.0f);
            sphere.radius = 0.5f;
            sphere.materialIndex = materialIndex;
            sphere.albedoTint = MakeRenderFloat3(1.0f, 1.0f, 1.0f);
            spheres.push_back(sphere);
            continue;
        }

        RenderMeshDefinition meshDefinition =
            primitive.type == PrimitiveType::Plane
                ? BuildRenderMeshDefinition(primitive.name + " Plane", BuildPlaneTriangles(materialIndex))
                : BuildRenderMeshDefinition(primitive.name + " Cube", BuildCubeTriangles(materialIndex));
        meshes.push_back(meshDefinition);

        RenderMeshInstance instance;
        instance.id = static_cast<int>(primitive.id);
        instance.name = primitive.name;
        instance.meshIndex = static_cast<int>(meshes.size()) - 1;
        instance.transform = ToRuntimeTransform(primitive.transform);
        instance.colorTint = MakeRenderFloat3(1.0f, 1.0f, 1.0f);
        meshInstances.push_back(instance);
    }

    lights.reserve(snapshot.lights.size());
    for (const Light& light : snapshot.lights) {
        RenderLight runtime;
        runtime.id = static_cast<int>(light.id);
        runtime.name = light.name;
        runtime.type = ToRuntimeLightType(light.type);
        runtime.transform = ToRuntimeTransform(light.transform);
        runtime.color = ToRuntime(light.color);
        runtime.intensity = light.intensity;
        runtime.areaSize = ToRuntime(light.areaSize);
        runtime.range = light.range;
        runtime.innerConeDegrees = light.innerConeDegrees;
        runtime.outerConeDegrees = light.outerConeDegrees;
        runtime.enabled = light.enabled;
        lights.push_back(runtime);
    }

    outCompiledScene.scene.ApplySceneSnapshot(
        snapshot.metadata.label,
        snapshot.metadata.description,
        ToRuntimeBackgroundMode(snapshot.metadata.backgroundMode),
        snapshot.metadata.environmentEnabled,
        snapshot.metadata.environmentIntensity,
        snapshot.metadata.fogEnabled,
        ToRuntime(snapshot.metadata.fogColor),
        snapshot.metadata.fogDensity,
        snapshot.metadata.fogAnisotropy,
        std::move(importedAssets),
        std::move(importedTextures),
        std::move(materials),
        std::move(meshes),
        std::move(meshInstances),
        std::move(spheres),
        std::move(triangles),
        std::move(lights),
        "Compiled foundation scene snapshot.");

    outCompiledScene.camera.ApplySnapshot(
        ToRuntime(snapshot.camera.position),
        snapshot.camera.yawDegrees,
        snapshot.camera.pitchDegrees,
        snapshot.camera.fieldOfViewDegrees,
        snapshot.camera.focusDistance,
        snapshot.camera.apertureRadius,
        snapshot.camera.exposure,
        "Compiled foundation camera snapshot.");

    outCompiledScene.settings.SetResolution(settings.resolutionX, settings.resolutionY);
    outCompiledScene.settings.SetPreviewSampleTarget(settings.previewSampleTarget);
    outCompiledScene.settings.SetAccumulationEnabled(settings.accumulationEnabled);
    outCompiledScene.settings.SetIntegratorMode(ToRuntimeIntegratorMode(settings.viewMode));
    outCompiledScene.settings.SetMaxBounceCount(settings.maxBounceCount);
    outCompiledScene.settings.SetGizmoMode(ToRuntimeGizmoMode(settings.transformMode));
    outCompiledScene.settings.SetTransformSpace(ToRuntimeTransformSpace(settings.transformSpace));
    outCompiledScene.settings.SetFinalRenderResolution(settings.finalRender.resolutionX, settings.finalRender.resolutionY);
    outCompiledScene.settings.SetFinalRenderSampleTarget(settings.finalRender.sampleTarget);
    outCompiledScene.settings.SetFinalRenderMaxBounceCount(settings.finalRender.maxBounceCount);
    outCompiledScene.settings.SetFinalRenderOutputName(settings.finalRender.outputName);
    outCompiledScene.pathTraceScene = BuildPathTraceScene(snapshot, outCompiledScene);
    outCompiledScene.valid = outCompiledScene.pathTraceScene.valid;
    return true;
}

} // namespace RenderContracts
