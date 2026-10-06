#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrameDenoise/Fusion.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "MFD Phase 7 validation failed: " << message << std::endl;
    }
    return condition;
}

bool NearlyEqual(double left, double right, double tolerance = 1.0e-12) {
    return std::abs(left - right) <= tolerance;
}

std::uint64_t DoubleBits(double value) {
    std::uint64_t bits = 0u;
    static_assert(sizeof(bits) == sizeof(value), "unexpected double width");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

Raw::Mfd::FusionReferenceSample MakeReference(
    double normalizedValue = 0.20,
    double variance = 0.04,
    double comparisonGain = 1.0) {
    Raw::Mfd::FusionReferenceSample sample;
    sample.valid = true;
    sample.noiseQuality = Raw::Mfd::NoiseModelQuality::TrustedMetadata;
    sample.normalizedValue = normalizedValue;
    sample.comparisonGain = comparisonGain;
    sample.gateVariance = variance;
    sample.fusionVariance = variance;
    sample.darkVariance = 0.25 * variance;
    sample.effectiveDnStep = 1.0e-4;
    return sample;
}

Raw::Mfd::FusionCandidateSample MakeCandidate(
    double value,
    double variance,
    Raw::Mfd::NoiseModelQuality quality =
        Raw::Mfd::NoiseModelQuality::TrustedMetadata) {
    Raw::Mfd::FusionCandidateSample sample;
    sample.valid = true;
    sample.hardValid = true;
    sample.noiseQuality = quality;
    sample.value = value;
    sample.gateVariance = variance;
    sample.fusionVariance = variance;
    sample.darkVariance = 0.25 * variance;
    sample.effectiveDnStep = 1.0e-4;
    sample.reliability = 1.0;
    sample.referenceDefectGate = 1.0;
    return sample;
}

bool Fuse(
    const Raw::Mfd::FusionReferenceSample& reference,
    const std::vector<Raw::Mfd::FusionCandidateSample>& candidates,
    Raw::Mfd::FusionPixelResult& result,
    Raw::Mfd::Parameters parameters = {}) {
    std::string error;
    return Check(Raw::Mfd::FuseRobustSample(
            reference, candidates, parameters, result, &error),
        "fusion sample failed: " + error);
}

bool ValidateEqualAndUnequalNoise() {
    bool ok = true;
    constexpr std::size_t frameCount = 8u;
    const auto reference = MakeReference(0.25, 0.04);
    std::vector<Raw::Mfd::FusionCandidateSample> equalCandidates(
        frameCount - 1u,
        MakeCandidate(0.25, 0.04));
    Raw::Mfd::FusionPixelResult equal;
    ok &= Fuse(reference, equalCandidates, equal);
    ok &= Check(equal.valid && !equal.diagnostics.exactReferenceCopy &&
            equal.diagnostics.contributingAlternateCount == frameCount - 1u &&
            NearlyEqual(equal.normalizedValue, 0.25) &&
            NearlyEqual(equal.diagnostics.effectiveSampleCount, 8.0) &&
            NearlyEqual(
                equal.diagnostics.outputVarianceComparisonDomain,
                0.04 / 8.0) &&
            NearlyEqual(
                std::sqrt(equal.diagnostics.outputVarianceComparisonDomain) /
                    std::sqrt(reference.fusionVariance),
                1.0 / std::sqrt(8.0)),
        "equal-noise fusion did not produce the expected 1/sqrt(N) behavior");

    Raw::Mfd::FusionPixelResult unequal;
    ok &= Fuse(
        MakeReference(0.20, 0.04),
        { MakeCandidate(0.30, 0.01) },
        unequal);
    ok &= Check(NearlyEqual(unequal.normalizedValue, 0.28) &&
            NearlyEqual(
                unequal.diagnostics.outputVarianceComparisonDomain,
                0.008) &&
            NearlyEqual(
                unequal.diagnostics.alternateToReferenceWeightRatio,
                4.0),
        "unequal-noise inverse-variance weighting is not optimal");

    std::vector<Raw::Mfd::FusionCandidateSample> symmetric {
        MakeCandidate(0.17, 0.04),
        MakeCandidate(0.23, 0.04)
    };
    Raw::Mfd::FusionPixelResult unbiased;
    ok &= Fuse(MakeReference(0.20, 0.04), symmetric, unbiased);
    ok &= Check(NearlyEqual(unbiased.normalizedValue, 0.20) &&
            NearlyEqual(unbiased.diagnostics.effectiveSampleCount, 3.0),
        "equal common-pilot variances rewarded an observation-dependent excursion");
    return ok;
}

bool ValidateCapsAndExactFallback() {
    bool ok = true;
    Raw::Mfd::FusionPixelResult individuallyCapped;
    ok &= Fuse(
        MakeReference(0.20, 0.04),
        { MakeCandidate(0.21, 1.0e-5) },
        individuallyCapped);
    ok &= Check(
        individuallyCapped.diagnostics.individuallyCappedAlternateCount == 1u &&
            NearlyEqual(
                individuallyCapped.diagnostics.alternateToReferenceWeightRatio,
                4.0),
        "one alternate exceeded the four-reference-weight cap");
    Raw::Mfd::Parameters tighterCap;
    tighterCap.fusion.oneAlternateWeightCapRelativeToReference = 2.0;
    Raw::Mfd::FusionPixelResult parameterizedCap;
    ok &= Fuse(
        MakeReference(0.20, 0.04),
        { MakeCandidate(0.21, 1.0e-5) },
        parameterizedCap,
        tighterCap);
    ok &= Check(NearlyEqual(
            parameterizedCap.diagnostics.alternateToReferenceWeightRatio,
            2.0),
        "the serialized one-alternate fusion cap was not consumed");

    std::vector<Raw::Mfd::FusionCandidateSample> lowConfidence(3u);
    for (auto& candidate : lowConfidence) {
        candidate = MakeCandidate(
            0.20,
            1.0e-5,
            Raw::Mfd::NoiseModelQuality::GenericLowConfidence);
    }
    Raw::Mfd::FusionPixelResult totalCapped;
    ok &= Fuse(MakeReference(0.20, 0.04), lowConfidence, totalCapped);
    ok &= Check(totalCapped.diagnostics.lowConfidenceTotalCapApplied &&
            NearlyEqual(
                totalCapped.diagnostics.alternateToReferenceWeightRatio,
                8.0),
        "low-confidence candidates exceeded the eight-reference-weight total cap");

    const float exactReferenceValue = 0.234567f;
    const auto exactReference = MakeReference(
        static_cast<double>(exactReferenceValue), 0.01);
    Raw::Mfd::FusionPixelResult insufficient;
    ok &= Fuse(
        exactReference,
        { MakeCandidate(exactReference.normalizedValue, 1.0) },
        insufficient);
    ok &= Check(insufficient.diagnostics.exactReferenceCopy &&
            insufficient.diagnostics.decisionReason ==
                Raw::Mfd::DecisionReason::AlternateWeightInsufficient &&
            DoubleBits(insufficient.normalizedValue) ==
                DoubleBits(exactReference.normalizedValue),
        "sub-threshold alternate weight did not copy the exact reference branch");

    Raw::Mfd::FusionCandidateSample unavailable;
    Raw::Mfd::FusionPixelResult none;
    ok &= Fuse(exactReference, { unavailable }, none);
    ok &= Check(none.diagnostics.exactReferenceCopy &&
            none.diagnostics.decisionReason ==
                Raw::Mfd::DecisionReason::NoValidCandidate &&
            none.diagnostics.dominantRejectionReason ==
                Raw::Mfd::FusionRejectReason::CandidateUnavailable,
        "all-alternate failure did not return an explained exact copy");

    auto clippedReference = exactReference;
    clippedReference.clipped = true;
    Raw::Mfd::FusionPixelResult clipped;
    ok &= Fuse(
        clippedReference,
        { MakeCandidate(0.10, 0.001) },
        clipped);
    ok &= Check(clipped.diagnostics.exactReferenceCopy &&
            clipped.diagnostics.decisionReason ==
                Raw::Mfd::DecisionReason::ReferenceClipped &&
            clipped.normalizedValue == exactReference.normalizedValue,
        "clipped reference policy did not preserve the exact captured sample");

    Raw::Mfd::FusionPixelResult moving;
    ok &= Fuse(
        MakeReference(0.20, 1.0e-4),
        { MakeCandidate(0.80, 1.0e-4) },
        moving);
    ok &= Check(moving.diagnostics.exactReferenceCopy &&
            moving.diagnostics.dominantRejectionReason ==
                Raw::Mfd::FusionRejectReason::PixelOutlier,
        "a moving candidate did not fall locally to the reference");

    auto invalidVariance = MakeCandidate(0.20, 0.01);
    invalidVariance.fusionVariance = 0.001;
    Raw::Mfd::FusionPixelResult invalidVarianceResult;
    ok &= Fuse(
        MakeReference(0.20, 0.01),
        { invalidVariance },
        invalidVarianceResult);
    ok &= Check(invalidVarianceResult.diagnostics.exactReferenceCopy &&
            invalidVarianceResult.diagnostics.dominantRejectionReason ==
                Raw::Mfd::FusionRejectReason::InvalidVariance,
        "fusion variance below gate variance did not fail closed");

    auto inconsistentReference = MakeReference(0.20, 0.01);
    inconsistentReference.fusionVariance = 0.02;
    Raw::Mfd::FusionPixelResult inconsistentReferenceResult;
    ok &= Fuse(
        inconsistentReference,
        { MakeCandidate(0.20, 0.01) },
        inconsistentReferenceResult);
    ok &= Check(inconsistentReferenceResult.diagnostics.exactReferenceCopy &&
            inconsistentReferenceResult.diagnostics.decisionReason ==
                Raw::Mfd::DecisionReason::NumericalFallback,
        "reference gate/fusion variance mismatch did not return the reference");

    Raw::Mfd::Parameters invalidParameters;
    invalidParameters.fusion.accumulatorPrecision = "float32";
    Raw::Mfd::FusionPixelResult invalidResult;
    std::string invalidError;
    ok &= Check(!Raw::Mfd::FuseRobustSample(
            MakeReference(),
            { MakeCandidate(0.20, 0.04) },
            invalidParameters,
            invalidResult,
            &invalidError),
        "an unsupported accumulator precision crossed the public fusion boundary");
    return ok;
}

bool ValidateWeightedAverageStrength() {
    bool ok = true;
    const auto reference = MakeReference(0.20, 0.04);
    std::vector<Raw::Mfd::FusionCandidateSample> candidates(
        4u,
        MakeCandidate(0.20, 0.04));
    for (auto& candidate : candidates) {
        candidate.reliability = 0.15;
    }

    Raw::Mfd::Parameters robustParameters;
    robustParameters.fusion.method = "robust";
    Raw::Mfd::FusionPixelResult robust;
    ok &= Fuse(reference, candidates, robust, robustParameters);

    Raw::Mfd::Parameters weightedParameters;
    weightedParameters.fusion.method = "weighted-average";
    weightedParameters.fusion.smoothing = 0.70;
    Raw::Mfd::FusionPixelResult weighted;
    ok &= Fuse(reference, candidates, weighted, weightedParameters);
    ok &= Check(
        weighted.diagnostics.contributingAlternateCount == 4u &&
            weighted.diagnostics.effectiveSampleCount >
                robust.diagnostics.effectiveSampleCount + 1.5 &&
            weighted.diagnostics.effectiveSampleCount <= 5.0,
        "weighted-average smoothing did not turn safe coverage into stronger effective averaging");

    const auto outlierReference = MakeReference(0.20, 1.0e-4);
    std::vector<Raw::Mfd::FusionCandidateSample> protectedCandidates(
        4u,
        MakeCandidate(0.20, 1.0e-4));
    for (auto& candidate : protectedCandidates) {
        candidate.reliability = 0.15;
    }
    protectedCandidates.push_back(MakeCandidate(0.80, 1.0e-4));
    Raw::Mfd::FusionPixelResult protectedResult;
    ok &= Fuse(
        outlierReference,
        protectedCandidates,
        protectedResult,
        weightedParameters);
    ok &= Check(
        protectedResult.diagnostics.contributingAlternateCount == 4u &&
            protectedResult.diagnostics.dominantRejectionReason ==
                Raw::Mfd::FusionRejectReason::PixelOutlier &&
            NearlyEqual(protectedResult.normalizedValue, 0.20),
        "weighted-average smoothing bypassed the per-pixel motion/outlier gate");
    return ok;
}

bool ValidateReferenceDefectException() {
    bool ok = true;
    auto reference = MakeReference(0.20, 0.01);
    reference.defective = true;
    std::vector<Raw::Mfd::FusionCandidateSample> consensus {
        MakeCandidate(0.399, 0.0001),
        MakeCandidate(0.400, 0.0001),
        MakeCandidate(0.401, 0.0001)
    };
    Raw::Mfd::FusionPixelResult reconstructed;
    ok &= Fuse(reference, consensus, reconstructed);
    ok &= Check(reconstructed.diagnostics.referenceDefectReconstructed &&
            !reconstructed.diagnostics.referenceIncluded &&
            reconstructed.diagnostics.contributingAlternateCount == 3u &&
            NearlyEqual(reconstructed.normalizedValue, 0.40) &&
            NearlyEqual(reconstructed.diagnostics.effectiveSampleCount, 3.0),
        "three agreeing high-confidence alternates did not reconstruct a reference defect");

    auto consensusWithOutlier = consensus;
    consensusWithOutlier.push_back(MakeCandidate(0.90, 0.0001));
    Raw::Mfd::FusionPixelResult robustConsensus;
    ok &= Fuse(reference, consensusWithOutlier, robustConsensus);
    ok &= Check(robustConsensus.diagnostics.referenceDefectReconstructed &&
            robustConsensus.diagnostics.contributingAlternateCount == 3u &&
            robustConsensus.diagnostics.dominantRejectionReason ==
                Raw::Mfd::FusionRejectReason::ReferenceDefectDisagreement &&
            NearlyEqual(robustConsensus.normalizedValue, 0.40),
        "weighted-median defect consensus did not discard a strong outlier");

    Raw::Mfd::FusionPixelResult tooFew;
    ok &= Fuse(reference, { consensus[0], consensus[1] }, tooFew);
    ok &= Check(tooFew.diagnostics.referenceDefectRepairDeferred &&
            tooFew.diagnostics.decisionReason ==
                Raw::Mfd::DecisionReason::ReferenceDefectDeferred &&
            tooFew.normalizedValue == reference.normalizedValue,
        "insufficient defect consensus did not defer to single-frame repair");

    auto disagreement = consensus;
    disagreement[2].value = 0.90;
    Raw::Mfd::FusionPixelResult split;
    ok &= Fuse(reference, disagreement, split);
    ok &= Check(split.diagnostics.referenceDefectRepairDeferred &&
            split.diagnostics.dominantRejectionReason ==
                Raw::Mfd::FusionRejectReason::ReferenceDefectDisagreement,
        "pairwise defect disagreement was fused instead of deferred");

    auto weakGate = consensus;
    weakGate[0].referenceDefectGate = 0.79;
    Raw::Mfd::FusionPixelResult weak;
    ok &= Fuse(reference, weakGate, weak);
    ok &= Check(weak.diagnostics.referenceDefectRepairDeferred &&
            weak.diagnostics.dominantRejectionReason ==
                Raw::Mfd::FusionRejectReason::ReferenceDefectGateInsufficient,
        "reference-defect candidates below gamma 0.8 were accepted");
    return ok;
}

bool ValidateStableOrderAndTileDiagnostics() {
    bool ok = true;
    const auto reference = MakeReference(0.25, 0.0625);
    std::vector<Raw::Mfd::FusionCandidateSample> candidates {
        MakeCandidate(0.125, 0.0625),
        MakeCandidate(0.250, 0.0625),
        MakeCandidate(0.375, 0.0625)
    };
    Raw::Mfd::FusionPixelResult baseline;
    ok &= Fuse(reference, candidates, baseline);
    const std::uint64_t baselineBits = DoubleBits(baseline.normalizedValue);
    for (std::uint32_t repeat = 0u; repeat < 100u; ++repeat) {
        Raw::Mfd::FusionPixelResult repeated;
        ok &= Fuse(reference, candidates, repeated);
        ok &= Check(DoubleBits(repeated.normalizedValue) == baselineBits &&
                repeated.diagnostics.effectiveSampleCount ==
                    baseline.diagnostics.effectiveSampleCount,
            "stable frame order did not reproduce the same fusion bits");
    }
    std::reverse(candidates.begin(), candidates.end());
    Raw::Mfd::FusionPixelResult equivalentPermutation;
    ok &= Fuse(reference, candidates, equivalentPermutation);
    ok &= Check(DoubleBits(equivalentPermutation.normalizedValue) ==
            baselineBits,
        "an exactly representable, mathematically equivalent permutation changed output");

    Raw::Mfd::FusionTileRequest request;
    request.originRawX = 17u;
    request.originRawY = 29u;
    request.extent = { 8u, 4u };
    request.alternateCount = 2u;
    request.referenceProvider = [](
        std::uint64_t x,
        std::uint64_t y,
        Raw::Mfd::FusionReferenceSample& sample) {
        const float value = static_cast<float>(
            0.10 + 0.001 * static_cast<double>((x + y) % 11u));
        sample = MakeReference(static_cast<double>(value), 0.04, 2.0);
        sample.clipped = y == 29u;
        return true;
    };
    request.candidateProvider = [](
        std::size_t alternate,
        std::uint64_t x,
        std::uint64_t y,
        Raw::Mfd::FusionCandidateSample& sample) {
        if (alternate == 1u && x == 20u) return false;
        const float normalized = static_cast<float>(
            0.10 + 0.001 * static_cast<double>((x + y) % 11u));
        sample = MakeCandidate(
            2.0 * static_cast<double>(normalized),
            0.04);
        return true;
    };
    Raw::Mfd::FusionTileResult first;
    Raw::Mfd::FusionTileResult second;
    std::string error;
    ok &= Check(Raw::Mfd::FuseRobustTile(request, first, &error),
        "first deterministic fusion tile failed: " + error);
    error.clear();
    ok &= Check(Raw::Mfd::FuseRobustTile(request, second, &error),
        "second deterministic fusion tile failed: " + error);
    ok &= Check(first.valid && second.valid &&
            first.normalizedMosaic == second.normalizedMosaic &&
            first.normalizedMosaic.size() == 32u &&
            first.exactReferencePixelCount == 8u &&
            first.fusedPixelCount == 24u,
        "tile fusion was not deterministic or its exact/fused maps are wrong");
    for (std::size_t index = 0u; index < first.diagnostics.size(); ++index) {
        ok &= Check(
            first.diagnostics[index].decisionReason ==
                second.diagnostics[index].decisionReason &&
            first.diagnostics[index].dominantRejectionReason ==
                second.diagnostics[index].dominantRejectionReason &&
            first.diagnostics[index].effectiveSampleCount ==
                second.diagnostics[index].effectiveSampleCount,
            "tile diagnostic maps changed between identical runs");
    }
    return ok;
}

} // namespace

bool ValidateMfdPhase7Fusion() {
    bool ok = true;
    ok &= ValidateEqualAndUnequalNoise();
    ok &= ValidateCapsAndExactFallback();
    ok &= ValidateWeightedAverageStrength();
    ok &= ValidateReferenceDefectException();
    ok &= ValidateStableOrderAndTileDiagnostics();
    if (ok) {
        std::cout << "MFD Phase 7 fusion validation passed." << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
