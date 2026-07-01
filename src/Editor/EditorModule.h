#pragma once

namespace StackAppearance {
    class AppearanceManager;
}

#include "Async/TaskState.h"
#include "Editor/EditorModuleTypes.h"
#include "Editor/LoadedProjectData.h"
#include "Layers/LayerBase.h"
#include "LayerRegistry.h"
#include "EditorRenderWorker.h"
#include "NodeGraph/EditorNodeGraph.h"
#include "UI/EditorSidebar.h"
#include "UI/EditorViewport.h"
#include "Raw/RawImageAnalysis.h"
#include "Raw/RawWorkspace.h"
#include "Raw/RawWorkspaceManagedGraph.h"
#include "Renderer/RenderPipeline.h"
#include "Persistence/StackBinaryFormat.h"
#include "Utils/UiNotifications.h"
#include <imgui.h>
#include <atomic>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "UI/EditorScopes.h"

struct GLFWwindow;
enum class ToneCurveSamplingBasis : int;
enum class ToneCurveScopeMaskAction : int;
class ToneCurveLayer;

// The main coordinator for the Editor context.
class EditorModule {
public:
    using DevelopCandidateFeedbackGateDecision =
        Stack::EditorModuleTypes::DevelopCandidateFeedbackGateDecision;
    using ViewportMode = Stack::EditorModuleTypes::ViewportMode;
    using CanvasToolKind = Stack::EditorModuleTypes::CanvasToolKind;
    using CompositeExportBoundsMode = Stack::EditorModuleTypes::CompositeExportBoundsMode;
    using CompositeExportBackgroundMode = Stack::EditorModuleTypes::CompositeExportBackgroundMode;
    using CompositeExportAspectPreset = Stack::EditorModuleTypes::CompositeExportAspectPreset;
    using CompositeSnapModePreset = Stack::EditorModuleTypes::CompositeSnapModePreset;
    using CompositeResizeMode = Stack::EditorModuleTypes::CompositeResizeMode;
    using CompositeScaleOriginMode = Stack::EditorModuleTypes::CompositeScaleOriginMode;
    using CompositeFloatRect = Stack::EditorModuleTypes::CompositeFloatRect;
    using CompositeExportSettings = Stack::EditorModuleTypes::CompositeExportSettings;
    using CompositeSnapSettings = Stack::EditorModuleTypes::CompositeSnapSettings;
    using CompositeSceneItem = Stack::EditorModuleTypes::CompositeSceneItem;
    using GraphPreviewPixels = Stack::EditorModuleTypes::GraphPreviewPixels;
    using GraphPerformanceStats = Stack::EditorModuleTypes::GraphPerformanceStats;
    using HdrMergeRenderState = Stack::EditorModuleTypes::HdrMergeRenderState;
    using HdrMergeInputSummary = Stack::EditorModuleTypes::HdrMergeInputSummary;
    using HdrMergeNodeStatus = Stack::EditorModuleTypes::HdrMergeNodeStatus;
    using HdrMergeConnectionTopology = Stack::EditorModuleTypes::HdrMergeConnectionTopology;
    using PersistedCompositeSceneEntry = Stack::EditorModuleTypes::PersistedCompositeSceneEntry;
    using LoadedProjectData = EditorLoadedProjectData;
    using NodeBrowserThumbnailView = Stack::EditorModuleTypes::NodeBrowserThumbnailView;
    using RawAutoValueOwner = Stack::EditorModuleTypes::RawAutoValueOwner;
    using RawWorkspaceAutoBaseUiState = Stack::EditorModuleTypes::RawWorkspaceAutoBaseUiState;
    using RawWorkspaceLayoutUiState = Stack::EditorModuleTypes::RawWorkspaceLayoutUiState;
    enum class RawWorkspacePreviewOutputKind {
        None,
        SingleTexture,
        Tiled
    };

    EditorModule();
    ~EditorModule();

    void Initialize(GLFWwindow* sharedWindow = nullptr, StackAppearance::AppearanceManager* appearance = nullptr);
    void RequestWorkerShutdownForAppClose();
    bool IsWorkerShutdownReadyForAppClose() const;
    void Shutdown();

    // Called every frame by the AppShell
    void RenderUI();
    void RenderRawWorkspaceUI();
    void ReleaseRawWorkspacePreviewForTabChange();
    bool FocusRawWorkspace();
    bool FlushActiveRawWorkspaceProjectIfDirty();
    void RenderDetachedPreviewWindow();
    void BeginLibraryLoadReveal();
    void PumpNonRenderingWork(double projectApplyBudgetMs = 2.5);
    bool BeginDeferredLoadedProjectApply(std::shared_ptr<LoadedProjectData> projectData);
    bool IsDeferredLoadedProjectApplyActive() const;
    bool HasDeferredLoadedProjectApplyFailed() const;
    bool HasDeferredLoadedProjectApplyCoreFinished() const;
    bool HasDeferredLoadedProjectFirstRenderReady() const;
    bool IsDeferredLoadedProjectReadyForReveal() const;
    const std::string& GetDeferredLoadedProjectStatusText() const;
    const char* GetDeferredLoadedProjectPhaseLabel() const;
    std::size_t GetPendingNodeBrowserThumbnailWarmCount() const;
    std::size_t GetPendingNodeBrowserThumbnailGenerationCount() const;
    void RequestToggleDetachedPreviewFullscreen();
    void ToggleDetachedPreviewFullscreen();
    void CloseDetachedPreviewFullscreen();
    bool IsDetachedPreviewActive() const { return m_DetachedPreviewActive; }
    bool IsDetachedPreviewLayoutDetached() const { return m_DetachedPreviewLayoutDetached; }
    struct DetachedPreviewNativeWindowRequest {
        GLFWwindow* window = nullptr;
        ImGuiID viewportId = 0;
        ImVec4 surfaceColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        ImU32 surfaceColorU32 = 0;
        ImU32 textColorU32 = 0;
        bool hasPlatformWindow = false;
        bool applyTheme = false;
        bool requestFocus = false;
        bool nativeShown = false;
        bool firstPresented = false;
        bool layoutDetached = false;
        int focusAttempt = 0;
        int waitFrames = 0;
    };
    bool QueryDetachedPreviewNativeWindow(DetachedPreviewNativeWindowRequest& request) const;
    void CompleteDetachedPreviewNativeWindowRequest(
        const DetachedPreviewNativeWindowRequest& request,
        bool themeApplied,
        bool focused);
    void MarkDetachedPreviewNativeWindowShown(
        const DetachedPreviewNativeWindowRequest& request,
        bool focused);
    void MarkDetachedPreviewPlatformPresented(GLFWwindow* window);

    RenderPipeline& GetPipeline() { return m_Pipeline; }
    std::vector<std::shared_ptr<LayerBase>>& GetLayers() { return m_Layers; }
    StackAppearance::AppearanceManager* GetAppearance() { return m_Appearance; }

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
    bool NodeUsesSidebarOnlyComplexEditor(int nodeId) const;
    bool NodeHasDedicatedComplexEditor(int nodeId) const;
    NodeSurfaceSpec GetLayerNodeSurfaceSpec(int layerIndex) const;
    NodeSurfaceSpec GetNodeSurfaceSpec(int nodeId) const;

    EditorNodeGraph::Graph& GetNodeGraph();
    const EditorNodeGraph::Graph& GetNodeGraph() const;
    bool IsGraphOutputConnected() const;
    void PromptAddImageNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void RequestPromptAddImageNodeAt(EditorNodeGraph::Vec2 graphPosition);
    bool AddImageNodeFromFile(const std::string& path, EditorNodeGraph::Vec2 graphPosition);
    bool AddRawSourceNodeFromFile(const std::string& path, EditorNodeGraph::Vec2 graphPosition);
    bool LoadLutNodeFromFile(int nodeId, const std::string& path, bool notifyOnFailure = true);
    bool ReloadLutNodeFromSourcePath(int nodeId, bool notifyOnFailure = true);
    bool ClearLutNodeData(int nodeId);
    bool AddCompositeImageChainFromFile(const std::string& path);
    bool AddCompositeLibraryAssetChain(const std::string& assetFileName);
    bool AddCompositeGeneratorChain(EditorNodeGraph::ImageGeneratorKind generatorKind);
    bool AddFullRawTreeToSource(int rawSourceNodeId);
    static void NormalizeDevelopAutoGuidance(EditorNodeGraph::DevelopAutoGuidance& guidance);
    static void NormalizeDevelopSubjectImportance(EditorNodeGraph::DevelopSubjectImportanceMap& importance);

    using DevelopSubjectViewportRegion = Stack::EditorModuleTypes::DevelopSubjectViewportRegion;
    using DevelopSubjectViewportStrokePoint = Stack::EditorModuleTypes::DevelopSubjectViewportStrokePoint;
    using DevelopSubjectViewportStroke = Stack::EditorModuleTypes::DevelopSubjectViewportStroke;
    using DevelopSubjectViewportMapCell = Stack::EditorModuleTypes::DevelopSubjectViewportMapCell;
    using DevelopSubjectViewportState = Stack::EditorModuleTypes::DevelopSubjectViewportState;

    bool GetDevelopSubjectImportanceViewportState(DevelopSubjectViewportState& outState) const;
    bool SetDevelopSubjectImportanceActiveRegion(int nodeId, int regionId);
    bool UpdateDevelopSubjectImportanceRegionFromViewport(
        int nodeId,
        int regionId,
        float centerX,
        float centerY,
        float radiusX,
        float radiusY);
    int BeginDevelopSubjectImportanceBrushStroke(int nodeId, float x, float y);
    bool AppendDevelopSubjectImportanceBrushStroke(int nodeId, int strokeId, float x, float y);
    bool EndDevelopSubjectImportanceBrushStroke(int nodeId, int strokeId);

    static void ApplyDevelopAutoSolve(
        EditorNodeGraph::RawDevelopPayload& payload,
        const Raw::RawMetadata& metadata,
        bool queueToneCalibration = true,
        bool rewriteRawSettings = true);
    static std::string ResolveDevelopRenderedRefineIntentForValidation(
        const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
        EditorNodeGraph::DevelopAutoIntent intent,
        std::string& outReason);
    static std::string ClassifyDevelopRenderedCandidateDamageForValidation(
        const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
        EditorNodeGraph::DevelopAutoIntent intent);
    static float ScoreDevelopRenderedCandidateRelativeToSelectedForValidation(
        const EditorRenderWorker::DevelopCandidateRenderMetrics& candidateMetrics,
        float candidateStandaloneScore,
        const EditorRenderWorker::DevelopCandidateRenderMetrics& selectedMetrics,
        float selectedScore,
        const std::string& activeRefineIntent,
        std::string& outStatus,
        std::string& outRepairMetric,
        float& outMetricDistance,
        float& outRepairDelta,
        float& outRepairBonus,
        float& outRegressionPenalty);
    static std::string ClassifyDevelopRenderedStageBoundaryForValidation(
        const EditorRenderWorker::DevelopCandidateRenderMetrics& selectedFinalMetrics,
        const EditorRenderWorker::DevelopCandidateRenderMetrics& bestFinalMetrics,
        bool finalMetricsValid,
        const EditorRenderWorker::DevelopCandidateRenderMetrics& selectedPreFinishMetrics,
        const EditorRenderWorker::DevelopCandidateRenderMetrics& bestPreFinishMetrics,
        bool preFinishMetricsValid,
        float& outFinalDistance,
        float& outPreFinishDistance);
    static bool ShouldTreatDevelopRenderedCandidateAsDuplicateForValidation(
        const EditorRenderWorker::DevelopCandidateRenderMetrics& candidateFinalMetrics,
        const EditorRenderWorker::DevelopCandidateRenderMetrics& representativeFinalMetrics,
        bool candidatePreFinishValid,
        const EditorRenderWorker::DevelopCandidateRenderMetrics& candidatePreFinishMetrics,
        bool representativePreFinishValid,
        const EditorRenderWorker::DevelopCandidateRenderMetrics& representativePreFinishMetrics,
        float& outFinalDistance,
        float& outPreFinishDistance,
        bool& outPreFinishDistinct);
    static bool IsDevelopCandidateRelevantToRevisionStageForValidation(
        const std::string& candidateId,
        const std::string& revisionStage);
    static bool IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
        const std::string& candidateId,
        const std::string& refineIntent);
    static bool IsDevelopRenderedFeedbackStopConvergedReason(
        const std::string& stopReason);
    static bool IsDevelopRenderedFeedbackStopConvergedForValidation(
        const std::string& stopReason);
    static bool CanScheduleDevelopCandidateRenderRequestForValidation(
        std::size_t totalRequestCount,
        std::size_t nodeRequestCount,
        std::size_t nodeRequestBudget = 4);
    static int ResolveDevelopCandidateMetricReadbackMaxDimensionForValidation(
        int sourceWidth,
        int sourceHeight);
    static bool ShouldDeferDevelopCandidateRenderRequestForValidation(
        double lastInteractionTime,
        double now);
    static double DevelopCandidateFeedbackQuietSecondsForValidation();
    static double DevelopCandidateFeedbackQuietRemainingSecondsForValidation(
        double lastInteractionTime,
        double now);
    static DevelopCandidateFeedbackGateDecision ClassifyDevelopCandidateFeedbackGateForValidation(
        std::uint64_t resultInteractionSerial,
        std::uint64_t currentInteractionSerial,
        double lastInteractionTime,
        double now);
    static std::size_t ResolveDevelopAdaptiveRenderBudgetForValidation(
        const nlohmann::json& toneJson,
        std::uint64_t solveFingerprint,
        std::uint64_t renderedFingerprint,
        std::size_t candidateCount,
        const std::string& activeRevisionStage,
        const std::string& activeRefineIntent,
        std::string& outReason,
        bool& outExpanded,
        bool* outNarrowed = nullptr);
    static std::string ClassifyDevelopCandidateStageCacheForValidation(
        const std::string& candidateRevisionStage,
        bool rawBaseCacheHit,
        bool preFinishCacheHit,
        bool& outExpectationMet,
        std::string& outExpectedBoundary,
        std::string& outValidationStatus);
    static int ClassifyDevelopCandidateStageScheduleForValidation(
        const std::string& candidateRevisionStage,
        bool selectedCandidate,
        std::string& outExpectedDirtyBoundary,
        std::string& outReason);
    static RenderGraphRawDevelopPayload BuildDevelopCandidateRenderPayloadForValidation(
        RenderGraphRawDevelopPayload payload,
        const EditorNodeGraph::DevelopAutoGuidance& currentGuidance,
        const EditorNodeGraph::DevelopAutoGuidance& candidateGuidance,
        const std::string& candidateId,
        EditorNodeGraph::DevelopAutoIntent intent);
    bool ConvertRawDetailFusionToHybrid(int fusionNodeId);
    bool SplitLayerNodeIntoChannels(int layerNodeId);
    bool SplitImageAverageNodeIntoChannelAverages(int dataMathNodeId);
    bool ToggleOutputNodeEnabled(int outputNodeId);
    bool ConnectGraphImageNode(int nodeId);
    bool ConnectGraphNodes(int fromNodeId, int toNodeId, std::string* errorMessage = nullptr);
    bool RotateImageNode(int nodeId, int quarterTurnsClockwise);
    int FindDirectDownstreamToneCurveNode(int sourceNodeId) const;
    int FindNearestDownstreamToneCurveNode(int sourceNodeId) const;
    bool RawDevelopNodeUsesIntegratedTone(int nodeId) const;
    bool CanAbsorbDirectDownstreamToneFinishIntoDevelop(int sourceNodeId, std::string* reason = nullptr) const;
    bool SelectOrCreateToneFinishAfterNode(int sourceNodeId);
    bool AbsorbDirectDownstreamToneFinishIntoDevelop(int sourceNodeId);
    int FindNearestUpstreamRawDevelopNode(int sourceNodeId) const;
    bool SelectUpstreamDevelopForToneNode(int toneNodeId);
    bool ConnectGraphSockets(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId, std::string* errorMessage = nullptr);
    bool RemoveGraphLink(int fromNodeId, int toNodeId);
    bool RemoveGraphLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId);
    bool DeleteSelectedGraphLink();
    bool RemoveGraphNode(int nodeId);
    bool DeleteSelectedGraphNodes();
    void AddScopeNodeAt(EditorNodeGraph::ScopeKind scopeKind, EditorNodeGraph::Vec2 graphPosition);
    void AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind maskKind, EditorNodeGraph::Vec2 graphPosition);
    void AddMaskCombineNodeAt(EditorNodeGraph::MaskCombineMode combineMode, EditorNodeGraph::Vec2 graphPosition);
    void AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind utilityKind, EditorNodeGraph::Vec2 graphPosition);
    void AddCustomMaskNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddImageToMaskNodeAt(EditorNodeGraph::ImageToMaskKind converterKind, EditorNodeGraph::Vec2 graphPosition);
    void AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind generatorKind, EditorNodeGraph::Vec2 graphPosition);
    void AddMixNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddDataMathNodeAt(EditorNodeGraph::DataMathMode mode, EditorNodeGraph::Vec2 graphPosition);
    void AddPreviewNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddChannelSplitNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddChannelCombineNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddRawDevelopmentNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddRawNeuralDenoiseNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddRawDecodeNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddRawDevelopNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddRawDetailAutoMaskNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddRawDetailFusionNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddHdrMergeNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddMfsrNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddLutNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddOutputNodeAt(EditorNodeGraph::Vec2 graphPosition);
    bool CreateToneCurveSelectionMask(
        int toneCurveNodeId,
        float low,
        float high,
        float softness,
        const std::array<float, 4>& sampleRgba,
        float sampleLuma,
        float sampleU,
        float sampleV,
        float toneSimilarity,
        float colorSimilarity,
        float regionRadius,
        float regionFeather,
        float edgeSensitivity,
        float localCoherence,
        ToneCurveScopeMaskAction action);
    void RenderRawSourceControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced);
    void RenderRawNeuralDenoiseControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced);
    void RenderRawDecodeControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced);
    void RenderRawDevelopControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced);
    void RenderRawDetailAutoMaskControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced);
    void RenderRawDetailFusionControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced);
    void RenderHdrMergeControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced);
    void RenderLutControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced);
    void RenderCustomMaskControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced);
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
    bool ProbeViewTransformInputStats(int viewTransformNodeId, RenderTextureStats& outStats) const;
    bool HasFocusedToneCurveViewportInteraction() const;
    bool IsToneCurveTargeting() const { return m_CanvasToolKind == CanvasToolKind::ToneCurveTarget; }
    void BeginToneCurveTargeting(int ownerNodeId, const std::string& statusText = "");
    void ClearToneCurveViewportProbe();
    void UpdateToneCurveViewportProbe(float u, float v);
    void BeginToneCurveViewportTargetDrag(float u, float v);
    void UpdateToneCurveViewportTargetDrag(float deltaCurveY);
    void EndToneCurveViewportTargetDrag();
    void RestoreIntegratedToneTransientState(int ownerNodeId, ToneCurveLayer& toneCurve) const;
    void StoreIntegratedToneTransientState(int ownerNodeId, const ToneCurveLayer& toneCurve) const;
    void ClearIntegratedToneTransientState(int ownerNodeId) const;
    bool OutputPathNeedsViewTransform(int outputNodeId) const;
    bool SelectedLayerInputContainsViewTransform() const;
    bool RenderLayerControlsWithDirtyTracking(EditorNodeGraph::Node& node, const std::function<void(LayerBase&)>& renderControls);
    void MarkSelectedLayerRenderDirty();
    void RenderGraphScopeNode(EditorNodeGraph::ScopeKind scopeKind, int sourceNodeId);
    void MarkRenderDirty(int touchedNodeId = -1);
    void MarkRenderRefreshDirty();
    RenderGraphSnapshot BuildGraphSnapshot() const;
    bool IsEditorRenderBusy() const { return m_RenderWorker.IsBusy() || m_RenderPending; }
    std::uint64_t GetRenderRevision() const { return m_RenderRevision; }
    const EditorRenderWorker::SharedTextureTileSet& GetViewportOutputTiles() const { return m_ViewportOutputTiles; }
    bool HasViewportOutputTiles() const {
        return m_ViewportOutputTiles.tiled && m_ViewportOutputTiles.complete && !m_ViewportOutputTiles.tiles.empty();
    }
    std::uint64_t GetPreviewNodeRevision(int previewNodeId) const;
    std::uint64_t GetScopeNodeRevision(int sourceNodeId) const;
    const GraphPreviewPixels* GetCachedPreviewPixelsForNode(int previewNodeId) const;
    HdrMergeNodeStatus GetHdrMergeNodeStatus(int nodeId) const;
    bool GetGraphPerformancePopupEnabled() const { return m_ShowGraphPerformancePopup; }
    void SetGraphPerformancePopupEnabled(bool enabled) { m_ShowGraphPerformancePopup = enabled; }
    const GraphPerformanceStats& GetGraphPerformanceStats() const { return m_GraphPerformanceStats; }

    // Persistence & Serialization
    nlohmann::json SerializePipeline();
    void DeserializePipeline(const nlohmann::json& j);
    void LoadSourceFromPixels(const unsigned char* data, int w, int h, int ch, bool loadCompositePreview = true);
    bool ApplyLoadedProject(const LoadedProjectData& projectData);
    void RequestLoadSourceImage(const std::string& path);
    bool ExportImage(const std::string& path);
    bool RequestExportImage(const std::string& path);
    bool RequestExportProject(const std::string& path);
    bool BuildProjectDocumentForSave(
        const std::string& displayName,
        StackBinaryFormat::ProjectDocument& outDocument);
    void EnsureNodeBrowserThumbnailCatalog();
    bool GetNodeBrowserThumbnailView(const std::string& previewKey, NodeBrowserThumbnailView& outView) const;
    std::vector<StackBinaryFormat::NodeBrowserThumbnailEntry> GetPersistedNodeBrowserThumbnails() const;
    bool RequestSaveCurrentProject(
        const std::string& fallbackName = "",
        std::function<void(bool)> onComplete = {});
    bool EnsureRawWorkspaceProjectForSelectedRecipeEdit(
        const Stack::RawRecipe::RawDevelopmentRecipe& recipe);
    bool ApplyRawWorkspaceRecipeEditForSelectedSource(
        const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
        bool interactionActive = false);
    bool SaveActiveRawWorkspaceProject(bool explicitSave = true);
    bool DecomposeActiveRawWorkspaceProjectToManagedGraph();
    bool ValidateActiveRawWorkspaceManagedGraph(bool transitionOnFailure = true);
    bool ApplyActiveRawWorkspaceRecipeToManagedGraph();
    bool ReadoptActiveRawWorkspaceGraphAsRecipe();
    bool RepairActiveRawWorkspaceManagedGraph();
    bool DetachActiveRawWorkspaceGraphFromRawTab();
    bool AdoptActiveRawWorkspaceGraphAsManagedRaw();

    const std::string& GetCurrentProjectName() const { return m_CurrentProjectName; }
    void SetCurrentProjectName(const std::string& name) { m_CurrentProjectName = name; }

    const std::string& GetCurrentProjectFileName() const { return m_CurrentProjectFileName; }
    void SetCurrentProjectFileName(const std::string& fileName) { m_CurrentProjectFileName = fileName; }

    bool IsDirty() const { return m_Dirty; }
    void ClearDirty() { m_Dirty = false; }
    void MarkDirty();
    bool IsRawWorkspaceProjectActive() const {
        return !m_ActiveRawWorkspaceSourceKey.empty() && !m_ActiveRawWorkspaceProjectPath.empty();
    }

    int GetSelectedLayerIndex() const { return m_SelectedLayerIndex; }
    void SetSelectedLayerIndex(int idx) { SelectLayer(idx); }
    bool ConsumeSelectedTabFocusRequest() {
        const bool requested = m_FocusSelectedTabNextRender;
        m_FocusSelectedTabNextRender = false;
        return requested;
    }
    void RequestOpenRawWorkspaceTab() { m_OpenRawWorkspaceTabRequested = true; }
    bool ConsumeOpenRawWorkspaceTabRequest() {
        const bool requested = m_OpenRawWorkspaceTabRequested;
        m_OpenRawWorkspaceTabRequested = false;
        return requested;
    }
    void RequestOpenEditorTab() { m_OpenEditorTabRequested = true; }
    bool ConsumeOpenEditorTabRequest() {
        const bool requested = m_OpenEditorTabRequested;
        m_OpenEditorTabRequested = false;
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
    bool ConsumeUiNotification(UiNotificationEvent& outEvent);
    const Stack::RawWorkspace::WorkspaceState& GetRawWorkspaceState() const { return m_RawWorkspace; }
    const Stack::RawWorkspace::WorkspaceState& GetRawWorkspaceStateForValidation() const { return m_RawWorkspace; }
    const Stack::RawWorkspace::GalleryPresentation& GetRawWorkspaceGalleryPresentation();
    void EnsureRawWorkspaceLoaded();
    void OpenRawWorkspaceFolderDialog();
    void RescanRawWorkspace();
    void ClearRawWorkspaceForUser();
    void SelectRawWorkspaceSourceForPreview(const std::string& sourceKey);
    bool IsRawWorkspaceScanBusy() const;
    bool IsRawWorkspaceThumbnailBusy() const;
    bool IsRawWorkspaceProjectLoadBusy() const {
        return m_RawWorkspacePreviewStageQueued ||
            Async::IsBusy(m_RawWorkspaceProjectLoadTaskState) ||
            (m_PendingRawWorkspaceDeferredProjectFinalize && IsDeferredLoadedProjectApplyActive());
    }
    bool IsRawWorkspaceProjectSaveBusy() const {
        return Async::IsBusy(m_RawWorkspaceProjectSaveTaskState) ||
            m_RawWorkspaceProjectSaveInFlightCount > 0;
    }
    std::string GetRawWorkspaceScanStatusText() const;
    std::string GetRawWorkspaceThumbnailStatusText() const;
    std::string GetRawWorkspaceProgramBarStatus() const;
    std::string GetRawWorkspaceProjectLoadStatusText() const {
        const std::string& deferredStatus = GetDeferredLoadedProjectStatusText();
        if (!deferredStatus.empty() && IsDeferredLoadedProjectApplyActive()) {
            return deferredStatus;
        }
        if (m_RawWorkspacePreviewStageQueued && m_RawWorkspaceProjectLoadStatusText.empty()) {
            return "Preparing RAW preview...";
        }
        return m_RawWorkspaceProjectLoadStatusText;
    }
    std::string GetRawWorkspaceProjectSaveStatusText() const {
        return m_RawWorkspaceProjectSaveStatusText;
    }
    Stack::RawWorkspace::ScanProgress GetRawWorkspaceScanProgress() const;
    Stack::RawWorkspace::ThumbnailProgress GetRawWorkspaceThumbnailProgress() const;
    void PumpRawWorkspaceThumbnailTextureUploads();
    unsigned int GetRawWorkspaceThumbnailTexture(
        const Stack::RawWorkspace::SourceRecord& source,
        int* outWidth = nullptr,
        int* outHeight = nullptr);

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

    enum class EditorSubWindow {
        NodeGraph = 0,
        ExportSettings = 1,
        ComplexNode = 2,
        Presets = 3
    };
    EditorSubWindow GetActiveSubWindow() const { return m_ActiveSubWindow; }
    int GetActiveComplexNodeId() const { return m_ActiveComplexNodeId; }
    int GetTargetComplexNodeId() const { return m_TargetComplexNodeId; }
    float GetSubWindowTransitionAlpha() const { return m_SubWindowTransitionAlpha; }
    void SwitchToSubWindow(EditorSubWindow target);
    void SwitchToComplexNodeSubWindow(int nodeId);
    void TogglePresetsSubWindow();
    void MoveCompositeOutputZOrder(int draggedOutputNodeId, int targetOutputNodeId);
    float GetLeftPanelWidthAnim() const { return m_LeftPanelWidthAnim; }
    ViewportMode GetViewportMode() const;
    bool IsCompositeViewportMode() const { return GetViewportMode() == ViewportMode::CompositeCanvas; }
    bool CanToggleActiveAutoGainMaskPreview() const;
    bool IsAutoGainMaskPreviewActive() const {
        return CanToggleActiveAutoGainMaskPreview() && m_AutoGainMaskPreviewNodeId == m_ActiveComplexNodeId;
    }
    bool HasActiveCustomMaskOverlay() const;
    const EditorNodeGraph::CustomMaskPayload* GetActiveCustomMaskPayload() const;
    float SampleCustomMaskForPreview(const EditorNodeGraph::CustomMaskPayload& payload, float u, float v) const;
    void ToggleActiveAutoGainMaskPreview();
    void ClearAutoGainMaskPreview();
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
    void SetCompositeSelectedOutputNodeId(int outputNodeId) {
        m_CompositeSelectedOutputNodeId = outputNodeId;
        if (outputNodeId > 0) {
            SelectGraphNode(outputNodeId);
        }
    }
    void ClearCompositeSelection();
    CompositeSceneItem* FindCompositeSceneItem(int outputNodeId);
    const CompositeSceneItem* FindCompositeSceneItem(int outputNodeId) const;
    void EnsureCompositeSceneState(const ImVec2& canvasSize);
    bool ReorderCompositeOutputBefore(int draggedOutputNodeId, int targetOutputNodeId);
    bool MoveCompositeOutputToIndex(int outputNodeId, int targetIndex);
    bool HasCompositeNode() const;
    void EnsureCompositeNode();
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
    bool BuildSingleOutputExportRaster(std::vector<unsigned char>& outPixels, int& outW, int& outH) const;
    void ClampCompositeViewPanToContent(const ImVec2& canvasSize);
    void RefreshGraphLayerMetadata();
    float GetNodesPanelWidthAnim() const { return m_NodesPanelWidthAnim; }
    bool AddGeneratedLutNodeFromPayload(EditorNodeGraph::LutPayload payload);

private:
    using PendingGraphDropImportRequest = Stack::EditorModuleTypes::PendingGraphDropImportRequest;
    using CachedCompositeChainState = Stack::EditorModuleTypes::CachedCompositeChainState;
    using ToneCurveViewportInteractionCache = Stack::EditorModuleTypes::ToneCurveViewportInteractionCache;
    using DevelopAutoGuidanceDraftState = Stack::EditorModuleTypes::DevelopAutoGuidanceDraftState;
    using RawDevelopExposureDraftState = Stack::EditorModuleTypes::RawDevelopExposureDraftState;
    using NodeBrowserThumbnailRuntimeEntry = Stack::EditorModuleTypes::NodeBrowserThumbnailRuntimeEntry;
    using NodeBrowserPreviewRequestMeta = Stack::EditorModuleTypes::NodeBrowserPreviewRequestMeta;
    using NodeBrowserPreviewSeed = Stack::EditorModuleTypes::NodeBrowserPreviewSeed;

    enum class ManagedRawGraphMutationConfirmAction {
        None,
        Connect,
        RemoveLink,
        RemoveNode,
        RemoveNodes
    };

    struct ManagedRawGraphMutationConfirmState {
        ManagedRawGraphMutationConfirmAction action = ManagedRawGraphMutationConfirmAction::None;
        bool openPopup = false;
        int fromNodeId = 0;
        std::string fromSocketId;
        int toNodeId = 0;
        std::string toSocketId;
        int nodeId = 0;
        std::vector<int> nodeIds;
        Stack::RawWorkspace::ManagedRawGraphMutationWarning warning;
    };

    struct RawWorkspaceScanSnapshot {
        Async::TaskState state = Async::TaskState::Idle;
        std::uint64_t generation = 0;
        Stack::RawWorkspace::ScanProgress progress;
        std::string statusText;
        std::string errorMessage;
    };

    struct RawWorkspaceThumbnailSnapshot {
        Async::TaskState state = Async::TaskState::Idle;
        std::uint64_t generation = 0;
        Stack::RawWorkspace::ThumbnailProgress progress;
        std::string statusText;
        std::string errorMessage;
    };

    struct RawWorkspaceThumbnailTexture {
        unsigned int texture = 0;
        std::filesystem::path absolutePath;
        Stack::RawWorkspace::ThumbnailStatus status = Stack::RawWorkspace::ThumbnailStatus::Unknown;
        int width = 0;
        int height = 0;
        Async::TaskState decodeState = Async::TaskState::Idle;
        std::uint64_t requestGeneration = 0;
        bool uploadPending = false;
        bool uploadQueued = false;
        std::vector<unsigned char> decodedPixels;
        int decodedWidth = 0;
        int decodedHeight = 0;
    };

    struct RawWorkspaceRecipePreviewCacheEntry {
        std::filesystem::path projectPath;
        std::int64_t projectModifiedTimeTicks = 0;
        Stack::RawRecipe::RawDevelopmentRecipe recipe;
        Stack::RawWorkspace::RawProjectMode mode = Stack::RawWorkspace::RawProjectMode::RecipeBacked;
        bool success = false;
        std::string errorMessage;
    };

    struct RawWorkspaceProjectSaveJob {
        std::filesystem::path workspaceRoot;
        std::filesystem::path projectPath;
        std::filesystem::path projectRelativePath;
        std::string sourceKey;
        std::string projectName;
        std::uint64_t sourceRevision = 0;
        Stack::RawWorkspace::RawProjectMode mode =
            Stack::RawWorkspace::RawProjectMode::RecipeBacked;
        Stack::RawWorkspace::ProjectStatus projectStatus =
            Stack::RawWorkspace::ProjectStatus::Existing;
        std::filesystem::path previousAbsolutePath;
        std::filesystem::path previousRelativePath;
        Stack::RawWorkspace::ProjectStatus previousProjectStatus =
            Stack::RawWorkspace::ProjectStatus::Unknown;
        Stack::RawWorkspace::RawProjectMode previousMode =
            Stack::RawWorkspace::RawProjectMode::RecipeBacked;
        bool previousAutosaved = false;
        bool previousDirty = false;
        StackBinaryFormat::ProjectDocument document;
    };

    EditorSidebar m_Sidebar;
    EditorViewport m_Viewport;
    EditorScopes m_Scopes;
    RenderPipeline m_Pipeline;
    EditorRenderWorker m_RenderWorker;
    EditorRenderWorker m_NodeBrowserRenderWorker;
    std::deque<EditorRenderWorker::Result> m_DeferredRenderResults;
    EditorSubWindow m_ActiveSubWindow = EditorSubWindow::NodeGraph;
    EditorSubWindow m_TargetSubWindow = EditorSubWindow::NodeGraph;
    int m_ActiveComplexNodeId = -1;
    int m_TargetComplexNodeId = -1;
    float m_SubWindowTransitionAlpha = 1.0f;
    bool m_SubWindowTransitionFadingOut = false;
    double m_LibraryLoadRevealStartTime = -1.0;
    bool m_LibraryLoadRevealPendingFirstFrame = false;
    bool m_LibraryLoadRevealLayoutPending = false;
    float m_LibraryLoadCanvasRevealAlpha = 1.0f;
    float m_LibraryLoadGraphRevealAlpha = 1.0f;
    float m_LibraryLoadToolbarRevealAlpha = 1.0f;
    unsigned int m_NodeGraphIconTexture = 0;
    unsigned int m_PresetsIconTexture = 0;
    unsigned int m_ExportIconTexture = 0;
    unsigned int m_SettingsIconTexture = 0;
    unsigned int m_BackgroundRemoverIconTexture = 0;
    unsigned int m_ColorGradeIconTexture = 0;
    bool m_LeftPanelExpanded = false;
    float m_LeftPanelWidthAnim = 0.0f;
    float m_NodesPanelWidthAnim = 0.0f;
    bool m_TexturesLoaded = false;
    std::map<int, double> m_ToolbarButtonSpawnTimes;
    double m_SpacebarPressTime = 0.0;
    bool m_SpacebarHeld = false;

    void LoadResourceTextures();
    void RenderFloatingToolbar();

    EditorNodeGraph::Graph m_NodeGraph;

    std::vector<std::shared_ptr<LayerBase>> m_Layers;
    int m_SelectedLayerIndex = -1;
    bool m_FocusSelectedTabNextRender = false;
    bool m_OpenRawWorkspaceTabRequested = false;
    bool m_OpenEditorTabRequested = false;
    float m_HoverFade = 0.0f;
    bool m_RenderOnlyUpToActive = false;
    std::string m_CurrentProjectName = "";
    std::string m_CurrentProjectFileName = "";
    bool m_Dirty = false;
    double m_LastUserActionTime = 0.0;
    double m_LastAutoSaveTime = -1.0;
    float m_GraphDropMinX = 0.0f;
    float m_GraphDropMinY = 0.0f;
    float m_GraphDropMaxX = 0.0f;
    float m_GraphDropMaxY = 0.0f;
    float m_GraphViewOriginX = 0.0f;
    float m_GraphViewOriginY = 0.0f;
    float m_GraphViewPanX = 0.0f;
    float m_GraphViewPanY = 0.0f;
    float m_GraphViewZoom = 1.0f;
    using GraphAutoFocusState = Stack::EditorModuleTypes::GraphAutoFocusState;
    GraphAutoFocusState m_GraphAutoFocus;

    std::uint64_t m_SourceLoadGeneration = 0;
    Async::TaskState m_SourceLoadTaskState = Async::TaskState::Idle;
    std::string m_SourceLoadStatusText;
    std::uint64_t m_GraphDropImportGeneration = 0;
    Async::TaskState m_GraphDropImportTaskState = Async::TaskState::Idle;
    std::string m_GraphDropImportStatusText;
    std::deque<UiNotificationEvent> m_UiNotifications;
    std::vector<PendingGraphDropImportRequest> m_PendingGraphDropImports;
    std::deque<EditorRenderWorker::SharedTextureResult> m_DeferredViewportOutputTextureReleases;
    std::deque<EditorRenderWorker::SharedTextureTileSet> m_DeferredViewportOutputTileReleases;
    Stack::RawWorkspace::WorkspaceState m_RawWorkspace;
    std::uint64_t m_RawWorkspaceScanGeneration = 0;
    mutable std::mutex m_RawWorkspaceScanMutex;
    RawWorkspaceScanSnapshot m_RawWorkspaceScanSnapshot;
    std::uint64_t m_RawWorkspaceThumbnailGeneration = 0;
    mutable std::mutex m_RawWorkspaceThumbnailMutex;
    RawWorkspaceThumbnailSnapshot m_RawWorkspaceThumbnailSnapshot;
    std::atomic<std::uint64_t> m_RawWorkspaceCatalogPersistGeneration { 0 };
    Async::TaskState m_RawWorkspaceCatalogPersistTaskState = Async::TaskState::Idle;
    bool m_RawWorkspaceCatalogPersistDirty = false;
    bool m_RawWorkspaceCatalogPersistInFlight = false;
    std::uint64_t m_RawWorkspaceCatalogPersistInFlightGeneration = 0;
    double m_RawWorkspaceCatalogPersistDirtyTime = -1.0;
    std::string m_RawWorkspaceCatalogPersistStatusText;
    std::size_t m_RawWorkspaceProjectSaveInFlightCount = 0;
    Async::TaskState m_RawWorkspaceProjectSaveTaskState = Async::TaskState::Idle;
    std::string m_RawWorkspaceProjectSaveStatusText;
    mutable std::mutex m_RawWorkspaceProjectSaveMutex;
    mutable std::mutex m_RawWorkspaceProjectFileWriteMutex;
    mutable std::mutex m_RawWorkspaceProjectSaveRevisionMutex;
    std::unordered_map<std::string, std::uint64_t> m_RawWorkspaceProjectSaveRevisions;
    std::condition_variable m_RawWorkspaceProjectSaveCv;
    std::deque<RawWorkspaceProjectSaveJob> m_RawWorkspaceProjectSaveQueue;
    std::thread m_RawWorkspaceProjectSaveWorker;
    bool m_RawWorkspaceProjectSaveWorkerStopRequested = false;
    bool m_RawWorkspaceProjectSaveWorkerBusy = false;
    std::atomic<std::uint64_t> m_RawWorkspaceAppStatePersistGeneration { 0 };
    Async::TaskState m_RawWorkspaceAppStatePersistTaskState = Async::TaskState::Idle;
    bool m_RawWorkspaceAppStatePersistDirty = false;
    bool m_RawWorkspaceAppStatePersistInFlight = false;
    std::uint64_t m_RawWorkspaceAppStatePersistInFlightGeneration = 0;
    double m_RawWorkspaceAppStatePersistDirtyTime = -1.0;
    std::string m_RawWorkspaceAppStatePersistStatusText;
    std::atomic<std::uint64_t> m_RawWorkspaceProjectLoadGeneration { 0 };
    Async::TaskState m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Idle;
    std::string m_RawWorkspaceProjectLoadSourceKey;
    std::string m_RawWorkspaceProjectLoadStatusText;
    bool m_PendingRawWorkspaceDeferredProjectFinalize = false;
    std::string m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey;
    bool m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
    std::string m_PendingRawWorkspaceOpenGraphSourceKey;
    bool m_RawWorkspaceAppStateLoaded = false;
    Stack::RawWorkspace::GalleryDisplayMode m_RawWorkspaceGalleryDisplayMode =
        Stack::RawWorkspace::GalleryDisplayMode::Grid;
    bool m_RawWorkspaceGalleryWindowOpen = false;
    RawWorkspaceLayoutUiState m_RawWorkspaceLayoutUi;
    std::uint64_t m_RawWorkspaceGalleryRevision = 1;
    std::uint64_t m_RawWorkspaceGalleryPresentationRevision = 0;
    Stack::RawWorkspace::GalleryPresentation m_RawWorkspaceGalleryPresentationCache;
    std::unordered_map<std::string, RawWorkspaceThumbnailTexture> m_RawWorkspaceThumbnailTextures;
    std::deque<std::string> m_RawWorkspaceThumbnailTextureUploadQueue;
    std::deque<unsigned int> m_RawWorkspaceThumbnailTextureDeleteQueue;
    std::atomic<std::uint64_t> m_RawWorkspaceThumbnailTextureResetGeneration { 0 };
    std::uint64_t m_RawWorkspaceThumbnailTextureRequestGeneration = 0;
    int m_RawWorkspaceThumbnailTextureRequestFrame = -1;
    std::size_t m_RawWorkspaceThumbnailTextureRequestsThisFrame = 0;
    int m_RawWorkspaceThumbnailTextureUploadFrame = -1;
    std::size_t m_RawWorkspaceThumbnailTextureUploadsThisFrame = 0;
    int m_RawWorkspaceThumbnailTextureDeleteFrame = -1;
    std::size_t m_RawWorkspaceThumbnailTextureDeletesThisFrame = 0;
    mutable std::unordered_map<std::string, RawWorkspaceRecipePreviewCacheEntry> m_RawWorkspaceRecipePreviewCache;
    std::string m_ActiveRawWorkspaceSourceKey;
    std::filesystem::path m_ActiveRawWorkspaceProjectPath;
    Stack::RawRecipe::RawDevelopmentRecipe m_ActiveRawWorkspaceRecipe;
    Stack::RawWorkspace::RawProjectMode m_ActiveRawWorkspaceMode =
        Stack::RawWorkspace::RawProjectMode::RecipeBacked;
    bool m_ShowRawWorkspaceRelinkPopup = false;
    bool m_ShowRawWorkspaceEmbedPopup = false;
    Stack::RawWorkspace::ManagedRawSection m_ActiveManagedRawSection;
    std::string m_RawWorkspacePreviewStageFailureSourceKey;
    bool m_RawWorkspacePreviewStageQueued = false;
    std::string m_RawWorkspacePreviewStageSourceKey;
    int m_RawWorkspacePreviewStageQueuedFrame = -1;
    std::string m_RawWorkspacePreviewSourceKey;
    double m_RawWorkspaceFastPreviewUntilTime = -1.0;
    bool m_RawWorkspaceFullResolutionPreviewPending = false;
    bool m_RawWorkspaceFullResolutionPreviewRequested = false;
    std::string m_RawWorkspaceLocalRangeOverlayMode = "none";
    unsigned int m_RawWorkspaceLocalRangeOverlayTexture = 0;
    int m_RawWorkspaceLocalRangeOverlayWidth = 0;
    int m_RawWorkspaceLocalRangeOverlayHeight = 0;
    std::string m_RawWorkspaceLocalRangeOverlaySourceKey;
    std::string m_RawWorkspaceLocalRangeOverlayAcceptedMode;
    std::uint64_t m_RawWorkspaceLocalRangeOverlayGeneration = 0;
    RenderTextureStats m_RawWorkspaceViewTransformInputStats;
    Stack::RawAnalysis::RawImageAnalysis m_RawWorkspaceAnalysis;
    RawWorkspaceAutoBaseUiState m_RawWorkspaceAutoBaseUi;
    bool m_RawWorkspaceLocalRangeTargetMode = false;
    bool m_RawWorkspaceLocalRangeTargetDragging = false;
    bool m_RawWorkspaceLocalRangeTargetSamplePending = false;
    bool m_RawWorkspaceLocalRangeTargetSampleValid = false;
    bool m_RawWorkspaceLocalRangeTargetApplyWhenSampled = false;
    std::string m_RawWorkspaceLocalRangeTargetSourceKey;
    float m_RawWorkspaceLocalRangeTargetU = 0.0f;
    float m_RawWorkspaceLocalRangeTargetV = 0.0f;
    float m_RawWorkspaceLocalRangeTargetSceneEv = 0.0f;
    float m_RawWorkspaceLocalRangeTargetSceneLuma = 0.0f;
    float m_RawWorkspaceLocalRangeTargetSceneR = 0.0f;
    float m_RawWorkspaceLocalRangeTargetSceneG = 0.0f;
    float m_RawWorkspaceLocalRangeTargetSceneB = 0.0f;
    float m_RawWorkspaceLocalRangeTargetStartMouseY = 0.0f;
    float m_RawWorkspaceLocalRangeTargetDeltaEv = 0.0f;
    int m_RawWorkspaceLocalRangeTargetPointIndex = -1;

    std::uint64_t m_ExportGeneration = 0;
    Async::TaskState m_ExportTaskState = Async::TaskState::Idle;
    std::string m_ExportStatusText;
    bool m_ShowNewProjectPrompt = false;
    bool m_ShowNewProjectDiscardConfirm = false;
    ManagedRawGraphMutationConfirmState m_ManagedRawGraphMutationConfirm;
    bool m_ExecutingManagedRawGraphMutationConfirmation = false;

    CanvasToolKind m_CanvasToolKind = CanvasToolKind::None;
    int m_CanvasToolOwnerNodeId = -1;
    std::string m_CanvasToolStatusText;
    bool m_IsPickingColor = false;
    std::function<void(float, float, float)> m_ColorPickerCallback;
    int m_LastToneCurveProbeNodeId = -1;
    bool m_RenderWorkerAvailable = false;
    bool m_NodeBrowserRenderWorkerAvailable = false;
    bool m_ShutdownComplete = false;
    bool m_RenderDirty = true;
    bool m_RenderPending = false;
    EditorRenderWorker::SharedTextureTileSet m_ViewportOutputTiles;
    RawWorkspacePreviewOutputKind m_RawWorkspacePreviewOutputKind =
        RawWorkspacePreviewOutputKind::None;
    std::string m_ViewportOutputRawWorkspaceSourceKey;
    int m_ViewportOutputPreviewMaxDimension = 0;
    std::uint64_t m_ViewportOutputRenderGeneration = 0;
    std::uint64_t m_RenderGeneration = 0;
    std::uint64_t m_LastCompletedRenderGeneration = 0;
    std::uint64_t m_RenderRevision = 1;
    std::uint64_t m_LastSubmittedRenderRevision = 0;
    int m_LastNonRenderingPumpFrame = -1;
    int m_AutoGainMaskPreviewNodeId = -1;
    double m_LastRenderDirtyTime = 0.0;
    bool m_ShowGraphPerformancePopup = false;
    GraphPerformanceStats m_GraphPerformanceStats;
    double m_LastRawDevelopInteractionTime = -1.0;
    std::uint64_t m_RawDevelopInteractionSerialCounter = 1;
    std::unordered_map<int, double> m_RawDevelopInteractionTimes;
    std::unordered_map<int, std::uint64_t> m_RawDevelopInteractionSerials;
    std::unordered_map<int, double> m_DeferredDevelopCandidateFeedbackTimes;
    mutable std::unordered_map<int, std::size_t> m_DevelopAutoSolveTriggerHashes;
    std::unordered_map<int, std::deque<EditorNodeGraph::CustomMaskPayload>> m_CustomMaskUndoStacks;
    std::unordered_map<int, std::deque<EditorNodeGraph::CustomMaskPayload>> m_CustomMaskRedoStacks;
    std::unordered_set<int> m_CustomMaskPaintingNodes;
    using CustomMaskBrushAdjustDrag = Stack::EditorModuleTypes::CustomMaskBrushAdjustDrag;
    CustomMaskBrushAdjustDrag m_CustomMaskBrushAdjustDrag;
    mutable std::unordered_map<int, std::size_t> m_DevelopAutoRawSolveTriggerHashes;
    mutable std::unordered_map<int, std::size_t> m_DevelopAutoRawCalibrationHashes;
    std::unordered_map<int, DevelopAutoGuidanceDraftState> m_DevelopAutoGuidanceDrafts;
    std::unordered_map<int, RawDevelopExposureDraftState> m_RawDevelopExposureDrafts;
    std::uint64_t m_NodeDirtyGenerationCounter = 1;
    std::unordered_map<int, std::uint64_t> m_NodeDirtyGenerations;
    float m_LeftPaneWidth = 0.0f;
    bool m_NodeGraphFullscreen = false;
    float m_LastUserNodeGraphWidth = 0.0f;
    EditorSubWindow m_LastSplitTargetSubWindow = EditorSubWindow::NodeGraph;
    int m_LastSplitTargetComplexNodeId = -1;
    bool m_DraggingSplitHandle = false;
    bool m_SplitHandlePressed = false;
    bool m_SplitHandleMoved = false;
    bool m_SplitHandlePressedFromViewportPane = false;
    bool m_SplitAutoAnimating = false;
    float m_SplitAutoAnimFrom = 0.0f;
    float m_SplitAutoAnimTo = 0.0f;
    double m_SplitAutoAnimStartTime = 0.0;
    ViewportMode m_LastViewportMode = ViewportMode::SingleOutputPreview;
    using CompositeEdgeSnapMode = Stack::EditorModuleTypes::CompositeEdgeSnapMode;
    CompositeEdgeSnapMode m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
    CompositeEdgeSnapMode m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
    bool m_DetachedPreviewActive = false;
    bool m_DetachedPreviewTogglePending = false;
    bool m_DetachedPreviewRequestFocus = false;
    bool m_DetachedPreviewPlacementInitialized = false;
    bool m_DetachedPreviewNativeShown = false;
    bool m_DetachedPreviewFirstPresented = false;
    bool m_DetachedPreviewLayoutDetached = false;
    int m_DetachedPreviewPlatformWaitFrames = 0;
    int m_DetachedPreviewFocusAttempts = 0;
    ImGuiID m_DetachedPreviewViewportId = 0;
    GLFWwindow* m_DetachedPreviewStyledWindow = nullptr;
    ImU32 m_DetachedPreviewStyledSurfaceColor = 0;
    ImU32 m_DetachedPreviewStyledTextColor = 0;
    ImVec4 m_DetachedPreviewSurfaceColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    float m_DetachedPreviewRestoreLeftPaneWidth = 0.0f;
    ImVec2 m_DetachedPreviewMonitorPos = ImVec2(0.0f, 0.0f);
    ImVec2 m_DetachedPreviewMonitorSize = ImVec2(0.0f, 0.0f);
    ImVec2 m_DetachedPreviewWindowPos = ImVec2(0.0f, 0.0f);
    ImVec2 m_DetachedPreviewWindowSize = ImVec2(0.0f, 0.0f);
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
    bool m_PendingAddImageNodePrompt = false;
    EditorNodeGraph::Vec2 m_PendingAddImageNodeGraphPosition {};
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
    std::unordered_map<int, std::uint64_t> m_HdrMergeRequestedGenerations;
    std::unordered_map<int, std::uint64_t> m_HdrMergeCompletedGenerations;
    std::unordered_map<int, std::string> m_HdrMergeFailureMessages;
    std::unordered_set<int> m_HdrMergeRenderingNodeIds;
    std::unordered_map<std::uint64_t, std::vector<int>> m_HdrMergeSubmittedNodesByGeneration;
    mutable std::unordered_map<int, ToneCurveViewportInteractionCache> m_IntegratedToneViewportInteractionCache;
    std::unordered_map<std::string, NodeBrowserThumbnailRuntimeEntry> m_NodeBrowserThumbnailEntries;
    std::unordered_map<int, NodeBrowserPreviewRequestMeta> m_NodeBrowserPreviewRequestMeta;
    std::uint64_t m_NodeBrowserThumbnailGeneration = 0;
    std::uint64_t m_NodeBrowserThumbnailRevisionCounter = 1;
    std::size_t m_NodeBrowserThumbnailWarmPendingEntries = 0;
    std::size_t m_NodeBrowserThumbnailPendingEntries = 0;
    bool m_NodeBrowserThumbnailBatchHasChanges = false;
    bool m_NodeBrowserThumbnailGenerationQueued = false;
    std::string m_NodeBrowserThumbnailSeedHash;
    std::uint64_t m_NodeBrowserThumbnailSeedSerial = 0;

    using DeferredLoadedProjectApplyState = Stack::EditorModuleTypes::DeferredLoadedProjectApplyState;
    DeferredLoadedProjectApplyState m_DeferredLoadedProjectApply;

    void ResetForPipelineDeserialization();
    bool DeserializeSinglePipelineLayer(const nlohmann::json& layerData);
    bool FinalizeDeserializedPipeline(const nlohmann::json& serialized, bool restoreSourceFromGraphState);
    void RestorePersistedNodeBrowserThumbnailEntries(
        const std::vector<StackBinaryFormat::NodeBrowserThumbnailEntry>& entries,
        std::size_t startIndex,
        std::size_t maxCount,
        std::size_t& outNextIndex);
    void ResetDeferredLoadedProjectApplyState();
    void FailDeferredLoadedProjectApply(std::string message);
    void TickDeferredLoadedProjectApply(double projectApplyBudgetMs);
    void ApplyGraphLayerOrder();
    std::vector<std::shared_ptr<LayerBase>> BuildGraphRenderLayers() const;
    std::vector<RenderLayerStep> BuildGraphRenderSteps() const;
    std::vector<RenderMaskSource> BuildGraphRenderMasks() const;
    EditorRenderWorker::Snapshot BuildRenderSnapshot(std::uint64_t generation);
    std::vector<EditorRenderWorker::CompositeOutputRequest> BuildCompositeOutputRequests();
    std::vector<EditorRenderWorker::PreviewRequest> BuildPreviewRequests();
    std::vector<EditorRenderWorker::DevelopCandidateRenderRequest> BuildDevelopCandidateRenderRequests(
        const RenderGraphSnapshot& graph,
        int sourceWidth,
        int sourceHeight);
    void ConsumeRenderWorkerResults();
    void ClearViewportOutputTiles();
    void QueueViewportOutputTextureRelease(EditorRenderWorker::SharedTextureResult& texture);
    void QueueViewportOutputTileSetRelease(EditorRenderWorker::SharedTextureTileSet& tileSet);
    void PumpViewportOutputTextureDeletes(bool drainAll = false);
    void PumpViewportOutputTileTextureDeletes(bool drainAll = false);
    void ApplyToneCurveAutoRewriteFeedback(const std::vector<ToneCurveAutoRewriteFeedback>& feedbacks);
    void ApplyDevelopCandidateRenderFeedback(
        const std::vector<EditorRenderWorker::DevelopCandidateRenderResult>& results);
    void SubmitRenderIfReady();
    void ConsumeNodeBrowserThumbnailWorkerResults();
    void ResetNodeBrowserThumbnailState();
    void MarkNodeBrowserThumbnailSourceChanged();
    NodeBrowserPreviewSeed ResolveNodeBrowserPreviewSeed() const;
    void StartNodeBrowserThumbnailGeneration(bool forceRefresh);
    void FinalizeNodeBrowserThumbnailBatch(std::uint64_t generation);
    void WarmNodeBrowserThumbnailPixelsAsync();
    void RefreshCompletedChainCacheIfNeeded() const;
    void RefreshCompositeMetadataCacheIfNeeded();
    void MarkDownstreamNodesDirty(int touchedNodeId);
    void MarkAllRenderNodesDirty();
    void MarkCompositeOutputsDirty(const std::vector<int>& outputNodeIds);
    void PruneCompositeDirtyState();
    bool HasPendingPreviewRefreshes() const;
    bool CanRefreshPreviewLikeNodes() const;
    bool IsRecentRawDevelopInteraction(double now = -1.0) const;
    bool IsRecentRawDevelopInteractionForNode(int nodeId, double now, double windowSeconds) const;
    void RecordRawDevelopInteraction(int nodeId);
    std::uint64_t GetRawDevelopInteractionSerial(int nodeId) const;
    void ScheduleDeferredDevelopCandidateFeedback(int nodeId, double now);
    void RefreshDeferredDevelopCandidateFeedbackIfReady(double now);
    bool GetDevelopCandidateFeedbackDeferredStatus(
        int nodeId,
        double now,
        double& outRemainingSeconds) const;
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
    void HandleSpacebarPress(float workspaceWidth, float paneHeight, float minLeftWidth, float maxLeftWidth, float splitGap);
    void HandleSpacebarLongPress(float workspaceWidth, float paneHeight, float minLeftWidth, float maxLeftWidth, float splitGap);
    bool UpdateDevelopAutoState(
        int nodeId,
        EditorNodeGraph::RawDevelopPayload& payload,
        const Raw::RawMetadata& metadata,
        bool forceReanalysis,
        bool forceFullReanalysis);
    bool AddImageNodeFromPayload(EditorNodeGraph::ImagePayload payload, EditorNodeGraph::Vec2 graphPosition);
    bool AddRawSourceNodeFromPayload(EditorNodeGraph::RawSourcePayload payload, EditorNodeGraph::Vec2 graphPosition);
    bool AddRawDevelopmentNodeFromPayload(EditorNodeGraph::RawDevelopmentPayload payload, EditorNodeGraph::Vec2 graphPosition);
    bool AddRawNeuralDenoiseNodeFromPayload(EditorNodeGraph::RawNeuralDenoisePayload payload, EditorNodeGraph::Vec2 graphPosition);
    bool AddRawDecodeNodeFromPayload(EditorNodeGraph::RawDecodePayload payload, EditorNodeGraph::Vec2 graphPosition);
    bool AddRawDevelopNodeFromPayload(EditorNodeGraph::RawDevelopPayload payload, EditorNodeGraph::Vec2 graphPosition);
    bool AddRawDetailAutoMaskNodeFromPayload(EditorNodeGraph::RawDetailAutoMaskPayload payload, EditorNodeGraph::Vec2 graphPosition);
    bool AddRawDetailFusionNodeFromPayload(EditorNodeGraph::RawDetailFusionPayload payload, EditorNodeGraph::Vec2 graphPosition);
    bool AddHdrMergeNodeFromPayload(EditorNodeGraph::HdrMergePayload payload, EditorNodeGraph::Vec2 graphPosition);
    bool AddMfsrNodeFromPayload(EditorNodeGraph::MfsrPayload payload, EditorNodeGraph::Vec2 graphPosition);
    bool AddLutNodeFromPayload(EditorNodeGraph::LutPayload payload, EditorNodeGraph::Vec2 graphPosition);
    bool AddGraphRawChainFromFile(const std::string& path, EditorNodeGraph::Vec2 sourcePosition);
    bool StartGraphImageChainImport(std::vector<std::string> paths, EditorNodeGraph::Vec2 sourcePosition);
    bool RequestGraphImageChainImports(const std::vector<std::string>& paths, EditorNodeGraph::Vec2 sourcePosition);
    bool AddGraphImageChainFromFile(const std::string& path, EditorNodeGraph::Vec2 sourcePosition);
    bool AddGraphImageChainFromPayload(EditorNodeGraph::ImagePayload payload, EditorNodeGraph::Vec2 sourcePosition);
    std::filesystem::path GetRawWorkspaceAppStatePath() const;
    RawWorkspaceScanSnapshot GetRawWorkspaceScanSnapshot() const;
    RawWorkspaceThumbnailSnapshot GetRawWorkspaceThumbnailSnapshot() const;
    void LoadRawWorkspaceAppState();
    void SaveRawWorkspaceAppState();
    void RequestOpenRawWorkspace(const std::filesystem::path& workspaceRoot);
    void RequestRawWorkspaceScan();
    void RequestRawWorkspaceThumbnailGeneration();
    void ClearRawWorkspace();
    void SelectRawWorkspaceSource(const std::string& sourceKey);
    void InvalidateRawWorkspaceGalleryPresentation();
    void QueueSelectedRawWorkspaceSourcePreviewStaging();
    void TickRawWorkspacePreviewStaging();
    bool EnsureSelectedRawWorkspaceSourcePreviewStaged();
    void PersistRawWorkspaceCatalog();
    void StartRawWorkspaceCatalogPersistIfNeeded();
    void ResetRawWorkspaceCatalogPersistState();
    void StartRawWorkspaceAppStatePersistIfNeeded();
    void ResetRawWorkspaceAppStatePersistState();
    void TickRawWorkspacePersistence();
    void FlushRawWorkspacePersistenceForShutdown();
    void NoteRawWorkspaceRecipePreviewEdit(bool interactionActive);
    bool IsRawWorkspaceFastPreviewRenderActive(double now) const;
    void UpdateRawWorkspaceSettledPreviewRender(double now);
    bool HasRawWorkspaceLivePreviewForSource(const std::string& sourceKey) const;
    bool HasRawWorkspaceFullResolutionPreviewForSource(const std::string& sourceKey) const;
    void ClearRawWorkspaceLivePreviewState();
    void ClearRawWorkspaceLocalRangeOverlayState();
    void AdoptRawWorkspaceLocalRangeOverlayFromResult(const EditorRenderWorker::Result& result);
    bool HasRawWorkspaceLocalRangeOverlayForSource(const std::string& sourceKey) const;
    void ClearRawWorkspaceLocalRangeTargetState(bool keepMode = true);
    void AdoptRawWorkspaceLocalRangeTargetSampleFromResult(const EditorRenderWorker::Result& result);
    bool HasRawWorkspaceLocalRangeTargetSampleForSource(const std::string& sourceKey) const;
    bool ApplyRawWorkspaceLocalRangeTargetDelta(bool interactionActive);
    void HandleRawWorkspaceLocalRangeTargetInteraction(
        const Stack::RawWorkspace::SourceRecord& selectedSource,
        const ImVec2& imageMin,
        const ImVec2& imageMax,
        bool selectedProjectActive,
        bool currentRawPreview);
    const Stack::RawWorkspace::SourceRecord* FindRawWorkspaceSourceByKey(const std::string& sourceKey) const;
    Stack::RawWorkspace::SourceRecord* FindRawWorkspaceSourceByKey(const std::string& sourceKey);
    Stack::RawRecipe::RawDevelopmentRecipe BuildRawWorkspaceDefaultRecipe(
        const Stack::RawWorkspace::SourceRecord& source) const;
    bool BuildRawWorkspaceProjectGraph(
        const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
        bool markEdited = true,
        std::string* outError = nullptr);
    bool RequestLoadRawWorkspaceProjectForSource(
        const Stack::RawWorkspace::SourceRecord& source,
        bool includeNodeBrowserThumbnails = false);
    void FinalizeDeferredRawWorkspaceProjectLoadIfNeeded();
    bool StageRawWorkspaceProjectForSourcePreview(const Stack::RawWorkspace::SourceRecord& source);
    bool ApplyActiveRawWorkspaceModeDataToDocument(StackBinaryFormat::ProjectDocument& document) const;
    void MarkActiveRawWorkspaceProjectAsCustomGraph(std::string reason);
    bool QueueManagedRawGraphMutationConfirmation(
        ManagedRawGraphMutationConfirmAction action,
        Stack::RawWorkspace::ManagedRawGraphMutationWarning warning,
        int nodeId = 0,
        int fromNodeId = 0,
        const std::string& fromSocketId = {},
        int toNodeId = 0,
        const std::string& toSocketId = {},
        std::vector<int> nodeIds = {});
    void RenderManagedRawGraphMutationConfirmPopup();
    void ExecuteManagedRawGraphMutationConfirmation();
    bool ResolveRawWorkspaceRecipeForSource(
        const Stack::RawWorkspace::SourceRecord& source,
        Stack::RawRecipe::RawDevelopmentRecipe& outRecipe,
        Stack::RawWorkspace::RawProjectMode* outMode = nullptr,
        std::string* outError = nullptr) const;
    bool FocusRawWorkspaceDevelopmentNode();
    bool OpenRawWorkspaceProjectInGraph(const Stack::RawWorkspace::SourceRecord& source);
    bool SaveActiveRawWorkspaceProjectIfDirty();
    std::uint64_t BumpRawWorkspaceProjectSaveRevision(
        const std::filesystem::path& workspaceRoot,
        const std::string& sourceKey);
    std::uint64_t GetRawWorkspaceProjectSaveRevision(
        const std::filesystem::path& workspaceRoot,
        const std::string& sourceKey) const;
    bool IsRawWorkspaceProjectSaveJobCurrent(const RawWorkspaceProjectSaveJob& job) const;
    void EnqueueRawWorkspaceProjectSave(RawWorkspaceProjectSaveJob job);
    void EnsureRawWorkspaceProjectSaveWorker();
    void RequestRawWorkspaceProjectSaveWorkerDrain();
    bool IsRawWorkspaceProjectSaveWorkerIdle() const;
    void ShutdownRawWorkspaceProjectSaveWorker();
    void RawWorkspaceProjectSaveWorkerLoop();
    bool WriteRawWorkspaceProjectSaveJob(RawWorkspaceProjectSaveJob& job, std::string& error) const;
    void CompleteRawWorkspaceProjectSave(
        RawWorkspaceProjectSaveJob job,
        bool success,
        bool skippedStale,
        std::string errorMessage);
    bool RelinkActiveRawWorkspaceProjectToSelectedSource();
    bool EmbedActiveRawWorkspaceProject();
    void RenderRawWorkspaceLifecyclePopups();
    void QueueRawWorkspaceThumbnailTextureDelete(unsigned int texture);
    void PumpRawWorkspaceThumbnailTextureDeletes(bool drainAll = false);
    void ClearRawWorkspaceThumbnailTextures(bool immediate = false);
    void RenderRawWorkspaceEmptyState(const RawWorkspaceScanSnapshot& scanSnapshot);
    void RenderRawWorkspaceBrowser(
        const RawWorkspaceScanSnapshot& scanSnapshot,
        const RawWorkspaceThumbnailSnapshot& thumbnailSnapshot);
    void RenderRawWorkspaceControlsPanel(
        const Stack::RawWorkspace::SourceRecord* selectedSource,
        const Stack::RawWorkspace::RawPanelState& panelState);
    bool RenderRawWorkspaceAutoBasePanel(
        const Stack::RawWorkspace::SourceRecord* selectedSource,
        Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe,
        float controlWidth);
    bool RenderRawWorkspaceLocalRangeControls(
        const Stack::RawWorkspace::SourceRecord* selectedSource,
        Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe,
        float controlWidth);
    void RenderRawWorkspaceAnalysisPanel(float controlWidth);
    void RenderRawWorkspacePreviewPanel(
        const Stack::RawWorkspace::SourceRecord* selectedSource,
        const Stack::RawWorkspace::RawPanelState& panelState);
    void TryApplyRawWorkspaceAutoBaseOnAnalysis();
    void RefreshRawWorkspaceAutoBaseRecommendations(
        const Stack::RawRecipe::RawDevelopmentRecipe& recipe);
    bool ApplyRawWorkspaceAutoBaseViewFitForSource(
        const Stack::RawWorkspace::SourceRecord& source,
        Stack::RawRecipe::RawDevelopmentRecipe& recipe,
        bool explicitApply);
    bool ApplyRawWorkspaceAutoBaseExposureSuggestion(
        Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe);
    bool ApplyRawWorkspaceAutoBaseWhiteBalanceSuggestion(
        Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe);
    bool ApplyRawWorkspaceAutoBaseHighlightProtection(
        Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe);
    bool ApplyRawWorkspaceAutoBaseLocalSuggestion(
        std::size_t suggestionIndex,
        Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe);
    bool RevertRawWorkspaceAutoBaseForSelectedSource();
    void MarkRawWorkspaceViewTransformUserEdited();
    bool RawWorkspaceViewTransformAutoOwnedForSource(const std::string& sourceKey) const;
    void ResetRawWorkspaceAutoBaseState();
    Stack::RawAnalysis::RawMetadataSummary ResolveRawWorkspaceMetadataSummaryForAutoBase() const;
    std::uint64_t BuildRawWorkspaceAutoBaseSourceHash(
        const Stack::RawWorkspace::SourceRecord& source) const;
    bool RawWorkspaceRecipeLooksDefaultForAutoBase(
        const Stack::RawWorkspace::SourceRecord& source,
        const Stack::RawRecipe::RawDevelopmentRecipe& recipe) const;
    void MoveCompositeOutputToFront(int outputNodeId);
    std::pair<EditorNodeGraph::Vec2, EditorNodeGraph::Vec2> BuildCompositeChainPlacement() const;
    std::size_t BuildCompositeChainFingerprint(const EditorNodeGraph::CompletedChainInfo& chain) const;
    std::string BuildCompositeChainLabel(const EditorNodeGraph::CompletedChainInfo& chain) const;
    std::string BuildCompositeChainLabel(int outputNodeId) const;
    void QueueUiNotification(UiNotificationSeverity severity, std::string message, std::string dedupeKey = "");
    SharedPixelBuffer EnsureSharedImagePixels(const EditorNodeGraph::ImagePayload& payload) const;
    RenderGraphImagePayload BuildRenderImagePayload(const EditorNodeGraph::ImagePayload& payload) const;
    SharedPixelBuffer MakeSharedSourcePixelBufferCopy(const std::vector<unsigned char>& pixels) const;
    bool TryCopyImageNodeSharedPixels(int sourceNodeId, SharedPixelBuffer& outPixels, int& outW, int& outH, int& outChannels) const;
    bool TryResolveReferenceSourceBuffer(int nodeId, const std::string& socketId, SharedPixelBuffer& outPixels, int& outW, int& outH, int& outChannels) const;
    bool TryResolveReferenceSourceBufferForOutput(int outputNodeId, SharedPixelBuffer& outPixels, int& outW, int& outH, int& outChannels) const;
    bool TryCopyImageNodePixels(int sourceNodeId, std::vector<unsigned char>& outPixels, int& outW, int& outH, int& outChannels) const;
    bool TryResolveReferenceSourcePixels(int nodeId, const std::string& socketId, std::vector<unsigned char>& outPixels, int& outW, int& outH, int& outChannels) const;
    bool TryResolveReferenceSourcePixelsForOutput(int outputNodeId, std::vector<unsigned char>& outPixels, int& outW, int& outH, int& outChannels) const;
    bool TryResolveReferenceSourceDimensions(int nodeId, const std::string& socketId, int& outW, int& outH) const;
    bool ShouldDeferPreviewLikeWork(double now = -1.0) const;
    void RenderGraphPerformancePopup(const ImVec2& graphPaneMin, const ImVec2& graphPaneMax);
    int ResolveFocusedToneCurveNodeId() const;
    bool SampleToneCurveViewportPixel(int toneCurveNodeId, ToneCurveSamplingBasis basis, float u, float v, std::array<float, 4>& outRgba) const;
    void ClearTrackedToneCurveProbe();
    bool CompletedChainSourceUsesScalableGenerator(int outputNodeId) const;
    bool CompletedChainSourceKeepsFullRasterFrame(int outputNodeId) const;
    std::vector<int> CollectHdrMergeNodesForOutput(int outputNodeId) const;
    HdrMergeConnectionTopology ResolveHdrMergeConnectionTopology(const EditorNodeGraph::Node& node) const;
    HdrMergeNodeStatus BuildHdrMergeNodeStatus(const EditorNodeGraph::Node& node) const;
    void ResetToBlankProject();
    void ResetRenderSubmissionState();
    void RenderProjectLifecyclePopups();
    StackAppearance::AppearanceManager* m_Appearance = nullptr;
};
