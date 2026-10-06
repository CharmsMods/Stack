#pragma once

#include "Raw/MultiFrame/CanonicalEstimator.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Raw::MultiFrame {

inline constexpr std::uint32_t kSharedPilotHuberEstimatorVersion = 1;
inline constexpr const char* kSharedPilotHuberEstimatorId =
    "stack-multiframe-shared-pilot-huber-v1";

struct SharedPilotHuberConfiguration {
    std::uint32_t contractVersion = kSharedPilotHuberEstimatorVersion;
    std::string contractId = kSharedPilotHuberEstimatorId;
    CfaPhase phase = CfaPhase::R;
    FixedGaussianWeightSemantics baseWeightSemantics =
        FixedGaussianWeightSemantics::BinaryInclusion;
    std::string modelIdentity;
    std::string geometryIdentity;
    std::string pilotIdentity;
    double initialPilot = 0.0;
    double huberThreshold = 1.345;
    std::uint32_t maximumIterations = 12;
    double absoluteTolerance = 1.0e-12;
    double relativeTolerance = 1.0e-10;
};

struct SharedPilotHuberObservationDiagnostic {
    std::uint64_t sourceOrdinal = 0;
    bool hasNumericResidual = false;
    double standardizedResidual = 0.0;
    double robustFactor = 0.0;
    double resolvedReliability = 0.0;
};

struct SharedPilotHuberEstimate {
    FixedGaussianEstimateState state =
        FixedGaussianEstimateState::Unresolved;
    CfaPhase phase = CfaPhase::R;
    FixedGaussianWeightSemantics baseWeightSemantics =
        FixedGaussianWeightSemantics::BinaryInclusion;
    double initialPilot = 0.0;
    double estimate = 0.0;
    // Variance implied by the frozen noise model and final robust precision.
    double conditionalQuadraticVariance = 0.0;
    // Sampling variance after policy reliability and robust attenuation.
    double policyConditionalSamplingVariance = 0.0;
    bool policyConditionalSamplingVarianceAvailable = false;
    double robustLinearizedVariance = 0.0;
    bool robustLinearizedVarianceAvailable = false;
    double finiteSampleCorrectedRobustVariance = 0.0;
    bool finiteSampleCorrectedRobustVarianceAvailable = false;
    // The value published downstream. It is the maximum valid variance above.
    double publishedConservativeVariance = 0.0;
    bool publishedConservativeVarianceAvailable = false;
    // This remains false until corpus coverage calibration proves otherwise.
    bool robustVarianceEmpiricallyCalibrated = false;
    double effectiveSupport = 0.0;
    double finalUpdateMagnitude = 0.0;
    std::uint32_t iterations = 0;
    bool converged = false;
    std::uint64_t equalitySupportCount = 0;
    std::uint64_t repairedEqualitySupportCount = 0;
    std::array<std::uint64_t, kSampleEvidenceStateCount>
        evidenceStateCounts {};
    RadiometricBounds anchorBounds;
    bool boundsContradictory = false;
    bool estimateOutsideBounds = false;
    std::vector<SharedPilotHuberObservationDiagnostic>
        observationDiagnostics;
};

bool EstimateSharedPilotHuber(
    const SharedPilotHuberConfiguration& configuration,
    const std::vector<FixedGaussianObservation>& observations,
    SharedPilotHuberEstimate& estimate,
    std::string* error = nullptr);

} // namespace Raw::MultiFrame
