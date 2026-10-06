#pragma once

#include "Raw/MultiFrameDenoise/Streaming.h"
#include "Raw/MultiFrameDenoise/SharedBurst.h"
#include "Raw/OpenGlTask.h"
#include "Raw/RawImageData.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Raw::Mfd {

inline constexpr std::uint32_t kProcessorContractVersion = 5;
inline constexpr const char* kProcessorContractId =
    "shared-burst-offline-processor-v5";

enum class MfdFusionBackend : std::uint8_t {
    SharedBurstV1 = 0,
    LegacyRaCfaV1
};

enum class MfdProcessingStatus : std::uint8_t {
    DenoisedCandidate = 0,
    ReferenceOnly,
    Canceled,
    Failed
};

const char* MfdProcessingStatusName(MfdProcessingStatus status);

enum class MfdAlignmentMode : std::uint8_t {
    Full = 0,
    TranslationOnly,
    Identity
};

const char* MfdAlignmentModeName(MfdAlignmentMode mode);
const char* MfdAlignmentModeId(MfdAlignmentMode mode);
bool ParseMfdAlignmentMode(
    const std::string& value,
    MfdAlignmentMode& mode);

enum class MfdProcessingStage : std::uint8_t {
    Queued = 0,
    MaterializingSources,
    PreparingReference,
    BuildingReferencePyramid,
    PreparingAlternate,
    RegisteringAlternate,
    FusingTiles,
    WritingInspection,
    Finalizing
};

const char* MfdProcessingStageName(MfdProcessingStage stage);

struct MfdProcessingProgress {
    MfdProcessingStage stage = MfdProcessingStage::Queued;
    double overallFraction = 0.0;
    double stageFraction = 0.0;
    std::uint64_t completedUnits = 0u;
    std::uint64_t totalUnits = 0u;
    std::uint32_t frameOrdinal = 0u;
    std::uint32_t frameCount = 0u;
    std::string message;
};

using MfdProcessingProgressCallback = std::function<void(
    const MfdProcessingProgress& progress)>;

struct MfdProcessingFrameInput {
    std::string stableFrameId;
    std::filesystem::path sourcePath;
    std::string expectedSourceSha256;
    std::uint64_t expectedSourceByteLength = 0u;
    PixelExtent expectedVisibleExtent;
    RawMosaicDenoiseSettings sharedCfaDenoise;
    std::string preprocessingIdentitySha256;
    double trustAttenuation = 1.0;
};

struct MfdProcessingFrameDiagnostic {
    std::string stableFrameId;
    bool referenceFrame = false;
    bool attempted = false;
    bool prepared = false;
    bool acceptedForFusion = false;
    DecisionReason decisionReason = DecisionReason::None;
    std::string message;
    NoiseModelQuality noiseQuality = NoiseModelQuality::Unavailable;
    GlobalFrameAlignmentChoice globalAlignment =
        GlobalFrameAlignmentChoice::Reject;
    bool globalRegistrationPerformed = false;
    bool localRegistrationPerformed = false;
    RawCoordinate globalTranslationRaw;
    double exposureScale = 1.0;
    double exposureDriftEv = 0.0;
    bool exposureGrouped = false;
    std::uint64_t structuredMotionNodes = 0u;
    std::uint64_t flatSafeMotionNodes = 0u;
    std::uint64_t rejectedMotionNodes = 0u;
    std::uint64_t usableReliabilityCells = 0u;
};

using MfdRawFrameLoader = std::function<bool(
    const std::filesystem::path& sourcePath,
    RawImageData& frame,
    const std::function<bool()>& shouldCancel,
    std::string& error)>;

struct MfdProcessingServices {
    MfdRawFrameLoader loadRawFrame;
    Raw::OpenGlTaskExecutor executeOpenGlTask;
};

struct MfdProcessingRequest {
    std::vector<MfdProcessingFrameInput> frames;
    std::uint64_t referenceFrameIndex = 0u;
    Parameters parameters;
    SharedBurstSettings sharedBurstSettings;
    MfdFusionBackend fusionBackend = MfdFusionBackend::SharedBurstV1;
    MfdAlignmentMode alignmentMode = MfdAlignmentMode::Full;
    std::filesystem::path workingDirectory;
    std::uint64_t memoryBudgetBytes = 2ull * 1024ull * 1024ull * 1024ull;
    // Strict validation and explicitly sandboxed callers may enforce the
    // estimate. Interactive Stack processing treats it as advisory and relies
    // on staged allocation failure, cancellation, and atomic publication.
    bool enforceMemoryBudget = true;
    bool preferGpuRegistration = true;
    bool preferGpuFusion = true;
    std::uint32_t workerCount = 1u;
    std::function<bool()> shouldCancel;
    MfdProcessingProgressCallback reportProgress;
};

struct MfdProcessingDiagnostics {
    MfdAlignmentMode alignmentMode = MfdAlignmentMode::Full;
    std::string executionBackend = "cpu-reference";
    std::string gpuDeviceIdentity;
    std::string gpuFallbackReason;
    std::uint32_t gpuDispatchedTileCount = 0u;
    std::string registrationBackend = "cpu-reference";
    std::string registrationGpuDeviceIdentity;
    std::string registrationGpuFallbackReason;
    std::uint32_t registrationGpuDispatchCount = 0u;
    std::uint64_t registrationGpuScoredCandidateCount = 0u;
    std::uint64_t estimatedPeakResidentBytes = 0u;
    std::uint64_t compatibleAlternateCount = 0u;
    std::uint64_t exposureGroupedCaptureCount = 0u;
    std::uint64_t exposureExcludedCaptureCount = 0u;
    std::uint64_t acceptedAlternateCount = 0u;
    std::uint64_t contributingPixelCount = 0u;
    std::uint64_t exactReferencePixelCount = 0u;
    double meanEffectiveCaptureCount = 1.0;
    double meanRobustAttenuation = 1.0;
    double predictedIndependentNoiseReduction = 1.0;
    bool independentNoiseReductionClaimQualified = true;
    StreamingFusionDiagnostics streaming;
    std::vector<MfdProcessingFrameDiagnostic> frames;
};

struct MfdProcessingResult {
    std::uint32_t contractVersion = kProcessorContractVersion;
    std::string contractId = kProcessorContractId;
    MfdProcessingStatus status = MfdProcessingStatus::Failed;
    MfdFusionBackend fusionBackend = MfdFusionBackend::SharedBurstV1;
    std::string message;
    MfdProcessingDiagnostics diagnostics;
    RawMetadata referenceMetadata;
    CfaPattern outputCfaPattern = CfaPattern::Unknown;
    std::vector<float> referenceNormalizedMosaic;
    // Processor-neutral virtual-measurement evidence. Variance is converted
    // back from the comparison domain into the published pre-gain mosaic
    // domain so a downstream MultiFrame node can propagate it honestly.
    std::vector<float> varianceProxy;
    std::vector<float> effectiveSupport;
    std::vector<std::uint8_t> validityMask;
    std::vector<std::uint8_t> clippingMask;
    PublishedFusionSnapshot published;
};

MfdProcessingServices MakeFilesystemMfdProcessingServices();

std::uint64_t EstimateMfdPeakResidentBytes(
    PixelExtent extent,
    std::size_t frameCount,
    const Parameters& parameters,
    std::uint32_t workerCount,
    MfdAlignmentMode alignmentMode);

MfdProcessingResult ProcessMfdBurst(
    const MfdProcessingRequest& request,
    const MfdProcessingServices& services);

nlohmann::json SerializeMfdProcessingResult(
    const MfdProcessingResult& result);

} // namespace Raw::Mfd
