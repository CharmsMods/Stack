#pragma once

#include "Raw/MultiFrameDenoise/LocalMotion.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Raw::Mfd {

inline constexpr std::uint32_t kReliabilityContractVersion = 1;
inline constexpr const char* kReliabilityContractId =
    "ra-cfa-reliability-v1";
inline constexpr double kStandardNormalMedianAbsoluteValue = 0.67448975;

enum class ReliabilityRejectBit : std::uint16_t {
    None = 0u,
    AlignmentInvalid = 1u << 0u,
    SampleInvalid = 1u << 1u,
    InsufficientPatchSupport = 1u << 2u,
    NoiseUnavailable = 1u << 3u,
    PatchRejected = 1u << 4u,
    NonFiniteEvidence = 1u << 5u
};

constexpr std::uint16_t ReliabilityRejectMask(ReliabilityRejectBit bit) {
    return static_cast<std::uint16_t>(bit);
}

struct ReliabilityResidualSample {
    bool valid = false;
    bool hardValid = false;
    double referenceValue = 0.0;
    double alternateValue = 0.0;
    double referenceGateVariance = 0.0;
    double alternateGateVariance = 0.0;
};

using ReliabilityResidualEvaluator = std::function<bool(
    RawCoordinate referenceRaw,
    CfaSite site,
    const LocalMotionFieldSample& motion,
    ReliabilityResidualSample& sample)>;

struct ReliabilityBuildRequest {
    PixelExtent rawExtent;
    CfaLayout layout;
    const LocalMotionGrid* motionGrid = nullptr;
    LocalMotionOptions motionOptions;
    NoiseModelQuality noiseQuality = NoiseModelQuality::Unavailable;
    Parameters parameters;
    ReliabilityResidualEvaluator residualEvaluator;
    std::function<bool()> shouldCancel;
    std::function<void(double)> reportProgress;
};

struct ReliabilityCell {
    std::uint32_t cellX = 0u;
    std::uint32_t cellY = 0u;
    std::uint16_t rejectionBits = 0u;
    std::uint32_t validResidualCount = 0u;
    double alignmentConfidence = 0.0;
    double patchScale = 0.0;
    double patchGate = 0.0;
    double rawConfidence = 0.0;
    double erodedConfidence = 0.0;
    double reliability = 0.0;
};

struct ReliabilityMap {
    bool valid = false;
    std::string message;
    PixelExtent rawExtent;
    PixelExtent cellExtent;
    NoiseModelQuality noiseQuality = NoiseModelQuality::Unavailable;
    double noiseConfidence = 0.0;
    // Frame usability counts spatially reliable cells before noise-model
    // confidence attenuates their final fusion reliability.
    std::uint64_t usableCellCount = 0u;
    std::uint64_t minimumUsableCellCount = 0u;
    bool frameUsable = false;
    std::vector<ReliabilityCell> cells;
};

double FlatTopQuinticGate(
    double magnitude,
    double fullWeightThreshold,
    double zeroWeightThreshold);

double PatchReliabilityGate(
    double patchScale,
    const ReliabilityParameters& parameters);

double RemapReliabilityConfidence(
    double erodedConfidence,
    const ReliabilityParameters& parameters);

std::uint64_t MinimumUsableReliabilityCells(
    std::uint64_t totalCellCount,
    const ReliabilityParameters& parameters);

bool BuildReliabilityMap(
    const ReliabilityBuildRequest& request,
    ReliabilityMap& result,
    std::string* error = nullptr);

enum class ReliabilityStorageFormat : std::uint8_t {
    Uint16Unorm = 0,
    Float32
};

struct ReliabilityTile {
    std::uint32_t tileX = 0u;
    std::uint32_t tileY = 0u;
    std::uint32_t originCellX = 0u;
    std::uint32_t originCellY = 0u;
    PixelExtent extent;
    std::vector<std::uint16_t> uint16Values;
    std::vector<float> floatValues;
    std::vector<std::uint16_t> rejectionBits;
};

struct ReliabilityStore {
    std::uint32_t contractVersion = kReliabilityContractVersion;
    std::string contractId = kReliabilityContractId;
    ReliabilityStorageFormat format = ReliabilityStorageFormat::Uint16Unorm;
    PixelExtent cellExtent;
    std::uint32_t tileCells = 0u;
    std::uint32_t tilesX = 0u;
    std::uint32_t tilesY = 0u;
    std::vector<ReliabilityTile> tiles;
};

std::uint16_t QuantizeReliabilityUnorm16(double reliability);
double DecodeReliabilityUnorm16(std::uint16_t reliability);

bool BuildReliabilityStore(
    const ReliabilityMap& map,
    ReliabilityStorageFormat format,
    std::uint32_t tileCells,
    ReliabilityStore& store,
    std::string* error = nullptr);

bool ReadReliabilityStoreCell(
    const ReliabilityStore& store,
    std::uint32_t cellX,
    std::uint32_t cellY,
    double& reliability,
    std::uint16_t* rejectionBits = nullptr,
    std::string* error = nullptr);

struct CandidateGateInput {
    bool hardValid = false;
    NoiseModelQuality noiseQuality = NoiseModelQuality::Unavailable;
    double reliability = 0.0;
    double referenceValue = 0.0;
    double alternateValue = 0.0;
    double referenceGateVariance = 0.0;
    double alternateGateVariance = 0.0;
    double referenceDarkVariance = 0.0;
    double alternateDarkVariance = 0.0;
    double referenceDnStep = 0.0;
    double alternateDnStep = 0.0;
};

enum class CandidateGateFailure : std::uint8_t {
    None = 0,
    HardInvalid,
    NoiseUnavailable,
    InvalidNumericInput,
    ReliabilityZero,
    PixelOutlier,
    AbsoluteSafetyFailure
};

const char* CandidateGateFailureName(CandidateGateFailure failure);

struct CandidateGateResult {
    bool valid = false;
    CandidateGateFailure failure = CandidateGateFailure::None;
    double difference = 0.0;
    double standardizedResidual = 0.0;
    double pixelGate = 0.0;
    bool absoluteSafetyApplied = false;
    bool absoluteSafetyPassed = false;
    double absoluteSafetyLimit = 0.0;
    double gate = 0.0;
};

bool EvaluateCandidateGate(
    const CandidateGateInput& input,
    const Parameters& parameters,
    CandidateGateResult& result,
    std::string* error = nullptr);

} // namespace Raw::Mfd
