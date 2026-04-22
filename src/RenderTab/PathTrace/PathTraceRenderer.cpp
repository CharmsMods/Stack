#include "RenderTab/PathTrace/PathTraceRenderer.h"

#include "RenderTab/Contracts/AccumulationManager.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <array>
#include <filesystem>
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
constexpr int kWorkgroupSize = 64;

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
    return GLHelpers::ReadFile(shaderPath.string());
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

void ClearDisplayTexture(GLuint framebuffer, int width, int height) {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
        m_AccumulateProgram != 0) {
        return true;
    }

    const std::string generateSource = BuildShaderSource("PathTraceGenerate.comp");
    const std::string intersectSource = BuildShaderSource("PathTraceIntersect.comp");
    const std::string shadeSource = BuildShaderSource("PathTraceShadeSurface.comp");
    const std::string shadowSource = BuildShaderSource("PathTraceShadow.comp");
    const std::string accumulateSource = BuildShaderSource("PathTraceAccumulate.comp");
    if (generateSource.empty() || intersectSource.empty() || shadeSource.empty() || shadowSource.empty() || accumulateSource.empty()) {
        errorMessage = "Failed to load one or more path-trace compute shaders.";
        return false;
    }

    m_GenerateProgram = GLHelpers::CreateComputeProgram(generateSource.c_str());
    m_IntersectProgram = GLHelpers::CreateComputeProgram(intersectSource.c_str());
    m_ShadeProgram = GLHelpers::CreateComputeProgram(shadeSource.c_str());
    m_ShadowProgram = GLHelpers::CreateComputeProgram(shadowSource.c_str());
    m_AccumulateProgram = GLHelpers::CreateComputeProgram(accumulateSource.c_str());
    if (m_GenerateProgram == 0 || m_IntersectProgram == 0 || m_ShadeProgram == 0 || m_ShadowProgram == 0 || m_AccumulateProgram == 0) {
        errorMessage = "Failed to compile the path-trace compute shaders.";
        ReleasePrograms();
        return false;
    }

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
        m_Buffers.hdrAccumTextures[1] != 0) {
        return true;
    }

    ReleaseTextures();
    m_Buffers.width = width;
    m_Buffers.height = height;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    for (int slot = 0; slot < 2; ++slot) {
        m_Buffers.hdrAccumTextures[slot] = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
        m_Buffers.displayTextures[slot] = GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F);
        m_Buffers.framebuffers[slot] = GLHelpers::CreateFBO(m_Buffers.displayTextures[slot]);
        if (m_Buffers.hdrAccumTextures[slot] == 0 || m_Buffers.displayTextures[slot] == 0 || m_Buffers.framebuffers[slot] == 0) {
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
    const CompiledPathTraceScene& scene,
    const RenderFoundation::Settings& settings,
    const RenderContracts::AccumulationManager& accumulationManager,
    int width,
    int height,
    std::string& errorMessage) {

    errorMessage.clear();
    if (!UploadScene(scene, errorMessage)) {
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

    for (int bounce = 0; bounce < settings.maxBounceCount; ++bounce) {
        glUseProgram(m_IntersectProgram);
        glUniform1i(glGetUniformLocation(m_IntersectProgram, "uPixelCount"), static_cast<int>(pixelCount));
        glUniform1i(glGetUniformLocation(m_IntersectProgram, "uSphereCount"), static_cast<int>(scene.spheres.size()));
        glUniform1i(glGetUniformLocation(m_IntersectProgram, "uTriangleCount"), static_cast<int>(scene.triangles.size()));
        glUniform1i(glGetUniformLocation(m_IntersectProgram, "uPrimitiveRefCount"), static_cast<int>(scene.primitiveRefs.size()));
        glUniform1i(glGetUniformLocation(m_IntersectProgram, "uBvhNodeCount"), static_cast<int>(scene.bvhNodes.size()));
        glDispatchCompute(groups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        UploadQueueCounts(m_Buffers.queueCountBuffer, pixelCount, 0u, 0u, pixelCount);

        glUseProgram(m_ShadeProgram);
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uPixelCount"), static_cast<int>(pixelCount));
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uBounceIndex"), bounce);
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uMaxBounces"), settings.maxBounceCount);
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uSampleIndex"), sampleIndex);
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uEpoch"), static_cast<int>(accumulationManager.GetTransportEpoch() & 0x7fffffff));
        glUniform1i(glGetUniformLocation(m_ShadeProgram, "uLightCount"), static_cast<int>(scene.lights.size()));
        SetPathTraceDebugUniforms(m_ShadeProgram, settings, width, height);
        glDispatchCompute(groups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        glUseProgram(m_ShadowProgram);
        glUniform1i(glGetUniformLocation(m_ShadowProgram, "uPixelCount"), static_cast<int>(pixelCount));
        glUniform1i(glGetUniformLocation(m_ShadowProgram, "uSphereCount"), static_cast<int>(scene.spheres.size()));
        glUniform1i(glGetUniformLocation(m_ShadowProgram, "uTriangleCount"), static_cast<int>(scene.triangles.size()));
        glUniform1i(glGetUniformLocation(m_ShadowProgram, "uPrimitiveRefCount"), static_cast<int>(scene.primitiveRefs.size()));
        glUniform1i(glGetUniformLocation(m_ShadowProgram, "uBvhNodeCount"), static_cast<int>(scene.bvhNodes.size()));
        glDispatchCompute(groups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    glUseProgram(m_AccumulateProgram);
    glUniform2i(glGetUniformLocation(m_AccumulateProgram, "uResolution"), width, height);
    glUniform1i(glGetUniformLocation(m_AccumulateProgram, "uPixelCount"), static_cast<int>(pixelCount));
    glUniform1i(glGetUniformLocation(m_AccumulateProgram, "uSampleIndex"), sampleIndex);
    glUniform1i(glGetUniformLocation(m_AccumulateProgram, "uAccumulationEnabled"), settings.accumulationEnabled ? 1 : 0);
    glUniform1f(glGetUniformLocation(m_AccumulateProgram, "uExposure"), scene.camera.exposure);
    SetPathTraceDebugUniforms(m_AccumulateProgram, settings, width, height);
    glBindImageTexture(0, m_Buffers.hdrAccumTextures[slotIndex], 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
    glBindImageTexture(1, m_Buffers.displayTextures[slotIndex], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
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
    const CompiledPathTraceScene& scene,
    const RenderFoundation::Settings& settings,
    RenderContracts::AccumulationManager& accumulationManager,
    int width,
    int height,
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

    if (!PollCompletedFence(settings, accumulationManager, errorMessage)) {
        return false;
    }

    const int committedWidth = std::max(1, accumulationManager.GetCommittedViewportWidth() > 0 ? accumulationManager.GetCommittedViewportWidth() : width);
    const int committedHeight = std::max(1, accumulationManager.GetCommittedViewportHeight() > 0 ? accumulationManager.GetCommittedViewportHeight() : height);
    if (!EnsureBuffers(committedWidth, committedHeight, errorMessage)) {
        return false;
    }

    if (!accumulationManager.IsResizeInteractive() && accumulationManager.CanSubmitSample(settings)) {
        if (!DispatchSample(scene, settings, accumulationManager, committedWidth, committedHeight, errorMessage)) {
            return false;
        }
        accumulationManager.MarkSubmissionStarted();
    }

    const int visibleSlot = accumulationManager.GetVisibleSlotIndex();
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
    m_UploadedSceneHash = 0;
}

void PathTraceRenderer::ReleaseTextures() {
    const std::array<GLuint*, 7> buffers = {
        &m_Buffers.rayStateBuffer,
        &m_Buffers.hitStateBuffer,
        &m_Buffers.activeQueueBuffer,
        &m_Buffers.nextQueueBuffer,
        &m_Buffers.shadowQueueBuffer,
        &m_Buffers.queueCountBuffer,
        &m_Buffers.debugLogBuffer
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
        if (m_Buffers.hdrAccumTextures[slot] != 0) {
            glDeleteTextures(1, &m_Buffers.hdrAccumTextures[slot]);
            m_Buffers.hdrAccumTextures[slot] = 0;
        }
        if (m_Buffers.displayTextures[slot] != 0) {
            glDeleteTextures(1, &m_Buffers.displayTextures[slot]);
            m_Buffers.displayTextures[slot] = 0;
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
    const std::array<GLuint*, 5> programs = {
        &m_GenerateProgram,
        &m_IntersectProgram,
        &m_ShadeProgram,
        &m_ShadowProgram,
        &m_AccumulateProgram
    };
    for (GLuint* program : programs) {
        if (*program != 0) {
            glDeleteProgram(*program);
            *program = 0;
        }
    }
}

void PathTraceRenderer::Shutdown() {
    ReleaseTextures();
    ReleaseSceneBuffers();
    ReleasePrograms();
    m_DebugReadback = {};
}

} // namespace RenderPathTrace
