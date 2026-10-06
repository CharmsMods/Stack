#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrameDenoise/Processor.h"
#include "Raw/MultiFrameDenoise/SharedBurst.h"
#include "Raw/MultiFrame/HuberEstimator.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Shared Burst V1 validation failed: "
                  << message << std::endl;
    }
    return condition;
}

Raw::Mfd::FusionReferenceSample Reference(
    double value,
    Raw::Mfd::CfaSite site,
    double variance = 0.01) {
    Raw::Mfd::FusionReferenceSample sample;
    sample.valid = true;
    sample.noiseQuality = Raw::Mfd::NoiseModelQuality::TrustedMetadata;
    sample.normalizedValue = value;
    sample.comparisonGain = 1.0;
    sample.gateVariance = variance;
    sample.fusionVariance = variance;
    sample.darkVariance = variance;
    sample.site = site;
    return sample;
}

Raw::Mfd::FusionCandidateSample Candidate(
    double value,
    std::uint64_t ordinal,
    double variance = 0.01,
    double trust = 1.0) {
    Raw::Mfd::FusionCandidateSample sample;
    sample.valid = true;
    sample.hardValid = true;
    sample.noiseQuality = Raw::Mfd::NoiseModelQuality::TrustedMetadata;
    sample.invalidReason = Raw::Mfd::FusionRejectReason::None;
    sample.value = value;
    sample.gateVariance = variance;
    sample.fusionVariance = variance;
    sample.darkVariance = variance;
    sample.reliability = 1.0;
    sample.referenceDefectGate = 1.0;
    sample.sourceOrdinal = ordinal;
    sample.trustAttenuation = trust;
    return sample;
}

bool ValidateContractsAndFrameCounts() {
    bool ok = true;
    Raw::Mfd::SharedBurstSettings settings;
    std::string error;
    ok &= Check(
        Raw::Mfd::ValidateSharedBurstSettings(settings, &error), error);
    Raw::Mfd::SharedBurstSettings tooManyIterations = settings;
    tooManyIterations.maximumHuberIterations = 13u;
    ok &= Check(
        !Raw::Mfd::ValidateSharedBurstSettings(
            tooManyIterations, &error),
        "Shared Burst accepted more than 12 Huber iterations");
    Raw::Mfd::SharedBurstSettings wrongThreshold = settings;
    wrongThreshold.huberThreshold = 1.5;
    ok &= Check(
        !Raw::Mfd::ValidateSharedBurstSettings(wrongThreshold, &error),
        "Static Maximum accepted a non-versioned Huber threshold");
    const auto serialized = Raw::Mfd::SerializeSharedBurstSettings(settings);
    Raw::Mfd::SharedBurstSettings restored;
    ok &= Check(
        Raw::Mfd::DeserializeSharedBurstSettings(
            serialized, restored, &error) &&
        restored.profile == "static-maximum" &&
        restored.exposureGroupToleranceEv == 0.5,
        "settings did not round-trip with pinned defaults");
    ok &= Check(
        Raw::Mfd::kSharedBurstMinimumEnabledCaptures == 2u &&
        Raw::Mfd::kSharedBurstMaximumEnabledCaptures == 30u &&
        std::string(Raw::Mfd::kSharedBurstAlgorithmVersionId) ==
            "shared-burst-v1" &&
        std::string(Raw::Mfd::kSharedBurstAlgorithmVersionId) !=
            Raw::Mfd::kAlgorithmVersionId &&
        std::string(Raw::Mfd::kSharedBurstFusionContractId) !=
            Raw::Mfd::kFusionContractId &&
        !std::string(Raw::Mfd::kSharedBurstResultCacheContractId).empty(),
        "public Shared Burst identities or capture limits changed");
    ok &= Check(
        sizeof(Raw::Mfd::FusionPixelDiagnostics) <= 88u,
        "full-resolution per-pixel diagnostics exceeded the bounded layout");
    constexpr std::uint64_t gibibyte = 1024ull * 1024ull * 1024ull;
    const Raw::Mfd::PixelExtent phoneRaw { 4080u, 3060u };
    const Raw::Mfd::Parameters parameters;
    const std::uint64_t sixFramePeak =
        Raw::Mfd::EstimateMfdPeakResidentBytes(
            phoneRaw,
            6u,
            parameters,
            4u,
            Raw::Mfd::MfdAlignmentMode::Full);
    const std::uint64_t thirtyFramePeak =
        Raw::Mfd::EstimateMfdPeakResidentBytes(
            phoneRaw,
            30u,
            parameters,
            4u,
            Raw::Mfd::MfdAlignmentMode::Full);
    ok &= Check(
        sixFramePeak < 3ull * gibibyte,
        "six 4080x3060 captures no longer fit a 3 GiB safe processing budget");
    ok &= Check(
        thirtyFramePeak < 6ull * gibibyte,
        "thirty 4080x3060 captures exceeded the bounded 6 GiB review ceiling");
    return ok;
}

bool ValidateStaticReductionAndCfa() {
    const std::array<Raw::Mfd::CfaSite, 4> sites {
        Raw::Mfd::CfaSite::Red,
        Raw::Mfd::CfaSite::Green0,
        Raw::Mfd::CfaSite::Green1,
        Raw::Mfd::CfaSite::Blue
    };
    const std::array<std::size_t, 5> counts { 2u, 4u, 10u, 20u, 30u };
    Raw::Mfd::Parameters parameters;
    Raw::Mfd::SharedBurstSettings settings;
    bool ok = true;
    for (const auto site : sites) {
        for (const std::size_t count : counts) {
            const double latent = site == Raw::Mfd::CfaSite::Red
                ? -0.125
                : site == Raw::Mfd::CfaSite::Blue ? 1.75 : 0.625;
            std::vector<Raw::Mfd::FusionCandidateSample> candidates;
            for (std::size_t index = 1u; index < count; ++index) {
                candidates.push_back(Candidate(latent, index));
            }
            Raw::Mfd::FusionPixelResult result;
            std::string error;
            ok &= Check(
                Raw::Mfd::FuseSharedBurstSample(
                    Reference(latent, site), candidates,
                    parameters, settings, result, &error),
                "static solve failed: " + error);
            ok &= Check(
                result.valid && result.normalizedValue == latent &&
                !result.diagnostics.exactReferenceCopy &&
                result.diagnostics.rawSupportCount == count &&
                std::abs(result.diagnostics.effectiveSampleCount -
                    static_cast<double>(count)) < 1.0e-9 &&
                std::abs(result.diagnostics.outputVarianceComparisonDomain -
                    0.01 / static_cast<double>(count)) < 1.0e-10,
                "static identity data lost detail, support, or 1/N variance");
        }
    }
    return ok;
}

bool ValidateSafetyAndTrust() {
    Raw::Mfd::Parameters parameters;
    Raw::Mfd::SharedBurstSettings settings;
    bool ok = true;
    std::string error;

    auto clipped = Reference(0.8, Raw::Mfd::CfaSite::Red);
    clipped.clipped = true;
    Raw::Mfd::FusionPixelResult result;
    ok &= Check(
        Raw::Mfd::FuseSharedBurstSample(
            clipped, { Candidate(0.8, 1u) },
            parameters, settings, result, &error) &&
        result.diagnostics.exactReferenceCopy &&
        result.normalizedValue == clipped.normalizedValue,
        "clipped temporal owner did not fall back exactly");

    result = {};
    ok &= Check(
        Raw::Mfd::FuseSharedBurstSample(
            Reference(0.25, Raw::Mfd::CfaSite::Green0),
            { Candidate(0.25, 1u, 0.01, 0.0) },
            parameters, settings, result, &error) &&
        result.diagnostics.exactReferenceCopy,
        "zero user trust revived an alternate");

    auto invalid = Candidate(0.25, 1u);
    invalid.hardValid = false;
    result = {};
    ok &= Check(
        Raw::Mfd::FuseSharedBurstSample(
            Reference(0.25, Raw::Mfd::CfaSite::Green1),
            { invalid }, parameters, settings, result, &error) &&
        result.diagnostics.exactReferenceCopy,
        "hard-invalid alternate bypassed exact-reference safety");
    return ok;
}

bool ValidateUncertaintyMonteCarlo() {
    constexpr std::size_t captureCount = 20u;
    constexpr std::size_t trials = 4000u;
    constexpr double variance = 0.04;
    constexpr double truth = 0.4;
    std::mt19937_64 random(0x535441434b425552ull);
    std::normal_distribution<double> noise(0.0, std::sqrt(variance));
    Raw::MultiFrame::SharedPilotHuberConfiguration configuration;
    configuration.phase = Raw::MultiFrame::CfaPhase::R;
    configuration.baseWeightSemantics =
        Raw::MultiFrame::FixedGaussianWeightSemantics::PolicyAttenuation;
    configuration.modelIdentity = "shared-burst-validation-noise-v1";
    configuration.geometryIdentity = "identity";
    configuration.pilotIdentity = "independent-reference-pilot";

    double squaredError = 0.0;
    double predicted = 0.0;
    double predictedModel = 0.0;
    double predictedPolicy = 0.0;
    double predictedRobust = 0.0;
    std::size_t withinOne = 0u;
    std::size_t withinTwo = 0u;
    for (std::size_t trial = 0u; trial < trials; ++trial) {
        std::vector<Raw::MultiFrame::FixedGaussianObservation> observations;
        observations.reserve(captureCount);
        for (std::size_t capture = 0u; capture < captureCount; ++capture) {
            Raw::MultiFrame::FixedGaussianObservation observation;
            observation.preparedValue = truth + noise(random);
            observation.frozenVariance = variance;
            observation.reliability = 1.0;
            observation.sourceOrdinal = capture;
            observations.push_back(observation);
        }
        configuration.initialPilot = observations.front().preparedValue;
        Raw::MultiFrame::SharedPilotHuberEstimate estimate;
        std::string error;
        if (!Raw::MultiFrame::EstimateSharedPilotHuber(
                configuration, observations, estimate, &error) ||
            !estimate.publishedConservativeVarianceAvailable) {
            return Check(false, "Monte Carlo estimator failed: " + error);
        }
        const double delta = estimate.estimate - truth;
        const double sigma = std::sqrt(
            estimate.publishedConservativeVariance);
        squaredError += delta * delta;
        predicted += estimate.publishedConservativeVariance;
        predictedModel += estimate.conditionalQuadraticVariance;
        predictedPolicy += estimate.policyConditionalSamplingVariance;
        predictedRobust += estimate.finiteSampleCorrectedRobustVariance;
        withinOne += std::abs(delta) <= sigma ? 1u : 0u;
        withinTwo += std::abs(delta) <= 1.96 * sigma ? 1u : 0u;
    }
    const double empiricalVariance = squaredError / trials;
    const double meanPredicted = predicted / trials;
    const double ratio = empiricalVariance / meanPredicted;
    const double coverage68 = static_cast<double>(withinOne) / trials;
    const double coverage95 = static_cast<double>(withinTwo) / trials;
    const std::string metrics =
        "uncertainty coverage left the provisional review band "
        "(empirical/predicted=" + std::to_string(ratio) +
        ", 68%=" + std::to_string(coverage68) +
        ", 95%=" + std::to_string(coverage95) +
        ", model=" + std::to_string(predictedModel / trials) +
        ", policy=" + std::to_string(predictedPolicy / trials) +
        ", robust=" + std::to_string(predictedRobust / trials) +
        ", published=" + std::to_string(meanPredicted) + ")";
    return Check(
        ratio >= 0.8 && ratio <= 1.25 &&
        coverage68 >= 0.63 && coverage68 <= 0.78 &&
        coverage95 >= 0.92 && coverage95 <= 0.99,
        metrics);
}

} // namespace

bool ValidateSharedBurstV1() {
    bool ok = true;
    ok &= ValidateContractsAndFrameCounts();
    ok &= ValidateStaticReductionAndCfa();
    ok &= ValidateSafetyAndTrust();
    ok &= ValidateUncertaintyMonteCarlo();
    ok &= ValidateSharedBurstEndToEndProcessor();
    if (ok) {
        std::cout
            << "Shared Burst V1 contracts, 2-30 frame fusion, CFA, safety, "
               "and uncertainty validation passed."
            << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
