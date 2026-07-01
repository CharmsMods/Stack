#include "Editor/Internal/EditorModuleDevelopAutoSolveApplicationContext.h"

#include "Editor/Internal/EditorModuleDevelopDefaults.h"

#include <algorithm>
#include <cmath>

using Stack::Editor::DevelopDefaults::BuildRawDevelopSettingsFromMetadata;

namespace Stack::Editor::DevelopAutoSolveApplication {

namespace {

float SaturateFloat(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

DevelopAutoSceneProfile ResolveDevelopAutoSceneProfile(int profile) {
    return static_cast<DevelopAutoSceneProfile>(std::clamp(
        profile,
        static_cast<int>(DevelopAutoSceneProfile::Balanced),
        static_cast<int>(DevelopAutoSceneProfile::NoisyLowLight)));
}

} // namespace

DevelopSelectedAutoSolveContext BuildDevelopSelectedAutoSolveContext(
    const Raw::RawMetadata& metadata,
    const DevelopDynamicRange::DevelopToneAutoStats& stats,
    const DevelopCandidateScoring::DevelopAutoCandidateSolveResult& candidateSolve,
    const EditorNodeGraph::DevelopAutoGuidance& solveGuidance,
    const DevelopAutoIntentProfile& intentProfile) {
    DevelopSelectedAutoSolveContext context;
    context.defaults = BuildRawDevelopSettingsFromMetadata(metadata);

    context.autoStrength = solveGuidance.autoStrength;
    context.exposureBias = solveGuidance.exposureBias;
    context.dynamicRange = solveGuidance.dynamicRange;
    context.shadowLift = solveGuidance.shadowLift;
    context.highlightGuard = solveGuidance.highlightGuard;
    context.highlightCharacter = solveGuidance.highlightCharacter;
    context.contrastBias = solveGuidance.contrastBias;
    context.sceneProfile = ResolveDevelopAutoSceneProfile(stats.sceneProfile);

    const nlohmann::json localExposureStrategyCandidate =
        candidateSolve.dynamicRangeStrategy.is_object()
            ? candidateSolve.dynamicRangeStrategy.value(
                "localExposureStrategy",
                nlohmann::json::object())
            : nlohmann::json::object();
    context.localExposureStrategy =
        localExposureStrategyCandidate.is_object()
            ? localExposureStrategyCandidate
            : nlohmann::json::object();
    context.localExposureRangeRedistribution =
        context.localExposureStrategy.value("rangeRedistribution", 0.0f);
    context.localExposureHighlightCompression =
        context.localExposureStrategy.value("highlightCompression", 0.0f);
    context.localExposureShadowOpening =
        context.localExposureStrategy.value("shadowOpening", 0.0f);
    context.localExposureNoiseGuard =
        context.localExposureStrategy.value("noiseGuard", 0.0f);
    context.localExposureHaloGuard =
        context.localExposureStrategy.value("haloGuard", 0.0f);
    context.localExposureTextureGuard =
        context.localExposureStrategy.value("textureGuard", 0.0f);
    context.localExposureShadowEvBudget =
        context.localExposureStrategy.value("shadowEvBudget", 0.0f);
    context.localExposureHighlightEvBudget =
        context.localExposureStrategy.value("highlightEvBudget", 0.0f);
    context.localExposureStrengthTarget =
        context.localExposureStrategy.value("strengthTarget", 0.5f);

    context.darkness =
        stats.valid ? std::clamp((0.18f - stats.midtonePercentile) / 0.18f, 0.0f, 1.0f) : 0.0f;
    const float deepShadow =
        stats.valid ? std::clamp((0.06f - stats.shadowPercentile) / 0.06f, 0.0f, 1.0f) : 0.0f;
    const float sceneKeyMatch = stats.valid
        ? SaturateFloat(
            1.0f -
            std::abs(std::log2(
                std::max(stats.midtonePercentile, 0.0001f) / 0.18f)) / 0.80f)
        : 0.0f;
    context.hdrNeed = SaturateFloat((stats.hdrSpreadEv - 2.8f) / 2.8f);
    context.flatSceneNeed =
        SaturateFloat((2.35f - stats.hdrSpreadEv) / 1.15f) *
        SaturateFloat(1.0f - stats.highlightPressure * 1.35f) *
        SaturateFloat(1.0f - stats.noiseRisk * 0.65f);
    context.shadowRescueNeed = SaturateFloat(context.darkness * 0.72f + deepShadow * 0.28f);
    context.stableSceneGuard = SaturateFloat(
        sceneKeyMatch *
        (1.0f - context.shadowRescueNeed * 0.70f) *
        (1.0f - stats.highlightPressure * 0.55f) *
        (1.0f - stats.noiseRisk * 0.40f));
    context.brightWindowNeed = SaturateFloat(
        context.shadowRescueNeed * (0.48f + stats.highlightPressure * 0.52f) +
        context.hdrNeed * 0.35f);
    context.highlightNeed = std::clamp(
        stats.highlightPressure +
            std::max(0.0f, context.highlightGuard) * 0.45f +
            std::max(0.0f, context.highlightCharacter) * 0.20f,
        0.0f,
        1.50f);
    context.highlightHeavyNeed = SaturateFloat(stats.highlightPressure * 0.70f + context.hdrNeed * 0.30f);
    const float broadHighlightConstraint = SaturateFloat(
        stats.highlightPressure * 0.62f +
        std::max(0.0f, stats.highlightPercentile - 0.88f) * 2.20f +
        context.hdrNeed * 0.12f);
    const float tinySpecularAllowance = stats.valid
        ? SaturateFloat((0.010f - stats.clippingRatio) / 0.010f) *
            SaturateFloat((0.72f - stats.highlightPressure) / 0.72f)
        : 0.0f;
    context.rawBaselineLiftNeed = SaturateFloat(
        context.shadowRescueNeed * 0.72f +
        std::max(0.0f, stats.recommendedBaseEv) * 0.18f);

    float rawExposureBias = intentProfile.rawExposureBias;
    float rawLiftScale = intentProfile.rawLiftScale;
    float rawHighlightRecoveryBias = intentProfile.rawHighlightRecoveryBias;
    float rawNoiseBias = intentProfile.rawNoiseBias;
    float prepStrengthBias = intentProfile.prepStrengthBias;
    float prepShadowBias = intentProfile.prepShadowBias;
    float prepHighlightBias = intentProfile.prepHighlightBias;
    float prepContrastLift = intentProfile.prepContrastLift;
    float prepNoiseBias = intentProfile.prepNoiseBias;
    switch (context.sceneProfile) {
        case DevelopAutoSceneProfile::HighlightHeavy:
            rawExposureBias += -0.18f;
            rawLiftScale *= 0.88f;
            rawHighlightRecoveryBias += 0.18f;
            prepStrengthBias += 0.04f;
            prepShadowBias += 0.10f;
            prepHighlightBias += 0.20f;
            break;
        case DevelopAutoSceneProfile::ShadowHeavy:
            rawExposureBias += 0.14f;
            rawLiftScale *= 1.10f;
            prepStrengthBias += 0.08f;
            prepShadowBias += 0.18f;
            prepContrastLift += -0.02f;
            break;
        case DevelopAutoSceneProfile::Flat:
            rawLiftScale *= 0.78f;
            prepStrengthBias += -0.08f;
            prepShadowBias += -0.05f;
            prepContrastLift += 0.08f;
            break;
        case DevelopAutoSceneProfile::NoisyLowLight:
            rawExposureBias += 0.06f;
            rawLiftScale *= 0.92f;
            rawNoiseBias += 0.16f;
            prepStrengthBias += -0.04f;
            prepShadowBias += -0.10f;
            prepHighlightBias += 0.08f;
            prepNoiseBias += 0.14f;
            break;
        case DevelopAutoSceneProfile::Balanced:
        default:
            break;
    }
    context.rawExposureBias = rawExposureBias;
    context.prepStrengthBias = prepStrengthBias;
    context.prepShadowBias = prepShadowBias;
    context.prepHighlightBias = prepHighlightBias;
    context.prepContrastLift = prepContrastLift;
    context.prepNoiseBias = prepNoiseBias;

    context.noiseNeed = std::clamp(
        stats.noiseRisk +
            std::max(0.0f, context.shadowLift) * 0.20f +
            std::max(0.0f, context.dynamicRange - 1.0f) * 0.12f +
            rawNoiseBias +
            context.brightWindowNeed * 0.05f,
        0.0f,
        1.0f);
    context.darkNoisyLowLightDenoiseNeed =
        context.sceneProfile == DevelopAutoSceneProfile::NoisyLowLight
            ? SaturateFloat((0.12f - stats.midtonePercentile) / 0.12f) *
                SaturateFloat((0.42f - stats.highlightPercentile) / 0.42f) *
                context.noiseNeed
            : 0.0f;
    context.shadowBoost =
        context.darkness * (0.85f + context.autoStrength * 0.90f) +
        deepShadow * (0.20f + std::max(0.0f, context.dynamicRange - 1.0f) * 0.30f) +
        std::max(0.0f, context.shadowLift) * 0.75f;
    context.recommendedLift =
        std::max(0.0f, stats.recommendedBaseEv) * (0.55f + context.autoStrength * 0.55f);
    context.recommendedTrim =
        std::min(0.0f, stats.recommendedBaseEv) * (0.18f + context.autoStrength * 0.18f);
    context.userBiasEv = context.exposureBias * 2.0f;
    context.shadowBoostScale = std::clamp(
        rawLiftScale *
            (0.88f + context.shadowRescueNeed * 0.16f + context.brightWindowNeed * 0.08f) *
            (1.0f - context.stableSceneGuard * 0.18f) *
            (1.0f - stats.noiseRisk * 0.10f),
        0.55f,
        1.25f);
    context.recommendedLiftScale = std::clamp(
        rawLiftScale *
            (1.0f - context.stableSceneGuard * 0.22f - context.flatSceneNeed * 0.12f),
        0.55f,
        1.20f);
    const float broadHighlightExposureReserve =
        broadHighlightConstraint *
        SaturateFloat(stats.highlightPressure) *
        (0.78f + std::max(0.0f, context.userBiasEv) * 0.25f + context.brightWindowNeed * 0.12f) *
        (1.0f - tinySpecularAllowance * 0.65f);
    context.highlightExposurePenalty =
        std::max(0.0f, context.highlightNeed - 0.50f) *
        (0.22f + broadHighlightConstraint * 0.30f + context.brightWindowNeed * 0.06f) *
        (1.0f - tinySpecularAllowance * 0.45f) +
        broadHighlightExposureReserve;
    context.rawLiftHeadroomScale = std::clamp(
        1.0f - broadHighlightConstraint * 0.22f + tinySpecularAllowance * 0.10f,
        0.68f,
        1.08f);
    const float underBrightBroadHighlightEv = stats.valid && stats.midtonePercentile > 0.08f
        ? std::clamp(std::log2(0.70f / std::max(stats.highlightPercentile, 0.0001f)), 0.0f, 1.25f)
        : 0.0f;
    context.broadHighlightPlacementLift =
        underBrightBroadHighlightEv *
        SaturateFloat((0.58f - stats.highlightPressure) / 0.58f) *
        SaturateFloat((0.012f - stats.clippingRatio) / 0.012f) *
        (1.0f - stats.noiseRisk * 0.35f) *
        (0.52f + context.autoStrength * 0.26f);

    context.highlightModeScore = std::clamp(
        context.highlightNeed +
            std::max(0.0f, context.dynamicRange - 1.0f) * 0.20f +
            context.hdrNeed * 0.18f +
            context.brightWindowNeed * 0.08f +
            rawHighlightRecoveryBias,
        0.0f,
        1.50f);
    return context;
}

} // namespace Stack::Editor::DevelopAutoSolveApplication
