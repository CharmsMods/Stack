#include "Editor/Internal/EditorModuleDevelopCandidateRenderPayload.h"

#include "Editor/EditorModule.h"
#include "Editor/Internal/EditorModuleDevelopCandidateShared.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace Stack::Editor::DevelopCandidate {

namespace {

constexpr const char* kDevelopLocalExposureStrategyVersion = "LocalExposureStrategyV1";

void ClampCandidateMosaicDenoiseSettings(Raw::RawMosaicDenoiseSettings& settings) {
    settings.hotPixelThreshold = std::clamp(settings.hotPixelThreshold, 0.005f, 0.50f);
    settings.lumaStrength = std::clamp(settings.lumaStrength, 0.0f, 1.0f);
    settings.chromaStrength = std::clamp(settings.chromaStrength, 0.0f, 1.0f);
    settings.radius = std::clamp(settings.radius, 1, 4);
    settings.edgeProtection = std::clamp(settings.edgeProtection, 0.0f, 1.0f);
    settings.iterations = std::clamp(settings.iterations, 1, 2);
}

} // namespace

EditorNodeGraph::DevelopAutoGuidance ReadDevelopAuthoredGuidanceFromToneJson(
    const nlohmann::json& toneJson,
    EditorNodeGraph::DevelopAutoGuidance fallback) {
    if (!toneJson.is_object()) {
        EditorModule::NormalizeDevelopAutoGuidance(fallback);
        return fallback;
    }

    fallback.autoStrength = toneJson.value("autoSceneAssistStrength", fallback.autoStrength);
    fallback.exposureBias = toneJson.value("autoBrightnessIntent", fallback.exposureBias);
    fallback.dynamicRange = toneJson.value("autoDynamicRange", fallback.dynamicRange);
    fallback.shadowLift = toneJson.value("autoShadowBias", fallback.shadowLift);
    fallback.highlightGuard = toneJson.value("autoHighlightBias", fallback.highlightGuard);
    fallback.highlightCharacter = toneJson.value("autoHighlightCharacter", fallback.highlightCharacter);
    fallback.contrastBias = toneJson.value("autoContrastBias", fallback.contrastBias);
    fallback.subjectSceneBias = toneJson.value("autoSubjectSceneBias", fallback.subjectSceneBias);
    fallback.moodReadabilityBias = toneJson.value("autoMoodReadabilityBias", fallback.moodReadabilityBias);
    EditorModule::NormalizeDevelopAutoGuidance(fallback);
    return fallback;
}

namespace {

void ClampCandidateScenePrepSettings(Raw::RawDetailFusionSettings& settings) {
    settings.mode = Raw::RawDetailFusionMode::AutoAnalyze;
    settings.debugView = Raw::RawDetailFusionDebugView::FinalImage;
    settings.invertMask = false;
    settings.maskBlackPoint = std::clamp(settings.maskBlackPoint, 0.0f, 1.0f);
    settings.maskWhitePoint = std::clamp(settings.maskWhitePoint, settings.maskBlackPoint + 0.001f, 1.0f);
    settings.maskGamma = std::clamp(settings.maskGamma, 0.05f, 8.0f);
    settings.minEvBias = std::clamp(settings.minEvBias, -2.0f, 2.0f);
    settings.maxEvBias = std::clamp(settings.maxEvBias, -2.0f, 2.0f);
    settings.baseEvBias = std::clamp(settings.baseEvBias, -1.25f, 1.25f);
    settings.noiseProtectionBias = std::clamp(settings.noiseProtectionBias, -1.0f, 1.0f);
    settings.highlightProtectionBias = std::clamp(settings.highlightProtectionBias, -1.0f, 1.0f);
    settings.shadowLiftLimitBias = std::clamp(settings.shadowLiftLimitBias, -1.0f, 1.0f);
    settings.wellExposedTargetBias = std::clamp(settings.wellExposedTargetBias, -1.0f, 1.0f);
    settings.strength = std::clamp(settings.strength, 0.0f, 1.25f);
    settings.manualBlend = 0.0f;
}

void PreserveCandidateRawCleanupSettings(
    Raw::RawDevelopSettings& settings,
    const Raw::RawDevelopSettings& baseSettings) {
    const float falseColorSuppression = settings.falseColorSuppression;
    const float defringeStrength = settings.defringeStrength;
    const float highlightEdgeCleanup = settings.highlightEdgeCleanup;
    const int chromaRadius = settings.chromaRadius;
    const float preserveRealColor = settings.preserveRealColor;
    const float lateralRedCyan = settings.lateralRedCyan;
    const float lateralBlueYellow = settings.lateralBlueYellow;
    const Raw::RawMosaicDenoiseSettings mosaicDenoise = settings.mosaicDenoise;

    settings = baseSettings;
    settings.debugView = Raw::RawDebugView::FinalOutput;
    settings.falseColorSuppression = falseColorSuppression;
    settings.defringeStrength = defringeStrength;
    settings.highlightEdgeCleanup = highlightEdgeCleanup;
    settings.chromaRadius = chromaRadius;
    settings.preserveRealColor = preserveRealColor;
    settings.lateralRedCyan = lateralRedCyan;
    settings.lateralBlueYellow = lateralBlueYellow;
    settings.mosaicDenoise = mosaicDenoise;
}

} // namespace

void ApplyDevelopGuidanceToCandidateRenderPayload(
    RenderGraphRawDevelopPayload& payload,
    const EditorNodeGraph::DevelopAutoGuidance& currentGuidance,
    const EditorNodeGraph::DevelopAutoGuidance& candidateGuidance,
    const std::string& candidateId,
    EditorNodeGraph::DevelopAutoIntent intent,
    const Raw::WhiteBalanceMode* whiteBalanceOverride) {
    const Raw::RawDevelopSettings baseRawSettings = payload.settings;
    const Raw::RawDetailFusionSettings baseScenePrepSettings = payload.scenePrepSettings;
    const std::string revisionStage = DevelopRenderedRevisionStageForCandidateId(candidateId);
    const float autoStrengthDelta = candidateGuidance.autoStrength - currentGuidance.autoStrength;
    const float brightnessDelta = candidateGuidance.exposureBias - currentGuidance.exposureBias;
    const float rangeDelta = candidateGuidance.dynamicRange - currentGuidance.dynamicRange;
    const float shadowDelta = candidateGuidance.shadowLift - currentGuidance.shadowLift;
    const float highlightDelta = candidateGuidance.highlightGuard - currentGuidance.highlightGuard;
    const float highlightCharacterDelta = candidateGuidance.highlightCharacter - currentGuidance.highlightCharacter;
    const float contrastDelta = candidateGuidance.contrastBias - currentGuidance.contrastBias;
    const bool cleanShadowProbe =
        candidateId == "cleanShadows" ||
        candidateId == "renderedLocalCleanShadows";
    const bool texturePreserveProbe =
        candidateId == "preserveTexture" ||
        candidateId == "renderedLocalPreserveTexture";
    const bool shadowOpeningProbe = candidateId == "renderedLocalShadowOpening";
    const bool protectedMidsProbe = candidateId == "highlightProtectedMids";
    const bool broadHighlightGuardProbe = candidateId == "broadHighlightGuard";
    const bool haloSafeLocalRangeProbe = candidateId == "haloSafeLocalRange";
    const bool localRangeGuardProbe = candidateId == "localRangeGuard";
    const bool shadowReadabilityLiftProbe = candidateId == "shadowReadabilityLift";
    const bool shadowNoiseFloorProbe = candidateId == "shadowNoiseFloor";
    const bool subjectReadableMidsProbe = candidateId == "subjectReadableMids";
    const bool sceneMoodPreservationProbe = candidateId == "sceneMoodPreservation";
    const bool finishToneProbe = IsFinishToneProbeCandidateIdForRenderRequest(candidateId);
    nlohmann::json localExposureStrategy = nlohmann::json::object();
    if (payload.integratedToneLayerJson.is_object()) {
        localExposureStrategy =
            payload.integratedToneLayerJson.value(
                "autoDynamicRangeLocalExposureStrategy",
                nlohmann::json::object());
        if (!localExposureStrategy.is_object() ||
            localExposureStrategy.value("version", std::string()).empty()) {
            const nlohmann::json dynamicRangeStrategy =
                payload.integratedToneLayerJson.value(
                    "autoDynamicRangeStrategy",
                    nlohmann::json::object());
            if (dynamicRangeStrategy.is_object()) {
                localExposureStrategy =
                    dynamicRangeStrategy.value(
                        "localExposureStrategy",
                        nlohmann::json::object());
            }
        }
    }
    const bool hasLocalExposureStrategy =
        localExposureStrategy.is_object() &&
        localExposureStrategy.value("version", std::string()) == kDevelopLocalExposureStrategyVersion;
    const float localRangeRedistribution =
        hasLocalExposureStrategy ? localExposureStrategy.value("rangeRedistribution", 0.0f) : 0.0f;
    const float localHighlightCompression =
        hasLocalExposureStrategy ? localExposureStrategy.value("highlightCompression", 0.0f) : 0.0f;
    const float localShadowOpening =
        hasLocalExposureStrategy ? localExposureStrategy.value("shadowOpening", 0.0f) : 0.0f;
    const float localNoiseGuard =
        hasLocalExposureStrategy ? localExposureStrategy.value("noiseGuard", 0.0f) : 0.0f;
    const float localHaloGuard =
        hasLocalExposureStrategy ? localExposureStrategy.value("haloGuard", 0.0f) : 0.0f;
    const float localTextureGuard =
        hasLocalExposureStrategy ? localExposureStrategy.value("textureGuard", 0.0f) : 0.0f;
    const float localStrengthTarget =
        hasLocalExposureStrategy ? localExposureStrategy.value("strengthTarget", 0.5f) : 0.5f;
    Raw::WhiteBalanceMode whiteBalanceProbeMode = Raw::WhiteBalanceMode::AsShot;
    bool whiteBalanceProbe =
        TryResolveWhiteBalanceProbeCandidateModeForRenderRequest(
            candidateId,
            whiteBalanceProbeMode);
    if (whiteBalanceOverride) {
        whiteBalanceProbeMode = *whiteBalanceOverride;
        whiteBalanceProbe = true;
    }

    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    payload.settings.debugView = Raw::RawDebugView::FinalOutput;
    payload.settings.exposureStops = std::clamp(
        payload.settings.exposureStops +
            brightnessDelta * 1.45f +
            std::max(0.0f, shadowDelta) * 0.18f -
            std::max(0.0f, highlightDelta) * 0.12f,
        -8.0f,
        8.0f);
    payload.settings.highlightStrength = std::clamp(
        payload.settings.highlightStrength +
            std::max(0.0f, highlightDelta) * 0.20f +
            std::max(0.0f, rangeDelta) * 0.08f,
        0.0f,
        1.0f);
    payload.settings.highlightThreshold = std::clamp(
        payload.settings.highlightThreshold -
            std::max(0.0f, highlightDelta) * 0.025f -
            std::max(0.0f, rangeDelta) * 0.015f +
            std::max(0.0f, highlightCharacterDelta) * 0.010f,
        0.82f,
        0.995f);
    if (whiteBalanceProbe) {
        payload.settings.whiteBalanceMode = whiteBalanceProbeMode;
        payload.settings.manualWhiteBalance = baseRawSettings.manualWhiteBalance;
    }

    if (cleanShadowProbe || shadowOpeningProbe) {
        // Shadow-opening candidates need cleanup support, otherwise the rendered
        // probe only proves that lifted shadows got noisier.
        Raw::RawMosaicDenoiseSettings& denoise = payload.settings.mosaicDenoise;
        denoise.enabled = true;
        denoise.hotPixelSuppression = true;
        denoise.lumaStrength += 0.12f + std::max(0.0f, shadowDelta) * 0.10f;
        denoise.chromaStrength += 0.10f + std::max(0.0f, shadowDelta) * 0.08f;
        denoise.edgeProtection += 0.08f;
        denoise.radius = std::max(denoise.radius, shadowOpeningProbe ? 3 : 2);
        if (shadowOpeningProbe) {
            denoise.iterations = std::max(denoise.iterations, 2);
        }
        payload.settings.falseColorSuppression = std::clamp(
            payload.settings.falseColorSuppression + 0.05f,
            0.0f,
            1.0f);
        payload.settings.defringeStrength = std::clamp(
            payload.settings.defringeStrength + 0.03f,
            0.0f,
            1.0f);
        payload.settings.highlightEdgeCleanup = std::clamp(
            payload.settings.highlightEdgeCleanup + 0.04f,
            0.0f,
            1.0f);
        payload.settings.preserveRealColor = std::clamp(
            payload.settings.preserveRealColor + 0.04f,
            0.0f,
            1.0f);
        ClampCandidateMosaicDenoiseSettings(denoise);
    } else if (texturePreserveProbe) {
        // Texture probes deliberately keep a little more real grain/detail so
        // rendered metrics can compare natural texture against cleaner shadows.
        Raw::RawMosaicDenoiseSettings& denoise = payload.settings.mosaicDenoise;
        denoise.lumaStrength -= 0.10f;
        denoise.chromaStrength -= 0.06f;
        denoise.edgeProtection += 0.14f;
        denoise.hotPixelThreshold += 0.02f;
        payload.settings.falseColorSuppression = std::clamp(
            payload.settings.falseColorSuppression - 0.04f,
            0.0f,
            1.0f);
        payload.settings.defringeStrength = std::clamp(
            payload.settings.defringeStrength - 0.02f,
            0.0f,
            1.0f);
        payload.settings.preserveRealColor = std::clamp(
            payload.settings.preserveRealColor + 0.08f,
            0.0f,
            1.0f);
        ClampCandidateMosaicDenoiseSettings(denoise);
    }

    Raw::RawDetailFusionSettings prep = payload.scenePrepSettings;
    // Candidate renders are probes of nearby authored states: bias the existing
    // solved payload instead of re-running the full Auto solve inside the worker.
    prep.strength += autoStrengthDelta * 0.16f + std::max(0.0f, rangeDelta) * 0.10f;
    prep.maxEvBias += shadowDelta * 0.95f + std::max(0.0f, rangeDelta) * 0.35f;
    prep.minEvBias -= std::max(0.0f, highlightDelta) * 0.30f + std::max(0.0f, rangeDelta) * 0.25f;
    prep.baseEvBias += brightnessDelta * 0.35f;
    prep.highlightProtectionBias += highlightDelta * 0.65f + std::max(0.0f, rangeDelta) * 0.10f;
    prep.noiseProtectionBias += std::max(0.0f, shadowDelta) * 0.10f + std::max(0.0f, rangeDelta) * 0.05f;
    prep.shadowLiftLimitBias -= std::max(0.0f, shadowDelta) * 0.10f + std::max(0.0f, rangeDelta) * 0.08f;
    prep.wellExposedTargetBias += contrastDelta * 0.16f + brightnessDelta * 0.05f;
    if (hasLocalExposureStrategy) {
        prep.strength +=
            (localStrengthTarget - 0.5f) * 0.06f +
            localRangeRedistribution * 0.04f -
            localHaloGuard * 0.02f;
        prep.maxEvBias +=
            localShadowOpening * 0.05f +
            localRangeRedistribution * 0.04f -
            localNoiseGuard * 0.03f -
            localHaloGuard * 0.03f;
        prep.minEvBias -=
            localHighlightCompression * 0.05f +
            localRangeRedistribution * 0.03f;
        prep.highlightProtectionBias += localHighlightCompression * 0.04f;
        prep.noiseProtectionBias += localNoiseGuard * 0.04f;
        prep.shadowLiftLimitBias +=
            localNoiseGuard * 0.03f +
            localHaloGuard * 0.02f -
            localShadowOpening * 0.02f;
        prep.haloGuard = std::clamp(prep.haloGuard + localHaloGuard * 0.03f, 0.0f, 1.0f);
        prep.smoothGradientProtection = std::clamp(
            prep.smoothGradientProtection + localHaloGuard * 0.03f,
            0.0f,
            1.0f);
        prep.edgeAwareness = std::clamp(
            prep.edgeAwareness + localHaloGuard * 0.02f + localTextureGuard * 0.02f,
            0.0f,
            1.0f);
        prep.textureSensitivity = std::clamp(
            prep.textureSensitivity + localTextureGuard * 0.03f - localNoiseGuard * 0.01f,
            0.0f,
            1.0f);
    }
    if (cleanShadowProbe || shadowOpeningProbe) {
        prep.noiseProtectionBias += 0.10f;
        prep.shadowLiftLimitBias += cleanShadowProbe ? 0.06f : 0.00f;
        prep.textureSensitivity = std::clamp(prep.textureSensitivity - 0.04f, 0.0f, 1.0f);
    } else if (texturePreserveProbe) {
        prep.noiseProtectionBias -= 0.06f;
        prep.shadowLiftLimitBias += 0.05f;
        prep.detailWeight = std::clamp(prep.detailWeight + 0.08f, 0.0f, 1.0f);
        prep.textureSensitivity = std::clamp(prep.textureSensitivity + 0.12f, 0.0f, 1.0f);
        prep.edgeAwareness = std::clamp(prep.edgeAwareness + 0.06f, 0.0f, 1.0f);
    } else if (protectedMidsProbe) {
        // This probe tests the Guide 02/03 "lower global placement, support mids
        // locally" family without introducing a separate render algorithm.
        prep.maxEvBias += 0.10f;
        prep.highlightProtectionBias += 0.08f;
        prep.wellExposedTargetBias += 0.03f;
    } else if (broadHighlightGuardProbe) {
        // Broad meaningful highlights need local compression, not the
        // tiny-specular exception path. Keep RAW frozen and test Scene Prep
        // highlight restraint with extra halo/smooth-gradient protection.
        prep.minEvBias -= 0.16f + localHighlightCompression * 0.08f;
        prep.maxEvBias -= 0.04f;
        prep.highlightProtectionBias += 0.18f + localHighlightCompression * 0.06f;
        prep.haloGuard = std::clamp(prep.haloGuard + 0.08f + localHaloGuard * 0.03f, 0.0f, 1.0f);
        prep.smoothGradientProtection = std::clamp(
            prep.smoothGradientProtection + 0.06f + localHaloGuard * 0.02f,
            0.0f,
            1.0f);
        prep.wellExposedTargetBias -= 0.03f;
    } else if (localRangeGuardProbe) {
        // Rendered regional evidence asked for a scene-prep/local-range check,
        // so bias only local exposure/range safety while RAW stays frozen below.
        prep.maxEvBias += 0.10f + localRangeRedistribution * 0.08f + localShadowOpening * 0.04f;
        prep.minEvBias -= 0.08f + localRangeRedistribution * 0.05f + localHighlightCompression * 0.03f;
        prep.highlightProtectionBias += 0.10f + localHighlightCompression * 0.04f;
        prep.haloGuard = std::clamp(prep.haloGuard + 0.08f + localHaloGuard * 0.05f, 0.0f, 1.0f);
        prep.wellExposedTargetBias -= 0.02f;
    } else if (haloSafeLocalRangeProbe) {
        // This Guide 04 safety probe backs away from aggressive local EV
        // moves and raises anti-halo/gradient protection to check whether
        // the image needs safer local exposure rather than more range.
        prep.strength -= 0.08f + localHaloGuard * 0.03f;
        prep.maxEvBias -= 0.14f + localRangeRedistribution * 0.04f;
        prep.minEvBias += 0.04f;
        prep.highlightProtectionBias += 0.08f;
        prep.shadowLiftLimitBias += 0.08f + localHaloGuard * 0.04f;
        prep.haloGuard = std::clamp(prep.haloGuard + 0.16f + localHaloGuard * 0.06f, 0.0f, 1.0f);
        prep.smoothGradientProtection = std::clamp(
            prep.smoothGradientProtection + 0.14f + localHaloGuard * 0.05f,
            0.0f,
            1.0f);
        prep.edgeAwareness = std::clamp(prep.edgeAwareness + 0.10f + localHaloGuard * 0.04f, 0.0f, 1.0f);
        prep.textureSensitivity = std::clamp(prep.textureSensitivity + 0.04f + localTextureGuard * 0.03f, 0.0f, 1.0f);
        prep.wellExposedTargetBias -= 0.03f;
    } else if (shadowReadabilityLiftProbe) {
        // This is the positive Guide 04 shadow path: open readable shadows
        // locally while preserving RAW placement and leaving noise guardrails on.
        prep.maxEvBias += 0.16f + localShadowOpening * 0.08f;
        prep.baseEvBias += 0.03f;
        prep.noiseProtectionBias += 0.04f + localNoiseGuard * 0.03f;
        prep.shadowLiftLimitBias -= 0.10f + localShadowOpening * 0.04f;
        prep.wellExposedTargetBias += 0.04f;
        prep.textureSensitivity = std::clamp(prep.textureSensitivity + 0.04f, 0.0f, 1.0f);
        prep.edgeAwareness = std::clamp(prep.edgeAwareness + 0.04f, 0.0f, 1.0f);
        prep.haloGuard = std::clamp(prep.haloGuard + 0.03f, 0.0f, 1.0f);
    } else if (subjectReadableMidsProbe) {
        // Guide 05 subject-readable probes open likely/marked important mids
        // through Scene Prep so RAW exposure and downstream tone are not
        // confused with the subject-intent question being tested.
        prep.maxEvBias += 0.14f + localShadowOpening * 0.06f;
        prep.baseEvBias += 0.03f;
        prep.highlightProtectionBias += 0.05f;
        prep.noiseProtectionBias += 0.06f + localNoiseGuard * 0.03f;
        prep.shadowLiftLimitBias -= 0.08f + localShadowOpening * 0.03f;
        prep.wellExposedTargetBias += 0.05f;
        prep.haloGuard = std::clamp(prep.haloGuard + 0.04f + localHaloGuard * 0.02f, 0.0f, 1.0f);
        prep.edgeAwareness = std::clamp(prep.edgeAwareness + 0.05f, 0.0f, 1.0f);
        prep.textureSensitivity = std::clamp(prep.textureSensitivity + 0.03f, 0.0f, 1.0f);
    } else if (sceneMoodPreservationProbe) {
        // This is the Guide 05 counter-probe for silhouettes/low-key scenes:
        // reduce local lifting pressure so subject importance stays a bias
        // rather than an automatic command to neutralize the scene mood.
        prep.maxEvBias -= 0.14f + localNoiseGuard * 0.03f;
        prep.baseEvBias -= 0.02f;
        prep.highlightProtectionBias += 0.05f;
        prep.noiseProtectionBias += 0.10f + localNoiseGuard * 0.04f;
        prep.shadowLiftLimitBias += 0.10f + localHaloGuard * 0.03f;
        prep.wellExposedTargetBias -= 0.04f;
        prep.haloGuard = std::clamp(prep.haloGuard + 0.05f, 0.0f, 1.0f);
        prep.smoothGradientProtection = std::clamp(prep.smoothGradientProtection + 0.04f, 0.0f, 1.0f);
    } else if (shadowNoiseFloorProbe) {
        // Guide 04 allows darkness to remain dark when lifting would reveal
        // noise or gray mush. Test that as a Scene Prep limit, not a RAW EV move.
        prep.maxEvBias -= 0.18f + localNoiseGuard * 0.04f;
        prep.noiseProtectionBias += 0.16f + localNoiseGuard * 0.06f;
        prep.shadowLiftLimitBias += 0.14f + localNoiseGuard * 0.06f;
        prep.haloGuard = std::clamp(prep.haloGuard + 0.05f, 0.0f, 1.0f);
        prep.wellExposedTargetBias -= 0.04f;
        prep.detailWeight = std::clamp(prep.detailWeight + 0.04f, 0.0f, 1.0f);
    }
    ClampCandidateScenePrepSettings(prep);
    payload.scenePrepSettings = prep;

    bool frozeRawStage = false;
    bool frozeScenePrepStage = false;
    std::string stageConstraintReason;
    if (revisionStage == "scenePrep" || revisionStage == "finishTone") {
        // Scene-prep and finish-tone probes should validate downstream choices,
        // not silently become new RAW exposure/highlight/WB experiments.
        payload.settings = baseRawSettings;
        payload.settings.debugView = Raw::RawDebugView::FinalOutput;
        frozeRawStage = true;
        stageConstraintReason =
            revisionStage == "scenePrep"
                ? "Scene-prep candidate render preserves RAW-stage placement so local exposure changes are measured cleanly."
                : "Finish-tone candidate render preserves RAW and scene prep so contrast/finish changes are measured cleanly.";
    } else if (revisionStage == "rawCleanup") {
        // Cleanup/detail probes may alter denoise and cleanup fields, but not
        // global RAW placement; otherwise noise/detail metrics are confounded by EV shifts.
        PreserveCandidateRawCleanupSettings(payload.settings, baseRawSettings);
        frozeRawStage = true;
        stageConstraintReason =
            "RAW cleanup candidate render preserves global RAW placement while varying cleanup/detail fields.";
    }

    if (revisionStage == "finishTone") {
        payload.scenePrepSettings = baseScenePrepSettings;
        frozeScenePrepStage = true;
    }

    if (!payload.integratedToneLayerJson.is_object()) {
        payload.integratedToneLayerJson = nlohmann::json::object();
    }
    nlohmann::json& toneJson = payload.integratedToneLayerJson;
    toneJson["autoIntent"] = EditorNodeGraph::DevelopAutoIntentStableString(intent);
    toneJson["autoSceneAssistStrength"] = candidateGuidance.autoStrength;
    toneJson["autoBrightnessIntent"] = candidateGuidance.exposureBias;
    toneJson["autoRawExposurePreferenceEv"] = candidateGuidance.exposureBias * 2.0f;
    toneJson["autoDynamicRange"] = candidateGuidance.dynamicRange;
    toneJson["autoShadowBias"] = candidateGuidance.shadowLift;
    toneJson["autoHighlightBias"] = candidateGuidance.highlightGuard;
    toneJson["autoHighlightCharacter"] = candidateGuidance.highlightCharacter;
    toneJson["autoContrastBias"] = candidateGuidance.contrastBias;
    toneJson["autoCandidateRenderedProbeId"] = candidateId;
    toneJson["autoCandidateStageConstraint"] = revisionStage;
    toneJson["autoCandidateStageConstraintApplied"] = frozeRawStage || frozeScenePrepStage;
    toneJson["autoCandidateStageConstraintFrozenRaw"] = frozeRawStage;
    toneJson["autoCandidateStageConstraintFrozenScenePrep"] = frozeScenePrepStage;
    if (!stageConstraintReason.empty()) {
        toneJson["autoCandidateStageConstraintReason"] = stageConstraintReason;
    }
    if (cleanShadowProbe || texturePreserveProbe) {
        toneJson["autoCandidateCleanupProbe"] =
            texturePreserveProbe ? "preserveTexture" : "cleanerShadows";
    }
    if (finishToneProbe) {
        toneJson["autoCandidateFinishToneProbe"] = candidateId;
    }
    if (broadHighlightGuardProbe || haloSafeLocalRangeProbe || localRangeGuardProbe || shadowReadabilityLiftProbe || shadowNoiseFloorProbe || subjectReadableMidsProbe || sceneMoodPreservationProbe) {
        toneJson["autoCandidateScenePrepProbe"] = candidateId;
    }
    if (subjectReadableMidsProbe || sceneMoodPreservationProbe) {
        toneJson["autoCandidateSubjectIntentProbe"] = candidateId;
    }
    if (hasLocalExposureStrategy) {
        toneJson["autoCandidateLocalExposureStrategyVersion"] =
            kDevelopLocalExposureStrategyVersion;
        toneJson["autoCandidateLocalExposureStrategy"] = localExposureStrategy;
        toneJson["autoCandidateLocalExposureStrategyId"] =
            localExposureStrategy.value("id", std::string());
        toneJson["autoCandidateLocalExposureRangeRedistribution"] =
            localRangeRedistribution;
        toneJson["autoCandidateLocalExposureHighlightCompression"] =
            localHighlightCompression;
        toneJson["autoCandidateLocalExposureShadowOpening"] =
            localShadowOpening;
        toneJson["autoCandidateLocalExposureNoiseGuard"] =
            localNoiseGuard;
        toneJson["autoCandidateLocalExposureHaloGuard"] =
            localHaloGuard;
        toneJson["autoCandidateLocalExposureTextureGuard"] =
            localTextureGuard;
        toneJson["autoCandidateLocalExposureStrengthTarget"] =
            localStrengthTarget;
    }
    if (whiteBalanceProbe) {
        toneJson["autoCandidateWhiteBalanceProbe"] = candidateId;
        toneJson["autoCandidateWhiteBalanceMode"] =
            Raw::WhiteBalanceModeName(whiteBalanceProbeMode);
    }
    toneJson["autoCalibratePending"] = true;
    toneJson["autoCalibrateVariant"] = 0;
    toneJson["autoCalibrateRequestId"] =
        toneJson.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0)) + 1;
}


} // namespace Stack::Editor::DevelopCandidate
