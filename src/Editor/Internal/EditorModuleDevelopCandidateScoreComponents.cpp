#include "Editor/Internal/EditorModuleDevelopCandidateScoreComponents.h"

#include <algorithm>

namespace {

constexpr const char* kDevelopContinuationCandidateBiasVersion = "ContinuationCandidateBiasV1";
constexpr const char* kDevelopContinuationCandidateExpansionVersion = "ContinuationCandidateExpansionV1";

float SaturateFloat(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

} // namespace

namespace Stack::Editor::DevelopCandidateScoring {

using namespace Stack::Editor::DevelopDynamicRange;
using namespace Stack::Editor::DevelopSubjectImportance;

nlohmann::json BuildDevelopAutoCandidateScoreComponents(
    const DevelopAutoCandidateSolve& candidate,
    const EditorNodeGraph::DevelopAutoGuidance& base,
    EditorNodeGraph::DevelopAutoIntent intent,
    const DevelopToneAutoStats& stats,
    const DevelopDynamicRangeRegionEvidence& regionEvidence,
    const DevelopDynamicRangeStrategy& dynamicRangeStrategy,
    const DevelopSubjectSceneIntent& subjectSceneIntent,
    float darkness,
    float shadowRescueNeed,
    float hdrNeed,
    float flatSceneNeed,
    float underBrightBroadHighlightEv) {
    const float exposureDelta = candidate.guidance.exposureBias - base.exposureBias;
    const float rangeDelta = candidate.guidance.dynamicRange - base.dynamicRange;
    const float shadowDelta = candidate.guidance.shadowLift - base.shadowLift;
    const float highlightGuardDelta = candidate.guidance.highlightGuard - base.highlightGuard;
    const float highlightCharacterDelta = candidate.guidance.highlightCharacter - base.highlightCharacter;
    const float contrastDelta = candidate.guidance.contrastBias - base.contrastBias;
    const float positiveExposureDelta = std::max(0.0f, exposureDelta);
    const float positiveRangeDelta = std::max(0.0f, rangeDelta);
    const float positiveShadowDelta = std::max(0.0f, shadowDelta);
    const float positiveContrastDelta = std::max(0.0f, contrastDelta);
    const float negativeContrastDelta = std::max(0.0f, -contrastDelta);
    const float underBrightNeed = SaturateFloat(underBrightBroadHighlightEv / 1.25f);
    const float mapHighlightBias =
        std::max(0.0f, dynamicRangeStrategy.strategyMapHighlightPriority - 0.5f);
    const float mapShadowBias =
        std::max(0.0f, dynamicRangeStrategy.strategyMapShadowVisibility - 0.5f);
    const float mapContrastBias =
        std::max(0.0f, dynamicRangeStrategy.strategyMapNaturalContrast - 0.5f);
    const float mapRangeBias =
        std::max(0.0f, dynamicRangeStrategy.strategyMapVisibleRange - 0.5f);
    const float subjectPriorityBias =
        std::max(0.0f, subjectSceneIntent.subjectPriority - 0.5f);
    const float subjectReadabilityBias =
        std::max(0.0f, subjectSceneIntent.improveReadability - 0.5f);
    const float subjectMoodBias =
        std::max(0.0f, subjectSceneIntent.preserveMood - 0.5f);
    const float subjectProtectionBias =
        std::max(0.0f, subjectSceneIntent.protectionPressure);
    const float subjectRefinedConfidenceBias =
        SaturateFloat(subjectSceneIntent.refinedMapConfidence + subjectSceneIntent.refinedMapCoverage * 0.28f);
    const float subjectRefinedReadabilityBias =
        SaturateFloat(subjectSceneIntent.refinedMapReadabilityCoverage + subjectRefinedConfidenceBias * 0.20f);
    const float subjectRefinedProtectionBias =
        SaturateFloat(subjectSceneIntent.refinedMapProtectionCoverage + subjectRefinedConfidenceBias * 0.18f);
    const float subjectRefinedMoodBias =
        SaturateFloat(
            subjectSceneIntent.refinedMapMoodCoverage +
            subjectSceneIntent.refinedMapLowPriorityCoverage * 0.30f +
            subjectRefinedConfidenceBias * 0.12f);
    const float smallSpecularSignal =
        SaturateFloat((0.012f - stats.clippingRatio) / 0.012f) *
        SaturateFloat((0.44f - stats.highlightPressure) / 0.44f) *
        ((!regionEvidence.valid || regionEvidence.smallSpecularLikely) ? 1.0f : 0.45f);
    const float broadHighlightSignal = SaturateFloat(
        stats.highlightPressure * 0.50f +
        regionEvidence.broadHighlightPressure * 0.24f +
        regionEvidence.meaningfulHighlightPressure * 0.16f +
        regionEvidence.localHighlightHotspotRisk * 0.16f +
        stats.clippingRatio * 1.60f -
        smallSpecularSignal * 0.18f);
    const float shadowReadabilitySignal = SaturateFloat(
        shadowRescueNeed * 0.44f +
        regionEvidence.localShadowHotspotRisk * 0.18f +
        SaturateFloat((0.74f - stats.noiseRisk) / 0.74f) * 0.18f +
        stats.textureConfidence * 0.10f -
        regionEvidence.shadowNoiseLiftRisk * 0.18f -
        darkness * 0.04f);
    const float naturalContrastSignal = SaturateFloat(
        flatSceneNeed * 0.28f +
        regionEvidence.flatGrayRisk * 0.26f +
        regionEvidence.brightnessHierarchyRisk * 0.24f +
        regionEvidence.highlightGrayRisk * 0.18f +
        stats.textureConfidence * 0.08f -
        hdrNeed * 0.05f -
        regionEvidence.localHaloRisk * 0.04f);
    const float highlightBrightnessSignal = SaturateFloat(
        stats.highlightPressure * 0.24f +
        broadHighlightSignal * 0.24f +
        regionEvidence.brightnessHierarchyRisk * 0.22f +
        regionEvidence.highlightGrayRisk * 0.20f +
        regionEvidence.flatGrayRisk * 0.10f +
        highlightCharacterDelta * 0.10f +
        positiveContrastDelta * 0.08f -
        stats.clippingRatio * 0.80f -
        smallSpecularSignal * 0.06f);
    const float localHaloSafetySignal = SaturateFloat(
        regionEvidence.localHaloRisk * 0.50f +
        regionEvidence.localRangeConflict * 0.18f +
        regionEvidence.localEvConflict * 0.12f +
        regionEvidence.localHighlightHotspotRisk * 0.10f +
        regionEvidence.localShadowHotspotRisk * 0.08f);

    const float highlightDamageRisk = SaturateFloat(
        stats.highlightPressure * 0.55f +
        stats.clippingRatio * 2.20f +
        regionEvidence.meaningfulHighlightPressure * 0.10f +
        regionEvidence.localHighlightHotspotRisk * 0.18f +
        positiveExposureDelta * 0.28f -
        std::max(0.0f, highlightGuardDelta) * 0.18f -
        positiveRangeDelta * 0.06f);
    const float shadowNoiseRisk = SaturateFloat(
        stats.noiseRisk * 0.55f +
        regionEvidence.shadowNoiseLiftRisk * 0.22f +
        positiveShadowDelta * 0.18f +
        positiveRangeDelta * 0.08f -
        std::max(0.0f, -shadowDelta) * 0.08f);
    const float flatteningRisk = SaturateFloat(
        positiveRangeDelta * 0.18f +
        negativeContrastDelta * 0.20f +
        hdrNeed * 0.08f +
        regionEvidence.highlightGrayRisk * 0.12f +
        regionEvidence.brightnessHierarchyRisk * 0.14f);

    float midtonePlacement = SaturateFloat(
        0.48f +
        shadowRescueNeed * 0.18f +
        regionEvidence.localShadowHotspotRisk * 0.06f -
        regionEvidence.shadowNoiseLiftRisk * 0.04f +
        underBrightNeed * 0.14f +
        positiveExposureDelta * 0.12f -
        stats.highlightPressure * 0.10f -
        stats.noiseRisk * 0.06f);
    float highlightIntegrity = SaturateFloat(
        0.62f +
        std::max(0.0f, highlightGuardDelta) * 0.20f +
        positiveRangeDelta * 0.08f -
        positiveExposureDelta * 0.16f -
        stats.highlightPressure * 0.20f -
        regionEvidence.localHighlightHotspotRisk * 0.10f -
        stats.clippingRatio * 1.10f);
    float shadowCleanliness = SaturateFloat(
        0.58f -
        stats.noiseRisk * 0.22f -
        positiveShadowDelta * 0.08f +
        std::max(0.0f, -shadowDelta) * 0.06f +
        (candidate.id == "cleanShadows" || candidate.id == "renderedLocalCleanShadows" ? 0.16f : 0.0f));
    float dynamicRangeFit = SaturateFloat(
        0.44f +
        hdrNeed * 0.20f +
        regionEvidence.localRangeConflict * 0.12f +
        regionEvidence.localEvConflict * 0.08f +
        positiveRangeDelta * 0.22f +
        std::max(0.0f, highlightGuardDelta) * 0.08f -
        positiveContrastDelta * 0.06f +
        mapRangeBias * 0.08f);
    float contrastShape = SaturateFloat(
        0.48f +
        flatSceneNeed * 0.14f +
        positiveContrastDelta * 0.18f -
        positiveRangeDelta * 0.08f -
        negativeContrastDelta * 0.04f +
        highlightCharacterDelta * 0.08f +
        mapContrastBias * 0.06f);
    float brightnessHierarchy = SaturateFloat(
        0.54f +
        positiveContrastDelta * 0.08f +
        highlightCharacterDelta * 0.12f -
        positiveRangeDelta * 0.08f -
        negativeContrastDelta * 0.10f -
        flatteningRisk * 0.20f -
        regionEvidence.highlightGrayRisk * 0.08f -
        regionEvidence.brightnessHierarchyRisk * 0.10f);
    float naturalContrastGuard = SaturateFloat(
        0.42f +
        naturalContrastSignal * 0.34f +
        positiveContrastDelta * 0.12f +
        highlightCharacterDelta * 0.10f -
        positiveRangeDelta * 0.06f -
        negativeContrastDelta * 0.08f -
        regionEvidence.localHaloRisk * 0.05f);
    float luminousHighlightAnchor = SaturateFloat(
        0.42f +
        highlightBrightnessSignal * 0.34f +
        positiveContrastDelta * 0.08f +
        highlightCharacterDelta * 0.14f -
        positiveRangeDelta * 0.08f -
        negativeContrastDelta * 0.06f -
        stats.clippingRatio * 0.40f);
    float specularTolerance = SaturateFloat(
        0.42f +
        smallSpecularSignal * 0.36f +
        highlightCharacterDelta * 0.10f +
        positiveContrastDelta * 0.04f -
        stats.highlightPressure * 0.08f -
        regionEvidence.broadHighlightPressure * 0.10f);
    float broadHighlightControl = SaturateFloat(
        0.42f +
        broadHighlightSignal * 0.34f +
        std::max(0.0f, highlightGuardDelta) * 0.12f +
        positiveRangeDelta * 0.06f -
        smallSpecularSignal * 0.10f -
        positiveExposureDelta * 0.06f +
        mapHighlightBias * 0.08f);
    float meaningfulHighlightControl = SaturateFloat(
        0.42f +
        regionEvidence.meaningfulHighlightPressure * 0.34f +
        regionEvidence.highlightStructureScore * 0.12f +
        std::max(0.0f, highlightGuardDelta) * 0.10f +
        positiveRangeDelta * 0.06f -
        smallSpecularSignal * 0.12f -
        positiveExposureDelta * 0.06f);
    float shadowReadabilityLift = SaturateFloat(
        0.42f +
        shadowReadabilitySignal * 0.34f +
        positiveShadowDelta * 0.12f +
        positiveRangeDelta * 0.06f -
        stats.noiseRisk * 0.08f -
        regionEvidence.shadowNoiseLiftRisk * 0.12f +
        mapShadowBias * 0.08f);
    float strategyHighlightFit = SaturateFloat(
        0.42f +
        dynamicRangeStrategy.strategyMapHighlightPriority * 0.28f +
        std::max(0.0f, highlightGuardDelta) * 0.10f -
        positiveExposureDelta * 0.06f);
    float strategyShadowFit = SaturateFloat(
        0.42f +
        dynamicRangeStrategy.strategyMapShadowVisibility * 0.28f +
        positiveShadowDelta * 0.10f +
        positiveExposureDelta * 0.04f -
        stats.noiseRisk * 0.05f);
    float strategyVisibleRangeFit = SaturateFloat(
        0.42f +
        dynamicRangeStrategy.strategyMapVisibleRange * 0.30f +
        positiveRangeDelta * 0.12f +
        std::max(0.0f, highlightGuardDelta) * 0.04f -
        positiveContrastDelta * 0.04f);
    float strategyNaturalContrastFit = SaturateFloat(
        0.42f +
        dynamicRangeStrategy.strategyMapNaturalContrast * 0.30f +
        positiveContrastDelta * 0.12f +
        highlightCharacterDelta * 0.06f -
        positiveRangeDelta * 0.05f -
        negativeContrastDelta * 0.04f);
    float noiseTextureQuality = SaturateFloat(
        0.46f +
        stats.textureConfidence * 0.22f -
        stats.noiseRisk * 0.14f +
        (candidate.id == "preserveTexture" || candidate.id == "renderedLocalPreserveTexture" ? 0.16f : 0.0f) +
        (candidate.id == "cleanShadows" || candidate.id == "renderedLocalCleanShadows" ? 0.08f : 0.0f));
    float colorPlausibility = SaturateFloat(
        0.52f +
        (candidate.whiteBalanceProbe ? 0.04f : 0.0f) -
        stats.clippingRatio * 0.20f);
    float moodColorPreservation = SaturateFloat(
        0.54f +
        darkness * 0.08f -
        (candidate.whiteBalanceProbe ? 0.03f : 0.0f));
    float localArtifactSafety = SaturateFloat(
        1.0f - std::max(
            std::max(highlightDamageRisk, flatteningRisk),
            std::max(
                std::max(regionEvidence.localHaloRisk * 0.60f, regionEvidence.localEvConflict * 0.22f),
                regionEvidence.localExposureDamageRisk * 0.70f)));
    float localHaloSafety = SaturateFloat(
        0.44f +
        localHaloSafetySignal * 0.34f +
        std::max(0.0f, highlightGuardDelta) * 0.06f -
        positiveRangeDelta * 0.06f -
        positiveShadowDelta * 0.04f);

    if (candidate.id == "brighterMids" || candidate.id == "renderedLocalBrightenMids") {
        midtonePlacement = SaturateFloat(midtonePlacement + 0.12f);
        strategyShadowFit = SaturateFloat(strategyShadowFit + 0.06f);
    } else if (candidate.id == "maximumRange") {
        dynamicRangeFit = SaturateFloat(dynamicRangeFit + 0.16f);
        strategyVisibleRangeFit = SaturateFloat(strategyVisibleRangeFit + 0.16f);
    } else if (candidate.id == "broadHighlightGuard") {
        highlightIntegrity = SaturateFloat(highlightIntegrity + 0.12f);
        dynamicRangeFit = SaturateFloat(dynamicRangeFit + 0.08f);
        broadHighlightControl = SaturateFloat(broadHighlightControl + 0.18f);
        meaningfulHighlightControl = SaturateFloat(meaningfulHighlightControl + 0.18f);
        brightnessHierarchy = SaturateFloat(brightnessHierarchy - 0.03f);
        localArtifactSafety = SaturateFloat(localArtifactSafety + 0.04f);
        strategyHighlightFit = SaturateFloat(strategyHighlightFit + 0.14f);
        strategyVisibleRangeFit = SaturateFloat(strategyVisibleRangeFit + 0.06f);
    } else if (candidate.id == "preserveMood") {
        midtonePlacement = SaturateFloat(midtonePlacement - 0.06f + darkness * 0.16f);
        strategyNaturalContrastFit = SaturateFloat(strategyNaturalContrastFit + 0.05f);
    } else if (candidate.id == "naturalContrastGuard") {
        contrastShape = SaturateFloat(contrastShape + 0.14f);
        brightnessHierarchy = SaturateFloat(brightnessHierarchy + 0.16f);
        naturalContrastGuard = SaturateFloat(naturalContrastGuard + 0.20f);
        dynamicRangeFit = SaturateFloat(dynamicRangeFit - 0.03f);
        localArtifactSafety = SaturateFloat(localArtifactSafety + 0.02f);
        strategyNaturalContrastFit = SaturateFloat(strategyNaturalContrastFit + 0.16f);
    } else if (candidate.id == "strongerContrast" || candidate.id == "renderedLocalContrastShape") {
        contrastShape = SaturateFloat(contrastShape + 0.16f);
        strategyNaturalContrastFit = SaturateFloat(strategyNaturalContrastFit + 0.10f);
    } else if (candidate.id == "toneSofterRolloff") {
        highlightIntegrity = SaturateFloat(highlightIntegrity + 0.12f);
        dynamicRangeFit = SaturateFloat(dynamicRangeFit + 0.06f);
        contrastShape = SaturateFloat(contrastShape - 0.04f);
    } else if (candidate.id == "brightHighlightRolloff") {
        highlightIntegrity = SaturateFloat(highlightIntegrity + 0.08f);
        contrastShape = SaturateFloat(contrastShape + 0.06f);
        brightnessHierarchy = SaturateFloat(brightnessHierarchy + 0.14f);
        strategyHighlightFit = SaturateFloat(strategyHighlightFit + 0.08f);
        strategyNaturalContrastFit = SaturateFloat(strategyNaturalContrastFit + 0.04f);
    } else if (candidate.id == "luminousHighlightAnchor") {
        // This branch is about perceived light, not clipped-data recovery:
        // keep the pre-finish image stable and test whether downstream
        // shoulder/contrast can stop protected highlights from going gray.
        highlightIntegrity = SaturateFloat(highlightIntegrity + 0.06f);
        contrastShape = SaturateFloat(contrastShape + 0.10f);
        brightnessHierarchy = SaturateFloat(brightnessHierarchy + 0.18f);
        luminousHighlightAnchor = SaturateFloat(luminousHighlightAnchor + 0.22f);
        naturalContrastGuard = SaturateFloat(naturalContrastGuard + 0.06f);
        strategyHighlightFit = SaturateFloat(strategyHighlightFit + 0.08f);
        strategyNaturalContrastFit = SaturateFloat(strategyNaturalContrastFit + 0.08f);
    } else if (candidate.id == "specularHighlightTolerance") {
        // Tiny glints may stay hot, but this should score as a visible-light
        // hierarchy choice, not as broad highlight recovery.
        highlightIntegrity = SaturateFloat(highlightIntegrity - 0.03f);
        contrastShape = SaturateFloat(contrastShape + 0.08f);
        brightnessHierarchy = SaturateFloat(brightnessHierarchy + 0.12f);
        specularTolerance = SaturateFloat(specularTolerance + 0.18f);
    } else if (candidate.id == "tonePunchierShape") {
        contrastShape = SaturateFloat(contrastShape + 0.14f);
        dynamicRangeFit = SaturateFloat(dynamicRangeFit - 0.04f);
    } else if (candidate.id == "toneFlatterEditing") {
        midtonePlacement = SaturateFloat(midtonePlacement + 0.05f);
        dynamicRangeFit = SaturateFloat(dynamicRangeFit + 0.12f);
        contrastShape = SaturateFloat(contrastShape - 0.06f);
    } else if (candidate.id == "toneDarkerToe") {
        midtonePlacement = SaturateFloat(midtonePlacement - 0.04f + darkness * 0.08f);
        shadowCleanliness = SaturateFloat(shadowCleanliness + 0.06f);
        contrastShape = SaturateFloat(contrastShape + 0.06f);
    } else if (candidate.id == "localRangeGuard") {
        // Local EV conflict means the solver should test controlled local
        // redistribution, not simply push more global range everywhere.
        dynamicRangeFit = SaturateFloat(dynamicRangeFit + 0.10f + regionEvidence.localEvConflict * 0.08f);
        strategyVisibleRangeFit = SaturateFloat(strategyVisibleRangeFit + 0.08f);
        localArtifactSafety = SaturateFloat(
            1.0f -
            std::max(
                highlightDamageRisk,
                std::max(
                    flatteningRisk,
                    std::max(regionEvidence.localHaloRisk * 0.80f, regionEvidence.localEvConflict * 0.26f))));
        highlightIntegrity = SaturateFloat(highlightIntegrity + 0.05f);
        shadowCleanliness = SaturateFloat(shadowCleanliness + 0.04f);
    } else if (candidate.id == "haloSafeLocalRange") {
        dynamicRangeFit = SaturateFloat(dynamicRangeFit + 0.04f);
        highlightIntegrity = SaturateFloat(highlightIntegrity + 0.04f);
        shadowCleanliness = SaturateFloat(shadowCleanliness + 0.03f);
        localArtifactSafety = SaturateFloat(localArtifactSafety + 0.12f);
        localHaloSafety = SaturateFloat(localHaloSafety + 0.22f);
        contrastShape = SaturateFloat(contrastShape - 0.02f);
        strategyVisibleRangeFit = SaturateFloat(strategyVisibleRangeFit + 0.06f);
    } else if (candidate.id == "shadowReadabilityLift") {
        midtonePlacement = SaturateFloat(midtonePlacement + 0.08f);
        shadowReadabilityLift = SaturateFloat(shadowReadabilityLift + 0.18f);
        dynamicRangeFit = SaturateFloat(dynamicRangeFit + 0.06f);
        shadowCleanliness = SaturateFloat(shadowCleanliness - 0.03f);
        localArtifactSafety = SaturateFloat(localArtifactSafety + 0.03f);
        strategyShadowFit = SaturateFloat(strategyShadowFit + 0.16f);
    } else if (candidate.id == "subjectReadableMids") {
        // Guide 05 subject intent is a bias, not a hard mask: test a local
        // readability branch while keeping noise/halo and highlight safeguards visible.
        midtonePlacement = SaturateFloat(midtonePlacement + 0.10f);
        shadowReadabilityLift = SaturateFloat(shadowReadabilityLift + 0.14f);
        dynamicRangeFit = SaturateFloat(dynamicRangeFit + 0.05f);
        localArtifactSafety = SaturateFloat(localArtifactSafety + 0.04f);
        strategyShadowFit = SaturateFloat(strategyShadowFit + 0.10f);
    } else if (candidate.id == "sceneMoodPreservation") {
        // This is the counter-candidate for silhouettes and low-key scenes:
        // preserve mood/scene hierarchy instead of assuming every likely subject
        // must be lifted toward neutral mids.
        midtonePlacement = SaturateFloat(midtonePlacement - 0.04f);
        shadowCleanliness = SaturateFloat(shadowCleanliness + 0.08f);
        contrastShape = SaturateFloat(contrastShape + 0.05f);
        localArtifactSafety = SaturateFloat(localArtifactSafety + 0.06f);
        strategyNaturalContrastFit = SaturateFloat(strategyNaturalContrastFit + 0.06f);
    } else if (candidate.id == "shadowNoiseFloor") {
        midtonePlacement = SaturateFloat(midtonePlacement - 0.05f);
        shadowCleanliness = SaturateFloat(shadowCleanliness + 0.14f);
        noiseTextureQuality = SaturateFloat(noiseTextureQuality + 0.08f);
        contrastShape = SaturateFloat(contrastShape + 0.05f);
        dynamicRangeFit = SaturateFloat(dynamicRangeFit - 0.04f);
        localArtifactSafety = SaturateFloat(localArtifactSafety + 0.08f);
        strategyNaturalContrastFit = SaturateFloat(strategyNaturalContrastFit + 0.04f);
    } else if (candidate.id == "wbDaylightCorrection") {
        colorPlausibility = SaturateFloat(colorPlausibility + 0.12f);
        moodColorPreservation = SaturateFloat(moodColorPreservation - 0.03f);
    } else if (candidate.id == "wbNeutralCorrection") {
        colorPlausibility = SaturateFloat(colorPlausibility + 0.14f);
        moodColorPreservation = SaturateFloat(moodColorPreservation - 0.08f);
    } else if (candidate.id == "wbCameraMood") {
        colorPlausibility = SaturateFloat(colorPlausibility + 0.03f);
        moodColorPreservation = SaturateFloat(moodColorPreservation + 0.14f);
    }

    const float modeIntentFit = DevelopAutoCandidateModeIntentFit(candidate.id, intent);
    const float dataRiskPenalty = SaturateFloat(
        highlightDamageRisk * 0.42f +
        shadowNoiseRisk * 0.32f +
        flatteningRisk * 0.22f +
        regionEvidence.localHaloRisk * 0.04f +
        regionEvidence.localEvConflict * 0.04f);

    return {
        { "version", "ParameterScoreComponentsV1" },
        { "scoreSource", "parameterCandidateHeuristic" },
        { "candidateId", candidate.id },
        { "autoIntent", EditorNodeGraph::DevelopAutoIntentStableString(intent) },
        { "dynamicRangeStrategyMap", {
            { "version", kDevelopDynamicRangeStrategyMapVersion },
            { "highlightShadowAxis", dynamicRangeStrategy.strategyMapHighlightShadowAxis },
            { "contrastRangeAxis", dynamicRangeStrategy.strategyMapContrastRangeAxis },
            { "highlightPriority", dynamicRangeStrategy.strategyMapHighlightPriority },
            { "shadowVisibility", dynamicRangeStrategy.strategyMapShadowVisibility },
            { "naturalContrast", dynamicRangeStrategy.strategyMapNaturalContrast },
            { "visibleRange", dynamicRangeStrategy.strategyMapVisibleRange }
        } },
        { "subjectSceneIntent", {
            { "version", kDevelopSubjectSceneIntentVersion },
            { "id", subjectSceneIntent.id },
            { "label", subjectSceneIntent.label },
            { "solveNotesVersion", kDevelopSubjectImportanceSolveNotesVersion },
            { "solveNotes", subjectSceneIntent.solveNotes },
            { "automaticConfidence", subjectSceneIntent.automaticConfidence },
            { "subjectSceneAxis", subjectSceneIntent.subjectSceneAxis },
            { "moodReadabilityAxis", subjectSceneIntent.moodReadabilityAxis },
            { "userGuidanceActive", subjectSceneIntent.userGuidanceActive },
            { "userGuidanceStatus", subjectSceneIntent.userGuidanceStatus },
            { "userSubjectSceneBias", subjectSceneIntent.userSubjectSceneBias },
            { "userMoodReadabilityBias", subjectSceneIntent.userMoodReadabilityBias },
            { "userGuidanceStrength", subjectSceneIntent.userGuidanceStrength },
            { "importanceRegionCount", subjectSceneIntent.importanceRegionCount },
            { "importanceStrokeCount", subjectSceneIntent.importanceStrokeCount },
            { "importanceStrength", subjectSceneIntent.importanceStrength },
            { "importanceImportant", subjectSceneIntent.importanceImportant },
            { "importanceReveal", subjectSceneIntent.importanceReveal },
            { "importanceProtect", subjectSceneIntent.importanceProtect },
            { "importancePreserveMood", subjectSceneIntent.importancePreserveMood },
            { "importanceIgnore", subjectSceneIntent.importanceIgnore },
            { "importanceMap", subjectSceneIntent.importanceMap },
            { "refinedImportanceMap", subjectSceneIntent.refinedImportanceMap },
            { "importanceMapCoverage", subjectSceneIntent.importanceMapCoverage },
            { "importanceMapPositiveCoverage", subjectSceneIntent.importanceMapPositiveCoverage },
            { "importanceMapLowPriorityCoverage", subjectSceneIntent.importanceMapLowPriorityCoverage },
            { "importanceMapPeak", subjectSceneIntent.importanceMapPeak },
            { "importanceMapConfidence", subjectSceneIntent.importanceMapConfidence },
            { "importanceMapCenterBias", subjectSceneIntent.importanceMapCenterBias },
            { "importanceMapEdgeBias", subjectSceneIntent.importanceMapEdgeBias },
            { "refinedMapCoverage", subjectSceneIntent.refinedMapCoverage },
            { "refinedMapLowPriorityCoverage", subjectSceneIntent.refinedMapLowPriorityCoverage },
            { "refinedMapReadabilityCoverage", subjectSceneIntent.refinedMapReadabilityCoverage },
            { "refinedMapProtectionCoverage", subjectSceneIntent.refinedMapProtectionCoverage },
            { "refinedMapMoodCoverage", subjectSceneIntent.refinedMapMoodCoverage },
            { "refinedMapPeak", subjectSceneIntent.refinedMapPeak },
            { "refinedMapConfidence", subjectSceneIntent.refinedMapConfidence },
            { "refinedMapBoundaryHint", subjectSceneIntent.refinedMapBoundaryHint }
        } },
        { "renderedContinuationBias", {
            { "version", kDevelopContinuationCandidateBiasVersion },
            { "active", candidate.continuationBiasActive },
            { "bonus", candidate.continuationBiasBonus },
            { "reason", candidate.continuationBiasReason },
            { "stageFocus", candidate.continuationBiasStage },
            { "refineIntent", candidate.continuationBiasRefineIntent }
        } },
        { "renderedContinuationExpansion", {
            { "version", kDevelopContinuationCandidateExpansionVersion },
            { "active", candidate.continuationExpansionCandidate },
            { "reason", candidate.continuationExpansionReason },
            { "stageFocus", candidate.continuationExpansionStage },
            { "refineIntent", candidate.continuationExpansionRefineIntent }
        } },
        { "finalScore", candidate.score },
        { "statsValid", stats.valid },
        { "signals", {
            { "darkness", darkness },
            { "shadowRescueNeed", shadowRescueNeed },
            { "hdrNeed", hdrNeed },
            { "flatSceneNeed", flatSceneNeed },
            { "underBrightBroadHighlightEv", underBrightBroadHighlightEv },
            { "broadHighlightSignal", broadHighlightSignal },
            { "shadowReadabilitySignal", shadowReadabilitySignal },
            { "naturalContrastSignal", naturalContrastSignal },
            { "highlightBrightnessSignal", highlightBrightnessSignal },
            { "localHaloSafetySignal", localHaloSafetySignal },
            { "strategyMapHighlightBias", mapHighlightBias },
            { "strategyMapShadowBias", mapShadowBias },
            { "strategyMapNaturalContrastBias", mapContrastBias },
            { "strategyMapVisibleRangeBias", mapRangeBias },
            { "subjectPriorityBias", subjectPriorityBias },
            { "subjectReadabilityBias", subjectReadabilityBias },
            { "subjectProtectionBias", subjectProtectionBias },
            { "subjectMoodBias", subjectMoodBias },
            { "subjectCenterPrior", subjectSceneIntent.centerPrior },
            { "subjectAutomaticConfidence", subjectSceneIntent.automaticConfidence },
            { "subjectUserGuidanceStrength", subjectSceneIntent.userGuidanceStrength },
            { "subjectUserSubjectSceneBias", subjectSceneIntent.userSubjectSceneBias },
            { "subjectUserMoodReadabilityBias", subjectSceneIntent.userMoodReadabilityBias },
            { "subjectImportanceRegionCount", subjectSceneIntent.importanceRegionCount },
            { "subjectImportanceStrokeCount", subjectSceneIntent.importanceStrokeCount },
            { "subjectImportanceStrength", subjectSceneIntent.importanceStrength },
            { "subjectImportanceImportant", subjectSceneIntent.importanceImportant },
            { "subjectImportanceReveal", subjectSceneIntent.importanceReveal },
            { "subjectImportanceProtect", subjectSceneIntent.importanceProtect },
            { "subjectImportancePreserveMood", subjectSceneIntent.importancePreserveMood },
            { "subjectImportanceIgnore", subjectSceneIntent.importanceIgnore },
            { "subjectImportanceMapCoverage", subjectSceneIntent.importanceMapCoverage },
            { "subjectImportanceMapPositiveCoverage", subjectSceneIntent.importanceMapPositiveCoverage },
            { "subjectImportanceMapLowPriorityCoverage", subjectSceneIntent.importanceMapLowPriorityCoverage },
            { "subjectImportanceMapRevealCoverage", subjectSceneIntent.importanceMapRevealCoverage },
            { "subjectImportanceMapProtectCoverage", subjectSceneIntent.importanceMapProtectCoverage },
            { "subjectImportanceMapMoodCoverage", subjectSceneIntent.importanceMapMoodCoverage },
            { "subjectImportanceMapPeak", subjectSceneIntent.importanceMapPeak },
            { "subjectImportanceMapConfidence", subjectSceneIntent.importanceMapConfidence },
            { "subjectImportanceMapCenterBias", subjectSceneIntent.importanceMapCenterBias },
            { "subjectImportanceMapEdgeBias", subjectSceneIntent.importanceMapEdgeBias },
            { "subjectRefinedMapCoverage", subjectSceneIntent.refinedMapCoverage },
            { "subjectRefinedMapLowPriorityCoverage", subjectSceneIntent.refinedMapLowPriorityCoverage },
            { "subjectRefinedMapReadabilityCoverage", subjectSceneIntent.refinedMapReadabilityCoverage },
            { "subjectRefinedMapProtectionCoverage", subjectSceneIntent.refinedMapProtectionCoverage },
            { "subjectRefinedMapMoodCoverage", subjectSceneIntent.refinedMapMoodCoverage },
            { "subjectRefinedMapPeak", subjectSceneIntent.refinedMapPeak },
            { "subjectRefinedMapConfidence", subjectSceneIntent.refinedMapConfidence },
            { "subjectRefinedMapBoundaryHint", subjectSceneIntent.refinedMapBoundaryHint },
            { "subjectReadabilityPressure", subjectSceneIntent.readabilityPressure },
            { "subjectProtectionPressure", subjectSceneIntent.protectionPressure },
            { "subjectMoodPreservationPressure", subjectSceneIntent.moodPreservationPressure },
            { "highlightPressure", stats.highlightPressure },
            { "clippingRatio", stats.clippingRatio },
            { "noiseRisk", stats.noiseRisk },
            { "textureConfidence", stats.textureConfidence },
            { "regionEvidenceValid", regionEvidence.valid },
            { "regionEvidenceSource", regionEvidence.source },
            { "localHighlightHotspotRisk", regionEvidence.localHighlightHotspotRisk },
            { "localShadowHotspotRisk", regionEvidence.localShadowHotspotRisk },
            { "shadowNoiseLiftRisk", regionEvidence.shadowNoiseLiftRisk },
            { "localRangeConflict", regionEvidence.localRangeConflict },
            { "localEvSpreadStops", regionEvidence.localEvSpreadStops },
            { "localEvConflict", regionEvidence.localEvConflict },
            { "localHaloRisk", regionEvidence.localHaloRisk },
            { "flatGrayRisk", regionEvidence.flatGrayRisk },
            { "highlightGrayRisk", regionEvidence.highlightGrayRisk },
            { "localExposureHighlightCrowding", regionEvidence.localExposureHighlightCrowding },
            { "localExposureShadowCrowding", regionEvidence.localExposureShadowCrowding },
            { "localExposureHaloStress", regionEvidence.localExposureHaloStress },
            { "localExposureFlatnessRisk", regionEvidence.localExposureFlatnessRisk },
            { "localExposureDamageRisk", regionEvidence.localExposureDamageRisk },
            { "highlightBandFraction", regionEvidence.highlightBandFraction },
            { "highlightMeanLuma", regionEvidence.highlightMeanLuma },
            { "highlightLowSaturationFraction", regionEvidence.highlightLowSaturationFraction },
            { "highlightTileCoverage", regionEvidence.highlightTileCoverage },
            { "highlightStructureScore", regionEvidence.highlightStructureScore },
            { "meaningfulHighlightPressure", regionEvidence.meaningfulHighlightPressure },
            { "regionalBrightnessHierarchyRisk", regionEvidence.brightnessHierarchyRisk },
            { "smallSpecularSignal", smallSpecularSignal }
        } },
        { "guidanceDelta", {
            { "brightnessIntentDelta", exposureDelta },
            { "dynamicRangeDelta", rangeDelta },
            { "shadowLiftDelta", shadowDelta },
            { "highlightGuardDelta", highlightGuardDelta },
            { "highlightCharacterDelta", highlightCharacterDelta },
            { "contrastBiasDelta", contrastDelta }
        } },
        { "dimensions", {
            { "midtonePlacement", midtonePlacement },
            { "highlightIntegrity", highlightIntegrity },
            { "shadowCleanliness", shadowCleanliness },
            { "dynamicRangeFit", dynamicRangeFit },
            { "contrastShape", contrastShape },
            { "brightnessHierarchy", brightnessHierarchy },
            { "naturalContrastGuard", naturalContrastGuard },
            { "luminousHighlightAnchor", luminousHighlightAnchor },
            { "specularTolerance", specularTolerance },
            { "broadHighlightControl", broadHighlightControl },
            { "meaningfulHighlightControl", meaningfulHighlightControl },
            { "shadowReadabilityLift", shadowReadabilityLift },
            { "strategyHighlightFit", strategyHighlightFit },
            { "strategyShadowFit", strategyShadowFit },
            { "strategyVisibleRangeFit", strategyVisibleRangeFit },
            { "strategyNaturalContrastFit", strategyNaturalContrastFit },
            { "subjectPriorityFit", SaturateFloat(
                0.42f + subjectSceneIntent.subjectPriority * 0.28f +
                subjectPriorityBias * 0.16f +
                subjectSceneIntent.importanceMapConfidence * 0.04f +
                subjectRefinedConfidenceBias * 0.05f +
                subjectSceneIntent.importanceMapCenterBias *
                    subjectSceneIntent.importanceMapPositiveCoverage * 0.04f +
                (candidate.id == "shadowReadabilityLift" ||
                 candidate.id == "subjectReadableMids" ||
                 candidate.id == "localRangeGuard" ||
                 candidate.id == "renderedLocalShadowOpening" ||
                 candidate.id == "renderedLocalBrightenMids" ? 0.08f : 0.0f)) },
            { "subjectReadabilityFit", SaturateFloat(
                0.42f + subjectSceneIntent.improveReadability * 0.28f +
                subjectReadabilityBias * 0.18f +
                subjectSceneIntent.importanceMapRevealCoverage * 0.05f +
                subjectRefinedReadabilityBias * 0.06f +
                (candidate.id == "shadowReadabilityLift" ||
                 candidate.id == "subjectReadableMids" ||
                 candidate.id == "brighterMids" ||
                 candidate.id == "renderedLocalShadowOpening" ||
                 candidate.id == "renderedLocalBrightenMids" ? 0.10f : 0.0f) -
                regionEvidence.shadowNoiseLiftRisk * 0.08f) },
            { "subjectProtectionFit", SaturateFloat(
                0.42f + subjectSceneIntent.protectionPressure * 0.34f +
                subjectProtectionBias * 0.14f +
                subjectSceneIntent.importanceMapProtectCoverage * 0.05f +
                subjectRefinedProtectionBias * 0.05f +
                (candidate.id == "protectHighlights" ||
                 candidate.id == "broadHighlightGuard" ||
                 candidate.id == "renderedLocalHighlightRestraint" ? 0.10f : 0.0f) -
                regionEvidence.localExposureHaloStress * 0.05f) },
            { "subjectMoodFit", SaturateFloat(
                0.42f + subjectSceneIntent.preserveMood * 0.26f +
                subjectMoodBias * 0.18f +
                subjectSceneIntent.importanceMapMoodCoverage * 0.05f +
                subjectSceneIntent.importanceMapLowPriorityCoverage * 0.03f +
                subjectRefinedMoodBias * 0.05f +
                (candidate.id == "preserveMood" ||
                 candidate.id == "sceneMoodPreservation" ||
                 candidate.id == "toneDarkerToe" ||
                 candidate.id == "shadowNoiseFloor" ? 0.10f : 0.0f) -
                subjectReadabilityBias * 0.06f) },
            { "noiseTextureQuality", noiseTextureQuality },
            { "colorPlausibility", colorPlausibility },
            { "moodColorPreservation", moodColorPreservation },
            { "localArtifactSafety", localArtifactSafety },
            { "localHaloSafety", localHaloSafety },
            { "localExposureDamageSafety", SaturateFloat(1.0f - regionEvidence.localExposureDamageRisk) },
            { "modeIntentFit", modeIntentFit },
            { "renderedContinuationFit", candidate.continuationBiasActive
                ? SaturateFloat(0.50f + candidate.continuationBiasBonus / 0.24f)
                : 0.50f },
            { "renderedContinuationCoverage", candidate.continuationExpansionCandidate ? 1.0f : 0.50f },
            { "candidateUniqueness", 1.0f }
        } },
        { "risks", {
            { "highlightDamageRisk", highlightDamageRisk },
            { "shadowNoiseRisk", shadowNoiseRisk },
            { "flatteningRisk", flatteningRisk },
            { "localHaloRisk", regionEvidence.localHaloRisk },
            { "localRangeConflict", regionEvidence.localRangeConflict },
            { "localEvConflict", regionEvidence.localEvConflict },
            { "localExposureDamageRisk", regionEvidence.localExposureDamageRisk },
            { "localExposureHaloStress", regionEvidence.localExposureHaloStress },
            { "subjectOverLiftRisk", SaturateFloat(
                subjectMoodBias * 0.44f +
                regionEvidence.shadowNoiseLiftRisk * 0.26f +
                positiveShadowDelta * 0.16f +
                positiveExposureDelta * 0.12f) },
            { "subjectProtectionTradeoffRisk", SaturateFloat(
                subjectProtectionBias * 0.42f +
                positiveExposureDelta * 0.22f +
                stats.clippingRatio * 1.20f -
                std::max(0.0f, highlightGuardDelta) * 0.12f) },
            { "dataRiskPenalty", dataRiskPenalty }
        } }
    };
}

nlohmann::json BuildFallbackDevelopAutoCandidateScoreComponents(
    const DevelopAutoCandidateSolve& candidate,
    const EditorNodeGraph::DevelopAutoGuidance& base) {
    const float distance = DevelopAutoCandidateDistance(candidate.guidance, base);
    return {
        { "version", "ParameterScoreComponentsV1" },
        { "scoreSource", "authoredStateFallback" },
        { "candidateId", candidate.id },
        { "autoIntent", EditorNodeGraph::DevelopAutoIntentStableString(candidate.guidance.intent) },
        { "renderedContinuationBias", {
            { "version", kDevelopContinuationCandidateBiasVersion },
            { "active", candidate.continuationBiasActive },
            { "bonus", candidate.continuationBiasBonus },
            { "reason", candidate.continuationBiasReason },
            { "stageFocus", candidate.continuationBiasStage },
            { "refineIntent", candidate.continuationBiasRefineIntent }
        } },
        { "renderedContinuationExpansion", {
            { "version", kDevelopContinuationCandidateExpansionVersion },
            { "active", candidate.continuationExpansionCandidate },
            { "reason", candidate.continuationExpansionReason },
            { "stageFocus", candidate.continuationExpansionStage },
            { "refineIntent", candidate.continuationExpansionRefineIntent }
        } },
        { "finalScore", candidate.score },
        { "statsValid", false },
        { "guidanceDistanceFromBase", distance },
        { "dimensions", {
            { "midtonePlacement", 0.50f },
            { "highlightIntegrity", 0.50f },
            { "shadowCleanliness", 0.50f },
            { "dynamicRangeFit", 0.50f },
            { "contrastShape", 0.50f },
            { "brightnessHierarchy", candidate.id == "brightHighlightRolloff" || candidate.id == "luminousHighlightAnchor" || candidate.id == "specularHighlightTolerance" || candidate.id == "naturalContrastGuard" ? 0.64f : 0.50f },
            { "naturalContrastGuard", candidate.id == "naturalContrastGuard" ? 0.70f : 0.50f },
            { "luminousHighlightAnchor", candidate.id == "luminousHighlightAnchor" ? 0.72f : 0.50f },
            { "specularTolerance", candidate.id == "specularHighlightTolerance" ? 0.70f : 0.50f },
            { "broadHighlightControl", candidate.id == "broadHighlightGuard" ? 0.70f : 0.50f },
            { "shadowReadabilityLift", candidate.id == "shadowReadabilityLift" ? 0.70f : 0.50f },
            { "noiseTextureQuality", 0.50f },
            { "colorPlausibility", candidate.whiteBalanceProbe ? 0.58f : 0.50f },
            { "moodColorPreservation", candidate.id == "wbCameraMood" ? 0.64f : 0.50f },
            { "localArtifactSafety", 0.50f },
            { "localHaloSafety", candidate.id == "haloSafeLocalRange" ? 0.70f : 0.50f },
            { "modeIntentFit", DevelopAutoCandidateModeIntentFit(candidate.id, candidate.guidance.intent) },
            { "renderedContinuationFit", candidate.continuationBiasActive
                ? SaturateFloat(0.50f + candidate.continuationBiasBonus / 0.24f)
                : 0.50f },
            { "renderedContinuationCoverage", candidate.continuationExpansionCandidate ? 1.0f : 0.50f },
            { "candidateUniqueness", 1.0f }
        } },
        { "risks", {
            { "highlightDamageRisk", 0.0f },
            { "shadowNoiseRisk", 0.0f },
            { "flatteningRisk", 0.0f },
            { "dataRiskPenalty", 0.0f }
        } }
    };
}

} // namespace Stack::Editor::DevelopCandidateScoring
