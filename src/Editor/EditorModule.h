#pragma once
#include "Editor/NodeGraph/GraphEditorContext.h"
#include "Editor/ProjectInteractionState.h"

namespace StackAppearance {
    class AppearanceManager;
}

#include "Raw/RawViewportTransition.h"
#include "Renderer/RawImageBackdrop.h"
#include "Raw/RawViewportTimingHistory.h"
#include "Raw/RawViewportPreferences.h"
#include "Raw/RawViewportController.h"
#include "Editor/Internal/RawWorkspace/RawViewportFadeRenderer.h"
#include "Editor/Internal/RawLab/RawGradingScopeRenderer.h"
#include "Editor/Internal/RawLab/RawLabGalleryPreviewState.h"
#include "Editor/Internal/RawLab/RawLabGalleryLayoutAnimation.h"
#include "Editor/Internal/RawLab/RawLabGalleryGridStacks.h"
#include "Editor/Internal/RawLab/RawFloatingSurfaceState.h"
#include "Async/TaskState.h"
#include "Async/TaskGroup.h"
#include "Editor/EditorModuleTypes.h"
#include "Editor/GraphCapture.h"
#include "Editor/LoadedProjectData.h"
#include "Project/FileOperationState.h"
#include "Project/ProjectSession.h"
#include "Project/ProcessedRawResult.h"
#include "Editor/NodeGraph/GraphWireReadout.h"
#include "Editor/RawLayerMaskWorkspace.h"
#include "Editor/RawLayerPanel.h"
#include "Editor/RawOperationMaskTarget.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include "Editor/RawLocalRangeTargetInteraction.h"
#include "Layers/LayerBase.h"
#include "LayerRegistry.h"
#include "EditorRenderWorker.h"
#include "RawRenderService.h"
#include "NodeGraph/EditorNodeGraph.h"
#include "NodeMath/PngMetadataWriter.h"
#include "UI/EditorSidebar.h"
#include "UI/EditorViewport.h"
#include "Raw/RawAutoStartPoint.h"
#include "Raw/RawImageAnalysis.h"
#include "Raw/MultiFrameDenoise/Processor.h"
#include "Raw/MultiFrameHdr/Processor.h"
#include "Raw/RawWorkspace.h"
#include "Raw/RawWorkspaceManagedGraph.h"
#include "Raw/RawEditAttributes.h"
#include "Raw/RawGalleryInspection.h"
#include "Raw/RawGalleryQueueRequest.h"
#include "Raw/RawGalleryActions.h"
#include "Editor/Internal/RawWorkspace/RawWorkspaceThumbnailScheduler.h"
#include "Renderer/RenderPipeline.h"
#include "Persistence/StackBinaryFormat.h"
#include "Persistence/ProjectSessionController.h"
#include "Persistence/ProjectSaveCoordinator.h"
#include "Persistence/ProjectFileStamp.h"
#include "Utils/UiNotifications.h"
#include "Notifications/Notifier.h"
#include "Utils/UiActivity.h"
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
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
namespace Raw::MultiFrame { class GraphProcessingCache; }
namespace Stack::Editor { struct FusionWorkspaceUiState; }
namespace Stack::Editor { struct BracketingSession; }
namespace Stack::Editor::RawLabInternal { class GalleryDetails; }

namespace Stack::AutoBracket { class AutoBracketCoordinator; struct Presentation; }

class EditorModule {
    friend struct RawViewportPresentationValidationAccess;
    friend struct BracketingRenderValidationAccess;
    friend struct BracketingPresentationValidationAccess;
    friend struct BracketingRawToolValidationAccess;
public:
    int GetRawViewportTargetFps() const;
    int GetRawViewportMaximumFps() const;
    void SetRawViewportTargetFps(int fps);
    void FinishRawViewportTargetFpsEdit();
    bool GetSmoothRawViewportUpdates() const;
    int GetRawViewportRequestedFps() const;
    Raw::ViewportPreferences GetRawViewportPreferences() const;
    void SetRawViewportPreferences(Raw::ViewportPreferences preferences);
    void ResetRawViewportLearnedTimings();
    std::string GetRawViewportDecisionStatus() const;
    void SetSmoothRawViewportUpdates(bool enabled);
    int GetRawViewportFadeBelowFps() const;
    void SetRawViewportFadeBelowFps(int fps);
    double GetRawViewportNativeFps() const;
    double GetRawViewportVisibleNativeFps() const;
    bool IsRawViewportCalibrationBusy() const;
    std::string GetRawViewportCalibrationStatus() const;
    enum class ProjectSessionKind {
        Empty,
        EditorProject,
        RawPreview,
        RawProject
    };

    struct ProjectFileCommandContext {
        ProjectSessionKind sessionKind = ProjectSessionKind::Empty;
        Stack::Project::ProjectLifecyclePhase lifecyclePhase =
            Stack::Project::ProjectLifecyclePhase::ReadyClean;
        std::filesystem::path projectPath;
        std::optional<Stack::Project::ProjectStorageKind> storageKind;
        std::string busyReason;
        bool dirty = false;
        bool busy = false;
        bool conflict = false;
        bool readOnlyRecovery = false;
        bool canSave = false;
        bool canSaveAs = false;
        bool canClose = false;
        bool canOpen = true;
        bool canCreateEditorProject = true;
    };

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
    using RawLabTool = Stack::EditorModuleTypes::RawLabTool;
    using RawGalleryHost = Stack::EditorModuleTypes::RawGalleryHost;
    using RawGalleryNavigationMode = Stack::EditorModuleTypes::RawGalleryNavigationMode;
    using RawWorkspaceLabUiState = Stack::EditorModuleTypes::RawWorkspaceLabUiState;
    using TimelineUiState = Stack::EditorModuleTypes::TimelineUiState;
    enum class RawWorkspacePreviewOutputKind {
        None,
        SingleTexture,
        Tiled
    };

    explicit EditorModule(std::shared_ptr<Stack::Project::ProjectSession> project = {});
    ~EditorModule();

    void Initialize(GLFWwindow* sharedWindow = nullptr, StackAppearance::AppearanceManager* appearance = nullptr,
        bool restoreWorkspace = true);
    void TickAutoBracketing(bool externalBusy);
    bool ConsumeAutoBracketWorkspaceRequest();
    bool ConsumeAutoBracketCatalogChanged();
    bool TransferAutoBracketingTo(EditorModule& target);
    void SetAutoBracketProtectedProjectIds(const std::vector<std::string>& projectIds,
        const std::vector<std::filesystem::path>& paths = {});
    bool IsAutoBracketProjectBusy(const std::filesystem::path& path) const;
    bool IsAutoBracketWorkspace() const { return m_IsAutoBracketWorkspace; }
    void CancelAutoBracketingForWorkspaceClose(bool pauseQueue = true);
    void RenderAutoBracketWorkspace();
    void RenderAutoBracketOverlay();
    void RenderAutoBracketControls();
    void RenderAutoBracketQueueItems();
    bool RequestAutoBracketForeground(const std::string& label, std::function<void()> action);
    bool AutoBracketWorkActive() const;
    void ShutdownAutoBracketing();
    const Stack::RawWorkspace::GalleryPresentation& GetRawWorkspaceCategoryPresentation();
    void SetDocumentPersistenceEnabled(bool enabled) {
        m_DocumentPersistenceEnabled = enabled;
    }
    void RequestWorkerShutdownForAppClose();
    bool IsWorkerShutdownReadyForAppClose() const;
    Async::TaskGroup& ProjectTasks() { return m_Project->tasks; }
    Stack::Project::ProjectSession& GetProjectSession() { return *m_Project; }
    const Stack::Project::ProjectSession& GetProjectSession() const { return *m_Project; }
    void SetProjectRemovalGuard(std::function<bool(const std::filesystem::path&)> guard) {
        m_ProjectRemovalGuard = std::move(guard);
    }
    void Shutdown();

    // Called every frame by the AppShell
    void RenderUI();
    void RenderRawWorkspaceUI();
    void RenderRawWorkspaceLabUI();
    // The shell reserves visibleWidth; contents retain their final width while clipped.
    void RenderRawWorkspaceSectionPanel(const ImVec2& position, const ImVec2& size, float visibleWidth);
    Stack::Editor::RawOperationMaskAvailability QueryRawOperationMaskAvailability() const;
    bool CreateRawOperationMask(const Stack::Editor::RawOperationMaskTarget& target,
        EditorNodeGraph::MaskGeneratorKind kind, std::string& error);
    void RequestRawSettingsPanel();
    bool ConsumeRawSettingsPanelRequest();
    bool GetRawActiveControlBounds(ImVec2& minimum, ImVec2& maximum) const;
    const Stack::Renderer::RawImageBackdropFrame* GetRawImageBackdrop() const;
    bool ConsumeRawToolPickerRequest();
    int GetRawLabToolIndex() const;
    void RequestRawLabToolIndex(int index);
    void RequestRawLabTool(RawLabTool tool);
    bool IsRawBracketModeActive() const;
    void RequestRawBracketMode(bool bracket);
    void RenderMultiFrameWorkspaceUI();
    void RenderBracketingUI();
    void RenderBracketingOrientationReview();
    bool IsBracketingPresentationActive() const;
    void UpdateBracketingPresentation(bool foreground = true);
    void RenderBracketingPresentation(ImVec4 background, ImVec2 bodyPosition, ImVec2 bodySize);
    void CancelBracketingPresentation();
    void TickBracketing(bool foreground = true);
    void ResetBracketingForProjectLoad(const std::filesystem::path& projectPath);
    void RefreshBracketingProjectCard();
    bool CaptureBracketingDraftForCopy(
        const Stack::Project::ProjectStoreHandle& store,
        const Stack::Project::ProjectStoreTransaction& transaction,
        Stack::Project::RawProjectSnapshot& snapshot, std::string& error) const;
    void EnterBracketingRaw();
    bool IsBracketingActive() const;
    bool StartBracketingProcessing(bool publish, std::string* error = nullptr);
    void CommitBracketingEdit();
    bool IsBracketingToolActive() const;
    void OpenBracketingTool();
    void BeginBracketingDraft(bool newProject = false);
    void SyncBracketingSelection();
    void SelectBracketingSources(const std::vector<std::string>& keys, bool toggle, bool range);
    void AddBracketingDraftFiles(const std::vector<std::filesystem::path>& paths);
    void RemoveBracketingDraftSource(const std::string& frameId);
    bool CommitBracketingDraft(bool process, std::string* error = nullptr);
    void RestoreBracketingSelection();
    void TickBracketingDraft();
    void RenderBracketingGroups();
    bool RenderBracketingViewport();
    bool HasPendingBracketingDraft() const;
    bool HasUnsavedBracketingDraft() const;
    bool NeedsWorkspaceSaveBeforeTransition() const;
    bool IsWorkspaceTransitionPending() const {
        return m_RawWorkspaceReplacementSavePending || m_RawWorkspaceReplacementExecuteAfterSave ||
            m_RawWorkspaceCloseAfterSave || m_ShowRawWorkspaceCloseProjectPopup;
    }
    bool RequestSaveWorkspaceBeforeClose(std::function<void(bool)> onComplete);
    void DiscardBracketingDraft();
    void RenderRawWorkspaceDetachedWindows();
    bool IsRawWorkspaceGalleryAvailable() const;
    bool IsRawWorkspaceGalleryOpen() const;
    void ToggleRawWorkspaceGallery();
    bool OpenRawWorkspaceGalleryWorkspace();
    bool RenderRawWorkspaceGalleryFileMenu(bool includeFilmstripSort = true);
    void SetPermanentGalleryWorkspace(bool permanent);
    void SetGalleryNavigationHandler(std::function<void()> handler) {
        m_GalleryNavigationHandler = std::move(handler);
    }
    bool IsRawWorkspaceGalleryWorkspaceOpen() const { return m_RawWorkspaceLabUi.galleryWorkspaceOpen; }
    void CloseRawWorkspaceGalleryWorkspace();
    bool IsRawWorkspaceInfoAvailable() const;
    bool IsRawWorkspaceInfoOpen() const;
    void ToggleRawWorkspaceInfo();
    bool IsRawWorkspaceToolPanelOnRight() const {
        return m_RawWorkspaceLabUi.toolRailOnRight;
    }
    void SetRawWorkspaceToolPanelOnRight(bool onRight);
    void BeginMultiFrameCaptureSetGallerySelection(
        bool returnToMultiFrameOnCancel = false);
    void BeginMultiFrameDenoiseGallerySelection(
        bool returnToMultiFrameOnCancel = false);
    void BeginMultiFrameHdrGallerySelection(
        bool returnToMultiFrameOnCancel = false);
    bool FinishWorkspaceInteraction();
    void BeginWorkspacePreview();
    void RestoreWorkspacePreviewLayout();
    void EndWorkspacePreview();
    void SetGraphCatalogHost(const ImVec2& position, const ImVec2& size,
        bool expanded, float visibleWidth = -1.0f);
    bool EnterRawWorkspaceRootTab();
    bool LeaveRawWorkspaceRootTab(bool enteringEditorTab);
    ProjectSessionKind GetProjectSessionKind() const;
    ProjectFileCommandContext GetProjectFileCommandContext() const;
    bool IsRawWorkspaceLockedByEditorProject() const {
        return m_RawWorkspaceLockedByEditorProject;
    }
    bool CloseCurrentProject(bool discardUnsavedChanges = false);
    bool CloseEditorProjectAndActivateRawWorkspace();
    bool CloseActiveRawWorkspaceProject(bool discardUnsavedChanges = false);
    void ReleaseRawWorkspacePreviewForTabChange();
    bool FocusRawWorkspace();
    bool FlushActiveRawWorkspaceProjectIfDirty();
    void RenderDetachedPreviewWindow();
    void BeginLibraryLoadReveal();
    void PumpNonRenderingWork(double projectApplyBudgetMs = 2.5, bool foreground = true);
    bool BeginDeferredLoadedProjectApply(
        std::shared_ptr<LoadedProjectData> projectData,
        std::function<void(bool, const std::string&)> onComplete = {});
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
    void SetWorkspaceDetachedWindowsVisible(bool visible);
    bool IsDetachedPreviewActive() const { return m_DetachedPreviewActive; }
    bool IsDetachedPreviewLayoutDetached() const { return m_DetachedPreviewLayoutDetached; }
    enum class DetachedSurfaceKind {
        EditorPreview,
        RawGallery
    };
    struct DetachedNativeWindowRequest {
        DetachedSurfaceKind kind = DetachedSurfaceKind::EditorPreview;
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
    bool QueryDetachedNativeWindow(
        DetachedSurfaceKind kind,
        DetachedNativeWindowRequest& request) const;
    void CompleteDetachedNativeWindowRequest(
        const DetachedNativeWindowRequest& request,
        bool themeApplied,
        bool focused);
    void MarkDetachedNativeWindowShown(
        const DetachedNativeWindowRequest& request,
        bool focused);
    void MarkDetachedPlatformPresented(DetachedSurfaceKind kind, GLFWwindow* window);

    RenderPipeline& GetPipeline() { return m_Pipeline; }
    Stack::Editor::GraphEditorContext GetGraphEditorContext();
    std::vector<std::shared_ptr<LayerBase>>& GetLayers();
    const std::vector<std::shared_ptr<LayerBase>>& GetLayers() const;
    StackAppearance::AppearanceManager* GetAppearance() {
        return m_GraphCaptureAppearanceOverride ? m_GraphCaptureAppearanceOverride : m_Appearance;
    }
    const StackAppearance::AppearanceManager* GetAppearance() const {
        return m_GraphCaptureAppearanceOverride ? m_GraphCaptureAppearanceOverride : m_Appearance;
    }

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
    bool IsEditingRawLayerMaskGraph() const;
    void OpenRawLayerMaskGraph(const std::string& layerId);
    bool UndoRawLayerEdit();
    bool RedoRawLayerEdit();
    bool RestoreRawLayerHistoryEdit(bool redo);
    void RenderRawLayerGraphToolbar();
    void FinishRawLayerGraphFrame();
    void MarkGraphEdited(int touchedNodeId = -1, bool affectsPixels = true);
    bool IsGraphOutputConnected() const;
    void PromptAddImageNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void RequestPromptAddImageNodeAt(EditorNodeGraph::Vec2 graphPosition);
    bool AddImageNodeFromFile(const std::string& path, EditorNodeGraph::Vec2 graphPosition);
    bool UseGraphImageNodeAsActiveSource(int nodeId);
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
    bool RemoveGraphNode(int nodeId, bool reconnect = true);
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
    void AddValueNodeAt(Stack::NodeMath::FirstClassValue value, EditorNodeGraph::Vec2 graphPosition);
    void AddFieldMeanNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddReformatNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddRawOperationNodeAt(Stack::RawRecipe::GraphOperationKind kind, EditorNodeGraph::Vec2 graphPosition);
    void AddTechnicalImageNodeAt(Stack::NodeMath::TechnicalImageOperation operation, EditorNodeGraph::Vec2 graphPosition);
    void AddCompoundTemplateNodeAt(std::size_t templateIndex, EditorNodeGraph::Vec2 graphPosition);
    bool MakeCompoundNodeUnique(int nodeId, std::string* error = nullptr);
    bool UnpackCompoundNode(int nodeId, std::string* error = nullptr);
    bool UpdateCompoundNodeToLatestEmbeddedVersion(int nodeId, std::string* error = nullptr);
    bool CreateCompoundFromSelection(const std::string& label, std::string* error = nullptr);
    void AddFrequencyFilterNodeAt(EditorNodeGraph::FrequencyFilterMode mode, EditorNodeGraph::Vec2 graphPosition);
    void AddFrequencyResponseNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddFrequencyFftNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddFrequencyIfftNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddSpectrumViewNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddApplyFrequencyResponseNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddCombineSpectraNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddSpectrumSeparateNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddSpectrumRecombineNodeAt(EditorNodeGraph::Vec2 graphPosition);
    bool SetFrequencyParameterExposed(
        int nodeId,
        const std::string& parameterId,
        bool exposed = true);
    bool ExtractFrequencyResponseNode(int filterNodeId, std::string* error = nullptr);
    bool ExpandFrequencyFilterNode(int filterNodeId, std::string* error = nullptr);
    bool CanUndoFrequencyGraphAction() const {
        return m_FrequencyGraphUndo.has_value();
    }
    bool CanRedoFrequencyGraphAction() const {
        return m_FrequencyGraphRedo.has_value();
    }
    bool UndoFrequencyGraphAction();
    bool RedoFrequencyGraphAction();
    void AddFrequencyMaskNodeAt(EditorNodeGraph::FrequencyMaskShape shape, EditorNodeGraph::Vec2 graphPosition);
    void AddSpectrumMathNodeAt(EditorNodeGraph::SpectrumMathMode mode, EditorNodeGraph::Vec2 graphPosition);
    void AddMagnitudePhaseNodeAt(EditorNodeGraph::MagnitudePhaseMode mode, EditorNodeGraph::Vec2 graphPosition);
    void AddSpectrumAnalyzerNodeAt(EditorNodeGraph::SpectrumAnalyzerMode mode, EditorNodeGraph::Vec2 graphPosition);
    void AddPreviewNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddChannelSplitNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddChannelCombineNodeAt(EditorNodeGraph::Vec2 graphPosition);
    void AddConstantChannelNodeAt(EditorNodeGraph::Vec2 graphPosition);
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
    bool HandleMultiFrameFileDrop(
        const std::vector<std::string>& paths,
        float screenX,
        float screenY);
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
    RenderGraphSnapshot BuildGraphSnapshotForTimelineFrame(int timelineFrame, int stageOutputNodeId = 0) const;
    bool UsesRawWorkspaceStageRender() const;
    int ResolveRawWorkspaceStageOutputNodeId() const;
    bool TryGetGraphOutputSemanticDescriptor(Stack::NodeMath::ValueDescriptor& descriptor) const;
    bool TryGetGraphLinkSemanticDescriptor(
        const EditorNodeGraph::Link& link,
        Stack::NodeMath::ValueDescriptor& descriptor) const;
    const EditorNodeGraph::GraphOutputDescription* GetGraphOutputDescription(
        int nodeId, const std::string& socketId) const;
    // Returns presentation-only source facts for a wire. This never evaluates
    // the graph and deliberately excludes destination diagnostics.
    bool TryGetGraphLinkWireReadoutInput(
        const EditorNodeGraph::Link& link,
        EditorNodeGraph::WireReadout::Input& input) const;
    const std::vector<Stack::NodeMath::Diagnostic>& GetGraphSemanticDiagnostics() const {
        return m_LastGraphSemanticDiagnostics;
    }
    bool IsEditorRenderBusy() const {
        return IsAnyRenderBackendBusy() || m_RenderPending;
    }
    std::uint64_t GetRenderRevision() const { return m_RenderRevision; }
    std::uint64_t GetViewportOutputRenderGeneration() const { return m_ViewportOutputRenderGeneration; }
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
    bool RequestQueueExportImage(const std::string& path);
    bool CaptureSettledFullQualityPreviewRaster(
        std::vector<unsigned char>& outPixels,
        int& outW,
        int& outH,
        int maxDimension = 2048);
    bool RequestFullQualityRender(std::string* errorMessage = nullptr);
    float GetFullQualityRenderProgress(
        std::string* statusText = nullptr) const;
    std::string GetFullQualityRenderDiagnostic() const;
    bool IsRenderSettledForFullQualityExport() const {
        return m_Project->graph.IsOutputConnected() &&
            !m_RenderDirty && !m_RenderPending &&
            !IsAnyRenderBackendBusy() &&
            m_ViewportOutputPreviewMaxDimension == 0;
    }
    bool RequestExportProject(const std::string& path);
    bool PackCurrentProject(
        const std::filesystem::path& destination,
        std::string* errorMessage = nullptr);
    bool BuildProjectDocumentForSave(
        const std::string& displayName,
        StackBinaryFormat::ProjectDocument& outDocument);
    void EnsureNodeBrowserThumbnailCatalog();
    bool GetNodeBrowserThumbnailView(const std::string& previewKey, NodeBrowserThumbnailView& outView) const;
    std::vector<StackBinaryFormat::NodeBrowserThumbnailEntry> GetPersistedNodeBrowserThumbnails() const;
    bool RequestSaveCurrentProject(
        const std::string& fallbackName = "",
        std::function<void(bool)> onComplete = {});
    bool RequestSaveProjectAs(
        const std::filesystem::path& destination,
        std::function<void(bool)> onComplete = {});
    bool RequestOpenProjectFromPath(
        const std::filesystem::path& projectPath,
        bool currentDispositionApproved = false);
    bool EnsureRawWorkspaceProjectForSelectedRecipeEdit(
        const Stack::RawRecipe::RawDevelopmentRecipe& recipe);
    bool MaterializeSelectedRawPreviewProject(
        const Stack::RawWorkspace::SourceRecord& source,
        Stack::RawRecipe::RawDevelopmentRecipe& managedRecipe,
        std::string* outError = nullptr);
    bool ApplyRawWorkspaceRecipeEditForSelectedSource(
        const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
        bool interactionActive = false);
    bool UpdateRawWorkspaceInteractionDraft(
        const Stack::RawRecipe::RawDevelopmentRecipe& persistedRecipe,
        const Stack::RawRecipe::RawDevelopmentRecipe& draftRecipe);
    bool ResolveRawWorkspaceInteractionDraft(bool cancel);
    bool SaveActiveRawWorkspaceProject(
        bool explicitSave = true,
        bool synchronousAutosave = false);
    bool LoadActiveRawWorkspaceProjectInGraph();
    bool DetachActiveRawWorkspaceGraphFromRawTab();

    const std::string& GetCurrentProjectName() const { return m_Project->name; }
    void SetCurrentProjectName(const std::string& name) { m_Project->name = name; }

    const std::string& GetCurrentProjectFileName() const { return m_Project->fileName; }
    void SetCurrentProjectFileName(const std::string& fileName);

    const std::string& GetProjectDocumentId() const { return m_Project->documentId; }
    std::string EnsureProjectDocumentId();
    const std::shared_ptr<Stack::Project::FileOperationState>& GetProjectFileOperations() const {
        return m_Project->files;
    }
    Async::TaskState GetProjectLoadTaskState() const { return m_Project->files->load.state; }
    const std::string& GetProjectLoadStatusText() const { return m_Project->files->load.statusText; }
    const std::filesystem::path& GetProjectAdoptionSourcePath() const {
        return m_Project->adoptionSourcePath;
    }
    bool ConsumeProjectNamingPromptRequest() {
        const bool requested = m_ProjectNamingPromptRequested;
        m_ProjectNamingPromptRequested = false;
        return requested;
    }

    bool IsDirty() const {
        return m_Project->dirty ||
            (IsUnifiedProjectStoreActive() &&
             m_Project->lifecycle.IsDirty());
    }
    std::uint64_t GetProjectEditRevision() const {
        return m_Project->editRevision;
    }
    void ClearDirty();
    bool ClearDirtyIfRevision(std::uint64_t expectedRevision);
    void MarkDirty();
    bool IsRawWorkspaceProjectActive() const {
        return (m_Project->snapshot != nullptr &&
                m_Project->snapshot->projectKindHint ==
                    StackBinaryFormat::kRawProjectKind) ||
            (m_Project->rawPipelineActive &&
             !m_Project->rawSourceKey.empty() &&
             !m_Project->storePath.empty());
    }
    bool IsMultiFrameRawProjectActive() const {
        if (!m_Project->snapshot || !m_Project->store) {
            return false;
        }
        return Stack::Project::IsMultiFrameProjectDocument(
            *m_Project->snapshot);
    }
    bool IsUnifiedProjectStoreActive() const {
        return m_Project->snapshot != nullptr &&
            m_Project->store != nullptr;
    }
    const Stack::Project::RawProjectSnapshot* GetActiveRawProjectSnapshot() const {
        return m_Project->snapshot.get();
    }
    bool AdoptSavedProjectStore(
        Stack::Project::ProjectStoreHandle store,
        Stack::Project::RawProjectSnapshot snapshot,
        const std::string& expectedDocumentId,
        std::uint64_t capturedEditRevision);
    Stack::Project::ProjectLifecyclePhase GetProjectLifecyclePhase() const {
        return m_Project->lifecycle.Phase();
    }
    bool CreateMultiFrameRawProject(
        const std::filesystem::path& path,
        Stack::Project::ProjectStorageKind storageKind,
        const std::string& projectName,
        const std::string& sourceSetName,
        Stack::Project::MultiFrameOperationIntent operationIntent,
        const std::vector<std::filesystem::path>& sourcePaths,
        std::size_t referenceFrameIndex,
        std::string* errorMessage = nullptr,
        const std::map<int, int>& orientationOverrides = {});
    bool AddMultiFrameSourceSet(
        const std::string& sourceSetName,
        Stack::Project::MultiFrameOperationIntent operationIntent,
        const std::vector<std::filesystem::path>& sourcePaths,
        std::string* errorMessage = nullptr);
    bool AddFramesToMultiFrameSourceSet(
        const std::string& sourceSetId,
        const std::vector<std::filesystem::path>& sourcePaths,
        std::string* errorMessage = nullptr,
        bool updateBracketRecipe = true);
    bool DuplicateMultiFrameSourceSet(
        const std::string& sourceSetId,
        std::string* errorMessage = nullptr);
    bool RenameMultiFrameSourceSet(
        const std::string& sourceSetId,
        const std::string& name,
        std::string* errorMessage = nullptr);
    bool DeleteMultiFrameSourceSet(
        const std::string& sourceSetId,
        std::string* errorMessage = nullptr);
    bool MoveMultiFrameFrame(
        const std::string& sourceSetId,
        std::size_t frameIndex,
        int direction,
        std::string* errorMessage = nullptr);
    bool SetMultiFrameFrameEnabled(
        const std::string& sourceSetId,
        const std::string& frameId,
        bool enabled,
        std::string* errorMessage = nullptr);
    bool SetMultiFrameReferenceFrame(
        const std::string& sourceSetId,
        const std::string& frameId,
        std::string* errorMessage = nullptr);
    bool SetMultiFrameFrameLabel(
        const std::string& sourceSetId,
        const std::string& frameId,
        const std::string& label,
        std::string* errorMessage = nullptr);
    bool SetMultiFrameFrameOrientation(
        const std::string& sourceSetId,
        const std::string& frameId,
        int orientation,
        std::string* errorMessage = nullptr);
    bool SetMultiFrameInternalViewTransformEnabled(
        const std::string& sourceSetId,
        bool enabled,
        std::string* errorMessage = nullptr);
    bool RemoveMultiFrameFrame(
        const std::string& sourceSetId,
        const std::string& frameId,
        std::string* errorMessage = nullptr);
    bool SetMultiFrameOperationIntent(
        const std::string& sourceSetId,
        Stack::Project::MultiFrameOperationIntent intent,
        std::string* errorMessage = nullptr);
    bool SetMultiFrameGraphDocument(
        Stack::Project::MultiFrameGraphDocument graph,
        std::string* errorMessage = nullptr);
    bool ActivateMultiFrameSourceSet(const std::string& sourceSetId);
    bool ActivateMultiFrameFrame(
        const std::string& sourceSetId,
        const std::string& frameId,
        bool selectGraphNode = true);
    bool OpenManagedMfdGraphNode(int nodeId);
    bool RequestCreateMfdProjectFromGallerySelection();
    bool RequestCreateMultiFrameProjectFromGallerySelection(
        Stack::Project::MultiFrameOperationIntent intent);
    void BeginMultiFrameGallerySelection(
        Stack::Project::MultiFrameOperationIntent intent,
        bool returnToMultiFrameOnCancel);
    bool RequestOpenRawWorkspaceProject(
        const std::filesystem::path& projectPath);
    bool RequestOpenRawWorkspaceProjectFromGallery(
        const std::filesystem::path& projectPath);
    bool SaveActiveMultiFrameRawProject(std::string* errorMessage = nullptr);
    bool SaveActiveMultiFrameRawProjectAs(
        const std::filesystem::path& destination,
        Stack::Project::ProjectStorageKind storageKind,
        std::string* errorMessage = nullptr);
    bool OptimizeActiveMultiFrameRawProject(std::string* errorMessage = nullptr);
    bool StartActiveMultiFrameProcessingForQueue(
        std::string* errorMessage = nullptr);
    bool IsActiveMultiFrameProcessingForQueueBusy() const;
    bool DidActiveMultiFrameProcessingForQueueFail(
        std::string* errorMessage = nullptr) const;
    bool GetActiveBracketingProcessingDiagnostic(
        double& progress, std::string& stage) const;

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
    void RequestOpenRawLabTab() { m_GraphEditorUsesRawLayer = false; m_OpenRawLabTabRequested = true; }
    bool ConsumeOpenRawLabTabRequest() {
        const bool requested = m_OpenRawLabTabRequested;
        m_OpenRawLabTabRequested = false;
        return requested;
    }
    void RequestOpenEditorTab() { m_OpenEditorTabRequested = true; }
    bool ConsumeOpenEditorTabRequest() {
        const bool requested = m_OpenEditorTabRequested;
        m_OpenEditorTabRequested = false;
        return requested;
    }
    void RequestOpenMultiFrameTab() { m_OpenMultiFrameTabRequested = true; }
    bool ConsumeOpenMultiFrameTabRequest() {
        const bool requested = m_OpenMultiFrameTabRequested;
        m_OpenMultiFrameTabRequested = false;
        return requested;
    }

    float GetHoverFade() const { return m_HoverFade; }
    void  SetHoverFade(float f) { m_HoverFade = f; }

    bool IsRenderOnlyUpToActive() const { return m_RenderOnlyUpToActive; }
    void SetRenderOnlyUpToActive(bool b) { m_RenderOnlyUpToActive = b; }
    bool HasProjectContent() const;
    bool HasOpenProjectSession() const;

    Async::TaskState GetSourceLoadTaskState() const { return m_SourceLoadTaskState; }
    const std::string& GetSourceLoadStatusText() const { return m_SourceLoadStatusText; }
    bool IsSourceLoadBusy() const { return Async::IsBusy(m_SourceLoadTaskState); }
    Async::TaskState GetGraphDropImportTaskState() const { return m_GraphDropImportTaskState; }
    const std::string& GetGraphDropImportStatusText() const { return m_GraphDropImportStatusText; }
    bool IsGraphDropImportBusy() const { return Async::IsBusy(m_GraphDropImportTaskState); }

    Async::TaskState GetExportTaskState() const { return m_ExportTaskState; }
    const std::string& GetExportStatusText() const { return m_ExportStatusText; }
    bool IsExportBusy() const { return Async::IsBusy(m_ExportTaskState); }
    Async::TaskState GetProjectFileSaveTaskState() const {
        return m_ProjectFileSaveTaskState;
    }
    const std::string& GetProjectFileSaveStatusText() const {
        return Async::IsBusy(m_ProjectFileSaveTaskState) ||
                m_Project->files->save.statusText.empty()
            ? m_ProjectFileSaveStatusText : m_Project->files->save.statusText;
    }
    bool IsProjectFileSaveBusy() const {
        return Async::IsBusy(m_ProjectFileSaveTaskState) ||
            Async::IsBusy(m_Project->files->save.state);
    }
    void CollectActivity(Stack::UiActivity::Snapshot& snapshot) const;
    void UpdateNotificationDecisions(bool foreground);
    Stack::Notifications::EventId RequestNotificationDecision(Stack::Notifications::NoticeSpec notice);
    void SetNotificationScope(Stack::Notifications::Notifier notifier);
    Stack::Notifications::Notifier& GetNotifier() { return m_Notifier; }
    const Stack::Notifications::Notifier& GetNotifier() const { return m_Notifier; }
    void ShowUiNotification(UiNotificationSeverity severity, std::string message, std::string dedupeKey = "");
    bool ConsumeGraphCaptureRequest(Stack::EditorGraphCapture::Request& outRequest);
    void SetGraphCaptureProgress(std::string statusText);
    Stack::Notifications::ActivityHandle GetGraphCaptureActivity() const { return m_GraphCaptureActivity; }
    void CompleteGraphCapture(Stack::EditorGraphCapture::Result result);
    void RenderGraphCaptureCanvas(
        EditorNodeGraphUI& renderer,
        EditorNodeGraph::Graph& graph,
        StackAppearance::AppearanceManager* captureAppearance,
        const Stack::EditorGraphCapture::Request& request,
        const ImVec2& canvasMin,
        const ImVec2& canvasMax);
    const Stack::RawWorkspace::WorkspaceState& GetRawWorkspaceState() const { return m_RawWorkspace; }
    void SetRawWorkspaceFolder(const std::filesystem::path& root);
    void SetRawWorkspaceFolderChangedHandler(std::function<void(const std::filesystem::path&)> handler) {
        m_RawWorkspaceFolderChangedHandler = std::move(handler);
    }
    void SetWorkspaceAppStatePersistenceEnabled(bool enabled);
    const Stack::RawWorkspace::WorkspaceState& GetRawWorkspaceStateForValidation() const { return m_RawWorkspace; }
    bool HasActiveRawWorkspacePresentationForValidation() const;
    bool IsRawWorkspaceUiInteractionActive() const {
        return m_Project->rawInteractionDraft.active ||
            m_Project->rawLayers.GestureActive() ||
            m_RawWorkspaceLabUi.zoneAreas.active ||
            std::any_of(m_RawWorkspaceLabUi.zoneAreas.graphs.begin(), m_RawWorkspaceLabUi.zoneAreas.graphs.end(),
                [](const auto& entry) {return entry.second.interaction.draggingPoint >= 0 || entry.second.interaction.draggingSegment >= 0;}) ||
            m_RawWorkspaceLocalRangeTargetDragging ||
            m_RawWorkspaceLabUi.previewPanning ||
            m_RawWorkspaceLabUi.previewZoomAnimating ||
            m_RawWorkspaceLabUi.colorWarpInteractionActive;
    }
    bool TryGetActiveRawWorkspacePresentationTexture(
        unsigned int& outTexture,
        int& outWidth,
        int& outHeight) const;
    std::string GetActiveRawWorkspacePresentationDiagnosticForValidation() const;
    const Stack::RawWorkspace::GalleryPresentation& GetRawWorkspaceGalleryPresentation();
    void EnsureRawWorkspaceLoaded();
    void OpenRawWorkspaceFolderDialog();
    void RescanRawWorkspace();
    void ClearRawWorkspaceForUser();
    void SelectRawWorkspaceSourceForPreview(const std::string& sourceKey);
    void SelectRawWorkspaceSourceForGallery(
        const std::string& sourceKey,
        bool toggle,
        bool extendRange,
        bool openForEditing);
    bool IsRawWorkspaceScanBusy() const;
    bool IsRawWorkspaceScanBlockingGallery() const;
    bool IsRawWorkspaceThumbnailBusy() const;
    bool CanEditRawWorkspaceFilmstripOrganization() const;
    bool IsRawWorkspaceProjectLoadBusy() const {
        return m_RawWorkspacePreviewStageQueued ||
            Async::IsBusy(m_RawWorkspaceProjectLoadTaskState) ||
            IsDeferredLoadedProjectApplyActive();
    }
    bool IsRawWorkspaceProjectSaveBusy() const {
        return m_Project->saves.IsBusy();
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
        return m_Project->saves.IsBusy()
            ? std::string("Saving project...")
            : std::string();
    }
    Stack::RawWorkspace::ScanProgress GetRawWorkspaceScanProgress() const;
    Stack::RawWorkspace::ThumbnailProgress GetRawWorkspaceThumbnailProgress() const;
    void PumpRawWorkspaceThumbnailTextureUploads();
    unsigned int GetRawWorkspaceThumbnailTexture(
        const Stack::RawWorkspace::SourceRecord& source,
        int* outWidth = nullptr,
        int* outHeight = nullptr,
        bool prioritize = false);
    std::shared_ptr<const Raw::RawMetadata> GetRawWorkspaceThumbnailMetadata(
        const std::string& thumbnailSourceKey) const;
    bool CopyRawEditAttributesFromGallerySource(
        const Stack::RawWorkspace::SourceRecord& source,
        std::string* errorMessage = nullptr);
    void RequestPasteRawEditAttributesForGallerySelection(
        bool includeSelectedProjects = true);
    using RawGalleryQueueRequestHandler = std::function<Stack::RawGalleryQueue::Result(
        const Stack::RawGalleryQueue::Request& request)>;
    void SetRawGalleryQueueRequestHandler(
        RawGalleryQueueRequestHandler handler) {
        m_RawGalleryQueueRequestHandler = std::move(handler);
    }
    struct RawGalleryOpenItem {
        std::filesystem::path path;
        bool project = false;
    };
    struct RawGalleryOpenSelection {
        std::vector<RawGalleryOpenItem> items;
        std::vector<std::filesystem::path> bracketSources;
    };
    using RawGalleryOpenSelectionHandler = std::function<void(
        RawGalleryOpenSelection selection, bool bracket)>;
    void SetRawGalleryOpenSelectionHandler(
        RawGalleryOpenSelectionHandler handler) {
        m_RawGalleryOpenSelectionHandler = std::move(handler);
    }
    using RawGalleryInspectionRequestHandler = std::function<void(
        const Stack::RawGalleryInspection::Request& request)>;
    void SetRawGalleryInspectionRequestHandler(
        RawGalleryInspectionRequestHandler handler) {
        m_RawGalleryInspectionRequestHandler = std::move(handler);
    }
    void CompleteRawGalleryInspection(
        Stack::RawGalleryInspection::Result result);
    void RenderRawEditAttributePasteDialog();

    CanvasToolKind GetCanvasToolKind() const { return m_CanvasToolKind; }
    int GetCanvasToolOwnerNodeId() const { return m_CanvasToolOwnerNodeId; }
    const std::string& GetCanvasToolStatusText() const { return m_CanvasToolStatusText; }
    bool HasActiveCanvasTool() const { return m_CanvasToolKind != CanvasToolKind::None; }
    bool IsCanvasToolActiveForNode(int nodeId, CanvasToolKind kind) const {
        return m_CanvasToolKind == kind && m_CanvasToolOwnerNodeId == nodeId;
    }
    void BeginCanvasColorPick(int ownerNodeId, const std::string& statusText, std::function<void(float, float, float)> callback);
    void BeginCanvasColorPickFromNodeInput(int ownerNodeId, const std::string& statusText, std::function<void(float, float, float)> callback);
    void CancelCanvasTool();
    void OnCanvasColorPicked(float r, float g, float b);
    bool IsPickingColor() const { return m_IsPickingColor; }
    bool IsCanvasColorPickSamplingNodeInput() const { return m_CanvasColorPickSamplesNodeInput; }
    bool SampleCanvasColorPickPixel(float u, float v, std::array<float, 4>& outRgba) const;
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
    void SetLibraryWindowHovered(bool hovered) { m_LibraryWindowHovered = hovered; }
    bool IsLibraryWindowHovered() const { return m_LibraryWindowHovered; }

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
    bool BuildSingleOutputExportRaster(std::vector<unsigned char>& outPixels, int& outW, int& outH);
    void BeginPngExportWrite(
        std::string path,
        std::vector<unsigned char> pixels,
        int width,
        int height,
        Stack::NodeMath::PngColorMetadataChunks colorChunks,
        std::uint64_t generation);
    void CompleteRawWorkspaceExportRender(
        EditorRenderWorker::Result& result,
        bool sourceMatchesActive);
    bool BuildSingleOutputTimelineFrameRaster(int timelineFrame, std::vector<unsigned char>& outPixels, int& outW, int& outH);
    void ClampCompositeViewPanToContent(const ImVec2& canvasSize);
    void RefreshGraphLayerMetadata();
    float GetNodesPanelWidthAnim() const { return m_NodesPanelWidthAnim; }
    bool AddGeneratedLutNodeFromPayload(EditorNodeGraph::LutPayload payload);

private:
    std::shared_ptr<Stack::Project::ProjectSession> m_Project;
    std::function<bool(const std::filesystem::path&)> m_ProjectRemovalGuard;
    using PendingGraphDropImportRequest = Stack::EditorModuleTypes::PendingGraphDropImportRequest;
    using CachedCompositeChainState = Stack::EditorModuleTypes::CachedCompositeChainState;
    using ToneCurveViewportInteractionCache = Stack::EditorModuleTypes::ToneCurveViewportInteractionCache;
    using DevelopAutoGuidanceDraftState = Stack::EditorModuleTypes::DevelopAutoGuidanceDraftState;
    using RawDevelopExposureDraftState = Stack::EditorModuleTypes::RawDevelopExposureDraftState;
    using NodeBrowserThumbnailRuntimeEntry = Stack::EditorModuleTypes::NodeBrowserThumbnailRuntimeEntry;
    using NodeBrowserPreviewRequestMeta = Stack::EditorModuleTypes::NodeBrowserPreviewRequestMeta;
    using NodeBrowserPreviewSeed = Stack::EditorModuleTypes::NodeBrowserPreviewSeed;



    struct RawWorkspaceScanSnapshot {
        Async::TaskState state = Async::TaskState::Idle;
        std::uint64_t generation = 0;
        bool sourcesPublished = false;
        bool completedSuccessfully = false;
        Stack::RawWorkspace::ScanProgress progress;
        std::string statusText;
        std::string errorMessage;
    };

    using RawWorkspaceThumbnailWorkItem =
        Stack::Editor::RawWorkspaceInternal::RawWorkspaceThumbnailWorkItem;

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
        std::string metadataIdentity;
        std::shared_ptr<const Raw::RawMetadata> metadata;
        bool metadataChecked = false;
        Stack::RawWorkspace::ThumbnailStatus status = Stack::RawWorkspace::ThumbnailStatus::Unknown;
        int width = 0;
        int height = 0;
        Async::TaskState decodeState = Async::TaskState::Idle;
        std::uint64_t requestGeneration = 0;
        bool uploadPending = false;
        bool uploadQueued = false;
        bool priority = false;
        std::vector<unsigned char> decodedPixels;
        int decodedWidth = 0;
        int decodedHeight = 0;
        std::uint64_t lastUseSerial = 0;
        int lastVisibleFrame = -1;
    };

    struct RawWorkspaceRecipePreviewCacheEntry {
        std::filesystem::path projectPath;
        std::int64_t projectModifiedTimeTicks = 0;
        Stack::RawRecipe::RawDevelopmentRecipe recipe;
        Stack::RawWorkspace::RawProjectMode mode = Stack::RawWorkspace::RawProjectMode::UnifiedLayers;
        bool success = false;
        std::string errorMessage;
    };

    struct RawWorkspaceGraphScopeCacheEntry {
        std::string sourceKey;
        std::size_t inputFingerprint = 0;
        RawDevelopmentGraphScopeReadback readback;
    };

    struct MfdExperimentalParameterDraft {
        std::string sourceSetId;
        std::uint64_t inputRevision = 0;
        double motionDisagreementHardLimitRawPixels = 0.75;
        double trustedPixelZeroWeightSigma = 5.0;
        double oneAlternateWeightCapRelativeToReference = 4.0;
        double exactFallbackAlternateToReferenceRatio = 0.05;
        int fusionMethod = 1;
        double fusionSmoothing = 0.70;
        double memoryBudgetGiB = 0.0;
        int alignmentMode = 0;
        bool initialized = false;
    };

    struct MfdExperimentalFrameReport {
        std::string label;
        std::string message;
        std::string noiseModelQuality;
        bool reference = false;
        bool attempted = false;
        bool acceptedForFusion = false;
        bool exposureGrouped = false;
        double exposureDriftEv = 0.0;
    };

    struct MfdExperimentalProcessingReport {
        std::string projectId;
        std::string sourceSetId;
        std::uint64_t inputRevision = 0;
        std::string statusName;
        std::string message;
        std::string executionBackend;
        std::string gpuDeviceIdentity;
        std::string gpuFallbackReason;
        std::uint32_t gpuDispatchedTileCount = 0u;
        std::string registrationBackend;
        std::string registrationGpuDeviceIdentity;
        std::string registrationGpuFallbackReason;
        std::uint32_t registrationGpuDispatchCount = 0u;
        std::uint64_t registrationGpuScoredCandidateCount = 0u;
        std::filesystem::path inspectionDirectory;
        std::filesystem::path outputPreviewPath;
        std::filesystem::path referencePreviewPath;
        std::uint64_t compatibleAlternateCount = 0;
        std::uint64_t selectedCaptureCount = 0;
        std::uint64_t acceptedAlternateCount = 0;
        std::uint64_t exposureGroupedCaptureCount = 0;
        std::uint64_t exposureExcludedCaptureCount = 0;
        std::uint64_t estimatedPeakResidentBytes = 0;
        std::uint64_t memoryBudgetBytes = 0;
        std::uint64_t availablePhysicalBytesAtStart = 0;
        std::uint64_t protectedMemoryReserveBytes = 0;
        bool automaticMemoryBudget = true;
        bool memoryBudgetConstrained = false;
        std::uint64_t computedTileCount = 0;
        std::uint64_t cacheHitTileCount = 0;
        double contributingPixelFraction = 0.0;
        double exactReferencePixelFraction = 0.0;
        double meanAbsoluteDelta = 0.0;
        double percentile99AbsoluteDelta = 0.0;
        double meanEffectiveSampleCount = 1.0;
        double meanRobustAttenuation = 1.0;
        double predictedIndependentNoiseReduction = 1.0;
        bool independentNoiseReductionClaimQualified = true;
        std::vector<MfdExperimentalFrameReport> frames;
    };

    using MfdAdoptedRawResult = Stack::Project::MfdAdoptedRawResult;

    struct MfdExperimentalProcessingProgressState {
        std::mutex mutex;
        std::string stageLabel = "Queued";
        std::string message = "Waiting for the processing worker.";
        double overallFraction = 0.0;
        double stageFraction = 0.0;
        std::uint64_t completedUnits = 0u;
        std::uint64_t totalUnits = 0u;
        std::uint32_t frameOrdinal = 0u;
        std::uint32_t frameCount = 0u;
        std::chrono::steady_clock::time_point startedAt =
            std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point lastAdvancedAt = startedAt;
    };

    struct HdrProcessingReport {
        std::string projectId;
        std::string sourceSetId;
        std::uint64_t inputRevision = 0;
        std::string statusName;
        std::string message;
        std::string cacheKey;
        std::string executionBackend;
        std::string gpuDeviceIdentity;
        std::string gpuFallbackReason;
        std::uint32_t gpuDispatchedTileCount = 0u;
        double exposureSpanEv = 0.0;
        double meanEffectiveSamples = 0.0;
        std::uint64_t colorCoherentRepairPixelCount = 0u;
        std::vector<std::string> warnings;
        std::vector<Raw::Hdr::FrameDiagnostic> frames;
        std::vector<Raw::Hdr::ExposureFitEdgeDiagnostic> exposureFitEdges;
    };

    using HdrAdoptedRawResult = Stack::Project::HdrAdoptedRawResult;

    EditorSidebar m_Sidebar;
    EditorViewport m_Viewport;
    EditorScopes m_Scopes;
    RenderPipeline m_Pipeline;
    EditorRenderWorker m_RenderWorker;
    Stack::EditorRendering::RawRenderService::ClientId
        m_RawRenderClientId = 0;
    static constexpr std::uint64_t
        kRawRenderProcessingContractRevision = 3;
    std::string m_RawRenderSessionSourceIdentity;
    std::uint64_t m_RawRenderSessionSourceHash = 0;
    std::uint64_t m_RawRenderSessionGraphStructureRevision = 0;
    std::uint64_t m_RawRenderSessionContractRevision = 0;
    int m_RawRenderSessionFullFrameWidth = 0;
    int m_RawRenderSessionFullFrameHeight = 0;
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
    unsigned int m_RawFolderIconTexture = 0;
    unsigned int m_RawRefreshIconTexture = 0;
    unsigned int m_RawClearIconTexture = 0;
    unsigned int m_RawGridIconTexture = 0;
    unsigned int m_RawListIconTexture = 0;
    unsigned int m_RawGalleryIconTexture = 0;
    unsigned int m_RawGalleryOptionsIconTexture = 0;
    unsigned int m_RawSidebarIconTextures[8] = {};
    unsigned int m_RawLabGalleryEyeClosedIconTexture = 0;
    unsigned int m_RawLabGalleryEyeOpenIconTexture = 0;
    unsigned int m_ChevronIconTexture = 0;
    bool m_LeftPanelExpanded = false;
    float m_LeftPanelWidthAnim = 0.0f;
    float m_LeftPanelHoverGrace = 0.0f;
    float m_NodesPanelWidthAnim = 0.0f;
    bool m_TexturesLoaded = false;
    std::map<int, double> m_ToolbarButtonSpawnTimes;
    double m_SpacebarPressTime = 0.0;
    bool m_SpacebarHeld = false;
    Stack::EditorGraphCapture::Settings m_GraphCaptureSettings;
    std::optional<Stack::EditorGraphCapture::Request> m_PendingGraphCaptureRequest;
    Stack::EditorGraphCapture::Result m_LastGraphCaptureResult;
    bool m_GraphCaptureWindowOpen = false;
    bool m_GraphCaptureFocusRequested = false;
    bool m_GraphCaptureBusy = false;
    Stack::Notifications::ActivityHandle m_GraphCaptureActivity;
    bool m_GraphCaptureSavePending = false;
    std::uint64_t m_GraphCaptureSaveGeneration = 0;
    int m_GraphCaptureShortcutGuardFrames = 0;
    std::string m_GraphCaptureStatusText;

    void LoadResourceTextures();
    void UnloadResourceTextures();
    void RenderFloatingToolbar();
    void OpenGraphCaptureWindow();
    void RenderGraphCaptureWindow();
    bool BeginGraphCaptureSave(Stack::EditorGraphCapture::Request request, std::string& error);

    struct FrequencyGraphHistoryNode {
        std::size_t index = 0;
        EditorNodeGraph::Node node;
    };
    struct FrequencyGraphHistoryLink {
        std::size_t index = 0;
        EditorNodeGraph::Link link;
    };
    struct FrequencyGraphHistoryPatch {
        std::vector<FrequencyGraphHistoryNode> beforeNodes;
        std::vector<FrequencyGraphHistoryNode> afterNodes;
        std::vector<FrequencyGraphHistoryLink> beforeLinks;
        std::vector<FrequencyGraphHistoryLink> afterLinks;
        std::vector<int> beforeSelection;
        std::vector<int> afterSelection;
        std::optional<EditorNodeGraph::Link> beforeSelectedLink;
        std::optional<EditorNodeGraph::Link> afterSelectedLink;
        int beforeNextNodeId = 1;
        int afterNextNodeId = 1;
        std::uint64_t expectedBeforeRevision = 0;
        std::uint64_t expectedAfterRevision = 0;
    };

    bool ApplyFrequencyGraphHistoryPatch(
        FrequencyGraphHistoryPatch& patch,
        bool applyAfter);

    std::optional<FrequencyGraphHistoryPatch> m_FrequencyGraphUndo;
    std::optional<FrequencyGraphHistoryPatch> m_FrequencyGraphRedo;
    mutable Stack::NodeMath::ValueDescriptor m_LastGraphOutputSemanticDescriptor;
    mutable std::string m_LastGraphOutputSemanticDescriptorIdentity;
    mutable std::unordered_map<std::string, Stack::NodeMath::ValueDescriptor>
        m_LastGraphLinkSemanticDescriptors;
    mutable std::vector<Stack::NodeMath::Diagnostic> m_LastGraphSemanticDiagnostics;
    mutable EditorNodeGraph::GraphOutputDescriptions m_GraphOutputDescriptions;
    mutable std::uint64_t m_GraphOutputDescriptionRenderRevision = 0;
    mutable std::uint64_t m_GraphOutputDescriptionStructureRevision = 0;
    std::unordered_map<std::string, Stack::NodeMath::FirstClassValue>
        m_LastGraphUniformOutputValues;
    std::uint64_t m_LastGraphUniformOutputGeneration = 0;

    int m_SelectedLayerIndex = -1;
    bool m_FocusSelectedTabNextRender = false;
    bool m_OpenRawWorkspaceTabRequested = false;
    bool m_OpenRawLabTabRequested = false;
    bool m_OpenEditorTabRequested = false;
    bool m_OpenMultiFrameTabRequested = false;
    bool m_RawWorkspaceRootTabActive = false;
    bool m_RawWorkspaceLockedByEditorProject = false;
    bool m_ShowRawWorkspaceCloseProjectPopup = false;
    bool m_RawWorkspaceCloseAfterSave = false;
    bool m_ShowRawWorkspaceReplaceProjectPopup = false;
    bool m_RawWorkspaceReplacementAuthorized = false;
    bool m_RawWorkspaceReplacementSavePending = false;
    bool m_RawWorkspaceReplacementExecuteAfterSave = false;
    std::string m_RawWorkspaceReplacementActionLabel;
    std::string m_RawWorkspaceReplacementTargetLabel;
    std::string m_RawWorkspaceReplacementDiscardOpenSourceKey;
    std::function<bool(std::string*)> m_PendingRawWorkspaceProjectReplacement;
    std::string m_PendingRawWorkspaceExplicitOpenSourceKey;
    std::string m_RawWorkspaceExplicitReplacementSourceKey;
    std::string m_RawWorkspaceReplacementSkipSaveSourceKey;
    float m_HoverFade = 0.0f;
    bool m_RenderOnlyUpToActive = false;
    bool m_ActivityRawRenderIsProxy = false;
    std::unique_ptr<Stack::AutoBracket::AutoBracketCoordinator> m_AutoBracket;
    std::unique_ptr<Stack::AutoBracket::Presentation> m_AutoBracketPresentation;
    bool m_IsAutoBracketWorkspace=false, m_AutoBracketWorkspaceRequested=false, m_AutoBracketWorkspaceClosing=false;
    bool m_AutoBracketCatalogChanged=false;
    std::vector<std::string> m_AutoBracketProtectedProjectIds;
    std::vector<std::filesystem::path> m_AutoBracketProtectedProjectPaths;
    std::string m_AutoBracketRoot;
    std::uint64_t m_AutoBracketCatalogRevision=0, m_AutoBracketGroupingGeneration=0, m_AutoBracketOrganizationRevision=0;
    Stack::RawWorkspace::GalleryPresentation m_RawWorkspaceCategoryPresentation;
    std::uint64_t m_RawWorkspaceCategoryRevision=0;
    std::uint64_t m_RawWorkspaceCategoryQueueRevision=0;
    Stack::RawWorkspace::GalleryContentMode m_RawWorkspaceCategoryMode=Stack::RawWorkspace::GalleryContentMode::Gallery;
    bool m_DocumentPersistenceEnabled = true;
    bool m_ProjectNamingPromptRequested = false;
    bool m_ProjectNamingPromptShown = false;
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
    std::uint64_t m_NextGraphImageImportRequestId = 1;
    Stack::Notifications::Notifier m_Notifier;
    std::string m_LastOutputConnectionDiagnostic;
    std::vector<PendingGraphDropImportRequest> m_PendingGraphDropImports;
    std::deque<EditorRenderWorker::SharedTextureResult> m_DeferredViewportOutputTextureReleases;
    std::deque<EditorRenderWorker::SharedTextureTileSet> m_DeferredViewportOutputTileReleases;
    Stack::RawWorkspace::WorkspaceState m_RawWorkspace;
    std::optional<Stack::RawWorkspace::SourceRecord> m_PinnedRawWorkspaceSource;
    std::string m_RawWorkspaceSelectedSourceBeforePinnedProject;
    std::uint64_t m_RawWorkspaceScanGeneration = 0;
    int m_RawGalleryShortcutFrame = -1;
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
    mutable std::mutex m_RawWorkspaceProjectFileWriteMutex;
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
    bool m_WorkspaceAppStatePersistenceEnabled = true;
    std::function<void(const std::filesystem::path&)> m_RawWorkspaceFolderChangedHandler;
    Stack::RawWorkspace::GalleryDisplayMode m_RawWorkspaceGalleryDisplayMode =
        Stack::RawWorkspace::GalleryDisplayMode::Grid;
    Stack::RawWorkspace::GalleryContentMode m_RawWorkspaceGalleryContentMode =
        Stack::RawWorkspace::GalleryContentMode::Gallery;
    bool m_RawWorkspaceGalleryWindowOpen = false;
    RawWorkspaceLayoutUiState m_RawWorkspaceLayoutUi;
    RawWorkspaceLabUiState m_RawWorkspaceLabUi;
    Stack::Editor::ProjectInteractionState m_ProjectInteractionUi;
    std::shared_ptr<Stack::Editor::RawLabInternal::GalleryDetails> m_RawWorkspaceGalleryDetails;
    Stack::Editor::RawLabInternal::GalleryPreviewState m_RawWorkspaceGalleryPreview;
    Stack::Editor::RawLabInternal::GalleryLayoutAnimation m_RawWorkspaceGalleryLayoutAnimation;
    Stack::Editor::RawLabInternal::GalleryGridStacks m_RawWorkspaceGalleryGridStacks;
    std::optional<RawWorkspaceLabUiState> m_WorkspacePreviewUi;
    float m_WorkspacePreviewPaneWidth = 0;
    EditorSubWindow m_WorkspacePreviewActiveSubWindow{}, m_WorkspacePreviewTargetSubWindow{};
    bool m_RawWorkspaceLabGalleryHeaderExpanded = true;
    float m_RawWorkspaceLabAnimatedGalleryHeaderAmount = 0.0f;
    bool m_RawWorkspaceLabGalleryPanelsOpen = true;
    float m_RawWorkspaceLabGalleryPanelsAnimation = 0.0f;
    bool m_RawWorkspaceGalleryPreviewHovered = false;
    bool m_RawWorkspaceGalleryDrawerNeedsInitialExpansion = true;
    float m_RawWorkspaceLabAnimatedLowerShelfHeight = 0.0f;
    float m_RawWorkspaceLabAnimatedFilmstripHeight = 0.0f;
    float m_RawWorkspaceLabFilmstripAnimationOpenHeight = 0.0f;
    Stack::RawWorkspace::RawGalleryFilmstripDrawerState
        m_RawWorkspaceLabFilmstripDrawerState;
    float m_RawWorkspaceLabFilmstripDrawerAnimatedHeight = 0.0f;
    float m_RawWorkspaceLabFilmstripDrawerFrozenTargetHeight = 0.0f;
    bool m_RawWorkspaceLabFilmstripResizeDirty = false;
    bool m_RawWorkspaceLabRailResizeDirty = false;
    bool m_RawWorkspaceLabFilmstripDrawerPointerInside = false;
    std::string m_RawWorkspaceLabFilmstripHoverSourceKey;
    std::filesystem::path m_RawWorkspaceLabFilmstripHoverProjectPath;
    std::string m_RawWorkspaceLabFilmstripPreviousHoverSourceKey;
    std::filesystem::path m_RawWorkspaceLabFilmstripPreviousHoverProjectPath;
    int m_RawWorkspaceLabFilmstripHoverFrame = -1;
    bool m_RawWorkspaceLabFilmstripHoverSuppressed = false;
    float m_RawWorkspaceLabFilmstripHoverOpacity = 0.0f;
    float m_RawWorkspaceLabEditingSurfaceReveal = 1.0f;
    float m_RawWorkspaceLabFilmstripHoverBlend = 1.0f;
    bool m_RawWorkspaceLabFilmstripDrawerInteractionRetained = false;
    bool m_RawWorkspaceLabFilmstripDrawerKeyboardToggleRequested = false;
    std::string m_RawWorkspaceLabFilmstripDrawerWorkspaceKey;
    std::unordered_map<std::string, std::vector<std::string>>
        m_RawWorkspaceLabFilmstripDrawerSessionOrders;
    float m_RawWorkspaceLabFilmstripScrollTargetX = -1.0f;
    struct RawLabFilmstripTimelineEntry {
        std::string label;
        std::int64_t firstTimestamp = 0;
        std::int64_t lastTimestamp = 0;
        std::size_t imageCount = 1;
    };
    std::vector<RawLabFilmstripTimelineEntry>
        m_RawWorkspaceLabFilmstripTimelineEntries;
    std::size_t m_RawWorkspaceLabFilmstripTimelineHoveredIndex =
        std::numeric_limits<std::size_t>::max();
    std::string m_RawWorkspaceLabFilmstripTimelineHoveredSourceKey;
    bool m_RawWorkspaceLabFilmstripTimelineHoveredStackMember = false;
    float m_RawWorkspaceLabFilmstripTimelineHoveredCenterX = 0.0f;
    struct FilmstripDateLabel {
        std::string date, time;
        float opacity = 0.0f;
    };
    std::vector<FilmstripDateLabel> m_FilmstripDateLabels;
    float m_FilmstripDateCenterX = 0.0f;
    int m_FilmstripDateLastFrame = -2;
    float m_RawWorkspaceLabFilmstripTimelineContentOriginX = 0.0f;
    float m_RawWorkspaceLabFilmstripScrollLastAppliedX = -1.0f;
    enum class RawGalleryFilmstripDragPhase {
        Idle,
        Dragging,
        AwaitingStackChoice,
        Returning,
        Settling
    };
    enum class RawGalleryFilmstripDropKind {
        None,
        Group,
        Reorder
    };
    struct RawGalleryFilmstripDragState {
        RawGalleryFilmstripDragPhase phase =
            RawGalleryFilmstripDragPhase::Idle;
        RawGalleryFilmstripDropKind dropKind =
            RawGalleryFilmstripDropKind::None;
        std::string sourceKey;
        std::vector<std::string> sourceStackMembers;
        std::vector<std::string> targetStackMembers;
        std::vector<Stack::RawWorkspace::RawGallerySimilarityStack>
            dropVisibleStacks;
        std::vector<std::string> selectedSourceKeysBeforeDrag;
        std::string selectedSourceKeyBeforeDrag;
        std::size_t insertionStackIndex = 0;
        ImVec2 grabOffset = ImVec2(0.0f, 0.0f);
        ImVec2 originMinimum = ImVec2(0.0f, 0.0f);
        ImVec2 proxyMinimum = ImVec2(0.0f, 0.0f);
        ImVec2 animationStartMinimum = ImVec2(0.0f, 0.0f);
        ImVec2 animationEndMinimum = ImVec2(0.0f, 0.0f);
        ImVec2 tileSize = ImVec2(0.0f, 0.0f);
        float imageHeight = 0.0f;
        float lastObservedScrollX = -1.0f;
        double phaseStartedAt = 0.0;
        int imageTargetPreviewFrame = -1;
        bool collapsedStackSource = false;
        bool popupRequested = false;
        bool deliveryHandledThisFrame = false;
    };
    RawGalleryFilmstripDragState m_RawWorkspaceLabFilmstripDragState;
    struct RawGalleryFolderAnimationState {
        float expansion = 1.0f;
        bool initialized = false;
    };
    std::unordered_set<std::string> m_RawWorkspaceLabCollapsedGalleryGroups;
    std::unordered_map<std::string, RawGalleryFolderAnimationState>
        m_RawWorkspaceLabGalleryGroupAnimations;
    std::unordered_map<std::string, float>
        m_RawWorkspaceLabGalleryMiniThumbnailHover;
    std::string m_RawWorkspaceLabGalleryScrollWorkspaceKey;
    float m_RawWorkspaceLabGalleryScrollTargetY = -1.0f;
    float m_RawWorkspaceLabGalleryScrollCurrentY = -1.0f;
    std::string m_RawLabGalleryScrollToSourceKey;
    std::string m_RawLabGalleryFlashSourceKey;
    double m_RawLabGalleryFlashStartTime = 0.0;
    std::filesystem::path m_RawWorkspaceLabFocusedProjectPath;
    std::string m_RawWorkspaceLabFocusedProjectName;
    std::vector<std::filesystem::path>
        m_RawWorkspaceLabSelectedProjectPaths;
    std::filesystem::path m_RawWorkspaceLabProjectSelectionAnchor;
    RawGalleryQueueRequestHandler m_RawGalleryQueueRequestHandler;
    RawGalleryOpenSelectionHandler m_RawGalleryOpenSelectionHandler;
    bool m_PermanentGalleryWorkspace = false;
    std::function<void()> m_GalleryNavigationHandler;
    RawGalleryInspectionRequestHandler m_RawGalleryInspectionRequestHandler;
    std::uint64_t m_RawGalleryInspectionNextRequestId = 1;
    std::uint64_t m_RawGalleryInspectionActiveRequestId = 0;
    std::string m_RawGalleryInspectionRequestKey;
    std::string m_RawGalleryInspectionDisplayName;
    bool m_RawGalleryInspectionShowBefore = false;
    bool m_RawGalleryInspectionLoading = false;
    std::string m_RawGalleryInspectionStatus;
    unsigned int m_RawGalleryInspectionTexture = 0;
    int m_RawGalleryInspectionTextureWidth = 0;
    int m_RawGalleryInspectionTextureHeight = 0;
    std::vector<std::filesystem::path>
        m_RawWorkspacePendingRevertProjectPaths;
    bool m_RawWorkspaceRevertPopupRequested = false;
    double m_RawWorkspaceRevertConfirmationStartedAt = 0.0;
    struct RawEditAttributePasteTarget {
        Stack::RawWorkspace::SourceRecord source;
        std::filesystem::path projectPath;
        std::string sourceSetId;
        std::string displayName;
        bool createProject = false;
    };
    struct RawEditAttributePasteProgressState {
        std::atomic<std::uint64_t> completed { 0 };
        std::atomic<std::uint64_t> succeeded { 0 };
        std::atomic<std::uint64_t> failed { 0 };
        std::atomic<std::uint64_t> skipped { 0 };
        std::atomic<bool> cancelRequested { false };
        std::uint64_t total = 0;
        mutable std::mutex mutex;
        std::string currentItem;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        std::vector<std::filesystem::path> createdProjectPaths;
    };
    std::optional<Stack::RawRecipe::RawEditAttributeBundle>
        m_RawEditAttributeClipboard;
    std::string m_RawEditAttributeClipboardObservedText;
    std::unordered_set<std::string> m_RawEditAttributePasteSelection;
    std::vector<RawEditAttributePasteTarget> m_RawEditAttributePasteTargets;
    bool m_RawEditAttributePastePopupRequested = false;
    Async::TaskState m_RawEditAttributePasteTaskState =
        Async::TaskState::Idle;
    std::shared_ptr<RawEditAttributePasteProgressState>
        m_RawEditAttributePasteProgress;
    std::atomic<std::uint64_t> m_RawEditAttributePasteGeneration { 0 };
    std::string m_RawEditAttributePasteStatusText;
    std::filesystem::path m_RawEditAttributePasteAutoOpenProjectPath;
    bool m_RawWorkspaceLabDrawerAnimationInitialized = false;
    std::string m_RawWorkspaceRestormerPackageStatusText;
    std::string m_RawWorkspaceStaleRenderStatusText;
    bool m_RawWorkspaceLabNativeGalleryRequestFocus = false;
    bool m_RawWorkspaceLabNativeGalleryPlacementInitialized = false;
    bool m_RawWorkspaceLabNativeGalleryShown = false;
    bool m_RawWorkspaceLabNativeGalleryFirstPresented = false;
    bool m_RawWorkspaceLabNativeGalleryLayoutDetached = false;
    int m_RawWorkspaceLabNativeGalleryPlatformWaitFrames = 0;
    int m_RawWorkspaceLabNativeGalleryFocusAttempts = 0;
    ImGuiID m_RawWorkspaceLabNativeGalleryViewportId = 0;
    GLFWwindow* m_RawWorkspaceLabNativeGalleryStyledWindow = nullptr;
    ImU32 m_RawWorkspaceLabNativeGalleryStyledSurfaceColor = 0;
    ImU32 m_RawWorkspaceLabNativeGalleryStyledTextColor = 0;
    ImVec4 m_RawWorkspaceLabNativeGallerySurfaceColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImVec2 m_RawWorkspaceLabNativeGalleryMonitorPos = ImVec2(0.0f, 0.0f);
    ImVec2 m_RawWorkspaceLabNativeGalleryMonitorSize = ImVec2(0.0f, 0.0f);
    ImVec2 m_RawWorkspaceLabNativeGalleryWindowPos = ImVec2(0.0f, 0.0f);
    ImVec2 m_RawWorkspaceLabNativeGalleryWindowSize = ImVec2(0.0f, 0.0f);
    std::uint64_t m_RawWorkspaceGalleryRevision = 1;
    std::uint64_t m_RawWorkspaceGalleryPresentationRevision = 0;
    Stack::RawWorkspace::GalleryPresentation m_RawWorkspaceGalleryPresentationCache;
    std::atomic<std::uint64_t> m_RawWorkspaceSimilarityGeneration { 0 };
    std::uint64_t m_RawWorkspaceSimilarityPublishedGeneration = 0;
    mutable std::vector<Stack::RawWorkspace::RawGallerySimilarityStack>
        m_RawWorkspaceFilmstripStacksCache;
    mutable std::size_t m_RawWorkspaceFilmstripMaximumStackSizeCache = 1u;
    mutable std::string m_RawWorkspaceFilmstripStacksCacheWorkspaceKey;
    mutable std::uint64_t m_RawWorkspaceFilmstripStacksCacheGalleryRevision = 0;
    mutable std::uint64_t m_RawWorkspaceFilmstripStacksCacheSimilarityGeneration = 0;
    mutable std::uint64_t m_RawWorkspaceFilmstripStacksCacheOrganizationRevision = 0;
    std::uint64_t m_RawWorkspaceFilmstripOrganizationRevision = 1;
    std::vector<Stack::RawWorkspace::RawGallerySimilarityStack>
        m_RawWorkspaceLabVisibleGalleryStacksCache;
    std::unordered_map<std::string, const Stack::RawWorkspace::GallerySourceView*>
        m_RawWorkspaceLabGalleryViewsBySource;
    std::string m_RawWorkspaceLabVisibleGalleryWorkspaceKey;
    std::uint64_t m_RawWorkspaceLabVisibleGalleryRevision = 0;
    std::uint64_t m_RawWorkspaceLabVisibleGallerySimilarityGeneration = 0;
    std::uint64_t m_RawWorkspaceLabVisibleGalleryOrganizationRevision = 0;
    bool m_RawWorkspaceLabFilmstripStackTimelineActive = false;
    bool m_RawWorkspaceLabFilmstripProjectTimelineActive = false;
    std::uint64_t m_RawWorkspaceLabFilmstripProjectTimelineRevision = 0;
    std::vector<const Stack::RawWorkspace::SourceRecord*>
        m_RawWorkspaceLabFilmstripProjectReferenceSources;
    std::string m_RawWorkspaceSimilarityWorkspaceKey;
    std::vector<Stack::RawWorkspace::RawGallerySimilarityStack>
        m_RawWorkspaceSimilarityStacks;
    std::unordered_map<
        std::string,
        Stack::RawWorkspace::RawGalleryManualGrouping>
        m_RawWorkspaceManualGroupings;
    struct RawGalleryFilmstripStackAnimationState {
        float slotX = 0.0f;
        bool initialized = false;
    };
    std::unordered_map<std::string, RawGalleryFilmstripStackAnimationState>
        m_RawWorkspaceLabFilmstripStackAnimations;
    std::unordered_map<std::string, float>
        m_RawWorkspaceLabFilmstripSourceLastSlotX;
    bool m_RawWorkspaceSimilarityWorkerActive = false;
    bool m_RawWorkspaceSimilarityRebuildPending = false;
    std::size_t m_RawWorkspaceSimilarityActiveSourceCount = 0;
    std::size_t m_RawWorkspaceSimilarityLastStackCount = 0;
    std::unordered_map<std::string, RawWorkspaceThumbnailTexture> m_RawWorkspaceThumbnailTextures;
    std::deque<std::string> m_RawWorkspaceThumbnailTextureUploadQueue;
    std::deque<unsigned int> m_RawWorkspaceThumbnailTextureDeleteQueue;
    Stack::Editor::RawWorkspaceInternal::RawWorkspaceThumbnailScheduler
        m_RawWorkspaceThumbnailScheduler;
    bool m_RawWorkspaceThumbnailWorkerActive = false;
    std::unordered_map<std::string, int> m_RawWorkspaceThumbnailDecodeRepairAttempts;
    std::atomic<std::uint64_t> m_RawWorkspaceThumbnailTextureResetGeneration { 0 };
    std::uint64_t m_RawWorkspaceThumbnailTextureRequestGeneration = 0;
    std::uint64_t m_RawWorkspaceThumbnailTextureUseSerial = 0;
    int m_RawWorkspaceThumbnailTextureRequestFrame = -1;
    std::size_t m_RawWorkspaceThumbnailTextureRequestsThisFrame = 0;
    int m_RawWorkspaceThumbnailTextureUploadFrame = -1;
    std::size_t m_RawWorkspaceThumbnailTextureUploadsThisFrame = 0;
    std::size_t m_RawWorkspaceThumbnailTextureUploadBytesThisFrame = 0;
    int m_RawWorkspaceThumbnailTextureDeleteFrame = -1;
    std::size_t m_RawWorkspaceThumbnailTextureDeletesThisFrame = 0;
    mutable std::unordered_map<std::string, RawWorkspaceRecipePreviewCacheEntry> m_RawWorkspaceRecipePreviewCache;
    using RawInteractionDraft = Stack::Project::RawInteractionDraft;
    std::string m_RawWorkspacePreviewStageFailureSourceKey;
    bool m_RawWorkspacePreviewStageQueued = false;
    std::string m_RawWorkspacePreviewStageSourceKey;
    std::string m_PendingDeleteMultiFrameSourceSetId;
    std::string m_PendingDeleteMultiFrameFrameSetId;
    std::string m_PendingDeleteMultiFrameFrameId;
    bool m_OpenMultiFrameDeletePopup = false;
    bool m_OpenMultiFrameFrameDeletePopup = false;
    bool m_OpenRawSourceSetProjectBrowser = false;
    bool m_OpenMultiFrameCreationPopup = false;
    bool m_PopulateMultiFrameCreationFromGallery = false;
    bool m_RequestCreateMultiFrameFromGallerySelection = false;
    Stack::Project::MultiFrameOperationIntent m_PendingMultiFrameCreationIntent =
        Stack::Project::MultiFrameOperationIntent::RawCaptureSet;
    std::vector<std::filesystem::path> m_PendingMultiFrameGallerySourcePaths;
    std::string m_RawWorkspaceGallerySelectionRestoreKey;
    std::vector<std::string> m_RawWorkspaceGallerySelectionRestoreKeys;
    RawGalleryNavigationMode m_RawWorkspaceGalleryRestoreNavigationMode =
        RawGalleryNavigationMode::ProjectRoot;
    std::string m_RawWorkspaceGalleryRestoreProjectId;
    RawGalleryHost m_RawWorkspaceGalleryRestoreHost = RawGalleryHost::Closed;
    bool m_ReturnToMultiFrameAfterGalleryCreationCancel = false;
    enum class MultiFrameWorkspaceSelection {
        CaptureSet,
        CaptureSubset,
        Frame,
        MatchAndAlign,
        Fusion,
        Publish
    };
    MultiFrameWorkspaceSelection m_MultiFrameWorkspaceSelection =
        MultiFrameWorkspaceSelection::CaptureSet;
    std::string m_MultiFrameWorkspaceSelectedFrameId;
    std::string m_MultiFrameWorkspaceSelectedNodeId;
    std::string m_MultiFrameWorkspaceConnectionFromNodeId;
    std::string m_MultiFrameWorkspacePresentedProjectId;
    std::string m_MultiFrameWorkspaceStatusText;
    float m_MultiFrameDropMinX = 0.0f;
    float m_MultiFrameDropMinY = 0.0f;
    float m_MultiFrameDropMaxX = 0.0f;
    float m_MultiFrameDropMaxY = 0.0f;
    double m_MultiFrameDropPanX = 0.0;
    double m_MultiFrameDropPanY = 0.0;
    double m_MultiFrameDropZoom = 1.0;
    bool m_MultiFrameWorkspaceExpanded = false;
    bool m_MultiFrameWorkspaceGraphGestureDirty = false;
    bool m_MfdBrowseWorkspace = false;
    std::string m_RawLabPresentedMultiFrameProjectId;
    bool m_RawLabMultiFrameAdvancedOpenedThisFrame = false;
    std::atomic<std::uint64_t> m_MfdExperimentalProcessingGeneration { 0 };
    Async::TaskState m_MfdExperimentalProcessingTaskState = Async::TaskState::Idle;
    Stack::Notifications::ActivityHandle m_MfdProcessingActivity;
    std::string m_MfdExperimentalProcessingStatusText;
    std::string m_MfdExperimentalProcessingProjectId;
    std::string m_MfdExperimentalProcessingSourceSetId;
    std::uint64_t m_MfdExperimentalProcessingInputRevision = 0;
    std::shared_ptr<MfdExperimentalProcessingProgressState>
        m_MfdExperimentalProcessingProgress;
    MfdExperimentalParameterDraft m_MfdExperimentalParameterDraft;
    std::optional<MfdExperimentalProcessingReport> m_MfdExperimentalProcessingReport;
    std::optional<MfdAdoptedRawResult> m_MfdAdoptedRawResult;
    std::atomic<std::uint64_t> m_HdrProcessingGeneration { 0 };
    Async::TaskState m_HdrProcessingTaskState = Async::TaskState::Idle;
    Stack::Notifications::ActivityHandle m_HdrProcessingActivity;
    std::string m_HdrProcessingStatusText;
    std::string m_HdrProcessingProjectId;
    std::string m_HdrProcessingSourceSetId;
    std::uint64_t m_HdrProcessingInputRevision = 0;
    std::shared_ptr<MfdExperimentalProcessingProgressState> m_HdrProcessingProgress;
    std::optional<HdrProcessingReport> m_HdrProcessingReport;
    std::optional<HdrAdoptedRawResult> m_HdrAdoptedRawResult;
    std::shared_ptr<Stack::Editor::BracketingSession> m_Bracketing;
    std::uint64_t m_BracketingPresentedSourceHash=0;
    ImVec2 m_BracketingImageMin{},m_BracketingImageMax{};
    std::atomic<std::uint64_t> m_MultiFrameGraphProcessingGeneration { 0 };
    Stack::Notifications::ActivityHandle m_MultiFrameProcessingActivity;
    Async::TaskState m_MultiFrameGraphProcessingTaskState =
        Async::TaskState::Idle;
    std::string m_MultiFrameGraphProcessingStatusText;
    std::string m_MultiFrameGraphProcessingProjectId;
    std::string m_MultiFrameGraphProcessingSourceSetId;
    std::string m_MultiFrameGraphProcessingIdentity;
    std::shared_ptr<MfdExperimentalProcessingProgressState>
        m_MultiFrameGraphProcessingProgress;
    std::shared_ptr<const Raw::MultiFrame::GraphProcessingCache> m_MultiFrameProcessingCache;
    std::shared_ptr<Stack::Editor::FusionWorkspaceUiState> m_FusionWorkspaceUi;
    bool m_MultiFramePreviewVisible = true;
    std::unordered_map<std::string, std::shared_ptr<const Raw::Hdr::Result>> m_MultiFrameFusionResults;
    std::string m_MultiFrameCacheProjectId;
    int m_HdrDiagnosticView = 0;
    unsigned int m_HdrDiagnosticOverlayTexture = 0;
    int m_HdrDiagnosticOverlayWidth = 0;
    int m_HdrDiagnosticOverlayHeight = 0;
    int m_HdrDiagnosticOverlayView = 0;
    std::string m_HdrDiagnosticOverlayCacheKey;
    int m_RawWorkspacePreviewStageQueuedFrame = -1;
    std::string m_RawWorkspacePreviewSourceKey;
    int m_RawWorkspaceInteractivePreviewMaxDimension = 1280;
    int m_RawWorkspacePhysicalViewportMaxDimension = 1280;
    int m_RawWorkspaceInteractivePreviewMaximumEdge = 384;
    Raw::ViewportCalibration m_RawViewportCalibration;
    Raw::ViewportTimingBank m_RawViewportTimingBank;
    Raw::ViewportTimingHistory m_RawViewportTimingHistory;
    Raw::ViewportController m_RawViewportController;
    mutable Raw::ViewportDecision m_RawViewportDecision;
    Raw::ViewportDecisionInput BuildRawViewportDecisionInput() const;
    int ResolveRawViewportInteractiveEdge();
    int m_RawViewportPhysicalWidth = 1280, m_RawViewportPhysicalHeight = 720;
    std::string m_RawViewportTimingRepresentation;
    Raw::ViewportEditTimingWindow m_RawViewportEditTimingWindow;
    std::array<std::size_t, Raw::kViewportStageCount> m_RawViewportCachedStages {};
    std::array<std::size_t, Raw::kViewportStageCount> m_RawViewportNativeCachedStages {};
    int m_RawViewportCachedEdge = 0;
    int m_RawViewportCachedRequestEdge = 0;
    std::map<std::size_t, double> m_RawViewportAdaptiveHistory;
    std::size_t m_RawViewportAdaptiveWorkload = 0;
    std::uint64_t m_RawViewportTimingViewGeneration = 0;
    Raw::ViewportRequest m_RawViewportRequest;
    Raw::ViewportRegion m_RawViewportPresentedRegion;
    EditorRenderWorker::SharedTextureResult m_RawViewportDetailTexture;
    Raw::ViewportRegion m_RawViewportDetailRegion;
    std::size_t m_RawViewportDetailContent = 0;
    std::uint64_t m_RawViewportPresentedGeneration = 0;
    std::size_t m_RawViewportPresentationContent = 0;
    std::size_t m_RawViewportOverviewContent = 0;
    std::size_t m_RawViewportOverviewFailedContent = 0;
    std::uint64_t m_RawViewportMaintenanceGeneration = 0;
    int m_RawViewportWarmupEdge = 0;
    bool m_RawViewportMaintenanceIsOverview = false;

    std::uint64_t m_RawViewportGestureId = 0;
    Raw::ViewportStage m_RawViewportEditStage = Raw::ViewportStage::RawBase;
    Stack::RawRecipe::RawDevelopmentRecipe m_RawViewportPreviousRecipe;
    Raw::ViewportTimingBank::Keys m_RawViewportGraphWorkloadKeys {};
    bool m_RawViewportHasPreviousRecipe = false;
    mutable std::shared_ptr<Raw::ViewportPreferencesStore> m_RawViewportPreferences;
    std::uint64_t m_RawViewportPreferencesRevision = 0;
    int m_RawViewportEffectiveFps = 0;
    int m_RawViewportDisplayRefreshRate = 60;
    void SyncRawViewportPreferences();
    int m_RawWorkspacePreviewSlowSamples = 0;
    int m_RawWorkspacePreviewScaleCooldown = 0;
    float m_RawWorkspaceAdaptivePreviewScale = 1.0f;
    double m_RawWorkspaceAdaptiveFrameTimeMs = 0.0;
    bool m_RawWorkspaceAdaptiveGestureActive = false;
    std::chrono::steady_clock::time_point
        m_RawWorkspaceAdaptiveLastAcceptedCommandTime {};
    std::chrono::steady_clock::time_point
        m_RawWorkspaceAdaptiveLastAdoptionTime {};
    int m_RawWorkspacePreviewSupersededStreak = 0;
    int m_RawWorkspacePreviewHealthyStreak = 0;
    int m_RawWorkspaceGlMaxTextureSize = 0;
    std::uint64_t m_RawWorkspaceVramWorkingBudgetBytes = 0;
    std::uint64_t m_RawWorkspaceVramAvailableBytes = 0;
    bool m_RawWorkspaceMinimumMemoryTiling = false;
    std::size_t m_LatestRawPresentationRecipeRevision = 0;
    std::size_t m_LatestRawAuxiliaryRecipeRevision = 0;
    std::chrono::steady_clock::time_point
        m_RawWorkspaceGpuBudgetLastRefresh {};
    double m_RawWorkspaceFastPreviewUntilTime = -1.0;
    bool m_RawWorkspaceFullResolutionPreviewPending = false;
    bool m_RawWorkspaceFullResolutionPreviewRequested = false;
    bool m_RawWorkspaceFullResolutionPreviewDeferredByBudget = false;
    std::uint64_t
        m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes = 0;
    std::uint64_t m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
    int m_RawWorkspaceFullResolutionPreviewRetryCount = 0;
    double m_RawWorkspaceAnalysisQuietUntilTime = -1.0;
    bool m_RawWorkspaceAnalysisPending = false;
    bool m_RawWorkspaceAnalysisRequested = false;
    int m_RawWorkspaceAnalysisScopeRetryCount = 0;
    struct RawWorkspaceCachePrewarmState {
        using Stage = Stack::Renderer::RawDevelopmentCache::Stage;

        bool hoverActive = false;
        bool submitted = false;
        bool attempted = false;
        Stage stage = Stage::NeutralPlacement;
        std::string sourceKey;
        std::uint64_t sourceHash = 0;
        std::size_t fingerprint = 0;
        int targetEdge = 0;
        double dwellStartedAt = -1.0;
        ImVec2 pointerAnchor = ImVec2(-10000.0f, -10000.0f);
        std::uint64_t generation = 0;

        bool completed = false;
        Stage completedStage = Stage::NeutralPlacement;
        std::string completedSourceKey;
        std::uint64_t completedSourceHash = 0;
        std::size_t completedFingerprint = 0;
        int completedTargetEdge = 0;
    };
    RawWorkspaceCachePrewarmState m_RawWorkspaceCachePrewarm;
    // Ordinary editing renders at the display's useful resolution. Queue,
    // export, and Gallery inspection opt into the expensive full raster.
    bool m_RawWorkspaceExplicitFullQualityRenderRequested = false;
    std::string m_RawWorkspaceLocalRangeOverlayMode = "none";
    unsigned int m_RawWorkspaceLocalRangeOverlayTexture = 0;
    unsigned int m_RawGradientOverlayProgram = 0;
    unsigned int m_RawGradientOverlayVertexArray = 0;
    unsigned int m_RawGradientOverlayTexture = 0;
    int m_RawGradientOverlayWidth = 0;
    int m_RawGradientOverlayHeight = 0;
    std::size_t m_RawGradientOverlayKey = 0;
    EditorRenderWorker::SharedTextureResult
        m_RawWorkspaceLocalRangeOverlayTextureLease;
    int m_RawWorkspaceLocalRangeOverlayWidth = 0;
    int m_RawWorkspaceLocalRangeOverlayHeight = 0;
    std::string m_RawWorkspaceLocalRangeOverlaySourceKey;
    std::string m_RawWorkspaceLocalRangeOverlayAcceptedMode;
    std::uint64_t m_RawWorkspaceLocalRangeOverlayGeneration = 0;
    std::uint64_t m_RawWorkspaceLocalRangeOverlayTargetPreviewGeneration = 0;
    RenderTextureStats m_RawWorkspaceViewTransformInputStats;
    RenderTextureStats m_RawWorkspaceFinalDisplayStats;
    std::vector<RawDevelopmentStageStatsReadback> m_RawWorkspaceStageStatsReadbacks;
    RawDevelopmentGraphScopeReadback m_RawWorkspaceGraphScopeReadback;
    std::shared_ptr<const RawGradingScopeVisualization>
        m_RawWorkspaceGradingScopeVisualization;
    Stack::Editor::RawLabInternal::RawGradingScopeRenderer m_RawGradingScopeRenderer;
    RawWorkspaceGraphScopeCacheEntry
        m_RawWorkspaceLocalRangeInputGraphScopeCache;
    RawWorkspaceGraphScopeCacheEntry
        m_RawWorkspaceFinishToneInputGraphScopeCache;
    RawWorkspaceGraphScopeCacheEntry
        m_RawWorkspaceColorWarpInputGraphScopeCache;
    Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics m_RawWorkspaceStartPointDiagnostics;
    Stack::EditorModuleTypes::RawWorkspaceStartingPointCandidateRenderQueueState
        m_RawWorkspaceStartPointCandidateRenderQueue;
    std::vector<EditorRenderWorker::RawWorkspaceStartPointCandidateRenderResult>
        m_RawWorkspaceStartPointCandidateRenderResults;
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
    float m_RawWorkspaceLocalRangeTargetStartDeltaEv = 0.0f;
    float m_RawWorkspaceLocalRangeTargetDragOffsetEv = 0.0f;
    float m_RawWorkspaceLocalRangeTargetDeltaEv = 0.0f;
    int m_RawWorkspaceLocalRangeTargetPointIndex = -1;
    std::string m_RawWorkspaceLocalRangeTargetZoneId;
    bool m_RawWorkspaceLocalRangeTargetCreateZone = false;
    bool m_RawWorkspaceLocalRangeTargetTransientZone = false;
    bool m_RawWorkspaceLocalRangeTargetHoverSample = false;
    bool m_RawWorkspaceLocalRangeTargetContextMenuRequested = false;
    Stack::RawLocalRangeTargetInteraction::State
        m_RawWorkspaceLocalRangeTargetInteractionState =
            Stack::RawLocalRangeTargetInteraction::State::Hover;
    bool m_RawWorkspaceLocalRangeTargetCreateIntent = false;
    bool m_RawWorkspaceLocalRangeTargetEditStarted = false;
    std::uint32_t m_RawWorkspaceLocalRangeTargetAuthoredZoneHitBits = 0;
    float m_RawWorkspaceLocalRangeTargetStrongestAuthoredZoneWeight = 0.0f;
    int m_RawWorkspaceLocalRangeTargetHoverZoneIndex = -1;
    std::string m_RawWorkspaceLocalRangeTargetHoverZoneName;
    RawLocalRangeTargetPreviewRequest m_RawWorkspaceLocalRangeTargetPreview;
    std::uint64_t m_RawWorkspaceLocalRangeTargetPreviewGeneration = 0;
    double m_RawWorkspaceLocalRangeTargetLastPointerMotionTime = -1.0;
    bool m_RawWorkspaceLocalRangeTargetPreviewRefined = false;
    bool m_RawWorkspaceLocalRangeTargetPreviewRefinementPending = false;
    double m_RawWorkspaceLocalRangeTargetLastRefinementPollTime = -1.0;
    std::uint64_t m_RawWorkspaceLocalRangeTargetZoneSerial = 0;
    double m_RawWorkspaceLocalRangeTargetLastHoverRequestTime = -1.0;
    ImVec2 m_RawWorkspaceLocalRangeTargetLastHoverMouse = ImVec2(-10000.0f, -10000.0f);
    std::string m_RawWorkspaceLocalRangeTargetPreviousOverlayMode = "none";

    std::uint64_t m_ExportGeneration = 0;
    Async::TaskState m_ExportTaskState = Async::TaskState::Idle;
    std::string m_ExportStatusText;
    bool m_RawWorkspaceExportRenderRequested = false;
    std::uint64_t m_RawWorkspaceExportRenderGeneration = 0;
    std::string m_RawWorkspaceExportPath;
    Stack::NodeMath::PngColorMetadataChunks m_RawWorkspaceExportColorChunks;
    bool m_SuppressExportProjectCheckpoint = false;
    std::uint64_t m_ProjectFileSaveGeneration = 0;
    Async::TaskState m_ProjectFileSaveTaskState = Async::TaskState::Idle;
    std::string m_ProjectFileSaveStatusText;
    bool m_NotificationForeground = true;
    Stack::Notifications::EventId m_ProjectConflictNotice = 0;
    bool m_ProjectConflictReloadPending = false;
    Stack::Notifications::OperationId m_ProjectConflictReloadOperation = 0;
    std::uint64_t m_ProjectConflictReloadGeneration = 0;
    std::string m_ProjectConflictDocument;
    std::uint64_t m_AutoBracketDecisionGeneration = 0;
    const void* m_AutoBracketDecisionOwner = nullptr;
    struct NotificationDecisionOwner {
        Stack::Notifications::OperationId operation = 0;
        std::string document;
        std::weak_ptr<Stack::Project::FileOperationState> files;
        std::uint64_t loadGeneration = 0;
    };
    std::vector<NotificationDecisionOwner> m_NotificationDecisionOwners;
    std::weak_ptr<Stack::Project::FileOperationState> m_NotificationDecisionFiles;
    std::string m_NotificationDecisionDocument;
    std::uint64_t m_NotificationDecisionLoadGeneration = 0;
    void UpdateRawWorkspaceNotificationDecisions();

    CanvasToolKind m_CanvasToolKind = CanvasToolKind::None;
    int m_CanvasToolOwnerNodeId = -1;
    std::string m_CanvasToolStatusText;
    bool m_IsPickingColor = false;
    bool m_CanvasColorPickSamplesNodeInput = false;
    std::function<void(float, float, float)> m_ColorPickerCallback;
    int m_LastToneCurveProbeNodeId = -1;
    bool m_RenderWorkerAvailable = false;
    bool m_NodeBrowserRenderWorkerAvailable = false;
    bool m_ShutdownComplete = false;
    bool m_RenderDirty = true;
    bool m_RenderPending = false;
    EditorRenderWorker::SharedTextureTileSet m_ViewportOutputTiles;
    EditorRenderWorker::SharedTextureResult m_RawWorkspacePresentationTexture;
    EditorRenderWorker::SharedTextureResult m_RawViewportOverviewTexture;
    EditorRenderWorker::SharedTextureResult m_RawViewportPreviousTexture;
    Raw::ViewportFadeRenderer m_RawViewportFadeRenderer;
    Raw::ViewportPresentation m_RawViewportLastPresentation;
    Raw::ViewportPresentation m_RawViewportFadeBase;
    double m_RawViewportPresentationTime = -1.0;
    double m_RawViewportFadeStarted = -1.0;
    double m_RawViewportFadeDuration = 0.0;
    double m_RawViewportCadenceMs = 0.0;
    double m_RawViewportFeedbackFixedMs = 0.0;
    int m_RawViewportFeedbackEdge = 0;
    std::string m_RawViewportFadeDecision;
    RawWorkspacePreviewOutputKind m_RawWorkspacePreviewOutputKind =
        RawWorkspacePreviewOutputKind::None;
    std::string m_ViewportOutputRawWorkspaceSourceKey;
    int m_ViewportOutputPreviewMaxDimension = 0;
    int m_ViewportOutputExpectedNativeWidth = 0;
    int m_ViewportOutputExpectedNativeHeight = 0;
    bool m_ViewportOutputNativeExtentVerified = false;
    std::uint64_t m_ViewportOutputRenderGeneration = 0;
    bool m_MultiFrameProjectCoverRefreshPending = false;
    bool m_ProjectCoverEncodeInFlight = false;
    std::uint64_t m_RenderGeneration = 0;
    std::uint64_t m_GraphAcceptedResultGeneration = 0;
    bool m_GraphRenderBackendFailureReported = false;
    std::uint64_t m_LastCompletedRenderGeneration = 0;
    std::uint64_t m_LatestRawPresentationGeneration = 0;
    std::uint64_t m_LatestRawAuxiliaryGeneration = 0;
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
    bool m_WorkspaceDetachedWindowsVisible = true;
    bool m_RestoreWorkspaceDetachedPreview = false;
    bool m_RestoreWorkspaceNativeGallery = false;
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
    TimelineUiState m_TimelineUi;
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
    std::uint64_t m_NodeBrowserThumbnailWarmGeneration = 0;
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
    bool TryBuildRenderSnapshot(
        std::uint64_t generation,
        EditorRenderWorker::Snapshot& snapshot) noexcept;
    std::vector<EditorRenderWorker::CompositeOutputRequest> BuildCompositeOutputRequests();
    bool PublishCompositeOutputPixels(
        int outputNodeId,
        std::vector<unsigned char> pixels,
        int width,
        int height,
        std::uint64_t renderRevision,
        std::size_t chainFingerprint, bool pixelsPrepared = false);
    void ResetCompositeOutputRequestForRetry(int outputNodeId);
    void ResetIncompleteCompositeOutputRequestsForRetry();
    bool PublishPreviewResultPixels(
        EditorRenderWorker::PreviewResult& previewResult);
    void ResetPreviewRequestForRetry(
        int previewNodeId,
        std::uint64_t requestGeneration);
    void ResetIncompletePreviewRequestsForRetry();
    std::vector<EditorRenderWorker::PreviewRequest> BuildPreviewRequests();
    std::vector<EditorRenderWorker::DevelopCandidateRenderRequest> BuildDevelopCandidateRenderRequests(
        const RenderGraphSnapshot& graph,
        int sourceWidth,
        int sourceHeight);
    void ConsumeRenderWorkerResults();
    bool IsAnyRenderBackendBusy() const;
    EditorRenderWorker::RenderProgress GetActiveRenderBackendProgress() const;
    bool SubmitRenderSnapshot(EditorRenderWorker::Snapshot snapshot);
    bool TrySubmitRawRenderCommand(
        std::uint64_t generation,
        std::vector<EditorRenderWorker::PreviewRequest>& previews);
    void ClearRawRenderSession();
    void InvalidateRenderSnapshotsBefore(std::uint64_t generation);
    bool ExecuteRenderOwnerOpenGlTaskBlocking(
        EditorRenderWorker::OpenGlTask task,
        std::string& error);
    void ClearViewportOutputTiles();
    void RefreshPendingMultiFrameProjectCover();
    void QueueViewportOutputTextureRelease(EditorRenderWorker::SharedTextureResult& texture);
    void QueueViewportOutputTileSetRelease(EditorRenderWorker::SharedTextureTileSet& tileSet);
    void PumpViewportOutputTextureDeletes(bool drainAll = false);
    void PumpViewportOutputTileTextureDeletes(bool drainAll = false);
    void ApplyToneCurveAutoRewriteFeedback(
        const std::vector<ToneCurveAutoRewriteFeedback>& feedbacks) noexcept;
    void ApplyDevelopCandidateRenderFeedback(
        const std::vector<EditorRenderWorker::DevelopCandidateRenderResult>& results);
    Stack::GraphRendering::RequestTag CurrentGraphRenderTag() const;
    bool IsCurrentGraphResult(const EditorRenderWorker::Result& result) const;
    void ReportGraphRenderFailure(const EditorRenderWorker::Result& result);
    bool GraphRenderBackendReady();
    void SubmitRenderIfReady() noexcept;
    void SubmitRenderIfReadyImpl();
    void ConsumeNodeBrowserThumbnailWorkerResults();
    void ResetNodeBrowserThumbnailState();
    void MarkNodeBrowserThumbnailSourceChanged();
    NodeBrowserPreviewSeed ResolveNodeBrowserPreviewSeed() const;
    void StartNodeBrowserThumbnailGeneration(bool forceRefresh);
    void StartNodeBrowserThumbnailGenerationImpl(bool forceRefresh);
    void FinalizeNodeBrowserThumbnailBatch(std::uint64_t generation);
    void FailNodeBrowserThumbnailEntries(
        std::uint64_t generation,
        const std::vector<std::string>& previewKeys);
    void WarmNodeBrowserThumbnailPixelsAsync();
    bool RefreshCompletedChainCacheIfNeeded() const noexcept;
    bool RefreshCompositeMetadataCacheIfNeeded() noexcept;
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
    Stack::Timeline::TimelineAnimationState& GetGraphAnimation();
    const Stack::Timeline::TimelineAnimationState& GetGraphAnimation() const;
    std::string GetEditedGraphId() const;
    std::vector<CachedCompositeChainState> BuildTimelineGraphChains() const;
    nlohmann::json SerializeTimelinePersistence() const;
    void DeserializeTimelinePersistence(const nlohmann::json& pipelineData);
    void SyncCompositeSceneItems(const ImVec2& canvasSize);
    void HandleViewportModeTransition(ViewportMode previousMode, ViewportMode currentMode);
    void EnterSingleOutputPreviewMode();
    void ClearCompositeTransientInteractionState();
    void TogglePartialSplitTargets(float workspaceWidth, float minLeftWidth, float maxLeftWidth, bool compositeViewportMode);
    void HandleSpacebarPress(float workspaceWidth, float paneHeight, float minLeftWidth, float maxLeftWidth, float splitGap);
    void HandleSpacebarLongPress(float workspaceWidth, float paneHeight, float minLeftWidth, float maxLeftWidth, float splitGap);
    float UpdateTimelinePanelHeight(float workspaceHeight);
    void RenderTimelinePanel(const ImVec2& workspacePos, const ImVec2& workspaceSize, float timelineHeight);
    void SetTimelineFrame(int frame);
    void StepTimelineFrame(int frameDelta);
    void ToggleTimelinePlayback();
    void StopTimelinePlayback(bool resetToStart);
    int ResolveTimelinePlaybackEndFrame() const;
    void MarkTimelineFrameRenderDirty();
    void ClearTimelineLiveEditPreview();
    void AddTimelineLiveEditPreviewTarget(const Stack::Timeline::AnimatableParameterTarget& target);
    bool UpdateTimelineExistingKeyframesForLayerEdit(
        const EditorNodeGraph::Node& node,
        const nlohmann::json& before,
        const nlohmann::json& after);
    std::vector<Stack::Timeline::AnimatableParameterDefinition> BuildTimelineAnimatableParametersForChain(
        const EditorNodeGraph::CompletedChainInfo& chain) const;
    bool EnsureTimelineSelectedParameter(
        const std::vector<Stack::Timeline::AnimatableParameterDefinition>& parameters);
    bool AddTimelineKeyframeForSelectedParameter();
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
    bool StartAsyncGraphImageNodeImport(const std::string& path, EditorNodeGraph::Vec2 graphPosition);
    bool HasPendingGraphImageImports() const;
    bool AddGraphRawChainFromFile(const std::string& path, EditorNodeGraph::Vec2 sourcePosition);
    bool AddGraphRawChainFromMetadata(
        const std::string& path,
        Raw::RawMetadata metadata,
        EditorNodeGraph::Vec2 sourcePosition);
    bool StartGraphImageChainImport(std::vector<std::string> paths, EditorNodeGraph::Vec2 sourcePosition);
    bool RequestGraphImageChainImports(const std::vector<std::string>& paths, EditorNodeGraph::Vec2 sourcePosition);
    bool AddGraphImageChainFromFile(const std::string& path, EditorNodeGraph::Vec2 sourcePosition);
    bool AddGraphImageChainFromPayload(EditorNodeGraph::ImagePayload payload, EditorNodeGraph::Vec2 sourcePosition);
    std::filesystem::path GetRawWorkspaceAppStatePath() const;
    Stack::RawWorkspace::AppState BuildRawWorkspaceAppStateSnapshot() const;
    RawWorkspaceScanSnapshot GetRawWorkspaceScanSnapshot() const;
    RawWorkspaceThumbnailSnapshot GetRawWorkspaceThumbnailSnapshot() const;
    void LoadRawWorkspaceAppState();
    void SaveRawWorkspaceAppState();
    void SaveRawWorkspaceGalleryGroupingState();
    void RequestOpenRawWorkspace(const std::filesystem::path& workspaceRoot);
    void RequestRawWorkspaceScan();
    void RequestRawWorkspaceScanImpl();
    void RequestRawWorkspaceThumbnailGeneration();
    void RequestRawWorkspaceThumbnailGenerationImpl();
    void PrioritizeRawWorkspaceThumbnailSource(const std::string& sourceKey);
    void PrioritizeRawWorkspaceThumbnailSources(
        const std::vector<std::string>& sourceKeys);
    void QueueRawWorkspaceThumbnailRepair(const std::string& sourceKey);
    void HandleRawWorkspaceThumbnailDecodeFailure(
        const std::string& sourceKey,
        const std::filesystem::path& thumbnailPath);
    void FailRawWorkspaceThumbnailGeneration(
        std::uint64_t generation,
        const std::vector<std::pair<std::size_t, std::string>>& pendingSources,
        const char* message);
    void ClearRawWorkspace();
    void SelectRawWorkspaceSource(const std::string& sourceKey);
    void InvalidateRawWorkspaceGalleryPresentation();
    void RequestRawWorkspaceSimilarityRebuild();
    void ResetRawWorkspaceSimilarityStacks();
    const std::vector<Stack::RawWorkspace::RawGallerySimilarityStack>&
        ResolveRawWorkspaceFilmstripStacks() const;
    const std::vector<Stack::RawWorkspace::RawGallerySimilarityStack>&
        ResolveRawWorkspaceVisibleGalleryStacks();
    void OpenRawWorkspaceGalleryStackInFilmstrip(const std::string& sourceKey);
    bool MergeRawWorkspaceFilmstripStack(
        const std::string& sourceKey,
        const std::vector<std::string>& targetMembers);
    bool MergeRawWorkspaceFilmstripStackMembers(
        const std::vector<std::string>& sourceKeys,
        const std::vector<std::string>& targetMembers);
    bool DetachRawWorkspaceFilmstripStackMember(
        const std::string& sourceKey);
    bool ReorderRawWorkspaceFilmstripSources(
        const std::vector<std::string>& sourceKeys,
        const std::vector<Stack::RawWorkspace::RawGallerySimilarityStack>&
            visibleStacks,
        std::size_t insertionStackIndex,
        bool detachSingleMember);
    void SetRawWorkspaceFilmstripSortMode(
        Stack::RawWorkspace::RawGalleryFilmstripSortMode mode);
    void NormalizeRawWorkspaceFilmstripOrganization();
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
    void NoteRawWorkspaceProjectOpenPreview();
    std::string GetActiveRawWorkspacePreviewIdentity() const;
    std::uint64_t GetActiveRawWorkspacePreviewSourceHash() const;
    bool IsRawWorkspaceFastPreviewRenderActive(double now) const;
    void RefreshRawWorkspaceGpuMemoryBudget();
    void UpdateRawWorkspaceInteractivePreviewDimension(
        const ImVec2& imageBounds);
    void UpdateRawWorkspaceSettledPreviewRender(double now);
    bool HasRawWorkspaceLivePreviewForSource(const std::string& sourceKey) const;
    bool HasRawWorkspaceFullResolutionPreviewForSource(const std::string& sourceKey) const;
    bool HasRawWorkspaceCurrentPresentationForSource(const std::string& sourceKey) const;
    void ClearRawWorkspaceLivePreviewState();
    void CacheRawWorkspaceGraphScopeReadback(
        const std::string& sourceKey,
        std::size_t inputFingerprint,
        const RawDevelopmentGraphScopeReadback& readback);
    bool RestoreRawWorkspaceGraphScopeReadbackForActiveLabTool();
    void ClearRawWorkspaceGraphScopeReadbackCaches();
    bool IsViewportTextureSafeForDrawing(unsigned int texture) const;
    void ClearRawWorkspacePresentationTexture();
    void ClearRawViewportTransition();
    void UpdateRawViewportCoverage(ImVec2 imageMin, ImVec2 imageMax,
        ImVec2 viewMin, ImVec2 viewMax, bool currentPreview);
    void PrepareRawViewportTransition(const EditorRenderWorker::Result& result);
    bool AdoptRawViewportFrame(EditorRenderWorker::Result& result);
    void ObserveRawViewportFeedback(const EditorRenderWorker::Result& result);
    bool DrawRawViewportTransition(ImDrawList* list, ImVec2 minimum, ImVec2 maximum);
    bool AdoptRawWorkspacePresentationTexture(
        EditorRenderWorker::SharedTextureResult& texture, const Raw::ViewportRegion& region = {});
    void ClearRawWorkspaceLocalRangeOverlayState();
    void AdoptRawWorkspaceLocalRangeOverlayFromResult(
        EditorRenderWorker::Result& result) noexcept;
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
    void PreserveActiveRawProjectSourceForLibraryNavigation();
    bool ApplyLoadedRawProjectSessionMetadata(
        const LoadedProjectData& projectData,
        std::string* outError = nullptr);
    bool ValidateAndRepairActiveRawProjectGraphBindings(
        bool* requiresRepairedCopy = nullptr,
        std::string* outError = nullptr);
    bool CommitActiveMultiFrameMutation(
        Stack::Project::RawProjectSnapshot snapshot,
        EditorNodeGraph::Graph graph,
        const Stack::Project::ProjectStoreTransaction& transaction,
        bool importInProgress,
        std::string* outError = nullptr,
        bool noteEdit = true);
    bool EnqueueCurrentProjectSave(
        Stack::Project::ProjectSaveReason reason,
        const std::string& fallbackName,
        std::function<void(bool)> onComplete = {});
    void StartManagedProjectSaveAsync(
        std::uint64_t capturedEditorRevision,
        Stack::Project::ProjectSaveReason reason,
        Stack::Project::ProjectSaveCoordinator::Completion completion);
    void CheckCurrentProjectSaveAsync(
        Stack::Project::ProjectSaveCoordinator::Completion completion,
        std::function<void(Stack::Project::ProjectSaveCoordinator::Completion, bool)> write);
    void RefreshUnifiedProjectViewsAfterSave(
        bool projectMembershipChanged = false,
        bool persistCatalog = true);
    void RequestCreateProjectVersion(
        const std::filesystem::path& sourceProject);
    void RenderMultiFrameRawLabTool();
    void RenderMultiFrameFusionPreview(const ImVec2& size);
    void RenderMultiFrameFusionInspector(
        const Stack::Project::MultiFrameGraphNode& node, bool busy,
        std::function<void()>& deferredAction);
    void RenderMultiFrameBurstInspector(
        const Stack::Project::MultiFrameGraphNode& node, bool busy,
        std::function<void()>& deferredAction);
    void RenderHdrRawLabTool();
    void RenderMultiFrameRawLabAdvanced();
    void RenderMultiFrameRawLabCreationPopup();
    void RenderMultiFrameSourceSetDeletePopup();
    void RenderMultiFrameFrameDeletePopup();
    bool SetMfdExperimentalParameters(
        const std::string& sourceSetId,
        double motionDisagreementHardLimitRawPixels,
        double trustedPixelZeroWeightSigma,
        double oneAlternateWeightCapRelativeToReference,
        double exactFallbackAlternateToReferenceRatio,
        int fusionMethod,
        double fusionSmoothing,
        std::string* outError = nullptr);
    bool SetMfdExperimentalMemoryBudgetGiB(
        const std::string& sourceSetId,
        double memoryBudgetGiB,
        std::string* outError = nullptr);
    bool SetMfdExperimentalAlignmentMode(
        const std::string& sourceSetId,
        Raw::Mfd::MfdAlignmentMode alignmentMode,
        std::string* outError = nullptr);
    bool SetMfdSharedBurstExposureTolerance(
        const std::string& sourceSetId,
        double toleranceEv,
        std::string* outError = nullptr);
    bool SetMfdSharedBurstFrameTrust(
        const std::string& sourceSetId,
        const std::string& frameId,
        double trustAttenuation,
        std::string* outError = nullptr);
    bool StartMfdExperimentalProcessing(
        const std::string& sourceSetId,
        std::string* outError = nullptr);
    void CancelMfdExperimentalProcessing(
        const std::string& reason = {},
        bool clearPublishedReport = false);
    bool IsMfdExperimentalProcessingBusy() const {
        return Async::IsBusy(m_MfdExperimentalProcessingTaskState);
    }
    bool StartHdrProcessing(
        const std::string& sourceSetId,
        std::string* outError = nullptr);
    bool PublishHdrResultToRawWorkspace(
        HdrAdoptedRawResult adopted,
        const std::filesystem::path& cacheDirectory = {},
        std::string* outError = nullptr);
    bool StartMultiFrameGraphProcessing(
        const std::string& sourceSetId,
        std::string* outError = nullptr);
    bool SetHdrProcessingConfiguration(
        const std::string& sourceSetId,
        Raw::Hdr::AlignmentMode alignmentMode,
        bool automaticGeometricReference,
        const std::string& geometricReferenceFrameId,
        bool automaticRadiometricAnchor,
        const std::string& radiometricAnchorFrameId,
        std::string* outError = nullptr);
    void RestoreHdrResultCacheAfterProjectLoad();
    void CancelHdrProcessing(const std::string& reason = {});
    void CancelMultiFrameGraphProcessing(const std::string& reason = {});
    bool IsHdrProcessingBusy() const {
        return Async::IsBusy(m_HdrProcessingTaskState);
    }
    bool IsMultiFrameGraphProcessingBusy() const {
        return Async::IsBusy(m_MultiFrameGraphProcessingTaskState);
    }
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
    bool StageRawWorkspaceProjectForSourcePreview(
        const Stack::RawWorkspace::SourceRecord& source,
        bool createNewProject = false);
    bool ApplyActiveRawWorkspaceModeDataToDocument(StackBinaryFormat::ProjectDocument& document) const;
    bool ResolveRawWorkspaceRecipeForSource(
        const Stack::RawWorkspace::SourceRecord& source,
        Stack::RawRecipe::RawDevelopmentRecipe& outRecipe,
        Stack::RawWorkspace::RawProjectMode* outMode = nullptr,
        std::string* outError = nullptr) const;
    bool FocusRawWorkspaceDevelopmentNode();
    bool OpenRawWorkspaceProjectInGraph(const Stack::RawWorkspace::SourceRecord& source);
    bool SaveActiveRawWorkspaceProjectIfDirty();
    bool RequestOpenRawWorkspaceSourceForEditing(
        const std::string& sourceKey);
    void QueueRawWorkspaceProjectReplacement(
        std::string actionLabel,
        std::string targetLabel,
        std::function<bool(std::string*)> action,
        std::string discardOpenSourceKey = {});
    bool ExecutePendingRawWorkspaceProjectReplacement(bool discardCurrent);
    void ClearPendingRawWorkspaceProjectReplacement();
    void RenderRawWorkspaceLifecyclePopups();
    void QueueRawWorkspaceThumbnailTextureDelete(unsigned int texture);
    void PumpRawWorkspaceThumbnailTextureDeletes(bool drainAll = false);
    void TrimRawWorkspaceThumbnailTextureCache();
    void ClearRawWorkspaceThumbnailTextures(bool immediate = false);
    void RenderRawWorkspaceEmptyState(const RawWorkspaceScanSnapshot& scanSnapshot);
    void RenderRawWorkspaceBrowser(
        const RawWorkspaceScanSnapshot& scanSnapshot,
        const RawWorkspaceThumbnailSnapshot& thumbnailSnapshot);
    void RenderRawWorkspaceControlsPanel(
        const Stack::RawWorkspace::SourceRecord* selectedSource,
        const Stack::RawWorkspace::RawPanelState& panelState);
    void RenderRawWorkspaceToneGraphsPanel(
        const Stack::RawWorkspace::SourceRecord* selectedSource,
        const Stack::RawWorkspace::RawPanelState& panelState);
    bool RenderRawWorkspaceAutoBasePanel(
        const Stack::RawWorkspace::SourceRecord* selectedSource,
        Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe,
        float controlWidth);
    bool RenderRawWorkspaceLocalRangeControls(
        const Stack::RawWorkspace::SourceRecord* selectedSource,
        Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe,
        float controlWidth,
        bool defaultOpen);
    void RenderRawWorkspaceAnalysisPanel(float controlWidth);
    void RenderRawWorkspacePreviewPanel(
        const Stack::RawWorkspace::SourceRecord* selectedSource,
        const Stack::RawWorkspace::RawPanelState& panelState);
    struct RawWorkspaceEditContext {
        std::string rawAdjustmentLayerId;
        std::string rawOperationUuid;
        const Stack::RawWorkspace::SourceRecord* source = nullptr;
        Stack::RawWorkspace::RawPanelState panelState;
        Stack::RawRecipe::RawDevelopmentRecipe recipe;
        // The authored document recipe remains immutable while this context
        // owns its one editable value copy.
        const Stack::RawRecipe::RawDevelopmentRecipe* persistedRecipe = nullptr;
        Stack::RawWorkspace::RawProjectMode resolvedMode =
            Stack::RawWorkspace::RawProjectMode::Unknown;
        std::string multiFrameSourceSetId;
        std::string error;
        bool multiFrameResult = false;
        bool canEdit = false;
    };
    std::optional<Stack::RawRecipe::GraphOperationKind> SelectedRawOperationKind() const;
    const EditorNodeGraph::Node* SelectedRawOperation() const;
    Stack::RawRecipe::RawDevelopmentRecipe ReadRawControlRecipe() const;
    bool RawStartingPointGraphSupported(std::string& reason) const;
    bool PrepareRawControlRecipeEdit(const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
        Stack::Project::RawLayerStackState& candidate, bool& changed, std::string& error) const;
    bool IsRawParameterDriven(const std::string& parameter) const;
    void BindRawOperationContext(RawWorkspaceEditContext& context) const;
    void RenderRawOperationInstancePicker(RawWorkspaceEditContext& context);
    struct RawWorkspacePreviewContext {
        RawWorkspaceEditContext edit;
        std::string identity;
        bool projectActive = false;
        bool loading = false;
    };
    RawWorkspacePreviewContext ResolveRawWorkspacePreviewContext(
        const Stack::RawWorkspace::SourceRecord* source) const;
    void DrawMultiFrameRawDiagnosticOverlay(
        ImDrawList* drawList, const ImVec2& minimum, const ImVec2& maximum);
    void UpdateRawWorkspaceCachePrewarmHover(
        const RawWorkspaceEditContext& context,
        RawLabTool tool,
        bool hovered);
    bool TrySubmitRawWorkspaceCachePrewarm(double now);
    bool TrySubmitRawViewportCalibration(double now);
    bool TrySubmitRawViewportMaintenance(double now);
    void AdoptRawViewportMaintenance(EditorRenderWorker::Result& result);
    void CancelRawViewportCalibration();
    void AdoptRawViewportCalibration(const EditorRenderWorker::Result& result);
    void ObserveRawViewportTiming(const EditorRenderWorker::Result& result);
    void RefreshRawViewportTimingHistory();
    void RetainRawViewportDetail(const EditorRenderWorker::Result& result);
    void DrawRawViewportDetail(ImDrawList* list, ImVec2 minimum, ImVec2 maximum);
    int GetCalibratedRawViewportEdge() const;
    std::uint64_t RawViewportSourceHash() const;
    Stack::RawRecipe::RawDevelopmentRecipe RawViewportRecipe() const;
    void UpdateRawViewportEditTiming(const Stack::RawRecipe::RawDevelopmentRecipe& recipe);
    void CancelRawWorkspaceCachePrewarm(bool clearCompleted = false);
    void AdoptRawWorkspaceCachePrewarmResult(
        const EditorRenderWorker::Result& result);
    bool BeginRawWorkspaceEditContext(
        const Stack::RawWorkspace::SourceRecord* selectedSource,
        RawWorkspaceEditContext& context) const;
    void CommitRawWorkspaceEditContext(
        RawWorkspaceEditContext& context,
        bool changed,
        bool interactionActive);
    bool RenderRawWorkspaceLabCalibrationSurface(RawWorkspaceEditContext& context);
    bool ApplyMultiFramePostRecipeEdit(
        const std::string& sourceSetId,
        const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
        bool interactionActive, bool cancelGesture = false);
    void RenderRawWorkspacePreviewCanvas(
        const Stack::RawWorkspace::SourceRecord* selectedSource,
        bool drawImageFrame,
        ImVec2* outImageMinimum = nullptr,
        ImVec2* outImageMaximum = nullptr);
    int GetRawWorkspaceColorWarpDisplayedPin(
        const Stack::RawRecipe::RawDevelopmentRecipe& recipe) const;
    int GetRawWorkspaceColorWarpViewMode(
        const Stack::RawRecipe::RawDevelopmentRecipe& recipe) const;
    void ApplyRawWorkspaceColorWarpViewOverride(
        Stack::RawRecipe::RawDevelopmentRecipe& recipe) const;
    void ApplyRawWorkspaceDenoiseViewOverride(
        Stack::RawRecipe::RawDevelopmentRecipe& recipe) const;
    bool RenderRawWorkspaceLabCommandStrip();
    void RenderRawWorkspaceLabPreview(
        const Stack::RawWorkspace::SourceRecord* selectedSource);
    void RenderRawWorkspaceLabFilmstripPreviewOverlay();
    void RenderRawWorkspaceLabGalleryWorkspace(const ImVec2& size);
    bool RenderRawWorkspaceLabGalleryNavigation(const ImVec2& size);
    void RenderRawWorkspaceLabFilmstripTimeline(
        float scrollX, float scrollMaxX, float viewportWidth);
    void PollRawWorkspaceLabGradingScope();
    void RenderRawWorkspaceLabGradingSurface();
    void ClearRawWorkspaceLabGradingScope();
    bool RenderRawWorkspaceActiveControls(RawWorkspaceEditContext& context);
    void RenderRawFloatingControls(RawWorkspaceEditContext& context,
        const ImVec2& minimum, const ImVec2& size, bool enabled);
    void RenderRawFloatingSurfaceHandle(const char* label);
    bool RawFloatingSurfaceOwnsPointer() const;
    Stack::Editor::RawLabInternal::FloatingSurfaceState m_RawFloatingSurface;
    void RenderRawLayerPanelContents();
    bool RenderRawLayerThumbnail(const std::string& layerId, const std::string& maskId,
        const ImVec2& size, bool selected);
    void AppendRawLayerThumbnailRequests(std::vector<EditorRenderWorker::PreviewRequest>& requests);
    void AdoptRawLayerThumbnail(const EditorRenderWorker::PreviewResult& result);
    Stack::Editor::RawLayerPanelState m_RawLayerPanel;
    void RenderRawLayerMaskAttachment(const char* label, bool wholeLayer = false);
    bool ApplyRawLayerStackEdit(Stack::Project::RawLayerStackState candidate);
    bool CreateRawLayerMask(const std::string& layerId, const std::string& operationUuid,
        EditorNodeGraph::MaskGeneratorKind kind, std::string& error);
    void SelectRawAdjustmentLayer(const std::string& id);
    bool m_GraphEditorUsesRawLayer = false;
    std::string m_SelectedRawAdjustmentLayer;
    std::string m_RawLayerStatus;
    std::optional<Stack::Editor::RawLayerMaskWorkspace> m_RawLayerMaskWorkspace;
    bool CommitRawLayerMaskGraph();
    void RenderRawLayerMaskHandles(const RawWorkspaceEditContext& context, const ImVec2& minimum, const ImVec2& maximum);
    std::optional<Stack::Project::RawMaskReference> m_EditingRawLayerMask;
    int m_RawLayerMaskGenerator = -1;
    int m_RawLayerMaskDrag = -1;
    ImVec2 m_RawLayerMaskDragStart;
    EditorNodeGraph::MaskGeneratorSettings m_RawLayerMaskDragOriginal;
    void ApplyRequestedRawLabTool(RawWorkspaceEditContext& context);
    std::optional<RawLabTool> m_RequestedRawLabTool;
    int m_RawSectionPanelFrame = -2;
    int m_RawActiveControlFrame = -2;
    Stack::Renderer::RawImageBackdropFrame m_RawImageBackdrop;
    void PublishRawImageBackdrop(ImVec2 imageMin, ImVec2 imageMax,
        ImVec2 viewMin, ImVec2 viewMax, bool rawStagePreview);
    ImVec2 m_RawActiveControlMinimum{}, m_RawActiveControlMaximum{};
    bool m_RawToolPickerRequested = false;
    std::optional<std::string> m_RawGalleryFolderFilter;
    std::string m_RawGalleryFolderWorkspaceKey;
    Stack::RawWorkspace::GalleryPresentation m_RawGalleryFilteredPresentation;
    std::uint64_t m_RawGalleryFilteredRevision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t m_RawGalleryFilteredQueueRevision = std::numeric_limits<std::uint64_t>::max();
    Stack::RawWorkspace::GalleryContentMode m_RawGalleryFilteredMode = Stack::RawWorkspace::GalleryContentMode::Gallery;
    void RenderRawGalleryFolderPanel();
    const Stack::RawWorkspace::GalleryPresentation& GetRawWorkspacePanelGalleryPresentation();
    bool RenderRawWorkspaceLabSecondaryControls(RawWorkspaceEditContext& context);
    void RenderRawWorkspaceToolSettings();
    bool ResetRawWorkspaceActiveTool(RawWorkspaceEditContext& context);
    bool m_RawSettingsPanelRequested = false;
    RawLabTool m_RawLabLastEditTool = RawLabTool::Light;
    bool RenderRawWorkspaceLabDenoiseSurface(RawWorkspaceEditContext& context);
    bool RenderRawWorkspaceLabRgbDenoiseSurface(RawWorkspaceEditContext& context);
    bool RenderRawWorkspaceLabLightSurface(RawWorkspaceEditContext& context);
    bool RenderRawWorkspaceLabZonesSurface(RawWorkspaceEditContext& context,
        Stack::Editor::RawLabInternal::RawLabControlSection section = Stack::Editor::RawLabInternal::RawLabControlSection::All);
    bool RenderRawWorkspaceLabLegacyZones(RawWorkspaceEditContext& context);
    bool RenderRawWorkspaceLabAreas(RawWorkspaceEditContext& context,
        Stack::Editor::RawLabInternal::RawLabControlSection section = Stack::Editor::RawLabInternal::RawLabControlSection::All);
    void RenderRawWorkspaceLabImageAreas(const Stack::RawWorkspace::SourceRecord* source,
        const ImVec2& minimum, const ImVec2& maximum);
    void RenderRawWorkspaceLabGradientDots(
        const Stack::RawWorkspace::SourceRecord* source,
        const ImVec2& minimum, float width);
    void RenderRawWorkspaceLabGradientImage(
        const Stack::RawWorkspace::SourceRecord* source,
        const ImVec2& minimum, const ImVec2& maximum);
    bool DrawRawWorkspaceLabGradientOverlay(
        const ImVec2& minimum, const ImVec2& maximum,
        const Stack::RawRecipe::RawGradientMask& mask,
        const std::array<double,9>& canvasToLocal,
        float sourceAspect);
    bool RenderRawWorkspaceLabToneSurface(RawWorkspaceEditContext& context,
        Stack::Editor::RawLabInternal::RawLabControlSection section = Stack::Editor::RawLabInternal::RawLabControlSection::All);
    bool RenderRawWorkspaceLabColorSurface(RawWorkspaceEditContext& context,
        Stack::Editor::RawLabInternal::RawLabControlSection section = Stack::Editor::RawLabInternal::RawLabControlSection::All);
    bool RenderRawWorkspaceLabViewSurface(RawWorkspaceEditContext& context);
    bool RenderRawWorkspaceLabDenoiseSecondaryControls(
        RawWorkspaceEditContext& context);
    bool RenderRawWorkspaceLabRgbDenoiseSecondaryControls(
        RawWorkspaceEditContext& context);
    bool RenderRawWorkspaceLabLightSecondaryControls(
        RawWorkspaceEditContext& context);
    bool RenderRawWorkspaceLabZonesSecondaryControls(
        RawWorkspaceEditContext& context);
    bool RenderRawWorkspaceLabColorSecondaryControls(
        RawWorkspaceEditContext& context);
    bool RenderRawWorkspaceLabViewSecondaryControls(
        RawWorkspaceEditContext& context);
    bool RenderRawWorkspaceLabGalleryHeader(bool nativeWindow, bool sidebar = false);
    bool RenderRawWorkspaceGalleryActionBar(bool nativeWindow = false);
    Stack::RawGalleryActions::Context CaptureRawGalleryActionContext();
    bool ExecuteRawGalleryAction(Stack::RawGalleryActions::Action action,
        const Stack::RawGalleryActions::Context& context);
    void RenderRawGalleryContextActions(const std::filesystem::path& focusedPath);
    void HandleRawGalleryActionShortcuts();
    void SetRawGalleryGridView(bool grid);
    void RefreshRawGalleryAfterFileMove(const std::filesystem::path& root);
    void RequestPasteRawEditAttributesForGalleryTargets(
        std::vector<Stack::RawWorkspace::SourceRecord> sources,
        std::vector<Stack::RawWorkspace::SourceSetProjectCatalogEntry> projects);
    void RenderRawWorkspaceLabGalleryContent(
        bool compactFilmstrip,
        bool expandedFilmstripDrawer = false,
        float filmstripDrawerExpansion = 1.0f);
    bool RecordRawWorkspaceGalleryThumbnail(const std::string& key,
        const ImVec2& minimum, const ImVec2& maximum, float opacity = 1.0f);
    void RecordRawWorkspaceGallerySlot(const Stack::RawWorkspace::SourceRecord& source,
        const ImVec2& minimum, const ImVec2& size, const ImRect& clip);
    void RenderRawWorkspaceGalleryLayoutTransition();
    void RenderRawWorkspaceGalleryRevertPopup();
    void RenderRawWorkspaceLabGalleryInfoPanel();
    void RenderRawWorkspaceLabGalleryImagePreviewPanel();
    void CancelRawWorkspaceLabGalleryImagePreview();
    bool CopyRawEditAttributesFromProjectPath(
        const std::filesystem::path& projectPath,
        const std::string& sourceSetId = {},
        std::string* errorMessage = nullptr);
    void RequestPasteRawEditAttributesForProject(
        const std::filesystem::path& projectPath,
        const std::string& sourceSetId,
        std::string displayName);
    bool StartRawEditAttributePaste(std::string* errorMessage = nullptr);
    void RefreshRawEditAttributeClipboardFromSystem();
    void RenderRawWorkspaceLabNativeGalleryWindow();
    void OpenRawWorkspaceLabNativeGallery();
    void CloseRawWorkspaceLabNativeGallery();
    bool QueryRawWorkspaceLabNativeGalleryWindow(DetachedNativeWindowRequest& request) const;
    void CompleteRawWorkspaceLabNativeGalleryWindowRequest(
        const DetachedNativeWindowRequest& request,
        bool themeApplied,
        bool focused);
    void MarkRawWorkspaceLabNativeGalleryWindowShown(
        const DetachedNativeWindowRequest& request,
        bool focused);
    void MarkRawWorkspaceLabNativeGalleryPlatformPresented(GLFWwindow* window);
    void TryContinueRawWorkspaceStartingPointOnAnalysis();
    void RefreshRawWorkspaceAutoBaseRecommendations(
        const Stack::RawRecipe::RawDevelopmentRecipe& recipe);
    bool ApplyRawWorkspaceAutoBaseViewFitForSource(
        const Stack::RawWorkspace::SourceRecord& source,
        Stack::RawRecipe::RawDevelopmentRecipe& recipe,
        bool explicitApply);
    bool ApplyRawWorkspaceBuildStartingPointForSource(
        const Stack::RawWorkspace::SourceRecord& source,
        Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe);
    bool BeginRawWorkspacePreciseStartingPointForSource(
        const Stack::RawWorkspace::SourceRecord& source,
        const Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe);
    void CancelRawWorkspacePreciseStartingPoint(std::string reason);
    void HandleRawWorkspacePreciseSolveResult(
        const Stack::PreciseIntegration::NativeSolveResult& result);
    void AdoptRawWorkspacePreciseAppliedRender();
    bool ApplyRawWorkspacePreciseCandidateAtomically(
        const Stack::RawWorkspace::SourceRecord& source,
        const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
        const std::string& expectedBaseRecipeIdentity,
        std::string& reason);
    bool ApplyRawWorkspaceStartingPointBalancedLocalForSource(
        const Stack::RawWorkspace::SourceRecord& source,
        Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe);
    bool ApplyRawWorkspaceStartingPointMildToneForSource(
        const Stack::RawWorkspace::SourceRecord& source,
        Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe);
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
    void CancelRawWorkspacePendingStartingPoint(std::string reason);
    void CaptureRawWorkspaceAutoBaseRevertSnapshotForSelectedSource(
        const Stack::RawRecipe::RawDevelopmentRecipe& recipe);
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
    void PostNotification(UiNotificationSeverity severity, std::string message, std::string dedupeKey = "");
    void LoadSourceFromImagePayload(const EditorNodeGraph::ImagePayload& payload, bool loadCompositePreview, bool markDirty);
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
    void ResetProjectInteractionState();
    void ResetRenderSubmissionState();
    StackAppearance::AppearanceManager* m_Appearance = nullptr;
    StackAppearance::AppearanceManager* m_GraphCaptureAppearanceOverride = nullptr;
    bool m_LibraryWindowHovered = false;
};
