#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrame/CanonicalEstimator.h"
#include "Raw/MultiFrame/HuberEstimator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Unified MultiFrame estimator validation failed: "
                  << message << std::endl;
    }
    return condition;
}

bool NearlyEqual(double left, double right, double tolerance = 1.0e-13) {
    return std::abs(left - right) <= tolerance;
}

Raw::MultiFrame::FixedGaussianConfiguration Configuration(
    Raw::MultiFrame::CfaPhase phase,
    Raw::MultiFrame::FixedGaussianWeightSemantics semantics =
        Raw::MultiFrame::FixedGaussianWeightSemantics::BinaryInclusion) {
    Raw::MultiFrame::FixedGaussianConfiguration configuration;
    configuration.phase = phase;
    configuration.weightSemantics = semantics;
    configuration.modelIdentity = "validation-frozen-variance-v1";
    configuration.geometryIdentity = "validation-identity-geometry-v1";
    return configuration;
}

Raw::MultiFrame::FixedGaussianObservation ExactObservation(
    double preparedValue,
    double exposureScale,
    double frozenVariance,
    double reliability = 1.0,
    double blackOffset = 0.0) {
    Raw::MultiFrame::FixedGaussianObservation observation;
    observation.preparedValue = preparedValue;
    observation.blackOffset = blackOffset;
    observation.exposureScale = exposureScale;
    observation.frozenVariance = frozenVariance;
    observation.reliability = reliability;
    return observation;
}

Raw::MultiFrame::SharedPilotHuberConfiguration HuberConfiguration(
    Raw::MultiFrame::CfaPhase phase,
    double initialPilot,
    double threshold = 1.345) {
    Raw::MultiFrame::SharedPilotHuberConfiguration configuration;
    configuration.phase = phase;
    configuration.modelIdentity = "validation-frozen-variance-v1";
    configuration.geometryIdentity = "validation-identity-geometry-v1";
    configuration.pilotIdentity = "validation-shared-pilot-v1";
    configuration.initialPilot = initialPilot;
    configuration.huberThreshold = threshold;
    return configuration;
}

bool ValidateEqualExposureReduction() {
    using Raw::MultiFrame::CfaPhase;
    const std::array<CfaPhase, 4> phases {
        CfaPhase::R, CfaPhase::G0, CfaPhase::G1, CfaPhase::B
    };
    const std::array<double, 4> latent {
        -0.25, 0.25, 0.75, 1.50
    };
    const std::array<double, 4> errors {
        -0.125, 0.125, -0.25, 0.25
    };

    bool ok = true;
    for (std::size_t phaseIndex = 0;
         phaseIndex < phases.size();
         ++phaseIndex) {
        std::vector<Raw::MultiFrame::FixedGaussianObservation> observations;
        for (const double error : errors) {
            observations.push_back(ExactObservation(
                0.0625 + latent[phaseIndex] + error,
                1.0,
                0.25,
                1.0,
                0.0625));
        }
        Raw::MultiFrame::FixedGaussianEstimate estimate;
        Raw::MultiFrame::FixedGaussianAccumulator accumulator;
        std::string error;
        ok &= Check(
            Raw::MultiFrame::EstimateFixedGaussian(
                Configuration(phases[phaseIndex]),
                observations,
                estimate,
                &accumulator,
                &error),
            "equal-exposure solve failed: " + error);
        ok &= Check(
            estimate.state ==
                    Raw::MultiFrame::FixedGaussianEstimateState::Estimate &&
                NearlyEqual(estimate.estimate, latent[phaseIndex]) &&
                NearlyEqual(estimate.quadraticVariance, 0.0625) &&
                NearlyEqual(estimate.effectiveSupport, 4.0) &&
                estimate.equalitySupportCount == 4u,
            "equal-exposure reduction changed signal, variance, or support");
    }
    return ok;
}

bool ValidateBracketAndCensoring() {
    using Raw::MultiFrame::FixedGaussianEstimateState;
    using Raw::MultiFrame::SampleEvidenceState;

    constexpr double latent = 0.375;
    std::vector<Raw::MultiFrame::FixedGaussianObservation> observations {
        ExactObservation(0.25 * latent, 0.25, 0.015625),
        ExactObservation(latent, 1.0, 0.25),
        ExactObservation(4.0 * latent, 4.0, 4.0)
    };
    Raw::MultiFrame::FixedGaussianObservation clipped =
        ExactObservation(0.0, 8.0, 1.0);
    clipped.evidence.state = SampleEvidenceState::UpperCensored;
    clipped.evidence.bounds.lowerInclusive = 2.0;
    observations.push_back(clipped);

    Raw::MultiFrame::FixedGaussianEstimate estimate;
    Raw::MultiFrame::FixedGaussianAccumulator accumulator;
    std::string error;
    bool ok = Check(
        Raw::MultiFrame::EstimateFixedGaussian(
            Configuration(Raw::MultiFrame::CfaPhase::G0),
            observations,
            estimate,
            &accumulator,
            &error),
        "bracket solve failed: " + error);
    ok &= Check(
        estimate.state == FixedGaussianEstimateState::Estimate &&
            NearlyEqual(estimate.estimate, latent) &&
            estimate.equalitySupportCount == 3u &&
            estimate.anchorBounds.lowerInclusive &&
            NearlyEqual(*estimate.anchorBounds.lowerInclusive, 0.25) &&
            accumulator.evidenceStateCounts[static_cast<std::size_t>(
                SampleEvidenceState::UpperCensored)] == 1u,
        "clipped bracket sample entered equality fusion or lost its bound");

    Raw::MultiFrame::FixedGaussianObservation lowerBound = clipped;
    lowerBound.exposureScale = 4.0;
    lowerBound.evidence.bounds.lowerInclusive = 2.0;
    Raw::MultiFrame::FixedGaussianObservation upperBound = clipped;
    upperBound.exposureScale = 2.0;
    upperBound.evidence.state = SampleEvidenceState::LowerCensored;
    upperBound.evidence.bounds.lowerInclusive.reset();
    upperBound.evidence.bounds.upperInclusive = 1.5;
    Raw::MultiFrame::FixedGaussianObservation invalidZero = clipped;
    invalidZero.exposureScale = 1.0;
    invalidZero.evidence.state = SampleEvidenceState::DecoderInvalid;
    invalidZero.evidence.bounds = {};
    Raw::MultiFrame::FixedGaussianEstimate bounded;
    ok &= Check(
        Raw::MultiFrame::EstimateFixedGaussian(
            Configuration(Raw::MultiFrame::CfaPhase::G0),
            { lowerBound, upperBound, invalidZero },
            bounded,
            nullptr,
            &error),
        "bounded-only solve failed: " + error);
    ok &= Check(
        bounded.state == FixedGaussianEstimateState::BoundedOnly &&
            bounded.equalitySupportCount == 0u &&
            bounded.anchorBounds.lowerInclusive &&
            bounded.anchorBounds.upperInclusive &&
            NearlyEqual(*bounded.anchorBounds.lowerInclusive, 0.5) &&
            NearlyEqual(*bounded.anchorBounds.upperInclusive, 0.75),
        "bounded-only evidence became a fabricated numeric estimate");

    Raw::MultiFrame::FixedGaussianEstimate unresolved;
    ok &= Check(
        Raw::MultiFrame::EstimateFixedGaussian(
            Configuration(Raw::MultiFrame::CfaPhase::G0),
            { invalidZero },
            unresolved,
            nullptr,
            &error) &&
            unresolved.state == FixedGaussianEstimateState::Unresolved,
        "invalid numeric zero became equality support");
    return ok;
}

bool ValidateObservedValueWeightBiasAblation() {
    const auto configuration = Configuration(Raw::MultiFrame::CfaPhase::R);
    Raw::MultiFrame::FixedGaussianEstimate sharedPilot;
    Raw::MultiFrame::FixedGaussianEstimate observedValue;
    std::string error;
    bool ok = Check(
        Raw::MultiFrame::EstimateFixedGaussian(
            configuration,
            {
                ExactObservation(0.10, 1.0, 0.03),
                ExactObservation(0.30, 1.0, 0.03)
            },
            sharedPilot,
            nullptr,
            &error),
        "shared-pilot bias fixture failed: " + error);
    ok &= Check(
        Raw::MultiFrame::EstimateFixedGaussian(
            configuration,
            {
                // Deliberately wrong ablation: variance follows each noisy
                // observation, so the negative excursion receives more weight.
                ExactObservation(0.10, 1.0, 0.02),
                ExactObservation(0.30, 1.0, 0.04)
            },
            observedValue,
            nullptr,
            &error),
        "observed-value bias ablation failed: " + error);
    ok &= Check(
        NearlyEqual(sharedPilot.estimate, 0.20) &&
            observedValue.estimate < 0.18 &&
            sharedPilot.estimate - observedValue.estimate > 0.03,
        "observed-value weighting did not expose the expected downward bias");
    return ok;
}

bool ValidateGroupAccumulatorParity() {
    using Raw::MultiFrame::FixedGaussianAccumulator;
    const auto configuration = Configuration(Raw::MultiFrame::CfaPhase::G1);
    FixedGaussianAccumulator joint;
    FixedGaussianAccumulator reduced;
    std::array<FixedGaussianAccumulator, 3> groups;
    std::string error;
    bool ok = Check(
        Raw::MultiFrame::InitializeFixedGaussianAccumulator(
            configuration, joint, &error) &&
        Raw::MultiFrame::InitializeFixedGaussianAccumulator(
            configuration, reduced, &error),
        "group parity accumulators could not initialize: " + error);
    for (FixedGaussianAccumulator& group : groups) {
        ok &= Check(
            Raw::MultiFrame::InitializeFixedGaussianAccumulator(
                configuration, group, &error),
            "group accumulator could not initialize: " + error);
    }

    const std::array<double, 3> exposure { 0.25, 1.0, 4.0 };
    constexpr double latent = 0.5;
    for (std::size_t groupIndex = 0;
         groupIndex < groups.size();
         ++groupIndex) {
        for (std::size_t capture = 0; capture < 10u; ++capture) {
            const double e = exposure[groupIndex];
            const auto observation = ExactObservation(
                e * latent,
                e,
                e * e);
            ok &= Check(
                Raw::MultiFrame::AddFixedGaussianObservation(
                    observation, joint, &error) &&
                Raw::MultiFrame::AddFixedGaussianObservation(
                    observation, groups[groupIndex], &error),
                "group parity observation could not accumulate: " + error);
        }
    }
    for (const FixedGaussianAccumulator& group : groups) {
        ok &= Check(
            Raw::MultiFrame::MergeFixedGaussianAccumulator(
                group, reduced, &error),
            "group accumulator could not merge: " + error);
    }
    const auto jointEstimate =
        Raw::MultiFrame::FinalizeFixedGaussianAccumulator(joint);
    const auto reducedEstimate =
        Raw::MultiFrame::FinalizeFixedGaussianAccumulator(reduced);
    ok &= Check(
        joint.numerator == reduced.numerator &&
            joint.precision == reduced.precision &&
            joint.policySamplingVarianceNumerator ==
                reduced.policySamplingVarianceNumerator &&
            joint.normalizedWeightSquareSum ==
                reduced.normalizedWeightSquareSum &&
            jointEstimate.estimate == reducedEstimate.estimate &&
            jointEstimate.quadraticVariance ==
                reducedEstimate.quadraticVariance &&
            joint.equalitySupportCount == 30u &&
            NearlyEqual(jointEstimate.estimate, latent),
        "three-by-ten reduced and joint fixed-Gaussian paths diverged");
    return ok;
}

bool ValidateWeightSemanticsAndAtomicFailure() {
    using Raw::MultiFrame::FixedGaussianAccumulator;
    using Raw::MultiFrame::FixedGaussianWeightSemantics;

    const auto policyConfiguration = Configuration(
        Raw::MultiFrame::CfaPhase::B,
        FixedGaussianWeightSemantics::PolicyAttenuation);
    Raw::MultiFrame::FixedGaussianEstimate policy;
    std::string error;
    bool ok = Check(
        Raw::MultiFrame::EstimateFixedGaussian(
            policyConfiguration,
            {
                ExactObservation(1.0, 1.0, 1.0, 0.5),
                ExactObservation(1.0, 1.0, 1.0, 0.5)
            },
            policy,
            nullptr,
            &error),
        "policy-weight solve failed: " + error);
    ok &= Check(
        NearlyEqual(policy.estimate, 1.0) &&
            NearlyEqual(policy.quadraticVariance, 1.0) &&
            NearlyEqual(policy.conditionalSamplingVariance, 0.5) &&
            !policy.modelVarianceCalibrated,
        "policy attenuation was mislabeled as calibrated precision");

    FixedGaussianAccumulator accumulator;
    const auto binaryConfiguration = Configuration(
        Raw::MultiFrame::CfaPhase::B);
    ok &= Check(
        Raw::MultiFrame::InitializeFixedGaussianAccumulator(
            binaryConfiguration, accumulator, &error),
        "atomic-failure accumulator could not initialize: " + error);
    const FixedGaussianAccumulator before = accumulator;
    auto invalid = ExactObservation(1.0, 0.0, 1.0);
    ok &= Check(
        !Raw::MultiFrame::AddFixedGaussianObservation(
            invalid, accumulator, &error) &&
            accumulator.observationCount == before.observationCount &&
            accumulator.numerator == before.numerator &&
            accumulator.precision == before.precision,
        "invalid observation partially mutated the accumulator");

    FixedGaussianAccumulator incompatible;
    ok &= Check(
        Raw::MultiFrame::InitializeFixedGaussianAccumulator(
            Configuration(Raw::MultiFrame::CfaPhase::R),
            incompatible,
            &error),
        "incompatible accumulator fixture could not initialize: " + error);
    ok &= Check(
        !Raw::MultiFrame::MergeFixedGaussianAccumulator(
            incompatible, accumulator, &error) &&
            accumulator.observationCount == before.observationCount &&
            accumulator.numerator == before.numerator &&
            accumulator.precision == before.precision,
        "incompatible merge partially mutated the destination");
    return ok;
}

bool ValidateHuberEqualAndBracketReduction() {
    using Raw::MultiFrame::CfaPhase;
    using Raw::MultiFrame::FixedGaussianEstimateState;
    const std::array<CfaPhase, 4> phases {
        CfaPhase::R, CfaPhase::G0, CfaPhase::G1, CfaPhase::B
    };
    const std::array<double, 4> latent {
        -0.25, 0.25, 0.75, 1.50
    };
    const std::array<double, 4> errors {
        -0.01, 0.01, -0.02, 0.02
    };

    bool ok = true;
    for (std::size_t phaseIndex = 0;
         phaseIndex < phases.size();
         ++phaseIndex) {
        std::vector<Raw::MultiFrame::FixedGaussianObservation> observations;
        for (std::size_t index = 0; index < errors.size(); ++index) {
            auto observation = ExactObservation(
                latent[phaseIndex] + errors[index], 1.0, 0.01);
            observation.sourceOrdinal = index;
            observations.push_back(observation);
        }
        Raw::MultiFrame::SharedPilotHuberEstimate estimate;
        std::string error;
        ok &= Check(
            Raw::MultiFrame::EstimateSharedPilotHuber(
                HuberConfiguration(phases[phaseIndex], latent[phaseIndex]),
                observations,
                estimate,
                &error),
            "Huber equal-exposure solve failed: " + error);
        ok &= Check(
            estimate.state == FixedGaussianEstimateState::Estimate &&
                estimate.converged &&
                NearlyEqual(estimate.estimate, latent[phaseIndex]) &&
                estimate.equalitySupportCount == 4u &&
                estimate.effectiveSupport > 3.99,
            "Huber equal-exposure reduction changed signal or support");
        for (const auto& diagnostic : estimate.observationDiagnostics) {
            ok &= Check(
                diagnostic.hasNumericResidual &&
                    NearlyEqual(diagnostic.robustFactor, 1.0),
                "Huber attenuated an in-model equal-exposure sample");
        }
    }

    constexpr double bracketLatent = 0.5;
    const std::array<double, 3> exposures { 0.25, 1.0, 4.0 };
    std::vector<Raw::MultiFrame::FixedGaussianObservation> bracket;
    for (std::size_t index = 0; index < exposures.size(); ++index) {
        const double exposure = exposures[index];
        auto observation = ExactObservation(
            exposure * bracketLatent,
            exposure,
            exposure * exposure * 0.01);
        observation.sourceOrdinal = index;
        bracket.push_back(observation);
    }
    Raw::MultiFrame::SharedPilotHuberEstimate bracketEstimate;
    std::string error;
    ok &= Check(
        Raw::MultiFrame::EstimateSharedPilotHuber(
            HuberConfiguration(CfaPhase::G0, 0.4),
            bracket,
            bracketEstimate,
            &error),
        "Huber bracket solve failed: " + error);
    ok &= Check(
        bracketEstimate.converged &&
            NearlyEqual(bracketEstimate.estimate, bracketLatent) &&
            bracketEstimate.equalitySupportCount == 3u &&
            bracketEstimate.effectiveSupport > 2.99,
        "Huber bracket reduction failed exposure-domain parity");
    return ok;
}

bool ValidateHuberBoundedInfluenceAndEvidence() {
    using Raw::MultiFrame::FixedGaussianEstimateState;
    using Raw::MultiFrame::SampleEvidenceState;

    std::vector<Raw::MultiFrame::FixedGaussianObservation> observations;
    const std::array<double, 4> values { 0.48, 0.50, 0.52, 2.0 };
    for (std::size_t index = 0; index < values.size(); ++index) {
        auto observation = ExactObservation(values[index], 1.0, 0.0004);
        observation.sourceOrdinal = index;
        observations.push_back(observation);
    }

    Raw::MultiFrame::FixedGaussianEstimate gaussian;
    Raw::MultiFrame::SharedPilotHuberEstimate huber;
    std::string error;
    bool ok = Check(
        Raw::MultiFrame::EstimateFixedGaussian(
            Configuration(Raw::MultiFrame::CfaPhase::R),
            observations,
            gaussian,
            nullptr,
            &error) &&
        Raw::MultiFrame::EstimateSharedPilotHuber(
            HuberConfiguration(Raw::MultiFrame::CfaPhase::R, 0.5, 1.5),
            observations,
            huber,
            &error),
        "Huber bounded-influence fixture failed: " + error);
    ok &= Check(
        huber.converged && NearlyEqual(huber.estimate, 0.51, 1.0e-9) &&
            gaussian.estimate > 0.87 &&
            huber.observationDiagnostics[3].robustFactor < 0.03 &&
            huber.observationDiagnostics[3].resolvedReliability < 0.03 &&
            huber.robustLinearizedVarianceAvailable &&
            !huber.robustVarianceEmpiricallyCalibrated,
        "Huber did not bound the outlier or mislabeled robust uncertainty");

    auto clipped = ExactObservation(0.0, 4.0, 1.0);
    clipped.sourceOrdinal = 10u;
    clipped.evidence.state = SampleEvidenceState::UpperCensored;
    clipped.evidence.bounds.lowerInclusive = 2.0;
    auto invalid = ExactObservation(0.0, 1.0, 1.0);
    invalid.sourceOrdinal = 11u;
    invalid.evidence.state = SampleEvidenceState::DecoderInvalid;
    Raw::MultiFrame::SharedPilotHuberEstimate bounded;
    ok &= Check(
        Raw::MultiFrame::EstimateSharedPilotHuber(
            HuberConfiguration(Raw::MultiFrame::CfaPhase::R, 0.0),
            { clipped, invalid },
            bounded,
            &error),
        "Huber bounded-only fixture failed: " + error);
    ok &= Check(
        bounded.state == FixedGaussianEstimateState::BoundedOnly &&
            bounded.equalitySupportCount == 0u &&
            bounded.anchorBounds.lowerInclusive &&
            NearlyEqual(*bounded.anchorBounds.lowerInclusive, 0.5) &&
            !bounded.observationDiagnostics[0].hasNumericResidual &&
            !bounded.observationDiagnostics[1].hasNumericResidual,
        "Huber converted censored or invalid evidence into numeric support");
    return ok;
}

bool ValidateHuberDeterminismAndAtomicFailure() {
    std::vector<Raw::MultiFrame::FixedGaussianObservation> forward;
    const std::array<double, 3> exposure { 0.25, 1.0, 4.0 };
    constexpr double latent = 0.5;
    for (std::size_t group = 0; group < exposure.size(); ++group) {
        for (std::size_t capture = 0; capture < 10u; ++capture) {
            const double e = exposure[group];
            const double sceneOffset = capture == 7u && group == 1u
                ? 0.25
                : (static_cast<double>(capture % 3u) - 1.0) * 0.00390625;
            auto observation = ExactObservation(
                e * (latent + sceneOffset),
                e,
                e * e * 0.00390625);
            observation.sourceOrdinal = group * 10u + capture;
            forward.push_back(observation);
        }
    }
    auto reverse = forward;
    std::reverse(reverse.begin(), reverse.end());

    const auto configuration = HuberConfiguration(
        Raw::MultiFrame::CfaPhase::G1, 0.5, 1.5);
    Raw::MultiFrame::SharedPilotHuberEstimate forwardEstimate;
    Raw::MultiFrame::SharedPilotHuberEstimate reverseEstimate;
    std::string error;
    bool ok = Check(
        Raw::MultiFrame::EstimateSharedPilotHuber(
            configuration, forward, forwardEstimate, &error) &&
        Raw::MultiFrame::EstimateSharedPilotHuber(
            configuration, reverse, reverseEstimate, &error),
        "Huber canonical-order fixture failed: " + error);
    ok &= Check(
        forwardEstimate.estimate == reverseEstimate.estimate &&
            forwardEstimate.conditionalQuadraticVariance ==
                reverseEstimate.conditionalQuadraticVariance &&
            forwardEstimate.robustLinearizedVariance ==
                reverseEstimate.robustLinearizedVariance &&
            forwardEstimate.iterations == reverseEstimate.iterations,
        "Huber result depends on caller observation order");

    Raw::MultiFrame::SharedPilotHuberEstimate untouched;
    untouched.estimate = 123.0;
    auto invalidConfiguration = configuration;
    invalidConfiguration.huberThreshold = 0.0;
    ok &= Check(
        !Raw::MultiFrame::EstimateSharedPilotHuber(
            invalidConfiguration, forward, untouched, &error) &&
            untouched.estimate == 123.0,
        "invalid Huber configuration partially replaced the prior result");
    return ok;
}

} // namespace

bool ValidateUnifiedMultiFrameEstimator() {
    bool ok = true;
    ok &= Check(
        Raw::MultiFrame::kFixedGaussianEstimatorVersion == 1u &&
            std::string(Raw::MultiFrame::kFixedGaussianEstimatorId) ==
                "stack-multiframe-fixed-gaussian-v1" &&
            Raw::MultiFrame::kSharedPilotHuberEstimatorVersion == 1u &&
            std::string(Raw::MultiFrame::kSharedPilotHuberEstimatorId) ==
                "stack-multiframe-shared-pilot-huber-v1",
        "unified estimator identity changed without review");
    ok &= ValidateEqualExposureReduction();
    ok &= ValidateBracketAndCensoring();
    ok &= ValidateObservedValueWeightBiasAblation();
    ok &= ValidateGroupAccumulatorParity();
    ok &= ValidateWeightSemanticsAndAtomicFailure();
    ok &= ValidateHuberEqualAndBracketReduction();
    ok &= ValidateHuberBoundedInfluenceAndEvidence();
    ok &= ValidateHuberDeterminismAndAtomicFailure();
    if (ok) {
        std::cout
            << "Unified MultiFrame fixed-Gaussian and shared-pilot Huber "
               "estimator validation passed."
            << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
