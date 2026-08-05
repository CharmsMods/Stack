#pragma once

#include "Raw/MultiFrameDenoise/Streaming.h"

#include "ThirdParty/json.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Raw::Mfd {

inline constexpr std::uint32_t kEvaluationContractVersion = 1;
inline constexpr const char* kEvaluationContractId =
    "ra-cfa-v1-corpus-evaluation-v1";
inline constexpr const char* kReleaseGateContractId =
    "ra-cfa-v1-release-gates-v1";

struct CfaErrorMetrics {
    CfaSite site = CfaSite::Red;
    std::uint64_t sampleCount = 0u;
    double meanBias = 0.0;
    double rmse = 0.0;
};

struct RawFidelityMetrics {
    bool available = false;
    std::uint64_t sampleCount = 0u;
    double meanBias = 0.0;
    double meanAbsoluteError = 0.0;
    double rmse = 0.0;
    double psnr = 0.0;
    double referenceRmse = 0.0;
    double rmseImprovementRatio = 0.0;
    double flatResidualStandardDeviation = 0.0;
    double referenceFlatResidualStandardDeviation = 0.0;
    double measuredFlatNoiseReductionRatio = 0.0;
    double expectedFlatStandardDeviation = 0.0;
    double observedToExpectedNoiseRatio = 0.0;
    double meanEffectiveSampleCount = 0.0;
    double idealIndependentNoiseReductionRatio = 0.0;
    double measuredToIdealNoiseReductionRatio = 0.0;
    double horizontalSameCfaResidualCorrelation = 0.0;
    double verticalSameCfaResidualCorrelation = 0.0;
    std::vector<CfaErrorMetrics> byCfaSite;
};

struct DetailRetentionMetrics {
    bool available = false;
    std::uint64_t evaluatedPixelCount = 0u;
    double gradientMagnitudeRetention = 0.0;
    double highFrequencyContrastRetention = 0.0;
    double maximumAbsoluteError = 0.0;
};

struct MotionMaskMetrics {
    bool available = false;
    std::uint64_t movingPixelCount = 0u;
    std::uint64_t staticPixelCount = 0u;
    double staticAcceptancePrecision = 0.0;
    double staticAcceptanceRecall = 0.0;
    double movingFalseMergeRate = 0.0;
    double movingRegionRmseToReferenceMoment = 0.0;
    double temporalStateLeakageFraction = 0.0;
};

struct MotionCalibrationSample {
    double errorRawX = 0.0;
    double errorRawY = 0.0;
    SymmetricRawCovariance predictedCovariance;
    double confidence = 0.0;
};

struct CovarianceCalibrationMetrics {
    bool available = false;
    std::uint64_t sampleCount = 0u;
    double coverage68 = 0.0;
    double coverage95 = 0.0;
    double coverage99 = 0.0;
    double coverage68AbsoluteError = 0.0;
    double coverage95AbsoluteError = 0.0;
    double coverage99AbsoluteError = 0.0;
    double medianEndpointErrorRawPixels = 0.0;
    double percentile95EndpointErrorRawPixels = 0.0;
    double highConfidenceCatastrophicErrorRate = 0.0;
    double confidenceExpectedCalibrationError = 0.0;
};

struct PerformanceStageTrace {
    std::string stage;
    double wallTimeMs = 0.0;
};

struct PerformanceMemoryTrace {
    bool available = false;
    std::vector<PerformanceStageTrace> stages;
    double totalWallTimeMs = 0.0;
    std::uint64_t peakResidentBytes = 0u;
    std::uint64_t peakWorkingBytes = 0u;
    std::uint64_t cacheBytesRead = 0u;
    std::uint64_t cacheBytesWritten = 0u;
    double cancellationLatencyMs = 0.0;
    double coldCacheWallTimeMs = 0.0;
    double warmCacheWallTimeMs = 0.0;
};

struct MfdEvaluationInput {
    std::string sampleId;
    CfaPattern cfaPattern = CfaPattern::Unknown;
    PixelExtent extent;
    std::vector<float> noiseFreeReferenceMosaic;
    std::vector<float> noisyReferenceMosaic;
    std::vector<float> outputMosaic;
    std::vector<float> alternateMomentMosaic;
    std::vector<std::uint8_t> flatRegionMask;
    std::vector<std::uint8_t> detailRegionMask;
    std::vector<std::uint8_t> movingRegionMask;
    std::vector<float> reliabilityMap;
    std::vector<float> motionConfidenceMap;
    std::vector<FusionPixelDiagnostics> fusionDiagnostics;
    std::vector<MotionCalibrationSample> motionCalibrationSamples;
    double expectedOutputNoiseStandardDeviation = 0.0;
    PerformanceMemoryTrace performance;
};

struct MfdEvaluationResult {
    bool valid = false;
    std::string message;
    std::string sampleId;
    PixelExtent extent;
    RawFidelityMetrics raw;
    DetailRetentionMetrics detail;
    MotionMaskMetrics motion;
    CovarianceCalibrationMetrics calibration;
    PerformanceMemoryTrace performance;
};

bool EvaluateMfdOutput(
    const MfdEvaluationInput& input,
    MfdEvaluationResult& result,
    std::string* error = nullptr);

nlohmann::json SerializeMfdEvaluationResult(
    const MfdEvaluationResult& result);

bool WriteMfdVisualDiagnostics(
    const MfdEvaluationInput& input,
    const MfdEvaluationResult& result,
    const std::filesystem::path& outputDirectory,
    std::vector<std::filesystem::path>& writtenFiles,
    std::string* error = nullptr);

struct MfdAblationVariant {
    std::string name;
    MfdEvaluationInput evaluation;
};

struct MfdAblationReport {
    bool valid = false;
    std::string message;
    std::vector<std::string> stableVariantOrder;
    std::vector<MfdEvaluationResult> results;
};

bool BuildMfdAblationReport(
    const std::vector<MfdAblationVariant>& variants,
    MfdAblationReport& report,
    std::string* error = nullptr);

nlohmann::json SerializeMfdAblationReport(const MfdAblationReport& report);

struct MfdParameterFreeze {
    bool valid = false;
    std::string parameterSetId;
    std::string parameterSha256;
    std::string corpusManifestSha256;
    std::uint32_t parameterSchemaVersion = 0u;
    nlohmann::json parameters;
};

bool FreezeMfdParameterCandidate(
    const Parameters& parameters,
    const std::string& parameterSetId,
    const std::string& corpusManifestSha256,
    MfdParameterFreeze& freeze,
    std::string* error = nullptr);

enum class MfdCorpusLayer : std::uint8_t {
    Synthetic = 0,
    ControlledCamera,
    UncontrolledReal
};

const char* MfdCorpusLayerName(MfdCorpusLayer layer);

struct MfdCorpusEntryEvidence {
    std::string sampleId;
    std::string sourceContentSetSha256;
    MfdCorpusLayer layer = MfdCorpusLayer::Synthetic;
    std::string cameraModeId;
    std::uint32_t frameCount = 0u;
    bool hasReferenceMomentGroundTruth = false;
    bool metricsComplete = false;
    bool visualReviewComplete = false;
};

struct MfdCorpusEvidence {
    std::string manifestSha256;
    bool manifestLocked = false;
    bool fullFrozenCorpusRerunComplete = false;
    bool performanceTracesComplete = false;
    bool noHandSelectedExceptions = false;
    std::vector<MfdCorpusEntryEvidence> entries;
};

bool ComputeMfdCorpusManifestSha256(
    const std::vector<MfdCorpusEntryEvidence>& entries,
    std::string& manifestSha256,
    std::string* error = nullptr);

nlohmann::json SerializeMfdCorpusManifest(
    const std::vector<MfdCorpusEntryEvidence>& entries);

struct MfdReleaseThresholds {
    std::uint64_t minimumSyntheticEntries = 12u;
    std::uint64_t minimumControlledEntries = 5u;
    std::uint64_t minimumUncontrolledEntries = 10u;
    std::uint64_t minimumDistinctCameraModes = 2u;
    double minimumRmseImprovementRatio = 1.05;
    double maximumAbsoluteBias = 1.0e-3;
    double minimumGradientRetention = 0.95;
    double maximumMovingFalseMergeRate = 0.01;
    double maximumCoverage68AbsoluteError = 0.10;
    double maximumCoverage95AbsoluteError = 0.05;
    double maximumCoverage99AbsoluteError = 0.03;
    double maximumHighConfidenceCatastrophicErrorRate = 0.001;
};

struct MfdReleaseGateResult {
    std::string id;
    bool passed = false;
    std::string message;
};

struct MfdReleaseGateReport {
    bool valid = false;
    bool productionReady = false;
    std::string message;
    std::vector<MfdReleaseGateResult> gates;
};

bool EvaluateMfdReleaseGates(
    const MfdParameterFreeze& freeze,
    const MfdCorpusEvidence& corpus,
    const std::vector<MfdEvaluationResult>& evaluations,
    const MfdReleaseThresholds& thresholds,
    MfdReleaseGateReport& report,
    std::string* error = nullptr);

nlohmann::json SerializeMfdReleaseGateReport(
    const MfdReleaseGateReport& report);

} // namespace Raw::Mfd
