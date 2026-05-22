#pragma once

#include "Async/TaskState.h"
#include "Layers/LayerBase.h"
#include "LayerRegistry.h"
#include "EditorRenderWorker.h"
#include "NodeGraph/EditorNodeGraph.h"
#include "UI/EditorSidebar.h"
#include "UI/EditorViewport.h"
#include "Renderer/RenderPipeline.h"
#include "Persistence/StackBinaryFormat.h"
#include <imgui.h>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "UI/EditorScopes.h"

struct GLFWwindow;

// The main coordinator for the Editor context.
class EditorModule {
public:
    enum class ViewportMode {
        SingleOutputPreview,
        CompositeCanvas
    };

    enum class CanvasToolKind {
        None,
        PickColor,
        AdjustAberrationCenter
    };

    enum class CompositeExportBoundsMode {
        Auto,
        Custom
    };

    enum class CompositeExportBackgroundMode {
        Transparent,
        Solid
    };

    enum class CompositeExportAspectPreset {
        Ratio1x1,
        Ratio4x3,
        Ratio3x2,
        Ratio16x9,
        Ratio9x16,
        Ratio2x3,
        Ratio5x4,
        Ratio21x9,
        Custom
    };

    enum class CompositeSnapModePreset {
        Off,
        ObjectOnly,
        Full,
        Custom
    };

    enum class CompositeResizeMode {
        Stretch,
        Scale
    };

    enum class CompositeScaleOriginMode {
        Opposite,
        Center
    };

    struct CompositeFloatRect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct CompositeExportSettings {
        CompositeExportBoundsMode boundsMode = CompositeExportBoundsMode::Auto;
        CompositeExportBackgroundMode backgroundMode = CompositeExportBackgroundMode::Transparent;
        std::array<float, 4> backgroundColor { 0.08f, 0.08f, 0.08f, 1.0f };
        float customX = -512.0f;
        float customY = -512.0f;
        float customWidth = 1024.0f;
        float customHeight = 1024.0f;
        CompositeExportAspectPreset aspectPreset = CompositeExportAspectPreset::Ratio1x1;
        float customAspectRatio = 1.0f;
        int outputWidth = 1024;
        int outputHeight = 1024;
    };

    struct CompositeSnapSettings {
        bool enabled = false;
        bool snapToObjects = true;
        bool snapToCenters = true;
        bool snapToCanvasCenter = true;
        bool snapToExportBounds = false;
        float rotateSnapStep = 15.0f;
        float scaleSnapStep = 0.1f;
        float lastNonZeroRotateSnapStep = 15.0f;
        float lastNonZeroScaleSnapStep = 0.1f;
    };

    struct CompositeSceneItem {
        int outputNodeId = -1;
        ImVec2 position = ImVec2(0.0f, 0.0f);
        ImVec2 scale = ImVec2(1.0f, 1.0f);
        float rotation = 0.0f;
        bool visible = true;
        bool locked = false;
        bool placementInitialized = false;
        std::string label;
        unsigned int texture = 0;
        int textureWidth = 0;
        int textureHeight = 0;
        std::vector<unsigned char> rgbaPixels;
        std::uint64_t cachedRenderRevision = 0;
        std::size_t cachedChainFingerprint = 0;
        std::uint64_t requestedRenderRevision = 0;
        std::size_t requestedChainFingerprint = 0;
        int requestedRasterWidth = 0;
        int requestedRasterHeight = 0;
    };

    struct GraphPreviewPixels {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        std::uint64_t revision = 0;
    };

    struct PersistedCompositeSceneEntry {
        int outputNodeId = -1;
        ImVec2 position = ImVec2(0.0f, 0.0f);
        ImVec2 scale = ImVec2(1.0f, 1.0f);
        float rotation = 0.0f;
        bool visible = true;
        bool locked = false;
    };

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
    bool SelectAdjacentMainChainNode(int direction);
    bool LayerUsesRichNodeSurface(int layerIndex) const;
    bool NodeUsesRichNodeSurface(int nodeId) const;
    NodeSurfaceSpec GetLayerNodeSurfaceSpec(int layerIndex) const;
    NodeSurfaceSpec GetNodeSurfaceSpec(int nodeId) const;

    EditorNodeGraph::Graph& GetNodeGraph() { return m_NodeGraph; }
    const EditorNodeGraph::Graph& GetNodeGraph() const { return m_NodeGraph; }
    bool IsGraphOutputConnected() const { return m_NodeGraph.IsOutputConnected(); }
    void PromptAddImageNodeAt(EditorNodeGraph::Vec2 graphPosition);
    bool AddImageNodeFromFile(const std::string& path, EditorNodeGraph::Vec2 graphPosition);
    bool AddCompositeImageChainFromFile(const std::string& path);
    bool AddCompositeLibraryAssetChain(const std::string& assetFileName);
    bool AddCompositeGeneratorChain(EditorNodeGraph::ImageGeneratorKind generatorKind);
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
    void AddOutputNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AutoLayoutGraph();
    void DisconnectGraphOutput();
    void SetGraphDropTargetRect(float minX, float minY, float maxX, float maxY);
    void SetGraphViewTransform(float originX, float originY, float panX, float panY, float zoom);
    void ApplyGraphAutoFocusFrame(float canvasWidth, float canvasHeight, float& panX, float& panY, float& zoom);
    void RequestGraphNodeAutoFocus(
        int nodeId,
        const EditorNodeGraph::Vec2& nodePosition,
        const EditorNodeGraph::Vec2& nodeSize,
        float currentPanX,
        float currentPanY,
        float currentZoom);
    void CancelGraphAutoFocusTracking();
    void ClearGraphAutoFocusIfTrackedNode(int nodeId);
    bool IsScreenPointOverGraph(float x, float y) const;
    bool HandleGraphFileDrop(const std::string& path, float screenX, float screenY);
    bool HandleGraphFileDrop(const std::vector<std::string>& paths, float screenX, float screenY);
    std::vector<unsigned char> GetScopePixelsForNode(int nodeId, int& outW, int& outH);
    std::vector<unsigned char> GetPreviewPixelsForNode(int nodeId, int& outW, int& outH);
    void RenderGraphScopeNode(EditorNodeGraph::ScopeKind scopeKind, int sourceNodeId);
    void MarkRenderDirty(int touchedNodeId = -1);
    bool IsEditorRenderBusy() const { return m_RenderWorker.IsBusy() || m_RenderPending; }
    std::uint64_t GetRenderRevision() const { return m_RenderRevision; }
    std::uint64_t GetPreviewNodeRevision(int previewNodeId) const;
    std::uint64_t GetScopeNodeRevision(int sourceNodeId) const;
    const GraphPreviewPixels* GetCachedPreviewPixelsForNode(int previewNodeId) const;

    // Persistence & Serialization
    nlohmann::json SerializePipeline();
    void DeserializePipeline(const nlohmann::json& j);
    void LoadSourceFromPixels(const unsigned char* data, int w, int h, int ch);
    bool ApplyLoadedProject(const LoadedProjectData& projectData);
    void RequestLoadSourceImage(const std::string& path);
    bool ExportImage(const std::string& path);
    bool RequestExportImage(const std::string& path);
    bool RequestExportProject(const std::string& path);
    bool BuildProjectDocumentForSave(
        const std::string& displayName,
        StackBinaryFormat::ProjectDocument& outDocument);

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
    void RequestNewProject();
    bool HasProjectContent() const;

    Async::TaskState GetSourceLoadTaskState() const { return m_SourceLoadTaskState; }
    const std::string& GetSourceLoadStatusText() const { return m_SourceLoadStatusText; }
    bool IsSourceLoadBusy() const { return Async::IsBusy(m_SourceLoadTaskState); }
    Async::TaskState GetGraphDropImportTaskState() const { return m_GraphDropImportTaskState; }
    const std::string& GetGraphDropImportStatusText() const { return m_GraphDropImportStatusText; }
    bool IsGraphDropImportBusy() const { return Async::IsBusy(m_GraphDropImportTaskState); }

    Async::TaskState GetExportTaskState() const { return m_ExportTaskState; }
    const std::string& GetExportStatusText() const { return m_ExportStatusText; }
    bool IsExportBusy() const { return Async::IsBusy(m_ExportTaskState); }

    CanvasToolKind GetCanvasToolKind() const { return m_CanvasToolKind; }
    int GetCanvasToolOwnerNodeId() const { return m_CanvasToolOwnerNodeId; }
    const std::string& GetCanvasToolStatusText() const { return m_CanvasToolStatusText; }
    bool HasActiveCanvasTool() const { return m_CanvasToolKind != CanvasToolKind::None; }
    bool IsCanvasToolActiveForNode(int nodeId, CanvasToolKind kind) const {
        return m_CanvasToolKind == kind && m_CanvasToolOwnerNodeId == nodeId;
    }
    void BeginCanvasColorPick(int ownerNodeId, const std::string& statusText, std::function<void(float, float, float)> callback);
    void CancelCanvasTool();
    void OnCanvasColorPicked(float r, float g, float b);
    bool IsPickingColor() const { return m_IsPickingColor; }
    void SetPickingColor(bool picking, std::function<void(float, float, float)> callback = nullptr) {
        if (picking) {
            BeginCanvasColorPick(-1, "Click canvas to sample color", std::move(callback));
        } else {
            CancelCanvasTool();
        }
    }
    void OnColorPicked(float r, float g, float b) {
        OnCanvasColorPicked(r, g, b);
    }
    ImVec4 GetWorkspaceBaseColor() const;
    bool CanConsumeEditorCommandKeys() const;
    ViewportMode GetViewportMode() const;
    bool IsCompositeViewportMode() const { return GetViewportMode() == ViewportMode::CompositeCanvas; }
    int GetCompletedChainCount() const;
    int GetConnectedOutputCount() const;
    const std::vector<CompositeSceneItem>& GetCompositeSceneItems() const { return m_CompositeSceneItems; }
    const std::vector<int>& GetCompositeZOrder() const { return m_CompositeZOrder; }
    int GetCompositeSelectedOutputNodeId() const { return m_CompositeSelectedOutputNodeId; }
    float GetCompositeViewZoom() const { return m_CompositeViewZoom; }
    float GetCompositeViewPanX() const { return m_CompositeViewPanX; }
    float GetCompositeViewPanY() const { return m_CompositeViewPanY; }
    ImVec2 GetLastCompositeCanvasSize() const { return m_LastCompositeCanvasSize; }
    void SetLastCompositeCanvasSize(const ImVec2& size) { m_LastCompositeCanvasSize = size; }
    void SetCompositeViewZoom(float zoom) { m_CompositeViewZoom = zoom; }
    void SetCompositeViewPan(float panX, float panY) { m_CompositeViewPanX = panX; m_CompositeViewPanY = panY; }
    void AddCompositeViewPan(float deltaX, float deltaY) { m_CompositeViewPanX += deltaX; m_CompositeViewPanY += deltaY; }
    void SetCompositeSelectedOutputNodeId(int outputNodeId) { m_CompositeSelectedOutputNodeId = outputNodeId; }
    void ClearCompositeSelection() { m_CompositeSelectedOutputNodeId = -1; }
    CompositeSceneItem* FindCompositeSceneItem(int outputNodeId);
    const CompositeSceneItem* FindCompositeSceneItem(int outputNodeId) const;
    void EnsureCompositeSceneState(const ImVec2& canvasSize);
    bool ReorderCompositeOutputBefore(int draggedOutputNodeId, int targetOutputNodeId);
    bool MoveCompositeOutputToIndex(int outputNodeId, int targetIndex);
    bool HasCompositeNode() const;
    void EnsureCompositeNode();
    bool HasExportBoundsSettingsNode() const;
    void EnsureExportBoundsSettingsNode();
    std::vector<unsigned char> GetCompositePixelsForOutputNode(int outputNodeId, int& outW, int& outH);
    void BeginCompositeMove(int outputNodeId, const ImVec2& mouseWorld);
    void UpdateCompositeMove(const ImVec2& mouseWorld);
    void EndCompositeMove();
    bool IsCompositeMoveActive() const { return m_CompositeMoveActive; }
    void BeginCompositePan(const ImVec2& mouseScreen);
    void UpdateCompositePan(const ImVec2& mouseScreen);
    void EndCompositePan();
    bool IsCompositePanActive() const { return m_CompositePanActive; }
    const CompositeExportSettings& GetCompositeExportSettings() const { return m_CompositeExportSettings; }
    CompositeExportSettings& GetMutableCompositeExportSettings() { return m_CompositeExportSettings; }
    const CompositeSnapSettings& GetCompositeSnapSettings() const { return m_CompositeSnapSettings; }
    CompositeSnapSettings& GetMutableCompositeSnapSettings() { return m_CompositeSnapSettings; }
    CompositeSnapModePreset GetCompositeSnapModePreset() const;
    void ApplyCompositeSnapModePreset(CompositeSnapModePreset preset);
    CompositeResizeMode GetCompositeResizeMode() const { return m_CompositeResizeMode; }
    CompositeScaleOriginMode GetCompositeScaleOriginMode() const { return m_CompositeScaleOriginMode; }
    void SetCompositeResizeMode(CompositeResizeMode mode) { m_CompositeResizeMode = mode; }
    void ToggleCompositeScaleOriginMode();
    bool IsCompositeExportBoundsEditMode() const { return m_CompositeExportBoundsEditMode; }
    void SetCompositeExportBoundsEditMode(bool enabled) { m_CompositeExportBoundsEditMode = enabled; }
    void ToggleCompositeExportBoundsEditMode() { m_CompositeExportBoundsEditMode = !m_CompositeExportBoundsEditMode; }
    float GetCurrentCompositeExportAspectRatio() const;
    void UpdateCompositeCustomExportAspectFromBounds();
    void SyncCompositeExportResolutionFromWidth();
    void SyncCompositeExportResolutionFromHeight();
    bool TryGetCompositeAutoExportBounds(CompositeFloatRect& outBounds) const;
    CompositeFloatRect GetCompositeViewWorldRect(const ImVec2& canvasSize) const;
    void UseCompositeViewAsExportBounds(const ImVec2& canvasSize);
    bool BuildCompositeExportRaster(std::vector<unsigned char>& outPixels, int& outW, int& outH);
    void ClampCompositeViewPanToContent(const ImVec2& canvasSize);
    void RefreshGraphLayerMetadata();

private:
    struct PendingGraphDropImportRequest {
        std::vector<std::string> paths;
        EditorNodeGraph::Vec2 sourcePosition;
    };

    struct CachedCompositeChainState {
        EditorNodeGraph::CompletedChainInfo info;
        std::size_t fingerprint = 0;
        std::string label;
    };

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
    struct GraphAutoFocusState {
        bool trackingActive = false;
        bool animationActive = false;
        int nodeId = -1;
        EditorNodeGraph::Vec2 nodePosition {};
        EditorNodeGraph::Vec2 nodeSize {};
        float startPanX = 0.0f;
        float startPanY = 0.0f;
        float startZoom = 1.0f;
        float targetPanX = 0.0f;
        float targetPanY = 0.0f;
        float targetZoom = 1.0f;
        float lastCanvasWidth = 0.0f;
        float lastCanvasHeight = 0.0f;
        double animationStartTime = 0.0;
    };
    GraphAutoFocusState m_GraphAutoFocus;

    std::uint64_t m_SourceLoadGeneration = 0;
    Async::TaskState m_SourceLoadTaskState = Async::TaskState::Idle;
    std::string m_SourceLoadStatusText;
    std::uint64_t m_GraphDropImportGeneration = 0;
    Async::TaskState m_GraphDropImportTaskState = Async::TaskState::Idle;
    std::string m_GraphDropImportStatusText;
    std::vector<PendingGraphDropImportRequest> m_PendingGraphDropImports;

    std::uint64_t m_ExportGeneration = 0;
    Async::TaskState m_ExportTaskState = Async::TaskState::Idle;
    std::string m_ExportStatusText;
    bool m_ShowNewProjectPrompt = false;
    bool m_ShowNewProjectDiscardConfirm = false;

    CanvasToolKind m_CanvasToolKind = CanvasToolKind::None;
    int m_CanvasToolOwnerNodeId = -1;
    std::string m_CanvasToolStatusText;
    bool m_IsPickingColor = false;
    std::function<void(float, float, float)> m_ColorPickerCallback;
    bool m_RenderWorkerAvailable = false;
    bool m_RenderDirty = true;
    bool m_RenderPending = false;
    std::uint64_t m_RenderGeneration = 0;
    std::uint64_t m_LastCompletedRenderGeneration = 0;
    std::uint64_t m_RenderRevision = 1;
    std::uint64_t m_LastSubmittedRenderRevision = 0;
    double m_LastRenderDirtyTime = 0.0;
    std::uint64_t m_NodeDirtyGenerationCounter = 1;
    std::unordered_map<int, std::uint64_t> m_NodeDirtyGenerations;
    float m_LeftPaneWidth = 0.0f;
    bool m_DraggingSplitHandle = false;
    bool m_SplitHandlePressed = false;
    bool m_SplitHandleMoved = false;
    bool m_SplitAutoAnimating = false;
    float m_SplitAutoAnimFrom = 0.0f;
    float m_SplitAutoAnimTo = 0.0f;
    double m_SplitAutoAnimStartTime = 0.0;
    ViewportMode m_LastViewportMode = ViewportMode::SingleOutputPreview;
    enum class CompositeEdgeSnapMode {
        None,
        GraphOnly,
        ViewportOnly
    };
    CompositeEdgeSnapMode m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
    CompositeEdgeSnapMode m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
    RenderPipeline m_CompositePreviewPipeline;
    std::vector<CompositeSceneItem> m_CompositeSceneItems;
    std::vector<int> m_CompositeZOrder;
    mutable std::vector<CachedCompositeChainState> m_CachedCompletedChains;
    std::unordered_map<int, std::size_t> m_CachedCompositeFingerprints;
    std::unordered_map<int, std::string> m_CachedCompositeLabels;
    std::unordered_map<int, std::uint64_t> m_CompositeOutputDirtyGenerations;
    std::unordered_map<int, std::uint64_t> m_CompositeOutputRequestedGenerations;
    std::unordered_map<int, std::uint64_t> m_CompositeOutputCompletedGenerations;
    std::uint64_t m_CompositeDirtyGenerationCounter = 1;
    mutable std::uint64_t m_CachedCompletedChainsStructureRevision = 0;
    mutable int m_CachedConnectedOutputCount = 0;
    std::uint64_t m_CachedCompositeMetadataStructureRevision = 0;
    std::uint64_t m_CachedCompositeMetadataRenderRevision = 0;
    std::uint64_t m_LastCompositeSceneSyncStructureRevision = 0;
    std::uint64_t m_LastCompositeSceneSyncRenderRevision = 0;
    ImVec2 m_LastCompositeSceneSyncCanvasSize = ImVec2(-1.0f, -1.0f);
    int m_CompositeSelectedOutputNodeId = -1;
    float m_CompositeViewZoom = 1.0f;
    float m_CompositeViewPanX = 0.0f;
    float m_CompositeViewPanY = 0.0f;
    bool m_CompositeMoveActive = false;
    int m_CompositeDragOutputNodeId = -1;
    ImVec2 m_CompositeDragStartMouseWorld = ImVec2(0.0f, 0.0f);
    ImVec2 m_CompositeDragStartPosition = ImVec2(0.0f, 0.0f);
    bool m_CompositePanActive = false;
    ImVec2 m_CompositePanStartMouseScreen = ImVec2(0.0f, 0.0f);
    float m_CompositePanStartX = 0.0f;
    float m_CompositePanStartY = 0.0f;
    ImVec2 m_LastCompositeCanvasSize = ImVec2(0.0f, 0.0f);
    CompositeExportSettings m_CompositeExportSettings;
    CompositeSnapSettings m_CompositeSnapSettings;
    CompositeResizeMode m_CompositeResizeMode = CompositeResizeMode::Scale;
    CompositeScaleOriginMode m_CompositeScaleOriginMode = CompositeScaleOriginMode::Opposite;
    bool m_CompositeExportBoundsEditMode = false;
    std::vector<PersistedCompositeSceneEntry> m_PersistedCompositeSceneEntries;
    mutable std::unordered_map<int, std::uint64_t> m_PreviewDisplayedRevisions;
    mutable std::unordered_map<int, std::uint64_t> m_ScopeDisplayedRevisions;
    std::unordered_map<int, GraphPreviewPixels> m_PreviewPixelCache;
    std::unordered_map<int, std::uint64_t> m_PreviewRequestedGenerations;
    std::unordered_map<int, std::uint64_t> m_PreviewCompletedGenerations;

    void ApplyGraphLayerOrder();
    std::vector<std::shared_ptr<LayerBase>> BuildGraphRenderLayers() const;
    std::vector<RenderLayerStep> BuildGraphRenderSteps() const;
    std::vector<RenderMaskSource> BuildGraphRenderMasks() const;
    RenderGraphSnapshot BuildGraphSnapshot() const;
    EditorRenderWorker::Snapshot BuildRenderSnapshot(std::uint64_t generation) const;
    std::vector<EditorRenderWorker::CompositeOutputRequest> BuildCompositeOutputRequests();
    std::vector<EditorRenderWorker::PreviewRequest> BuildPreviewRequests();
    void ConsumeRenderWorkerResults();
    void SubmitRenderIfReady();
    void RefreshCompletedChainCacheIfNeeded() const;
    void RefreshCompositeMetadataCacheIfNeeded();
    void MarkDownstreamNodesDirty(int touchedNodeId);
    void MarkAllRenderNodesDirty();
    void MarkCompositeOutputsDirty(const std::vector<int>& outputNodeIds);
    void PruneCompositeDirtyState();
    bool HasPendingPreviewRefreshes() const;
    bool CanRefreshPreviewLikeNodes() const;
    std::uint64_t GetNodeDirtyGeneration(int nodeId) const;
    void ClearCompositeRuntimeState();
    void ClearCompositeSceneTextures();
    void ClearPersistedCompositeState();
    const PersistedCompositeSceneEntry* FindPersistedCompositeSceneEntry(int outputNodeId) const;
    nlohmann::json SerializeCompositePersistence() const;
    void DeserializeCompositePersistence(const nlohmann::json& pipelineData);
    void SyncCompositeSceneItems(const ImVec2& canvasSize);
    void HandleViewportModeTransition(ViewportMode previousMode, ViewportMode currentMode);
    void EnterSingleOutputPreviewMode();
    void ClearCompositeTransientInteractionState();
    void TogglePartialSplitTargets(float workspaceWidth, float minLeftWidth, float maxLeftWidth, bool compositeViewportMode);
    bool AddImageNodeFromPayload(EditorNodeGraph::ImagePayload payload, EditorNodeGraph::Vec2 graphPosition);
    bool StartGraphImageChainImport(std::vector<std::string> paths, EditorNodeGraph::Vec2 sourcePosition);
    bool RequestGraphImageChainImports(const std::vector<std::string>& paths, EditorNodeGraph::Vec2 sourcePosition);
    bool AddGraphImageChainFromFile(const std::string& path, EditorNodeGraph::Vec2 sourcePosition);
    bool AddGraphImageChainFromPayload(EditorNodeGraph::ImagePayload payload, EditorNodeGraph::Vec2 sourcePosition);
    void MoveCompositeOutputToFront(int outputNodeId);
    std::pair<EditorNodeGraph::Vec2, EditorNodeGraph::Vec2> BuildCompositeChainPlacement() const;
    std::size_t BuildCompositeChainFingerprint(const EditorNodeGraph::CompletedChainInfo& chain) const;
    std::string BuildCompositeChainLabel(const EditorNodeGraph::CompletedChainInfo& chain) const;
    std::string BuildCompositeChainLabel(int outputNodeId) const;
    bool CompletedChainSourceUsesScalableGenerator(int outputNodeId) const;
    bool CompletedChainSourceKeepsFullRasterFrame(int outputNodeId) const;
    void ResetToBlankProject();
    void RenderProjectLifecyclePopups();
};
