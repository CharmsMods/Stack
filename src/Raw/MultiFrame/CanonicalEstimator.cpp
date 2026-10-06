#include "Raw/MultiFrame/CanonicalEstimator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Raw::MultiFrame {
namespace {

bool SetError(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

bool ValidPhase(CfaPhase phase) {
    return phase == CfaPhase::R || phase == CfaPhase::G0 ||
        phase == CfaPhase::G1 || phase == CfaPhase::B;
}

bool ValidWeightSemantics(FixedGaussianWeightSemantics semantics) {
    return semantics == FixedGaussianWeightSemantics::BinaryInclusion ||
        semantics ==
            FixedGaussianWeightSemantics::CalibratedPrecisionMultiplier ||
        semantics == FixedGaussianWeightSemantics::PolicyAttenuation;
}

bool ValidateConfiguration(
    const FixedGaussianConfiguration& configuration,
    std::string* error) {
    if (configuration.contractVersion != kFixedGaussianEstimatorVersion ||
        configuration.contractId != kFixedGaussianEstimatorId) {
        return SetError(error,
            "Fixed-Gaussian estimator contract identity is unsupported.");
    }
    if (!ValidPhase(configuration.phase) ||
        !ValidWeightSemantics(configuration.weightSemantics) ||
        configuration.modelIdentity.empty() ||
        configuration.geometryIdentity.empty()) {
        return SetError(error,
            "Fixed-Gaussian estimator configuration is incomplete.");
    }
    return true;
}

bool EqualConfiguration(
    const FixedGaussianConfiguration& left,
    const FixedGaussianConfiguration& right) {
    return left.contractVersion == right.contractVersion &&
        left.contractId == right.contractId &&
        left.phase == right.phase &&
        left.weightSemantics == right.weightSemantics &&
        left.modelIdentity == right.modelIdentity &&
        left.geometryIdentity == right.geometryIdentity;
}

void IncludeLowerBound(
    double value,
    FixedGaussianAccumulator& accumulator) {
    if (!accumulator.anchorBounds.lowerInclusive ||
        value > *accumulator.anchorBounds.lowerInclusive) {
        accumulator.anchorBounds.lowerInclusive = value;
    }
}

void IncludeUpperBound(
    double value,
    FixedGaussianAccumulator& accumulator) {
    if (!accumulator.anchorBounds.upperInclusive ||
        value < *accumulator.anchorBounds.upperInclusive) {
        accumulator.anchorBounds.upperInclusive = value;
    }
}

void RefreshBoundsState(FixedGaussianAccumulator& accumulator) {
    accumulator.boundsContradictory =
        accumulator.anchorBounds.lowerInclusive &&
        accumulator.anchorBounds.upperInclusive &&
        *accumulator.anchorBounds.lowerInclusive >
            *accumulator.anchorBounds.upperInclusive;
}

std::size_t EvidenceIndex(SampleEvidenceState state) {
    const std::size_t index = static_cast<std::size_t>(state);
    return index < kSampleEvidenceStateCount
        ? index
        : kSampleEvidenceStateCount - 1u;
}

} // namespace

const char* FixedGaussianWeightSemanticsName(
    FixedGaussianWeightSemantics semantics) {
    switch (semantics) {
        case FixedGaussianWeightSemantics::BinaryInclusion:
            return "binary-inclusion";
        case FixedGaussianWeightSemantics::CalibratedPrecisionMultiplier:
            return "calibrated-precision-multiplier";
        case FixedGaussianWeightSemantics::PolicyAttenuation:
            return "policy-attenuation";
        default:
            return "unknown";
    }
}

bool InitializeFixedGaussianAccumulator(
    const FixedGaussianConfiguration& configuration,
    FixedGaussianAccumulator& accumulator,
    std::string* error) {
    if (!ValidateConfiguration(configuration, error)) return false;
    FixedGaussianAccumulator initialized;
    initialized.configuration = configuration;
    accumulator = std::move(initialized);
    if (error) error->clear();
    return true;
}

bool AddFixedGaussianObservation(
    const FixedGaussianObservation& observation,
    FixedGaussianAccumulator& accumulator,
    std::string* error) {
    if (!ValidateConfiguration(accumulator.configuration, error) ||
        !ValidateSampleEvidence(observation.evidence, error)) {
        return false;
    }
    if (!std::isfinite(observation.blackOffset) ||
        !std::isfinite(observation.exposureScale) ||
        observation.exposureScale <= 0.0 ||
        !std::isfinite(observation.reliability) ||
        observation.reliability < 0.0 || observation.reliability > 1.0) {
        return SetError(error,
            "Fixed-Gaussian observation calibration or reliability is invalid.");
    }
    if (accumulator.configuration.weightSemantics ==
            FixedGaussianWeightSemantics::BinaryInclusion &&
        observation.reliability != 0.0 && observation.reliability != 1.0) {
        return SetError(error,
            "Binary fixed-Gaussian reliability must be zero or one.");
    }

    FixedGaussianAccumulator updated = accumulator;
    ++updated.observationCount;
    ++updated.evidenceStateCounts[EvidenceIndex(observation.evidence.state)];

    if (observation.evidence.bounds.lowerInclusive) {
        IncludeLowerBound(
            (*observation.evidence.bounds.lowerInclusive -
                observation.blackOffset) /
                observation.exposureScale,
            updated);
    }
    if (observation.evidence.bounds.upperInclusive) {
        IncludeUpperBound(
            (*observation.evidence.bounds.upperInclusive -
                observation.blackOffset) /
                observation.exposureScale,
            updated);
    }
    RefreshBoundsState(updated);

    if (IsNumericMeasurement(observation.evidence)) {
        if (!std::isfinite(observation.preparedValue) ||
            !std::isfinite(observation.frozenVariance) ||
            observation.frozenVariance <= 0.0) {
            return SetError(error,
                "Fixed-Gaussian numeric observation or frozen variance is invalid.");
        }
        if (observation.reliability == 0.0) {
            ++updated.zeroWeightEqualityCount;
        } else {
            const double centered =
                observation.preparedValue - observation.blackOffset;
            const double q = observation.reliability;
            const double e = observation.exposureScale;
            const double variance = observation.frozenVariance;
            const double numeratorTerm = q * e * centered / variance;
            const double precisionTerm = q * e * e / variance;
            const double policyVarianceTerm = q * q * e * e / variance;
            const double normalizedWeightSquare =
                precisionTerm * precisionTerm;
            const double nextNumerator = updated.numerator + numeratorTerm;
            const double nextPrecision = updated.precision + precisionTerm;
            const double nextPolicyVariance =
                updated.policySamplingVarianceNumerator + policyVarianceTerm;
            const double nextWeightSquare =
                updated.normalizedWeightSquareSum + normalizedWeightSquare;
            if (!std::isfinite(numeratorTerm) ||
                !std::isfinite(precisionTerm) || precisionTerm <= 0.0 ||
                !std::isfinite(policyVarianceTerm) ||
                !std::isfinite(normalizedWeightSquare) ||
                !std::isfinite(nextNumerator) ||
                !std::isfinite(nextPrecision) ||
                !std::isfinite(nextPolicyVariance) ||
                !std::isfinite(nextWeightSquare)) {
                return SetError(error,
                    "Fixed-Gaussian accumulation overflowed its float64 domain.");
            }
            updated.numerator = nextNumerator;
            updated.precision = nextPrecision;
            updated.policySamplingVarianceNumerator = nextPolicyVariance;
            updated.normalizedWeightSquareSum = nextWeightSquare;
            ++updated.equalitySupportCount;
            if (observation.evidence.state ==
                SampleEvidenceState::RepairedMeasurement) {
                ++updated.repairedEqualitySupportCount;
            }
        }
    }

    accumulator = std::move(updated);
    if (error) error->clear();
    return true;
}

bool MergeFixedGaussianAccumulator(
    const FixedGaussianAccumulator& source,
    FixedGaussianAccumulator& destination,
    std::string* error) {
    if (!ValidateConfiguration(source.configuration, error) ||
        !ValidateConfiguration(destination.configuration, error) ||
        !EqualConfiguration(source.configuration, destination.configuration)) {
        return SetError(error,
            "Fixed-Gaussian accumulators have incompatible contracts or models.");
    }

    FixedGaussianAccumulator merged = destination;
    merged.numerator += source.numerator;
    merged.precision += source.precision;
    merged.policySamplingVarianceNumerator +=
        source.policySamplingVarianceNumerator;
    merged.normalizedWeightSquareSum += source.normalizedWeightSquareSum;
    if (!std::isfinite(merged.numerator) || !std::isfinite(merged.precision) ||
        !std::isfinite(merged.policySamplingVarianceNumerator) ||
        !std::isfinite(merged.normalizedWeightSquareSum)) {
        return SetError(error,
            "Fixed-Gaussian accumulator merge overflowed its float64 domain.");
    }
    merged.observationCount += source.observationCount;
    merged.equalitySupportCount += source.equalitySupportCount;
    merged.repairedEqualitySupportCount +=
        source.repairedEqualitySupportCount;
    merged.zeroWeightEqualityCount += source.zeroWeightEqualityCount;
    for (std::size_t index = 0;
         index < merged.evidenceStateCounts.size();
         ++index) {
        merged.evidenceStateCounts[index] += source.evidenceStateCounts[index];
    }
    if (source.anchorBounds.lowerInclusive)
        IncludeLowerBound(*source.anchorBounds.lowerInclusive, merged);
    if (source.anchorBounds.upperInclusive)
        IncludeUpperBound(*source.anchorBounds.upperInclusive, merged);
    RefreshBoundsState(merged);
    destination = std::move(merged);
    if (error) error->clear();
    return true;
}

FixedGaussianEstimate FinalizeFixedGaussianAccumulator(
    const FixedGaussianAccumulator& accumulator) {
    FixedGaussianEstimate result;
    result.phase = accumulator.configuration.phase;
    result.weightSemantics = accumulator.configuration.weightSemantics;
    result.equalitySupportCount = accumulator.equalitySupportCount;
    result.repairedEqualitySupportCount =
        accumulator.repairedEqualitySupportCount;
    result.anchorBounds = accumulator.anchorBounds;
    result.boundsContradictory = accumulator.boundsContradictory;
    if (accumulator.precision <= 0.0 ||
        !std::isfinite(accumulator.precision)) {
        result.state =
            accumulator.anchorBounds.lowerInclusive ||
                accumulator.anchorBounds.upperInclusive
            ? FixedGaussianEstimateState::BoundedOnly
            : FixedGaussianEstimateState::Unresolved;
        return result;
    }

    result.state = FixedGaussianEstimateState::Estimate;
    result.estimate = accumulator.numerator / accumulator.precision;
    result.quadraticVariance = 1.0 / accumulator.precision;
    result.modelVarianceCalibrated =
        accumulator.configuration.weightSemantics !=
            FixedGaussianWeightSemantics::PolicyAttenuation;
    result.conditionalSamplingVariance = result.modelVarianceCalibrated
        ? result.quadraticVariance
        : accumulator.policySamplingVarianceNumerator /
            (accumulator.precision * accumulator.precision);
    result.effectiveSupport = accumulator.normalizedWeightSquareSum > 0.0
        ? accumulator.precision * accumulator.precision /
            accumulator.normalizedWeightSquareSum
        : 0.0;
    if (result.anchorBounds.lowerInclusive &&
        result.estimate < *result.anchorBounds.lowerInclusive) {
        result.estimateOutsideBounds = true;
    }
    if (result.anchorBounds.upperInclusive &&
        result.estimate > *result.anchorBounds.upperInclusive) {
        result.estimateOutsideBounds = true;
    }
    return result;
}

bool EstimateFixedGaussian(
    const FixedGaussianConfiguration& configuration,
    const std::vector<FixedGaussianObservation>& observations,
    FixedGaussianEstimate& estimate,
    FixedGaussianAccumulator* accumulator,
    std::string* error) {
    FixedGaussianAccumulator working;
    if (!InitializeFixedGaussianAccumulator(configuration, working, error))
        return false;
    for (const FixedGaussianObservation& observation : observations) {
        if (!AddFixedGaussianObservation(observation, working, error))
            return false;
    }
    FixedGaussianEstimate finalized =
        FinalizeFixedGaussianAccumulator(working);
    estimate = finalized;
    if (accumulator) *accumulator = std::move(working);
    if (error) error->clear();
    return true;
}

} // namespace Raw::MultiFrame
