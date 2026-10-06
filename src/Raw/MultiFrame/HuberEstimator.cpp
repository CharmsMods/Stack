#include "Raw/MultiFrame/HuberEstimator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
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
    const SharedPilotHuberConfiguration& configuration,
    std::string* error) {
    if (configuration.contractVersion !=
            kSharedPilotHuberEstimatorVersion ||
        configuration.contractId != kSharedPilotHuberEstimatorId) {
        return SetError(error,
            "Shared-pilot Huber estimator contract identity is unsupported.");
    }
    if (!ValidPhase(configuration.phase) ||
        !ValidWeightSemantics(configuration.baseWeightSemantics) ||
        configuration.modelIdentity.empty() ||
        configuration.geometryIdentity.empty() ||
        configuration.pilotIdentity.empty()) {
        return SetError(error,
            "Shared-pilot Huber estimator configuration is incomplete.");
    }
    if (!std::isfinite(configuration.initialPilot) ||
        !std::isfinite(configuration.huberThreshold) ||
        configuration.huberThreshold <= 0.0 ||
        configuration.maximumIterations == 0u ||
        !std::isfinite(configuration.absoluteTolerance) ||
        configuration.absoluteTolerance < 0.0 ||
        !std::isfinite(configuration.relativeTolerance) ||
        configuration.relativeTolerance < 0.0) {
        return SetError(error,
            "Shared-pilot Huber iteration settings are invalid.");
    }
    return true;
}

std::size_t EvidenceIndex(SampleEvidenceState state) {
    const std::size_t index = static_cast<std::size_t>(state);
    return index < kSampleEvidenceStateCount
        ? index
        : kSampleEvidenceStateCount - 1u;
}

void IncludeLowerBound(double value, SharedPilotHuberEstimate& estimate) {
    if (!estimate.anchorBounds.lowerInclusive ||
        value > *estimate.anchorBounds.lowerInclusive) {
        estimate.anchorBounds.lowerInclusive = value;
    }
}

void IncludeUpperBound(double value, SharedPilotHuberEstimate& estimate) {
    if (!estimate.anchorBounds.upperInclusive ||
        value < *estimate.anchorBounds.upperInclusive) {
        estimate.anchorBounds.upperInclusive = value;
    }
}

double HuberFactor(double standardizedResidual, double threshold) {
    const double magnitude = std::abs(standardizedResidual);
    return magnitude <= threshold || magnitude == 0.0
        ? 1.0
        : threshold / magnitude;
}

double HuberPsi(double standardizedResidual, double threshold) {
    return std::max(-threshold,
        std::min(threshold, standardizedResidual));
}

struct FrozenPass {
    double numerator = 0.0;
    double precision = 0.0;
    double precisionSquareSum = 0.0;
    double policySamplingNumerator = 0.0;
};

} // namespace

bool EstimateSharedPilotHuber(
    const SharedPilotHuberConfiguration& configuration,
    const std::vector<FixedGaussianObservation>& observations,
    SharedPilotHuberEstimate& estimate,
    std::string* error) {
    if (!ValidateConfiguration(configuration, error)) return false;

    SharedPilotHuberEstimate working;
    working.phase = configuration.phase;
    working.baseWeightSemantics = configuration.baseWeightSemantics;
    working.initialPilot = configuration.initialPilot;
    working.observationDiagnostics.resize(observations.size());

    std::vector<std::size_t> order(observations.size());
    std::iota(order.begin(), order.end(), std::size_t { 0 });
    std::stable_sort(order.begin(), order.end(),
        [&observations](std::size_t left, std::size_t right) {
            return observations[left].sourceOrdinal <
                observations[right].sourceOrdinal;
        });

    for (std::size_t index = 0; index < observations.size(); ++index) {
        const FixedGaussianObservation& observation = observations[index];
        if (!ValidateSampleEvidence(observation.evidence, error)) return false;
        if (!std::isfinite(observation.blackOffset) ||
            !std::isfinite(observation.exposureScale) ||
            observation.exposureScale <= 0.0 ||
            !std::isfinite(observation.reliability) ||
            observation.reliability < 0.0 ||
            observation.reliability > 1.0) {
            return SetError(error,
                "Shared-pilot Huber observation calibration or reliability is invalid.");
        }
        if (configuration.baseWeightSemantics ==
                FixedGaussianWeightSemantics::BinaryInclusion &&
            observation.reliability != 0.0 &&
            observation.reliability != 1.0) {
            return SetError(error,
                "Binary shared-pilot Huber reliability must be zero or one.");
        }
        if (IsNumericMeasurement(observation.evidence) &&
            (!std::isfinite(observation.preparedValue) ||
             !std::isfinite(observation.frozenVariance) ||
             observation.frozenVariance <= 0.0)) {
            return SetError(error,
                "Shared-pilot Huber numeric observation or frozen variance is invalid.");
        }
        ++working.evidenceStateCounts[EvidenceIndex(
            observation.evidence.state)];
        working.observationDiagnostics[index].sourceOrdinal =
            observation.sourceOrdinal;

        if (observation.evidence.bounds.lowerInclusive) {
            const double lower =
                (*observation.evidence.bounds.lowerInclusive -
                    observation.blackOffset) /
                observation.exposureScale;
            if (!std::isfinite(lower)) {
                return SetError(error,
                    "Shared-pilot Huber lower bound overflowed the anchor domain.");
            }
            IncludeLowerBound(lower, working);
        }
        if (observation.evidence.bounds.upperInclusive) {
            const double upper =
                (*observation.evidence.bounds.upperInclusive -
                    observation.blackOffset) /
                observation.exposureScale;
            if (!std::isfinite(upper)) {
                return SetError(error,
                    "Shared-pilot Huber upper bound overflowed the anchor domain.");
            }
            IncludeUpperBound(upper, working);
        }
    }
    working.boundsContradictory =
        working.anchorBounds.lowerInclusive &&
        working.anchorBounds.upperInclusive &&
        *working.anchorBounds.lowerInclusive >
            *working.anchorBounds.upperInclusive;

    auto evaluatePass = [&](double pilot,
                            FrozenPass& pass,
                            bool recordDiagnostics) -> bool {
        FrozenPass next;
        for (const std::size_t index : order) {
            const FixedGaussianObservation& observation = observations[index];
            if (!IsNumericMeasurement(observation.evidence) ||
                observation.reliability == 0.0) {
                continue;
            }
            const double centered =
                observation.preparedValue - observation.blackOffset;
            const double sigma = std::sqrt(observation.frozenVariance);
            const double residual =
                (centered - observation.exposureScale * pilot) / sigma;
            if (!std::isfinite(residual)) {
                return SetError(error,
                    "Shared-pilot Huber residual overflowed its float64 domain.");
            }
            const double robustFactor =
                HuberFactor(residual, configuration.huberThreshold);
            const double resolvedReliability =
                observation.reliability * robustFactor;
            const double precisionTerm = resolvedReliability *
                observation.exposureScale * observation.exposureScale /
                observation.frozenVariance;
            const double numeratorTerm = resolvedReliability *
                observation.exposureScale * centered /
                observation.frozenVariance;
            const double precisionSquare = precisionTerm * precisionTerm;
            const double samplingTerm = resolvedReliability *
                resolvedReliability * observation.exposureScale *
                observation.exposureScale / observation.frozenVariance;
            next.precision += precisionTerm;
            next.numerator += numeratorTerm;
            next.precisionSquareSum += precisionSquare;
            next.policySamplingNumerator += samplingTerm;
            if (!std::isfinite(next.precision) ||
                !std::isfinite(next.numerator) ||
                !std::isfinite(next.precisionSquareSum) ||
                !std::isfinite(next.policySamplingNumerator)) {
                return SetError(error,
                    "Shared-pilot Huber accumulation overflowed its float64 domain.");
            }
            if (recordDiagnostics) {
                auto& diagnostic = working.observationDiagnostics[index];
                diagnostic.hasNumericResidual = true;
                diagnostic.standardizedResidual = residual;
                diagnostic.robustFactor = robustFactor;
                diagnostic.resolvedReliability = resolvedReliability;
            }
        }
        pass = next;
        return true;
    };

    double current = configuration.initialPilot;
    FrozenPass pass;
    for (std::uint32_t iteration = 0;
         iteration < configuration.maximumIterations;
         ++iteration) {
        if (!evaluatePass(current, pass, false)) return false;
        if (pass.precision <= 0.0) break;
        const double next = pass.numerator / pass.precision;
        if (!std::isfinite(next)) {
            return SetError(error,
                "Shared-pilot Huber update is not finite.");
        }
        working.finalUpdateMagnitude = std::abs(next - current);
        ++working.iterations;
        const double tolerance = configuration.absoluteTolerance +
            configuration.relativeTolerance *
                std::max(std::abs(current), std::abs(next));
        current = next;
        if (working.finalUpdateMagnitude <= tolerance) {
            working.converged = true;
            break;
        }
    }

    FrozenPass finalPass;
    if (!evaluatePass(current, finalPass, true)) return false;
    if (finalPass.precision <= 0.0) {
        working.state = working.anchorBounds.lowerInclusive ||
                working.anchorBounds.upperInclusive
            ? FixedGaussianEstimateState::BoundedOnly
            : FixedGaussianEstimateState::Unresolved;
        estimate = std::move(working);
        if (error) error->clear();
        return true;
    }

    working.state = FixedGaussianEstimateState::Estimate;
    working.estimate = current;
    working.conditionalQuadraticVariance = 1.0 / finalPass.precision;
    working.policyConditionalSamplingVariance =
        finalPass.policySamplingNumerator /
        (finalPass.precision * finalPass.precision);
    working.policyConditionalSamplingVarianceAvailable =
        std::isfinite(working.policyConditionalSamplingVariance) &&
        working.policyConditionalSamplingVariance >= 0.0;
    working.effectiveSupport = finalPass.precisionSquareSum > 0.0
        ? finalPass.precision * finalPass.precision /
            finalPass.precisionSquareSum
        : 0.0;

    double robustBread = 0.0;
    double robustMeat = 0.0;
    for (const std::size_t index : order) {
        const FixedGaussianObservation& observation = observations[index];
        const auto& diagnostic = working.observationDiagnostics[index];
        if (!diagnostic.hasNumericResidual ||
            observation.reliability == 0.0) {
            continue;
        }
        ++working.equalitySupportCount;
        if (observation.evidence.state ==
            SampleEvidenceState::RepairedMeasurement) {
            ++working.repairedEqualitySupportCount;
        }
        const double eOverSigma = observation.exposureScale /
            std::sqrt(observation.frozenVariance);
        if (std::abs(diagnostic.standardizedResidual) <=
            configuration.huberThreshold) {
            robustBread += observation.reliability *
                eOverSigma * eOverSigma;
        }
        const double score = observation.reliability * eOverSigma *
            HuberPsi(diagnostic.standardizedResidual,
                configuration.huberThreshold);
        robustMeat += score * score;
    }
    if (!std::isfinite(working.conditionalQuadraticVariance) ||
        !std::isfinite(working.effectiveSupport) ||
        !std::isfinite(robustBread) || !std::isfinite(robustMeat)) {
        return SetError(error,
            "Shared-pilot Huber final diagnostics overflowed float64.");
    }
    if (robustBread > 0.0) {
        working.robustLinearizedVariance =
            robustMeat / (robustBread * robustBread);
        working.robustLinearizedVarianceAvailable =
            std::isfinite(working.robustLinearizedVariance) &&
            working.robustLinearizedVariance >= 0.0;
        if (working.robustLinearizedVarianceAvailable &&
            working.effectiveSupport > 1.0) {
            working.finiteSampleCorrectedRobustVariance =
                working.robustLinearizedVariance *
                (working.effectiveSupport /
                    (working.effectiveSupport - 1.0));
            working.finiteSampleCorrectedRobustVarianceAvailable =
                std::isfinite(
                    working.finiteSampleCorrectedRobustVariance) &&
                working.finiteSampleCorrectedRobustVariance >= 0.0;
        }
    }
    working.publishedConservativeVariance =
        working.conditionalQuadraticVariance;
    if (working.policyConditionalSamplingVarianceAvailable) {
        working.publishedConservativeVariance = std::max(
            working.publishedConservativeVariance,
            working.policyConditionalSamplingVariance);
    }
    if (working.finiteSampleCorrectedRobustVarianceAvailable) {
        working.publishedConservativeVariance = std::max(
            working.publishedConservativeVariance,
            working.finiteSampleCorrectedRobustVariance);
    } else if (working.robustLinearizedVarianceAvailable) {
        working.publishedConservativeVariance = std::max(
            working.publishedConservativeVariance,
            working.robustLinearizedVariance);
    }
    working.publishedConservativeVarianceAvailable =
        std::isfinite(working.publishedConservativeVariance) &&
        working.publishedConservativeVariance >= 0.0;
    if (working.anchorBounds.lowerInclusive &&
        working.estimate < *working.anchorBounds.lowerInclusive) {
        working.estimateOutsideBounds = true;
    }
    if (working.anchorBounds.upperInclusive &&
        working.estimate > *working.anchorBounds.upperInclusive) {
        working.estimateOutsideBounds = true;
    }

    estimate = std::move(working);
    if (error) error->clear();
    return true;
}

} // namespace Raw::MultiFrame
