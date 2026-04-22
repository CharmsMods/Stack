#pragma once

#include "RenderTab/Foundation/RenderFoundationTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace RenderPathTrace {

inline constexpr int kSpectralSampleCount = 4;
inline constexpr int kPathTraceDebugMaxBounces = 8;

enum class PathTraceFeatureMask : std::uint32_t {
    None = 0,
    Environment = 1u << 0,
    EmissiveGeometry = 1u << 1,
    AreaLights = 1u << 2,
    ThinWalledGlass = 1u << 3,
    RoughDielectricGlass = 1u << 4,
    SpectralDielectricEta = 1u << 5,
    ClearCoatLayering = 1u << 6,
    PolishedFrostedLayering = 1u << 7,
    ThinFilmDeferred = 1u << 8
};

inline PathTraceFeatureMask operator|(PathTraceFeatureMask left, PathTraceFeatureMask right) {
    return static_cast<PathTraceFeatureMask>(
        static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

inline PathTraceFeatureMask& operator|=(PathTraceFeatureMask& left, PathTraceFeatureMask right) {
    left = left | right;
    return left;
}

struct SpectralBasisCoefficients {
    std::array<float, kSpectralSampleCount> coefficients {};
};

struct alignas(16) GpuMaterial {
    float baseCoefficients[4] {};
    float emissionCoefficients[4] {};
    float absorptionCoefficients[4] {};
    float params0[4] {};
    float params1[4] {};
    float params2[4] {};
    int layerInfo[4] {};
};

struct alignas(16) GpuMaterialLayer {
    float coefficients[4] {};
    float params0[4] {};
    float params1[4] {};
};

struct alignas(16) GpuSphere {
    float centerRadius[4] {};
    float surface[4] {};
};

struct alignas(16) GpuTriangle {
    float a[4] {};
    float b[4] {};
    float c[4] {};
    float normalA[4] {};
    float normalB[4] {};
    float normalC[4] {};
    float uvAuvB[4] {};
    float uvC[4] {};
    float surface[4] {};
};

struct alignas(16) GpuPrimitiveRef {
    int meta[4] {};
};

struct alignas(16) GpuBvhNode {
    float minBounds[4] {};
    float maxBounds[4] {};
    int meta0[4] {};
    int meta1[4] {};
};

struct alignas(16) GpuLight {
    float positionType[4] {};
    float directionRange[4] {};
    float colorCoefficients[4] {};
    float params[4] {};
    float basis0[4] {};
    float basis1[4] {};
};

struct alignas(16) GpuEnvironment {
    float zenithCoefficients[4] {};
    float horizonCoefficients[4] {};
    float groundCoefficients[4] {};
    float params[4] {};
};

struct CompiledPathTraceScene {
    std::uint64_t structuralHash = 0;
    std::uint64_t uploadHash = 0;
    PathTraceFeatureMask featureMask = PathTraceFeatureMask::None;
    std::vector<GpuMaterial> materials;
    std::vector<GpuMaterialLayer> materialLayers;
    std::vector<GpuSphere> spheres;
    std::vector<GpuTriangle> triangles;
    std::vector<GpuPrimitiveRef> primitiveRefs;
    std::vector<GpuBvhNode> bvhNodes;
    std::vector<GpuLight> lights;
    GpuEnvironment environment {};
    RenderFoundation::SceneMetadata metadata {};
    RenderFoundation::Camera camera {};
    bool valid = false;
};

struct PathTraceDebugBounce {
    float hitT = 0.0f;
    int objectId = -1;
    int materialIndex = -1;
    bool frontFace = false;
    bool insideMedium = false;
    float etaI = 1.0f;
    float etaT = 1.0f;
    float etaRatio = 1.0f;
    float fresnel = 0.0f;
    int decision = 0;
    RenderFoundation::Vec3 geometricNormal {};
    RenderFoundation::Vec3 shadingNormal {};
    RenderFoundation::Vec3 spawnedOrigin {};
    RenderFoundation::Vec3 spawnedDirection {};
};

struct PathTraceDebugReadback {
    bool valid = false;
    int pixelX = -1;
    int pixelY = -1;
    int bounceCount = 0;
    std::array<PathTraceDebugBounce, kPathTraceDebugMaxBounces> bounces {};
};

} // namespace RenderPathTrace
