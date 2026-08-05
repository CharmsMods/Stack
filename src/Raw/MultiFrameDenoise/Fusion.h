#pragma once

#include "Raw/MultiFrameDenoise/Reliability.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Raw::Mfd {

inline constexpr std::uint32_t kFusionContractVersion = 1;
inline constexpr const char* kFusionContractId =
    "ra-cfa-robust-inverse-variance-fusion-v1";

enum class FusionRejectReason : std::uint8_t {
    None = 0,
    CandidateUnavailable,
    HardInvalid,
    NoiseUnavailable,
    InvalidNumericInput,
    ReliabilityZero,
    PixelOutlier,
    AbsoluteSafetyFailure,
    InvalidVariance,
    InvalidWeight,
    ReferenceDefectGateInsufficient,
    ReferenceDefectDisagreement
};

const char* FusionRejectReasonName(FusionRejectReason reason);

struct FusionReferenceSample {
    // normalizedValue is the canonical pre-reference-gain sample. It is the
    // exact value copied by every ordinary fallback branch.
    bool valid = false;
    bool clipped = false;
    bool defective = false;
    NoiseModelQuality noiseQuality = NoiseModelQuality::Unavailable;
    double normalizedValue = 0.0;
    double comparisonGain = 1.0;
    double gateVariance = 0.0;
    double fusionVariance = 0.0;
    double darkVariance = 0.0;
    double effectiveDnStep = 0.0;
};

struct FusionCandidateSample {
    // Candidate vectors are consumed in caller-provided stable project order.
    bool valid = false;
    bool hardValid = false;
    NoiseModelQuality noiseQuality = NoiseModelQuality::Unavailable;
    FusionRejectReason invalidReason = FusionRejectReason::CandidateUnavailable;
    double value = 0.0;
    double gateVariance = 0.0;
    double fusionVariance = 0.0;
    double darkVariance = 0.0;
    double effectiveDnStep = 0.0;
    double reliability = 0.0;

    // The reference-defect path cannot compare against the defective sample.
    // Its upstream, reference-independent confidence must be explicit.
    double referenceDefectGate = 0.0;
};

struct FusionPixelDiagnostics {
    DecisionReason decisionReason = DecisionReason::None;
    FusionRejectReason dominantRejectionReason = FusionRejectReason::None;
    bool exactReferenceCopy = false;
    bool referenceIncluded = true;
    bool referenceDefectReconstructed = false;
    bool referenceDefectRepairDeferred = false;
    bool lowConfidenceTotalCapApplied = false;
    std::uint64_t eligibleAlternateCount = 0u;
    std::uint64_t contributingAlternateCount = 0u;
    std::uint64_t rejectedAlternateCount = 0u;
    std::uint64_t individuallyCappedAlternateCount = 0u;
    double referenceWeight = 0.0;
    double alternateWeight = 0.0;
    double alternateToReferenceWeightRatio = 0.0;
    double effectiveSampleCount = 1.0;
    double outputVarianceComparisonDomain = 0.0;
};

struct FusionPixelResult {
    bool valid = false;
    double normalizedValue = 0.0;
    FusionPixelDiagnostics diagnostics;
};

bool FuseRobustSample(
    const FusionReferenceSample& reference,
    const std::vector<FusionCandidateSample>& candidates,
    const Parameters& parameters,
    FusionPixelResult& result,
    std::string* error = nullptr);

using FusionReferenceProvider = std::function<bool(
    std::uint64_t rawX,
    std::uint64_t rawY,
    FusionReferenceSample& sample)>;

using FusionCandidateProvider = std::function<bool(
    std::size_t stableAlternateIndex,
    std::uint64_t rawX,
    std::uint64_t rawY,
    FusionCandidateSample& sample)>;

struct FusionTileRequest {
    std::uint64_t originRawX = 0u;
    std::uint64_t originRawY = 0u;
    PixelExtent extent;
    std::size_t alternateCount = 0u;
    Parameters parameters;
    FusionReferenceProvider referenceProvider;
    FusionCandidateProvider candidateProvider;
    std::function<bool()> shouldCancel;
    std::uint32_t cancellationCheckRawPixels = 256u;
};

struct FusionTileResult {
    bool valid = false;
    std::string message;
    std::uint64_t originRawX = 0u;
    std::uint64_t originRawY = 0u;
    PixelExtent extent;
    std::vector<float> normalizedMosaic;
    std::vector<FusionPixelDiagnostics> diagnostics;
    std::uint64_t fusedPixelCount = 0u;
    std::uint64_t exactReferencePixelCount = 0u;
    std::uint64_t reconstructedReferenceDefectCount = 0u;
    std::uint64_t deferredReferenceDefectCount = 0u;
};

bool FuseRobustTile(
    const FusionTileRequest& request,
    FusionTileResult& result,
    std::string* error = nullptr);

} // namespace Raw::Mfd
