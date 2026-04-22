#pragma once

#include <cstdint>
#include <string>

struct RenderFinalRenderSettings {
    int resolutionX = 1920;
    int resolutionY = 1080;
    int sampleTarget = 256;
    int maxBounceCount = 8;
    std::string outputName = "Final Render";
};

enum class RenderDisplayMode {
    Color = 0,
    Luminance,
    SampleTint,
    AlbedoAov,
    WorldNormalAov,
    DepthAov,
    MaterialIdAov,
    PrimitiveIdAov,
    SampleCountAov,
    VarianceAov
};

enum class RenderTonemapMode {
    LinearClamp = 0,
    Reinhard,
    AcesFilm
};

enum class RenderIntegratorMode {
    RasterPreview = 0,
    PathTracePreview,
    DebugPreview
};

enum class RenderGizmoMode {
    Translate = 0,
    Rotate,
    Scale
};

enum class RenderTransformSpace {
    World = 0,
    Local
};

enum class RenderDebugViewMode {
    Disabled = 0,
    WorldNormal,
    HitDistance,
    PrimitiveId,
    BvhDepth
};

class RenderSettings {
public:
    RenderSettings();

    int GetResolutionX() const { return m_ResolutionX; }
    int GetResolutionY() const { return m_ResolutionY; }
    int GetPreviewSampleTarget() const { return m_PreviewSampleTarget; }
    bool IsAccumulationEnabled() const { return m_AccumulationEnabled; }
    RenderIntegratorMode GetIntegratorMode() const { return m_IntegratorMode; }
    int GetMaxBounceCount() const { return m_MaxBounceCount; }
    RenderDisplayMode GetDisplayMode() const { return m_DisplayMode; }
    RenderTonemapMode GetTonemapMode() const { return m_TonemapMode; }
    RenderGizmoMode GetGizmoMode() const { return m_GizmoMode; }
    RenderTransformSpace GetTransformSpace() const { return m_TransformSpace; }
    RenderDebugViewMode GetDebugViewMode() const { return m_DebugViewMode; }
    const RenderFinalRenderSettings& GetFinalRenderSettings() const { return m_FinalRenderSettings; }
    int GetFinalRenderResolutionX() const { return m_FinalRenderSettings.resolutionX; }
    int GetFinalRenderResolutionY() const { return m_FinalRenderSettings.resolutionY; }
    int GetFinalRenderSampleTarget() const { return m_FinalRenderSettings.sampleTarget; }
    int GetFinalRenderMaxBounceCount() const { return m_FinalRenderSettings.maxBounceCount; }
    const std::string& GetFinalRenderOutputName() const { return m_FinalRenderSettings.outputName; }
    bool IsBvhTraversalEnabled() const { return m_UseBvhTraversal; }
    std::uint64_t GetRevision() const { return m_Revision; }
    const std::string& GetLastChangeReason() const { return m_LastChangeReason; }

    bool SetResolution(int width, int height);
    bool SetPreviewSampleTarget(int target);
    bool SetAccumulationEnabled(bool enabled);
    bool SetIntegratorMode(RenderIntegratorMode mode);
    bool SetMaxBounceCount(int count);
    bool SetDisplayMode(RenderDisplayMode mode);
    bool SetTonemapMode(RenderTonemapMode mode);
    bool SetGizmoMode(RenderGizmoMode mode);
    bool SetTransformSpace(RenderTransformSpace mode);
    bool SetDebugViewMode(RenderDebugViewMode mode);
    bool SetFinalRenderResolution(int width, int height);
    bool SetFinalRenderSampleTarget(int target);
    bool SetFinalRenderMaxBounceCount(int count);
    bool SetFinalRenderOutputName(const std::string& outputName);
    bool SetBvhTraversalEnabled(bool enabled);

private:
    void Touch(const std::string& reason);

    int m_ResolutionX = 1280;
    int m_ResolutionY = 720;
    int m_PreviewSampleTarget = 64;
    bool m_AccumulationEnabled = true;
    RenderIntegratorMode m_IntegratorMode = RenderIntegratorMode::RasterPreview;
    int m_MaxBounceCount = 4;
    RenderDisplayMode m_DisplayMode = RenderDisplayMode::Color;
    RenderTonemapMode m_TonemapMode = RenderTonemapMode::AcesFilm;
    RenderGizmoMode m_GizmoMode = RenderGizmoMode::Translate;
    RenderTransformSpace m_TransformSpace = RenderTransformSpace::World;
    RenderDebugViewMode m_DebugViewMode = RenderDebugViewMode::Disabled;
    RenderFinalRenderSettings m_FinalRenderSettings {};
    bool m_UseBvhTraversal = true;
    std::uint64_t m_Revision = 0;
    std::string m_LastChangeReason;
};
