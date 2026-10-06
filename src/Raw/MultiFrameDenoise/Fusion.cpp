#include "Raw/MultiFrameDenoise/Fusion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Raw::Mfd {
namespace {

constexpr std::size_t kFusionRejectReasonCount =
    static_cast<std::size_t>(FusionRejectReason::ReferenceDefectDisagreement) + 1u;

using RejectionCounts =
    std::array<std::uint64_t, kFusionRejectReasonCount>;

bool Finite(double value) {
    return std::isfinite(value);
}

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool LowConfidenceNoise(NoiseModelQuality quality) {
    return quality == NoiseModelQuality::EstimatedBurst ||
        quality == NoiseModelQuality::GenericLowConfidence;
}

bool ValidNoiseQuality(NoiseModelQuality quality) {
    switch (quality) {
        case NoiseModelQuality::TrustedMetadata:
        case NoiseModelQuality::CalibratedCamera:
        case NoiseModelQuality::EstimatedBurst:
        case NoiseModelQuality::GenericLowConfidence:
        case NoiseModelQuality::Unavailable:
            return true;
    }
    return false;
}

bool ValidRejectReason(FusionRejectReason reason) {
    return static_cast<std::size_t>(reason) < kFusionRejectReasonCount;
}

void RecordRejection(
    FusionRejectReason reason,
    RejectionCounts& counts,
    FusionPixelDiagnostics& diagnostics) {
    if (!ValidRejectReason(reason) || reason == FusionRejectReason::None) {
        reason = FusionRejectReason::CandidateUnavailable;
    }
    ++counts[static_cast<std::size_t>(reason)];
    ++diagnostics.rejectedAlternateCount;
}

FusionRejectReason DominantRejection(const RejectionCounts& counts) {
    FusionRejectReason dominant = FusionRejectReason::None;
    std::uint64_t largest = 0u;
    for (std::size_t index = 1u; index < counts.size(); ++index) {
        if (counts[index] > largest) {
            largest = counts[index];
            dominant = static_cast<FusionRejectReason>(index);
        }
    }
    return dominant;
}

FusionRejectReason GateFailureReason(CandidateGateFailure failure) {
    switch (failure) {
        case CandidateGateFailure::None:
            return FusionRejectReason::None;
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

double DivisionVarianceFloor(double effectiveDnStep, const Parameters& parameters) {
    if (!Finite(effectiveDnStep) || effectiveDnStep < 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double quantizationFloor =
        effectiveDnStep * effectiveDnStep / 12.0;
    return std::max(
        parameters.fusion.numericalVarianceFloor,
        quantizationFloor);
}

double VarianceTolerance(double left, double right, const Parameters& parameters) {
    return 64.0 * std::numeric_limits<double>::epsilon() * std::max({
        std::abs(left),
        std::abs(right),
        parameters.fusion.numericalVarianceFloor
    });
}

bool ValidCandidateMeasurement(
    const FusionCandidateSample& candidate,
    const Parameters& parameters) {
    if (!Finite(candidate.value) || !Finite(candidate.gateVariance) ||
        !Finite(candidate.fusionVariance) || !Finite(candidate.darkVariance) ||
        !Finite(candidate.effectiveDnStep) || candidate.gateVariance <= 0.0 ||
        candidate.fusionVariance <= 0.0 || candidate.darkVariance < 0.0 ||
        candidate.effectiveDnStep < 0.0) {
        return false;
    }
    return candidate.fusionVariance + VarianceTolerance(
        candidate.fusionVariance,
        candidate.gateVariance,
        parameters) >= candidate.gateVariance;
}

void SetExactReference(
    const FusionReferenceSample& reference,
    DecisionReason reason,
    FusionPixelResult& result) {
    result.valid = true;
    result.normalizedValue = reference.normalizedValue;
    result.diagnostics.decisionReason = reason;
    result.diagnostics.exactReferenceCopy = true;
    result.diagnostics.referenceIncluded = true;
    result.diagnostics.contributingAlternateCount = 0u;
    result.diagnostics.effectiveSampleCount = 1.0;
    if (Finite(reference.fusionVariance) && reference.fusionVariance >= 0.0) {
        result.diagnostics.outputVarianceComparisonDomain =
            reference.fusionVariance;
    }
}

void SetDeferredReferenceDefect(
    const FusionReferenceSample& reference,
    FusionPixelResult& result) {
    result.valid = true;
    result.normalizedValue = reference.normalizedValue;
    result.diagnostics.decisionReason = DecisionReason::ReferenceDefectDeferred;
    result.diagnostics.exactReferenceCopy = false;
    result.diagnostics.referenceIncluded = false;
    result.diagnostics.referenceDefectRepairDeferred = true;
    result.diagnostics.contributingAlternateCount = 0u;
    result.diagnostics.referenceWeight = 0.0;
    result.diagnostics.alternateWeight = 0.0;
    result.diagnostics.alternateToReferenceWeightRatio = 0.0;
    result.diagnostics.effectiveSampleCount = 0.0;
    result.diagnostics.outputVarianceComparisonDomain = 0.0;
}

struct DefectCandidate {
    std::size_t stableIndex = 0u;
    const FusionCandidateSample* sample = nullptr;
    double weight = 0.0;
};

bool FuseReferenceDefect(
    const FusionReferenceSample& reference,
    const std::vector<FusionCandidateSample>& candidates,
    const Parameters& parameters,
    RejectionCounts& rejectionCounts,
    FusionPixelResult& result) {
    std::vector<DefectCandidate> eligible;
    eligible.reserve(candidates.size());
    for (std::size_t index = 0u; index < candidates.size(); ++index) {
        const FusionCandidateSample& candidate = candidates[index];
        if (!candidate.valid) {
            RecordRejection(
                candidate.invalidReason, rejectionCounts, result.diagnostics);
            continue;
        }
        if (!candidate.hardValid) {
            RecordRejection(
                FusionRejectReason::HardInvalid,
                rejectionCounts,
                result.diagnostics);
            continue;
        }
        if (!ValidNoiseQuality(candidate.noiseQuality)) {
            RecordRejection(
                FusionRejectReason::InvalidNumericInput,
                rejectionCounts,
                result.diagnostics);
            continue;
        }
        if (candidate.noiseQuality == NoiseModelQuality::Unavailable) {
            RecordRejection(
                FusionRejectReason::NoiseUnavailable,
                rejectionCounts,
                result.diagnostics);
            continue;
        }
        if (!ValidCandidateMeasurement(candidate, parameters)) {
            RecordRejection(
                FusionRejectReason::InvalidVariance,
                rejectionCounts,
                result.diagnostics);
            continue;
        }
        if (!Finite(candidate.reliability) || candidate.reliability < 0.0 ||
            candidate.reliability > 1.0 ||
            !Finite(candidate.referenceDefectGate) ||
            candidate.referenceDefectGate < 0.0 ||
            candidate.referenceDefectGate > candidate.reliability) {
            RecordRejection(
                FusionRejectReason::InvalidNumericInput,
                rejectionCounts,
                result.diagnostics);
            continue;
        }
        if (candidate.referenceDefectGate <
                parameters.fusion.referenceDefectMinimumGate) {
            RecordRejection(
                FusionRejectReason::ReferenceDefectGateInsufficient,
                rejectionCounts,
                result.diagnostics);
            continue;
        }
        const double divisionFloor = DivisionVarianceFloor(
            candidate.effectiveDnStep, parameters);
        if (!Finite(divisionFloor) ||
            divisionFloor <= 0.0) {
            RecordRejection(
                FusionRejectReason::InvalidVariance,
                rejectionCounts,
                result.diagnostics);
            continue;
        }
        const double divisionVariance = std::max(
            candidate.fusionVariance, divisionFloor);
        const double weight = 1.0 / divisionVariance;
        if (!Finite(weight) || weight <= 0.0) {
            RecordRejection(
                FusionRejectReason::InvalidWeight,
                rejectionCounts,
                result.diagnostics);
            continue;
        }
        eligible.push_back({ index, &candidate, weight });
    }
    result.diagnostics.eligibleAlternateCount =
        static_cast<std::uint64_t>(eligible.size());
    const std::size_t minimum =
        parameters.fusion.referenceDefectMinimumAlternates;
    if (eligible.size() < minimum) {
        SetDeferredReferenceDefect(reference, result);
        return true;
    }

    std::vector<DefectCandidate> survivors = eligible;
    if (survivors.size() > minimum) {
        std::stable_sort(
            survivors.begin(), survivors.end(),
            [](const DefectCandidate& left, const DefectCandidate& right) {
                if (left.sample->value != right.sample->value) {
                    return left.sample->value < right.sample->value;
                }
                return left.stableIndex < right.stableIndex;
            });
        double totalWeight = 0.0;
        for (const DefectCandidate& candidate : survivors) {
            totalWeight += candidate.weight;
        }
        if (!Finite(totalWeight) || totalWeight <= 0.0) {
            SetDeferredReferenceDefect(reference, result);
            return true;
        }
        const double halfWeight = 0.5 * totalWeight;
        double cumulative = 0.0;
        const DefectCandidate* pilot = &survivors.back();
        for (const DefectCandidate& candidate : survivors) {
            cumulative += candidate.weight;
            if (cumulative >= halfWeight) {
                pilot = &candidate;
                break;
            }
        }
        std::vector<DefectCandidate> filtered;
        filtered.reserve(survivors.size());
        for (const DefectCandidate& candidate : survivors) {
            const double variance = candidate.sample->fusionVariance +
                pilot->sample->fusionVariance +
                parameters.fusion.numericalVarianceFloor;
            const double standardized = variance > 0.0 && Finite(variance)
                ? std::abs(candidate.sample->value - pilot->sample->value) /
                    std::sqrt(variance)
                : std::numeric_limits<double>::infinity();
            if (Finite(standardized) && standardized <
                parameters.fusion.referenceDefectAgreementSigma) {
                filtered.push_back(candidate);
            } else {
                RecordRejection(
                    FusionRejectReason::ReferenceDefectDisagreement,
                    rejectionCounts,
                    result.diagnostics);
            }
        }
        survivors = std::move(filtered);
    }

    if (survivors.size() < minimum) {
        SetDeferredReferenceDefect(reference, result);
        return true;
    }
    for (std::size_t left = 0u; left < survivors.size(); ++left) {
        for (std::size_t right = left + 1u; right < survivors.size(); ++right) {
            const double variance =
                survivors[left].sample->fusionVariance +
                survivors[right].sample->fusionVariance +
                parameters.fusion.numericalVarianceFloor;
            const double standardized = variance > 0.0 && Finite(variance)
                ? std::abs(
                    survivors[left].sample->value -
                    survivors[right].sample->value) / std::sqrt(variance)
                : std::numeric_limits<double>::infinity();
            if (!Finite(standardized) || standardized >=
                parameters.fusion.referenceDefectAgreementSigma) {
                RecordRejection(
                    FusionRejectReason::ReferenceDefectDisagreement,
                    rejectionCounts,
                    result.diagnostics);
                SetDeferredReferenceDefect(reference, result);
                return true;
            }
        }
    }

    double numerator = 0.0;
    double denominator = 0.0;
    double weightSquares = 0.0;
    double varianceNumerator = 0.0;
    for (const DefectCandidate& candidate : survivors) {
        const double weight = candidate.weight;
        numerator += weight * candidate.sample->value;
        denominator += weight;
        weightSquares += weight * weight;
        varianceNumerator +=
            weight * weight * candidate.sample->fusionVariance;
    }
    if (!Finite(numerator) || !Finite(denominator) || denominator <= 0.0 ||
        !Finite(weightSquares) || weightSquares <= 0.0 ||
        !Finite(varianceNumerator) || varianceNumerator < 0.0) {
        SetDeferredReferenceDefect(reference, result);
        return true;
    }
    const double comparisonValue = numerator / denominator;
    const double normalizedValue = comparisonValue / reference.comparisonGain;
    const double effectiveSampleCount =
        denominator * denominator / weightSquares;
    const double outputVariance =
        varianceNumerator / (denominator * denominator);
    if (!Finite(normalizedValue) ||
        std::abs(normalizedValue) >
            static_cast<double>(std::numeric_limits<float>::max()) ||
        !Finite(effectiveSampleCount) ||
        effectiveSampleCount <= 0.0 || !Finite(outputVariance) ||
        outputVariance < 0.0) {
        SetDeferredReferenceDefect(reference, result);
        return true;
    }
    result.valid = true;
    result.normalizedValue = normalizedValue;
    result.diagnostics.decisionReason = DecisionReason::None;
    result.diagnostics.exactReferenceCopy = false;
    result.diagnostics.referenceIncluded = false;
    result.diagnostics.referenceDefectReconstructed = true;
    result.diagnostics.referenceDefectRepairDeferred = false;
    result.diagnostics.contributingAlternateCount =
        static_cast<std::uint64_t>(survivors.size());
    result.diagnostics.referenceWeight = 0.0;
    result.diagnostics.alternateWeight = denominator;
    result.diagnostics.alternateToReferenceWeightRatio = 0.0;
    result.diagnostics.effectiveSampleCount = effectiveSampleCount;
    result.diagnostics.outputVarianceComparisonDomain = outputVariance;
    return true;
}

} // namespace

const char* FusionRejectReasonName(FusionRejectReason reason) {
    switch (reason) {
        case FusionRejectReason::None: return "none";
        case FusionRejectReason::CandidateUnavailable: return "candidate-unavailable";
        case FusionRejectReason::HardInvalid: return "hard-invalid";
        case FusionRejectReason::NoiseUnavailable: return "noise-unavailable";
        case FusionRejectReason::InvalidNumericInput: return "invalid-numeric-input";
        case FusionRejectReason::ReliabilityZero: return "reliability-zero";
        case FusionRejectReason::PixelOutlier: return "pixel-outlier";
        case FusionRejectReason::AbsoluteSafetyFailure: return "absolute-safety-failure";
        case FusionRejectReason::InvalidVariance: return "invalid-variance";
        case FusionRejectReason::InvalidWeight: return "invalid-weight";
        case FusionRejectReason::ReferenceDefectGateInsufficient:
            return "reference-defect-gate-insufficient";
        case FusionRejectReason::ReferenceDefectDisagreement:
            return "reference-defect-disagreement";
    }
    return "invalid-numeric-input";
}

bool FuseRobustSample(
    const FusionReferenceSample& reference,
    const std::vector<FusionCandidateSample>& candidates,
    const Parameters& parameters,
    FusionPixelResult& result,
    std::string* error) {
    result = {};
    if (!ValidateParameters(parameters, error)) return false;
    if (!reference.valid || !Finite(reference.normalizedValue) ||
        std::abs(reference.normalizedValue) >
            static_cast<double>(std::numeric_limits<float>::max())) {
        return Fail(error, "MFD fusion reference sample is unavailable.");
    }

    RejectionCounts rejectionCounts {};
    if (reference.clipped) {
        SetExactReference(reference, DecisionReason::ReferenceClipped, result);
        return true;
    }
    if (!Finite(reference.comparisonGain) ||
        reference.comparisonGain <= 0.0 ||
        reference.comparisonGain > parameters.radiometric.maximumComparisonGain) {
        SetExactReference(reference, DecisionReason::InvalidReferenceGain, result);
        return true;
    }
    if (reference.defective) {
        const bool fused = FuseReferenceDefect(
            reference,
            candidates,
            parameters,
            rejectionCounts,
            result);
        result.diagnostics.dominantRejectionReason =
            DominantRejection(rejectionCounts);
        return fused;
    }
    if (!ValidNoiseQuality(reference.noiseQuality) ||
        reference.noiseQuality == NoiseModelQuality::Unavailable) {
        SetExactReference(reference, DecisionReason::NoiseModelUnavailable, result);
        return true;
    }
    const double referenceValue =
        reference.comparisonGain * reference.normalizedValue;
    const double referenceDivisionFloor = DivisionVarianceFloor(
        reference.effectiveDnStep, parameters);
    if (!Finite(referenceValue) || !Finite(reference.gateVariance) ||
        !Finite(reference.fusionVariance) || !Finite(reference.darkVariance) ||
        reference.gateVariance <= 0.0 || reference.fusionVariance <= 0.0 ||
        reference.darkVariance < 0.0 || !Finite(referenceDivisionFloor) ||
        referenceDivisionFloor <= 0.0 ||
        std::abs(reference.gateVariance - reference.fusionVariance) >
            VarianceTolerance(
                reference.gateVariance,
                reference.fusionVariance,
                parameters)) {
        SetExactReference(reference, DecisionReason::NumericalFallback, result);
        return true;
    }
    const double referenceDivisionVariance = std::max(
        reference.fusionVariance, referenceDivisionFloor);
    const double referenceWeight = 1.0 / referenceDivisionVariance;
    if (!Finite(referenceWeight) || referenceWeight <= 0.0) {
        SetExactReference(reference, DecisionReason::NumericalFallback, result);
        return true;
    }
    result.diagnostics.referenceWeight = referenceWeight;

    double alternateNumerator = 0.0;
    double alternateWeight = 0.0;
    double alternateWeightSquares = 0.0;
    double alternateVarianceNumerator = 0.0;
    bool acceptedLowConfidenceNoise = false;
    for (const FusionCandidateSample& candidate : candidates) {
        if (!candidate.valid) {
            RecordRejection(
                candidate.invalidReason, rejectionCounts, result.diagnostics);
            continue;
        }
        if (!ValidNoiseQuality(candidate.noiseQuality)) {
            RecordRejection(
                FusionRejectReason::InvalidNumericInput,
                rejectionCounts,
                result.diagnostics);
            continue;
        }
        if (!ValidCandidateMeasurement(candidate, parameters)) {
            RecordRejection(
                FusionRejectReason::InvalidVariance,
                rejectionCounts,
                result.diagnostics);
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
        if (!EvaluateCandidateGate(gateInput, parameters, gate, &ignored)) {
            RecordRejection(
                GateFailureReason(gate.failure),
                rejectionCounts,
                result.diagnostics);
            continue;
        }
        const double divisionFloor = DivisionVarianceFloor(
            candidate.effectiveDnStep, parameters);
        if (!Finite(divisionFloor) ||
            divisionFloor <= 0.0) {
            RecordRejection(
                FusionRejectReason::InvalidVariance,
                rejectionCounts,
                result.diagnostics);
            continue;
        }
        const double divisionVariance = std::max(
            candidate.fusionVariance, divisionFloor);
        double fusionGate = gate.gate;
        if (parameters.fusion.method == "weighted-average") {
            // Smoothing changes how strongly an already-safe sample is
            // discounted by spatial confidence. It never revives a rejected
            // sample and leaves the redescending per-pixel outlier gate intact.
            // At the maximum, a fourth-root confidence curve lets static,
            // lower-confidence frames contribute meaningfully without giving
            // them the same authority as a fully trusted sample.
            const double reliabilityExponent =
                1.0 - 0.75 * parameters.fusion.smoothing;
            const double softenedReliability = std::pow(
                std::clamp(candidate.reliability, 0.0, 1.0),
                reliabilityExponent);
            fusionGate = softenedReliability * gate.pixelGate;
        }
        double weight = fusionGate / divisionVariance;
        if (!Finite(weight) || weight <= 0.0) {
            RecordRejection(
                FusionRejectReason::InvalidWeight,
                rejectionCounts,
                result.diagnostics);
            continue;
        }
        const double individualCap =
            parameters.fusion.oneAlternateWeightCapRelativeToReference *
            referenceWeight;
        if (!Finite(individualCap) || individualCap <= 0.0) {
            SetExactReference(reference, DecisionReason::NumericalFallback, result);
            return true;
        }
        if (weight > individualCap) {
            weight = individualCap;
            ++result.diagnostics.individuallyCappedAlternateCount;
        }
        alternateNumerator += weight * candidate.value;
        alternateWeight += weight;
        alternateWeightSquares += weight * weight;
        alternateVarianceNumerator +=
            weight * weight * candidate.fusionVariance;
        ++result.diagnostics.eligibleAlternateCount;
        acceptedLowConfidenceNoise = acceptedLowConfidenceNoise ||
            LowConfidenceNoise(candidate.noiseQuality);
    }
    result.diagnostics.dominantRejectionReason =
        DominantRejection(rejectionCounts);

    if (!Finite(alternateNumerator) || !Finite(alternateWeight) ||
        alternateWeight < 0.0 || !Finite(alternateWeightSquares) ||
        alternateWeightSquares < 0.0 ||
        !Finite(alternateVarianceNumerator) ||
        alternateVarianceNumerator < 0.0) {
        SetExactReference(reference, DecisionReason::NumericalFallback, result);
        return true;
    }
    if (acceptedLowConfidenceNoise) {
        // A mixed-quality candidate set uses the conservative policy: any
        // contributing estimated/generic model activates the total pool cap.
        const double totalCap =
            parameters.fusion.lowConfidenceTotalWeightCapRelativeToReference *
            referenceWeight;
        if (!Finite(totalCap) || totalCap <= 0.0) {
            SetExactReference(reference, DecisionReason::NumericalFallback, result);
            return true;
        }
        if (alternateWeight > totalCap) {
            const double scale = totalCap / alternateWeight;
            alternateNumerator *= scale;
            alternateWeight *= scale;
            alternateWeightSquares *= scale * scale;
            alternateVarianceNumerator *= scale * scale;
            result.diagnostics.lowConfidenceTotalCapApplied = true;
        }
    }
    result.diagnostics.alternateWeight = alternateWeight;
    result.diagnostics.alternateToReferenceWeightRatio =
        alternateWeight / referenceWeight;
    if (!Finite(result.diagnostics.alternateToReferenceWeightRatio)) {
        SetExactReference(reference, DecisionReason::NumericalFallback, result);
        return true;
    }
    if (result.diagnostics.eligibleAlternateCount == 0u ||
        alternateWeight <= 0.0) {
        SetExactReference(reference, DecisionReason::NoValidCandidate, result);
        return true;
    }
    if (result.diagnostics.alternateToReferenceWeightRatio <
        parameters.fusion.exactFallbackAlternateToReferenceRatio) {
        SetExactReference(
            reference,
            DecisionReason::AlternateWeightInsufficient,
            result);
        return true;
    }

    const double numerator =
        referenceWeight * referenceValue + alternateNumerator;
    const double denominator = referenceWeight + alternateWeight;
    const double weightSquares =
        referenceWeight * referenceWeight + alternateWeightSquares;
    const double varianceNumerator =
        referenceWeight * referenceWeight * reference.fusionVariance +
        alternateVarianceNumerator;
    if (!Finite(numerator) || !Finite(denominator) ||
        denominator < referenceWeight || !Finite(weightSquares) ||
        weightSquares <= 0.0 || !Finite(varianceNumerator) ||
        varianceNumerator < 0.0) {
        SetExactReference(reference, DecisionReason::NumericalFallback, result);
        return true;
    }
    const double comparisonValue = numerator / denominator;
    const double normalizedValue = comparisonValue / reference.comparisonGain;
    const double effectiveSampleCount =
        denominator * denominator / weightSquares;
    const double outputVariance =
        varianceNumerator / (denominator * denominator);
    if (!Finite(normalizedValue) ||
        std::abs(normalizedValue) >
            static_cast<double>(std::numeric_limits<float>::max()) ||
        !Finite(effectiveSampleCount) ||
        effectiveSampleCount <= 0.0 || !Finite(outputVariance) ||
        outputVariance < 0.0) {
        SetExactReference(reference, DecisionReason::NumericalFallback, result);
        return true;
    }

    result.valid = true;
    result.normalizedValue = normalizedValue;
    result.diagnostics.decisionReason = DecisionReason::None;
    result.diagnostics.exactReferenceCopy = false;
    result.diagnostics.referenceIncluded = true;
    result.diagnostics.contributingAlternateCount =
        result.diagnostics.eligibleAlternateCount;
    result.diagnostics.effectiveSampleCount = effectiveSampleCount;
    result.diagnostics.outputVarianceComparisonDomain = outputVariance;
    return true;
}

bool FuseRobustTile(
    const FusionTileRequest& request,
    FusionTileResult& result,
    std::string* error) {
    result = {};
    result.originRawX = request.originRawX;
    result.originRawY = request.originRawY;
    result.extent = request.extent;
    if (!ValidateParameters(request.parameters, error)) return false;
    if (request.extent.width == 0u || request.extent.height == 0u ||
        request.extent.width > request.parameters.fusion.outputTileRawPixels ||
        request.extent.height > request.parameters.fusion.outputTileRawPixels ||
        request.extent.width >
            std::numeric_limits<std::uint64_t>::max() /
                request.extent.height ||
        request.originRawX > std::numeric_limits<std::uint64_t>::max() -
            request.extent.width ||
        request.originRawY > std::numeric_limits<std::uint64_t>::max() -
            request.extent.height ||
        !request.referenceProvider ||
        request.cancellationCheckRawPixels == 0u ||
        (request.alternateCount > 0u && !request.candidateProvider)) {
        return Fail(error, "MFD fusion tile request is invalid.");
    }
    const std::uint64_t sampleCount64 =
        request.extent.width * request.extent.height;
    if (sampleCount64 > std::numeric_limits<std::size_t>::max()) {
        return Fail(error, "MFD fusion tile allocation would overflow.");
    }
    const std::size_t sampleCount = static_cast<std::size_t>(sampleCount64);
    result.normalizedMosaic.resize(sampleCount);
    result.diagnostics.resize(sampleCount);
    std::vector<FusionCandidateSample> candidates(request.alternateCount);

    for (std::uint64_t localY = 0u; localY < request.extent.height; ++localY) {
        for (std::uint64_t localX = 0u; localX < request.extent.width; ++localX) {
            const std::uint64_t rawX = request.originRawX + localX;
            const std::uint64_t rawY = request.originRawY + localY;
            const std::size_t index = static_cast<std::size_t>(
                localY * request.extent.width + localX);
            if ((index % request.cancellationCheckRawPixels) == 0u &&
                request.shouldCancel && request.shouldCancel()) {
                result = {};
                return Fail(error, "MFD fusion tile was canceled.");
            }
            FusionReferenceSample reference;
            if (!request.referenceProvider(rawX, rawY, reference)) {
                result = {};
                return Fail(error, "MFD fusion reference provider failed.");
            }
            for (std::size_t alternate = 0u;
                 alternate < request.alternateCount;
                 ++alternate) {
                candidates[alternate] = {};
                if (!request.candidateProvider(
                        alternate, rawX, rawY, candidates[alternate])) {
                    candidates[alternate] = {};
                }
            }
            FusionPixelResult pixel;
            if (!FuseRobustSample(
                    reference,
                    candidates,
                    request.parameters,
                    pixel,
                    error)) {
                result = {};
                return false;
            }
            const float output = static_cast<float>(pixel.normalizedValue);
            if (!std::isfinite(output)) {
                result = {};
                return Fail(error, "MFD fusion tile output is not finite float32.");
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
    if (request.shouldCancel && request.shouldCancel()) {
        result = {};
        return Fail(error, "MFD fusion tile was canceled.");
    }
    result.valid = true;
    result.message = "MFD robust inverse-variance fusion tile is valid.";
    return true;
}

} // namespace Raw::Mfd
