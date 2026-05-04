#pragma once

#include "Async/TaskState.h"
#include "LayerRegistry.h"
#include "EditorRenderWorker.h"
#include "NodeGraph/EditorNodeGraph.h"
#include "UI/EditorSidebar.h"
#include "UI/EditorViewport.h"
#include "Renderer/RenderPipeline.h"
#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <functional>

#include "UI/EditorScopes.h"

struct GLFWwindow;

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

    void Initialize(GLFWwindow* sharedWindow = nullptr);
    
    // Called every frame by the AppShell
    void RenderUI();

    RenderPipeline& GetPipeline() { return m_Pipeline; }
    std::vector<std::shared_ptr<LayerBase>>& GetLayers() { return m_Layers; }

    // Dynamic Layer Management
    void AddLayer(LayerType type);
    void AddLayerNodeAt(LayerType type, EditorNodeGraph::Vec2 graphPosition);
    void RemoveLayer(int index);
    void MoveLayer(int from, int to);
    void SetLayerVisible(int index, bool visible);
    void SelectLayer(int index);
    void SelectGraphNode(int nodeId);

    EditorNodeGraph::Graph& GetNodeGraph() { return m_NodeGraph; }
    const EditorNodeGraph::Graph& GetNodeGraph() const { return m_NodeGraph; }
    bool IsGraphOutputConnected() const { return m_NodeGraph.IsOutputConnected(); }
    void PromptAddImageNodeAt(EditorNodeGraph::Vec2 graphPosition);
    bool AddImageNodeFromFile(const std::string& path, EditorNodeGraph::Vec2 graphPosition);
    bool ConnectGraphImageNode(int nodeId);
    bool ConnectGraphNodes(int fromNodeId, int toNodeId, std::string* errorMessage = nullptr);
    bool ConnectGraphSockets(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId, std::string* errorMessage = nullptr);
    bool RemoveGraphLink(int fromNodeId, int toNodeId);
    bool RemoveGraphLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId);
    bool DeleteSelectedGraphLink();
    bool RemoveGraphNode(int nodeId);
    bool DeleteSelectedGraphNodes();
    void AddScopeNodeAt(EditorNodeGraph::ScopeKind scopeKind, EditorNodeGraph::Vec2 graphPosition);
    void AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind maskKind, EditorNodeGraph::Vec2 graphPosition);
    void AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind utilityKind, EditorNodeGraph::Vec2 graphPosition);
    void AddImageToMaskNodeAt(EditorNodeGraph::ImageToMaskKind converterKind, EditorNodeGraph::Vec2 graphPosition);
    void AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind generatorKind, EditorNodeGraph::Vec2 graphPosition);
    void AddMixNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddPreviewNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AutoLayoutGraph();
    void DisconnectGraphOutput();
    void SetGraphDropTargetRect(float minX, float minY, float maxX, float maxY);
    void SetGraphViewTransform(float originX, float originY, float panX, float panY, float zoom);
    bool IsScreenPointOverGraph(float x, float y) const;
    bool HandleGraphFileDrop(const std::string& path, float screenX, float screenY);
    std::vector<unsigned char> GetScopePixelsForNode(int nodeId, int& outW, int& outH);
    std::vector<unsigned char> GetPreviewPixelsForNode(int nodeId, int& outW, int& outH);
    void RenderGraphScopeNode(EditorNodeGraph::ScopeKind scopeKind, int sourceNodeId);
    void MarkRenderDirty();
    bool IsEditorRenderBusy() const { return m_RenderWorker.IsBusy() || m_RenderPending; }

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
    void SetSelectedLayerIndex(int idx) { SelectLayer(idx); }
    bool ConsumeSelectedTabFocusRequest() {
        const bool requested = m_FocusSelectedTabNextRender;
        m_FocusSelectedTabNextRender = false;
        return requested;
    }

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

    bool IsPickingColor() const { return m_IsPickingColor; }
    void SetPickingColor(bool picking, std::function<void(float, float, float)> callback = nullptr) {
        m_IsPickingColor = picking;
        m_ColorPickerCallback = callback;
    }
    void OnColorPicked(float r, float g, float b) {
        if (m_ColorPickerCallback) m_ColorPickerCallback(r, g, b);
        m_IsPickingColor = false;
    }

private:
    EditorSidebar m_Sidebar;
    EditorViewport m_Viewport;
    EditorScopes m_Scopes;
    RenderPipeline m_Pipeline;
    EditorRenderWorker m_RenderWorker;
    EditorNodeGraph::Graph m_NodeGraph;

    std::vector<std::shared_ptr<LayerBase>> m_Layers;
    int m_SelectedLayerIndex = -1;
    bool m_FocusSelectedTabNextRender = false;
    float m_HoverFade = 0.0f;
    bool m_RenderOnlyUpToActive = false;
    std::string m_CurrentProjectName = "";
    std::string m_CurrentProjectFileName = "";
    float m_GraphDropMinX = 0.0f;
    float m_GraphDropMinY = 0.0f;
    float m_GraphDropMaxX = 0.0f;
    float m_GraphDropMaxY = 0.0f;
    float m_GraphViewOriginX = 0.0f;
    float m_GraphViewOriginY = 0.0f;
    float m_GraphViewPanX = 0.0f;
    float m_GraphViewPanY = 0.0f;
    float m_GraphViewZoom = 1.0f;

    std::uint64_t m_SourceLoadGeneration = 0;
    Async::TaskState m_SourceLoadTaskState = Async::TaskState::Idle;
    std::string m_SourceLoadStatusText;

    std::uint64_t m_ExportGeneration = 0;
    Async::TaskState m_ExportTaskState = Async::TaskState::Idle;
    std::string m_ExportStatusText;

    bool m_IsPickingColor = false;
    std::function<void(float, float, float)> m_ColorPickerCallback;
    bool m_RenderWorkerAvailable = false;
    bool m_RenderDirty = true;
    bool m_RenderPending = false;
    std::uint64_t m_RenderGeneration = 0;
    std::uint64_t m_LastCompletedRenderGeneration = 0;
    double m_LastRenderDirtyTime = 0.0;
    std::string m_LastRenderSignature;

    void RefreshGraphLayerMetadata();
    void ApplyGraphLayerOrder();
    std::vector<std::shared_ptr<LayerBase>> BuildGraphRenderLayers() const;
    std::vector<RenderLayerStep> BuildGraphRenderSteps() const;
    std::vector<RenderMaskSource> BuildGraphRenderMasks() const;
    RenderGraphSnapshot BuildGraphSnapshot() const;
    EditorRenderWorker::Snapshot BuildRenderSnapshot(std::uint64_t generation) const;
    std::string BuildRenderSignature() const;
    void ConsumeRenderWorkerResults();
    void SubmitRenderIfReady();
};
