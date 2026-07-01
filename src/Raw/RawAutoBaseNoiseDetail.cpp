#include "Raw/RawAutoBase.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

namespace Stack::RawAutoBase {
namespace {

float Clamp01(float value) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return std::clamp(value, 0.0f, 1.0f);
}

bool HasUsableIso(float iso) {
    return std::isfinite(iso) && iso > 0.0f;
}

bool SuggestionIsShadowLift(const SuggestedLocalAdjustment& suggestion) {
    if (!suggestion.valid || !std::isfinite(suggestion.deltaEv) || suggestion.deltaEv <= 0.0f) {
        return false;
    }
    return suggestion.kind == SuggestedLocalAdjustmentKind::OpenShadows ||
        suggestion.kind == SuggestedLocalAdjustmentKind::OpenBacklitSubject;
}

float EstimateLocalRangeShadowLiftEv(const Stack::RawRecipe::RawLocalRangeRecipe& localRange) {
    if (!Stack::RawRecipe::IsLocalRangeEnabled(localRange)) {
        return 0.0f;
    }

    const Stack::RawRecipe::RawLocalRangeRecipe sanitized =
        Stack::RawRecipe::SanitizeLocalRangeRecipe(localRange);
    const float highEv = std::min(0.5f, sanitized.maxEv);
    if (highEv < sanitized.minEv) {
        return 0.0f;
    }

    float maxLiftEv = 0.0f;
    constexpr int kSampleCount = 17;
    for (int index = 0; index < kSampleCount; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(kSampleCount - 1);
        const float ev = sanitized.minEv + (highEv - sanitized.minEv) * t;
        maxLiftEv = std::max(
            maxLiftEv,
            Stack::RawRecipe::EvaluateLocalRangeDeltaEv(sanitized, ev));
    }

    for (const Stack::RawRecipe::RawLocalRangePoint& point : sanitized.points) {
        if (point.ev <= highEv) {
            maxLiftEv = std::max(maxLiftEv, sanitized.strength * point.deltaEv);
        }
    }

    return std::clamp(maxLiftEv, 0.0f, 4.0f);
}

float EstimateLocalExposureShadowLiftEv(const Stack::RawRecipe::RawLocalExposureRecipe& localExposure) {
    if (!localExposure.enabled) {
        return 0.0f;
    }
    const float amount = std::clamp(
        std::isfinite(localExposure.amount) ? localExposure.amount : 1.0f,
        0.0f,
        4.0f);
    const float lift = std::isfinite(localExposure.shadowLiftEv)
        ? localExposure.shadowLiftEv
        : 0.0f;
    return std::clamp(std::max(0.0f, lift) * amount, 0.0f, 4.0f);
}

std::string FormatNoiseSummary(const NoiseDetailRecommendation& recommendation) {
    std::ostringstream out;
    out << "ISO " << std::fixed << std::setprecision(0) << recommendation.iso
        << ", effective noise "
        << std::setprecision(0) << Clamp01(recommendation.effectiveNoiseScore) * 100.0f
        << "%";
    if (recommendation.shadowLiftEv > 0.05f) {
        out << ", shadow lift +" << std::setprecision(2) << recommendation.shadowLiftEv << " EV";
    }
    out << ". ";
    return out.str();
}

} // namespace

float EstimateShadowLiftEvForNoiseDetail(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const std::vector<SuggestedLocalAdjustment>* localSuggestions) {
    float shadowLiftEv = 0.0f;
    shadowLiftEv = std::max(shadowLiftEv, EstimateLocalExposureShadowLiftEv(recipe.localExposure));
    shadowLiftEv = std::max(shadowLiftEv, EstimateLocalRangeShadowLiftEv(recipe.localRange));

    if (localSuggestions != nullptr) {
        for (const SuggestedLocalAdjustment& suggestion : *localSuggestions) {
            if (SuggestionIsShadowLift(suggestion)) {
                shadowLiftEv = std::max(shadowLiftEv, suggestion.deltaEv);
            }
        }
    }

    return std::clamp(shadowLiftEv, 0.0f, 4.0f);
}

NoiseDetailRecommendation BuildNoiseDetailRecommendation(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const std::vector<SuggestedLocalAdjustment>* localSuggestions,
    bool visibleRawDenoiseControlsAvailable) {
    NoiseDetailRecommendation recommendation;
    recommendation.shadowLiftEv = EstimateShadowLiftEvForNoiseDetail(recipe, localSuggestions);
    recommendation.baselineNoise =
        analysis.metadata.hasBaselineNoise && std::isfinite(analysis.metadata.baselineNoise)
            ? std::clamp(analysis.metadata.baselineNoise, 0.25f, 8.0f)
            : 1.0f;

    if (!analysis.valid) {
        recommendation.rationale =
            "Noise/detail suggestion needs Auto Base analysis before it can estimate ISO and shadow-lift risk.";
        return recommendation;
    }

    recommendation.iso = analysis.metadata.iso;
    if (!HasUsableIso(recommendation.iso)) {
        recommendation.confidence = analysis.metadata.hasBaselineNoise ? 0.20f : 0.10f;
        recommendation.rationale =
            "ISO metadata is unavailable, and no reliable flat dark-patch noise estimator exists yet. "
            "Noise/detail changes are withheld instead of guessed.";
        return recommendation;
    }

    const float isoFactor = std::sqrt(std::max(recommendation.iso, 100.0f) / 100.0f);
    const float liftFactor = std::exp2(std::max(0.0f, recommendation.shadowLiftEv) * 0.5f);
    const float effectiveNoise = recommendation.baselineNoise * isoFactor * liftFactor;
    recommendation.effectiveNoiseScore = Remap01(effectiveNoise, 1.5f, 8.0f);

    recommendation.valid = true;
    recommendation.confidence = 0.75f;
    if (analysis.metadata.hasBaselineNoise) {
        recommendation.confidence += 0.10f;
    }
    if (analysis.currentFrameStats.valid || analysis.technicalStats.valid) {
        recommendation.confidence += 0.05f;
    }
    if (analysis.metadata.hasBaselineNoise && recommendation.baselineNoise > 2.0f) {
        recommendation.confidence += 0.05f;
    }
    recommendation.confidence = Clamp01(recommendation.confidence);

    if (recommendation.iso < 800.0f && recommendation.effectiveNoiseScore < 0.25f) {
        recommendation.sharpeningScale = 1.0f;
        recommendation.rationale =
            FormatNoiseSummary(recommendation) +
            "Low ISO and low effective noise do not need denoise/detail changes.";
    } else if (recommendation.iso >= 3200.0f || recommendation.effectiveNoiseScore > 0.65f) {
        recommendation.suggestChromaDenoise = true;
        recommendation.suggestLumaDenoise = true;
        recommendation.suggestReduceSharpening = true;
        recommendation.chromaDenoiseAmount = 0.35f;
        recommendation.lumaDenoiseAmount = 0.18f;
        recommendation.sharpeningScale = 0.75f;
        recommendation.rationale =
            FormatNoiseSummary(recommendation) +
            "High ISO or high effective noise suggests mild chroma cleanup, light luma denoise, "
            "and reduced sharpening. Strong denoise is not applied automatically.";
    } else if (recommendation.iso >= 800.0f && recommendation.iso < 3200.0f) {
        recommendation.suggestChromaDenoise = true;
        recommendation.chromaDenoiseAmount = 0.20f;
        recommendation.lumaDenoiseAmount = 0.05f;
        recommendation.sharpeningScale = 0.90f;
        recommendation.suggestReduceSharpening = recommendation.sharpeningScale < 0.95f;
        recommendation.rationale =
            FormatNoiseSummary(recommendation) +
            "Moderate ISO suggests mild chroma cleanup and a small sharpening reduction. "
            "Luma denoise remains conservative.";
    } else {
        recommendation.sharpeningScale = 1.0f;
        recommendation.rationale =
            FormatNoiseSummary(recommendation) +
            "Effective noise is below the threshold for denoise/detail suggestions.";
    }

    const bool shadowLiftRisk =
        recommendation.shadowLiftEv >= 0.5f &&
        (recommendation.effectiveNoiseScore > 0.20f || recommendation.iso >= 800.0f);
    if (shadowLiftRisk) {
        recommendation.suggestReduceSharpening = true;
        recommendation.sharpeningScale = std::min(recommendation.sharpeningScale, 0.80f);
        recommendation.rationale +=
            " Suggested or applied shadow lift increases visible shadow-noise risk, so sharpening should be reduced before adding stronger denoise.";
    }

    recommendation.autoApplyMinimalChromaDenoise =
        visibleRawDenoiseControlsAvailable &&
        recommendation.iso >= 3200.0f &&
        recommendation.effectiveNoiseScore > 0.55f &&
        recommendation.chromaDenoiseAmount <= 0.35f;
    if (!visibleRawDenoiseControlsAvailable) {
        recommendation.autoApplyMinimalChromaDenoise = false;
    }

    return recommendation;
}

} // namespace Stack::RawAutoBase
