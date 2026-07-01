#include "Editor/Internal/EditorModuleDevelopDynamicRangeStrategy.h"

#include <algorithm>

namespace {

float SaturateFloat(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float DevelopRegionRiskAbove(float value, float safeValue, float fullRiskValue) {
    if (fullRiskValue <= safeValue) {
        return value > safeValue ? 1.0f : 0.0f;
    }
    return SaturateFloat((value - safeValue) / (fullRiskValue - safeValue));
}

float DevelopRegionRiskBelow(float value, float safeValue, float fullRiskValue) {
    if (safeValue <= fullRiskValue) {
        return value < safeValue ? 1.0f : 0.0f;
    }
    return SaturateFloat((safeValue - value) / (safeValue - fullRiskValue));
}

} // namespace

namespace Stack::Editor::DevelopDynamicRange {

DevelopToneAutoStats ReadDevelopToneAutoStats(const nlohmann::json& toneJson) {
    DevelopToneAutoStats stats;
    if (!toneJson.is_object()) {
        return stats;
    }

    stats.valid = toneJson.value("autoSceneStatsValid", false);
    stats.shadowPercentile = toneJson.value("autoSceneShadowPercentile", stats.shadowPercentile);
    stats.midtonePercentile = toneJson.value("autoSceneMidtonePercentile", stats.midtonePercentile);
    stats.highlightPercentile = toneJson.value("autoSceneHighlightPercentile", stats.highlightPercentile);
    stats.clippingRatio = toneJson.value("autoSceneClippingRatio", stats.clippingRatio);
    stats.noiseRisk = toneJson.value("autoSceneNoiseRisk", stats.noiseRisk);
    stats.highlightPressure = toneJson.value("autoSceneHighlightPressure", stats.highlightPressure);
    stats.textureConfidence = toneJson.value("autoSceneTextureConfidence", stats.textureConfidence);
    stats.hdrSpreadEv = toneJson.value("autoSceneHdrSpreadEv", stats.hdrSpreadEv);
    stats.recommendedBaseEv = toneJson.value("autoRecommendedBaseEv", stats.recommendedBaseEv);
    stats.recommendedLocalStrength = toneJson.value("autoRecommendedLocalStrength", stats.recommendedLocalStrength);
    stats.recommendedShadowOpening = toneJson.value("autoRecommendedShadowOpening", stats.recommendedShadowOpening);
    stats.recommendedHighlightCompression = toneJson.value("autoRecommendedHighlightCompression", stats.recommendedHighlightCompression);
    stats.sceneProfile = toneJson.value("autoSceneProfile", 0);
    return stats;
}

void ResolveDevelopDynamicRangeStrategyMap(
    DevelopDynamicRangeStrategy& strategy,
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    const DevelopToneAutoStats& stats,
    const DevelopDynamicRangeRegionEvidence& regionEvidence,
    float shadowRescueNeed,
    float hdrNeed,
    float tinySpecularAllowance) {
    if (!stats.valid) {
        return;
    }

    const bool naturalIntent = intent == EditorNodeGraph::DevelopAutoIntent::NaturalFinished;
    const bool rangeIntent = intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;
    const bool flatIntent = intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
    const bool darkIntent = intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural;
    const bool punchyIntent = intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;
    const bool brightIntent = intent == EditorNodeGraph::DevelopAutoIntent::BrightNatural;
    const float clippingPressure = SaturateFloat(stats.clippingRatio / 0.018f);

    const float highlightSidePressure = SaturateFloat(
        strategy.highlightImportance * 0.46f +
        strategy.broadHighlightGuardNeed * 0.18f +
        regionEvidence.meaningfulHighlightPressure * 0.14f +
        regionEvidence.broadHighlightPressure * 0.10f +
        regionEvidence.highlightGrayRisk * 0.08f +
        clippingPressure * 0.16f +
        (darkIntent ? 0.05f : 0.0f) +
        (rangeIntent ? 0.05f : 0.0f) -
        tinySpecularAllowance * 0.10f -
        strategy.specularHighlightToleranceNeed * 0.04f);
    const float shadowSidePressure = SaturateFloat(
        strategy.shadowReadability * 0.44f +
        strategy.shadowReadabilityLiftNeed * 0.22f +
        shadowRescueNeed * 0.16f +
        regionEvidence.localShadowHotspotRisk * 0.10f +
        std::max(0.0f, guidance.shadowLift) * 0.08f +
        (brightIntent ? 0.08f : 0.0f) +
        (flatIntent ? 0.06f : 0.0f) -
        strategy.shadowNoiseFloorNeed * 0.14f -
        strategy.noiseConstraint * 0.08f -
        (darkIntent ? 0.04f : 0.0f));

    const float visibleRangePressure = SaturateFloat(
        strategy.rangeCompression * 0.40f +
        hdrNeed * 0.22f +
        regionEvidence.localRangeConflict * 0.12f +
        regionEvidence.localEvConflict * 0.12f +
        strategy.broadHighlightGuardNeed * 0.07f +
        std::max(0.0f, guidance.dynamicRange - 1.0f) * 0.10f +
        (rangeIntent ? 0.16f : 0.0f) +
        (flatIntent ? 0.08f : 0.0f) -
        strategy.naturalContrastGuardNeed * 0.04f -
        (punchyIntent ? 0.08f : 0.0f));
    const float naturalContrastPressure = SaturateFloat(
        strategy.naturalContrastGuardNeed * 0.36f +
        strategy.brightnessHierarchyRisk * 0.20f +
        regionEvidence.highlightGrayRisk * 0.10f +
        regionEvidence.flatGrayRisk * 0.08f +
        std::max(0.0f, guidance.contrastBias) * 0.08f +
        (punchyIntent ? 0.16f : 0.0f) +
        (naturalIntent ? 0.07f : 0.0f) +
        (darkIntent ? 0.07f : 0.0f) -
        (rangeIntent ? 0.12f : 0.0f) -
        (flatIntent ? 0.08f : 0.0f));

    strategy.strategyMapHighlightShadowAxis =
        std::clamp(shadowSidePressure - highlightSidePressure, -1.0f, 1.0f);
    strategy.strategyMapContrastRangeAxis =
        std::clamp(visibleRangePressure - naturalContrastPressure, -1.0f, 1.0f);
    strategy.strategyMapHighlightPriority =
        SaturateFloat(0.5f - strategy.strategyMapHighlightShadowAxis * 0.5f);
    strategy.strategyMapShadowVisibility =
        SaturateFloat(0.5f + strategy.strategyMapHighlightShadowAxis * 0.5f);
    strategy.strategyMapNaturalContrast =
        SaturateFloat(0.5f - strategy.strategyMapContrastRangeAxis * 0.5f);
    strategy.strategyMapVisibleRange =
        SaturateFloat(0.5f + strategy.strategyMapContrastRangeAxis * 0.5f);
    strategy.strategyMapReason =
        "Internal solver map: horizontal balances highlight priority against shadow visibility; vertical balances natural contrast against maximum visible range. Future graph controls can expose these coordinates without turning Auto into presets.";
}

void ResolveDevelopLocalExposureStrategy(
    DevelopDynamicRangeStrategy& strategy,
    EditorNodeGraph::DevelopAutoIntent intent,
    const DevelopToneAutoStats& stats,
    const DevelopDynamicRangeRegionEvidence& regionEvidence,
    float shadowRescueNeed,
    float hdrNeed) {
    if (!stats.valid) {
        strategy.localExposureStrategyId = "pendingLocalEvidence";
        strategy.localExposureStrategyLabel = "Pending Local Evidence";
        strategy.localExposureStrategyReason =
            "Develop needs rendered statistics before it can choose a local exposure strategy.";
        return;
    }

    const bool rangeIntent = intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;
    const bool flatIntent = intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
    const bool darkIntent = intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural;
    const bool punchyIntent = intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;
    const bool brightIntent = intent == EditorNodeGraph::DevelopAutoIntent::BrightNatural;

    strategy.localExposureRangeRedistribution = SaturateFloat(
        strategy.rangeCompression * 0.28f +
        strategy.strategyMapVisibleRange * 0.22f +
        regionEvidence.localRangeConflict * 0.16f +
        regionEvidence.localEvConflict * 0.16f +
        regionEvidence.localExposureHighlightCrowding * 0.08f +
        regionEvidence.localExposureShadowCrowding * 0.08f +
        hdrNeed * 0.16f +
        (rangeIntent ? 0.12f : 0.0f) +
        (flatIntent ? 0.08f : 0.0f) -
        regionEvidence.localExposureHaloStress * 0.08f -
        regionEvidence.localExposureDamageRisk * 0.06f -
        strategy.localHaloGuardNeed * 0.08f -
        (punchyIntent ? 0.06f : 0.0f));
    strategy.localExposureHighlightCompression = SaturateFloat(
        strategy.broadHighlightGuardNeed * 0.32f +
        strategy.highlightImportance * 0.18f +
        strategy.strategyMapHighlightPriority * 0.18f +
        regionEvidence.meaningfulHighlightPressure * 0.12f +
        regionEvidence.localHighlightHotspotRisk * 0.10f +
        regionEvidence.localExposureHighlightCrowding * 0.16f +
        (rangeIntent ? 0.06f : 0.0f) -
        strategy.specularHighlightToleranceNeed * 0.10f);
    strategy.localExposureShadowOpening = SaturateFloat(
        strategy.shadowReadabilityLiftNeed * 0.32f +
        strategy.shadowReadability * 0.18f +
        strategy.strategyMapShadowVisibility * 0.18f +
        shadowRescueNeed * 0.16f +
        regionEvidence.localShadowHotspotRisk * 0.10f +
        regionEvidence.localExposureShadowCrowding * 0.14f +
        (brightIntent ? 0.08f : 0.0f) +
        (flatIntent ? 0.05f : 0.0f) -
        regionEvidence.localExposureDamageRisk * 0.08f -
        strategy.shadowNoiseFloorNeed * 0.18f -
        strategy.noiseConstraint * 0.10f -
        (darkIntent ? 0.05f : 0.0f));
    strategy.localExposureNoiseGuard = SaturateFloat(
        strategy.noiseConstraint * 0.34f +
        strategy.shadowNoiseFloorNeed * 0.28f +
        regionEvidence.shadowNoiseLiftRisk * 0.18f +
        regionEvidence.localExposureShadowCrowding * 0.10f +
        regionEvidence.localExposureDamageRisk * 0.06f +
        stats.noiseRisk * 0.14f -
        strategy.shadowReadabilityLiftNeed * 0.08f);
    strategy.localExposureHaloGuard = SaturateFloat(
        strategy.localHaloGuardNeed * 0.36f +
        regionEvidence.localHaloRisk * 0.24f +
        regionEvidence.localExposureHaloStress * 0.18f +
        regionEvidence.localExposureDamageRisk * 0.08f +
        regionEvidence.localRangeConflict * 0.12f +
        regionEvidence.localEvConflict * 0.10f +
        strategy.localExposureRangeRedistribution * 0.10f);
    strategy.localExposureTextureGuard = SaturateFloat(
        stats.textureConfidence * 0.28f +
        strategy.strategyMapNaturalContrast * 0.20f +
        strategy.naturalContrastGuardNeed * 0.16f +
        regionEvidence.localExposureFlatnessRisk * 0.12f +
        strategy.localExposureHaloGuard * 0.12f +
        strategy.localExposureNoiseGuard * 0.10f -
        strategy.localExposureRangeRedistribution * 0.08f);
    strategy.localExposureShadowEvBudget = std::clamp(
        0.18f +
            strategy.localExposureShadowOpening * 0.84f +
            strategy.localExposureRangeRedistribution * 0.42f -
            strategy.localExposureNoiseGuard * 0.38f -
            strategy.localExposureHaloGuard * 0.20f -
            regionEvidence.localExposureDamageRisk * 0.12f,
        0.0f,
        1.35f);
    strategy.localExposureHighlightEvBudget = std::clamp(
        0.12f +
            strategy.localExposureHighlightCompression * 0.76f +
            strategy.localExposureRangeRedistribution * 0.28f -
            strategy.specularHighlightToleranceNeed * 0.16f -
            regionEvidence.localExposureHaloStress * 0.08f,
        0.0f,
        1.25f);
    strategy.localExposureStrengthTarget = std::clamp(
        0.42f +
            strategy.localExposureRangeRedistribution * 0.24f +
            strategy.localExposureHighlightCompression * 0.10f +
            strategy.localExposureShadowOpening * 0.10f -
            strategy.localExposureHaloGuard * 0.08f -
            regionEvidence.localExposureDamageRisk * 0.06f -
            (punchyIntent ? 0.04f : 0.0f),
        0.25f,
        1.0f);

    if (strategy.localExposureHaloGuard > 0.58f) {
        strategy.localExposureStrategyId = "haloGuardedLocalPrep";
        strategy.localExposureStrategyLabel = "Halo-Guarded Local Prep";
        strategy.localExposureStrategyReason =
            "Local exposure has useful range pressure, but halo/edge evidence says Scene Prep should prioritize safer transitions.";
    } else if (strategy.localExposureHighlightCompression > 0.58f &&
        strategy.localExposureShadowOpening > 0.44f) {
        strategy.localExposureStrategyId = "highlightShadowBalance";
        strategy.localExposureStrategyLabel = "Highlight / Shadow Balance";
        strategy.localExposureStrategyReason =
            "Scene Prep should protect broad highlights while selectively opening readable shadows, keeping both moves local.";
    } else if (strategy.localExposureHighlightCompression > 0.56f) {
        strategy.localExposureStrategyId = "highlightLocalCompression";
        strategy.localExposureStrategyLabel = "Highlight Local Compression";
        strategy.localExposureStrategyReason =
            "Broad meaningful highlight evidence asks Scene Prep to compress visible bright regions without a global exposure cut.";
    } else if (strategy.localExposureShadowOpening > 0.56f &&
        strategy.localExposureNoiseGuard < 0.55f) {
        strategy.localExposureStrategyId = "readableShadowLocalLift";
        strategy.localExposureStrategyLabel = "Readable Shadow Local Lift";
        strategy.localExposureStrategyReason =
            "Shadow evidence is clean enough for Scene Prep to test local opening while RAW placement stays stable.";
    } else if (strategy.localExposureNoiseGuard > 0.58f || darkIntent) {
        strategy.localExposureStrategyId = "shadowFloorProtected";
        strategy.localExposureStrategyLabel = "Shadow Floor Protected";
        strategy.localExposureStrategyReason =
            "Noise or dark-scene intent says Scene Prep should avoid turning low-value dark areas into gray mush.";
    } else if (strategy.localExposureRangeRedistribution > 0.58f) {
        strategy.localExposureStrategyId = "rangeRedistribution";
        strategy.localExposureStrategyLabel = "Range Redistribution";
        strategy.localExposureStrategyReason =
            "Scene Prep should redistribute visible range locally while preserving believable lighting hierarchy.";
    }
}

nlohmann::json DevelopDynamicRangeRegionEvidenceToJson(
    const DevelopDynamicRangeRegionEvidence& evidence) {
    return {
        { "version", kDevelopDynamicRangeRegionEvidenceVersion },
        { "valid", evidence.valid },
        { "source", evidence.source },
        { "candidateId", evidence.candidateId },
        { "renderScore", evidence.renderScore },
        { "localHighlightPressure", evidence.localHighlightPressure },
        { "localShadowPressure", evidence.localShadowPressure },
        { "localDamageRiskMean", evidence.localDamageRiskMean },
        { "localDamageRiskPeak", evidence.localDamageRiskPeak },
        { "localLumaSpread", evidence.localLumaSpread },
        { "localEvSpreadStops", evidence.localEvSpreadStops },
        { "localEvConflict", evidence.localEvConflict },
        { "localContrastPeak", evidence.localContrastPeak },
        { "centerShadowFraction", evidence.centerShadowFraction },
        { "centerHighlightFraction", evidence.centerHighlightFraction },
        { "clippedFraction", evidence.clippedFraction },
        { "highlightFraction", evidence.highlightFraction },
        { "shadowFraction", evidence.shadowFraction },
        { "shadowTextureRisk", evidence.shadowTextureRisk },
        { "haloRiskFraction", evidence.haloRiskFraction },
        { "lowSaturationFraction", evidence.lowSaturationFraction },
        { "highlightBandFraction", evidence.highlightBandFraction },
        { "highlightMeanLuma", evidence.highlightMeanLuma },
        { "highlightLowSaturationFraction", evidence.highlightLowSaturationFraction },
        { "highlightGrayRisk", evidence.highlightGrayRisk },
        { "highlightTileCoverage", evidence.highlightTileCoverage },
        { "highlightStructureScore", evidence.highlightStructureScore },
        { "meaningfulHighlightPressure", evidence.meaningfulHighlightPressure },
        { "contrastSpan", evidence.contrastSpan },
        { "peakTile", evidence.peakTile },
        { "broadHighlightPressure", evidence.broadHighlightPressure },
        { "localHighlightHotspotRisk", evidence.localHighlightHotspotRisk },
        { "localShadowHotspotRisk", evidence.localShadowHotspotRisk },
        { "shadowNoiseLiftRisk", evidence.shadowNoiseLiftRisk },
        { "localHaloRisk", evidence.localHaloRisk },
        { "flatGrayRisk", evidence.flatGrayRisk },
        { "localRangeConflict", evidence.localRangeConflict },
        { "brightnessHierarchyRisk", evidence.brightnessHierarchyRisk },
        { "localExposureHighlightCrowding", evidence.localExposureHighlightCrowding },
        { "localExposureShadowCrowding", evidence.localExposureShadowCrowding },
        { "localExposureHaloStress", evidence.localExposureHaloStress },
        { "localExposureFlatnessRisk", evidence.localExposureFlatnessRisk },
        { "localExposureDamageRisk", evidence.localExposureDamageRisk },
        { "subjectCenterPrior", evidence.subjectCenterPrior },
        { "subjectReadabilityPressure", evidence.subjectReadabilityPressure },
        { "subjectProtectionPressure", evidence.subjectProtectionPressure },
        { "subjectMoodPreservationPressure", evidence.subjectMoodPreservationPressure },
        { "subjectImportanceConfidence", evidence.subjectImportanceConfidence },
        { "smallSpecularLikely", evidence.smallSpecularLikely }
    };
}

DevelopDynamicRangeStrategy ResolveDevelopDynamicRangeStrategy(
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    const DevelopToneAutoStats& stats,
    const DevelopDynamicRangeRegionEvidence& regionEvidence,
    float darkness,
    float shadowRescueNeed,
    float hdrNeed,
    float flatSceneNeed,
    float tinySpecularAllowance) {
    DevelopDynamicRangeStrategy strategy;
    if (!stats.valid) {
        strategy.id = "pendingRenderAnalysis";
        strategy.label = "Pending Render Analysis";
        strategy.reason = "Develop needs a rendered analysis pass before it can name the highlight and shadow strategy.";
        strategy.highlightPolicy = "Use conservative highlight rolloff until render statistics are available.";
        strategy.shadowPolicy = "Avoid aggressive shadow lift until noise and tone statistics are available.";
        return strategy;
    }

    const bool naturalIntent = intent == EditorNodeGraph::DevelopAutoIntent::NaturalFinished;
    const bool rangeIntent = intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;
    const bool flatIntent = intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
    const bool darkIntent = intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural;
    const bool punchyIntent = intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;
    const bool brightIntent = intent == EditorNodeGraph::DevelopAutoIntent::BrightNatural;

    strategy.highlightImportance = SaturateFloat(
        stats.highlightPressure * 0.58f +
        stats.clippingRatio * 5.0f +
        regionEvidence.localHighlightHotspotRisk * 0.16f +
        regionEvidence.meaningfulHighlightPressure * 0.18f +
        regionEvidence.broadHighlightPressure * 0.10f +
        hdrNeed * 0.18f +
        std::max(0.0f, guidance.highlightGuard) * 0.12f -
        tinySpecularAllowance * 0.12f);
    strategy.shadowReadability = SaturateFloat(
        shadowRescueNeed * 0.68f +
        regionEvidence.localShadowHotspotRisk * 0.12f +
        std::max(0.0f, guidance.shadowLift) * 0.16f +
        std::max(0.0f, guidance.dynamicRange - 1.0f) * 0.10f -
        stats.noiseRisk * 0.20f -
        regionEvidence.shadowNoiseLiftRisk * 0.12f -
        (darkIntent ? 0.12f : 0.0f));
    strategy.noiseConstraint = SaturateFloat(
        stats.noiseRisk +
        regionEvidence.shadowNoiseLiftRisk * 0.18f +
        std::max(0.0f, guidance.shadowLift) * 0.18f +
        std::max(0.0f, guidance.dynamicRange - 1.0f) * 0.08f);
    strategy.rangeCompression = SaturateFloat(
        hdrNeed * 0.50f +
        regionEvidence.localRangeConflict * 0.16f +
        regionEvidence.localEvConflict * 0.12f +
        std::max(0.0f, guidance.dynamicRange - 1.0f) * 0.24f +
        std::max(0.0f, guidance.highlightGuard) * 0.10f +
        std::max(0.0f, guidance.shadowLift) * 0.08f);
    strategy.brightnessHierarchyRisk = SaturateFloat(
        strategy.rangeCompression * 0.34f +
        regionEvidence.brightnessHierarchyRisk * 0.22f +
        regionEvidence.highlightGrayRisk * 0.18f +
        regionEvidence.flatGrayRisk * 0.10f +
        flatSceneNeed * 0.20f +
        stats.highlightPressure * 0.18f -
        std::max(0.0f, guidance.contrastBias) * 0.10f -
        std::max(0.0f, guidance.highlightCharacter) * 0.08f);
    strategy.smallSpecularClippingAllowed =
        tinySpecularAllowance > 0.42f &&
        stats.clippingRatio < 0.012f &&
        (!regionEvidence.valid || regionEvidence.smallSpecularLikely) &&
        regionEvidence.meaningfulHighlightPressure < 0.32f &&
        !rangeIntent;
    strategy.specularHighlightToleranceNeed = SaturateFloat(
        tinySpecularAllowance * 0.52f +
        (regionEvidence.smallSpecularLikely ? 0.22f : 0.0f) +
        ((naturalIntent || brightIntent || punchyIntent) ? 0.08f : 0.0f) -
        stats.highlightPressure * 0.20f -
        regionEvidence.broadHighlightPressure * 0.22f -
        regionEvidence.meaningfulHighlightPressure * 0.18f -
        SaturateFloat((stats.clippingRatio - 0.006f) / 0.020f) * 0.35f -
        (rangeIntent ? 0.16f : 0.0f) -
        (flatIntent ? 0.08f : 0.0f));
    strategy.broadHighlightGuardNeed = SaturateFloat(
        strategy.highlightImportance * 0.34f +
        regionEvidence.broadHighlightPressure * 0.24f +
        regionEvidence.meaningfulHighlightPressure * 0.18f +
        regionEvidence.localHighlightHotspotRisk * 0.16f +
        stats.clippingRatio * 2.60f +
        hdrNeed * 0.12f +
        (rangeIntent ? 0.08f : 0.0f) +
        (flatIntent ? 0.04f : 0.0f) -
        tinySpecularAllowance * 0.18f -
        strategy.specularHighlightToleranceNeed * 0.10f -
        (punchyIntent ? 0.03f : 0.0f));
    strategy.naturalContrastGuardNeed = SaturateFloat(
        strategy.brightnessHierarchyRisk * 0.42f +
        regionEvidence.brightnessHierarchyRisk * 0.20f +
        regionEvidence.highlightGrayRisk * 0.18f +
        regionEvidence.flatGrayRisk * 0.20f +
        flatSceneNeed * 0.16f +
        stats.textureConfidence * 0.06f +
        (naturalIntent ? 0.06f : 0.0f) +
        (punchyIntent ? 0.08f : 0.0f) -
        strategy.broadHighlightGuardNeed * 0.06f -
        (rangeIntent ? 0.10f : 0.0f) -
        (flatIntent ? 0.12f : 0.0f));
    strategy.brightHighlightRolloffNeed = SaturateFloat(
        strategy.highlightImportance * 0.54f +
        strategy.brightnessHierarchyRisk * 0.26f +
        regionEvidence.highlightGrayRisk * 0.08f +
        regionEvidence.localHighlightHotspotRisk * 0.10f +
        (brightIntent ? 0.08f : 0.0f) +
        (punchyIntent ? 0.06f : 0.0f) -
        stats.clippingRatio * 2.0f -
        (rangeIntent ? 0.04f : 0.0f));
    strategy.highlightBrightnessAnchorNeed = SaturateFloat(
        strategy.highlightImportance * 0.24f +
        strategy.brightnessHierarchyRisk * 0.22f +
        regionEvidence.highlightGrayRisk * 0.22f +
        strategy.broadHighlightGuardNeed * 0.16f +
        strategy.naturalContrastGuardNeed * 0.14f +
        strategy.brightHighlightRolloffNeed * 0.14f +
        regionEvidence.broadHighlightPressure * 0.08f +
        (naturalIntent ? 0.05f : 0.0f) +
        (brightIntent ? 0.08f : 0.0f) +
        (punchyIntent ? 0.05f : 0.0f) -
        strategy.specularHighlightToleranceNeed * 0.06f -
        stats.clippingRatio * 1.20f -
        regionEvidence.localHaloRisk * 0.04f -
        (rangeIntent ? 0.06f : 0.0f) -
        (flatIntent ? 0.04f : 0.0f));
    strategy.shadowNoiseFloorNeed = SaturateFloat(
        strategy.noiseConstraint * 0.42f +
        regionEvidence.shadowNoiseLiftRisk * 0.30f +
        regionEvidence.localShadowHotspotRisk * 0.12f +
        darkness * 0.14f +
        (darkIntent ? 0.08f : 0.0f) -
        strategy.shadowReadability * 0.18f -
        (rangeIntent ? 0.08f : 0.0f) -
        (flatIntent ? 0.04f : 0.0f));
    strategy.shadowReadabilityLiftNeed = SaturateFloat(
        strategy.shadowReadability * 0.42f +
        shadowRescueNeed * 0.22f +
        regionEvidence.localShadowHotspotRisk * 0.16f +
        SaturateFloat((0.74f - stats.noiseRisk) / 0.74f) * 0.16f +
        stats.textureConfidence * 0.08f +
        (rangeIntent ? 0.08f : 0.0f) +
        (flatIntent ? 0.06f : 0.0f) +
        (brightIntent ? 0.05f : 0.0f) -
        strategy.noiseConstraint * 0.20f -
        regionEvidence.shadowNoiseLiftRisk * 0.18f -
        strategy.shadowNoiseFloorNeed * 0.12f -
        (darkIntent ? 0.06f : 0.0f) -
        (punchyIntent ? 0.04f : 0.0f));
    strategy.localHaloGuardNeed = SaturateFloat(
        regionEvidence.localHaloRisk * 0.52f +
        regionEvidence.localRangeConflict * 0.18f +
        regionEvidence.localEvConflict * 0.14f +
        regionEvidence.localHighlightHotspotRisk * 0.10f +
        regionEvidence.localShadowHotspotRisk * 0.08f +
        strategy.rangeCompression * 0.10f +
        std::max(0.0f, guidance.dynamicRange - 1.0f) * 0.08f -
        (punchyIntent ? 0.04f : 0.0f));
    strategy.localHighlightHotspotRisk = regionEvidence.localHighlightHotspotRisk;
    strategy.localShadowHotspotRisk = regionEvidence.localShadowHotspotRisk;
    strategy.localRangeConflict = regionEvidence.localRangeConflict;
    strategy.localEvConflict = regionEvidence.localEvConflict;
    strategy.localHaloRisk = regionEvidence.localHaloRisk;
    strategy.flatGrayRisk = regionEvidence.flatGrayRisk;
    strategy.highlightGrayRisk = regionEvidence.highlightGrayRisk;
    strategy.meaningfulHighlightPressure = regionEvidence.meaningfulHighlightPressure;
    strategy.localExposureHighlightCrowding = regionEvidence.localExposureHighlightCrowding;
    strategy.localExposureShadowCrowding = regionEvidence.localExposureShadowCrowding;
    strategy.localExposureHaloStress = regionEvidence.localExposureHaloStress;
    strategy.localExposureFlatnessRisk = regionEvidence.localExposureFlatnessRisk;
    strategy.localExposureDamageRisk = regionEvidence.localExposureDamageRisk;
    strategy.regionEvidence = DevelopDynamicRangeRegionEvidenceToJson(regionEvidence);
    ResolveDevelopDynamicRangeStrategyMap(
        strategy,
        intent,
        guidance,
        stats,
        regionEvidence,
        shadowRescueNeed,
        hdrNeed,
        tinySpecularAllowance);
    ResolveDevelopLocalExposureStrategy(
        strategy,
        intent,
        stats,
        regionEvidence,
        shadowRescueNeed,
        hdrNeed);

    if (rangeIntent || (hdrNeed > 0.62f && strategy.highlightImportance > 0.42f)) {
        strategy.id = "maximumVisibleRange";
        strategy.label = "Maximum Visible Range";
        strategy.reason = "Auto is prioritizing range fitting with extra highlight and shadow guardrails for the selected intent.";
        strategy.highlightPolicy = "Compress broad highlights without claiming clipped-data recovery.";
        strategy.shadowPolicy = "Lift only shadows that clear the current noise and texture checks.";
    } else if (regionEvidence.valid &&
        strategy.localHaloGuardNeed > 0.46f &&
        regionEvidence.localHaloRisk > 0.34f) {
        strategy.id = "haloSafeLocalRange";
        strategy.label = "Halo-Safe Local Range";
        strategy.reason = "Rendered regional evidence shows halo or edge-risk pressure, so Auto should test safer Scene Prep range shaping before pushing local exposure harder.";
        strategy.highlightPolicy = "Protect local highlight transitions with stronger halo and smooth-gradient guardrails, without claiming clipped-data recovery.";
        strategy.shadowPolicy = "Avoid opening shadows across strong edges when that would create local glow or artificial relighting.";
    } else if (regionEvidence.valid &&
        (regionEvidence.localRangeConflict > 0.58f || regionEvidence.localEvConflict > 0.54f) &&
        (regionEvidence.localHaloRisk > 0.32f || regionEvidence.localEvConflict > 0.60f)) {
        strategy.id = "localizedRangeGuard";
        strategy.label = "Localized Range Guard";
        strategy.reason = "Rendered regional evidence shows local EV conflict near risky local range transitions, so Auto should prefer controlled local exposure over broad flattening.";
        strategy.highlightPolicy = "Restrain local highlight shaping where regional damage or halo risk is concentrated.";
        strategy.shadowPolicy = "Open shadows selectively and avoid pushing local range hard enough to create halos or noisy gray patches.";
    } else if (strategy.broadHighlightGuardNeed > 0.52f &&
        !strategy.smallSpecularClippingAllowed) {
        strategy.id = "broadHighlightGuard";
        strategy.label = "Broad Highlight Guard";
        strategy.reason = "Auto sees broad meaningful highlight pressure, so it should test local highlight compression instead of treating bright areas like disposable glints.";
        strategy.highlightPolicy = "Use Scene Prep to protect broad bright regions and smooth transitions without claiming clipped-data recovery.";
        strategy.shadowPolicy = "Avoid sacrificing useful mids and shadows unless broad highlight evidence really needs the headroom.";
    } else if (strategy.shadowNoiseFloorNeed > 0.54f &&
        (strategy.noiseConstraint > 0.58f ||
            regionEvidence.shadowNoiseLiftRisk > 0.54f ||
            darkIntent)) {
        strategy.id = "shadowNoiseFloor";
        strategy.label = "Shadow Noise Floor";
        strategy.reason = "Auto is testing whether noisy or low-value dark regions should stay darker instead of being lifted into gray noise.";
        strategy.highlightPolicy = "Keep highlight guard active while avoiding a broad exposure move that would force the shadow floor upward.";
        strategy.shadowPolicy = "Hold noisy shadows down with Scene Prep limits unless rendered evidence proves the shadows are clean and important.";
    } else if (strategy.shadowReadabilityLiftNeed > 0.50f &&
        strategy.noiseConstraint < 0.66f &&
        strategy.shadowNoiseFloorNeed < 0.48f) {
        strategy.id = "shadowReadabilityLift";
        strategy.label = "Shadow Readability Lift";
        strategy.reason = "Auto sees shadows that look readable enough to open locally, so it should test Scene Prep shadow lift without moving the RAW baseline.";
        strategy.highlightPolicy = "Keep highlight guard active while local shadow and midtone support is tested.";
        strategy.shadowPolicy = "Open clean meaningful shadows locally, with noise and halo guardrails still active.";
    } else if (darkIntent || (darkness > 0.42f && strategy.noiseConstraint > 0.54f)) {
        strategy.id = "moodPreservingRange";
        strategy.label = "Mood-Preserving Range";
        strategy.reason = "Auto is preserving low-key hierarchy because broad shadow lift would risk gray, noisy darkness.";
        strategy.highlightPolicy = "Keep bright accents controlled while preserving the dark scene mood.";
        strategy.shadowPolicy = "Hold noisy or mood-critical shadows darker unless rendered evidence asks for selective cleanup.";
    } else if (strategy.highlightImportance > 0.52f && strategy.shadowReadability > 0.34f) {
        strategy.id = "highlightMidsBalance";
        strategy.label = "Highlight / Midtone Balance";
        strategy.reason = "Auto sees both broad highlight pressure and useful mids/shadows, so it is testing lower placement with local support.";
        strategy.highlightPolicy = "Protect broad highlights first, then support mids locally.";
        strategy.shadowPolicy = "Use local exposure rather than a broad global lift where possible.";
    } else if (strategy.highlightBrightnessAnchorNeed > 0.40f &&
        !strategy.smallSpecularClippingAllowed) {
        strategy.id = "luminousHighlightAnchor";
        strategy.label = "Luminous Highlight Anchor";
        strategy.reason = "Auto sees protected highlight areas that may be flattening toward gray, so it should test finish-tone separation that keeps bright regions feeling bright.";
        strategy.highlightPolicy = "Anchor protected highlights with downstream shoulder and contrast shape; this preserves brightness feeling and does not recover clipped detail.";
        strategy.shadowPolicy = "Keep surrounding mids and darks separated enough that bright regions still read as light.";
    } else if (strategy.brightHighlightRolloffNeed > 0.45f) {
        strategy.id = "brightHighlightRolloff";
        strategy.label = "Bright Highlight Rolloff";
        strategy.reason = "Auto is guarding highlights but keeping a candidate that preserves the feeling of bright light instead of flattening highlights toward gray.";
        strategy.highlightPolicy = "Use finish-tone rolloff to keep highlights bright and smooth; tiny specular clipping may remain acceptable.";
        strategy.shadowPolicy = "Avoid trading believable highlight brightness for unnecessary global shadow lift.";
    } else if (strategy.naturalContrastGuardNeed > 0.42f &&
        !rangeIntent &&
        !flatIntent) {
        strategy.id = "naturalContrastGuard";
        strategy.label = "Natural Contrast Guard";
        strategy.reason = "Auto sees range compression or flat-gray risk, so it should test restoring natural separation in finish tone instead of adding more local exposure.";
        strategy.highlightPolicy = "Keep bright areas separated from mids with downstream shoulder/contrast shaping, without claiming highlight recovery.";
        strategy.shadowPolicy = "Let unimportant darks keep depth while protecting readable shadows from being crushed.";
    } else if (strategy.smallSpecularClippingAllowed &&
        strategy.specularHighlightToleranceNeed > 0.34f) {
        strategy.id = "specularHighlightTolerance";
        strategy.label = "Specular Highlight Tolerance";
        strategy.reason = "Auto sees mostly tiny point-source clipping, so it can test keeping glints bright instead of pulling the whole image down for them.";
        strategy.highlightPolicy = "Allow tiny specular cores to clip when broad highlight evidence is low, while keeping smooth finish-tone rolloff around them.";
        strategy.shadowPolicy = "Do not trade midtone or shadow placement away just to save tiny, low-importance glints.";
    } else if (strategy.shadowReadability > 0.50f && strategy.noiseConstraint < 0.70f) {
        strategy.id = "shadowReadability";
        strategy.label = "Shadow Readability";
        strategy.reason = "Auto is opening readable shadows because the selected intent and noise estimate allow it.";
        strategy.highlightPolicy = "Keep highlight guard active while allowing mids and shadows to rise.";
        strategy.shadowPolicy = "Open meaningful shadows with noise-aware limits.";
    } else if (punchyIntent) {
        strategy.id = "contrastHierarchy";
        strategy.label = "Contrast Hierarchy";
        strategy.reason = "Auto is preserving punch and endpoint separation while still watching highlight and shadow damage.";
        strategy.highlightPolicy = "Allow brighter highlights and deeper darks where rendered metrics stay believable.";
        strategy.shadowPolicy = "Let unimportant darkness stay dark when it supports the selected contrast intent.";
    }

    return strategy;
}

nlohmann::json DevelopDynamicRangeStrategyToJson(const DevelopDynamicRangeStrategy& strategy) {
    return {
        { "version", kDevelopDynamicRangeStrategyVersion },
        { "id", strategy.id },
        { "label", strategy.label },
        { "reason", strategy.reason },
        { "highlightPolicy", strategy.highlightPolicy },
        { "shadowPolicy", strategy.shadowPolicy },
        { "highlightImportance", strategy.highlightImportance },
        { "shadowReadability", strategy.shadowReadability },
        { "noiseConstraint", strategy.noiseConstraint },
        { "rangeCompression", strategy.rangeCompression },
        { "brightnessHierarchyRisk", strategy.brightnessHierarchyRisk },
        { "meaningfulHighlightPressure", strategy.meaningfulHighlightPressure },
        { "naturalContrastGuardNeed", strategy.naturalContrastGuardNeed },
        { "brightHighlightRolloffNeed", strategy.brightHighlightRolloffNeed },
        { "highlightBrightnessAnchorNeed", strategy.highlightBrightnessAnchorNeed },
        { "broadHighlightGuardNeed", strategy.broadHighlightGuardNeed },
        { "specularHighlightToleranceNeed", strategy.specularHighlightToleranceNeed },
        { "shadowReadabilityLiftNeed", strategy.shadowReadabilityLiftNeed },
        { "shadowNoiseFloorNeed", strategy.shadowNoiseFloorNeed },
        { "localHighlightHotspotRisk", strategy.localHighlightHotspotRisk },
        { "localShadowHotspotRisk", strategy.localShadowHotspotRisk },
        { "localRangeConflict", strategy.localRangeConflict },
        { "localEvConflict", strategy.localEvConflict },
        { "localHaloRisk", strategy.localHaloRisk },
        { "localHaloGuardNeed", strategy.localHaloGuardNeed },
        { "flatGrayRisk", strategy.flatGrayRisk },
        { "highlightGrayRisk", strategy.highlightGrayRisk },
        { "strategyMap", {
            { "version", kDevelopDynamicRangeStrategyMapVersion },
            { "highlightShadowAxis", strategy.strategyMapHighlightShadowAxis },
            { "contrastRangeAxis", strategy.strategyMapContrastRangeAxis },
            { "highlightPriority", strategy.strategyMapHighlightPriority },
            { "shadowVisibility", strategy.strategyMapShadowVisibility },
            { "naturalContrast", strategy.strategyMapNaturalContrast },
            { "visibleRange", strategy.strategyMapVisibleRange },
            { "reason", strategy.strategyMapReason }
        } },
        { "strategyMapVersion", kDevelopDynamicRangeStrategyMapVersion },
        { "strategyMapHighlightShadowAxis", strategy.strategyMapHighlightShadowAxis },
        { "strategyMapContrastRangeAxis", strategy.strategyMapContrastRangeAxis },
        { "strategyMapHighlightPriority", strategy.strategyMapHighlightPriority },
        { "strategyMapShadowVisibility", strategy.strategyMapShadowVisibility },
        { "strategyMapNaturalContrast", strategy.strategyMapNaturalContrast },
        { "strategyMapVisibleRange", strategy.strategyMapVisibleRange },
        { "strategyMapReason", strategy.strategyMapReason },
        { "localExposureStrategy", {
            { "version", kDevelopLocalExposureStrategyVersion },
            { "id", strategy.localExposureStrategyId },
            { "label", strategy.localExposureStrategyLabel },
            { "reason", strategy.localExposureStrategyReason },
            { "rangeRedistribution", strategy.localExposureRangeRedistribution },
            { "highlightCompression", strategy.localExposureHighlightCompression },
            { "shadowOpening", strategy.localExposureShadowOpening },
            { "noiseGuard", strategy.localExposureNoiseGuard },
            { "haloGuard", strategy.localExposureHaloGuard },
            { "textureGuard", strategy.localExposureTextureGuard },
            { "shadowEvBudget", strategy.localExposureShadowEvBudget },
            { "highlightEvBudget", strategy.localExposureHighlightEvBudget },
            { "strengthTarget", strategy.localExposureStrengthTarget },
            { "highlightCrowding", strategy.localExposureHighlightCrowding },
            { "shadowCrowding", strategy.localExposureShadowCrowding },
            { "haloStress", strategy.localExposureHaloStress },
            { "flatnessRisk", strategy.localExposureFlatnessRisk },
            { "damageRisk", strategy.localExposureDamageRisk }
        } },
        { "localExposureStrategyVersion", kDevelopLocalExposureStrategyVersion },
        { "localExposureStrategyId", strategy.localExposureStrategyId },
        { "localExposureStrategyLabel", strategy.localExposureStrategyLabel },
        { "localExposureStrategyReason", strategy.localExposureStrategyReason },
        { "localExposureRangeRedistribution", strategy.localExposureRangeRedistribution },
        { "localExposureHighlightCompression", strategy.localExposureHighlightCompression },
        { "localExposureShadowOpening", strategy.localExposureShadowOpening },
        { "localExposureNoiseGuard", strategy.localExposureNoiseGuard },
        { "localExposureHaloGuard", strategy.localExposureHaloGuard },
        { "localExposureTextureGuard", strategy.localExposureTextureGuard },
        { "localExposureShadowEvBudget", strategy.localExposureShadowEvBudget },
        { "localExposureHighlightEvBudget", strategy.localExposureHighlightEvBudget },
        { "localExposureStrengthTarget", strategy.localExposureStrengthTarget },
        { "localExposureHighlightCrowding", strategy.localExposureHighlightCrowding },
        { "localExposureShadowCrowding", strategy.localExposureShadowCrowding },
        { "localExposureHaloStress", strategy.localExposureHaloStress },
        { "localExposureFlatnessRisk", strategy.localExposureFlatnessRisk },
        { "localExposureDamageRisk", strategy.localExposureDamageRisk },
        { "smallSpecularClippingAllowed", strategy.smallSpecularClippingAllowed },
        { "regionEvidence", strategy.regionEvidence }
    };
}

DevelopDynamicRangeRegionEvidence BuildDevelopDynamicRangeRegionEvidenceFromMetrics(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
    const std::string& source,
    const std::string& candidateId,
    float renderScore) {
    DevelopDynamicRangeRegionEvidence evidence;
    evidence.valid = true;
    evidence.source = source;
    evidence.candidateId = candidateId;
    evidence.renderScore = renderScore;
    evidence.localHighlightPressure = SaturateFloat(metrics.localHighlightPressure);
    evidence.localShadowPressure = SaturateFloat(metrics.localShadowPressure);
    evidence.localDamageRiskMean = SaturateFloat(metrics.localDamageRiskMean);
    evidence.localDamageRiskPeak = SaturateFloat(metrics.localDamageRiskPeak);
    evidence.localLumaSpread = SaturateFloat(metrics.localLumaSpread);
    evidence.localEvSpreadStops = std::clamp(metrics.localEvSpreadStops, 0.0f, 8.0f);
    evidence.localEvConflict = SaturateFloat(metrics.localEvConflict);
    evidence.localContrastPeak = SaturateFloat(metrics.localContrastPeak);
    evidence.centerShadowFraction = SaturateFloat(metrics.centerShadowFraction);
    evidence.centerHighlightFraction = SaturateFloat(metrics.centerHighlightFraction);
    evidence.clippedFraction = SaturateFloat(metrics.clippedFraction);
    evidence.highlightFraction = SaturateFloat(metrics.highlightFraction);
    evidence.shadowFraction = SaturateFloat(metrics.shadowFraction);
    evidence.shadowTextureRisk = SaturateFloat(metrics.shadowTextureRisk);
    evidence.haloRiskFraction = SaturateFloat(metrics.haloRiskFraction);
    evidence.lowSaturationFraction = SaturateFloat(metrics.lowSaturationFraction);
    evidence.highlightBandFraction = SaturateFloat(metrics.highlightBandFraction);
    evidence.highlightMeanLuma = SaturateFloat(metrics.highlightMeanLuma);
    evidence.highlightLowSaturationFraction = SaturateFloat(metrics.highlightLowSaturationFraction);
    evidence.highlightGrayRisk = SaturateFloat(metrics.highlightGrayRisk);
    evidence.highlightTileCoverage = SaturateFloat(metrics.highlightTileCoverage);
    evidence.highlightStructureScore = SaturateFloat(metrics.highlightStructureScore);
    evidence.meaningfulHighlightPressure = SaturateFloat(metrics.meaningfulHighlightPressure);
    evidence.contrastSpan = SaturateFloat(metrics.contrastSpan);
    evidence.peakTile = metrics.localDamageRiskPeakTile;
    evidence.localExposureHighlightCrowding = SaturateFloat(metrics.localExposureHighlightCrowding);
    evidence.localExposureShadowCrowding = SaturateFloat(metrics.localExposureShadowCrowding);
    evidence.localExposureHaloStress = SaturateFloat(metrics.localExposureHaloStress);
    evidence.localExposureFlatnessRisk = SaturateFloat(metrics.localExposureFlatnessRisk);
    evidence.localExposureDamageRisk = SaturateFloat(metrics.localExposureDamageRisk);
    evidence.subjectCenterPrior = SaturateFloat(metrics.subjectCenterPrior);
    evidence.subjectReadabilityPressure = SaturateFloat(metrics.subjectReadabilityPressure);
    evidence.subjectProtectionPressure = SaturateFloat(metrics.subjectProtectionPressure);
    evidence.subjectMoodPreservationPressure = SaturateFloat(metrics.subjectMoodPreservationPressure);
    evidence.subjectImportanceConfidence = SaturateFloat(metrics.subjectImportanceConfidence);

    const float broadHighlightArea = SaturateFloat(
        DevelopRegionRiskAbove(metrics.highlightFraction, 0.12f, 0.42f) * 0.42f +
        DevelopRegionRiskAbove(metrics.localHighlightPressure, 0.34f, 0.72f) * 0.42f +
        DevelopRegionRiskAbove(metrics.centerHighlightFraction, 0.18f, 0.46f) * 0.16f);
    evidence.broadHighlightPressure = SaturateFloat(
        broadHighlightArea +
        evidence.meaningfulHighlightPressure * 0.16f +
        DevelopRegionRiskAbove(metrics.clippedFraction, 0.006f, 0.026f) * 0.20f);
    evidence.localHighlightHotspotRisk = SaturateFloat(
        DevelopRegionRiskAbove(metrics.localHighlightPressure, 0.48f, 0.86f) * 0.40f +
        DevelopRegionRiskAbove(metrics.centerHighlightFraction, 0.26f, 0.58f) * 0.18f +
        DevelopRegionRiskAbove(metrics.clippedFraction, 0.004f, 0.020f) * 0.20f +
        evidence.meaningfulHighlightPressure * 0.10f +
        evidence.localExposureHighlightCrowding * 0.12f +
        metrics.localDamageRiskPeak * 0.16f +
        DevelopRegionRiskAbove(metrics.haloRiskFraction, 0.04f, 0.16f) * 0.06f);
    evidence.localShadowHotspotRisk = SaturateFloat(
        DevelopRegionRiskAbove(metrics.localShadowPressure, 0.58f, 0.90f) * 0.42f +
        DevelopRegionRiskAbove(metrics.centerShadowFraction, 0.38f, 0.72f) * 0.18f +
        DevelopRegionRiskAbove(metrics.shadowFraction, 0.48f, 0.82f) * 0.18f +
        evidence.localExposureShadowCrowding * 0.10f +
        metrics.localDamageRiskPeak * 0.12f +
        metrics.shadowTextureRisk * 0.10f);
    evidence.shadowNoiseLiftRisk = SaturateFloat(
        evidence.localShadowHotspotRisk * 0.42f +
        metrics.shadowTextureRisk * 0.42f +
        DevelopRegionRiskAbove(metrics.shadowFraction, 0.48f, 0.86f) * 0.16f);
    evidence.localHaloRisk = SaturateFloat(
        DevelopRegionRiskAbove(metrics.haloRiskFraction, 0.04f, 0.18f) * 0.48f +
        DevelopRegionRiskAbove(metrics.edgeContrast, 0.34f, 0.72f) * 0.22f +
        DevelopRegionRiskAbove(metrics.localContrastPeak, 0.76f, 0.96f) * 0.20f +
        evidence.localExposureHaloStress * 0.14f +
        evidence.localEvConflict * 0.08f +
        metrics.localDamageRiskMean * 0.10f);
    evidence.flatGrayRisk = SaturateFloat(
        DevelopRegionRiskBelow(metrics.contrastSpan, 0.30f, 0.12f) * 0.32f +
        DevelopRegionRiskBelow(metrics.localContrastPeak, 0.34f, 0.14f) * 0.22f +
        DevelopRegionRiskAbove(metrics.lowSaturationFraction, 0.70f, 0.94f) * 0.22f +
        evidence.highlightGrayRisk * 0.12f +
        evidence.localExposureFlatnessRisk * 0.14f +
        DevelopRegionRiskBelow(metrics.localLumaSpread, 0.16f, 0.04f) * 0.18f -
        DevelopRegionRiskAbove(metrics.highlightFraction, 0.22f, 0.42f) * 0.10f);
    evidence.localRangeConflict = SaturateFloat(
        evidence.localHighlightHotspotRisk * 0.34f +
        evidence.localShadowHotspotRisk * 0.28f +
        evidence.localHaloRisk * 0.18f +
        evidence.localEvConflict * 0.22f +
        evidence.localExposureDamageRisk * 0.12f +
        DevelopRegionRiskAbove(metrics.localLumaSpread, 0.28f, 0.58f) * 0.14f +
        metrics.localDamageRiskMean * 0.12f);
    evidence.brightnessHierarchyRisk = SaturateFloat(
        evidence.flatGrayRisk * 0.38f +
        evidence.highlightGrayRisk * 0.30f +
        DevelopRegionRiskBelow(metrics.localLumaSpread, 0.18f, 0.06f) * 0.20f +
        DevelopRegionRiskBelow(metrics.contrastSpan, 0.32f, 0.16f) * 0.18f +
        DevelopRegionRiskAbove(metrics.lowSaturationFraction, 0.72f, 0.94f) * 0.14f +
        DevelopRegionRiskAbove(metrics.highlightFraction, 0.62f, 0.86f) * 0.10f);
    evidence.smallSpecularLikely =
        metrics.clippedFraction > 0.0f &&
        metrics.clippedFraction < 0.010f &&
        metrics.highlightFraction < 0.18f &&
        metrics.localHighlightPressure < 0.38f &&
        evidence.meaningfulHighlightPressure < 0.28f;
    return evidence;
}

} // namespace Stack::Editor::DevelopDynamicRange
