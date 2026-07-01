#pragma once

#include "ThirdParty/json.hpp"
#include "Raw/RawAutoBase.h"
#include "Raw/RawImageAnalysis.h"
#include "Renderer/GLLoader.h"
#include "Renderer/MaskRenderTypes.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/RenderTiling.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

struct GLFWwindow;
class RenderPipeline;

class EditorRenderWorker {
public:
    struct SharedTextureResult {
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
        GLsync readyFence = nullptr;
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
        std::uint64_t dirtyGeneration = 0;
        std::size_t chainFingerprint = 0;
    };

    struct CompositeOutputResult {
        int outputNodeId = -1;
        bool success = false;
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        std::uint64_t dirtyGeneration = 0;
        std::size_t chainFingerprint = 0;
        std::string error;
    };

    struct PreviewRequest {
        int previewNodeId = -1;
        int sourceNodeId = -1;
        std::string sourceSocketId;
        bool maskInput = false;
        bool directSourceOutput = false;
        SharedPixelBuffer sourcePixels;
        int width = 0;
        int height = 0;
        int channels = 4;
        std::uint64_t dirtyGeneration = 0;
    };

    struct PreviewResult {
        int previewNodeId = -1;
        bool success = false;
        std::vector<unsigned char> pixels;
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
        std::string localRangeOverlayMode;
        bool hasRecipe = false;
        Stack::RawRecipe::RawDevelopmentRecipe recipe;
        bool localRangeTargetSampleRequested = false;
        float localRangeTargetSampleU = 0.0f;
        float localRangeTargetSampleV = 0.0f;
        bool analysisRequested = true;
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
    };

    struct RawWorkspaceResult {
        std::string sourceKey;
        std::string localRangeOverlayMode;
        std::vector<unsigned char> localRangeOverlayPixels;
        int localRangeOverlayWidth = 0;
        int localRangeOverlayHeight = 0;
        RenderTextureStats viewTransformInputStats;
        Stack::RawAnalysis::RawImageAnalysis analysis;
        Stack::RawAutoBase::AutoBaseRecommendations recommendations;
        RawWorkspaceTargetSampleResult localRangeTargetSample;

        bool HasSource() const { return !sourceKey.empty(); }
    };

    struct Snapshot {
        std::uint64_t generation = 0;
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
    };

    struct Result {
        std::uint64_t generation = 0;
        int previewMaxDimension = 0;
        RawWorkspaceResult rawWorkspace;
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
        GraphExecutionStats mainGraphStats;
    };

    struct RenderProgress {
        bool busy = false;
        int completedSteps = 0;
        int totalSteps = 0;
        std::string label;
    };

    EditorRenderWorker();
    ~EditorRenderWorker();

    bool Initialize(GLFWwindow* sharedWindow);
    void RequestStopForShutdown();
    void Shutdown();
    void InvalidateSnapshotsBefore(std::uint64_t generation);
    void Submit(Snapshot snapshot);
    bool TryConsumeCompleted(Result& result);
    bool IsBusy() const { return m_Busy.load(); }
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
    void ThreadMain();
    Result RenderSnapshot(const Snapshot& snapshot);
    void RenderDevelopCandidateRequests(
        const Snapshot& snapshot,
        RenderPipeline& pipeline,
        Result& result,
        int totalProgressSteps,
        int& progressCompleted);
    void SetProgress(int completedSteps, int totalSteps, std::string label);
    void AdvanceProgress(std::string label = {});
    bool ShouldAbortStaleSnapshot(std::uint64_t currentGeneration) const;

    GLFWwindow* m_WorkerWindow = nullptr;
    std::thread m_Thread;
    mutable std::mutex m_Mutex;
    std::condition_variable m_Cv;
    bool m_StopRequested = false;
    bool m_InitComplete = false;
    bool m_InitSucceeded = false;
    std::string m_InitError;
    bool m_HasPending = false;
    Snapshot m_Pending;
    std::queue<Result> m_Completed;
    std::atomic<bool> m_Busy = false;
    std::uint64_t m_InvalidBeforeGeneration = 0;
    int m_ProgressCompletedSteps = 0;
    int m_ProgressTotalSteps = 0;
    std::string m_ProgressLabel;
    std::unique_ptr<RenderPipeline> m_PersistentPipeline;
};
