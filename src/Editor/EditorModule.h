#pragma once

#include "Async/TaskState.h"
#include "UI/EditorSidebar.h"
#include "UI/EditorViewport.h"
#include "Renderer/RenderPipeline.h"
#include "Layers/LayerBase.h"
#include <cstdint>
#include <vector>
#include <memory>
#include <string>

#include "UI/EditorScopes.h"

// Available Layer Types for the Factory
enum class LayerType {
    Adjustments,
    ColorGrade,
    HDR,
    CropTransform,
    Blur,
    Noise,
    Vignette,
    ChromaticAberration,
    LensDistortion,
    TiltShiftBlur,
    Dither,
    Compression,
    CellShading,
    Heatwave,
    PaletteReconstructor,
    EdgeEffects,
    AiryBloom,
    ImageBreaks,
    AnalogVideo,
    BilateralFilter,
    Denoising,
    Halftoning,
    HankelBlur,
    GlareRays,
    Corruption,
    AlphaHandling,
    BackgroundPatcher,
    Expander,
    TextOverlay
};

// The main coordinator for the Editor context.
class EditorModule {
public:
    struct LoadedProjectData {
        std::vector<unsigned char> sourcePixels;
        int width = 0;
        int height = 0;
        int channels = 4;
        nlohmann::json pipelineData = nlohmann::json::array();
        std::string projectName;
        std::string projectFileName;
    };

    EditorModule();
    ~EditorModule();

    void Initialize();
    
    // Called every frame by the AppShell
    void RenderUI();

    RenderPipeline& GetPipeline() { return m_Pipeline; }
    std::vector<std::shared_ptr<LayerBase>>& GetLayers() { return m_Layers; }

    // Dynamic Layer Management
    void AddLayer(LayerType type);
    void RemoveLayer(int index);
    void MoveLayer(int from, int to);

    // Persistence & Serialization
    nlohmann::json SerializePipeline();
    void DeserializePipeline(const nlohmann::json& j);
    void LoadSourceFromPixels(const unsigned char* data, int w, int h, int ch);
    bool ApplyLoadedProject(const LoadedProjectData& projectData);
    void RequestLoadSourceImage(const std::string& path);
    bool ExportImage(const std::string& path);
    bool RequestExportImage(const std::string& path);

    const std::string& GetCurrentProjectName() const { return m_CurrentProjectName; }
    void SetCurrentProjectName(const std::string& name) { m_CurrentProjectName = name; }

    const std::string& GetCurrentProjectFileName() const { return m_CurrentProjectFileName; }
    void SetCurrentProjectFileName(const std::string& fileName) { m_CurrentProjectFileName = fileName; }

    int GetSelectedLayerIndex() const { return m_SelectedLayerIndex; }
    void SetSelectedLayerIndex(int idx) { m_SelectedLayerIndex = idx; }

    float GetHoverFade() const { return m_HoverFade; }
    void  SetHoverFade(float f) { m_HoverFade = f; }

    bool IsRenderOnlyUpToActive() const { return m_RenderOnlyUpToActive; }
    void SetRenderOnlyUpToActive(bool b) { m_RenderOnlyUpToActive = b; }

    Async::TaskState GetSourceLoadTaskState() const { return m_SourceLoadTaskState; }
    const std::string& GetSourceLoadStatusText() const { return m_SourceLoadStatusText; }
    bool IsSourceLoadBusy() const { return Async::IsBusy(m_SourceLoadTaskState); }

    Async::TaskState GetExportTaskState() const { return m_ExportTaskState; }
    const std::string& GetExportStatusText() const { return m_ExportStatusText; }
    bool IsExportBusy() const { return Async::IsBusy(m_ExportTaskState); }

private:
    EditorSidebar m_Sidebar;
    EditorViewport m_Viewport;
    EditorScopes m_Scopes;
    RenderPipeline m_Pipeline;

    std::vector<std::shared_ptr<LayerBase>> m_Layers;
    int m_SelectedLayerIndex = -1;
    float m_HoverFade = 0.0f;
    bool m_RenderOnlyUpToActive = false;
    std::string m_CurrentProjectName = "";
    std::string m_CurrentProjectFileName = "";

    std::uint64_t m_SourceLoadGeneration = 0;
    Async::TaskState m_SourceLoadTaskState = Async::TaskState::Idle;
    std::string m_SourceLoadStatusText;

    std::uint64_t m_ExportGeneration = 0;
    Async::TaskState m_ExportTaskState = Async::TaskState::Idle;
    std::string m_ExportStatusText;
};
