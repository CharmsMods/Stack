#include "Raw/RawAutoBase.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace Stack::RawAutoBase {
namespace {

constexpr float kLumaEpsilon = 1.0e-8f;

float ClampFinite(float value, float fallback, float low, float high) {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, low, high);
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

float SafeLuma(float value) {
    return std::max(0.0f, std::isfinite(value) ? value : 0.0f);
}

float SampleLuma(const WhiteBalanceSample& sample) {
    if (std::isfinite(sample.luma) && sample.luma > 0.0f) {
        return sample.luma;
    }
    return 0.2126f * SafeLuma(sample.r) +
        0.7152f * SafeLuma(sample.g) +
        0.0722f * SafeLuma(sample.b);
}

float SampleSaturation(float r, float g, float b) {
    const float maximum = std::max({ SafeLuma(r), SafeLuma(g), SafeLuma(b) });
    const float minimum = std::min({ SafeLuma(r), SafeLuma(g), SafeLuma(b) });
    if (maximum <= kLumaEpsilon) {
        return 0.0f;
    }
    return std::clamp((maximum - minimum) / maximum, 0.0f, 1.0f);
}

float ChromaResidual(float r, float g, float b) {
    return SampleSaturation(r, g, b);
}

bool GainsAreExtreme(float gainR, float gainB) {
    return gainR < 0.45f || gainR > 2.20f ||
        gainB < 0.45f || gainB > 2.20f;
}

const Stack::RawAnalysis::PercentileStats* SelectExposureStats(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    bool& usedCurrentFrameFallback) {
    usedCurrentFrameFallback = false;
    if (analysis.technicalStats.valid) {
        return &analysis.technicalStats;
    }
    if (analysis.currentFrameStats.valid) {
        usedCurrentFrameFallback = true;
        return &analysis.currentFrameStats;
    }
    return nullptr;
}

bool HasFiniteCurrentFrameStats(const Stack::RawAnalysis::PercentileStats& stats) {
    return stats.valid &&
        std::isfinite(stats.p01Ev) &&
        std::isfinite(stats.p05Ev) &&
        std::isfinite(stats.p50Ev) &&
        std::isfinite(stats.p99Ev) &&
        std::isfinite(stats.dynamicRangeEv);
}

float ResolveWhiteAnchorEv(const Stack::RawAnalysis::PercentileStats& stats) {
    if (std::isfinite(stats.p999Ev) && stats.p999Luma > 0.0f && stats.p999Ev >= stats.p50Ev) {
        return stats.p999Ev;
    }
    return stats.p99Ev;
}

const char* WhiteBalanceMethodLabel(WhiteBalanceRecommendation::Method method) {
    switch (method) {
        case WhiteBalanceRecommendation::Method::CameraAsShot: return "Camera/as-shot";
        case WhiteBalanceRecommendation::Method::GrayWorld: return "Gray World";
        case WhiteBalanceRecommendation::Method::ShadesOfGray: return "Shades of Gray";
        case WhiteBalanceRecommendation::Method::GreyEdge: return "Grey Edge";
        default: return "Unknown";
    }
}

} // namespace

float Remap01(float value, float low, float high) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    if (high <= low) {
        return value >= high ? 1.0f : 0.0f;
    }
    return std::clamp((value - low) / (high - low), 0.0f, 1.0f);
}

float ComputeHighlightRisk01(const Stack::RawAnalysis::RawImageAnalysis& analysis) {
    float risk = 0.0f;
    risk = std::max(risk, Remap01(analysis.currentFrameStats.dynamicRangeEv, 8.0f, 13.0f));
    risk = std::max(risk, Remap01(analysis.highlight.hdrPixelPercent, 0.5f, 5.0f));
    risk = std::max(risk, Remap01(analysis.highlight.anyChannelClipPercent, 0.02f, 0.25f));
    risk = std::max(risk, Remap01(analysis.highlight.displayClipPercent, 0.5f, 5.0f));
    return std::clamp(risk, 0.0f, 1.0f);
}

float ComputeShadowCompressionRisk01(const Stack::RawAnalysis::RawImageAnalysis& analysis) {
    const Stack::RawAnalysis::PercentileStats& stats = analysis.currentFrameStats;
    if (!stats.valid) {
        return 0.0f;
    }
    const float shadowMassRisk = Remap01(stats.p50Ev - stats.p05Ev, 1.0f, 4.0f);
    const float deepShadowRisk = Remap01(-stats.p05Ev, 6.0f, 12.0f);
    return std::clamp(std::max(shadowMassRisk, deepShadowRisk), 0.0f, 1.0f);
}

ViewTransformFit FitViewTransformFromAnalysis(const Stack::RawAnalysis::RawImageAnalysis& analysis) {
    ViewTransformFit fit;
    const Stack::RawAnalysis::PercentileStats& stats = analysis.currentFrameStats;
    if (!analysis.valid || !HasFiniteCurrentFrameStats(stats)) {
        fit.reason = "Current-frame stats are unavailable.";
        return fit;
    }

    fit.medianEv = stats.p50Ev;
    fit.blackAnchorEv = stats.p01Ev;
    fit.whiteAnchorEv = ResolveWhiteAnchorEv(stats);
    if (!std::isfinite(fit.whiteAnchorEv) || fit.whiteAnchorEv < fit.medianEv) {
        fit.whiteAnchorEv = stats.p99Ev;
    }

    fit.highlightRisk01 = ComputeHighlightRisk01(analysis);
    fit.shadowCompressionRisk01 = ComputeShadowCompressionRisk01(analysis);

    fit.whiteMarginEv = 0.35f;
    fit.blackMarginEv = 0.30f;
    if (analysis.highlight.anyChannelClipPercent > 0.05f ||
        stats.dynamicRangeEv > 10.0f) {
        fit.whiteMarginEv += 0.25f;
    }
    if (analysis.effectiveNoiseScore > 0.65f) {
        fit.blackMarginEv += 0.20f;
    }

    fit.middleGrey = ClampFinite(std::exp2(fit.medianEv), 0.18f, 0.01f, 1.0f);
    const float storedMiddleEv = Stack::RawAnalysis::SafeLog2Luma(fit.middleGrey);
    const float whiteDistanceEv =
        std::max(0.0f, fit.whiteAnchorEv - storedMiddleEv) + fit.whiteMarginEv;
    const float blackDistanceEv =
        std::max(0.0f, storedMiddleEv - fit.blackAnchorEv) + fit.blackMarginEv;

    fit.whiteEv = ClampFinite(whiteDistanceEv, 4.0f, 2.5f, 10.0f);
    // Stack's recipe stores black EV as a negative offset below middle grey.
    fit.blackEv = -ClampFinite(blackDistanceEv, 8.0f, 4.0f, 14.0f);

    const float rangeEv = fit.whiteEv + std::abs(fit.blackEv);
    fit.contrast = ClampFinite(1.15f - 0.04f * (rangeEv - 10.0f), 1.0f, 0.90f, 1.20f);
    fit.shoulder = Lerp(0.20f, 0.60f, fit.highlightRisk01);
    fit.toe = Lerp(0.15f, 0.45f, fit.shadowCompressionRisk01);
    fit.exposure = 0.0f;
    fit.saturation = 1.0f;
    fit.preserveHue = true;
    fit.valid = true;
    fit.reason = "View fit from current-frame scene-linear stats.";
    return fit;
}

ViewFitDecision BuildAutoBaseViewFitDecision(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    (void)recipe;
    ViewFitDecision decision;
    decision.fit = FitViewTransformFromAnalysis(analysis);
    decision.valid = decision.fit.valid;
    decision.canApply = decision.fit.valid;
    if (decision.canApply) {
        decision.summary = "Auto Base applied: View fit from current frame. RAW Exposure unchanged.";
        decision.reason = "View Transform Auto Fit is safe because it maps scene-linear values to display without changing RAW Exposure.";
    } else {
        decision.summary = "Auto Base pending: render preview to analyze the frame.";
        decision.reason = decision.fit.reason.empty()
            ? "Current-frame stats are unavailable."
            : decision.fit.reason;
    }
    return decision;
}

void ApplyViewTransformFitToRecipe(
    Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const ViewTransformFit& fit) {
    if (!fit.valid) {
        return;
    }

    nlohmann::json viewTransform = recipe.viewTransform.layerJson.is_object()
        ? recipe.viewTransform.layerJson
        : Stack::RawRecipe::DefaultViewTransformJson();
    viewTransform["type"] = "ViewTransform";
    viewTransform["exposure"] = fit.exposure;
    viewTransform["middleGrey"] = fit.middleGrey;
    viewTransform["blackEv"] = fit.blackEv;
    viewTransform["whiteEv"] = fit.whiteEv;
    viewTransform["shoulder"] = fit.shoulder;
    viewTransform["toe"] = fit.toe;
    viewTransform["contrast"] = fit.contrast;
    viewTransform["saturation"] = fit.saturation;
    viewTransform["preserveHue"] = fit.preserveHue;
    viewTransform["debugFalseColor"] = false;
    recipe.viewTransform.layerJson = std::move(viewTransform);
}

WhiteBalanceCandidateEvidence BuildWhiteBalanceCandidateEvidence(
    const std::vector<WhiteBalanceSample>& samples,
    const Stack::RawAnalysis::PercentileStats& stats,
    WhiteBalanceRecommendation::Method method) {
    WhiteBalanceCandidateEvidence evidence;
    evidence.method = method;
    if (method == WhiteBalanceRecommendation::Method::CameraAsShot) {
        evidence.rationale = "Camera/as-shot white balance comes from RAW metadata, not sampled neutral pixels.";
        return evidence;
    }
    if (method == WhiteBalanceRecommendation::Method::GreyEdge) {
        evidence.rationale = "Grey Edge candidate generation is deferred until edge-aware color sampling exists.";
        return evidence;
    }
    if (samples.empty() || !stats.valid) {
        evidence.rationale = "Neutral-pixel samples are unavailable.";
        return evidence;
    }

    struct EligibleSample {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
    };
    std::vector<EligibleSample> eligible;
    eligible.reserve(samples.size());
    int validCount = 0;
    float saturationSum = 0.0f;
    for (const WhiteBalanceSample& sample : samples) {
        if (!sample.valid || sample.clipped) {
            continue;
        }
        const float r = SafeLuma(sample.r);
        const float g = SafeLuma(sample.g);
        const float b = SafeLuma(sample.b);
        const float luma = SampleLuma(sample);
        if (!std::isfinite(luma)) {
            continue;
        }
        ++validCount;
        const float saturation = SampleSaturation(r, g, b);
        saturationSum += saturation;
        if (luma < stats.p05Luma || luma > stats.p95Luma) {
            continue;
        }
        if (saturation >= 0.65f) {
            continue;
        }
        eligible.push_back({ r, g, b });
    }

    if (validCount <= 0) {
        evidence.rationale = "White balance samples contained no valid pixels.";
        return evidence;
    }
    evidence.eligiblePixelPercent =
        100.0f * static_cast<float>(eligible.size()) / static_cast<float>(validCount);
    const float averageSaturation = saturationSum / static_cast<float>(validCount);
    evidence.sceneLooksStylized =
        averageSaturation > 0.55f || evidence.eligiblePixelPercent < 5.0f;
    if (eligible.empty()) {
        evidence.rationale = "No medium-luma, low-saturation pixels were eligible for auto WB.";
        return evidence;
    }

    float meanR = 0.0f;
    float meanG = 0.0f;
    float meanB = 0.0f;
    float residualBefore = 0.0f;
    if (method == WhiteBalanceRecommendation::Method::GrayWorld) {
        for (const EligibleSample& sample : eligible) {
            meanR += sample.r;
            meanG += sample.g;
            meanB += sample.b;
            residualBefore += ChromaResidual(sample.r, sample.g, sample.b);
        }
        const float count = static_cast<float>(eligible.size());
        meanR /= count;
        meanG /= count;
        meanB /= count;
        residualBefore /= count;
    } else {
        constexpr float p = 6.0f;
        for (const EligibleSample& sample : eligible) {
            meanR += std::pow(sample.r, p);
            meanG += std::pow(sample.g, p);
            meanB += std::pow(sample.b, p);
            residualBefore += ChromaResidual(sample.r, sample.g, sample.b);
        }
        const float count = static_cast<float>(eligible.size());
        meanR = std::pow(meanR / count, 1.0f / p);
        meanG = std::pow(meanG / count, 1.0f / p);
        meanB = std::pow(meanB / count, 1.0f / p);
        residualBefore /= count;
    }

    const float target = std::cbrt(
        std::max(kLumaEpsilon, meanR) *
        std::max(kLumaEpsilon, meanG) *
        std::max(kLumaEpsilon, meanB));
    float gainR = target / std::max(kLumaEpsilon, meanR);
    float gainG = target / std::max(kLumaEpsilon, meanG);
    float gainB = target / std::max(kLumaEpsilon, meanB);
    gainR /= std::max(kLumaEpsilon, gainG);
    gainB /= std::max(kLumaEpsilon, gainG);
    gainG = 1.0f;

    float residualAfter = 0.0f;
    for (const EligibleSample& sample : eligible) {
        residualAfter += ChromaResidual(sample.r * gainR, sample.g * gainG, sample.b * gainB);
    }
    residualAfter /= static_cast<float>(eligible.size());

    evidence.valid = true;
    evidence.gainsR = gainR;
    evidence.gainsG = gainG;
    evidence.gainsB = gainB;
    evidence.neutralResidualBefore = residualBefore;
    evidence.neutralResidualAfter = residualAfter;
    evidence.candidateGainsAreExtreme = GainsAreExtreme(gainR, gainB);
    std::ostringstream rationale;
    rationale << WhiteBalanceMethodLabel(method) << " candidate from "
              << evidence.eligiblePixelPercent
              << "% eligible medium-luma, low-saturation pixels.";
    evidence.rationale = rationale.str();
    return evidence;
}

RawExposureRecommendation BuildRawExposureRecommendation(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    RawExposureRecommendation recommendation;
    recommendation.currentEv = recipe.preToneExposureEv;
    recommendation.suggestedEv = recipe.preToneExposureEv;

    bool usedCurrentFrameFallback = false;
    const Stack::RawAnalysis::PercentileStats* stats =
        SelectExposureStats(analysis, usedCurrentFrameFallback);
    if (!analysis.valid || stats == nullptr || !stats->valid) {
        recommendation.rationale =
            "RAW exposure suggestion needs technical RAW luminance stats or a current-frame fallback.";
        return recommendation;
    }

    const float fittedSceneWhiteEv = ResolveWhiteAnchorEv(*stats);
    const float subjectMedianEv = stats->p50Ev;
    if (!std::isfinite(fittedSceneWhiteEv) || !std::isfinite(subjectMedianEv)) {
        recommendation.rationale = "RAW exposure suggestion needs finite median and white-anchor EV values.";
        return recommendation;
    }

    constexpr float targetMedianRelativeToWhiteEv = -2.7f;
    const float targetMedianEv = fittedSceneWhiteEv + targetMedianRelativeToWhiteEv;
    const float rawDelta = std::clamp(targetMedianEv - subjectMedianEv, -0.5f, 1.0f);

    float confidence = 1.0f;
    if (analysis.highlight.anyChannelClipPercent > 0.05f) {
        confidence -= 0.45f;
    }
    if (analysis.highlight.allChannelClipPercent > 0.005f) {
        confidence -= 0.30f;
    }
    if (stats->dynamicRangeEv > 10.0f) {
        confidence -= 0.20f;
    }
    if (stats->dynamicRangeEv > 12.0f) {
        confidence -= 0.20f;
    }
    if (analysis.highlight.partialClipColorRisk) {
        confidence -= 0.25f;
    }
    if (usedCurrentFrameFallback) {
        confidence -= 0.10f;
    }
    confidence = std::clamp(confidence, 0.0f, 1.0f);

    recommendation.valid = std::abs(rawDelta) >= 0.15f;
    recommendation.deltaEv = rawDelta;
    recommendation.suggestedEv = recipe.preToneExposureEv + rawDelta;
    recommendation.confidence = confidence;
    recommendation.usedCurrentFrameFallback = usedCurrentFrameFallback;
    recommendation.blockedByHighlightRisk =
        rawDelta > 0.0f && analysis.highlight.blocksPositiveRawExposure;
    recommendation.autoApplyAllowed =
        recommendation.valid &&
        confidence >= 0.85f &&
        std::abs(rawDelta) <= 0.5f &&
        !analysis.highlight.blocksPositiveRawExposure;
    recommendation.action = recommendation.valid
        ? RecommendationAction::ApplyVisibleRecipeValue
        : RecommendationAction::None;

    if (!recommendation.valid) {
        recommendation.rationale = "RAW exposure is already close to the conservative median target.";
    } else if (recommendation.blockedByHighlightRisk) {
        recommendation.rationale =
            "RAW exposure lift not auto-applied because highlights are near clipping. Use View Transform or Local Range instead.";
    } else if (usedCurrentFrameFallback) {
        recommendation.rationale =
            "Suggested from current-frame scene-linear luminance statistics; technical sensor-domain stats are not available yet.";
    } else {
        recommendation.rationale =
            "Suggested from technical RAW luminance statistics using a conservative median-relative-to-white target.";
    }
    return recommendation;
}

WhiteBalanceRecommendation BuildWhiteBalanceRecommendation(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const WhiteBalanceCandidateEvidence* alternateEvidence) {
    WhiteBalanceRecommendation recommendation;
    recommendation.cameraWhiteBalanceAvailable = analysis.metadata.hasCameraWhiteBalance;
    recommendation.manualWhiteBalanceProtected =
        recipe.whiteBalance.mode == Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers ||
        recipe.whiteBalance.mode == Stack::RawRecipe::WhiteBalanceMode::SampledGrayPoint;

    if (recommendation.cameraWhiteBalanceAvailable) {
        recommendation.valid = true;
        recommendation.method = WhiteBalanceRecommendation::Method::CameraAsShot;
        recommendation.gainsR = analysis.metadata.cameraWbR;
        recommendation.gainsG = analysis.metadata.cameraWbG;
        recommendation.gainsB = analysis.metadata.cameraWbB;
        recommendation.confidence = 1.0f;
        recommendation.action = RecommendationAction::None;
        recommendation.rationale = "Camera/as-shot white balance is available and remains the default.";
    } else {
        recommendation.rationale =
            "Camera/as-shot white balance metadata is unavailable.";
    }

    if (recommendation.manualWhiteBalanceProtected) {
        recommendation.autoApplyAllowed = false;
        recommendation.rationale =
            "Manual/custom white balance is active, so alternate auto WB is diagnostics-only.";
        return recommendation;
    }

    if (alternateEvidence == nullptr || !alternateEvidence->valid) {
        if (!recommendation.valid) {
            recommendation.rationale +=
                " Alternate auto WB needs medium-luma, low-saturation pixel evidence before it can be suggested.";
        }
        return recommendation;
    }

    recommendation.valid = true;
    recommendation.alternateCandidateAvailable = true;
    recommendation.method = alternateEvidence->method;
    recommendation.gainsR = alternateEvidence->gainsR;
    recommendation.gainsG = alternateEvidence->gainsG;
    recommendation.gainsB = alternateEvidence->gainsB;
    recommendation.eligiblePixelPercent = alternateEvidence->eligiblePixelPercent;
    recommendation.neutralResidualBefore = alternateEvidence->neutralResidualBefore;
    recommendation.neutralResidualAfter = alternateEvidence->neutralResidualAfter;

    const float improvement =
        alternateEvidence->neutralResidualBefore - alternateEvidence->neutralResidualAfter;
    float confidence = Remap01(improvement, 0.02f, 0.12f);
    if (alternateEvidence->eligiblePixelPercent < 5.0f) {
        confidence -= 0.35f;
    }
    if (alternateEvidence->sceneLooksStylized) {
        confidence -= 0.45f;
    }
    if (alternateEvidence->candidateGainsAreExtreme) {
        confidence -= 0.35f;
    }
    recommendation.confidence = std::clamp(confidence, 0.0f, 1.0f);
    recommendation.autoApplyAllowed =
        !recommendation.cameraWhiteBalanceAvailable &&
        recommendation.confidence >= 0.85f;
    recommendation.action = RecommendationAction::ApplyVisibleRecipeValue;

    if (recommendation.cameraWhiteBalanceAvailable) {
        recommendation.autoApplyAllowed = false;
        recommendation.rationale =
            "Alternate WB reduces neutral residual, but camera/as-shot WB remains the default unless you apply the suggestion.";
    } else if (recommendation.confidence < 0.5f) {
        recommendation.rationale =
            "Auto WB withheld because the scene appears intentionally colored or lacks reliable neutral pixels.";
    } else {
        recommendation.rationale =
            "Alternate WB candidate reduces residual color in neutral-looking pixels.";
    }
    return recommendation;
}

HighlightRecommendation BuildHighlightRecommendation(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const ViewTransformFit* fit) {
    HighlightRecommendation recommendation;
    recommendation.valid = analysis.highlight.valid || analysis.currentFrameStats.valid;
    if (!recommendation.valid) {
        recommendation.rationale = "Highlight recommendation needs sensor or display highlight signals.";
        return recommendation;
    }

    recommendation.recommendNoPositiveRawExposure =
        analysis.highlight.blocksPositiveRawExposure;
    recommendation.recommendProtectiveViewShoulder =
        analysis.highlight.anyChannelClipPercent > 0.02f ||
        analysis.highlight.hdrPixelPercent > 1.0f ||
        analysis.highlight.displayClipPercent > 1.0f;
    recommendation.recommendReconstruction =
        analysis.highlight.anyChannelClipPercent > 0.10f ||
        analysis.highlight.severeSensorClip ||
        analysis.highlight.partialClipColorRisk;
    recommendation.recommendAchromaticClip =
        analysis.highlight.partialClipColorRisk &&
        analysis.highlight.allChannelClipPercent <
            analysis.highlight.anyChannelClipPercent * 0.5f;

    float confidence = ComputeHighlightRisk01(analysis);
    if (fit != nullptr && fit->valid) {
        confidence = std::max(confidence, fit->highlightRisk01);
    }
    if (recommendation.recommendNoPositiveRawExposure) {
        confidence = std::max(confidence, 0.55f);
    }
    recommendation.confidence = std::clamp(confidence, 0.0f, 1.0f);
    recommendation.protectionAction = recommendation.recommendProtectiveViewShoulder
        ? RecommendationAction::ApplyVisibleRecipeValue
        : RecommendationAction::None;
    recommendation.reconstructionAction =
        recommendation.recommendReconstruction || recommendation.recommendAchromaticClip
            ? RecommendationAction::CreateSuggestionOnly
            : RecommendationAction::None;

    if (recommendation.recommendReconstruction || recommendation.recommendAchromaticClip) {
        recommendation.rationale =
            "Sensor clipping risk detected. RAW clipping means data may be lost; reconstruction remains read-only until RAW workspace exposes visible highlight controls.";
    } else if (recommendation.recommendProtectiveViewShoulder) {
        recommendation.rationale =
            "Display highlight pressure detected. View Transform shoulder/white EV can protect highlights without changing RAW Exposure.";
    } else if (recommendation.recommendNoPositiveRawExposure) {
        recommendation.rationale =
            "Positive RAW exposure is blocked until sensor-domain clipping analysis is available.";
    } else {
        recommendation.rationale = "No strong highlight protection action is needed.";
    }
    return recommendation;
}

AutoBaseRecommendations BuildAutoBaseRecommendations(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const WhiteBalanceCandidateEvidence* alternateWhiteBalanceEvidence,
    const LocalSuggestionAnalysisImage* localSuggestionImage) {
    AutoBaseRecommendations recommendations;
    recommendations.exposure = BuildRawExposureRecommendation(analysis, recipe);
    recommendations.whiteBalance =
        BuildWhiteBalanceRecommendation(analysis, recipe, alternateWhiteBalanceEvidence);
    const ViewTransformFit fit = FitViewTransformFromAnalysis(analysis);
    recommendations.highlight = BuildHighlightRecommendation(analysis, &fit);
    if (localSuggestionImage != nullptr) {
        recommendations.localAdjustments =
            BuildSuggestedLocalAdjustments(analysis, *localSuggestionImage, &recommendations.localReport);
        recommendations.localSuggestionRationale = recommendations.localReport.statusMessage;
    } else {
        recommendations.localSuggestionRationale =
            "Local suggestions need scene-linear RGB analysis before Local Range and View Transform.";
    }
    recommendations.noiseDetail =
        BuildNoiseDetailRecommendation(analysis, recipe, &recommendations.localAdjustments, false);
    return recommendations;
}

void ApplyWhiteBalanceRecommendationToRecipe(
    Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const WhiteBalanceRecommendation& recommendation) {
    if (!recommendation.valid ||
        recommendation.method == WhiteBalanceRecommendation::Method::CameraAsShot ||
        !recommendation.alternateCandidateAvailable) {
        return;
    }

    recipe.whiteBalance.mode = Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers;
    recipe.whiteBalance.hasMultipliers = true;
    recipe.whiteBalance.multipliers = {
        recommendation.gainsR,
        recommendation.gainsG,
        recommendation.gainsB
    };
    recipe.whiteBalance.hasTemperatureKelvin = false;
    recipe.whiteBalance.temperatureKelvin = 0.0f;
    recipe.whiteBalance.hasTint = false;
    recipe.whiteBalance.tint = 0.0f;
    recipe.whiteBalance.hasSamplePoint = false;
    recipe.whiteBalance.sampleX = 0.5f;
    recipe.whiteBalance.sampleY = 0.5f;
}

void ApplyHighlightProtectionToRecipe(
    Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const HighlightRecommendation& recommendation,
    const ViewTransformFit& fit) {
    if (!recommendation.valid || !recommendation.recommendProtectiveViewShoulder) {
        return;
    }

    nlohmann::json viewTransform = recipe.viewTransform.layerJson.is_object()
        ? recipe.viewTransform.layerJson
        : Stack::RawRecipe::DefaultViewTransformJson();
    viewTransform["type"] = "ViewTransform";
    const float currentShoulder = viewTransform.value("shoulder", 0.45f);
    const float currentWhiteEv = viewTransform.value("whiteEv", 4.0f);
    const float targetShoulder = fit.valid
        ? std::max(currentShoulder, fit.shoulder)
        : std::max(currentShoulder, 0.60f);
    const float targetWhiteEv = fit.valid
        ? std::max(currentWhiteEv, fit.whiteEv)
        : currentWhiteEv + 0.35f;
    viewTransform["shoulder"] = ClampFinite(targetShoulder, 0.60f, 0.05f, 4.0f);
    viewTransform["whiteEv"] = ClampFinite(targetWhiteEv, 4.0f, 0.0f, 16.0f);
    viewTransform["preserveHue"] = true;
    recipe.viewTransform.layerJson = std::move(viewTransform);
}

} // namespace Stack::RawAutoBase
