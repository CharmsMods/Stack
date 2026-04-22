#include "RenderRasterPreviewRenderer.h"

#include "RenderCamera.h"
#include "RenderScene.h"
#include "RenderSettings.h"
#include "RenderTab/Runtime/Geometry/RenderMesh.h"
#include "RenderTab/Runtime/Geometry/RenderSceneGeometry.h"
#include "Renderer/GLHelpers.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <vector>

namespace {

constexpr int kSphereSlices = 24;
constexpr int kSphereStacks = 16;
constexpr int kPreviewTextureSize = 256;

struct RasterVertex {
    float position[3];
    float normal[3];
    float uv[2];
    float materialIndex = 0.0f;
    float tint[3];
    float objectId = 0.0f;
};

struct alignas(16) GpuRasterMaterial {
    float baseColor[4];
    float emissionColorStrength[4];
    float surfaceParams[4];
    float dielectricParams[4];
    float mediumParams0[4];
    float mediumParams1[4];
    int textureRefs[4];
};

struct alignas(16) GpuRasterLight {
    float positionType[4];
    float directionRange[4];
    float colorIntensity[4];
    float sizeAndAngles[4];
    float rightEnabled[4];
    float upData[4];
};

std::string LoadShaderSource(const char* fileName) {
    const std::filesystem::path shaderPath =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "Shaders" / fileName;
    return GLHelpers::ReadFile(shaderPath.string());
}

void StoreFloat3(float target[3], const RenderFloat3& value) {
    target[0] = value.x;
    target[1] = value.y;
    target[2] = value.z;
}

void StoreFloat2(float target[2], const RenderFloat2& value) {
    target[0] = value.x;
    target[1] = value.y;
}

void AppendTriangle(
    std::vector<RasterVertex>& vertices,
    const RenderFloat3& a,
    const RenderFloat3& b,
    const RenderFloat3& c,
    const RenderFloat3& normalA,
    const RenderFloat3& normalB,
    const RenderFloat3& normalC,
    const RenderFloat2& uvA,
    const RenderFloat2& uvB,
    const RenderFloat2& uvC,
    int materialIndex,
    const RenderFloat3& tint,
    int objectId) {
    RasterVertex va {};
    StoreFloat3(va.position, a);
    StoreFloat3(va.normal, normalA);
    StoreFloat2(va.uv, uvA);
    va.materialIndex = static_cast<float>(materialIndex);
    StoreFloat3(va.tint, tint);
    va.objectId = static_cast<float>(objectId);
    vertices.push_back(va);

    RasterVertex vb {};
    StoreFloat3(vb.position, b);
    StoreFloat3(vb.normal, normalB);
    StoreFloat2(vb.uv, uvB);
    vb.materialIndex = static_cast<float>(materialIndex);
    StoreFloat3(vb.tint, tint);
    vb.objectId = static_cast<float>(objectId);
    vertices.push_back(vb);

    RasterVertex vc {};
    StoreFloat3(vc.position, c);
    StoreFloat3(vc.normal, normalC);
    StoreFloat2(vc.uv, uvC);
    vc.materialIndex = static_cast<float>(materialIndex);
    StoreFloat3(vc.tint, tint);
    vc.objectId = static_cast<float>(objectId);
    vertices.push_back(vc);
}

void AppendResolvedTriangle(std::vector<RasterVertex>& vertices, const RenderResolvedTriangle& triangle) {
    const int objectId = triangle.meshInstanceId > 0 ? triangle.meshInstanceId : triangle.triangleId;
    AppendTriangle(
        vertices,
        triangle.a,
        triangle.b,
        triangle.c,
        triangle.normalA,
        triangle.normalB,
        triangle.normalC,
        triangle.uvA,
        triangle.uvB,
        triangle.uvC,
        triangle.materialIndex,
        triangle.albedoTint,
        objectId);
}

RenderFloat3 BuildSpherePoint(float radius, float theta, float phi) {
    const float sinPhi = std::sin(phi);
    return MakeRenderFloat3(
        radius * sinPhi * std::cos(theta),
        radius * std::cos(phi),
        radius * sinPhi * std::sin(theta));
}

RenderFloat2 BuildSphereUv(float u, float v) {
    return MakeRenderFloat2(u, 1.0f - v);
}

void AppendSphere(std::vector<RasterVertex>& vertices, const RenderResolvedSphere& sphere) {
    for (int stack = 0; stack < kSphereStacks; ++stack) {
        const float v0 = static_cast<float>(stack) / static_cast<float>(kSphereStacks);
        const float v1 = static_cast<float>(stack + 1) / static_cast<float>(kSphereStacks);
        const float phi0 = v0 * 3.14159265358979323846f;
        const float phi1 = v1 * 3.14159265358979323846f;

        for (int slice = 0; slice < kSphereSlices; ++slice) {
            const float u0 = static_cast<float>(slice) / static_cast<float>(kSphereSlices);
            const float u1 = static_cast<float>(slice + 1) / static_cast<float>(kSphereSlices);
            const float theta0 = u0 * 6.28318530717958647692f;
            const float theta1 = u1 * 6.28318530717958647692f;

            const RenderFloat3 localA = BuildSpherePoint(sphere.radius, theta0, phi0);
            const RenderFloat3 localB = BuildSpherePoint(sphere.radius, theta0, phi1);
            const RenderFloat3 localC = BuildSpherePoint(sphere.radius, theta1, phi1);
            const RenderFloat3 localD = BuildSpherePoint(sphere.radius, theta1, phi0);

            const RenderFloat3 worldA = Add(sphere.center, localA);
            const RenderFloat3 worldB = Add(sphere.center, localB);
            const RenderFloat3 worldC = Add(sphere.center, localC);
            const RenderFloat3 worldD = Add(sphere.center, localD);

            AppendTriangle(
                vertices,
                worldA,
                worldB,
                worldC,
                Normalize(localA),
                Normalize(localB),
                Normalize(localC),
                BuildSphereUv(u0, v0),
                BuildSphereUv(u0, v1),
                BuildSphereUv(u1, v1),
                sphere.materialIndex,
                sphere.albedoTint,
                sphere.objectId);
            AppendTriangle(
                vertices,
                worldA,
                worldC,
                worldD,
                Normalize(localA),
                Normalize(localC),
                Normalize(localD),
                BuildSphereUv(u0, v0),
                BuildSphereUv(u1, v1),
                BuildSphereUv(u1, v0),
                sphere.materialIndex,
                sphere.albedoTint,
                sphere.objectId);
        }
    }
}

RenderFloat3 DecodeBackgroundColor(RenderBackgroundMode mode) {
    switch (mode) {
    case RenderBackgroundMode::Gradient:
        return MakeRenderFloat3(0.25f, 0.38f, 0.58f);
    case RenderBackgroundMode::Checker:
        return MakeRenderFloat3(0.22f, 0.22f, 0.24f);
    case RenderBackgroundMode::Grid:
        return MakeRenderFloat3(0.19f, 0.21f, 0.26f);
    case RenderBackgroundMode::Black:
        return MakeRenderFloat3(0.0f, 0.0f, 0.0f);
    }

    return MakeRenderFloat3(0.1f, 0.1f, 0.1f);
}

void MultiplyMatrix(float result[16], const float left[16], const float right[16]) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result[column * 4 + row] =
                left[0 * 4 + row] * right[column * 4 + 0] +
                left[1 * 4 + row] * right[column * 4 + 1] +
                left[2 * 4 + row] * right[column * 4 + 2] +
                left[3 * 4 + row] * right[column * 4 + 3];
        }
    }
}

void BuildPerspectiveMatrix(float result[16], float fieldOfViewDegrees, float aspect, float nearPlane, float farPlane) {
    const float f = 1.0f / std::tan(fieldOfViewDegrees * 0.00872664626f);
    for (int i = 0; i < 16; ++i) {
        result[i] = 0.0f;
    }
    result[0] = f / aspect;
    result[5] = f;
    result[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    result[11] = -1.0f;
    result[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
}

void BuildViewMatrix(float result[16], const RenderCamera& camera) {
    const RenderFloat3 forward = Normalize(camera.GetForwardVector());
    const RenderFloat3 right = Normalize(camera.GetRightVector());
    const RenderFloat3 up = Normalize(camera.GetUpVector());
    const RenderFloat3 position = camera.GetPosition();

    result[0] = right.x;
    result[1] = up.x;
    result[2] = -forward.x;
    result[3] = 0.0f;
    result[4] = right.y;
    result[5] = up.y;
    result[6] = -forward.y;
    result[7] = 0.0f;
    result[8] = right.z;
    result[9] = up.z;
    result[10] = -forward.z;
    result[11] = 0.0f;
    result[12] = -Dot(right, position);
    result[13] = -Dot(up, position);
    result[14] = Dot(forward, position);
    result[15] = 1.0f;
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

GpuRasterMaterial ToGpuMaterial(const RenderMaterial& material) {
    GpuRasterMaterial result {};
    result.baseColor[0] = material.baseColor.x;
    result.baseColor[1] = material.baseColor.y;
    result.baseColor[2] = material.baseColor.z;
    result.baseColor[3] = 1.0f;
    result.emissionColorStrength[0] = material.emissionColor.x;
    result.emissionColorStrength[1] = material.emissionColor.y;
    result.emissionColorStrength[2] = material.emissionColor.z;
    result.emissionColorStrength[3] = material.emissionStrength;
    result.surfaceParams[0] = material.roughness;
    result.surfaceParams[1] = material.metallic;
    result.surfaceParams[2] = static_cast<float>(material.sourceAssetIndex);
    result.surfaceParams[3] = 0.0f;
    result.dielectricParams[0] = material.transmission;
    result.dielectricParams[1] = material.ior;
    result.dielectricParams[2] = 0.0f;
    result.dielectricParams[3] = 0.0f;
    result.mediumParams0[0] = material.absorptionColor.x;
    result.mediumParams0[1] = material.absorptionColor.y;
    result.mediumParams0[2] = material.absorptionColor.z;
    result.mediumParams0[3] = material.absorptionDistance;
    result.mediumParams1[0] = material.transmissionRoughness;
    result.mediumParams1[1] = material.thinWalled ? 1.0f : 0.0f;
    result.mediumParams1[2] = static_cast<float>(material.surfacePreset);
    result.mediumParams1[3] = 0.0f;
    result.textureRefs[0] = material.baseColorTexture.textureIndex;
    result.textureRefs[1] = material.metallicRoughnessTexture.textureIndex;
    result.textureRefs[2] = material.emissiveTexture.textureIndex;
    result.textureRefs[3] = material.normalTexture.textureIndex;
    return result;
}

GpuRasterLight ToGpuLight(const RenderLight& light) {
    GpuRasterLight result {};
    const RenderFloat3 direction = GetRenderLightDirection(light);
    const RenderFloat3 right = GetRenderLightRight(light);
    const RenderFloat3 up = GetRenderLightUp(light);
    result.positionType[0] = light.transform.translation.x;
    result.positionType[1] = light.transform.translation.y;
    result.positionType[2] = light.transform.translation.z;
    result.positionType[3] = static_cast<float>(light.type);
    result.directionRange[0] = direction.x;
    result.directionRange[1] = direction.y;
    result.directionRange[2] = direction.z;
    result.directionRange[3] = light.range;
    result.colorIntensity[0] = light.color.x;
    result.colorIntensity[1] = light.color.y;
    result.colorIntensity[2] = light.color.z;
    result.colorIntensity[3] = light.intensity;
    result.sizeAndAngles[0] = light.areaSize.x;
    result.sizeAndAngles[1] = light.areaSize.y;
    result.sizeAndAngles[2] = light.innerConeDegrees;
    result.sizeAndAngles[3] = light.outerConeDegrees;
    result.rightEnabled[0] = right.x;
    result.rightEnabled[1] = right.y;
    result.rightEnabled[2] = right.z;
    result.rightEnabled[3] = light.enabled ? 1.0f : 0.0f;
    result.upData[0] = up.x;
    result.upData[1] = up.y;
    result.upData[2] = up.z;
    result.upData[3] = 0.0f;
    return result;
}

void EncodeDefaultLayer(
    std::vector<unsigned char>& pixels,
    unsigned char r,
    unsigned char g,
    unsigned char b,
    unsigned char a) {
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = a;
    }
}

} // namespace

RenderRasterPreviewRenderer::~RenderRasterPreviewRenderer() {
    Shutdown();
}

bool RenderRasterPreviewRenderer::Initialize() {
    if (IsInitialized()) {
        return true;
    }

    const std::string rasterVertexSource = LoadShaderSource("RenderRasterPreview.vert");
    const std::string rasterFragmentSource = LoadShaderSource("RenderRasterPreview.frag");
    const std::string compositeVertexSource = LoadShaderSource("RenderOutlineComposite.vert");
    const std::string compositeFragmentSource = LoadShaderSource("RenderOutlineComposite.frag");
    if (rasterVertexSource.empty() || rasterFragmentSource.empty() ||
        compositeVertexSource.empty() || compositeFragmentSource.empty()) {
        m_LastError = "Failed to load raster preview shader sources.";
        return false;
    }

    m_RasterProgram = GLHelpers::CreateShaderProgram(rasterVertexSource.c_str(), rasterFragmentSource.c_str());
    m_CompositeProgram = GLHelpers::CreateShaderProgram(compositeVertexSource.c_str(), compositeFragmentSource.c_str());
    if (m_RasterProgram == 0 || m_CompositeProgram == 0) {
        m_LastError = "Failed to compile the raster preview shaders.";
        Shutdown();
        return false;
    }

    glGenVertexArrays(1, &m_VertexArray);
    glGenBuffers(1, &m_VertexBuffer);
    glGenBuffers(1, &m_MaterialBuffer);
    glGenBuffers(1, &m_LightBuffer);
    glBindVertexArray(m_VertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RasterVertex), reinterpret_cast<const void*>(offsetof(RasterVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RasterVertex), reinterpret_cast<const void*>(offsetof(RasterVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(RasterVertex), reinterpret_cast<const void*>(offsetof(RasterVertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(RasterVertex), reinterpret_cast<const void*>(offsetof(RasterVertex, materialIndex)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(RasterVertex), reinterpret_cast<const void*>(offsetof(RasterVertex, tint)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(RasterVertex), reinterpret_cast<const void*>(offsetof(RasterVertex, objectId)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    if (!EnsureDefaultTextureArrays()) {
        return false;
    }

    m_LastError.clear();
    return true;
}

void RenderRasterPreviewRenderer::Shutdown() {
    ReleaseSceneBuffers();
    ReleaseTargets();
    ReleaseTextureArrays();
    if (m_RasterProgram != 0) {
        glDeleteProgram(m_RasterProgram);
        m_RasterProgram = 0;
    }
    if (m_CompositeProgram != 0) {
        glDeleteProgram(m_CompositeProgram);
        m_CompositeProgram = 0;
    }
    if (m_VertexBuffer != 0) {
        glDeleteBuffers(1, &m_VertexBuffer);
        m_VertexBuffer = 0;
    }
    if (m_MaterialBuffer != 0) {
        glDeleteBuffers(1, &m_MaterialBuffer);
        m_MaterialBuffer = 0;
    }
    if (m_LightBuffer != 0) {
        glDeleteBuffers(1, &m_LightBuffer);
        m_LightBuffer = 0;
    }
    if (m_VertexArray != 0) {
        glDeleteVertexArrays(1, &m_VertexArray);
        m_VertexArray = 0;
    }
    m_UploadedSceneRevision = 0;
    m_UploadedVertexCount = 0;
    m_UploadedMaterialCount = 0;
    m_UploadedLightCount = 0;
    m_UploadedTextureCount = 0;
    m_LastError.clear();
}

bool RenderRasterPreviewRenderer::EnsureDefaultTextureArrays() {
    if (m_BaseColorArray != 0 && m_MaterialParamsArray != 0 && m_EmissiveArray != 0 && m_NormalArray != 0) {
        return true;
    }

    ReleaseTextureArrays();
    m_BaseColorArray = GLHelpers::CreateTextureArray(1, 1, 1, GL_RGBA8);
    m_MaterialParamsArray = GLHelpers::CreateTextureArray(1, 1, 1, GL_RGBA8);
    m_EmissiveArray = GLHelpers::CreateTextureArray(1, 1, 1, GL_RGBA8);
    m_NormalArray = GLHelpers::CreateTextureArray(1, 1, 1, GL_RGBA8);
    if (m_BaseColorArray == 0 || m_MaterialParamsArray == 0 || m_EmissiveArray == 0 || m_NormalArray == 0) {
        m_LastError = "Failed to allocate default raster texture arrays.";
        return false;
    }

    const std::array<unsigned char, 4> white = { 255, 255, 255, 255 };
    const std::array<unsigned char, 4> whiteEmissive = { 255, 255, 255, 255 };
    const std::array<unsigned char, 4> neutralNormal = { 128, 128, 255, 255 };
    GLHelpers::UploadTextureArrayLayer(m_BaseColorArray, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white.data());
    GLHelpers::UploadTextureArrayLayer(m_MaterialParamsArray, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white.data());
    GLHelpers::UploadTextureArrayLayer(m_EmissiveArray, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, whiteEmissive.data());
    GLHelpers::UploadTextureArrayLayer(m_NormalArray, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, neutralNormal.data());
    return true;
}

bool RenderRasterPreviewRenderer::EnsureTargets(int width, int height) {
    if (width <= 0 || height <= 0) {
        m_LastError = "Invalid raster preview target size.";
        return false;
    }

    if (m_TargetWidth == width && m_TargetHeight == height &&
        m_SceneFbo != 0 && m_CompositeFbo != 0 &&
        m_SceneColorTexture != 0 && m_ObjectIdTexture != 0 &&
        m_DepthTexture != 0 && m_CompositedTexture != 0) {
        return true;
    }

    ReleaseTargets();
    m_TargetWidth = width;
    m_TargetHeight = height;

    m_SceneColorTexture = GLHelpers::CreateStorageTexture(width, height, GL_RGBA8);
    m_ObjectIdTexture = GLHelpers::CreateStorageTexture(width, height, GL_RGBA8);
    m_DepthTexture = GLHelpers::CreateDepthTexture(width, height);
    m_CompositedTexture = GLHelpers::CreateStorageTexture(width, height, GL_RGBA8);
    if (m_SceneColorTexture == 0 || m_ObjectIdTexture == 0 || m_DepthTexture == 0 || m_CompositedTexture == 0) {
        m_LastError = "Failed to allocate raster preview textures.";
        ReleaseTargets();
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, m_ObjectIdTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &m_SceneFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_SceneFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_SceneColorTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_ObjectIdTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_DepthTexture, 0);
    const GLenum sceneAttachments[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, sceneAttachments);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        m_LastError = "Raster scene framebuffer is incomplete.";
        ReleaseTargets();
        return false;
    }

    glGenFramebuffers(1, &m_CompositeFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_CompositeFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_CompositedTexture, 0);
    const GLenum compositeAttachments[] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, compositeAttachments);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        m_LastError = "Raster composite framebuffer is incomplete.";
        ReleaseTargets();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

bool RenderRasterPreviewRenderer::UploadScene(const RenderScene& scene) {
    if (m_UploadedSceneRevision == scene.GetRevision()) {
        return true;
    }

    std::vector<RasterVertex> vertices;
    vertices.reserve(static_cast<std::size_t>(scene.GetResolvedTriangleCount() * 3 + scene.GetSphereCount() * kSphereSlices * kSphereStacks * 6));

    for (int triangleIndex = 0; triangleIndex < scene.GetResolvedTriangleCount(); ++triangleIndex) {
        AppendResolvedTriangle(vertices, scene.GetResolvedTriangle(triangleIndex));
    }
    for (int sphereIndex = 0; sphereIndex < scene.GetSphereCount(); ++sphereIndex) {
        AppendSphere(vertices, ResolveSphere(scene.GetSphere(sphereIndex)));
    }

    std::vector<GpuRasterMaterial> materials;
    materials.reserve(static_cast<std::size_t>(scene.GetMaterialCount()));
    for (int materialIndex = 0; materialIndex < scene.GetMaterialCount(); ++materialIndex) {
        materials.push_back(ToGpuMaterial(scene.GetMaterial(materialIndex)));
    }

    std::vector<GpuRasterLight> lights;
    lights.reserve(static_cast<std::size_t>(scene.GetLightCount()));
    for (int lightIndex = 0; lightIndex < scene.GetLightCount(); ++lightIndex) {
        lights.push_back(ToGpuLight(scene.GetLight(lightIndex)));
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(RasterVertex)), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_MaterialBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(materials.size() * sizeof(GpuRasterMaterial)), materials.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_LightBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(lights.size() * sizeof(GpuRasterLight)), lights.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    ReleaseTextureArrays();
    const int layerCount = std::max(1, scene.GetImportedTextureCount() + 1);
    m_BaseColorArray = GLHelpers::CreateTextureArray(kPreviewTextureSize, kPreviewTextureSize, layerCount, GL_RGBA8);
    m_MaterialParamsArray = GLHelpers::CreateTextureArray(kPreviewTextureSize, kPreviewTextureSize, layerCount, GL_RGBA8);
    m_EmissiveArray = GLHelpers::CreateTextureArray(kPreviewTextureSize, kPreviewTextureSize, layerCount, GL_RGBA8);
    m_NormalArray = GLHelpers::CreateTextureArray(kPreviewTextureSize, kPreviewTextureSize, layerCount, GL_RGBA8);
    if (m_BaseColorArray == 0 || m_MaterialParamsArray == 0 || m_EmissiveArray == 0 || m_NormalArray == 0) {
        m_LastError = "Failed to allocate raster texture arrays.";
        return false;
    }

    std::vector<unsigned char> defaultWhite(static_cast<std::size_t>(kPreviewTextureSize * kPreviewTextureSize * 4), 255);
    std::vector<unsigned char> defaultNormal(static_cast<std::size_t>(kPreviewTextureSize * kPreviewTextureSize * 4), 255);
    EncodeDefaultLayer(defaultNormal, 128, 128, 255, 255);
    GLHelpers::UploadTextureArrayLayer(m_BaseColorArray, 0, kPreviewTextureSize, kPreviewTextureSize, GL_RGBA, GL_UNSIGNED_BYTE, defaultWhite.data());
    GLHelpers::UploadTextureArrayLayer(m_MaterialParamsArray, 0, kPreviewTextureSize, kPreviewTextureSize, GL_RGBA, GL_UNSIGNED_BYTE, defaultWhite.data());
    GLHelpers::UploadTextureArrayLayer(m_EmissiveArray, 0, kPreviewTextureSize, kPreviewTextureSize, GL_RGBA, GL_UNSIGNED_BYTE, defaultWhite.data());
    GLHelpers::UploadTextureArrayLayer(m_NormalArray, 0, kPreviewTextureSize, kPreviewTextureSize, GL_RGBA, GL_UNSIGNED_BYTE, defaultNormal.data());

    for (int textureIndex = 0; textureIndex < scene.GetImportedTextureCount(); ++textureIndex) {
        const RenderImportedTexture& texture = scene.GetImportedTexture(textureIndex);
        const int layer = textureIndex + 1;
        GLHelpers::UploadTextureArrayLayer(m_BaseColorArray, layer, kPreviewTextureSize, kPreviewTextureSize, GL_RGBA, GL_UNSIGNED_BYTE, defaultWhite.data());
        GLHelpers::UploadTextureArrayLayer(m_MaterialParamsArray, layer, kPreviewTextureSize, kPreviewTextureSize, GL_RGBA, GL_UNSIGNED_BYTE, defaultWhite.data());
        GLHelpers::UploadTextureArrayLayer(m_EmissiveArray, layer, kPreviewTextureSize, kPreviewTextureSize, GL_RGBA, GL_UNSIGNED_BYTE, defaultWhite.data());
        GLHelpers::UploadTextureArrayLayer(m_NormalArray, layer, kPreviewTextureSize, kPreviewTextureSize, GL_RGBA, GL_UNSIGNED_BYTE, defaultNormal.data());

        std::vector<unsigned char> resizedPixels = ResizeTexturePixels(texture, 255, 255, 255, 255);
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
            targetTexture = m_NormalArray;
            break;
        }
        GLHelpers::UploadTextureArrayLayer(targetTexture, layer, kPreviewTextureSize, kPreviewTextureSize, GL_RGBA, GL_UNSIGNED_BYTE, resizedPixels.data());
    }

    m_UploadedSceneRevision = scene.GetRevision();
    m_UploadedVertexCount = static_cast<int>(vertices.size());
    m_UploadedMaterialCount = static_cast<int>(materials.size());
    m_UploadedLightCount = static_cast<int>(lights.size());
    m_UploadedTextureCount = scene.GetImportedTextureCount();
    return true;
}

bool RenderRasterPreviewRenderer::RenderScenePreview(
    const RenderScene& scene,
    const RenderCamera& camera,
    const RenderSettings& settings,
    int width,
    int height) {
    if (!Initialize()) {
        return false;
    }
    if (!EnsureTargets(width, height) || !UploadScene(scene)) {
        return false;
    }

    const float aspect = width > 0 && height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    float projection[16] {};
    float view[16] {};
    float viewProjection[16] {};
    BuildPerspectiveMatrix(projection, camera.GetFieldOfViewDegrees(), aspect, 0.05f, 200.0f);
    BuildViewMatrix(view, camera);
    MultiplyMatrix(viewProjection, projection, view);

    const RenderFloat3 background = DecodeBackgroundColor(scene.GetBackgroundMode());
    glBindFramebuffer(GL_FRAMEBUFFER, m_SceneFbo);
    glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(background.x, background.y, background.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(m_RasterProgram);
    glUniformMatrix4fv(glGetUniformLocation(m_RasterProgram, "uViewProjection"), 1, GL_FALSE, viewProjection);
    glUniform3f(glGetUniformLocation(m_RasterProgram, "uCameraPosition"), camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z);
    glUniform1i(glGetUniformLocation(m_RasterProgram, "uEnvironmentEnabled"), scene.IsEnvironmentEnabled() ? 1 : 0);
    glUniform1f(glGetUniformLocation(m_RasterProgram, "uEnvironmentIntensity"), scene.GetEnvironmentIntensity());
    glUniform1i(glGetUniformLocation(m_RasterProgram, "uFogEnabled"), scene.IsFogEnabled() ? 1 : 0);
    glUniform3f(glGetUniformLocation(m_RasterProgram, "uFogColor"), scene.GetFogColor().x, scene.GetFogColor().y, scene.GetFogColor().z);
    glUniform1f(glGetUniformLocation(m_RasterProgram, "uFogDensity"), scene.GetFogDensity());
    glUniform1f(glGetUniformLocation(m_RasterProgram, "uFogAnisotropy"), scene.GetFogAnisotropy());
    glUniform1i(glGetUniformLocation(m_RasterProgram, "uMaterialCount"), scene.GetMaterialCount());
    glUniform1i(glGetUniformLocation(m_RasterProgram, "uLightCount"), scene.GetLightCount());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_MaterialBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_LightBuffer);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_BaseColorArray);
    glUniform1i(glGetUniformLocation(m_RasterProgram, "uBaseColorTextures"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_MaterialParamsArray);
    glUniform1i(glGetUniformLocation(m_RasterProgram, "uMaterialParamTextures"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_EmissiveArray);
    glUniform1i(glGetUniformLocation(m_RasterProgram, "uEmissiveTextures"), 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_NormalArray);
    glUniform1i(glGetUniformLocation(m_RasterProgram, "uNormalTextures"), 3);

    glBindVertexArray(m_VertexArray);
    glDrawArrays(GL_TRIANGLES, 0, m_UploadedVertexCount);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_LastError.clear();
    return true;
}

bool RenderRasterPreviewRenderer::ComposeViewport(unsigned int baseTexture, int width, int height, int selectedObjectId) {
    if (!Initialize()) {
        return false;
    }
    if (!EnsureTargets(width, height)) {
        return false;
    }
    if (baseTexture == 0) {
        m_LastError = "No base texture is available for viewport composition.";
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_CompositeFbo);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_CompositeProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, baseTexture);
    glUniform1i(glGetUniformLocation(m_CompositeProgram, "uBaseTexture"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_ObjectIdTexture);
    glUniform1i(glGetUniformLocation(m_CompositeProgram, "uObjectIdTexture"), 1);
    glUniform1i(glGetUniformLocation(m_CompositeProgram, "uSelectedObjectId"), selectedObjectId);
    glUniform2i(glGetUniformLocation(m_CompositeProgram, "uResolution"), width, height);
    glBindVertexArray(m_VertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_LastError.clear();
    return true;
}

void RenderRasterPreviewRenderer::ReleaseSceneBuffers() {
    m_UploadedSceneRevision = 0;
    m_UploadedVertexCount = 0;
    m_UploadedMaterialCount = 0;
    m_UploadedLightCount = 0;
    m_UploadedTextureCount = 0;
}

void RenderRasterPreviewRenderer::ReleaseTargets() {
    if (m_SceneFbo != 0) {
        glDeleteFramebuffers(1, &m_SceneFbo);
        m_SceneFbo = 0;
    }
    if (m_CompositeFbo != 0) {
        glDeleteFramebuffers(1, &m_CompositeFbo);
        m_CompositeFbo = 0;
    }
    if (m_SceneColorTexture != 0) {
        glDeleteTextures(1, &m_SceneColorTexture);
        m_SceneColorTexture = 0;
    }
    if (m_ObjectIdTexture != 0) {
        glDeleteTextures(1, &m_ObjectIdTexture);
        m_ObjectIdTexture = 0;
    }
    if (m_DepthTexture != 0) {
        glDeleteTextures(1, &m_DepthTexture);
        m_DepthTexture = 0;
    }
    if (m_CompositedTexture != 0) {
        glDeleteTextures(1, &m_CompositedTexture);
        m_CompositedTexture = 0;
    }
    m_TargetWidth = 0;
    m_TargetHeight = 0;
}

void RenderRasterPreviewRenderer::ReleaseTextureArrays() {
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
    if (m_NormalArray != 0) {
        glDeleteTextures(1, &m_NormalArray);
        m_NormalArray = 0;
    }
}
