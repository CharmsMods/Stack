#pragma once
#include "Editor/GraphScopeData.h"
#include "Editor/GraphRenderPolicy.h"

#include "ThirdParty/json.hpp"
#include "Raw/RawAutoBase.h"
#include "Raw/RawViewportCalibration.h"
#include "Raw/RawViewportRegion.h"
#include "Raw/RawAutoStartPoint.h"
#include "Raw/RawColorCloudPacket.h"
#include "Raw/RawGpuImageLease.h"
#include "Raw/RawImageAnalysis.h"
#include "Raw/RawPreciseNativeRuntime.h"
#include "Editor/RawRenderPurpose.h"
#include "Renderer/GLLoader.h"
#include "Renderer/MaskRenderTypes.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/RenderTiling.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

struct GLFWwindow;
class RenderPipeline;

class EditorRenderWorker {
public:
    struct RawRenderTelemetry {
        std::chrono::steady_clock::time_point snapshotReadyAt {};
        std::chrono::steady_clock::time_point queuedAt {};
        double queueWaitMs = 0.0;
        double commandEnqueueMs = 0.0;
        double sessionExpansionMs = 0.0;
        double workerTotalMs = 0.0;
        double uiAdoptionMs = 0.0;
        double predictedMs = 0.0;
        double cpuDispatchMs = 0.0;
        double gpuServiceMs = 0.0;
        double completedServiceMs = 0.0;
        std::size_t timingContext = 0;
        std::size_t timingWorkload = 0;
        std::uint64_t timingHistoryEpoch = 0;
        std::size_t sourceTransferredBytes = 0;
        std::size_t publishedTextureBytes = 0;
        std::size_t readbackTransferredBytes = 0;
        std::size_t fullFrameEstimatedWorkingSetBytes = 0;
        bool workerStarted = false;
        int targetFps = 30;
        std::uint64_t gestureId = 0;
        bool overloadSkipped = false;
        RawPreviewSkipReason skipReason = RawPreviewSkipReason::None;
        bool interactionActive = false;
        bool fullFrameRefinementRequested = false;
        bool fullFrameRefinementBudgetAllowed = false;
        bool superseded = false;
    };
    struct SharedTextureResult {
        SharedTextureResult() = default;
        SharedTextureResult(const SharedTextureResult&) = delete;
        SharedTextureResult& operator=(const SharedTextureResult&) = delete;
        SharedTextureResult(SharedTextureResult&& other) noexcept {
            MoveFrom(std::move(other));
        }
        SharedTextureResult& operator=(SharedTextureResult&& other) noexcept {
            if (this != &other) {
                Reset();
                MoveFrom(std::move(other));
            }
            return *this;
        }
        ~SharedTextureResult() { Reset(); }

        bool EnsureLease(
            Raw::RawGpuImageFamily family =
                Raw::RawGpuImageFamily::Presentation) {
            if (lease) {
                return lease.Texture() == texture;
            }
            if (texture == 0 || width <= 0 || height <= 0) {
                return false;
            }
            lease = Raw::RawGpuImageLease::AdoptOwned(
                texture, width, height, family);
            return static_cast<bool>(lease);
        }

        unsigned int ReleaseTextureName() {
            if (readyFence != nullptr) {
                glDeleteSync(readyFence);
                readyFence = nullptr;
            }
            unsigned int released = lease
                ? lease.ReleaseTextureName()
                : texture;
            if (lease && released == 0) {
                return 0;
            }
            texture = 0;
            width = 0;
            height = 0;
            return released;
        }

        void Reset() {
            if (readyFence != nullptr) {
                glDeleteSync(readyFence);
                readyFence = nullptr;
            }
            if (lease) {
                lease.Reset();
            } else if (texture != 0) {
                glDeleteTextures(1, &texture);
            }
            texture = 0;
            width = 0;
            height = 0;
        }

        unsigned int texture = 0;
        int width = 0;
        int height = 0;
        GLsync readyFence = nullptr;
        Raw::RawGpuImageLease lease;

    private:
        void MoveFrom(SharedTextureResult&& other) noexcept {
            texture = other.texture;
            width = other.width;
            height = other.height;
            readyFence = other.readyFence;
            lease = std::move(other.lease);
            other.texture = 0;
            other.width = 0;
            other.height = 0;
            other.readyFence = nullptr;
        }
    };

    struct SharedTextureTile {
        unsigned int texture = 0;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        int haloX = 0;
        int haloY = 0;
        int haloWidth = 0;
        int haloHeight = 0;
    };

    struct SharedTextureTileSet {
        std::vector<SharedTextureTile> tiles;
        int fullWidth = 0;
        int fullHeight = 0;
        bool tiled = false;
        bool complete = false;
        bool debugOverlay = false;
        GLsync readyFence = nullptr;
    };

    struct CompositeOutputRequest {
        int outputNodeId = -1;
        int sourceNodeId = -1;
        SharedPixelBuffer sourcePixels;
        int width = 0;
        int height = 0;
        int channels = 4;
        bool preparePixels = false;
        int trimPadding = 0;
        bool keepFullFrame = false;
        std::uint64_t dirtyGeneration = 0;
        std::size_t chainFingerprint = 0;
    };

    struct CompositeOutputResult {
        int outputNodeId = -1;
        bool success = false;
        bool pixelsPrepared = false;
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        std::uint64_t dirtyGeneration = 0;
        std::size_t chainFingerprint = 0;
        std::string error;
    };

    struct PreviewRequest {
        std::string rawLayerId;
        std::string rawMaskId;
        int previewNodeId = -1;
        int sourceNodeId = -1;
        std::string sourceSocketId;
        bool maskInput = false;
        bool directSourceOutput = false;
        bool scopeAnalysis = false;
        bool frequencySpectrumInput = false;
        RenderFrequencyEdgePolicy frequencyEdgePolicy =
            RenderFrequencyEdgePolicy::Mirror;
        SharedPixelBuffer sourcePixels;
        int width = 0;
        int height = 0;
        int channels = 4;
        std::uint64_t dirtyGeneration = 0;
    };

    struct PreviewResult {
        std::string rawLayerId;
        std::string rawMaskId;
        int previewNodeId = -1;
        bool success = false;
        std::vector<unsigned char> pixels;
        std::shared_ptr<const GraphScopeData> scopeData;
        int width = 0;
        int height = 0;
        std::uint64_t dirtyGeneration = 0;
        std::string error;
    };

    struct DevelopSubjectMetricPoint {
        float x = 0.5f;
        float y = 0.5f;
    };

    struct DevelopSubjectMetricRegion {
        int id = 0;
        int mode = 0;
        bool enabled = true;
        bool lowPriority = false;
        float centerX = 0.5f;
        float centerY = 0.5f;
        float radiusX = 0.18f;
        float radiusY = 0.18f;
        float feather = 0.35f;
        float strength = 0.75f;
    };

    struct DevelopSubjectMetricStroke {
        int id = 0;
        int mode = 0;
        bool enabled = true;
        bool lowPriority = false;
        float radius = 0.045f;
        float feather = 0.35f;
        float strength = 0.75f;
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 1.0f;
        float maxY = 1.0f;
        std::vector<DevelopSubjectMetricPoint> points;
    };

    struct DevelopSubjectMetricSampling {
        bool enabled = false;
        std::vector<DevelopSubjectMetricRegion> regions;
        std::vector<DevelopSubjectMetricStroke> strokes;
    };

    struct DevelopCandidateRenderRequest {
        int developNodeId = -1;
        std::string candidateId;
        std::string candidateLabel;
        std::string candidateRevisionStage;
        std::string activeRevisionStage;
        std::string activeRefineIntent;
        std::string stageSchedulerExpectedDirtyBoundary;
        std::string stageSchedulerReason;
        RenderGraphRawDevelopPayload rawDevelop;
        std::uint64_t dirtyGeneration = 0;
        std::uint64_t solveFingerprint = 0;
        std::uint64_t rawDevelopInteractionSerial = 0;
        std::uint64_t guidanceFingerprint = 0;
        float solveScore = 0.0f;
        DevelopSubjectMetricSampling subjectSampling;
        int stageSchedulerOrder = 0;
        int stageSchedulerRank = 0;
        int adaptiveRenderBudget = 4;
        std::string adaptiveRenderBudgetVersion;
        std::string adaptiveRenderBudgetReason;
        std::string adaptiveRenderBudgetContinuationDecision;
        std::string adaptiveRenderBudgetConvergenceState;
        std::string adaptiveRenderBudgetConvergenceDecision;
        std::string adaptiveRenderBudgetConvergenceReason;
        bool activeStageMatch = false;
        bool stageReservedRequest = false;
        bool activeRefineIntentMatch = false;
        bool refineIntentReservedRequest = false;
        bool adaptiveRenderBudgetExpanded = false;
        bool adaptiveRenderBudgetNarrowed = false;
        bool measurePreFinish = true;
        int metricReadbackMaxDimension = 0;
    };

    struct DevelopCandidateRenderMetrics {
        float meanLuma = 0.0f;
        float medianLuma = 0.0f;
        float p10Luma = 0.0f;
        float p90Luma = 0.0f;
        float shadowFraction = 0.0f;
        float highlightFraction = 0.0f;
        float clippedFraction = 0.0f;
        float contrastSpan = 0.0f;
        float meanRed = 0.0f;
        float meanGreen = 0.0f;
        float meanBlue = 0.0f;
        float warmCoolBias = 0.0f;
        float magentaGreenBias = 0.0f;
        float channelImbalance = 0.0f;
        float colorCastRisk = 0.0f;
        float meanSaturation = 0.0f;
        float lowSaturationFraction = 0.0f;
        float highlightBandFraction = 0.0f;
        float highlightMeanLuma = 0.0f;
        float highlightLowSaturationFraction = 0.0f;
        float highlightGrayRisk = 0.0f;
        float highlightTileCoverage = 0.0f;
        float highlightStructureScore = 0.0f;
        float meaningfulHighlightPressure = 0.0f;
        float edgeContrast = 0.0f;
        float haloRiskFraction = 0.0f;
        float shadowTextureRisk = 0.0f;
        std::array<float, 9> localMeanLuma {};
        std::array<float, 9> localContrastSpan {};
        std::array<float, 9> localDamageRiskScore {};
        float localLumaSpread = 0.0f;
        float localEvSpreadStops = 0.0f;
        float localEvConflict = 0.0f;
        float localContrastPeak = 0.0f;
        float localShadowPressure = 0.0f;
        float localHighlightPressure = 0.0f;
        float localDamageRiskMean = 0.0f;
        float localDamageRiskPeak = 0.0f;
        int localDamageRiskPeakTile = -1;
        float localExposureHighlightCrowding = 0.0f;
        float localExposureShadowCrowding = 0.0f;
        float localExposureHaloStress = 0.0f;
        float localExposureFlatnessRisk = 0.0f;
        float localExposureDamageRisk = 0.0f;
        float subjectCenterPrior = 0.0f;
        float subjectReadabilityPressure = 0.0f;
        float subjectProtectionPressure = 0.0f;
        float subjectMoodPreservationPressure = 0.0f;
        float subjectImportanceConfidence = 0.0f;
        float centerMeanLuma = 0.0f;
        float centerShadowFraction = 0.0f;
        float centerHighlightFraction = 0.0f;
        int subjectMarkedSampleCount = 0;
        float subjectMarkedCoverage = 0.0f;
        float subjectMarkedPositiveCoverage = 0.0f;
        float subjectMarkedRevealCoverage = 0.0f;
        float subjectMarkedProtectCoverage = 0.0f;
        float subjectMarkedMoodCoverage = 0.0f;
        float subjectMarkedLowPriorityCoverage = 0.0f;
        float subjectMarkedMeanLuma = 0.0f;
        float subjectMarkedShadowFraction = 0.0f;
        float subjectMarkedHighlightFraction = 0.0f;
        float subjectMarkedClippedFraction = 0.0f;
        float subjectMarkedContrastSpan = 0.0f;
        float subjectMarkedReadabilityScore = 0.0f;
        float subjectMarkedProtectionRisk = 0.0f;
        float subjectMarkedMoodPreservationScore = 0.0f;
        float subjectMarkedLowPriorityMeanLuma = 0.0f;
        float subjectMarkedLowPriorityBrightFraction = 0.0f;
        float subjectMarkedLowPriorityPressure = 0.0f;
    };

    struct DevelopCandidateRenderResult {
        int developNodeId = -1;
        std::string candidateId;
        std::string candidateLabel;
        std::string candidateRevisionStage;
        std::string activeRevisionStage;
        std::string activeRefineIntent;
        std::string stageSchedulerExpectedDirtyBoundary;
        std::string stageSchedulerReason;
        bool success = false;
        int width = 0;
        int height = 0;
        bool preFinishSuccess = false;
        int preFinishWidth = 0;
        int preFinishHeight = 0;
        bool preFinishReusedFromFinalRender = false;
        bool rawBaseCacheHitDuringFinalRender = false;
        bool preFinishCacheHitDuringFinalRender = false;
        std::uint64_t dirtyGeneration = 0;
        std::uint64_t solveFingerprint = 0;
        std::uint64_t rawDevelopInteractionSerial = 0;
        std::uint64_t guidanceFingerprint = 0;
        float solveScore = 0.0f;
        int stageSchedulerOrder = 0;
        int stageSchedulerRank = 0;
        int adaptiveRenderBudget = 4;
        std::string adaptiveRenderBudgetVersion;
        std::string adaptiveRenderBudgetReason;
        std::string adaptiveRenderBudgetContinuationDecision;
        std::string adaptiveRenderBudgetConvergenceState;
        std::string adaptiveRenderBudgetConvergenceDecision;
        std::string adaptiveRenderBudgetConvergenceReason;
        bool activeStageMatch = false;
        bool stageReservedRequest = false;
        bool activeRefineIntentMatch = false;
        bool refineIntentReservedRequest = false;
        bool adaptiveRenderBudgetExpanded = false;
        bool adaptiveRenderBudgetNarrowed = false;
        int metricReadbackMaxDimension = 0;
        bool metricsReadbackDownsampled = false;
        bool preFinishMetricsReadbackDownsampled = false;
        float finalGraphMs = 0.0f;
        float finalReadbackMs = 0.0f;
        float finalAnalysisMs = 0.0f;
        float preFinishGraphMs = 0.0f;
        float preFinishReadbackMs = 0.0f;
        float preFinishAnalysisMs = 0.0f;
        float totalElapsedMs = 0.0f;
        DevelopCandidateRenderMetrics metrics;
        DevelopCandidateRenderMetrics preFinishMetrics;
        std::string error;
    };

    struct RawWorkspaceSnapshot {
        std::string sourceKey;
        std::uint64_t sourceHash = 0;
        std::size_t recipeRevision = 0;
        int fullFrameWidth = 0;
        int fullFrameHeight = 0;
        std::uint64_t gpuWorkingBudgetBytes = 0;
        std::uint64_t gpuCacheBudgetBytes = 0;
        std::uint64_t minimumRawStageCacheBytes = 0;
        int managedRawDecodeNodeId = -1;
        int managedToneCurveNodeId = -1;
        int managedViewTransformNodeId = -1;
        std::string localRangeOverlayMode;
        Raw::ViewportRequest viewport;
        Raw::ViewportStage editStage = Raw::ViewportStage::RawBase;
        Raw::ViewportTimingBank::Keys graphWorkloadKeys {};
        bool calibrationFirstUse = true;
        int viewportDependencyEdge = -1;
        bool hasRecipe = false;
        Stack::RawRecipe::RawDevelopmentRecipe recipe;
        bool localRangeTargetSampleRequested = false;
        bool localRangeTargetHoverSample = false;
        float localRangeTargetSampleU = 0.0f;
        float localRangeTargetSampleV = 0.0f;
        RawLocalRangeTargetPreviewRequest localRangeTargetPreview;
        bool globalExposureInteractionActive = false;
        bool analysisRequested = true;
        RawDevelopmentGraphScopeStage graphScopeStage =
            RawDevelopmentGraphScopeStage::None;
        int graphScopeMaxDimension = 192;
        std::size_t graphScopeInputFingerprint = 0;
        RawDevelopmentGradingScopeSource gradingScopeSource =
            RawDevelopmentGradingScopeSource::None;
        std::vector<Stack::RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest>
            startPointCandidateRenderRequests;
        std::optional<Stack::PreciseIntegration::NativeSolveRequest>
            preciseSolveRequest;
        std::optional<Stack::Renderer::RawDevelopmentCache::Stage>
            cachePrewarmStage;
        std::optional<Stack::Renderer::RawDevelopmentCache::Stage>
            preferredCacheInputStage;
        std::size_t cachePrewarmFingerprint = 0;
        std::uint64_t cachePrewarmByteBudget = 0;
    };

    struct RawWorkspaceTargetSampleResult {
        bool valid = false;
        float sceneEv = 0.0f;
        float sceneLuma = 0.0f;
        float sceneR = 0.0f;
        float sceneG = 0.0f;
        float sceneB = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        std::uint32_t authoredZoneHitBits = 0;
        float strongestAuthoredZoneWeight = 0.0f;
    };

    using RawWorkspaceStartPointCandidateRenderResult =
        Stack::RawAutoStartPoint::RawAutoStartPointCandidateRenderResult;

    struct RawWorkspaceResult {
        std::string sourceKey;
        std::uint64_t sourceHash = 0;
        std::size_t recipeRevision = 0;
        bool viewportEncodedSrgb = false;
        bool viewportDiagnostic = false;
        std::size_t presentationFingerprint = 0;
        Raw::ViewportRegion viewportRegion;
        std::uint64_t viewportGeneration = 0;
        int expectedNativeOutputWidth = 0;
        int expectedNativeOutputHeight = 0;
        bool analysisCaptured = false;
        std::string localRangeOverlayMode;
        SharedTextureResult localRangeOverlayTexture;
        int localRangeOverlayWidth = 0;
        int localRangeOverlayHeight = 0;
        std::uint64_t localRangeTargetPreviewGeneration = 0;
        bool localRangeTargetPreviewRefined = false;
        bool localRangeTargetPreviewRefinementPending = false;
        RawLocalRangeTargetPreviewMetrics localRangeTargetPreviewMetrics;
        RenderTextureStats viewTransformInputStats;
        RenderTextureStats finalDisplayStats;
        std::vector<RawDevelopmentStageStatsReadback> stageStatsReadbacks;
        RawDevelopmentGraphScopeReadback graphScopeReadback;
        Raw::RawColorCloudPacket colorWarpCloudPacket;
        std::shared_ptr<const RawGradingScopeVisualization> gradingScopeVisualization;
        std::size_t graphScopeInputFingerprint = 0;
        Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics startPointDiagnostics;
        std::vector<Stack::RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest>
            startPointCandidateRenderRequests;
        std::vector<RawWorkspaceStartPointCandidateRenderResult>
            startPointCandidateRenderResults;
        Stack::RawAnalysis::RawImageAnalysis analysis;
        Stack::RawAutoBase::AutoBaseRecommendations recommendations;
        RawWorkspaceTargetSampleResult localRangeTargetSample;
        std::optional<Stack::PreciseIntegration::NativeSolveResult>
            preciseSolveResult;
        std::optional<Stack::Renderer::RawDevelopmentCache::Stage>
            cachePrewarmStage;
        std::size_t cachePrewarmFingerprint = 0;
        bool cachePrewarmCompleted = false;

        bool HasSource() const { return !sourceKey.empty(); }
    };

    struct Snapshot {
        std::uint64_t ownerId = 0;
        std::uint64_t generation = 0;
        std::uint64_t schedulingSerial = 0;
        Stack::GraphRendering::RequestTag graphRequest;
        std::uint64_t lastAcceptedGeneration = 0;
        RawRenderPurpose rawRenderPurpose =
            RawRenderPurpose::InteractivePresentation;
        RawRenderTelemetry telemetry;
        bool outputConnected = false;
        int previewMaxDimension = 0;
        RawWorkspaceSnapshot rawWorkspace;
        SharedPixelBuffer sourcePixels;
        int width = 0;
        int height = 0;
        int channels = 4;
        ViewportTilingSettings viewportTiling;
        std::vector<nlohmann::json> layers;
        std::vector<nlohmann::json> layerSteps;
        std::vector<RenderMaskSource> masks;
        RenderGraphSnapshot graph;
        std::vector<CompositeOutputRequest> compositeOutputs;
        std::vector<PreviewRequest> previews;
        std::vector<DevelopCandidateRenderRequest> developCandidateRenders;
        // A compact RAW command points at an immutable session template. The
        // worker expands and patches that template on its own thread, keeping
        // graph/JSON copies out of authoring input frames.
        std::shared_ptr<const Snapshot> rawSessionTemplate;
        std::uint64_t rawSessionContractRevision = 0;
        bool rawSessionCommand = false;
    };

    struct Result {
        std::uint64_t ownerId = 0;
        std::uint64_t generation = 0;
        std::uint64_t schedulingSerial = 0;
        Stack::GraphRendering::RequestTag graphRequest;
        std::uint64_t lastAcceptedGeneration = 0;
        RawRenderPurpose rawRenderPurpose =
            RawRenderPurpose::InteractivePresentation;
        RawRenderTelemetry telemetry;
        int previewMaxDimension = 0;
        RawWorkspaceResult rawWorkspace;
        Raw::ViewportStageCosts stageCosts {};
        Raw::ViewportStageCosts cpuStageCosts {}, gpuStageCosts {};
        std::array<bool,Raw::kViewportStageCount> measuredStages {};
        bool timingOnly = false;
        int timingNativeWidth = 0, timingNativeHeight = 0;
        int timingOutputWidth = 0, timingOutputHeight = 0;
        std::string timingRepresentation;
        std::size_t firstMeasuredStage = 0;
        std::array<std::size_t, Raw::kViewportStageCount> cachedStages {};
        int cacheEdge = 0;
        bool cacheStateMeasured = false;
        std::array<std::size_t, Raw::kViewportStageCount> nativeCachedStages {};
        std::array<std::size_t, Raw::kViewportStageCount> workloadKeys {};
        Raw::ViewportStage editStage = Raw::ViewportStage::RawBase;
        bool success = false;
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        SharedTextureResult outputTexture;
        SharedTextureTileSet outputTiles;
        std::string error;
        std::vector<CompositeOutputResult> compositeOutputs;
        std::vector<PreviewResult> previews;
        std::vector<DevelopCandidateRenderResult> developCandidateRenders;
        std::vector<ToneCurveAutoRewriteFeedback> toneCurveAutoRewrites;
        float mainRenderMs = 0.0f;
        float previewRenderMs = 0.0f;
        float compositeRenderMs = 0.0f;
        int renderedPreviewCount = 0;
        int renderedCompositeCount = 0;
        bool mainRegionPlanAvailable = false;
        bool mainRegionPlanTileable = false;
        int mainRegionPlanHaloX = 0;
        int mainRegionPlanHaloY = 0;
        std::string mainRegionPlanReason;
        GraphExecutionStats mainGraphStats;
        Raw::ViewportCalibrationSample calibration;
    };

    struct RenderProgress {
        bool busy = false;
        int completedSteps = 0;
        int totalSteps = 0;
        std::string label;
    };

    EditorRenderWorker();
    ~EditorRenderWorker();

    using OpenGlTask = std::function<bool(std::string&)>;

    bool Initialize(GLFWwindow* sharedWindow);
    // Executes a short, self-contained compute task on the worker's hidden
    // shared OpenGL context. The caller may be any non-render thread. Shutdown
    // and worker failure always release a blocked caller with an error.
    bool ExecuteOpenGlTaskBlocking(OpenGlTask task, std::string& error);
    void RequestStopForShutdown();
    void Shutdown();
    void InvalidateSnapshotsBefore(std::uint64_t generation);
    void CancelActiveAndPending();
    void CancelOwnerSnapshots(std::uint64_t ownerId,
        std::optional<RawRenderPurpose> purpose = std::nullopt);
    void InvalidateOwnerSnapshotsBefore(std::uint64_t ownerId, std::uint64_t generation);
    void ReleaseOwner(std::uint64_t ownerId);
    bool HasPendingForOwner(std::uint64_t ownerId) const;
    bool TryConsumeReadyDenoiseSnapshot(Snapshot& snapshot);
    bool Submit(Snapshot snapshot);
    bool TryConsumeCompleted(Result& result);
    bool TryConsumeViewportTiming(Result& result);
    bool IsBusy() const { return m_Busy.load(); }
    bool IsAvailable() const;
    void UpdateGraphContext(const Stack::GraphRendering::RequestTag& context);
    bool IsWaitingForRawDenoiseCompletion() const;
    bool HasPendingOrBusyForShutdown() const;
    RenderProgress GetProgress() const;
    static DevelopCandidateRenderMetrics AnalyzeDevelopCandidatePixelsForValidation(
        const std::vector<unsigned char>& pixels,
        int width,
        int height);
    static DevelopCandidateRenderMetrics AnalyzeDevelopCandidatePixelsForValidation(
        const std::vector<unsigned char>& pixels,
        int width,
        int height,
        const DevelopSubjectMetricSampling& subjectSampling);
    static float CompareDevelopCandidateRenderMetrics(
        const DevelopCandidateRenderMetrics& a,
        const DevelopCandidateRenderMetrics& b);
    static bool ShouldAbortStaleSnapshotForValidation(
        std::uint64_t currentGeneration,
        bool stopRequested,
        bool hasPendingSnapshot,
        std::uint64_t pendingGeneration);
    static std::string BuildDevelopCandidateProgressLabelForValidation(
        const std::string& candidateLabel,
        const std::string& candidateRevisionStage,
        int candidateIndex,
        int candidateCount);

private:
    struct OwnerRenderState {
        std::shared_ptr<RawRgbDenoiseState> denoise;
        std::optional<Snapshot> continuation;
        bool continuationReady = false;
        bool resumeOriginalPurpose = false;
        bool denoiseInFlight = false;
        bool resetDenoiseRequested = false;
        bool released = false;
        RawRenderPurpose denoisePurpose = RawRenderPurpose::InteractivePresentation;
        std::uint64_t invalidBeforeGeneration = 0;
        std::uint64_t invalidBeforeSerial = 0;
        std::unordered_map<RawRenderPurpose, std::uint64_t> canceledPurposeBeforeSerial;
        std::uint64_t latestGeneration = 0;
        std::uint64_t latestSerial = 0;
        Stack::GraphRendering::RequestTag latestGraphRequest;
    };

    struct DrainingDenoiseState {
        std::uint64_t ownerId = 0;
        std::shared_ptr<RawRgbDenoiseState> state;
    };

    struct OpenGlTaskState {
        OpenGlTask task;
        std::mutex mutex;
        std::condition_variable cv;
        bool completed = false;
        bool success = false;
        std::string error;
    };

    void PrepareGraphResourceBudget(const Snapshot& snapshot, RenderPipeline& pipeline, Result& result);
    void ThreadMain();
    void CaptureViewportTiming(const Snapshot& snapshot, Result& result);
    void PollViewportTimings();
    struct PendingViewportTiming {
        Raw::ViewportGpuTiming::Batch batch;
        Result evidence;
    };
    std::deque<PendingViewportTiming> m_PendingViewportTimings;
    void HandleThreadFailure(const char* message) noexcept;
    Result RenderSnapshot(const Snapshot& snapshot);
    void RenderDevelopCandidateRequests(
        const Snapshot& snapshot,
        RenderPipeline& pipeline,
        Result& result,
        int totalProgressSteps,
        int& progressCompleted);
    void SetProgress(int completedSteps, int totalSteps, std::string label);
    void AdvanceProgress(std::string label = {});
    bool ShouldAbortStaleSnapshot(const Snapshot& snapshot) const;
    bool IsOwnerSnapshotInvalidLocked(std::uint64_t ownerId,
        std::uint64_t generation, std::uint64_t serial, RawRenderPurpose purpose) const;
    bool IsOwnerResultStaleLocked(const Result& result) const;
    bool HasDenoiseWorkLocked() const;
    void BindDenoiseState(const Snapshot& snapshot, RenderPipeline& pipeline);
    void PollDenoiseContinuationsLocked();
    bool CanStartAuthoritativeRenderLocked(std::uint64_t ownerId) const;
    bool DeferAuthoritativeRenderLocked(Snapshot& snapshot);
    void RetainDenoiseContinuationLocked(Snapshot& snapshot);
    void DrainDenoiseForShutdown() noexcept;

    GLFWwindow* m_WorkerWindow = nullptr;
    std::thread m_Thread;
    mutable std::mutex m_Mutex;
    std::condition_variable m_Cv;
    bool m_StopRequested = false;
    bool m_InitComplete = false;
    bool m_InitSucceeded = false;
    std::string m_InitError;
    bool m_HasPending = false;
    Stack::GraphRendering::RequestTag m_LatestGraphRequest;
    Snapshot m_Pending;
    std::queue<std::shared_ptr<OpenGlTaskState>> m_OpenGlTasks;
    std::shared_ptr<OpenGlTaskState> m_ActiveOpenGlTask;
    std::deque<Result> m_Completed;
    std::queue<Result> m_CompletedTimings;
    std::queue<Result> m_CompletedViewportTimings;
    std::optional<Result> m_FallbackCompleted;
    std::unordered_map<std::uint64_t, OwnerRenderState> m_OwnerStates;
    std::vector<DrainingDenoiseState> m_DrainingDenoise;
    bool m_Rendering = false;
    std::uint64_t m_RenderingOwner = 0;
    std::atomic<bool> m_Busy = false;
    std::uint64_t m_InvalidBeforeGeneration = 0;
    std::uint64_t m_NextSchedulingSerial = 1;
    std::uint64_t m_InvalidBeforeSchedulingSerial = 0;
    int m_ProgressCompletedSteps = 0;
    int m_ProgressTotalSteps = 0;
    std::string m_ProgressLabel;
    std::unique_ptr<RenderPipeline> m_PersistentPipeline;
    std::unique_ptr<RenderPipeline> m_CalibrationPipeline;
};
