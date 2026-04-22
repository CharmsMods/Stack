#pragma once

#include <cstdint>
#include <string>

class RenderCamera;
class RenderScene;
class RenderSettings;

class RenderRasterPreviewRenderer {
public:
    RenderRasterPreviewRenderer() = default;
    ~RenderRasterPreviewRenderer();

    bool Initialize();
    void Shutdown();
    bool IsInitialized() const { return m_RasterProgram != 0 && m_CompositeProgram != 0; }

    bool RenderScenePreview(const RenderScene& scene, const RenderCamera& camera, const RenderSettings& settings, int width, int height);
    bool ComposeViewport(unsigned int baseTexture, int width, int height, int selectedObjectId);

    unsigned int GetSceneColorTexture() const { return m_SceneColorTexture; }
    unsigned int GetCompositedTexture() const { return m_CompositedTexture; }
    unsigned int GetObjectIdTexture() const { return m_ObjectIdTexture; }
    int GetUploadedVertexCount() const { return m_UploadedVertexCount; }
    int GetUploadedMaterialCount() const { return m_UploadedMaterialCount; }
    int GetUploadedTextureCount() const { return m_UploadedTextureCount; }
    const std::string& GetLastError() const { return m_LastError; }

private:
    bool EnsureTargets(int width, int height);
    bool UploadScene(const RenderScene& scene);
    void ReleaseSceneBuffers();
    void ReleaseTargets();
    void ReleaseTextureArrays();
    bool EnsureDefaultTextureArrays();

    unsigned int m_RasterProgram = 0;
    unsigned int m_CompositeProgram = 0;
    unsigned int m_VertexArray = 0;
    unsigned int m_VertexBuffer = 0;
    unsigned int m_MaterialBuffer = 0;
    unsigned int m_LightBuffer = 0;
    unsigned int m_SceneFbo = 0;
    unsigned int m_CompositeFbo = 0;
    unsigned int m_SceneColorTexture = 0;
    unsigned int m_ObjectIdTexture = 0;
    unsigned int m_DepthTexture = 0;
    unsigned int m_CompositedTexture = 0;
    unsigned int m_BaseColorArray = 0;
    unsigned int m_MaterialParamsArray = 0;
    unsigned int m_EmissiveArray = 0;
    unsigned int m_NormalArray = 0;
    int m_TargetWidth = 0;
    int m_TargetHeight = 0;
    std::uint64_t m_UploadedSceneRevision = 0;
    int m_UploadedVertexCount = 0;
    int m_UploadedMaterialCount = 0;
    int m_UploadedLightCount = 0;
    int m_UploadedTextureCount = 0;
    std::string m_LastError;
};
