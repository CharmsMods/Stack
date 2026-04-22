#include "ComputePreviewRenderer.h"

#include "RenderBuffers.h"
#include "RenderCamera.h"
#include "RenderScene.h"
#include "RenderSettings.h"
#include "RenderTab/Runtime/Bvh/RenderBvh.h"
#include "RenderTab/Runtime/Geometry/RenderSceneGeometry.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <vector>

namespace {

constexpr GLuint kSphereBindingIndex = 2;
constexpr GLuint kTriangleBindingIndex = 3;
constexpr GLuint kPrimitiveRefBindingIndex = 4;
constexpr GLuint kBvhBindingIndex = 5;
constexpr GLuint kLightBindingIndex = 6;
constexpr int kPreviewTextureSize = 256;

void StoreFloat3(float target[4], const RenderFloat3& value, float w = 0.0f) {
    target[0] = value.x;
    target[1] = value.y;
    target[2] = value.z;
    target[3] = w;
}

void StoreFloat2Pair(float target[4], const RenderFloat2& a, const RenderFloat2& b) {
    target[0] = a.x;
    target[1] = a.y;
    target[2] = b.x;
    target[3] = b.y;
}

std::vector<unsigned char> ResizeTexturePixels(const RenderImportedTexture& texture, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(kPreviewTextureSize * kPreviewTextureSize * 4), 0);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = a;
    }

    if (texture.width <= 0 || texture.height <= 0 || texture.pixels.empty()) {
        return pixels;
    }

    for (int y = 0; y < kPreviewTextureSize; ++y) {
        const int sourceY = std::clamp((y * texture.height) / kPreviewTextureSize, 0, texture.height - 1);
        for (int x = 0; x < kPreviewTextureSize; ++x) {
            const int sourceX = std::clamp((x * texture.width) / kPreviewTextureSize, 0, texture.width - 1);
            const std::size_t sourceIndex = static_cast<std::size_t>((sourceY * texture.width + sourceX) * 4);
            const std::size_t targetIndex = static_cast<std::size_t>((y * kPreviewTextureSize + x) * 4);
            pixels[targetIndex + 0] = texture.pixels[sourceIndex + 0];
            pixels[targetIndex + 1] = texture.pixels[sourceIndex + 1];
            pixels[targetIndex + 2] = texture.pixels[sourceIndex + 2];
            pixels[targetIndex + 3] = texture.pixels[sourceIndex + 3];
        }
    }

    return pixels;
}

struct alignas(16) GpuSphere {
    float centerRadius[4];
    float albedo[4];
    float emission[4];
    float surfaceParams[4];
    float dielectricParams[4];
    float mediumParams0[4];
    float mediumParams1[4];
    int textureRefs[4];
};

struct alignas(16) GpuTriangle {
    float a[4];
    float b[4];
    float c[4];
    float normalA[4];
    float normalB[4];
    float normalC[4];
    float uvAuvB[4];
    float uvC[4];
    float albedo[4];
    float emission[4];
    float surfaceParams[4];
    float dielectricParams[4];
    float mediumParams0[4];
    float mediumParams1[4];
    int textureRefs[4];
};

struct alignas(16) GpuPrimitiveRef {
    int meta[4];
};

struct alignas(16) GpuBvhNode {
    float minBounds[4];
    float maxBounds[4];
    int meta0[4];
    int meta1[4];
};

struct alignas(16) GpuLight {
    float positionType[4];
    float directionRange[4];
    float colorIntensity[4];
    float sizeAndAngles[4];
    float rightEnabled[4];
    float upData[4];
};

template <typename T>
void UploadBuffer(GLuint& buffer, GLuint binding, const std::vector<T>& data) {
    if (data.empty()) {
        if (buffer != 0) {
            glDeleteBuffers(1, &buffer);
            buffer = 0;
        }
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, 0);
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
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

std::string LoadComputeShaderSource() {
    const std::filesystem::path shaderPath =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "Shaders" / "ComputePreview.comp";
    return GLHelpers::ReadFile(shaderPath.string());
}

RenderResolvedMaterial ResolveSceneMaterial(const RenderScene& scene, int materialIndex, const RenderFloat3& tint) {
    if (scene.IsMaterialIndexValid(materialIndex)) {
        return ResolveMaterial(scene.GetMaterial(materialIndex), tint);
    }

    return RenderResolvedMaterial { tint, MakeRenderFloat3(0.0f, 0.0f, 0.0f) };
}

GpuSphere ToGpuSphere(const RenderScene& scene, const RenderSphere& sphere) {
    const RenderResolvedSphere resolved = ResolveSphere(sphere);
    const RenderResolvedMaterial material = ResolveSceneMaterial(scene, resolved.materialIndex, resolved.albedoTint);
    GpuSphere result {};
    StoreFloat3(result.centerRadius, resolved.center, resolved.radius);
    StoreFloat3(result.albedo, material.albedo, 1.0f);
    StoreFloat3(result.emission, material.emission, 1.0f);
    result.surfaceParams[0] = material.roughness;
    result.surfaceParams[1] = material.metallic;
    result.surfaceParams[2] = static_cast<float>(resolved.materialIndex);
    result.surfaceParams[3] = 0.0f;
    result.dielectricParams[0] = material.transmission;
    result.dielectricParams[1] = material.ior;
    result.dielectricParams[2] = 0.0f;
    result.dielectricParams[3] = 0.0f;
    StoreFloat3(result.mediumParams0, material.absorptionColor, material.absorptionDistance);
    result.mediumParams1[0] = material.transmissionRoughness;
    result.mediumParams1[1] = material.thinWalled ? 1.0f : 0.0f;
    result.mediumParams1[2] = static_cast<float>(scene.IsMaterialIndexValid(resolved.materialIndex)
        ? static_cast<int>(scene.GetMaterial(resolved.materialIndex).surfacePreset)
        : static_cast<int>(RenderSurfacePreset::Diffuse));
    result.mediumParams1[3] = 0.0f;
    result.textureRefs[0] = material.baseColorTexture.textureIndex;
    result.textureRefs[1] = material.metallicRoughnessTexture.textureIndex;
    result.textureRefs[2] = material.emissiveTexture.textureIndex;
    result.textureRefs[3] = material.normalTexture.textureIndex;
    return result;
}

GpuTriangle ToGpuTriangle(const RenderScene& scene, const RenderResolvedTriangle& triangle) {
    const RenderResolvedMaterial material = ResolveSceneMaterial(scene, triangle.materialIndex, triangle.albedoTint);
    GpuTriangle result {};
    StoreFloat3(result.a, triangle.a, 1.0f);
    StoreFloat3(result.b, triangle.b, 1.0f);
    StoreFloat3(result.c, triangle.c, 1.0f);
    StoreFloat3(result.normalA, triangle.normalA, 0.0f);
    StoreFloat3(result.normalB, triangle.normalB, 0.0f);
    StoreFloat3(result.normalC, triangle.normalC, 0.0f);
    StoreFloat2Pair(result.uvAuvB, triangle.uvA, triangle.uvB);
    result.uvC[0] = triangle.uvC.x;
    result.uvC[1] = triangle.uvC.y;
    result.uvC[2] = 0.0f;
    result.uvC[3] = 0.0f;
    StoreFloat3(result.albedo, material.albedo, 1.0f);
    StoreFloat3(result.emission, material.emission, 1.0f);
    result.surfaceParams[0] = material.roughness;
    result.surfaceParams[1] = material.metallic;
    result.surfaceParams[2] = static_cast<float>(triangle.materialIndex);
    result.surfaceParams[3] = 0.0f;
    result.dielectricParams[0] = material.transmission;
    result.dielectricParams[1] = material.ior;
    result.dielectricParams[2] = 0.0f;
    result.dielectricParams[3] = 0.0f;
    StoreFloat3(result.mediumParams0, material.absorptionColor, material.absorptionDistance);
    result.mediumParams1[0] = material.transmissionRoughness;
    result.mediumParams1[1] = material.thinWalled ? 1.0f : 0.0f;
    result.mediumParams1[2] = static_cast<float>(scene.IsMaterialIndexValid(triangle.materialIndex)
        ? static_cast<int>(scene.GetMaterial(triangle.materialIndex).surfacePreset)
        : static_cast<int>(RenderSurfacePreset::Diffuse));
    result.mediumParams1[3] = 0.0f;
    result.textureRefs[0] = material.baseColorTexture.textureIndex;
    result.textureRefs[1] = material.metallicRoughnessTexture.textureIndex;
    result.textureRefs[2] = material.emissiveTexture.textureIndex;
    result.textureRefs[3] = material.normalTexture.textureIndex;
    return result;
}

GpuPrimitiveRef ToGpuPrimitiveRef(const RenderPrimitiveRef& primitiveRef) {
    GpuPrimitiveRef result {};
    result.meta[0] = primitiveRef.type == RenderPrimitiveType::Sphere ? 0 : 1;
    result.meta[1] = primitiveRef.index;
    result.meta[2] = 0;
    result.meta[3] = 0;
    return result;
}

GpuBvhNode ToGpuBvhNode(const RenderBvhNode& node) {
    GpuBvhNode result {};
    result.minBounds[0] = node.bounds.min.x;
    result.minBounds[1] = node.bounds.min.y;
    result.minBounds[2] = node.bounds.min.z;
    result.minBounds[3] = 0.0f;
    result.maxBounds[0] = node.bounds.max.x;
    result.maxBounds[1] = node.bounds.max.y;
    result.maxBounds[2] = node.bounds.max.z;
    result.maxBounds[3] = 0.0f;
    result.meta0[0] = node.leftChild;
    result.meta0[1] = node.rightChild;
    result.meta0[2] = node.firstPrimitive;
    result.meta0[3] = node.primitiveCount;
    result.meta1[0] = node.depth;
    result.meta1[1] = 0;
    result.meta1[2] = 0;
    result.meta1[3] = 0;
    return result;
}

GpuLight ToGpuLight(const RenderLight& light) {
    GpuLight result {};
    const RenderFloat3 direction = GetRenderLightDirection(light);
    const RenderFloat3 right = GetRenderLightRight(light);
    const RenderFloat3 up = GetRenderLightUp(light);
    StoreFloat3(result.positionType, light.transform.translation, static_cast<float>(light.type));
    StoreFloat3(result.directionRange, direction, light.range);
    StoreFloat3(result.colorIntensity, light.color, light.intensity);
    result.sizeAndAngles[0] = light.areaSize.x;
    result.sizeAndAngles[1] = light.areaSize.y;
    result.sizeAndAngles[2] = light.innerConeDegrees;
    result.sizeAndAngles[3] = light.outerConeDegrees;
    StoreFloat3(result.rightEnabled, right, light.enabled ? 1.0f : 0.0f);
    StoreFloat3(result.upData, up, 0.0f);
    return result;
}

} // namespace

ComputePreviewRenderer::~ComputePreviewRenderer() {
    Shutdown();
}

bool ComputePreviewRenderer::Initialize() {
    if (m_Program != 0) {
        return true;
    }

    const std::string shaderSource = LoadComputeShaderSource();
    if (shaderSource.empty()) {
        m_LastError = "Failed to load the compute preview shader source.";
        return false;
    }

    m_Program = GLHelpers::CreateComputeProgram(shaderSource.c_str());
    if (m_Program == 0) {
        m_LastError = "Failed to compile the compute preview shader.";
        return false;
    }

    m_LastError.clear();
    return true;
}

void ComputePreviewRenderer::Shutdown() {
    ReleaseSceneBuffers();
    ReleaseTextureArrays();

    if (m_Program != 0) {
        glDeleteProgram(m_Program);
        m_Program = 0;
    }

    m_UploadedSceneRevision = 0;
    m_DispatchGroupsX = 0;
    m_DispatchGroupsY = 0;
    m_UploadedSphereCount = 0;
    m_UploadedTriangleCount = 0;
    m_UploadedPrimitiveCount = 0;
    m_UploadedBvhNodeCount = 0;
    m_UploadedLightCount = 0;
    m_UploadedTextureCount = 0;
    m_LastError.clear();
}

bool ComputePreviewRenderer::UploadSceneBuffers(const RenderScene& scene) {
    if (m_UploadedSceneRevision == scene.GetRevision()) {
        return true;
    }

    std::vector<GpuSphere> gpuSpheres;
    gpuSpheres.reserve(static_cast<std::size_t>(scene.GetSphereCount()));
    for (int i = 0; i < scene.GetSphereCount(); ++i) {
        gpuSpheres.push_back(ToGpuSphere(scene, scene.GetSphere(i)));
    }

    std::vector<GpuTriangle> gpuTriangles;
    gpuTriangles.reserve(static_cast<std::size_t>(scene.GetResolvedTriangleCount()));
    for (int i = 0; i < scene.GetResolvedTriangleCount(); ++i) {
        gpuTriangles.push_back(ToGpuTriangle(scene, scene.GetResolvedTriangle(i)));
    }

    std::vector<GpuPrimitiveRef> gpuPrimitiveRefs;
    gpuPrimitiveRefs.reserve(scene.GetPrimitiveRefs().size());
    for (const RenderPrimitiveRef& primitiveRef : scene.GetPrimitiveRefs()) {
        gpuPrimitiveRefs.push_back(ToGpuPrimitiveRef(primitiveRef));
    }

    std::vector<GpuBvhNode> gpuBvhNodes;
    gpuBvhNodes.reserve(scene.GetBvhNodes().size());
    for (const RenderBvhNode& node : scene.GetBvhNodes()) {
        gpuBvhNodes.push_back(ToGpuBvhNode(node));
    }

    std::vector<GpuLight> gpuLights;
    gpuLights.reserve(static_cast<std::size_t>(scene.GetLightCount()));
    for (int i = 0; i < scene.GetLightCount(); ++i) {
        gpuLights.push_back(ToGpuLight(scene.GetLight(i)));
    }

    UploadBuffer(m_SphereBuffer, kSphereBindingIndex, gpuSpheres);
    UploadBuffer(m_TriangleBuffer, kTriangleBindingIndex, gpuTriangles);
    UploadBuffer(m_PrimitiveRefBuffer, kPrimitiveRefBindingIndex, gpuPrimitiveRefs);
    UploadBuffer(m_BvhBuffer, kBvhBindingIndex, gpuBvhNodes);
    UploadBuffer(m_LightBuffer, kLightBindingIndex, gpuLights);

    ReleaseTextureArrays();
    const int layerCount = std::max(1, scene.GetImportedTextureCount() + 1);
    m_BaseColorArray = GLHelpers::CreateTextureArray(kPreviewTextureSize, kPreviewTextureSize, layerCount, GL_RGBA8);
    m_MaterialParamsArray = GLHelpers::CreateTextureArray(kPreviewTextureSize, kPreviewTextureSize, layerCount, GL_RGBA8);
    m_EmissiveArray = GLHelpers::CreateTextureArray(kPreviewTextureSize, kPreviewTextureSize, layerCount, GL_RGBA8);
    if (m_BaseColorArray == 0 || m_MaterialParamsArray == 0 || m_EmissiveArray == 0) {
        m_LastError = "Failed to allocate compute material texture arrays.";
        return false;
    }

    std::vector<unsigned char> defaultWhite(static_cast<std::size_t>(kPreviewTextureSize * kPreviewTextureSize * 4), 255);
    for (int layer = 0; layer < layerCount; ++layer) {
        GLHelpers::UploadTextureArrayLayer(m_BaseColorArray, layer, kPreviewTextureSize, kPreviewTextureSize, GL_RGBA, GL_UNSIGNED_BYTE, defaultWhite.data());
        GLHelpers::UploadTextureArrayLayer(m_MaterialParamsArray, layer, kPreviewTextureSize, kPreviewTextureSize, GL_RGBA, GL_UNSIGNED_BYTE, defaultWhite.data());
        GLHelpers::UploadTextureArrayLayer(m_EmissiveArray, layer, kPreviewTextureSize, kPreviewTextureSize, GL_RGBA, GL_UNSIGNED_BYTE, defaultWhite.data());
    }

    for (int textureIndex = 0; textureIndex < scene.GetImportedTextureCount(); ++textureIndex) {
        const RenderImportedTexture& texture = scene.GetImportedTexture(textureIndex);
        const int layer = textureIndex + 1;
        const std::vector<unsigned char> resizedPixels = ResizeTexturePixels(texture, 255, 255, 255, 255);
        unsigned int targetTexture = 0;
        switch (texture.semantic) {
        case RenderTextureSemantic::BaseColor:
            targetTexture = m_BaseColorArray;
            break;
        case RenderTextureSemantic::MetallicRoughness:
            targetTexture = m_MaterialParamsArray;
            break;
        case RenderTextureSemantic::Emissive:
            targetTexture = m_EmissiveArray;
            break;
        case RenderTextureSemantic::Normal:
            targetTexture = 0;
            break;
        }

        if (targetTexture != 0) {
            GLHelpers::UploadTextureArrayLayer(targetTexture, layer, kPreviewTextureSize, kPreviewTextureSize, GL_RGBA, GL_UNSIGNED_BYTE, resizedPixels.data());
        }
    }

    m_UploadedSceneRevision = scene.GetRevision();
    m_UploadedSphereCount = static_cast<int>(gpuSpheres.size());
    m_UploadedTriangleCount = static_cast<int>(gpuTriangles.size());
    m_UploadedPrimitiveCount = static_cast<int>(gpuPrimitiveRefs.size());
    m_UploadedBvhNodeCount = static_cast<int>(gpuBvhNodes.size());
    m_UploadedLightCount = static_cast<int>(gpuLights.size());
    m_UploadedTextureCount = scene.GetImportedTextureCount();
    return true;
}

void ComputePreviewRenderer::ReleaseSceneBuffers() {
    if (m_SphereBuffer != 0) {
        glDeleteBuffers(1, &m_SphereBuffer);
        m_SphereBuffer = 0;
    }
    if (m_TriangleBuffer != 0) {
        glDeleteBuffers(1, &m_TriangleBuffer);
        m_TriangleBuffer = 0;
    }
    if (m_PrimitiveRefBuffer != 0) {
        glDeleteBuffers(1, &m_PrimitiveRefBuffer);
        m_PrimitiveRefBuffer = 0;
    }
    if (m_BvhBuffer != 0) {
        glDeleteBuffers(1, &m_BvhBuffer);
        m_BvhBuffer = 0;
    }
    if (m_LightBuffer != 0) {
        glDeleteBuffers(1, &m_LightBuffer);
        m_LightBuffer = 0;
    }
}

void ComputePreviewRenderer::ReleaseTextureArrays() {
    if (m_BaseColorArray != 0) {
        glDeleteTextures(1, &m_BaseColorArray);
        m_BaseColorArray = 0;
    }
    if (m_MaterialParamsArray != 0) {
        glDeleteTextures(1, &m_MaterialParamsArray);
        m_MaterialParamsArray = 0;
    }
    if (m_EmissiveArray != 0) {
        glDeleteTextures(1, &m_EmissiveArray);
        m_EmissiveArray = 0;
    }
}

bool ComputePreviewRenderer::RenderPreview(const RenderScene& scene, const RenderCamera& camera, const RenderSettings& settings, RenderBuffers& buffers) {
    if (!Initialize()) {
        return false;
    }

    if (buffers.GetWidth() <= 0 || buffers.GetHeight() <= 0 ||
        buffers.GetAccumulationTexture() == 0 || buffers.GetDisplayTexture() == 0) {
        m_LastError = "Render buffers are not allocated.";
        return false;
    }

    if (!UploadSceneBuffers(scene)) {
        m_LastError = "Failed to upload scene buffers.";
        return false;
    }

    const bool accumulationEnabled =
        settings.GetIntegratorMode() == RenderIntegratorMode::PathTracePreview &&
        settings.IsAccumulationEnabled();
    const int sampleIndex = accumulationEnabled ? static_cast<int>(buffers.GetSampleCount()) : 0;
    m_DispatchGroupsX = (buffers.GetWidth() + 7) / 8;
    m_DispatchGroupsY = (buffers.GetHeight() + 7) / 8;

    glUseProgram(m_Program);
    glBindImageTexture(0, buffers.GetAccumulationTexture(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
    glBindImageTexture(1, buffers.GetDisplayTexture(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glBindImageTexture(2, buffers.GetMomentTexture(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kSphereBindingIndex, m_SphereBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kTriangleBindingIndex, m_TriangleBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kPrimitiveRefBindingIndex, m_PrimitiveRefBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kBvhBindingIndex, m_BvhBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kLightBindingIndex, m_LightBuffer);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_BaseColorArray);
    glUniform1i(glGetUniformLocation(m_Program, "uBaseColorTextures"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_MaterialParamsArray);
    glUniform1i(glGetUniformLocation(m_Program, "uMaterialParamTextures"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_EmissiveArray);
    glUniform1i(glGetUniformLocation(m_Program, "uEmissiveTextures"), 2);

    glUniform2i(glGetUniformLocation(m_Program, "uResolution"), buffers.GetWidth(), buffers.GetHeight());
    glUniform1i(glGetUniformLocation(m_Program, "uSampleIndex"), sampleIndex);
    glUniform1i(glGetUniformLocation(m_Program, "uPreviewSampleTarget"), settings.GetPreviewSampleTarget());
    glUniform1i(glGetUniformLocation(m_Program, "uAccumulationEnabled"), accumulationEnabled ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_Program, "uIntegratorMode"), static_cast<int>(settings.GetIntegratorMode()));
    glUniform1i(glGetUniformLocation(m_Program, "uMaxBounces"), settings.GetMaxBounceCount());
    glUniform1i(glGetUniformLocation(m_Program, "uDisplayMode"), static_cast<int>(settings.GetDisplayMode()));
    glUniform1i(glGetUniformLocation(m_Program, "uTonemapMode"), static_cast<int>(settings.GetTonemapMode()));
    glUniform1i(glGetUniformLocation(m_Program, "uBackgroundMode"), static_cast<int>(scene.GetBackgroundMode()));
    glUniform1i(glGetUniformLocation(m_Program, "uDebugViewMode"), static_cast<int>(settings.GetDebugViewMode()));
    glUniform1i(glGetUniformLocation(m_Program, "uEnvironmentEnabled"), scene.IsEnvironmentEnabled() ? 1 : 0);
    glUniform1f(glGetUniformLocation(m_Program, "uEnvironmentIntensity"), scene.GetEnvironmentIntensity());
    glUniform1i(glGetUniformLocation(m_Program, "uFogEnabled"), scene.IsFogEnabled() ? 1 : 0);
    glUniform3f(glGetUniformLocation(m_Program, "uFogColor"), scene.GetFogColor().x, scene.GetFogColor().y, scene.GetFogColor().z);
    glUniform1f(glGetUniformLocation(m_Program, "uFogDensity"), scene.GetFogDensity());
    glUniform1f(glGetUniformLocation(m_Program, "uFogAnisotropy"), scene.GetFogAnisotropy());
    glUniform1i(glGetUniformLocation(m_Program, "uUseBvhTraversal"), settings.IsBvhTraversalEnabled() ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_Program, "uSphereCount"), m_UploadedSphereCount);
    glUniform1i(glGetUniformLocation(m_Program, "uTriangleCount"), m_UploadedTriangleCount);
    glUniform1i(glGetUniformLocation(m_Program, "uPrimitiveRefCount"), m_UploadedPrimitiveCount);
    glUniform1i(glGetUniformLocation(m_Program, "uBvhNodeCount"), m_UploadedBvhNodeCount);
    glUniform1i(glGetUniformLocation(m_Program, "uLightCount"), m_UploadedLightCount);
    const RenderFloat3& cameraPosition = camera.GetPosition();
    glUniform3f(glGetUniformLocation(m_Program, "uCameraPosition"), cameraPosition.x, cameraPosition.y, cameraPosition.z);
    glUniform1f(glGetUniformLocation(m_Program, "uYawDegrees"), camera.GetYawDegrees());
    glUniform1f(glGetUniformLocation(m_Program, "uPitchDegrees"), camera.GetPitchDegrees());
    glUniform1f(glGetUniformLocation(m_Program, "uFieldOfViewDegrees"), camera.GetFieldOfViewDegrees());
    glUniform1f(glGetUniformLocation(m_Program, "uFocusDistance"), camera.GetFocusDistance());
    glUniform1f(glGetUniformLocation(m_Program, "uApertureRadius"), camera.GetApertureRadius());
    glUniform1f(glGetUniformLocation(m_Program, "uExposure"), camera.GetExposure());

    glDispatchCompute(static_cast<GLuint>(m_DispatchGroupsX), static_cast<GLuint>(m_DispatchGroupsY), 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(0);

    buffers.MarkSampleRendered(accumulationEnabled);
    m_LastError.clear();
    return true;
}
