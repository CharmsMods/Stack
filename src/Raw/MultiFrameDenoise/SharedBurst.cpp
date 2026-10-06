#include "Raw/MultiFrameDenoise/SharedBurst.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace Raw::Mfd {
namespace {

inline constexpr std::size_t kSharedBurstRejectReasonCount =
    static_cast<std::size_t>(
        FusionRejectReason::ReferenceDefectDisagreement) + 1u;
using RejectionCounts =
    std::array<std::uint64_t, kSharedBurstRejectReasonCount>;

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool Finite(double value) {
    return std::isfinite(value);
}

FusionRejectReason GateFailureReason(CandidateGateFailure failure) {
    switch (failure) {
        case CandidateGateFailure::None: return FusionRejectReason::None;
        case CandidateGateFailure::HardInvalid:
            return FusionRejectReason::HardInvalid;
        case CandidateGateFailure::NoiseUnavailable:
            return FusionRejectReason::NoiseUnavailable;
        case CandidateGateFailure::InvalidNumericInput:
            return FusionRejectReason::InvalidNumericInput;
        case CandidateGateFailure::ReliabilityZero:
            return FusionRejectReason::ReliabilityZero;
        case CandidateGateFailure::PixelOutlier:
            return FusionRejectReason::PixelOutlier;
        case CandidateGateFailure::AbsoluteSafetyFailure:
            return FusionRejectReason::AbsoluteSafetyFailure;
    }
    return FusionRejectReason::InvalidNumericInput;
}

void RecordRejection(
    FusionRejectReason reason,
    RejectionCounts& counts,
    FusionPixelDiagnostics& diagnostics) {
    const std::size_t index = static_cast<std::size_t>(reason);
    if (index < counts.size() && reason != FusionRejectReason::None) {
        ++counts[index];
    }
    ++diagnostics.rejectedAlternateCount;
}

FusionRejectReason DominantRejection(
    const RejectionCounts& counts) {
    std::size_t largestIndex = 0u;
    for (std::size_t index = 1u; index < counts.size(); ++index) {
        if (counts[index] > counts[largestIndex]) largestIndex = index;
    }
    return static_cast<FusionRejectReason>(largestIndex);
}

double DivisionVarianceFloor(
    double effectiveDnStep,
    const Parameters& parameters) {
    if (!Finite(effectiveDnStep) || effectiveDnStep < 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::max(parameters.fusion.numericalVarianceFloor,
        effectiveDnStep * effectiveDnStep / 12.0);
}

void SetExactReference(
    const FusionReferenceSample& reference,
    DecisionReason reason,
    FusionPixelResult& result) {
    result = {};
    result.valid = true;
    result.normalizedValue = reference.normalizedValue;
    result.diagnostics.decisionReason = reason;
    result.diagnostics.exactReferenceCopy = true;
    result.diagnostics.referenceIncluded = true;
    result.diagnostics.rawSupportCount = 1u;
    result.diagnostics.ownerSourceIndex = 0u;
    result.diagnostics.ownerContribution = 1.0f;
    result.diagnostics.robustAttenuation = 1.0f;
    result.diagnostics.effectiveSampleCount = 1.0;
    if (Finite(reference.fusionVariance) &&
        reference.fusionVariance >= 0.0) {
        result.diagnostics.modelQuadraticVariance =
            static_cast<float>(reference.fusionVariance);
        result.diagnostics.policyConditionalSamplingVariance =
            static_cast<float>(reference.fusionVariance);
        result.diagnostics.finiteSampleRobustVariance =
            static_cast<float>(reference.fusionVariance);
        result.diagnostics.outputVarianceComparisonDomain =
            reference.fusionVariance;
    }
}

bool ValidReference(
    const FusionReferenceSample& reference,
    const Parameters& parameters,
    double& comparisonValue,
    double& frozenVariance) {
    comparisonValue = reference.comparisonGain * reference.normalizedValue;
    const double floor = DivisionVarianceFloor(
        reference.effectiveDnStep, parameters);
    if (!reference.valid || reference.clipped || reference.defective ||
        reference.noiseQuality == NoiseModelQuality::Unavailable ||
        !Finite(reference.normalizedValue) ||
        !Finite(reference.comparisonGain) || reference.comparisonGain <= 0.0 ||
        reference.comparisonGain >
            parameters.radiometric.maximumComparisonGain ||
        !Finite(comparisonValue) || !Finite(reference.gateVariance) ||
        !Finite(reference.fusionVariance) || !Finite(reference.darkVariance) ||
        reference.gateVariance <= 0.0 || reference.fusionVariance <= 0.0 ||
        reference.darkVariance < 0.0 || !Finite(floor) || floor <= 0.0) {
        return false;
    }
    frozenVariance = std::max(reference.fusionVariance, floor);
    return Finite(frozenVariance) && frozenVariance > 0.0;
}

bool ValidCandidate(
    const FusionCandidateSample& candidate,
    const Parameters& parameters,
    double& frozenVariance) {
    const double floor = DivisionVarianceFloor(
        candidate.effectiveDnStep, parameters);
    if (!candidate.valid || !candidate.hardValid ||
        candidate.noiseQuality == NoiseModelQuality::Unavailable ||
        !Finite(candidate.value) || !Finite(candidate.gateVariance) ||
        !Finite(candidate.fusionVariance) || !Finite(candidate.darkVariance) ||
        !Finite(candidate.reliability) || candidate.reliability < 0.0 ||
        candidate.reliability > 1.0 ||
        !Finite(candidate.trustAttenuation) ||
        candidate.trustAttenuation < 0.0 ||
        candidate.trustAttenuation > 1.0 ||
        candidate.gateVariance <= 0.0 || candidate.fusionVariance <= 0.0 ||
        candidate.darkVariance < 0.0 || !Finite(floor) || floor <= 0.0) {
        return false;
    }
    frozenVariance = std::max(candidate.fusionVariance, floor);
    return Finite(frozenVariance) && frozenVariance > 0.0;
}

struct CompactObservation {
    double value = 0.0;
    double variance = 1.0;
    double reliability = 1.0;
    std::uint64_t sourceOrdinal = 0u;
};

struct CompactHuberEstimate {
    double estimate = 0.0;
    double modelVariance = 0.0;
    double policyVariance = 0.0;
    double finiteSampleRobustVariance = 0.0;
    double publishedVariance = 0.0;
    double effectiveSupport = 0.0;
    bool finiteSampleRobustVarianceAvailable = false;
    std::array<double, kSharedBurstMaximumEnabledCaptures> robustFactors {};
};

double HuberFactor(double residual, double threshold) {
    const double magnitude = std::abs(residual);
    return magnitude <= threshold || magnitude == 0.0
        ? 1.0
        : threshold / magnitude;
}

double HuberPsi(double residual, double threshold) {
    return std::clamp(residual, -threshold, threshold);
}

bool EstimateCompactSharedPilotHuber(
    const std::array<CompactObservation,
        kSharedBurstMaximumEnabledCaptures>& observations,
    std::size_t observationCount,
    double initialPilot,
    const SharedBurstSettings& settings,
    CompactHuberEstimate& estimate) {
    if (observationCount == 0u ||
        observationCount > observations.size()) {
        return false;
    }
    std::array<std::size_t, kSharedBurstMaximumEnabledCaptures> order {};
    for (std::size_t index = 0u; index < observationCount; ++index) {
        order[index] = index;
    }
    std::stable_sort(order.begin(), order.begin() + observationCount,
        [&observations](std::size_t left, std::size_t right) {
            return observations[left].sourceOrdinal <
                observations[right].sourceOrdinal;
        });

    auto evaluate = [&](double pilot,
                        bool recordFactors,
                        double& numerator,
                        double& precision,
                        double& precisionSquares,
                        double& policyNumerator) {
        numerator = 0.0;
        precision = 0.0;
        precisionSquares = 0.0;
        policyNumerator = 0.0;
        for (std::size_t ordered = 0u;
             ordered < observationCount; ++ordered) {
            const std::size_t index = order[ordered];
            const CompactObservation& observation = observations[index];
            if (!Finite(observation.value) ||
                !Finite(observation.variance) ||
                observation.variance <= 0.0 ||
                !Finite(observation.reliability) ||
                observation.reliability < 0.0 ||
                observation.reliability > 1.0) {
                return false;
            }
            const double residual = (observation.value - pilot) /
                std::sqrt(observation.variance);
            const double robust = HuberFactor(
                residual, settings.huberThreshold);
            const double resolved = observation.reliability * robust;
            const double precisionTerm = resolved / observation.variance;
            numerator += precisionTerm * observation.value;
            precision += precisionTerm;
            precisionSquares += precisionTerm * precisionTerm;
            policyNumerator += resolved * resolved / observation.variance;
            if (recordFactors) estimate.robustFactors[index] = robust;
        }
        return Finite(numerator) && Finite(precision) &&
            Finite(precisionSquares) && Finite(policyNumerator);
    };

    double current = initialPilot;
    double numerator = 0.0;
    double precision = 0.0;
    double precisionSquares = 0.0;
    double policyNumerator = 0.0;
    for (std::uint32_t iteration = 0u;
         iteration < settings.maximumHuberIterations; ++iteration) {
        if (!evaluate(current, false, numerator, precision,
                precisionSquares, policyNumerator) || precision <= 0.0) {
            return false;
        }
        const double next = numerator / precision;
        const double tolerance = settings.absoluteHuberTolerance +
            settings.relativeHuberTolerance *
                std::max(std::abs(current), std::abs(next));
        const double change = std::abs(next - current);
        current = next;
        if (change <= tolerance) break;
    }
    if (!evaluate(current, true, numerator, precision,
            precisionSquares, policyNumerator) || precision <= 0.0 ||
        precisionSquares <= 0.0) {
        return false;
    }

    CompactHuberEstimate working;
    working.robustFactors = estimate.robustFactors;
    working.estimate = current;
    working.modelVariance = 1.0 / precision;
    working.policyVariance =
        policyNumerator / (precision * precision);
    working.effectiveSupport =
        precision * precision / precisionSquares;
    double bread = 0.0;
    double meat = 0.0;
    for (std::size_t index = 0u; index < observationCount; ++index) {
        const CompactObservation& observation = observations[index];
        const double residual = (observation.value - current) /
            std::sqrt(observation.variance);
        if (std::abs(residual) <= settings.huberThreshold) {
            bread += observation.reliability / observation.variance;
        }
        const double score = observation.reliability /
            std::sqrt(observation.variance) *
            HuberPsi(residual, settings.huberThreshold);
        meat += score * score;
    }
    if (bread > 0.0 && working.effectiveSupport > 1.0) {
        const double robustVariance = meat / (bread * bread);
        working.finiteSampleRobustVariance = robustVariance *
            (working.effectiveSupport /
                (working.effectiveSupport - 1.0));
        working.finiteSampleRobustVarianceAvailable =
            Finite(working.finiteSampleRobustVariance) &&
            working.finiteSampleRobustVariance >= 0.0;
    }
    working.publishedVariance = std::max(
        working.modelVariance, working.policyVariance);
    if (working.finiteSampleRobustVarianceAvailable) {
        working.publishedVariance = std::max(
            working.publishedVariance,
            working.finiteSampleRobustVariance);
    }
    if (!Finite(working.estimate) ||
        !Finite(working.modelVariance) || working.modelVariance < 0.0 ||
        !Finite(working.policyVariance) || working.policyVariance < 0.0 ||
        !Finite(working.publishedVariance) || working.publishedVariance < 0.0 ||
        !Finite(working.effectiveSupport) ||
        working.effectiveSupport <= 0.0) {
        return false;
    }
    estimate = working;
    return true;
}

} // namespace

nlohmann::json SerializeSharedBurstSettings(
    const SharedBurstSettings& settings) {
    return {
        { "schemaVersion", settings.schemaVersion },
        { "algorithmId", settings.algorithmId },
        { "algorithmVersion", settings.algorithmVersion },
        { "profile", settings.profile },
        { "exposureGroupToleranceEv", settings.exposureGroupToleranceEv },
        { "huberThreshold", settings.huberThreshold },
        { "maximumHuberIterations", settings.maximumHuberIterations },
        { "absoluteHuberTolerance", settings.absoluteHuberTolerance },
        { "relativeHuberTolerance", settings.relativeHuberTolerance }
    };
}

bool ValidateSharedBurstSettings(
    const SharedBurstSettings& settings,
    std::string* error) {
    if (settings.schemaVersion != kSharedBurstSettingsSchemaVersion ||
        settings.algorithmId != kSharedBurstAlgorithmId ||
        settings.algorithmVersion != kSharedBurstAlgorithmVersion ||
        settings.profile != "static-maximum") {
        return Fail(error, "Shared Burst settings identity is unsupported.");
    }
    if (!Finite(settings.exposureGroupToleranceEv) ||
        settings.exposureGroupToleranceEv <= 0.0 ||
        settings.exposureGroupToleranceEv > 4.0 ||
        !Finite(settings.huberThreshold) ||
        std::abs(settings.huberThreshold - 1.345) > 1.0e-12 ||
        settings.maximumHuberIterations == 0u ||
        settings.maximumHuberIterations > 12u ||
        !Finite(settings.absoluteHuberTolerance) ||
        settings.absoluteHuberTolerance < 0.0 ||
        !Finite(settings.relativeHuberTolerance) ||
        settings.relativeHuberTolerance < 0.0) {
        return Fail(error, "Shared Burst settings are outside supported bounds.");
    }
    return true;
}

bool DeserializeSharedBurstSettings(
    const nlohmann::json& value,
    SharedBurstSettings& settings,
    std::string* error) {
    if (!value.is_object()) {
        return Fail(error, "Shared Burst settings must be an object.");
    }
    SharedBurstSettings parsed;
    try {
        parsed.schemaVersion = value.at("schemaVersion").get<std::uint32_t>();
        parsed.algorithmId = value.at("algorithmId").get<std::string>();
        parsed.algorithmVersion = value.at("algorithmVersion").get<std::uint32_t>();
        parsed.profile = value.at("profile").get<std::string>();
        parsed.exposureGroupToleranceEv =
            value.at("exposureGroupToleranceEv").get<double>();
        parsed.huberThreshold = value.at("huberThreshold").get<double>();
        parsed.maximumHuberIterations =
            value.at("maximumHuberIterations").get<std::uint32_t>();
        parsed.absoluteHuberTolerance =
            value.at("absoluteHuberTolerance").get<double>();
        parsed.relativeHuberTolerance =
            value.at("relativeHuberTolerance").get<double>();
    } catch (...) {
        return Fail(error, "Shared Burst settings are incomplete or invalid.");
    }
    if (!ValidateSharedBurstSettings(parsed, error)) return false;
    settings = std::move(parsed);
    return true;
}

bool FuseSharedBurstSample(
    const FusionReferenceSample& reference,
    const std::vector<FusionCandidateSample>& candidates,
    const Parameters& preparationParameters,
    const SharedBurstSettings& settings,
    FusionPixelResult& result,
    std::string* error) {
    result = {};
    if (!ValidateParameters(preparationParameters, error) ||
        !ValidateSharedBurstSettings(settings, error)) {
        return false;
    }
    if (!reference.valid || !Finite(reference.normalizedValue)) {
        return Fail(error, "Shared Burst reference sample is unavailable.");
    }
    // The legacy backend remains the separately flagged three-alternate
    // consensus implementation for a defective temporal owner.
    if (reference.defective) {
        return FuseRobustSample(reference, candidates,
            preparationParameters, result, error);
    }
    if (reference.clipped) {
        SetExactReference(reference, DecisionReason::ReferenceClipped, result);
        return true;
    }

    double referenceValue = 0.0;
    double referenceVariance = 0.0;
    if (!ValidReference(reference, preparationParameters,
            referenceValue, referenceVariance)) {
        SetExactReference(reference, DecisionReason::NumericalFallback, result);
        return true;
    }

    if (candidates.size() + 1u >
        kSharedBurstMaximumEnabledCaptures) {
        return Fail(error,
            "Shared Burst sample exceeds the 30-capture contract.");
    }
    RejectionCounts rejectionCounts {};
    std::array<CompactObservation,
        kSharedBurstMaximumEnabledCaptures> observations {};
    std::size_t observationCount = 0u;

    CompactObservation owner;
    owner.value = referenceValue;
    owner.variance = referenceVariance;
    owner.reliability = 1.0;
    owner.sourceOrdinal = 0u;
    observations[observationCount++] = owner;

    for (const FusionCandidateSample& candidate : candidates) {
        double frozenVariance = 0.0;
        if (!candidate.valid) {
            RecordRejection(candidate.invalidReason,
                rejectionCounts, result.diagnostics);
            continue;
        }
        if (!ValidCandidate(candidate, preparationParameters,
                frozenVariance)) {
            RecordRejection(FusionRejectReason::InvalidVariance,
                rejectionCounts, result.diagnostics);
            continue;
        }
        CandidateGateInput gateInput;
        gateInput.hardValid = candidate.hardValid;
        gateInput.noiseQuality = candidate.noiseQuality;
        gateInput.reliability = candidate.reliability;
        gateInput.referenceValue = referenceValue;
        gateInput.alternateValue = candidate.value;
        gateInput.referenceGateVariance = reference.gateVariance;
        gateInput.alternateGateVariance = candidate.gateVariance;
        gateInput.referenceDarkVariance = reference.darkVariance;
        gateInput.alternateDarkVariance = candidate.darkVariance;
        gateInput.referenceDnStep = reference.effectiveDnStep;
        gateInput.alternateDnStep = candidate.effectiveDnStep;
        CandidateGateResult gate;
        std::string ignored;
        if (!EvaluateCandidateGate(gateInput, preparationParameters,
                gate, &ignored)) {
            RecordRejection(GateFailureReason(gate.failure),
                rejectionCounts, result.diagnostics);
            continue;
        }
        const double resolvedReliability = std::pow(
            std::clamp(gate.gate, 0.0, 1.0), 0.25) *
            candidate.trustAttenuation;
        if (!Finite(resolvedReliability) || resolvedReliability <= 0.0) {
            RecordRejection(FusionRejectReason::ReliabilityZero,
                rejectionCounts, result.diagnostics);
            continue;
        }
        CompactObservation observation;
        observation.value = candidate.value;
        observation.variance = frozenVariance;
        observation.reliability = resolvedReliability;
        observation.sourceOrdinal = candidate.sourceOrdinal + 1u;
        observations[observationCount++] = observation;
        ++result.diagnostics.eligibleAlternateCount;
    }
    result.diagnostics.dominantRejectionReason =
        DominantRejection(rejectionCounts);
    if (observationCount < 2u) {
        SetExactReference(reference, DecisionReason::NoValidCandidate, result);
        result.diagnostics.dominantRejectionReason =
            DominantRejection(rejectionCounts);
        result.diagnostics.rejectedAlternateCount =
            std::accumulate(rejectionCounts.begin(), rejectionCounts.end(),
                std::uint64_t { 0 });
        return true;
    }

    CompactHuberEstimate estimate;
    if (!EstimateCompactSharedPilotHuber(
            observations,
            observationCount,
            referenceValue,
            settings,
            estimate)) {
        SetExactReference(reference, DecisionReason::NumericalFallback, result);
        return true;
    }

    double totalWeight = 0.0;
    double referenceWeight = 0.0;
    double alternateWeight = 0.0;
    double robustWeighted = 0.0;
    double baseWeight = 0.0;
    double ownerWeight = -1.0;
    std::uint16_t ownerIndex = 0u;
    for (std::size_t index = 0u; index < observationCount; ++index) {
        const auto& observation = observations[index];
        const double base = observation.reliability /
            observation.variance;
        const double weight = base * estimate.robustFactors[index];
        baseWeight += base;
        robustWeighted += weight;
        totalWeight += weight;
        if (index == 0u) referenceWeight = weight;
        else alternateWeight += weight;
        if (weight > ownerWeight) {
            ownerWeight = weight;
            ownerIndex = static_cast<std::uint16_t>(std::min<std::uint64_t>(
                observation.sourceOrdinal,
                std::numeric_limits<std::uint16_t>::max()));
        }
    }
    if (!Finite(totalWeight) || totalWeight <= 0.0 ||
        !Finite(referenceWeight) || referenceWeight <= 0.0 ||
        alternateWeight / referenceWeight <
            preparationParameters.fusion.exactFallbackAlternateToReferenceRatio) {
        SetExactReference(reference,
            DecisionReason::AlternateWeightInsufficient, result);
        return true;
    }
    const double normalizedValue = estimate.estimate /
        reference.comparisonGain;
    if (!Finite(normalizedValue) ||
        std::abs(normalizedValue) >
            static_cast<double>(std::numeric_limits<float>::max())) {
        SetExactReference(reference, DecisionReason::NumericalFallback, result);
        return true;
    }

    result.valid = true;
    result.normalizedValue = normalizedValue;
    result.diagnostics.decisionReason = DecisionReason::None;
    result.diagnostics.exactReferenceCopy = false;
    result.diagnostics.referenceIncluded = true;
    result.diagnostics.contributingAlternateCount =
        static_cast<std::uint64_t>(observationCount - 1u);
    result.diagnostics.referenceWeight = referenceWeight;
    result.diagnostics.alternateWeight = alternateWeight;
    result.diagnostics.alternateToReferenceWeightRatio =
        alternateWeight / referenceWeight;
    result.diagnostics.effectiveSampleCount = estimate.effectiveSupport;
    result.diagnostics.rawSupportCount = static_cast<std::uint16_t>(
        std::min<std::size_t>(observationCount,
            std::numeric_limits<std::uint16_t>::max()));
    result.diagnostics.ownerSourceIndex = ownerIndex;
    result.diagnostics.ownerContribution = static_cast<float>(
        std::clamp(ownerWeight / totalWeight, 0.0, 1.0));
    result.diagnostics.robustAttenuation = static_cast<float>(
        baseWeight > 0.0
            ? std::clamp(robustWeighted / baseWeight, 0.0, 1.0)
            : 0.0);
    result.diagnostics.modelQuadraticVariance = static_cast<float>(
        estimate.modelVariance);
    result.diagnostics.policyConditionalSamplingVariance =
        static_cast<float>(estimate.policyVariance);
    result.diagnostics.finiteSampleRobustVariance = static_cast<float>(
        estimate.finiteSampleRobustVarianceAvailable
            ? estimate.finiteSampleRobustVariance
            : estimate.modelVariance);
    result.diagnostics.outputVarianceComparisonDomain =
        estimate.publishedVariance;
    return true;
}

bool FuseSharedBurstTile(
    const FusionTileRequest& request,
    FusionTileResult& result,
    std::string* error) {
    return FuseSharedBurstTile(
        request, SharedBurstSettings {}, result, error);
}

bool FuseSharedBurstTile(
    const FusionTileRequest& request,
    const SharedBurstSettings& settings,
    FusionTileResult& result,
    std::string* error) {
    result = {};
    result.originRawX = request.originRawX;
    result.originRawY = request.originRawY;
    result.extent = request.extent;
    if (!ValidateParameters(request.parameters, error) ||
        !ValidateSharedBurstSettings(settings, error) ||
        request.extent.width == 0u || request.extent.height == 0u ||
        request.extent.width > request.parameters.fusion.outputTileRawPixels ||
        request.extent.height > request.parameters.fusion.outputTileRawPixels ||
        request.extent.width >
            std::numeric_limits<std::uint64_t>::max() /
                request.extent.height ||
        !request.referenceProvider ||
        request.cancellationCheckRawPixels == 0u ||
        (request.alternateCount > 0u && !request.candidateProvider)) {
        return Fail(error, "Shared Burst fusion tile request is invalid.");
    }
    const std::size_t sampleCount = static_cast<std::size_t>(
        request.extent.width * request.extent.height);
    result.normalizedMosaic.resize(sampleCount);
    result.diagnostics.resize(sampleCount);
    std::vector<FusionCandidateSample> candidates(request.alternateCount);
    for (std::uint64_t localY = 0u; localY < request.extent.height; ++localY) {
        for (std::uint64_t localX = 0u; localX < request.extent.width; ++localX) {
            const std::size_t index = static_cast<std::size_t>(
                localY * request.extent.width + localX);
            if ((index % request.cancellationCheckRawPixels) == 0u &&
                request.shouldCancel && request.shouldCancel()) {
                result = {};
                return Fail(error, "Shared Burst fusion tile was canceled.");
            }
            const std::uint64_t rawX = request.originRawX + localX;
            const std::uint64_t rawY = request.originRawY + localY;
            FusionReferenceSample reference;
            if (!request.referenceProvider(rawX, rawY, reference)) {
                result = {};
                return Fail(error, "Shared Burst reference provider failed.");
            }
            for (std::size_t alternate = 0u;
                 alternate < request.alternateCount; ++alternate) {
                candidates[alternate] = {};
                request.candidateProvider(
                    alternate, rawX, rawY, candidates[alternate]);
            }
            FusionPixelResult pixel;
            if (!FuseSharedBurstSample(reference, candidates,
                    request.parameters, settings, pixel, error)) {
                result = {};
                return false;
            }
            const float output = static_cast<float>(pixel.normalizedValue);
            if (!std::isfinite(output)) {
                result = {};
                return Fail(error, "Shared Burst tile output is non-finite.");
            }
            result.normalizedMosaic[index] = output;
            result.diagnostics[index] = pixel.diagnostics;
            if (pixel.diagnostics.exactReferenceCopy) {
                ++result.exactReferencePixelCount;
            } else if (pixel.diagnostics.referenceDefectRepairDeferred) {
                ++result.deferredReferenceDefectCount;
            } else {
                ++result.fusedPixelCount;
            }
            if (pixel.diagnostics.referenceDefectReconstructed) {
                ++result.reconstructedReferenceDefectCount;
            }
        }
    }
    result.valid = true;
    result.message = "Shared Burst V1 Static Maximum fusion tile is valid.";
    return true;
}

} // namespace Raw::Mfd
