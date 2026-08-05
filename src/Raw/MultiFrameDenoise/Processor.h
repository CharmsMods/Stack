#pragma once

#include "Raw/MultiFrameDenoise/Streaming.h"
#include "Raw/RawImageData.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Raw::Mfd {

inline constexpr std::uint32_t kProcessorContractVersion = 1;
inline constexpr const char* kProcessorContractId =
    "ra-cfa-offline-processor-v1";

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
};

struct MfdProcessingRequest {
    std::vector<MfdProcessingFrameInput> frames;
    std::uint64_t referenceFrameIndex = 0u;
    Parameters parameters;
    MfdAlignmentMode alignmentMode = MfdAlignmentMode::Full;
    std::filesystem::path workingDirectory;
    std::uint64_t memoryBudgetBytes = 2ull * 1024ull * 1024ull * 1024ull;
    std::uint32_t workerCount = 1u;
    std::function<bool()> shouldCancel;
    MfdProcessingProgressCallback reportProgress;
};

struct MfdProcessingDiagnostics {
    MfdAlignmentMode alignmentMode = MfdAlignmentMode::Full;
    std::uint64_t estimatedPeakResidentBytes = 0u;
    std::uint64_t compatibleAlternateCount = 0u;
    std::uint64_t acceptedAlternateCount = 0u;
    std::uint64_t contributingPixelCount = 0u;
    std::uint64_t exactReferencePixelCount = 0u;
    StreamingFusionDiagnostics streaming;
    std::vector<MfdProcessingFrameDiagnostic> frames;
};

struct MfdProcessingResult {
    std::uint32_t contractVersion = kProcessorContractVersion;
    std::string contractId = kProcessorContractId;
    MfdProcessingStatus status = MfdProcessingStatus::Failed;
    std::string message;
    MfdProcessingDiagnostics diagnostics;
    RawMetadata referenceMetadata;
    CfaPattern outputCfaPattern = CfaPattern::Unknown;
    std::vector<float> referenceNormalizedMosaic;
    PublishedFusionSnapshot published;
};

MfdProcessingServices MakeFilesystemMfdProcessingServices();

MfdProcessingResult ProcessMfdBurst(
    const MfdProcessingRequest& request,
    const MfdProcessingServices& services);

nlohmann::json SerializeMfdProcessingResult(
    const MfdProcessingResult& result);

} // namespace Raw::Mfd
