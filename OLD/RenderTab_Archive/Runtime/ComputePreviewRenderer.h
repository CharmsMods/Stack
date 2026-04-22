#pragma once

#include <cstdint>
#include <string>

class RenderBuffers;
class RenderCamera;
class RenderScene;
class RenderSettings;

class ComputePreviewRenderer {
public:
    ComputePreviewRenderer() = default;
    ~ComputePreviewRenderer();

    bool Initialize();
    void Shutdown();
    bool IsInitialized() const { return m_Program != 0; }

    bool RenderPreview(const RenderScene& scene, const RenderCamera& camera, const RenderSettings& settings, RenderBuffers& buffers);

    int GetDispatchGroupsX() const { return m_DispatchGroupsX; }
    int GetDispatchGroupsY() const { return m_DispatchGroupsY; }
    int GetUploadedSphereCount() const { return m_UploadedSphereCount; }
    int GetUploadedTriangleCount() const { return m_UploadedTriangleCount; }
    int GetUploadedPrimitiveCount() const { return m_UploadedPrimitiveCount; }
    int GetUploadedBvhNodeCount() const { return m_UploadedBvhNodeCount; }
    const std::string& GetLastError() const { return m_LastError; }

private:
    bool UploadSceneBuffers(const RenderScene& scene);
    void ReleaseSceneBuffers();
    void ReleaseTextureArrays();

    unsigned int m_Program = 0;
    unsigned int m_SphereBuffer = 0;
    unsigned int m_TriangleBuffer = 0;
    unsigned int m_PrimitiveRefBuffer = 0;
    unsigned int m_BvhBuffer = 0;
    unsigned int m_LightBuffer = 0;
    unsigned int m_BaseColorArray = 0;
    unsigned int m_MaterialParamsArray = 0;
    unsigned int m_EmissiveArray = 0;
    std::uint64_t m_UploadedSceneRevision = 0;
    int m_DispatchGroupsX = 0;
    int m_DispatchGroupsY = 0;
    int m_UploadedSphereCount = 0;
    int m_UploadedTriangleCount = 0;
    int m_UploadedPrimitiveCount = 0;
    int m_UploadedBvhNodeCount = 0;
    int m_UploadedLightCount = 0;
    int m_UploadedTextureCount = 0;
    std::string m_LastError;
};
