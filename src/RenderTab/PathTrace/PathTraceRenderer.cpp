#include "RenderTab/PathTrace/PathTraceRenderer.h"

#include "RenderTab/Contracts/AccumulationManager.h"
#include "RenderTab/Shaders/EmbeddedShaders.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <utility>
#include <vector>

namespace RenderPathTrace {

namespace {

constexpr GLuint kMaterialBinding = 0;
constexpr GLuint kMaterialLayerBinding = 1;
constexpr GLuint kSphereBinding = 2;
constexpr GLuint kTriangleBinding = 3;
constexpr GLuint kPrimitiveRefBinding = 4;
constexpr GLuint kBvhBinding = 5;
constexpr GLuint kLightBinding = 6;
constexpr GLuint kEnvironmentBinding = 7;
constexpr GLuint kRayStateBinding = 8;
constexpr GLuint kHitStateBinding = 9;
constexpr GLuint kActiveQueueBinding = 10;
constexpr GLuint kNextQueueBinding = 11;
constexpr GLuint kShadowQueueBinding = 12;
constexpr GLuint kQueueCountBinding = 13;
constexpr GLuint kDebugLogBinding = 14;
constexpr GLuint kGuideStateBinding = 15;
constexpr int kWorkgroupSize = 64;
constexpr int kImageWorkgroupSize = 8;
constexpr int kMaterialTextureSize = 256;
constexpr int kSmartBounceBudgetBase = 24;
constexpr int kSmartBounceBudgetSpecular = 48;
constexpr int kSmartBounceBudgetMedium = 64;
constexpr int kSmartBounceBudgetReference = 96;
constexpr int kViewportBounceCeiling = 8;
constexpr int kCameraPreviewBounceCeiling = 8;

struct alignas(16) GpuRayState {
    float origin[4] {};
    float direction[4] {};
    float throughput[4] {};
    float radiance[4] {};
    float wavelengths[4] {};
    float wavelengthPdfs[4] {};
    float absorptionCoefficients[4] {};
    int meta[4] {};
    float params[4] {};
};

struct alignas(16) GpuHitState {
    float hit0[4] {};
    float hit1[4] {};
    float hit2[4] {};
    float hit3[4] {};
};

struct alignas(16) GpuGuideState {
    float albedo[4] {};
    float normalDepth[4] {};
};

struct alignas(16) GpuShadowRay {
    float originMaxDistance[4] {};
    float directionContributionScale[4] {};
    float contribution[4] {};
    int meta[4] {};
};

struct alignas(16) QueueCounts {
    unsigned int counts[4] {};
};

struct alignas(16) GpuDebugHeader {
    int meta[4] {};
};

struct alignas(16) GpuDebugBounce {
    float hitInfo[4] {};
    float geometricNormal[4] {};
    float shadingNormal[4] {};
    float etaInfo[4] {};
    float mediumInfo[4] {};
    float lightInfo[4] {};
    float spawnedOrigin[4] {};
    float spawnedDirection[4] {};
};

struct alignas(16) GpuDebugReadback {
    GpuDebugHeader header {};
    GpuDebugBounce bounces[kPathTraceDebugMaxBounces] {};
};

RenderFoundation::Vec3 ToFoundationVec3(const float value[4]) {
    return { value[0], value[1], value[2] };
}

RenderFoundation::PathTraceDebugMode ToPathTraceDebugMode(const RenderFoundation::Settings& settings) {
    return settings.pathTraceDebugMode;
}

std::array<int, 2> ResolveDebugPixel(
    const RenderFoundation::Settings& settings,
    int width,
    int height) {

    const int resolvedWidth = std::max(width, 1);
    const int resolvedHeight = std::max(height, 1);
    const int defaultX = resolvedWidth / 2;
    const int defaultY = resolvedHeight / 2;
    const int pixelX = settings.pathTraceDebugPixelX >= 0
        ? std::clamp(settings.pathTraceDebugPixelX, 0, resolvedWidth - 1)
        : defaultX;
    const int pixelY = settings.pathTraceDebugPixelY >= 0
        ? std::clamp(settings.pathTraceDebugPixelY, 0, resolvedHeight - 1)
        : defaultY;
    return { pixelX, pixelY };
}

bool HasPathTraceFeature(
    RenderPathTrace::PathTraceFeatureMask featureMask,
    RenderPathTrace::PathTraceFeatureMask feature) {

    return (static_cast<std::uint32_t>(featureMask) & static_cast<std::uint32_t>(feature)) != 0u;
}

int ResolveEffectiveMaxBounces(
    RenderSurfaceClass surfaceClass,
    const RenderFoundation::Settings& settings,
    const RenderPathTrace::CompiledPathTraceScene& scene) {

    int surfaceBounceCeiling = 512;
    switch (surfaceClass) {
        case RenderSurfaceClass::Viewport:
            surfaceBounceCeiling = kViewportBounceCeiling;
            break;
        case RenderSurfaceClass::CameraPreview:
            surfaceBounceCeiling = kCameraPreviewBounceCeiling;
            break;
        case RenderSurfaceClass::FinalRender:
        default:
            break;
    }

    if (settings.pathTraceTerminationMode != RenderFoundation::PathTraceTerminationMode::Smart) {
        return std::min(std::clamp(settings.maxBounceCount, 1, 512), surfaceBounceCeiling);
    }

    int automaticBudget = kSmartBounceBudgetBase;
    if (HasPathTraceFeature(scene.featureMask, RenderPathTrace::PathTraceFeatureMask::SpectralDielectricEta) ||
        HasPathTraceFeature(scene.featureMask, RenderPathTrace::PathTraceFeatureMask::ThinWalledGlass) ||
        HasPathTraceFeature(scene.featureMask, RenderPathTrace::PathTraceFeatureMask::RoughDielectricGlass) ||
        HasPathTraceFeature(scene.featureMask, RenderPathTrace::PathTraceFeatureMask::ClearCoatLayering)) {
        automaticBudget = std::max(automaticBudget, kSmartBounceBudgetSpecular);
    }
    if (HasPathTraceFeature(scene.featureMask, RenderPathTrace::PathTraceFeatureMask::FogMedium) ||
        HasPathTraceFeature(scene.featureMask, RenderPathTrace::PathTraceFeatureMask::LaserLight)) {
        automaticBudget = std::max(automaticBudget, kSmartBounceBudgetMedium);
    }
    if (settings.pathTraceTransportMode == RenderFoundation::PathTraceTransportMode::CausticsReference) {
        automaticBudget = std::max(automaticBudget, kSmartBounceBudgetReference);
    }

    switch (surfaceClass) {
        case RenderSurfaceClass::Viewport:
            automaticBudget = std::min(automaticBudget, kViewportBounceCeiling);
            break;
        case RenderSurfaceClass::CameraPreview:
            automaticBudget = std::min(automaticBudget, kCameraPreviewBounceCeiling);
            break;
        case RenderSurfaceClass::FinalRender:
        default:
            break;
    }

    return automaticBudget;
}

void SetPathTraceDebugUniforms(
    GLuint program,
    const RenderFoundation::Settings& settings,
    int width,
    int height) {

    const auto debugPixel = ResolveDebugPixel(settings, width, height);
    glUniform1i(glGetUniformLocation(program, "uDebugMode"), static_cast<int>(ToPathTraceDebugMode(settings)));
    glUniform2i(glGetUniformLocation(program, "uDebugPixel"), debugPixel[0], debugPixel[1]);
}

std::string LoadShaderFile(const char* fileName) {
    const std::filesystem::path shaderPath =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "Shaders" / fileName;
    std::string source = GLHelpers::ReadFile(shaderPath.string());
    if (source.empty()) {
        source = EmbeddedShaders::Get(fileName);
    }
    return source;
}

std::string BuildShaderSource(const char* fileName) {
    const std::string commonSource = LoadShaderFile("PathTraceCommon.glsl");
    const std::string stageSource = LoadShaderFile(fileName);
    if (commonSource.empty() || stageSource.empty()) {
        return {};
    }
    return std::string("#version 430 core\n") + commonSource + "\n" + stageSource;
}

template <typename T>
void UploadStaticBuffer(GLuint& buffer, GLuint bindingIndex, const std::vector<T>& data) {
    if (data.empty()) {
        if (buffer != 0) {
            glDeleteBuffers(1, &buffer);
            buffer = 0;
        }
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingIndex, 0);
        return;
    }

    if (buffer == 0) {
        glGenBuffers(1, &buffer);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        static_cast<GLsizeiptr>(data.size() * sizeof(T)),
        data.data(),
        GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingIndex, buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void EnsureDynamicBuffer(GLuint& buffer, GLuint bindingIndex, std::size_t byteSize) {
    if (buffer == 0) {
        glGenBuffers(1, &buffer);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(byteSize), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingIndex, buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void UploadQueueCounts(GLuint buffer, unsigned int activeCount, unsigned int nextCount, unsigned int shadowCount, unsigned int pixelCount) {
    QueueCounts counts {};
    counts.counts[0] = activeCount;
    counts.counts[1] = nextCount;
    counts.counts[2] = shadowCount;
    counts.counts[3] = pixelCount;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(QueueCounts), &counts);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void ReadbackQueueCounts(GLuint buffer, QueueCounts& outCounts) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(QueueCounts), &outCounts);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void ClearDisplayTexture(GLuint framebuffer, int width, int height) {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

std::vector<unsigned char> ResizeTexturePixels(const RenderImportedTexture& texture, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(kMaterialTextureSize * kMaterialTextureSize * 4), 0);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = a;
    }

    if (texture.width <= 0 || texture.height <= 0 || texture.pixels.empty()) {
        return pixels;
    }

    for (int y = 0; y < kMaterialTextureSize; ++y) {
        const int sourceY = std::clamp((y * texture.height) / kMaterialTextureSize, 0, texture.height - 1);
        for (int x = 0; x < kMaterialTextureSize; ++x) {
            const int sourceX = std::clamp((x * texture.width) / kMaterialTextureSize, 0, texture.width - 1);
            const std::size_t sourceIndex = static_cast<std::size_t>((sourceY * texture.width + sourceX) * 4);
            const std::size_t targetIndex = static_cast<std::size_t>((y * kMaterialTextureSize + x) * 4);
            pixels[targetIndex + 0] = texture.pixels[sourceIndex + 0];
            pixels[targetIndex + 1] = texture.pixels[sourceIndex + 1];
            pixels[targetIndex + 2] = texture.pixels[sourceIndex + 2];
            pixels[targetIndex + 3] = texture.pixels[sourceIndex + 3];
        }
    }

    return pixels;
}

bool DenoiserEnabled(const RenderFoundation::Settings& settings) {
    return settings.denoiser.enabled && settings.denoiser.mode != RenderFoundation::DenoiserMode::Off;
}

std::array<GLuint, 2> ResolveImageGroups2D(int width, int height) {
    return {
        static_cast<GLuint>((std::max(width, 1) + kImageWorkgroupSize - 1) / kImageWorkgroupSize),
        static_cast<GLuint>((std::max(height, 1) + kImageWorkgroupSize - 1) / kImageWorkgroupSize)
    };
}

} // namespace

PathTraceRenderer::~PathTraceRenderer() {
    Shutdown();
}

bool PathTraceRenderer::InitializePrograms(std::string& errorMessage) {
    if (m_GenerateProgram != 0 &&
        m_IntersectProgram != 0 &&
        m_ShadeProgram != 0 &&
        m_ShadowProgram != 0 &&
        m_AccumulateProgram != 0 &&
        m_ResolveProgram != 0 &&
        m_ResolveVarianceProgram != 0 &&
        m_BilateralProgram != 0 &&
        m_ATrousProgram != 0 &&
        m_PresentProgram != 0) {
        return true;
    }

    const std::string generateSource = BuildShaderSource("PathTraceGenerate.comp");
    const std::string intersectSource = BuildShaderSource("PathTraceIntersect.comp");
    const std::string shadeSource = BuildShaderSource("PathTraceShadeSurface.comp");
    const std::string shadowSource = BuildShaderSource("PathTraceShadow.comp");
    const std::string accumulateSource = BuildShaderSource("PathTraceAccumulate.comp");
    const std::string resolveSource = BuildShaderSource("PathTraceResolve.comp");
    const std::string resolveVarianceSource = BuildShaderSource("PathTraceResolveVariance.comp");
    const std::string bilateralSource = BuildShaderSource("PathTraceDenoiseBilateral.comp");
    const std::string atrousSource = BuildShaderSource("PathTraceDenoiseATrous.comp");
    const std::string presentSource = BuildShaderSource("PathTracePresent.comp");
    if (generateSource.empty() || intersectSource.empty() || shadeSource.empty() || shadowSource.empty() ||
        accumulateSource.empty() || resolveSource.empty() || resolveVarianceSource.empty() || bilateralSource.empty() ||
        atrousSource.empty() || presentSource.empty()) {
        errorMessage = "Failed to load one or more path-trace compute shaders.";
        return false;
    }

    m_GenerateProgram = GLHelpers::CreateComputeProgram(generateSource.c_str());
    m_IntersectProgram = GLHelpers::CreateComputeProgram(intersectSource.c_str());
    m_ShadeProgram = GLHelpers::CreateComputeProgram(shadeSource.c_str());
    m_ShadowProgram = GLHelpers::CreateComputeProgram(shadowSource.c_str());
    m_AccumulateProgram = GLHelpers::CreateComputeProgram(accumulateSource.c_str());
    m_ResolveProgram = GLHelpers::CreateComputeProgram(resolveSource.c_str());
    m_ResolveVarianceProgram = GLHelpers::CreateComputeProgram(resolveVarianceSource.c_str());
    m_BilateralProgram = GLHelpers::CreateComputeProgram(bilateralSource.c_str());
    m_ATrousProgram = GLHelpers::CreateComputeProgram(atrousSource.c_str());
    m_PresentProgram = GLHelpers::CreateComputeProgram(presentSource.c_str());
    if (m_GenerateProgram == 0 || m_IntersectProgram == 0 || m_ShadeProgram == 0 ||
        m_ShadowProgram == 0 || m_AccumulateProgram == 0 || m_ResolveProgram == 0 ||
        m_ResolveVarianceProgram == 0 ||
        m_BilateralProgram == 0 || m_ATrousProgram == 0 || m_PresentProgram == 0) {
        errorMessage = "Failed to compile the path-trace compute shaders.";
        ReleasePrograms();
        return false;
    }

    return true;
}

bool PathTraceRenderer::EnsureDefaultTextureArrays(std::string& errorMessage) {
    errorMessage.clear();
    if (m_BaseColorArray != 0 && m_MaterialParamsArray != 0 && m_EmissiveArray != 0 && m_NormalArray != 0) {
        return true;
    }

    ReleaseTextureArrays();
    m_BaseColorArray = GLHelpers::CreateTextureArray(1, 1, 1, GL_RGBA8);
    m_MaterialParamsArray = GLHelpers::CreateTextureArray(1, 1, 1, GL_RGBA8);
    m_EmissiveArray = GLHelpers::CreateTextureArray(1, 1, 1, GL_RGBA8);
    m_NormalArray = GLHelpers::CreateTextureArray(1, 1, 1, GL_RGBA8);
    if (m_BaseColorArray == 0 || m_MaterialParamsArray == 0 || m_EmissiveArray == 0 || m_NormalArray == 0) {
        errorMessage = "Failed to allocate default path-trace texture arrays.";
        ReleaseTextureArrays();
        return false;
    }

    const std::array<unsigned char, 4> white = { 255, 255, 255, 255 };
    const std::array<unsigned char, 4> neutralNormal = { 128, 128, 255, 255 };
    GLHelpers::UploadTextureArrayLayer(m_BaseColorArray, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white.data());
    GLHelpers::UploadTextureArrayLayer(m_MaterialParamsArray, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white.data());
    GLHelpers::UploadTextureArrayLayer(m_EmissiveArray, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white.data());
    GLHelpers::UploadTextureArrayLayer(m_NormalArray, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, neutralNormal.data());
    m_UploadedTextureSceneRevision = 0;
    return true;
}

bool PathTraceRenderer::UploadMaterialTextures(const RenderScene& runtimeScene, std::string& errorMessage) {
    errorMessage.clear();
    if (!EnsureDefaultTextureArrays(errorMessage)) {
        return false;
    }

    if (m_UploadedTextureSceneRevision == runtimeScene.GetRevision()) {
        return true;
    }

    ReleaseTextureArrays();
    const int layerCount = std::max(1, runtimeScene.GetImportedTextureCount() + 1);
    m_BaseColorArray = GLHelpers::CreateTextureArray(kMaterialTextureSize, kMaterialTextureSize, layerCount, GL_RGBA8);
    m_MaterialParamsArray = GLHelpers::CreateTextureArray(kMaterialTextureSize, kMaterialTextureSize, layerCount, GL_RGBA8);
    m_EmissiveArray = GLHelpers::CreateTextureArray(kMaterialTextureSize, kMaterialTextureSize, layerCount, GL_RGBA8);
    m_NormalArray = GLHelpers::CreateTextureArray(kMaterialTextureSize, kMaterialTextureSize, layerCount, GL_RGBA8);
    if (m_BaseColorArray == 0 || m_MaterialParamsArray == 0 || m_EmissiveArray == 0 || m_NormalArray == 0) {
        errorMessage = "Failed to allocate path-trace material texture arrays.";
        ReleaseTextureArrays();
        return false;
    }

    const std::array<unsigned char, 4> white = { 255, 255, 255, 255 };
    const std::array<unsigned char, 4> neutralNormal = { 128, 128, 255, 255 };
    GLHelpers::UploadTextureArrayLayer(m_BaseColorArray, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white.data());
    GLHelpers::UploadTextureArrayLayer(m_MaterialParamsArray, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white.data());
    GLHelpers::UploadTextureArrayLayer(m_EmissiveArray, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white.data());
    GLHelpers::UploadTextureArrayLayer(m_NormalArray, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, neutralNormal.data());

    for (int textureIndex = 0; textureIndex < runtimeScene.GetImportedTextureCount(); ++textureIndex) {
        const RenderImportedTexture& texture = runtimeScene.GetImportedTexture(textureIndex);
        std::array<unsigned char, 4> fallback = white;
        unsigned int targetArray = m_BaseColorArray;
        switch (texture.semantic) {
            case RenderTextureSemantic::BaseColor:
                targetArray = m_BaseColorArray;
                fallback = white;
                break;
            case RenderTextureSemantic::MetallicRoughness:
                targetArray = m_MaterialParamsArray;
                fallback = white;
                break;
            case RenderTextureSemantic::Emissive:
                targetArray = m_EmissiveArray;
                fallback = white;
                break;
            case RenderTextureSemantic::Normal:
                targetArray = m_NormalArray;
                fallback = neutralNormal;
                break;
        }

        std::vector<unsigned char> resizedPixels = ResizeTexturePixels(texture, fallback[0], fallback[1], fallback[2], fallback[3]);
        GLHelpers::UploadTextureArrayLayer(
            targetArray,
            textureIndex + 1,
            kMaterialTextureSize,
            kMaterialTextureSize,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            resizedPixels.data());
    }

    m_UploadedTextureSceneRevision = runtimeScene.GetRevision();
    return true;
}

bool PathTraceRenderer::EnsureBuffers(int width, int height, std::string& errorMessage) {
    if (width <= 0 || height <= 0) {
        errorMessage = "Invalid path-trace target size.";
        return false;
    }

    if (m_Buffers.width == width &&
        m_Buffers.height == height &&
        m_Buffers.displayTextures[0] != 0 &&
        m_Buffers.displayTextures[1] != 0 &&
        m_Buffers.hdrAccumTextures[0] != 0 &&
        m_Buffers.hdrAccumTextures[1] != 0 &&
        m_Buffers.denoisePongTextures[0] != 0 &&
        m_Buffers.denoisePongTextures[1] != 0) {
        return true;
    }

    ReleaseTextures();
    m_Buffers.width = width;
    m_Buffers.height = height;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    for (int slot = 0; slot < 2; ++slot) {
        m_Buffers.hdrAccumTextures[slot] = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
        m_Buffers.currentSampleTextures[slot] = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
        m_Buffers.guideAlbedoAccumTextures[slot] = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
        m_Buffers.guideNormalAccumTextures[slot] = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
        m_Buffers.guideDepthAccumTextures[slot] = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
        m_Buffers.varianceAccumTextures[slot] = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
        m_Buffers.resolvedBeautyTextures[slot] = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
        m_Buffers.resolvedAlbedoTextures[slot] = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
        m_Buffers.resolvedNormalTextures[slot] = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
        m_Buffers.resolvedDepthTextures[slot] = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
        m_Buffers.resolvedVarianceTextures[slot] = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
        m_Buffers.denoisePingTextures[slot] = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
        m_Buffers.denoisePongTextures[slot] = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
        m_Buffers.displayTextures[slot] = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
        m_Buffers.framebuffers[slot] = GLHelpers::CreateFBO(m_Buffers.displayTextures[slot]);
        if (m_Buffers.hdrAccumTextures[slot] == 0 ||
            m_Buffers.currentSampleTextures[slot] == 0 ||
            m_Buffers.guideAlbedoAccumTextures[slot] == 0 ||
            m_Buffers.guideNormalAccumTextures[slot] == 0 ||
            m_Buffers.guideDepthAccumTextures[slot] == 0 ||
            m_Buffers.varianceAccumTextures[slot] == 0 ||
            m_Buffers.resolvedBeautyTextures[slot] == 0 ||
            m_Buffers.resolvedAlbedoTextures[slot] == 0 ||
            m_Buffers.resolvedNormalTextures[slot] == 0 ||
            m_Buffers.resolvedDepthTextures[slot] == 0 ||
            m_Buffers.resolvedVarianceTextures[slot] == 0 ||
            m_Buffers.denoisePingTextures[slot] == 0 ||
            m_Buffers.denoisePongTextures[slot] == 0 ||
            m_Buffers.displayTextures[slot] == 0 ||
            m_Buffers.framebuffers[slot] == 0) {
            errorMessage = "Failed to allocate path-trace render targets.";
            ReleaseTextures();
            return false;
        }
        ClearDisplayTexture(m_Buffers.framebuffers[slot], width, height);
    }

    EnsureDynamicBuffer(m_Buffers.rayStateBuffer, kRayStateBinding, pixelCount * sizeof(GpuRayState));
    EnsureDynamicBuffer(m_Buffers.hitStateBuffer, kHitStateBinding, pixelCount * sizeof(GpuHitState));
    EnsureDynamicBuffer(m_Buffers.activeQueueBuffer, kActiveQueueBinding, pixelCount * sizeof(unsigned int));
    EnsureDynamicBuffer(m_Buffers.nextQueueBuffer, kNextQueueBinding, pixelCount * sizeof(unsigned int));
    EnsureDynamicBuffer(m_Buffers.shadowQueueBuffer, kShadowQueueBinding, pixelCount * sizeof(GpuShadowRay));
    EnsureDynamicBuffer(m_Buffers.queueCountBuffer, kQueueCountBinding, sizeof(QueueCounts));
    EnsureDynamicBuffer(m_Buffers.debugLogBuffer, kDebugLogBinding, sizeof(GpuDebugReadback));
    EnsureDynamicBuffer(m_Buffers.guideStateBuffer, kGuideStateBinding, pixelCount * sizeof(GpuGuideState));
    return true;
}

bool PathTraceRenderer::UploadScene(const CompiledPathTraceScene& scene, std::string& errorMessage) {
    errorMessage.clear();
    if (m_UploadedSceneHash == scene.uploadHash) {
        return true;
    }

    UploadStaticBuffer(m_Buffers.materialBuffer, kMaterialBinding, scene.materials);
    UploadStaticBuffer(m_Buffers.materialLayerBuffer, kMaterialLayerBinding, scene.materialLayers);
    UploadStaticBuffer(m_Buffers.sphereBuffer, kSphereBinding, scene.spheres);
    UploadStaticBuffer(m_Buffers.triangleBuffer, kTriangleBinding, scene.triangles);
    UploadStaticBuffer(m_Buffers.primitiveRefBuffer, kPrimitiveRefBinding, scene.primitiveRefs);
    UploadStaticBuffer(m_Buffers.bvhBuffer, kBvhBinding, scene.bvhNodes);
    UploadStaticBuffer(m_Buffers.lightBuffer, kLightBinding, scene.lights);

    if (m_Buffers.environmentBuffer == 0) {
        glGenBuffers(1, &m_Buffers.environmentBuffer);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_Buffers.environmentBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GpuEnvironment), &scene.environment, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kEnvironmentBinding, m_Buffers.environmentBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    m_UploadedSceneHash = scene.uploadHash;
    return true;
}

bool PathTraceRenderer::DispatchSample(
    RenderSurfaceClass surfaceClass,
    const CompiledPathTraceScene& scene,
    const RenderScene& runtimeScene,
    const RenderFoundation::Settings& settings,
    const RenderContracts::AccumulationManager& accumulationManager,
    int width,
    int height,
    std::string& errorMessage) {

    errorMessage.clear();
    if (!UploadScene(scene, errorMessage)) {
        return false;
    }
    if (!UploadMaterialTextures(runtimeScene, errorMessage)) {
        return false;
    }

    const unsigned int pixelCount = static_cast<unsigned int>(width * height);
    const unsigned int groups = (pixelCount + kWorkgroupSize - 1u) / kWorkgroupSize;
    const int slotIndex = accumulationManager.GetRenderSlotIndex();
    const int sampleIndex = accumulationManager.GetNextSampleIndex(settings);

    UploadQueueCounts(m_Buffers.queueCountBuffer, pixelCount, 0u, 0u, pixelCount);
    if (sampleIndex == 0) {
        ClearDisplayTexture(m_Buffers.framebuffers[slotIndex], width, height);
    }

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialBinding, m_Buffers.materialBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialLayerBinding, m_Buffers.materialLayerBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kSphereBinding, m_Buffers.sphereBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kTriangleBinding, m_Buffers.triangleBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kPrimitiveRefBinding, m_Buffers.primitiveRefBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kBvhBinding, m_Buffers.bvhBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kLightBinding, m_Buffers.lightBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kEnvironmentBinding, m_Buffers.environmentBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kRayStateBinding, m_Buffers.rayStateBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kHitStateBinding, m_Buffers.hitStateBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kActiveQueueBinding, m_Buffers.activeQueueBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kNextQueueBinding, m_Buffers.nextQueueBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kShadowQueueBinding, m_Buffers.shadowQueueBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kQueueCountBinding, m_Buffers.queueCountBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kDebugLogBinding, m_Buffers.debugLogBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kGuideStateBinding, m_Buffers.guideStateBuffer);

    if (m_Buffers.debugLogBuffer != 0) {
        const GpuDebugReadback emptyDebugLog {};
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_Buffers.debugLogBuffer);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GpuDebugReadback), &emptyDebugLog);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    glUseProgram(m_GenerateProgram);
    glUniform2i(glGetUniformLocation(m_GenerateProgram, "uResolution"), width, height);
    glUniform1i(glGetUniformLocation(m_GenerateProgram, "uPixelCount"), static_cast<int>(pixelCount));
    glUniform1i(glGetUniformLocation(m_GenerateProgram, "uSampleIndex"), sampleIndex);
    glUniform1i(glGetUniformLocation(m_GenerateProgram, "uEpoch"), static_cast<int>(accumulationManager.GetTransportEpoch() & 0x7fffffff));
    glUniform3f(glGetUniformLocation(m_GenerateProgram, "uCameraPosition"), scene.camera.position.x, scene.camera.position.y, scene.camera.position.z);
    glUniform1f(glGetUniformLocation(m_GenerateProgram, "uYawDegrees"), scene.camera.yawDegrees);
    glUniform1f(glGetUniformLocation(m_GenerateProgram, "uPitchDegrees"), scene.camera.pitchDegrees);
    glUniform1f(glGetUniformLocation(m_GenerateProgram, "uFieldOfViewDegrees"), scene.camera.fieldOfViewDegrees);
    glUniform1f(glGetUniformLocation(m_GenerateProgram, "uFocusDistance"), scene.camera.focusDistance);
    glUniform1f(glGetUniformLocation(m_GenerateProgram, "uApertureRadius"), scene.camera.apertureRadius);
    SetPathTraceDebugUniforms(m_GenerateProgram, settings, width, height);
    glDispatchCompute(groups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    GLuint activeQueueBuffer = m_Buffers.activeQueueBuffer;
    GLuint nextQueueBuffer = m_Buffers.nextQueueBuffer;
    unsigned int currentActiveCount = pixelCount;
    const bool useOptimization = settings.pathTraceTerminationMode != RenderFoundation::PathTraceTerminationMode::BruteForce;
    const int effectiveMaxBounces = ResolveEffectiveMaxBounces(surfaceClass, settings, scene);

    for (int bounce = 0; bounce < effectiveMaxBounces; ++bounce) {
        if (useOptimization && currentActiveCount == 0) {
            break;
        }

        const unsigned int bounceGroups = useOptimization
            ? (currentActiveCount + kWorkgroupSize - 1u) / kWorkgroupSize
            : groups;

        if (useOptimization) {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kActiveQueueBinding, activeQueueBuffer);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kNextQueueBinding, nextQueueBuffer);
        }

        UploadQueueCounts(m_Buffers.queueCountBuffer, useOptimization ? currentActiveCount : pixelCount, 0u, 0u, pixelCount);

        glUseProgram(m_IntersectProgram);
        glUniform1i(glGetUniformLocation(m_IntersectProgram, "uPixelCount"), static_cast<int>(pixelCount));
        glUniform1i(glGetUniformLocation(m_IntersectProgram, "uSphereCount"), static_cast<int>(scene.spheres.size()));
        glUniform1i(glGetUniformLocation(m_IntersectProgram, "uTriangleCount"), static_cast<int>(scene.triangles.size()));
        glUniform1i(glGetUniformLocation(m_IntersectProgram, "uPrimitiveRefCount"), static_cast<int>(scene.primitiveRefs.size()));
        glUniform1i(glGetUniformLocation(m_IntersectProgram, "uBvhNodeCount"), static_cast<int>(scene.bvhNodes.size()));
        glDispatchCompute(bounceGroups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        glUseProgram(m_ShadeProgram);
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uPixelCount"), static_cast<int>(pixelCount));
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uBounceIndex"), bounce);
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uMaxBounces"), effectiveMaxBounces);
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uTerminationMode"), static_cast<int>(settings.pathTraceTerminationMode));
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uSampleIndex"), sampleIndex);
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uEpoch"), static_cast<int>(accumulationManager.GetTransportEpoch() & 0x7fffffff));
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uLightCount"), static_cast<int>(scene.lights.size()));
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uTransportMode"), static_cast<int>(settings.pathTraceTransportMode));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_BaseColorArray);
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uBaseColorTextures"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_MaterialParamsArray);
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uMaterialParamTextures"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_EmissiveArray);
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uEmissiveTextures"), 2);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_NormalArray);
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uNormalTextures"), 3);
        SetPathTraceDebugUniforms(m_ShadeProgram, settings, width, height);
        glDispatchCompute(bounceGroups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        if (useOptimization) {
            QueueCounts qCounts {};
            ReadbackQueueCounts(m_Buffers.queueCountBuffer, qCounts);
            const unsigned int nextActiveCount = qCounts.counts[1];
            const unsigned int shadowCount = qCounts.counts[2];

            if (shadowCount > 0) {
                const unsigned int shadowGroups = (shadowCount + kWorkgroupSize - 1u) / kWorkgroupSize;
                glUseProgram(m_ShadowProgram);
                glUniform1i(glGetUniformLocation(m_ShadowProgram, "uPixelCount"), static_cast<int>(pixelCount));
                glUniform1i(glGetUniformLocation(m_ShadowProgram, "uSphereCount"), static_cast<int>(scene.spheres.size()));
                glUniform1i(glGetUniformLocation(m_ShadowProgram, "uTriangleCount"), static_cast<int>(scene.triangles.size()));
                glUniform1i(glGetUniformLocation(m_ShadowProgram, "uPrimitiveRefCount"), static_cast<int>(scene.primitiveRefs.size()));
                glUniform1i(glGetUniformLocation(m_ShadowProgram, "uBvhNodeCount"), static_cast<int>(scene.bvhNodes.size()));
                glDispatchCompute(shadowGroups, 1, 1);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            }

            currentActiveCount = nextActiveCount;
            std::swap(activeQueueBuffer, nextQueueBuffer);
        } else {
            glUseProgram(m_ShadowProgram);
            glUniform1i(glGetUniformLocation(m_ShadowProgram, "uPixelCount"), static_cast<int>(pixelCount));
            glUniform1i(glGetUniformLocation(m_ShadowProgram, "uSphereCount"), static_cast<int>(scene.spheres.size()));
            glUniform1i(glGetUniformLocation(m_ShadowProgram, "uTriangleCount"), static_cast<int>(scene.triangles.size()));
            glUniform1i(glGetUniformLocation(m_ShadowProgram, "uPrimitiveRefCount"), static_cast<int>(scene.primitiveRefs.size()));
            glUniform1i(glGetUniformLocation(m_ShadowProgram, "uBvhNodeCount"), static_cast<int>(scene.bvhNodes.size()));
            glDispatchCompute(bounceGroups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
    }

    glUseProgram(m_AccumulateProgram);
    glUniform2i(glGetUniformLocation(m_AccumulateProgram, "uResolution"), width, height);
    glUniform1i(glGetUniformLocation(m_AccumulateProgram, "uPixelCount"), static_cast<int>(pixelCount));
    glUniform1i(glGetUniformLocation(m_AccumulateProgram, "uSampleIndex"), sampleIndex);
    glUniform1i(glGetUniformLocation(m_AccumulateProgram, "uAccumulationEnabled"), settings.accumulationEnabled ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_AccumulateProgram, "uFireflyClampEnabled"), settings.denoiser.fireflyClampEnabled ? 1 : 0);
    glUniform1f(glGetUniformLocation(m_AccumulateProgram, "uFireflyClampThreshold"), settings.denoiser.fireflyClampThreshold);
    SetPathTraceDebugUniforms(m_AccumulateProgram, settings, width, height);
    glBindImageTexture(0, m_Buffers.hdrAccumTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
    glBindImageTexture(1, m_Buffers.currentSampleTextures[slotIndex], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glBindImageTexture(2, m_Buffers.guideAlbedoAccumTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
    glBindImageTexture(3, m_Buffers.guideNormalAccumTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
    glBindImageTexture(4, m_Buffers.guideDepthAccumTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
    glBindImageTexture(5, m_Buffers.varianceAccumTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
    glDispatchCompute(groups, 1, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(0);

    if (m_Buffers.completionFences[slotIndex] != nullptr) {
        glDeleteSync(m_Buffers.completionFences[slotIndex]);
        m_Buffers.completionFences[slotIndex] = nullptr;
    }
    m_Buffers.completionFences[slotIndex] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    m_SubmissionInFlight = true;
    m_InFlightSlotIndex = slotIndex;
    m_InFlightWidth = width;
    m_InFlightHeight = height;
    return true;
}

bool PathTraceRenderer::PrepareDisplayForSlot(
    const CompiledPathTraceScene& scene,
    const RenderFoundation::Settings& settings,
    int width,
    int height,
    int slotIndex,
    std::string& errorMessage) {

    errorMessage.clear();
    if (slotIndex < 0 || slotIndex > 1) {
        errorMessage = "Path-trace display preparation requested for an invalid slot.";
        return false;
    }
    if (width <= 0 || height <= 0) {
        errorMessage = "Path-trace display preparation requested with an invalid size.";
        return false;
    }

    const unsigned int pixelCount = static_cast<unsigned int>(width * height);
    const unsigned int groups1D = (pixelCount + kWorkgroupSize - 1u) / kWorkgroupSize;
    const auto groups2D = ResolveImageGroups2D(width, height);

    glUseProgram(m_ResolveProgram);
    glUniform2i(glGetUniformLocation(m_ResolveProgram, "uResolution"), width, height);
    glUniform1i(glGetUniformLocation(m_ResolveProgram, "uPixelCount"), static_cast<int>(pixelCount));
    glBindImageTexture(0, m_Buffers.hdrAccumTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, m_Buffers.guideAlbedoAccumTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(2, m_Buffers.guideNormalAccumTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(3, m_Buffers.guideDepthAccumTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(4, m_Buffers.resolvedBeautyTextures[slotIndex], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glBindImageTexture(5, m_Buffers.resolvedAlbedoTextures[slotIndex], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glBindImageTexture(6, m_Buffers.resolvedNormalTextures[slotIndex], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glBindImageTexture(7, m_Buffers.resolvedDepthTextures[slotIndex], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glDispatchCompute(groups1D, 1, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    glUseProgram(m_ResolveVarianceProgram);
    glUniform2i(glGetUniformLocation(m_ResolveVarianceProgram, "uResolution"), width, height);
    glUniform1i(glGetUniformLocation(m_ResolveVarianceProgram, "uPixelCount"), static_cast<int>(pixelCount));
    glBindImageTexture(0, m_Buffers.varianceAccumTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, m_Buffers.resolvedVarianceTextures[slotIndex], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glDispatchCompute(groups1D, 1, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    if (DenoiserEnabled(settings)) {
        glUseProgram(m_BilateralProgram);
        glUniform2i(glGetUniformLocation(m_BilateralProgram, "uResolution"), width, height);
        glUniform1i(glGetUniformLocation(m_BilateralProgram, "uPixelCount"), static_cast<int>(pixelCount));
        glUniform1i(glGetUniformLocation(m_BilateralProgram, "uBilateralRadius"), settings.denoiser.bilateralRadius);
        glUniform1f(glGetUniformLocation(m_BilateralProgram, "uSpatialSigma"), settings.denoiser.bilateralSpatialSigma);
        glUniform1f(glGetUniformLocation(m_BilateralProgram, "uColorSigma"), settings.denoiser.bilateralColorSigma);
        glUniform1f(glGetUniformLocation(m_BilateralProgram, "uDepthPhi"), settings.denoiser.bilateralDepthPhi);
        glUniform1f(glGetUniformLocation(m_BilateralProgram, "uNormalPhi"), settings.denoiser.bilateralNormalPhi);
        glUniform1f(glGetUniformLocation(m_BilateralProgram, "uAlbedoPhi"), settings.denoiser.bilateralAlbedoPhi);
        glUniform1i(glGetUniformLocation(m_BilateralProgram, "uVarianceEnabled"), settings.denoiser.varianceEnabled ? 1 : 0);
        glUniform1f(glGetUniformLocation(m_BilateralProgram, "uVarianceStrength"), settings.denoiser.varianceStrength);
        glBindImageTexture(0, m_Buffers.resolvedBeautyTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(1, m_Buffers.resolvedAlbedoTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(2, m_Buffers.resolvedNormalTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(3, m_Buffers.resolvedDepthTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(4, m_Buffers.resolvedVarianceTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(5, m_Buffers.denoisePingTextures[slotIndex], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        glDispatchCompute(groups2D[0], groups2D[1], 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        if (settings.denoiser.mode == RenderFoundation::DenoiserMode::ATrous) {
            GLuint sourceTexture = m_Buffers.denoisePingTextures[slotIndex];
            GLuint destinationTexture = m_Buffers.denoisePongTextures[slotIndex];
            const int passCount = std::clamp(settings.denoiser.atrousPassCount, 1, 5);
            for (int passIndex = 0; passIndex < passCount; ++passIndex) {
                glUseProgram(m_ATrousProgram);
                glUniform2i(glGetUniformLocation(m_ATrousProgram, "uResolution"), width, height);
                glUniform1i(glGetUniformLocation(m_ATrousProgram, "uPixelCount"), static_cast<int>(pixelCount));
                glUniform1i(glGetUniformLocation(m_ATrousProgram, "uStepWidth"), 1 << passIndex);
                glUniform1f(glGetUniformLocation(m_ATrousProgram, "uDepthPhi"), settings.denoiser.atrousDepthPhi);
                glUniform1f(glGetUniformLocation(m_ATrousProgram, "uNormalPhi"), settings.denoiser.atrousNormalPhi);
                glUniform1f(glGetUniformLocation(m_ATrousProgram, "uAlbedoPhi"), settings.denoiser.atrousAlbedoPhi);
                glUniform1i(glGetUniformLocation(m_ATrousProgram, "uVarianceEnabled"), settings.denoiser.varianceEnabled ? 1 : 0);
                glUniform1f(glGetUniformLocation(m_ATrousProgram, "uVarianceStrength"), settings.denoiser.varianceStrength);
                glBindImageTexture(0, sourceTexture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
                glBindImageTexture(1, m_Buffers.resolvedAlbedoTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
                glBindImageTexture(2, m_Buffers.resolvedNormalTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
                glBindImageTexture(3, m_Buffers.resolvedDepthTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
                glBindImageTexture(4, m_Buffers.resolvedVarianceTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
                glBindImageTexture(5, destinationTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
                glDispatchCompute(groups2D[0], groups2D[1], 1);
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
                std::swap(sourceTexture, destinationTexture);
            }
        }
    }

    glUseProgram(m_PresentProgram);
    glUniform2i(glGetUniformLocation(m_PresentProgram, "uResolution"), width, height);
    glUniform1i(glGetUniformLocation(m_PresentProgram, "uPixelCount"), static_cast<int>(pixelCount));
    glUniform1f(glGetUniformLocation(m_PresentProgram, "uExposure"), scene.camera.exposure);
    glUniform1f(glGetUniformLocation(m_PresentProgram, "uWhiteBalanceTemperature"), scene.camera.whiteBalanceTemperature);
    glUniform1i(glGetUniformLocation(m_PresentProgram, "uDenoiserEnabled"), DenoiserEnabled(settings) ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_PresentProgram, "uDenoiserDebugView"), static_cast<int>(settings.denoiser.debugView));
    SetPathTraceDebugUniforms(m_PresentProgram, settings, width, height);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kRayStateBinding, m_Buffers.rayStateBuffer);
    glBindImageTexture(0, m_Buffers.resolvedBeautyTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, m_Buffers.currentSampleTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(2, m_Buffers.resolvedAlbedoTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(3, m_Buffers.resolvedNormalTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(4, m_Buffers.resolvedDepthTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(5, m_Buffers.resolvedVarianceTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(6, ResolveDenoisedTextureId(slotIndex, settings), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(7, m_Buffers.displayTextures[slotIndex], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glDispatchCompute(groups2D[0], groups2D[1], 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    glUseProgram(0);
    return true;
}

void PathTraceRenderer::ReadDebugLog(
    const RenderFoundation::Settings& settings,
    int width,
    int height) {

    m_DebugReadback = {};
    if (ToPathTraceDebugMode(settings) != RenderFoundation::PathTraceDebugMode::SelectedRayLog ||
        m_Buffers.debugLogBuffer == 0) {
        return;
    }

    GpuDebugReadback gpuReadback {};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_Buffers.debugLogBuffer);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GpuDebugReadback), &gpuReadback);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    const int bounceCount = std::clamp(gpuReadback.header.meta[0], 0, kPathTraceDebugMaxBounces);
    const auto debugPixel = ResolveDebugPixel(settings, width, height);
    m_DebugReadback.valid = bounceCount > 0;
    m_DebugReadback.pixelX = debugPixel[0];
    m_DebugReadback.pixelY = debugPixel[1];
    m_DebugReadback.bounceCount = bounceCount;
    for (int index = 0; index < bounceCount; ++index) {
        const GpuDebugBounce& source = gpuReadback.bounces[index];
        PathTraceDebugBounce& target = m_DebugReadback.bounces[static_cast<std::size_t>(index)];
        target.hitT = source.hitInfo[0];
        target.objectId = static_cast<int>(source.hitInfo[1]);
        target.materialIndex = static_cast<int>(source.hitInfo[2]);
        target.frontFace = source.hitInfo[3] > 0.5f;
        target.insideMedium = source.geometricNormal[3] > 0.5f;
        target.geometricNormal = ToFoundationVec3(source.geometricNormal);
        target.shadingNormal = ToFoundationVec3(source.shadingNormal);
        target.etaI = source.etaInfo[0];
        target.etaT = source.etaInfo[1];
        target.etaRatio = source.etaInfo[2];
        target.fresnel = source.etaInfo[3];
        target.decision = static_cast<int>(source.spawnedOrigin[3]);
        target.mediumInfo[0] = source.mediumInfo[0];
        target.mediumInfo[1] = source.mediumInfo[1];
        target.mediumInfo[2] = source.mediumInfo[2];
        target.mediumInfo[3] = source.mediumInfo[3];
        target.lightInfo[0] = source.lightInfo[0];
        target.lightInfo[1] = source.lightInfo[1];
        target.lightInfo[2] = source.lightInfo[2];
        target.lightInfo[3] = source.lightInfo[3];
        target.spawnedOrigin = ToFoundationVec3(source.spawnedOrigin);
        target.spawnedDirection = ToFoundationVec3(source.spawnedDirection);
    }
}

bool PathTraceRenderer::PollCompletedFence(
    const RenderFoundation::Settings& settings,
    RenderContracts::AccumulationManager& accumulationManager,
    std::string& errorMessage) {

    errorMessage.clear();
    if (!m_SubmissionInFlight || m_InFlightSlotIndex < 0 || m_InFlightSlotIndex > 1) {
        return true;
    }

    if (!accumulationManager.IsSubmissionInFlight()) {
        if (m_Buffers.completionFences[m_InFlightSlotIndex] != nullptr) {
            glDeleteSync(m_Buffers.completionFences[m_InFlightSlotIndex]);
            m_Buffers.completionFences[m_InFlightSlotIndex] = nullptr;
        }
        m_SubmissionInFlight = false;
        m_InFlightSlotIndex = -1;
        return true;
    }

    GLsync& fence = m_Buffers.completionFences[m_InFlightSlotIndex];
    if (fence == nullptr) {
        m_SubmissionInFlight = false;
        m_InFlightSlotIndex = -1;
        return true;
    }

    const GLenum status = glClientWaitSync(fence, 0, 0);
    if (status == GL_TIMEOUT_EXPIRED) {
        return true;
    }
    if (status == GL_WAIT_FAILED) {
        errorMessage = "Path-trace GPU fence polling failed.";
        return false;
    }

    glDeleteSync(fence);
    fence = nullptr;
    m_SubmissionInFlight = false;
    m_InFlightSlotIndex = -1;
    accumulationManager.MarkSampleCompleted(settings);
    ReadDebugLog(settings, m_InFlightWidth, m_InFlightHeight);
    return true;
}

bool PathTraceRenderer::RenderFrame(
    RenderSurfaceClass surfaceClass,
    const CompiledPathTraceScene& scene,
    const RenderScene& runtimeScene,
    const RenderFoundation::Settings& settings,
    RenderContracts::AccumulationManager& accumulationManager,
    int width,
    int height,
    bool allowNewSamples,
    unsigned int& outTextureId,
    std::string& errorMessage) {

    errorMessage.clear();
    outTextureId = 0;
    if (!scene.valid) {
        errorMessage = "The path-trace scene is not compiled.";
        return false;
    }

    if (!InitializePrograms(errorMessage)) {
        return false;
    }
    if (!EnsureDefaultTextureArrays(errorMessage)) {
        return false;
    }

    if (!PollCompletedFence(settings, accumulationManager, errorMessage)) {
        return false;
    }

    const int committedWidth = std::max(1, accumulationManager.GetCommittedViewportWidth() > 0 ? accumulationManager.GetCommittedViewportWidth() : width);
    const int committedHeight = std::max(1, accumulationManager.GetCommittedViewportHeight() > 0 ? accumulationManager.GetCommittedViewportHeight() : height);
    if (!EnsureBuffers(committedWidth, committedHeight, errorMessage)) {
        return false;
    }

    if (allowNewSamples && !accumulationManager.IsResizeInteractive() && accumulationManager.CanSubmitSample(settings)) {
        if (!DispatchSample(surfaceClass, scene, runtimeScene, settings, accumulationManager, committedWidth, committedHeight, errorMessage)) {
            return false;
        }
        accumulationManager.MarkSubmissionStarted();
    }

    const int visibleSlot = accumulationManager.GetVisibleSlotIndex();
    if (!PrepareDisplayForSlot(scene, settings, committedWidth, committedHeight, visibleSlot, errorMessage)) {
        return false;
    }

    outTextureId = m_Buffers.displayTextures[visibleSlot];
    return outTextureId != 0;
}

bool PathTraceRenderer::CaptureVisiblePixels(
    int width,
    int height,
    const RenderContracts::AccumulationManager& accumulationManager,
    std::vector<unsigned char>& outPixels,
    std::string& errorMessage) const {

    const int committedWidth = accumulationManager.GetCommittedViewportWidth() > 0
        ? accumulationManager.GetCommittedViewportWidth()
        : width;
    const int committedHeight = accumulationManager.GetCommittedViewportHeight() > 0
        ? accumulationManager.GetCommittedViewportHeight()
        : height;
    const int visibleSlot = accumulationManager.GetVisibleSlotIndex();
    return CaptureTexturePixels(
        m_Buffers.displayTextures[visibleSlot],
        committedWidth,
        committedHeight,
        outPixels,
        errorMessage);
}

bool PathTraceRenderer::CaptureLinearPixels(
    const RenderFoundation::Settings& settings,
    int width,
    int height,
    const RenderContracts::AccumulationManager& accumulationManager,
    LinearCaptureMode captureMode,
    std::vector<float>& outPixels,
    std::string& errorMessage) const {

    const int committedWidth = accumulationManager.GetCommittedViewportWidth() > 0
        ? accumulationManager.GetCommittedViewportWidth()
        : width;
    const int committedHeight = accumulationManager.GetCommittedViewportHeight() > 0
        ? accumulationManager.GetCommittedViewportHeight()
        : height;
    const int visibleSlot = accumulationManager.GetVisibleSlotIndex();
    const unsigned int textureId =
        captureMode == LinearCaptureMode::Denoised && DenoiserEnabled(settings)
            ? ResolveDenoisedTextureId(visibleSlot, settings)
            : m_Buffers.resolvedBeautyTextures[visibleSlot];
    return CaptureTexturePixels(textureId, committedWidth, committedHeight, outPixels, errorMessage);
}

bool PathTraceRenderer::CaptureTexturePixels(
    unsigned int textureId,
    int width,
    int height,
    std::vector<unsigned char>& outPixels,
    std::string& errorMessage) const {

    errorMessage.clear();
    outPixels.clear();
    if (textureId == 0 || width <= 0 || height <= 0) {
        errorMessage = "No visible path-trace texture is available for capture.";
        return false;
    }

    outPixels.resize(static_cast<std::size_t>(width * height * 4), 0);
    const unsigned int captureFbo = GLHelpers::CreateFBO(textureId);
    if (captureFbo == 0) {
        errorMessage = "Failed to create a path-trace capture framebuffer.";
        outPixels.clear();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, captureFbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, outPixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &captureFbo);

    const std::size_t stride = static_cast<std::size_t>(width * 4);
    std::vector<unsigned char> flipped(outPixels.size(), 0);
    for (int y = 0; y < height; ++y) {
        const std::size_t sourceOffset = static_cast<std::size_t>(y) * stride;
        const std::size_t targetOffset = static_cast<std::size_t>(height - 1 - y) * stride;
        std::copy_n(outPixels.data() + sourceOffset, stride, flipped.data() + targetOffset);
    }
    outPixels.swap(flipped);
    return true;
}

bool PathTraceRenderer::CaptureTexturePixels(
    unsigned int textureId,
    int width,
    int height,
    std::vector<float>& outPixels,
    std::string& errorMessage) const {

    errorMessage.clear();
    outPixels.clear();
    if (textureId == 0 || width <= 0 || height <= 0) {
        errorMessage = "No linear path-trace texture is available for capture.";
        return false;
    }

    outPixels.resize(static_cast<std::size_t>(width * height * 4), 0.0f);
    const unsigned int captureFbo = GLHelpers::CreateFBO(textureId);
    if (captureFbo == 0) {
        errorMessage = "Failed to create a framebuffer for linear path-trace capture.";
        outPixels.clear();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, captureFbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, outPixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &captureFbo);

    const std::size_t stride = static_cast<std::size_t>(width * 4);
    std::vector<float> flipped(outPixels.size(), 0.0f);
    for (int y = 0; y < height; ++y) {
        const std::size_t sourceOffset = static_cast<std::size_t>(y) * stride;
        const std::size_t targetOffset = static_cast<std::size_t>(height - 1 - y) * stride;
        std::copy_n(outPixels.data() + sourceOffset, stride, flipped.data() + targetOffset);
    }
    outPixels.swap(flipped);
    return true;
}

unsigned int PathTraceRenderer::ResolveDenoisedTextureId(int slotIndex, const RenderFoundation::Settings& settings) const {
    if (slotIndex < 0 || slotIndex > 1 || !DenoiserEnabled(settings)) {
        return m_Buffers.resolvedBeautyTextures[slotIndex < 0 ? 0 : slotIndex > 1 ? 1 : slotIndex];
    }

    if (settings.denoiser.mode == RenderFoundation::DenoiserMode::ATrous) {
        const int passCount = std::clamp(settings.denoiser.atrousPassCount, 1, 5);
        return (passCount % 2 == 1)
            ? m_Buffers.denoisePongTextures[slotIndex]
            : m_Buffers.denoisePingTextures[slotIndex];
    }

    return m_Buffers.denoisePingTextures[slotIndex];
}

void PathTraceRenderer::ReleaseSceneBuffers() {
    const std::array<GLuint*, 7> buffers = {
        &m_Buffers.materialBuffer,
        &m_Buffers.materialLayerBuffer,
        &m_Buffers.sphereBuffer,
        &m_Buffers.triangleBuffer,
        &m_Buffers.primitiveRefBuffer,
        &m_Buffers.bvhBuffer,
        &m_Buffers.lightBuffer
    };
    for (GLuint* buffer : buffers) {
        if (*buffer != 0) {
            glDeleteBuffers(1, buffer);
            *buffer = 0;
        }
    }
    if (m_Buffers.environmentBuffer != 0) {
        glDeleteBuffers(1, &m_Buffers.environmentBuffer);
        m_Buffers.environmentBuffer = 0;
    }
    ReleaseTextureArrays();
    m_UploadedSceneHash = 0;
}

void PathTraceRenderer::ReleaseTextures() {
    const std::array<GLuint*, 8> buffers = {
        &m_Buffers.rayStateBuffer,
        &m_Buffers.hitStateBuffer,
        &m_Buffers.activeQueueBuffer,
        &m_Buffers.nextQueueBuffer,
        &m_Buffers.shadowQueueBuffer,
        &m_Buffers.queueCountBuffer,
        &m_Buffers.debugLogBuffer,
        &m_Buffers.guideStateBuffer
    };
    for (GLuint* buffer : buffers) {
        if (*buffer != 0) {
            glDeleteBuffers(1, buffer);
            *buffer = 0;
        }
    }

    for (int slot = 0; slot < 2; ++slot) {
        if (m_Buffers.completionFences[slot] != nullptr) {
            glDeleteSync(m_Buffers.completionFences[slot]);
            m_Buffers.completionFences[slot] = nullptr;
        }
        if (m_Buffers.framebuffers[slot] != 0) {
            glDeleteFramebuffers(1, &m_Buffers.framebuffers[slot]);
            m_Buffers.framebuffers[slot] = 0;
        }

        const std::array<GLuint*, 14> textures = {
            &m_Buffers.hdrAccumTextures[slot],
            &m_Buffers.currentSampleTextures[slot],
            &m_Buffers.guideAlbedoAccumTextures[slot],
            &m_Buffers.guideNormalAccumTextures[slot],
            &m_Buffers.guideDepthAccumTextures[slot],
            &m_Buffers.varianceAccumTextures[slot],
            &m_Buffers.resolvedBeautyTextures[slot],
            &m_Buffers.resolvedAlbedoTextures[slot],
            &m_Buffers.resolvedNormalTextures[slot],
            &m_Buffers.resolvedDepthTextures[slot],
            &m_Buffers.resolvedVarianceTextures[slot],
            &m_Buffers.denoisePingTextures[slot],
            &m_Buffers.denoisePongTextures[slot],
            &m_Buffers.displayTextures[slot]
        };
        for (GLuint* texture : textures) {
            if (*texture != 0) {
                glDeleteTextures(1, texture);
                *texture = 0;
            }
        }
    }

    m_Buffers.width = 0;
    m_Buffers.height = 0;
    m_SubmissionInFlight = false;
    m_InFlightSlotIndex = -1;
    m_InFlightWidth = 0;
    m_InFlightHeight = 0;
    m_DebugReadback = {};
}

void PathTraceRenderer::ReleasePrograms() {
    const std::array<GLuint*, 10> programs = {
        &m_GenerateProgram,
        &m_IntersectProgram,
        &m_ShadeProgram,
        &m_ShadowProgram,
        &m_AccumulateProgram,
        &m_ResolveProgram,
        &m_ResolveVarianceProgram,
        &m_BilateralProgram,
        &m_ATrousProgram,
        &m_PresentProgram
    };
    for (GLuint* program : programs) {
        if (*program != 0) {
            glDeleteProgram(*program);
            *program = 0;
        }
    }
}

void PathTraceRenderer::ReleaseTextureArrays() {
    const std::array<GLuint*, 4> arrays = {
        &m_BaseColorArray,
        &m_MaterialParamsArray,
        &m_EmissiveArray,
        &m_NormalArray
    };
    for (GLuint* texture : arrays) {
        if (*texture != 0) {
            glDeleteTextures(1, texture);
            *texture = 0;
        }
    }
    m_UploadedTextureSceneRevision = 0;
}

void PathTraceRenderer::Shutdown() {
    ReleaseTextures();
    ReleaseSceneBuffers();
    ReleasePrograms();
    m_DebugReadback = {};
}

} // namespace RenderPathTrace
