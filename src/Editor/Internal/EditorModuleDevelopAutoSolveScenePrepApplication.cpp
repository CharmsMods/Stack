#include "Editor/Internal/EditorModuleDevelopAutoSolveApplicationContext.h"

#include <algorithm>

namespace Stack::Editor::DevelopAutoSolveApplication {

namespace {

void ClampIntegratedDevelopScenePrepSettings(Raw::RawDetailFusionSettings& settings) {
    settings.mode = Raw::RawDetailFusionMode::AutoAnalyze;
    settings.debugView = Raw::RawDetailFusionDebugView::FinalImage;
    settings.invertMask = false;
    settings.maskBlackPoint = std::clamp(settings.maskBlackPoint, 0.0f, 1.0f);
    settings.maskWhitePoint = std::clamp(settings.maskWhitePoint, settings.maskBlackPoint + 0.001f, 1.0f);
    settings.maskGamma = std::clamp(settings.maskGamma, 0.05f, 8.0f);
    settings.minEv = std::clamp(settings.minEv, -2.5f, 0.5f);
    settings.maxEv = std::clamp(settings.maxEv, std::max(settings.minEv + 0.01f, 0.25f), 2.5f);
    settings.baseEv = std::clamp(settings.baseEv, -1.0f, 1.0f);
    settings.minEvBias = std::clamp(settings.minEvBias, -2.0f, 2.0f);
    settings.maxEvBias = std::clamp(settings.maxEvBias, -2.0f, 2.0f);
    settings.baseEvBias = std::clamp(settings.baseEvBias, -1.25f, 1.25f);
    settings.noiseProtectionBias = std::clamp(settings.noiseProtectionBias, -1.0f, 1.0f);
    settings.highlightProtectionBias = std::clamp(settings.highlightProtectionBias, -1.0f, 1.0f);
    settings.shadowLiftLimitBias = std::clamp(settings.shadowLiftLimitBias, -1.0f, 1.0f);
    settings.wellExposedTargetBias = std::clamp(settings.wellExposedTargetBias, -1.0f, 1.0f);
    settings.strength = std::clamp(settings.strength, 0.0f, 1.25f);
    settings.sampleCount = std::clamp(settings.sampleCount, 3, 33);
    settings.baseRadiusPercent = std::clamp(settings.baseRadiusPercent, 0.002f, 0.030f);
    settings.highlightProtection = std::clamp(settings.highlightProtection, 0.0f, 1.0f);
    settings.shadowLiftLimit = std::clamp(settings.shadowLiftLimit, 0.0f, 1.0f);
    settings.noiseProtection = std::clamp(settings.noiseProtection, 0.0f, 1.0f);
    settings.detailWeight = std::clamp(settings.detailWeight, 0.0f, 1.0f);
    settings.wellExposedTarget = std::clamp(settings.wellExposedTarget, 0.10f, 0.55f);
    settings.smoothGradientProtection = std::clamp(settings.smoothGradientProtection, 0.0f, 1.0f);
    settings.textureSensitivity = std::clamp(settings.textureSensitivity, 0.0f, 1.0f);
    settings.skyBias = std::clamp(settings.skyBias, 0.0f, 1.0f);
    settings.smoothnessRadius = std::clamp(settings.smoothnessRadius, 0, 16);
    settings.smoothAreaRadius = std::clamp(settings.smoothAreaRadius, 0, 32);
    settings.edgeAwareness = std::clamp(settings.edgeAwareness, 0.0f, 1.0f);
    settings.haloGuard = std::clamp(settings.haloGuard, 0.0f, 1.0f);
    settings.maskDebandDither = std::clamp(settings.maskDebandDither, 0.0f, 1.0f);
    settings.manualBlend = 0.0f;
}

} // namespace

Raw::RawDetailFusionSettings ApplyDevelopSelectedScenePrepSettings(
    EditorNodeGraph::RawDevelopPayload& payload,
    const DevelopDynamicRange::DevelopToneAutoStats& stats,
    const DevelopSelectedAutoSolveContext& context) {
    Raw::RawDetailFusionSettings prepSettings = payload.scenePrepSettings;
    prepSettings.autoSafetyEnabled = true;
    prepSettings.overrideMinEv = false;
    prepSettings.overrideMaxEv = false;
    prepSettings.overrideBaseEv = false;
    prepSettings.overrideNoiseProtection = false;
    prepSettings.overrideHighlightProtection = false;
    prepSettings.overrideShadowLiftLimit = false;
    prepSettings.overrideWellExposedTarget = false;
    prepSettings.strength = std::clamp(
        0.50f +
            context.autoStrength * 0.30f +
            std::max(0.0f, context.dynamicRange - 1.0f) * 0.14f +
            context.prepStrengthBias +
            context.rawBaselineLiftNeed * 0.05f +
            context.brightWindowNeed * 0.07f -
            context.stableSceneGuard * 0.10f,
        0.0f,
        1.25f);
    prepSettings.maxEvBias = std::clamp(
        context.shadowLift * 1.20f +
            context.shadowBoost * 0.62f +
            std::max(0.0f, context.dynamicRange - 1.0f) * 0.55f +
            context.prepShadowBias +
            context.brightWindowNeed * 0.36f -
            stats.noiseRisk * 0.10f -
            context.flatSceneNeed * 0.08f,
        -2.0f,
        2.0f);
    prepSettings.minEvBias = std::clamp(
        -std::max(0.0f, context.dynamicRange - 1.0f) * 0.45f -
            std::max(0.0f, context.highlightGuard) * 0.35f -
            context.highlightNeed * 0.18f -
            context.hdrNeed * 0.22f -
            context.prepHighlightBias * 0.18f,
        -2.0f,
        2.0f);
    prepSettings.baseEvBias = std::clamp(
        context.exposureBias * 0.55f +
            stats.recommendedBaseEv * 0.18f +
            context.rawExposureBias * 0.18f -
            context.stableSceneGuard * 0.04f,
        -1.25f,
        1.25f);
    prepSettings.noiseProtectionBias = std::clamp(
        context.noiseNeed * 0.68f +
            std::max(0.0f, context.shadowLift) * 0.25f +
            context.prepNoiseBias +
            context.brightWindowNeed * 0.06f,
        -1.0f,
        1.0f);
    prepSettings.highlightProtectionBias = std::clamp(
        context.highlightGuard * 0.85f +
            stats.highlightPressure * 0.35f +
            context.prepHighlightBias +
            context.hdrNeed * 0.14f +
            context.brightWindowNeed * 0.10f,
        -1.0f,
        1.0f);
    prepSettings.shadowLiftLimitBias = std::clamp(
        -std::max(0.0f, context.dynamicRange - 1.0f) * 0.30f -
            std::max(0.0f, context.shadowLift) * 0.15f +
            (1.0f - context.noiseNeed) * 0.05f -
            context.prepShadowBias * 0.20f +
            context.shadowRescueNeed * 0.06f -
            context.prepNoiseBias * 0.08f,
        -1.0f,
        1.0f);
    prepSettings.wellExposedTargetBias = std::clamp(
        context.contrastBias * 0.18f +
            context.prepContrastLift +
            context.flatSceneNeed * 0.08f +
            (context.darkness > 0.35f ? (-0.04f + context.brightWindowNeed * 0.06f) : 0.04f) +
            context.brightWindowNeed * 0.07f +
            context.shadowRescueNeed * 0.03f -
            context.stableSceneGuard * 0.03f,
        -1.0f,
        1.0f);
    prepSettings.noiseProtection = std::clamp(
        0.58f + context.noiseNeed * 0.22f + context.prepNoiseBias * 0.10f,
        0.0f,
        1.0f);
    prepSettings.highlightProtection = std::clamp(
        0.82f +
            context.highlightNeed * 0.14f +
            std::max(0.0f, context.highlightGuard) * 0.08f +
            context.hdrNeed * 0.06f +
            context.brightWindowNeed * 0.05f,
        0.0f,
        1.0f);
    prepSettings.shadowLiftLimit = std::clamp(
        0.66f -
            std::max(0.0f, context.shadowLift) * 0.10f -
            std::max(0.0f, context.dynamicRange - 1.0f) * 0.05f +
            context.noiseNeed * 0.12f -
            context.shadowRescueNeed * 0.04f +
            context.flatSceneNeed * 0.03f,
        0.0f,
        1.0f);
    prepSettings.detailWeight = std::clamp(
        0.50f +
            stats.textureConfidence * 0.20f +
            context.contrastBias * 0.08f +
            context.flatSceneNeed * 0.06f -
            context.prepNoiseBias * 0.04f,
        0.0f,
        1.0f);
    prepSettings.wellExposedTarget = std::clamp(
        0.28f +
            context.contrastBias * 0.03f +
            std::max(0.0f, context.exposureBias) * 0.02f +
            context.flatSceneNeed * 0.03f -
            context.shadowRescueNeed * 0.015f +
            context.brightWindowNeed * 0.035f -
            context.stableSceneGuard * 0.02f,
        0.10f,
        0.55f);
    prepSettings.smoothGradientProtection = std::clamp(0.82f + context.highlightNeed * 0.10f, 0.0f, 1.0f);
    prepSettings.textureSensitivity = std::clamp(
        0.42f +
            stats.textureConfidence * 0.25f +
            context.flatSceneNeed * 0.06f -
            context.prepNoiseBias * 0.06f,
        0.0f,
        1.0f);
    prepSettings.skyBias = std::clamp(
        0.52f +
            std::max(0.0f, context.highlightGuard) * 0.16f +
            std::max(0.0f, context.highlightCharacter) * 0.05f +
            context.hdrNeed * 0.06f,
        0.0f,
        1.0f);
    prepSettings.sampleCount = context.noiseNeed > 0.60f ? 19 : 17;
    prepSettings.baseRadiusPercent = std::clamp(
        0.010f +
            std::max(0.0f, context.dynamicRange - 1.0f) * 0.003f +
            context.flatSceneNeed * 0.0015f +
            context.brightWindowNeed * 0.0010f,
        0.002f,
        0.030f);
    prepSettings.smoothnessRadius = context.noiseNeed > 0.65f ? 6 : 5;
    prepSettings.smoothAreaRadius = context.noiseNeed > 0.65f ? 14 : 12;
    prepSettings.edgeAwareness = std::clamp(
        0.62f +
            stats.textureConfidence * 0.16f +
            context.brightWindowNeed * 0.06f -
            context.prepNoiseBias * 0.04f,
        0.0f,
        1.0f);
    prepSettings.haloGuard = std::clamp(
        0.88f + context.highlightNeed * 0.08f + context.brightWindowNeed * 0.08f + context.hdrNeed * 0.05f,
        0.0f,
        1.0f);
    prepSettings.maskDebandDither = (context.noiseNeed > 0.55f || context.flatSceneNeed > 0.70f) ? 0.10f : 0.0f;
    // The local exposure strategy turns Guide 04's highlight/shadow/range map
    // into authored Scene Prep pressure. These are coordinated guardrail moves,
    // not one-slider-one-setting edits: bright regions, shadows, noise, halos,
    // and texture have to move together to avoid fake HDR or gray noisy darks.
    prepSettings.strength = std::clamp(
        prepSettings.strength * 0.86f +
            context.localExposureStrengthTarget * 0.14f +
            context.localExposureRangeRedistribution * 0.04f,
        0.0f,
        1.25f);
    prepSettings.maxEvBias = std::clamp(
        prepSettings.maxEvBias +
            context.localExposureShadowOpening * 0.18f +
            context.localExposureRangeRedistribution * 0.10f +
            context.localExposureShadowEvBudget * 0.04f -
            context.localExposureNoiseGuard * 0.10f -
            context.localExposureHaloGuard * 0.06f,
        -2.0f,
        2.0f);
    prepSettings.minEvBias = std::clamp(
        prepSettings.minEvBias -
            context.localExposureHighlightCompression * 0.16f -
            context.localExposureRangeRedistribution * 0.06f -
            context.localExposureHighlightEvBudget * 0.04f +
            context.localExposureHaloGuard * 0.03f,
        -2.0f,
        2.0f);
    prepSettings.highlightProtectionBias = std::clamp(
        prepSettings.highlightProtectionBias +
            context.localExposureHighlightCompression * 0.10f,
        -1.0f,
        1.0f);
    prepSettings.noiseProtectionBias = std::clamp(
        prepSettings.noiseProtectionBias +
            context.localExposureNoiseGuard * 0.10f,
        -1.0f,
        1.0f);
    prepSettings.shadowLiftLimitBias = std::clamp(
        prepSettings.shadowLiftLimitBias +
            context.localExposureNoiseGuard * 0.06f +
            context.localExposureHaloGuard * 0.04f -
            context.localExposureShadowOpening * 0.04f,
        -1.0f,
        1.0f);
    prepSettings.haloGuard = std::clamp(
        prepSettings.haloGuard +
            context.localExposureHaloGuard * 0.06f,
        0.0f,
        1.0f);
    prepSettings.smoothGradientProtection = std::clamp(
        prepSettings.smoothGradientProtection +
            context.localExposureHaloGuard * 0.05f,
        0.0f,
        1.0f);
    prepSettings.edgeAwareness = std::clamp(
        prepSettings.edgeAwareness +
            context.localExposureHaloGuard * 0.04f +
            context.localExposureTextureGuard * 0.03f,
        0.0f,
        1.0f);
    prepSettings.textureSensitivity = std::clamp(
        prepSettings.textureSensitivity +
            context.localExposureTextureGuard * 0.04f -
            context.localExposureNoiseGuard * 0.02f,
        0.0f,
        1.0f);
    ClampIntegratedDevelopScenePrepSettings(prepSettings);
    payload.scenePrepSettings = prepSettings;
    return prepSettings;
}

} // namespace Stack::Editor::DevelopAutoSolveApplication
