#pragma once

#include "Raw/MultiFrame/Contracts.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Raw::MultiFrame {

inline constexpr std::uint32_t kFixedGaussianEstimatorVersion = 1;
inline constexpr const char* kFixedGaussianEstimatorId =
    "stack-multiframe-fixed-gaussian-v1";

enum class FixedGaussianWeightSemantics : std::uint8_t {
    BinaryInclusion = 0,
    CalibratedPrecisionMultiplier,
    PolicyAttenuation
};

const char* FixedGaussianWeightSemanticsName(
    FixedGaussianWeightSemantics semantics);

struct FixedGaussianConfiguration {
    std::uint32_t contractVersion = kFixedGaussianEstimatorVersion;
    std::string contractId = kFixedGaussianEstimatorId;
    CfaPhase phase = CfaPhase::R;
    FixedGaussianWeightSemantics weightSemantics =
        FixedGaussianWeightSemantics::BinaryInclusion;
    std::string modelIdentity;
    std::string geometryIdentity;
};

struct FixedGaussianObservation {
    // Prepared sensor-domain observation y = b + e * L + noise.
    double preparedValue = 0.0;
    double blackOffset = 0.0;
    double exposureScale = 1.0;
    // Must be frozen independently of this observation (normally from a
    // shared pilot). Observation-derived variance is an explicit ablation,
    // not the canonical contract.
    double frozenVariance = 1.0;
    double reliability = 1.0;
    SampleEvidence evidence;
    std::uint64_t sourceOrdinal = 0;
};

inline constexpr std::size_t kSampleEvidenceStateCount = 9u;

struct FixedGaussianAccumulator {
    FixedGaussianConfiguration configuration;
    double numerator = 0.0;
    double precision = 0.0;
    double policySamplingVarianceNumerator = 0.0;
    double normalizedWeightSquareSum = 0.0;
    std::uint64_t observationCount = 0;
    std::uint64_t equalitySupportCount = 0;
    std::uint64_t repairedEqualitySupportCount = 0;
    std::uint64_t zeroWeightEqualityCount = 0;
    std::array<std::uint64_t, kSampleEvidenceStateCount> evidenceStateCounts {};
    RadiometricBounds anchorBounds;
    bool boundsContradictory = false;
};

enum class FixedGaussianEstimateState : std::uint8_t {
    Estimate = 0,
    BoundedOnly,
    Unresolved
};

struct FixedGaussianEstimate {
    FixedGaussianEstimateState state = FixedGaussianEstimateState::Unresolved;
    CfaPhase phase = CfaPhase::R;
    FixedGaussianWeightSemantics weightSemantics =
        FixedGaussianWeightSemantics::BinaryInclusion;
    double estimate = 0.0;
    double quadraticVariance = 0.0;
    double conditionalSamplingVariance = 0.0;
    double effectiveSupport = 0.0;
    std::uint64_t equalitySupportCount = 0;
    std::uint64_t repairedEqualitySupportCount = 0;
    RadiometricBounds anchorBounds;
    bool boundsContradictory = false;
    bool estimateOutsideBounds = false;
    bool modelVarianceCalibrated = false;
};

bool InitializeFixedGaussianAccumulator(
    const FixedGaussianConfiguration& configuration,
    FixedGaussianAccumulator& accumulator,
    std::string* error = nullptr);

bool AddFixedGaussianObservation(
    const FixedGaussianObservation& observation,
    FixedGaussianAccumulator& accumulator,
    std::string* error = nullptr);

bool MergeFixedGaussianAccumulator(
    const FixedGaussianAccumulator& source,
    FixedGaussianAccumulator& destination,
    std::string* error = nullptr);

FixedGaussianEstimate FinalizeFixedGaussianAccumulator(
    const FixedGaussianAccumulator& accumulator);

bool EstimateFixedGaussian(
    const FixedGaussianConfiguration& configuration,
    const std::vector<FixedGaussianObservation>& observations,
    FixedGaussianEstimate& estimate,
    FixedGaussianAccumulator* accumulator = nullptr,
    std::string* error = nullptr);

} // namespace Raw::MultiFrame
