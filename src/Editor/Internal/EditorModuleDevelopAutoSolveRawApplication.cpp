#include "Editor/Internal/EditorModuleDevelopAutoSolveApplicationContext.h"

#include <algorithm>

namespace Stack::Editor::DevelopAutoSolveApplication {

namespace {

void ClampRawMosaicDenoiseSettings(Raw::RawMosaicDenoiseSettings& settings) {
    settings.hotPixelThreshold = std::clamp(settings.hotPixelThreshold, 0.01f, 0.50f);
    settings.lumaStrength = std::clamp(settings.lumaStrength, 0.0f, 1.0f);
    settings.chromaStrength = std::clamp(settings.chromaStrength, 0.0f, 1.0f);
    settings.radius = std::clamp(settings.radius, 1, 5);
    settings.edgeProtection = std::clamp(settings.edgeProtection, 0.0f, 1.0f);
    settings.iterations = std::clamp(settings.iterations, 1, 4);
}

bool HasMeaningfulRawWhiteBalanceMetadata(const Raw::RawMetadata& metadata) {
    return metadata.cameraWhiteBalance[0] > 0.001f &&
        metadata.cameraWhiteBalance[1] > 0.001f &&
        metadata.cameraWhiteBalance[2] > 0.001f &&
        metadata.cameraWhiteBalance[3] > 0.001f;
}

} // namespace

void ApplyDevelopSelectedRawSettings(
    EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawMetadata& metadata,
    const DevelopDynamicRange::DevelopToneAutoStats& stats,
    const DevelopCandidateScoring::DevelopAutoCandidateSolveResult& candidateSolve,
    const DevelopSelectedAutoSolveContext& context,
    bool rewriteRawSettings) {
    if (!rewriteRawSettings) {
        return;
    }

    const Raw::RawDevelopSettings& defaults = context.defaults;
    Raw::RawDevelopSettings authoredSettings = payload.settings;
    authoredSettings.debugView = Raw::RawDebugView::FinalOutput;
    authoredSettings.cameraTransformEnabled = true;
    authoredSettings.debugBypassCameraTransform = false;
    authoredSettings.debugTransposeCameraMatrix = false;
    authoredSettings.cameraTransformSource = defaults.cameraTransformSource;

    float authoredExposure =
        defaults.exposureStops +
        context.userBiasEv +
        context.rawExposureBias +
        context.shadowBoost * context.shadowBoostScale * context.rawLiftHeadroomScale +
        context.recommendedLift * context.recommendedLiftScale * context.rawLiftHeadroomScale +
        context.broadHighlightPlacementLift +
        context.recommendedTrim -
        context.highlightExposurePenalty;
    if (context.stableSceneGuard > 0.0001f) {
        const float stableAnchor = defaults.exposureStops + context.userBiasEv + context.rawExposureBias * 0.25f;
        const float guardBlend = 0.22f * context.stableSceneGuard;
        authoredExposure = authoredExposure * (1.0f - guardBlend) + stableAnchor * guardBlend;
    }
    if (stats.valid &&
        stats.midtonePercentile > 0.14f &&
        stats.highlightPressure < 0.55f &&
        authoredExposure < defaults.exposureStops + context.userBiasEv - 0.12f) {
        authoredExposure = defaults.exposureStops + context.userBiasEv - 0.12f;
    }
    authoredSettings.exposureStops = std::clamp(authoredExposure, -8.0f, 8.0f);

    authoredSettings.whiteBalanceMode =
        HasMeaningfulRawWhiteBalanceMetadata(metadata)
            ? Raw::WhiteBalanceMode::AsShot
            : Raw::WhiteBalanceMode::Auto;
    authoredSettings.manualWhiteBalance = defaults.manualWhiteBalance;
    if (candidateSolve.authoredWhiteBalanceProbe) {
        authoredSettings.whiteBalanceMode = candidateSolve.authoredWhiteBalanceMode;
        authoredSettings.manualWhiteBalance = defaults.manualWhiteBalance;
    }

    if (context.highlightModeScore > 0.80f) {
        authoredSettings.highlightMode = Raw::HighlightReconstructionMode::ColorReconstruction;
    } else if (context.highlightModeScore > 0.45f) {
        authoredSettings.highlightMode = Raw::HighlightReconstructionMode::Luminance;
    } else if (context.highlightModeScore > 0.18f) {
        authoredSettings.highlightMode = Raw::HighlightReconstructionMode::ClipNeutral;
    } else {
        authoredSettings.highlightMode = Raw::HighlightReconstructionMode::Off;
    }
    authoredSettings.highlightStrength = std::clamp(0.35f + context.highlightModeScore * 0.42f, 0.15f, 1.0f);
    authoredSettings.highlightThreshold = std::clamp(
        0.985f - context.highlightModeScore * 0.08f - std::max(0.0f, context.highlightGuard) * 0.03f,
        0.82f,
        0.995f);

    authoredSettings.falseColorSuppression = std::clamp(
        0.18f + context.noiseNeed * 0.25f + context.highlightModeScore * 0.08f +
            context.darkNoisyLowLightDenoiseNeed * 0.14f,
        0.0f,
        1.0f);
    authoredSettings.defringeStrength = std::clamp(0.28f + context.highlightModeScore * 0.22f, 0.0f, 1.0f);
    authoredSettings.highlightEdgeCleanup = std::clamp(
        0.35f + context.highlightModeScore * 0.28f + std::max(0.0f, context.highlightGuard) * 0.10f,
        0.0f,
        1.0f);
    authoredSettings.preserveRealColor = std::clamp(
        0.68f +
            std::max(0.0f, context.highlightGuard) * 0.10f +
            context.highlightHeavyNeed * 0.06f -
            std::max(0.0f, context.highlightCharacter) * 0.08f -
            context.darkNoisyLowLightDenoiseNeed * 0.05f,
        0.0f,
        1.0f);
    authoredSettings.chromaRadius = context.highlightModeScore > 0.75f ? 2 : 1;
    authoredSettings.mosaicDenoise.enabled =
        context.noiseNeed > 0.18f ||
        context.shadowBoost > 0.55f ||
        context.sceneProfile == DevelopAutoSceneProfile::NoisyLowLight;
    authoredSettings.mosaicDenoise.hotPixelSuppression = context.noiseNeed > 0.25f;
    authoredSettings.mosaicDenoise.hotPixelThreshold = std::clamp(
        0.12f - context.noiseNeed * 0.03f - context.darkNoisyLowLightDenoiseNeed * 0.02f,
        0.05f,
        0.18f);
    authoredSettings.mosaicDenoise.lumaStrength = std::clamp(
        0.30f + context.noiseNeed * 0.38f + std::max(0.0f, context.shadowLift) * 0.08f +
            context.prepNoiseBias * 0.10f + context.darkNoisyLowLightDenoiseNeed * 0.24f,
        0.0f,
        1.0f);
    authoredSettings.mosaicDenoise.chromaStrength = std::clamp(
        0.48f + context.noiseNeed * 0.30f + context.highlightModeScore * 0.10f +
            context.prepNoiseBias * 0.06f + context.darkNoisyLowLightDenoiseNeed * 0.26f,
        0.0f,
        1.0f);
    authoredSettings.mosaicDenoise.radius =
        context.darkNoisyLowLightDenoiseNeed > 0.45f
            ? 4
            : ((context.noiseNeed > 0.65f || context.sceneProfile == DevelopAutoSceneProfile::NoisyLowLight) ? 3 : 2);
    authoredSettings.mosaicDenoise.edgeProtection = std::clamp(
        0.55f + context.highlightModeScore * 0.18f - std::max(0.0f, context.shadowLift) * 0.06f -
            context.darkNoisyLowLightDenoiseNeed * 0.08f,
        0.0f,
        1.0f);
    authoredSettings.mosaicDenoise.iterations =
        (context.noiseNeed > 0.70f ||
         (context.sceneProfile == DevelopAutoSceneProfile::NoisyLowLight && context.noiseNeed > 0.58f)) ? 2 : 1;
    ClampRawMosaicDenoiseSettings(authoredSettings.mosaicDenoise);
    payload.settings = authoredSettings;
}

} // namespace Stack::Editor::DevelopAutoSolveApplication
