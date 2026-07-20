#pragma once

#include "Raw/RawDevelopmentRecipe.h"
#include "ThirdParty/json.hpp"

#include <array>
#include <string>
#include <vector>

namespace Stack::RawAnalysis {
struct RawImageAnalysis;
} // namespace Stack::RawAnalysis

namespace Stack::RawAutoBase {
struct AutoBaseRecommendations;
} // namespace Stack::RawAutoBase

namespace Stack::RawAutoStartPoint {

inline constexpr int kRawAutoStartPointDiagnosticsVersion = 1;

enum class RawAutoStartPointIntent {
    Base,
    Balanced,
    Farther
};

enum class RawAutoStartPointCandidateKind {
    CurrentFit,
    Base,
    Balanced,
    Farther
};

enum class RawAutoStartPointStage {
    RawTechnical,
    NeutralScene,
    RawPlacement,
    LocalCandidate,
    FinishToneCandidate,
    DisplayCandidate
};

enum class RawAutoStartPointStageStatus {
    Unavailable,
    Pending,
    Complete,
    Fallback,
    Projected
};

enum class RawAutoStartPointControl {
    RawExposure,
    WhiteBalance,
    LocalRange,
    FinishTone,
    DisplayFit
};

enum class RawAutoStartPointDiagnosticSeverity {
    Info,
    Warning,
    Error
};

struct RawAutoStartPointPercentiles {
    bool valid = false;
    float p01 = 0.0f;
    float p05 = 0.0f;
    float p10 = 0.0f;
    float p25 = 0.0f;
    float p50 = 0.0f;
    float p75 = 0.0f;
    float p90 = 0.0f;
    float p95 = 0.0f;
    float p99 = 0.0f;
    float p999 = 0.0f;
};

struct RawAutoStartPointEvBucketValue {
    float lowEv = 0.0f;
    float highEv = 0.0f;
    float value = 0.0f;
    std::string label;
};

struct RawAutoStartPointToneBucketSpatialSummary {
    std::string bucketId;
    float areaFraction = 0.0f;
    float centerWeight = 0.0f;
    float topBandWeight = 0.0f;
    float medianEv = 0.0f;
};

struct RawAutoStartPointNeutralSampleSummary {
    bool valid = false;
    int sampleCount = 0;
    float eligibleFraction = 0.0f;
    float medianChroma = 0.0f;
    float gainDistanceEv = 0.0f;
    std::array<float, 3> estimatedGains { 1.0f, 1.0f, 1.0f };
    std::string method;
    std::string statusMessage;
};

struct RawAutoStartPointRawSafetyStats {
    bool valid = false;
    float activeValidFraction = 0.0f;
    std::string blackLevelSource;
    std::string whiteLevelSource;
    float maskedBlackMean = 0.0f;
    float maskedBlackStd = 0.0f;
    bool blackDriftWarning = false;
    float lensShadingConfidence = 0.0f;
    float cornerFalloffEv = 0.0f;
    std::array<float, 3> perChannelClippedFraction { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> perChannelNearClippedFraction { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> perChannelP999 { 0.0f, 0.0f, 0.0f };
    float rawWhiteP999 = 0.0f;
    float linearResponseLimit = 1.0f;
    float headroomEv = 0.0f;
    float wbScaledHeadroomEv = 0.0f;
    float singleChannelClipFraction = 0.0f;
    float multiChannelClipFraction = 0.0f;
    float fullClipFraction = 0.0f;
    float highlightRecoverabilityScore = 0.0f;
    float hotPixelFraction = 0.0f;
    bool noiseProfileAvailable = false;
    std::vector<RawAutoStartPointEvBucketValue> shadowSnrByEvBucket;
    float baselineExposureEv = 0.0f;
    float colorMatrixConfidence = 0.0f;
    bool asShotWbAvailable = false;
    std::string statusMessage;
};

struct RawAutoStartPointSceneStageStats {
    bool valid = false;
    RawAutoStartPointStage stage = RawAutoStartPointStage::NeutralScene;
    RawAutoStartPointStageStatus status = RawAutoStartPointStageStatus::Unavailable;
    float validPixelFraction = 0.0f;
    RawAutoStartPointPercentiles lumaPercentiles;
    RawAutoStartPointPercentiles evPercentiles;
    float logAverageY = 0.0f;
    float midSpreadEv = 0.0f;
    float wideSpreadEv = 0.0f;
    float shadowMassFraction = 0.0f;
    float highlightMassFraction = 0.0f;
    float centerMedianEv = 0.0f;
    float topBandMedianEv = 0.0f;
    RawAutoStartPointPercentiles gradientWeightedPercentiles;
    std::vector<RawAutoStartPointToneBucketSpatialSummary> toneBucketSpatialSummaries;
    RawAutoStartPointNeutralSampleSummary neutralSampleSummary;
    float negativeChannelFraction = 0.0f;
    float wideGamutPressure = 0.0f;
    float matrixGamutConfidence = 0.0f;
    float localMaskHaloRisk = 0.0f;
    std::vector<RawAutoStartPointEvBucketValue> noiseAfterGainByEvBucket;
    std::string statusMessage;
};

struct RawAutoStartPointDisplayStageStats {
    bool valid = false;
    RawAutoStartPointStageStatus status = RawAutoStartPointStageStatus::Unavailable;
    std::string transferFamily;
    float displayClipHighFraction = 0.0f;
    float displayClipLowFraction = 0.0f;
    float displayLinearP05 = 0.0f;
    float displayLinearP50 = 0.0f;
    float displayLinearP95 = 0.0f;
    float displayP05 = 0.0f;
    float displayP50 = 0.0f;
    float displayP95 = 0.0f;
    float displaySpread = 0.0f;
    float readabilityScore = 0.0f;
    bool metricsAreLinearDisplay = true;
    std::string statusMessage;
};

struct RawAutoStartPointStageDiagnostics {
    RawAutoStartPointStage stage = RawAutoStartPointStage::RawTechnical;
    RawAutoStartPointStageStatus status = RawAutoStartPointStageStatus::Unavailable;
    float confidence01 = 0.0f;
    RawAutoStartPointRawSafetyStats rawSafety;
    RawAutoStartPointSceneStageStats scene;
    RawAutoStartPointDisplayStageStats display;
    std::vector<std::string> warnings;
    std::string statusMessage;
};

struct RawAutoStartPointStageImage {
    bool valid = false;
    RawAutoStartPointStage stage = RawAutoStartPointStage::RawTechnical;
    RawAutoStartPointStageStatus status = RawAutoStartPointStageStatus::Unavailable;
    std::string stageId;
    std::string measurementDomain;
    bool sceneLinearBeforeViewTransform = false;
    bool displayMappedLinearRgb = false;
    int width = 0;
    int height = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    std::vector<float> pixels;
};

struct RawAutoStartPointScoreTerm {
    std::string id;
    std::string label;
    float value01 = 0.0f;
    float weight = 0.0f;
    float weightedValue = 0.0f;
    std::string rationale;
};

struct RawAutoStartPointPenaltyTerm {
    std::string id;
    std::string label;
    float value = 0.0f;
    float weight = 1.0f;
    float weightedValue = 0.0f;
    std::string rationale;
};

struct RawAutoStartPointCandidateSubscores {
    bool valid = false;
    float rawSafetyScore = 0.0f;
    float scenePlacementScore = 0.0f;
    float localConflictScore = 0.0f;
    float toneShapeScore = 0.0f;
    float displayReadabilityScore = 0.0f;
    float editConservatismScore = 0.0f;
    float noisePenalty = 0.0f;
    float colorConstancyPenalty = 0.0f;
    float hiddenCompensationPenalty = 0.0f;
};

struct RawAutoStartPointCandidateScore {
    bool valid = false;
    RawAutoStartPointCandidateSubscores subscores;
    std::vector<RawAutoStartPointScoreTerm> terms;
    std::vector<RawAutoStartPointPenaltyTerm> penalties;
    float totalScore = 0.0f;
    std::string summary;
};

struct RawAutoStartPointVisibleRecipeEdits {
    // Future RAW Exposure automation must write this visible value to RawDevelopmentRecipe::preToneExposureEv.
    bool rawExposureEvValid = false;
    float preToneExposureEv = 0.0f;

    // Future image-derived WB automation must write visible WB mode/multiplier fields, never a hidden color pass.
    bool whiteBalanceValid = false;
    std::string whiteBalancePolicy;
    std::array<float, 3> whiteBalanceMultipliers { 1.0f, 1.0f, 1.0f };

    // Future Local Range automation must author visible graph or mask fields in RawDevelopmentRecipe::localRange.
    bool localRangeValid = false;
    int localRangePointCount = 0;
    std::string localRangeSummary;

    // Future Finish Tone automation must write editable RawDevelopmentRecipe::finishTone.layerJson points.
    bool finishToneValid = false;
    bool finishToneNeutral = true;
    std::string finishToneSummary;

    // Future Display Fit automation must write RawDevelopmentRecipe::viewTransform.layerJson.
    bool displayFitValid = false;
    std::string viewTransformSummary;

    std::vector<RawAutoStartPointControl> touchedControls;
};

struct RawAutoStartPointCandidateRenderRequest {
    bool valid = false;
    std::string id;
    RawAutoStartPointStage stage = RawAutoStartPointStage::RawPlacement;
    RawAutoStartPointStageStatus replacesStatus = RawAutoStartPointStageStatus::Unavailable;
    bool hasRecipe = false;
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    std::vector<RawAutoStartPointControl> expectedControls;
    int featureReadbackMaxDimension = 0;
    std::string reason;
};

struct RawAutoStartPointCandidate {
    bool valid = false;
    RawAutoStartPointCandidateKind kind = RawAutoStartPointCandidateKind::Base;
    std::string id;
    std::string label;
    std::string summary;
    bool hasRecipe = false;
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    RawAutoStartPointVisibleRecipeEdits visibleEdits;
    std::vector<RawAutoStartPointStageDiagnostics> stageDiagnostics;
    std::vector<RawAutoStartPointCandidateRenderRequest> renderRequests;
    RawAutoStartPointCandidateScore score;
    std::vector<std::string> warnings;
};

struct RawAutoStartPointDiagnosticLine {
    RawAutoStartPointDiagnosticSeverity severity = RawAutoStartPointDiagnosticSeverity::Info;
    std::string label;
    std::string value;
    std::string detail;
};

struct RawAutoStartPointDiagnosticsView {
    std::string title = "Build Starting Point Diagnostics";
    std::string summary;
    std::vector<RawAutoStartPointDiagnosticLine> lines;
};

struct RawAutoStartPointDiagnostics {
    int version = kRawAutoStartPointDiagnosticsVersion;
    bool valid = false;
    bool dryRunOnly = true;
    bool appliedRecipeValues = false;
    RawAutoStartPointIntent requestedIntent = RawAutoStartPointIntent::Base;
    bool hasSelectedCandidate = false;
    int selectedCandidateIndex = -1;
    RawAutoStartPointCandidateKind selectedCandidateKind = RawAutoStartPointCandidateKind::Base;
    std::string sourceKey;
    std::string statusMessage;
    std::vector<std::string> warnings;
    RawAutoStartPointRawSafetyStats rawSafety;
    std::vector<RawAutoStartPointCandidate> candidates;
    RawAutoStartPointDiagnosticsView uiView;
};

struct RawAutoStartPointCandidateRenderResult {
    RawAutoStartPointCandidateRenderRequest request;
    bool attempted = false;
    bool success = false;
    std::string error;
    RawAutoStartPointDiagnostics diagnostics;
    bool hasRenderedRecipe = false;
    Stack::RawRecipe::RawDevelopmentRecipe renderedRecipe;
    int renderWidth = 0;
    int renderHeight = 0;
    std::vector<RawAutoStartPointStageImage> stageImageReadbacks;
    int imageCacheHits = 0;
    int imageCacheMisses = 0;
    int rawStageCacheHits = 0;
    int rawStageCacheMisses = 0;
    float renderMs = 0.0f;
};

struct RawAutoStartPointFinishToneProposal {
    bool valid = false;
    bool shadowReveal = false;
    bool dynamicRangeBalance = false;
    float strength = 0.0f;
    float midSpreadEv = 0.0f;
    float wideSpreadEv = 0.0f;
    float logMinEv = -10.0f;
    float logMaxEv = 6.0f;
    nlohmann::json layerJson;
    std::string summary;
    std::vector<std::string> warnings;
};

struct RawAutoStartPointConservativePlan {
    bool valid = false;
    bool hasAnalysis = false;
    bool hasUpstreamRecipeChanges = false;
    bool displayFitRecommended = false;
    bool needsPostApplyDisplayFit = false;
    bool usedRenderedDisplayFit = false;

    Stack::RawRecipe::RawDevelopmentRecipe upstreamRecipe;
    RawAutoStartPointVisibleRecipeEdits visibleEdits;
    std::vector<std::string> appliedSummaries;
    std::vector<std::string> withheldSummaries;
    std::vector<std::string> evidenceSummaries;
    std::vector<std::string> warnings;
    std::string summary;
};

const char* IntentStableString(RawAutoStartPointIntent intent);
const char* IntentLabel(RawAutoStartPointIntent intent);
RawAutoStartPointIntent IntentFromStableString(const std::string& value);

const char* CandidateKindStableString(RawAutoStartPointCandidateKind kind);
const char* CandidateKindLabel(RawAutoStartPointCandidateKind kind);
RawAutoStartPointCandidateKind CandidateKindFromStableString(const std::string& value);

const char* StageStableString(RawAutoStartPointStage stage);
const char* StageLabel(RawAutoStartPointStage stage);
RawAutoStartPointStage StageFromStableString(const std::string& value);

const char* StageStatusStableString(RawAutoStartPointStageStatus status);
const char* ControlStableString(RawAutoStartPointControl control);
const char* ControlLabel(RawAutoStartPointControl control);
const char* DiagnosticSeverityStableString(RawAutoStartPointDiagnosticSeverity severity);

RawAutoStartPointDiagnostics MakeUnavailableDiagnostics(
    std::string sourceKey,
    std::string statusMessage);
RawAutoStartPointDiagnostics BuildDryRunCandidateDiagnostics(
    RawAutoStartPointDiagnostics diagnostics,
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations);
RawAutoStartPointDiagnostics MergeCandidateRenderResults(
    RawAutoStartPointDiagnostics diagnostics,
    const std::vector<RawAutoStartPointCandidateRenderResult>& results,
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations);
RawAutoStartPointFinishToneProposal BuildMildFinishToneProposal(
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const RawAutoStartPointDiagnostics& diagnostics);
RawAutoStartPointConservativePlan BuildConservativeStartingPointPlan(
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations,
    const RawAutoStartPointDiagnostics& diagnostics);
RawAutoStartPointDiagnosticsView BuildDiagnosticsView(
    const RawAutoStartPointDiagnostics& diagnostics);
std::vector<RawAutoStartPointCandidateRenderRequest> CollectCandidateRenderRequests(
    const RawAutoStartPointDiagnostics& diagnostics);
nlohmann::json SerializeDiagnosticsView(
    const RawAutoStartPointDiagnosticsView& view);
nlohmann::json SerializeDiagnostics(
    const RawAutoStartPointDiagnostics& diagnostics);

} // namespace Stack::RawAutoStartPoint
