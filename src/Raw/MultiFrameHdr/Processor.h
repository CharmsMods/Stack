#pragma once

#include "Raw/MultiFrameHdr/Contracts.h"
#include "Raw/MultiFrameHdr/FusionControls.h"
#include "Raw/OpenGlTask.h"
#include "Raw/RawImageData.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Raw::Hdr {

class PreparedFusion;

inline constexpr std::uint32_t kProcessorContractVersion = 2;
inline constexpr const char* kProcessorContractId = "tripod-cfa-hdr-processor-v2";

enum class ProcessingStatus : std::uint8_t {
    Published = 0,
    Canceled,
    Failed
};

enum class ProcessingStage : std::uint8_t {
    Queued = 0,
    Decoding,
    Preparing,
    Registering,
    Calibrating,
    Fusing,
    Caching,
    Finalizing
};

const char* ProcessingStatusName(ProcessingStatus status);
const char* ProcessingStageName(ProcessingStage stage);

struct Progress {
    ProcessingStage stage = ProcessingStage::Queued;
    double overallFraction = 0.0;
    double stageFraction = 0.0;
    std::uint32_t frameOrdinal = 0;
    std::uint32_t frameCount = 0;
    std::string message;
};

struct FrameInput {
    std::string stableFrameId;
    std::filesystem::path sourcePath;
    std::string expectedSourceSha256;
    std::uint64_t expectedSourceByteLength = 0;
};

using RawFrameLoader = std::function<bool(
    const std::filesystem::path&,
    RawImageData&,
    const std::function<bool()>&,
    std::string&)>;

using OpenGlTask = Raw::OpenGlTask;
using OpenGlTaskExecutor = Raw::OpenGlTaskExecutor;

struct Services {
    RawFrameLoader loadRawFrame;
    // Optional host-owned OpenGL 4.3 context executor. Absence or failure
    // selects the normative CPU implementation automatically.
    OpenGlTaskExecutor executeOpenGlTask;
};

struct Request {
    std::vector<FrameInput> frames;
    std::int64_t geometricReferenceFrameIndex = -1;
    std::int64_t radiometricAnchorFrameIndex = -1;
    Parameters parameters;
    FusionControls fusion;
    bool interactiveFusion = false;
    std::shared_ptr<const PreparedFusion> preparedFusion;
    std::filesystem::path workingDirectory;
    std::uint64_t inputRevision = 0;
    std::uint64_t memoryBudgetBytes = 2ull * 1024ull * 1024ull * 1024ull;
    std::uint32_t workerCount = 1u;
    bool enforceMemoryBudget = true;
    bool preferGpuFusion = true;
    std::function<bool()> shouldCancel;
    std::function<void(const Progress&)> reportProgress;
};

struct FrameDiagnostic {
    std::string stableFrameId;
    bool geometricReference = false;
    bool radiometricAnchor = false;
    bool structurallyUsable = false;
    bool exposureVerified = false;
    bool lowConfidence = false;
    double metadataExposureRelativeToAnchor = 1.0;
    double fittedExposureRelativeToAnchor = 1.0;
    double fittedExposureUncertaintyEv = 0.0;
    double metadataDisagreementEv = 0.0;
    double translationRawX = 0.0;
    double translationRawY = 0.0;
    double aggregateContribution = 0.0;
    double shadowContribution = 0.0;
    double midtoneContribution = 0.0;
    double highlightContribution = 0.0;
    std::vector<std::string> exposureFitPath;
    std::string noiseModelSource;
    std::string noiseModelQuality;
    double noiseVarianceInflation = 1.0;
    std::string message;
};

struct ExposureFitEdgeDiagnostic {
    std::string fromFrameId;
    std::string toFrameId;
    bool verified = false;
    double fittedScale = 1.0;
    double fittedOffset = 0.0;
    double uncertaintyEv = 0.0;
    double residualNoiseInflation = 1.0;
    std::uint64_t sampleCount = 0u;
    std::array<std::uint64_t, 4> siteSampleCounts {};
};

enum ResultFlag : std::uint8_t {
    ResultFlagNone = 0,
    ResultFlagReferenceFallback = 1u << 0u,
    ResultFlagLowConfidenceOwner = 1u << 1u,
    ResultFlagRecoveredHighlight = 1u << 2u,
    ResultFlagSingleExposure = 1u << 3u,
    ResultFlagHighlightSafeHandoff = 1u << 4u,
    ResultFlagColorCoherentRepair = 1u << 5u,
    ResultFlagNoValidMeasurement = 1u << 6u
};

struct Diagnostics {
    std::string cacheKey;
    bool reusedPreparation = false;
    std::string executionBackend = "cpu-reference";
    std::string gpuDeviceIdentity;
    std::string gpuFallbackReason;
    std::uint32_t gpuDispatchedTileCount = 0;
    std::uint64_t inputRevision = 0;
    std::uint64_t finitePixelCount = 0;
    std::uint64_t referenceFallbackPixelCount = 0;
    std::uint64_t highlightSafeHandoffPixelCount = 0;
    std::uint64_t colorCoherentRepairPixelCount = 0;
    std::uint64_t recoveredHighlightPixelCount = 0;
    double exposureSpanEv = 0.0;
    double meanEffectiveSamples = 0.0;
    std::vector<FrameDiagnostic> frames;
    std::vector<ExposureFitEdgeDiagnostic> exposureFitEdges;
    std::vector<std::string> warnings;
};

struct Result {
    // Session-only caches. Serialized projects retain source IDs and controls.
    std::shared_ptr<const PreparedFusion> preparedFusion;
    std::shared_ptr<const FusionPreview> fusionPreview;
    std::uint32_t contractVersion = kProcessorContractVersion;
    std::string contractId = kProcessorContractId;
    ProcessingStatus status = ProcessingStatus::Failed;
    std::string message;
    RawMetadata referenceMetadata;
    // The virtual anchor mosaic is radiometrically expressed in this
    // capture's exposure domain. Downstream MultiFrame nodes must use it for
    // exposure reasoning even when a different frame owned geometry.
    RawMetadata radiometricAnchorMetadata;
    CfaPattern outputCfaPattern = CfaPattern::Unknown;
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    std::string geometricReferenceFrameId;
    std::string radiometricAnchorFrameId;
    std::vector<float> virtualAnchorMosaic;
    std::vector<float> varianceProxy;
    std::vector<float> mergeConfidence;
    std::vector<float> effectiveSampleCount;
    std::vector<float> recoveredHeadroomStops;
    std::vector<std::uint8_t> validityMask;
    std::vector<std::uint8_t> ownerFrame;
    std::vector<std::uint8_t> flags;
    Diagnostics diagnostics;
};

Services MakeFilesystemServices();
Result ProcessBurst(const Request& request, const Services& services);
nlohmann::json SerializeDiagnostics(const Result& result);

// Deterministic content-addressed result persistence. A failed verification
// never returns a partially decoded result.
bool WriteResultCache(
    const std::filesystem::path& directory,
    const Result& result,
    std::string* error = nullptr);
bool ReadResultCache(
    const std::filesystem::path& directory,
    const std::string& cacheKey,
    Result& result,
    std::string* error = nullptr);

} // namespace Raw::Hdr
