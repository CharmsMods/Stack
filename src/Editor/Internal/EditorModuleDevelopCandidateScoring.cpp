#include "Editor/Internal/EditorModuleDevelopCandidateScoring.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace {

float SaturateFloat(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

} // namespace

namespace Stack::Editor::DevelopCandidateScoring {

using namespace Stack::Editor::DevelopDynamicRange;
using namespace Stack::Editor::DevelopSubjectImportance;

bool TryResolveDevelopWhiteBalanceProbeMode(
    const std::string& candidateId,
    Raw::WhiteBalanceMode& outMode) {
    if (candidateId == "wbNeutralCorrection") {
        outMode = Raw::WhiteBalanceMode::Neutral;
        return true;
    }
    if (candidateId == "wbDaylightCorrection") {
        outMode = Raw::WhiteBalanceMode::Auto;
        return true;
    }
    if (candidateId == "wbCameraMood") {
        outMode = Raw::WhiteBalanceMode::AsShot;
        return true;
    }
    return false;
}

bool IsDevelopWhiteBalanceProbeCandidateId(const std::string& candidateId) {
    Raw::WhiteBalanceMode mode = Raw::WhiteBalanceMode::AsShot;
    return TryResolveDevelopWhiteBalanceProbeMode(candidateId, mode);
}

float DevelopAutoCandidateDistance(
    const EditorNodeGraph::DevelopAutoGuidance& a,
    const EditorNodeGraph::DevelopAutoGuidance& b) {
    return
        std::fabs(a.exposureBias - b.exposureBias) * 0.70f +
        std::fabs(a.dynamicRange - b.dynamicRange) * 0.35f +
        std::fabs(a.shadowLift - b.shadowLift) * 0.40f +
        std::fabs(a.highlightGuard - b.highlightGuard) * 0.40f +
        std::fabs(a.highlightCharacter - b.highlightCharacter) * 0.20f +
        std::fabs(a.contrastBias - b.contrastBias) * 0.45f +
        std::fabs(a.subjectSceneBias - b.subjectSceneBias) * 0.18f +
        std::fabs(a.moodReadabilityBias - b.moodReadabilityBias) * 0.18f +
        std::fabs(a.autoStrength - b.autoStrength) * 0.20f;
}

std::string DevelopRenderedRevisionStageForRefineIntent(const std::string& refineIntent) {
    if (refineIntent == "protectHighlights") {
        return "rawGlobal";
    }
    if (refineIntent == "brightenMids" || refineIntent == "openShadows") {
        return "scenePrep";
    }
    if (refineIntent == "addContrast") {
        return "finishTone";
    }
    if (refineIntent == "cleanShadows" || refineIntent == "preserveTexture") {
        return "rawCleanup";
    }
    return "multiStage";
}

bool IsDevelopFinishToneProbeCandidateId(const std::string& candidateId) {
    return
        candidateId == "strongerContrast" ||
        candidateId == "toneSofterRolloff" ||
        candidateId == "naturalContrastGuard" ||
        candidateId == "brightHighlightRolloff" ||
        candidateId == "luminousHighlightAnchor" ||
        candidateId == "specularHighlightTolerance" ||
        candidateId == "tonePunchierShape" ||
        candidateId == "toneFlatterEditing" ||
        candidateId == "toneDarkerToe" ||
        candidateId == "renderedLocalContrastShape";
}

bool IsDevelopSubjectIntentCandidateId(const std::string& candidateId) {
    return
        candidateId == "subjectReadableMids" ||
        candidateId == "sceneMoodPreservation";
}

std::string DevelopRenderedRevisionStageForCandidateId(const std::string& candidateId) {
    if (candidateId == "protectHighlights" ||
        candidateId == "highlightProtectedMids" ||
        candidateId == "renderedLocalHighlightRestraint" ||
        IsDevelopWhiteBalanceProbeCandidateId(candidateId)) {
        return "rawGlobal";
    }
    if (candidateId == "brighterMids" ||
        candidateId == "maximumRange" ||
        candidateId == "broadHighlightGuard" ||
        candidateId == "haloSafeLocalRange" ||
        candidateId == "localRangeGuard" ||
        candidateId == "shadowReadabilityLift" ||
        candidateId == "shadowNoiseFloor" ||
        IsDevelopSubjectIntentCandidateId(candidateId) ||
        candidateId == "renderedLocalBrightenMids" ||
        candidateId == "renderedLocalShadowOpening") {
        return "scenePrep";
    }
    if (IsDevelopFinishToneProbeCandidateId(candidateId)) {
        return "finishTone";
    }
    if (candidateId == "cleanShadows" ||
        candidateId == "preserveTexture" ||
        candidateId == "renderedLocalCleanShadows" ||
        candidateId == "renderedLocalPreserveTexture") {
        return "rawCleanup";
    }
    return "multiStage";
}

std::string DevelopRenderedRevisionStageForGuidanceDelta(
    const EditorNodeGraph::DevelopAutoGuidance& from,
    const EditorNodeGraph::DevelopAutoGuidance& to,
    const std::string& candidateId) {
    const std::string explicitStage = DevelopRenderedRevisionStageForCandidateId(candidateId);
    if (explicitStage != "multiStage") {
        return explicitStage;
    }

    const float rawGlobalDelta =
        std::fabs(to.exposureBias - from.exposureBias) * 0.80f +
        std::max(0.0f, to.highlightGuard - from.highlightGuard) * 0.45f;
    const float scenePrepDelta =
        std::fabs(to.dynamicRange - from.dynamicRange) * 0.35f +
        std::fabs(to.shadowLift - from.shadowLift) * 0.45f +
        std::fabs(to.highlightGuard - from.highlightGuard) * 0.20f;
    const float finishToneDelta =
        std::fabs(to.contrastBias - from.contrastBias) * 0.55f +
        std::fabs(to.highlightCharacter - from.highlightCharacter) * 0.35f;

    if (rawGlobalDelta < 0.055f &&
        scenePrepDelta < 0.055f &&
        finishToneDelta < 0.055f) {
        return "multiStage";
    }
    if (rawGlobalDelta >= scenePrepDelta && rawGlobalDelta >= finishToneDelta) {
        return "rawGlobal";
    }
    if (scenePrepDelta >= finishToneDelta) {
        return "scenePrep";
    }
    return "finishTone";
}

std::string DevelopRenderedRevisionStageReason(
    const std::string& stage,
    const std::string& fallback) {
    if (stage == "rawGlobal") {
        return "Rendered feedback points to RAW/global placement or highlight reconstruction as the earliest responsible stage.";
    }
    if (stage == "scenePrep") {
        return "Rendered feedback points to scene-prep local exposure/range shaping as the earliest responsible stage.";
    }
    if (stage == "finishTone") {
        return "Rendered feedback points to finish tone or contrast shape as the earliest responsible stage.";
    }
    if (stage == "rawCleanup") {
        return "Rendered feedback points to RAW cleanup, denoise, or texture handling as the earliest responsible stage.";
    }
    return fallback.empty()
        ? "Rendered feedback affects multiple authored stages and must be validated by another rendered pass."
        : fallback;
}

std::uint64_t BuildDevelopAutoCandidateContextFingerprint(
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopSubjectImportanceMap& subjectImportance,
    const DevelopToneAutoStats& stats) {
    std::uint64_t hash = 1469598103934665603ull;
    auto addValue = [&](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    auto addString = [&](const std::string& value) {
        for (unsigned char ch : value) {
            addValue(static_cast<std::uint64_t>(ch));
        }
    };
    auto addFloat = [&](float value, float scale) {
        const int quantized = static_cast<int>(std::lround(value * scale));
        addValue(static_cast<std::uint64_t>(static_cast<std::int64_t>(quantized)));
    };

    addString(EditorNodeGraph::DevelopAutoIntentStableString(intent));
    addFloat(guidance.autoStrength, 100.0f);
    addFloat(guidance.exposureBias, 100.0f);
    addFloat(guidance.dynamicRange, 100.0f);
    addFloat(guidance.shadowLift, 100.0f);
    addFloat(guidance.highlightGuard, 100.0f);
    addFloat(guidance.highlightCharacter, 100.0f);
    addFloat(guidance.contrastBias, 100.0f);
    addFloat(guidance.subjectSceneBias, 100.0f);
    addFloat(guidance.moodReadabilityBias, 100.0f);
    addValue(subjectImportance.enabled ? 1ull : 0ull);
    addValue(static_cast<std::uint64_t>(subjectImportance.regions.size()));
    for (const EditorNodeGraph::DevelopSubjectImportanceRegion& region : subjectImportance.regions) {
        addValue(region.enabled ? 1ull : 0ull);
        addValue(static_cast<std::uint64_t>(region.id));
        addString(EditorNodeGraph::DevelopSubjectImportanceModeStableString(region.mode));
        addFloat(region.centerX, 1000.0f);
        addFloat(region.centerY, 1000.0f);
        addFloat(region.radiusX, 1000.0f);
        addFloat(region.radiusY, 1000.0f);
        addFloat(region.feather, 1000.0f);
        addFloat(region.strength, 1000.0f);
    }
    addValue(static_cast<std::uint64_t>(subjectImportance.strokes.size()));
    for (const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : subjectImportance.strokes) {
        addValue(stroke.enabled ? 1ull : 0ull);
        addValue(stroke.subtract ? 1ull : 0ull);
        addValue(static_cast<std::uint64_t>(stroke.id));
        addString(EditorNodeGraph::DevelopSubjectImportanceModeStableString(stroke.mode));
        addFloat(stroke.radius, 1000.0f);
        addFloat(stroke.feather, 1000.0f);
        addFloat(stroke.strength, 1000.0f);
        addValue(static_cast<std::uint64_t>(stroke.points.size()));
        for (const EditorNodeGraph::DevelopSubjectImportanceStrokePoint& point : stroke.points) {
            addFloat(point.x, 1000.0f);
            addFloat(point.y, 1000.0f);
        }
    }
    addValue(stats.valid ? 1ull : 0ull);
    addValue(static_cast<std::uint64_t>(std::max(0, stats.sceneProfile)));
    addFloat(stats.shadowPercentile, 200.0f);
    addFloat(stats.midtonePercentile, 200.0f);
    addFloat(stats.highlightPercentile, 200.0f);
    addFloat(stats.clippingRatio, 5000.0f);
    addFloat(stats.highlightPressure, 100.0f);
    addFloat(stats.noiseRisk, 100.0f);
    addFloat(stats.textureConfidence, 100.0f);
    addFloat(stats.hdrSpreadEv, 20.0f);
    return hash;
}

std::uint64_t BuildDevelopAutoCandidateGuidanceFingerprint(
    const std::string& candidateId,
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    const EditorNodeGraph::DevelopSubjectImportanceMap* subjectImportance) {
    std::uint64_t hash = 1469598103934665603ull;
    auto addValue = [&](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    auto addString = [&](const std::string& value) {
        for (unsigned char ch : value) {
            addValue(static_cast<std::uint64_t>(ch));
        }
    };
    auto addFloat = [&](float value, float scale) {
        const int quantized = static_cast<int>(std::lround(value * scale));
        addValue(static_cast<std::uint64_t>(static_cast<std::int64_t>(quantized)));
    };

    addString(candidateId);
    addString(EditorNodeGraph::DevelopAutoIntentStableString(guidance.intent));
    addFloat(guidance.autoStrength, 100.0f);
    addFloat(guidance.exposureBias, 100.0f);
    addFloat(guidance.dynamicRange, 100.0f);
    addFloat(guidance.shadowLift, 100.0f);
    addFloat(guidance.highlightGuard, 100.0f);
    addFloat(guidance.highlightCharacter, 100.0f);
    addFloat(guidance.contrastBias, 100.0f);
    addFloat(guidance.subjectSceneBias, 100.0f);
    addFloat(guidance.moodReadabilityBias, 100.0f);
    if (subjectImportance) {
        addValue(subjectImportance->enabled ? 1ull : 0ull);
        addValue(static_cast<std::uint64_t>(subjectImportance->regions.size()));
        for (const EditorNodeGraph::DevelopSubjectImportanceRegion& region : subjectImportance->regions) {
            addValue(region.enabled ? 1ull : 0ull);
            addValue(static_cast<std::uint64_t>(region.id));
            addString(EditorNodeGraph::DevelopSubjectImportanceModeStableString(region.mode));
            addFloat(region.centerX, 1000.0f);
            addFloat(region.centerY, 1000.0f);
            addFloat(region.radiusX, 1000.0f);
            addFloat(region.radiusY, 1000.0f);
            addFloat(region.feather, 1000.0f);
            addFloat(region.strength, 1000.0f);
        }
        addValue(static_cast<std::uint64_t>(subjectImportance->strokes.size()));
        for (const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : subjectImportance->strokes) {
            addValue(stroke.enabled ? 1ull : 0ull);
            addValue(stroke.subtract ? 1ull : 0ull);
            addValue(static_cast<std::uint64_t>(stroke.id));
            addString(EditorNodeGraph::DevelopSubjectImportanceModeStableString(stroke.mode));
            addFloat(stroke.radius, 1000.0f);
            addFloat(stroke.feather, 1000.0f);
            addFloat(stroke.strength, 1000.0f);
            addValue(static_cast<std::uint64_t>(stroke.points.size()));
            for (const EditorNodeGraph::DevelopSubjectImportanceStrokePoint& point : stroke.points) {
                addFloat(point.x, 1000.0f);
                addFloat(point.y, 1000.0f);
            }
        }
    }
    return hash;
}

float ScoreDevelopAutoCandidate(
    const std::string& id,
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopAutoGuidance& base,
    const DevelopToneAutoStats& stats,
    const DevelopDynamicRangeRegionEvidence& regionEvidence,
    const DevelopDynamicRangeStrategy& dynamicRangeStrategy,
    const DevelopSubjectSceneIntent& subjectSceneIntent,
    float darkness,
    float shadowRescueNeed,
    float hdrNeed,
    float flatSceneNeed,
    float underBrightBroadHighlightEv) {
    const bool naturalIntent = intent == EditorNodeGraph::DevelopAutoIntent::NaturalFinished;
    const bool cleanIntent = intent == EditorNodeGraph::DevelopAutoIntent::CleanBase;
    const bool flatIntent = intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
    const bool brightIntent = intent == EditorNodeGraph::DevelopAutoIntent::BrightNatural;
    const bool darkIntent = intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural;
    const bool punchyIntent = intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;
    const bool rangeIntent = intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;
    const float smallSpecularSignal =
        SaturateFloat((0.012f - stats.clippingRatio) / 0.012f) *
        SaturateFloat((0.44f - stats.highlightPressure) / 0.44f) *
        ((!regionEvidence.valid || regionEvidence.smallSpecularLikely) ? 1.0f : 0.45f);
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
    const float broadHighlightSignal = SaturateFloat(
        stats.highlightPressure * 0.50f +
        regionEvidence.broadHighlightPressure * 0.24f +
        regionEvidence.meaningfulHighlightPressure * 0.20f +
        regionEvidence.localHighlightHotspotRisk * 0.16f +
        stats.clippingRatio * 1.60f -
        smallSpecularSignal * 0.18f);
    const float shadowReadabilitySignal = SaturateFloat(
        shadowRescueNeed * 0.44f +
        regionEvidence.localShadowHotspotRisk * 0.18f +
        SaturateFloat((0.74f - stats.noiseRisk) / 0.74f) * 0.18f +
        stats.textureConfidence * 0.10f -
        regionEvidence.shadowNoiseLiftRisk * 0.18f -
        darkness * (darkIntent ? 0.08f : 0.02f));
    const float highlightBrightnessSignal = SaturateFloat(
        stats.highlightPressure * 0.28f +
        broadHighlightSignal * 0.22f +
        regionEvidence.brightnessHierarchyRisk * 0.20f +
        regionEvidence.flatGrayRisk * 0.10f +
        hdrNeed * 0.08f +
        SaturateFloat((0.020f - stats.clippingRatio) / 0.020f) * 0.08f -
        smallSpecularSignal * 0.06f -
        regionEvidence.localHaloRisk * 0.04f);

    float score = 0.0f;
    if (id == "base") {
        const float stableNatural = SaturateFloat(
            (1.0f - shadowRescueNeed * 0.70f) *
            (1.0f - stats.highlightPressure * 0.55f) *
            (1.0f - stats.noiseRisk * 0.40f) *
            (1.0f - regionEvidence.localRangeConflict * 0.25f) *
            (1.0f - subjectReadabilityBias * 0.12f));
        score = stats.valid ? (0.52f + stableNatural * 0.22f) : 0.70f;
        if (naturalIntent) score += 0.05f;
    } else if (id == "protectHighlights") {
        score =
            0.40f +
            stats.highlightPressure * 0.34f +
            regionEvidence.localHighlightHotspotRisk * 0.08f +
            stats.clippingRatio * 2.40f +
            hdrNeed * 0.15f +
            mapHighlightBias * 0.08f +
            subjectProtectionBias * 0.08f +
            subjectRefinedProtectionBias * 0.04f;
        if (rangeIntent) score += 0.10f;
        if (brightIntent) score -= 0.05f;
        if (darkIntent) score += 0.05f;
    } else if (id == "highlightProtectedMids") {
        score =
            0.39f +
            stats.highlightPressure * 0.22f +
            hdrNeed * 0.16f +
            shadowRescueNeed * 0.10f +
            regionEvidence.localRangeConflict * 0.07f +
            regionEvidence.localHighlightHotspotRisk * 0.04f +
            mapHighlightBias * 0.04f +
            mapRangeBias * 0.04f +
            subjectPriorityBias * 0.05f +
            subjectReadabilityBias * 0.04f +
            subjectRefinedReadabilityBias * 0.03f +
            SaturateFloat(underBrightBroadHighlightEv / 1.25f) * 0.06f -
            stats.noiseRisk * 0.08f;
        if (rangeIntent) score += 0.10f;
        if (flatIntent) score += 0.06f;
        if (brightIntent) score -= 0.04f;
        if (darkIntent) score += 0.04f;
    } else if (id == "broadHighlightGuard") {
        score =
            0.36f +
            broadHighlightSignal * 0.28f +
            hdrNeed * 0.08f +
            SaturateFloat(stats.clippingRatio / 0.030f) * 0.08f -
            smallSpecularSignal * 0.10f -
            stats.noiseRisk * 0.03f +
            mapHighlightBias * 0.08f +
            mapRangeBias * 0.03f +
            subjectProtectionBias * 0.06f +
            subjectRefinedProtectionBias * 0.04f;
        if (rangeIntent) score += 0.14f;
        if (flatIntent || cleanIntent) score += 0.07f;
        if (naturalIntent) score += 0.04f;
        if (brightIntent) score -= 0.03f;
        if (punchyIntent) score -= 0.05f;
    } else if (id == "brighterMids") {
        score =
            0.38f +
            shadowRescueNeed * 0.30f +
            regionEvidence.localShadowHotspotRisk * 0.08f -
            regionEvidence.shadowNoiseLiftRisk * 0.05f +
            SaturateFloat(underBrightBroadHighlightEv / 1.25f) * 0.16f -
            stats.highlightPressure * 0.16f -
            stats.noiseRisk * 0.10f +
            mapShadowBias * 0.08f +
            subjectReadabilityBias * 0.08f +
            subjectRefinedReadabilityBias * 0.04f;
        if (brightIntent) score += 0.16f;
        if (flatIntent) score += 0.08f;
        if (darkIntent) score -= 0.12f;
    } else if (id == "maximumRange") {
        score =
            0.38f +
            hdrNeed * 0.32f +
            regionEvidence.localRangeConflict * 0.10f +
            regionEvidence.localEvConflict * 0.06f +
            SaturateFloat((base.dynamicRange - 1.0f) / 1.4f) * 0.12f +
            stats.highlightPressure * 0.10f -
            stats.noiseRisk * 0.08f -
            regionEvidence.shadowNoiseLiftRisk * 0.04f +
            mapRangeBias * 0.12f -
            mapContrastBias * 0.04f;
        if (rangeIntent) score += 0.20f;
        if (flatIntent) score += 0.10f;
        if (punchyIntent) score -= 0.06f;
    } else if (id == "shadowReadabilityLift") {
        score =
            0.36f +
            shadowReadabilitySignal * 0.30f +
            shadowRescueNeed * 0.10f +
            regionEvidence.localShadowHotspotRisk * 0.08f -
            stats.noiseRisk * 0.12f -
            regionEvidence.shadowNoiseLiftRisk * 0.12f -
            darkness * 0.04f +
            mapShadowBias * 0.08f +
            subjectReadabilityBias * 0.12f +
            subjectPriorityBias * 0.04f +
            subjectRefinedReadabilityBias * 0.05f;
        if (brightIntent) score += 0.12f;
        if (flatIntent || rangeIntent) score += 0.10f;
        if (naturalIntent) score += 0.05f;
        if (cleanIntent) score += 0.03f;
        if (darkIntent) score -= 0.08f;
        if (punchyIntent) score -= 0.04f;
    } else if (id == "subjectReadableMids") {
        const float userRevealBias = SaturateFloat(
            std::max(0.0f, subjectSceneIntent.userSubjectSceneBias) * 0.46f +
            std::max(0.0f, subjectSceneIntent.userMoodReadabilityBias) * 0.42f +
            subjectSceneIntent.userGuidanceStrength * 0.12f);
        const float subjectRevealSignal = SaturateFloat(
            subjectSceneIntent.subjectPriority * 0.26f +
            subjectSceneIntent.improveReadability * 0.24f +
            subjectSceneIntent.readabilityPressure * 0.14f +
            subjectSceneIntent.automaticConfidence * 0.08f +
            userRevealBias * 0.24f +
            regionEvidence.localShadowHotspotRisk * 0.08f -
            regionEvidence.shadowNoiseLiftRisk * 0.10f -
            regionEvidence.localHaloRisk * 0.05f);
        score =
            0.37f +
            subjectRevealSignal * 0.30f +
            subjectPriorityBias * 0.08f +
            subjectReadabilityBias * 0.12f +
            subjectRefinedReadabilityBias * 0.07f +
            subjectRefinedConfidenceBias * 0.03f +
            shadowReadabilitySignal * 0.08f -
            subjectMoodBias * 0.04f -
            stats.noiseRisk * 0.08f -
            regionEvidence.shadowNoiseLiftRisk * 0.08f -
            regionEvidence.localHaloRisk * 0.04f;
        if (brightIntent) score += 0.10f;
        if (flatIntent || rangeIntent) score += 0.07f;
        if (naturalIntent) score += 0.05f;
        if (darkIntent) score -= 0.05f;
    } else if (id == "sceneMoodPreservation") {
        const float userSceneBias = SaturateFloat(
            std::max(0.0f, -subjectSceneIntent.userSubjectSceneBias) * 0.34f +
            std::max(0.0f, -subjectSceneIntent.userMoodReadabilityBias) * 0.44f +
            subjectSceneIntent.userGuidanceStrength * 0.10f);
        const float sceneMoodSignal = SaturateFloat(
            subjectSceneIntent.sceneIntegrity * 0.24f +
            subjectSceneIntent.preserveMood * 0.26f +
            subjectSceneIntent.moodPreservationPressure * 0.14f +
            darkness * 0.12f +
            stats.noiseRisk * 0.08f +
            userSceneBias * 0.22f -
            subjectReadabilityBias * 0.06f);
        score =
            0.36f +
            sceneMoodSignal * 0.30f +
            subjectMoodBias * 0.12f +
            subjectRefinedMoodBias * 0.06f +
            std::max(0.0f, subjectSceneIntent.sceneIntegrity - 0.5f) * 0.08f +
            regionEvidence.shadowNoiseLiftRisk * 0.06f +
            stats.noiseRisk * 0.04f -
            subjectReadabilityBias * 0.08f;
        if (darkIntent) score += 0.16f;
        if (naturalIntent) score += 0.06f;
        if (cleanIntent) score += 0.03f;
        if (brightIntent) score -= 0.10f;
        if (rangeIntent || flatIntent) score -= 0.04f;
    } else if (id == "preserveMood") {
        score =
            0.36f +
            darkness * 0.25f +
            regionEvidence.shadowNoiseLiftRisk * 0.10f +
            stats.noiseRisk * 0.10f +
            stats.highlightPressure * 0.05f +
            subjectMoodBias * 0.14f;
        if (darkIntent) score += 0.22f;
        if (naturalIntent) score += 0.04f;
        if (brightIntent) score -= 0.12f;
    } else if (id == "naturalContrastGuard") {
        score =
            0.36f +
            flatSceneNeed * 0.18f +
            regionEvidence.flatGrayRisk * 0.18f +
            regionEvidence.brightnessHierarchyRisk * 0.18f +
            stats.textureConfidence * 0.08f -
            hdrNeed * 0.05f -
            stats.highlightPressure * 0.04f -
            regionEvidence.localHaloRisk * 0.04f +
            mapContrastBias * 0.08f -
            mapRangeBias * 0.03f +
            std::max(0.0f, subjectSceneIntent.sceneIntegrity - 0.5f) * 0.06f;
        if (naturalIntent) score += 0.10f;
        if (punchyIntent) score += 0.12f;
        if (darkIntent) score += 0.05f;
        if (brightIntent) score += 0.03f;
        if (rangeIntent) score -= 0.10f;
        if (flatIntent) score -= 0.12f;
    } else if (id == "strongerContrast") {
        score =
            0.36f +
            flatSceneNeed * 0.22f +
            regionEvidence.flatGrayRisk * 0.08f -
            regionEvidence.localHaloRisk * 0.05f +
            (1.0f - SaturateFloat(stats.highlightPressure)) * 0.05f -
            hdrNeed * 0.08f +
            mapContrastBias * 0.06f;
        if (punchyIntent) score += 0.24f;
        if (naturalIntent) score += 0.04f;
        if (rangeIntent || flatIntent) score -= 0.08f;
    } else if (id == "toneSofterRolloff") {
        score =
            0.37f +
            stats.highlightPressure * 0.20f +
            regionEvidence.localHighlightHotspotRisk * 0.08f +
            hdrNeed * 0.14f +
            stats.clippingRatio * 1.10f -
            flatSceneNeed * 0.03f +
            mapHighlightBias * 0.04f;
        if (rangeIntent || flatIntent) score += 0.08f;
        if (brightIntent) score += 0.04f;
        if (punchyIntent) score -= 0.06f;
    } else if (id == "brightHighlightRolloff") {
        score =
            0.37f +
            stats.highlightPressure * 0.18f +
            hdrNeed * 0.10f +
            flatSceneNeed * 0.04f +
            regionEvidence.brightnessHierarchyRisk * 0.10f +
            regionEvidence.localHighlightHotspotRisk * 0.04f +
            SaturateFloat((0.018f - stats.clippingRatio) / 0.018f) * 0.06f -
            stats.clippingRatio * 0.75f;
        if (naturalIntent) score += 0.06f;
        if (brightIntent || punchyIntent) score += 0.09f;
        if (rangeIntent) score += 0.03f;
        if (darkIntent) score -= 0.03f;
    } else if (id == "luminousHighlightAnchor") {
        score =
            0.36f +
            highlightBrightnessSignal * 0.26f +
            regionEvidence.brightnessHierarchyRisk * 0.08f +
            regionEvidence.broadHighlightPressure * 0.06f +
            stats.textureConfidence * 0.05f -
            stats.clippingRatio * 0.70f -
            regionEvidence.localHaloRisk * 0.04f +
            mapHighlightBias * 0.05f +
            mapContrastBias * 0.03f;
        if (naturalIntent) score += 0.10f;
        if (brightIntent) score += 0.12f;
        if (punchyIntent) score += 0.08f;
        if (rangeIntent) score -= 0.06f;
        if (flatIntent) score -= 0.04f;
        if (darkIntent) score -= 0.03f;
    } else if (id == "specularHighlightTolerance") {
        score =
            0.35f +
            smallSpecularSignal * 0.24f +
            SaturateFloat((0.52f - stats.highlightPressure) / 0.52f) * 0.06f -
            hdrNeed * 0.06f -
            stats.clippingRatio * 0.90f -
            regionEvidence.broadHighlightPressure * 0.10f;
        if (naturalIntent) score += 0.08f;
        if (brightIntent || punchyIntent) score += 0.10f;
        if (rangeIntent || flatIntent) score -= 0.10f;
        if (darkIntent) score -= 0.03f;
    } else if (id == "tonePunchierShape") {
        score =
            0.36f +
            flatSceneNeed * 0.20f +
            regionEvidence.flatGrayRisk * 0.08f +
            stats.textureConfidence * 0.10f -
            stats.highlightPressure * 0.08f -
            hdrNeed * 0.04f +
            mapContrastBias * 0.06f;
        if (punchyIntent) score += 0.22f;
        if (naturalIntent) score += 0.04f;
        if (rangeIntent || flatIntent) score -= 0.08f;
    } else if (id == "toneFlatterEditing") {
        score =
            0.36f +
            hdrNeed * 0.18f +
            shadowRescueNeed * 0.08f +
            regionEvidence.localRangeConflict * 0.08f +
            stats.highlightPressure * 0.04f -
            stats.noiseRisk * 0.04f +
            mapRangeBias * 0.08f +
            subjectReadabilityBias * 0.05f;
        if (flatIntent) score += 0.20f;
        if (rangeIntent) score += 0.10f;
        if (punchyIntent) score -= 0.10f;
    } else if (id == "toneDarkerToe") {
        score =
            0.34f +
            darkness * 0.22f +
            regionEvidence.shadowNoiseLiftRisk * 0.10f +
            stats.noiseRisk * 0.08f +
            stats.highlightPressure * 0.04f +
            subjectMoodBias * 0.12f;
        if (darkIntent) score += 0.22f;
        if (cleanIntent) score += 0.05f;
        if (brightIntent) score -= 0.12f;
    } else if (id == "cleanShadows") {
        score =
            0.34f +
            stats.noiseRisk * 0.34f +
            regionEvidence.shadowNoiseLiftRisk * 0.12f +
            (1.0f - stats.textureConfidence) * 0.12f;
        if (cleanIntent) score += 0.22f;
        if (rangeIntent) score -= 0.05f;
    } else if (id == "preserveTexture") {
        score =
            0.35f +
            stats.textureConfidence * 0.26f +
            SaturateFloat(1.0f - regionEvidence.shadowNoiseLiftRisk) * 0.04f +
            SaturateFloat((0.78f - stats.noiseRisk) / 0.78f) * 0.10f +
            flatSceneNeed * 0.04f;
        if (naturalIntent) score += 0.05f;
        if (punchyIntent) score += 0.08f;
        if (rangeIntent) score += 0.04f;
        if (cleanIntent) score -= 0.08f;
    } else if (id == "localRangeGuard") {
        score =
            0.36f +
            regionEvidence.localRangeConflict * 0.24f +
            regionEvidence.localEvConflict * 0.16f +
            regionEvidence.localHaloRisk * 0.14f +
            regionEvidence.localHighlightHotspotRisk * 0.08f +
            regionEvidence.localShadowHotspotRisk * 0.06f -
            stats.noiseRisk * 0.04f +
            subjectPriorityBias * 0.07f +
            subjectReadabilityBias * 0.04f +
            subjectProtectionBias * 0.04f;
        if (rangeIntent || flatIntent) score += 0.08f;
        if (punchyIntent) score -= 0.03f;
    } else if (id == "haloSafeLocalRange") {
        score =
            0.36f +
            regionEvidence.localHaloRisk * 0.30f +
            regionEvidence.localRangeConflict * 0.14f +
            regionEvidence.localEvConflict * 0.08f +
            regionEvidence.localHighlightHotspotRisk * 0.08f +
            regionEvidence.localShadowHotspotRisk * 0.06f +
            hdrNeed * 0.04f -
            stats.noiseRisk * 0.03f +
            subjectPriorityBias * 0.04f;
        if (naturalIntent || cleanIntent) score += 0.08f;
        if (rangeIntent || flatIntent) score += 0.06f;
        if (punchyIntent) score -= 0.04f;
    } else if (id == "shadowNoiseFloor") {
        score =
            0.35f +
            stats.noiseRisk * 0.18f +
            regionEvidence.shadowNoiseLiftRisk * 0.24f +
            regionEvidence.localShadowHotspotRisk * 0.12f +
            darkness * 0.12f -
            shadowRescueNeed * 0.06f -
            stats.textureConfidence * 0.05f +
            subjectMoodBias * 0.10f -
            subjectReadabilityBias * 0.05f;
        if (darkIntent) score += 0.18f;
        if (naturalIntent || cleanIntent) score += 0.06f;
        if (brightIntent) score -= 0.12f;
        if (rangeIntent || flatIntent) score -= 0.08f;
    } else if (id == "wbDaylightCorrection") {
        // Daylight correction tests a plausible technical neutral without erasing all camera mood.
        score =
            0.40f +
            stats.highlightPressure * 0.04f +
            SaturateFloat((0.70f - stats.noiseRisk) / 0.70f) * 0.04f +
            hdrNeed * 0.03f;
        if (cleanIntent || flatIntent || rangeIntent) score += 0.08f;
        if (darkIntent) score -= 0.05f;
    } else if (id == "wbNeutralCorrection") {
        // Neutral correction is most useful for clean/editing bases where color accuracy beats mood.
        score =
            0.38f +
            SaturateFloat((0.72f - stats.noiseRisk) / 0.72f) * 0.05f +
            flatSceneNeed * 0.03f;
        if (cleanIntent || flatIntent) score += 0.11f;
        if (rangeIntent) score += 0.06f;
        if (darkIntent || punchyIntent) score -= 0.05f;
    } else if (id == "wbCameraMood") {
        // Camera mood keeps the recorded illuminant when a correction candidate looks too clinical.
        score =
            0.39f +
            darkness * 0.08f +
            stats.textureConfidence * 0.03f -
            stats.clippingRatio * 0.30f;
        if (naturalIntent || darkIntent) score += 0.08f;
        if (cleanIntent || flatIntent) score -= 0.05f;
    } else if (id == "modeNeighborNaturalMoreRange") {
        score =
            0.42f +
            hdrNeed * 0.22f +
            stats.highlightPressure * 0.06f +
            shadowRescueNeed * 0.04f -
            stats.noiseRisk * 0.05f;
        if (naturalIntent) score += 0.05f;
    } else if (id == "modeNeighborNaturalBrighterMids") {
        score =
            0.38f +
            shadowRescueNeed * 0.24f +
            SaturateFloat(underBrightBroadHighlightEv / 1.25f) * 0.12f -
            stats.highlightPressure * 0.10f -
            stats.noiseRisk * 0.06f +
            subjectReadabilityBias * 0.06f;
        if (naturalIntent || brightIntent) score += 0.05f;
        if (darkIntent) score -= 0.08f;
    } else if (id == "modeNeighborNaturalPunchier") {
        score =
            0.37f +
            flatSceneNeed * 0.18f +
            stats.textureConfidence * 0.06f -
            hdrNeed * 0.06f -
            stats.highlightPressure * 0.05f;
        if (naturalIntent || punchyIntent) score += 0.05f;
        if (rangeIntent || flatIntent) score -= 0.06f;
    } else if (id == "modeNeighborBrightHighlightSafe") {
        score =
            0.40f +
            stats.highlightPressure * 0.24f +
            hdrNeed * 0.10f -
            darkness * 0.04f;
        if (brightIntent) score += 0.08f;
    } else if (id == "modeNeighborDarkReadableMids") {
        score =
            0.39f +
            shadowRescueNeed * 0.20f +
            darkness * 0.12f -
            stats.noiseRisk * 0.06f +
            subjectReadabilityBias * 0.04f +
            subjectMoodBias * 0.04f;
        if (darkIntent) score += 0.10f;
    } else if (id == "modeNeighborPunchySaferRange") {
        score =
            0.39f +
            hdrNeed * 0.16f +
            stats.highlightPressure * 0.10f +
            flatSceneNeed * 0.04f;
        if (punchyIntent) score += 0.10f;
    } else if (id == "modeNeighborRangeNaturalShape") {
        score =
            0.40f +
            flatSceneNeed * 0.13f +
            stats.textureConfidence * 0.06f -
            stats.clippingRatio * 0.80f;
        if (rangeIntent) score += 0.10f;
    } else if (id == "modeNeighborFlatNaturalShape") {
        score =
            0.39f +
            flatSceneNeed * 0.14f +
            stats.textureConfidence * 0.05f -
            stats.highlightPressure * 0.04f;
        if (flatIntent) score += 0.10f;
    } else if (id == "modeNeighborCleanTextureCheck") {
        score =
            0.38f +
            stats.textureConfidence * 0.18f +
            SaturateFloat((0.72f - stats.noiseRisk) / 0.72f) * 0.08f;
        if (cleanIntent || naturalIntent) score += 0.06f;
    } else if (id == "renderedLocalBrightenMids") {
        score =
            0.55f +
            shadowRescueNeed * 0.16f +
            SaturateFloat(underBrightBroadHighlightEv / 1.25f) * 0.10f -
            stats.highlightPressure * 0.10f -
            stats.noiseRisk * 0.06f +
            subjectReadabilityBias * 0.08f;
        if (brightIntent || flatIntent) score += 0.06f;
        if (darkIntent) score -= 0.08f;
    } else if (id == "renderedLocalShadowOpening") {
        score =
            0.56f +
            shadowRescueNeed * 0.18f +
            darkness * 0.08f -
            stats.noiseRisk * 0.08f +
            subjectReadabilityBias * 0.10f;
        if (rangeIntent || flatIntent) score += 0.08f;
        if (darkIntent) score -= 0.05f;
    } else if (id == "renderedLocalHighlightRestraint") {
        score =
            0.56f +
            stats.highlightPressure * 0.16f +
            hdrNeed * 0.12f +
            stats.clippingRatio * 1.40f +
            subjectProtectionBias * 0.08f;
        if (rangeIntent || darkIntent) score += 0.06f;
        if (brightIntent) score -= 0.04f;
    } else if (id == "renderedLocalContrastShape") {
        score =
            0.54f +
            flatSceneNeed * 0.16f +
            (1.0f - SaturateFloat(stats.highlightPressure)) * 0.04f -
            hdrNeed * 0.04f;
        if (punchyIntent) score += 0.12f;
        if (rangeIntent || flatIntent) score -= 0.05f;
    } else if (id == "renderedLocalCleanShadows") {
        score =
            0.55f +
            stats.noiseRisk * 0.20f +
            shadowRescueNeed * 0.06f +
            (1.0f - stats.textureConfidence) * 0.08f;
        if (cleanIntent || darkIntent) score += 0.07f;
        if (rangeIntent) score -= 0.04f;
    } else if (id == "renderedLocalPreserveTexture") {
        score =
            0.54f +
            stats.textureConfidence * 0.18f +
            SaturateFloat((0.70f - stats.noiseRisk) / 0.70f) * 0.08f +
            flatSceneNeed * 0.04f;
        if (naturalIntent || punchyIntent) score += 0.06f;
        if (cleanIntent) score -= 0.06f;
    }
    return SaturateFloat(score);
}

std::string PreferredRenderedRefineCandidateId(const std::string& refineIntent) {
    if (refineIntent == "brightenMids") {
        return "renderedLocalBrightenMids";
    }
    if (refineIntent == "openShadows") {
        return "renderedLocalShadowOpening";
    }
    if (refineIntent == "protectHighlights") {
        return "renderedLocalHighlightRestraint";
    }
    if (refineIntent == "addContrast") {
        return "renderedLocalContrastShape";
    }
    if (refineIntent == "cleanShadows") {
        return "renderedLocalCleanShadows";
    }
    if (refineIntent == "preserveTexture") {
        return "renderedLocalPreserveTexture";
    }
    return {};
}

bool IsRenderedLocalRefineCandidateId(const std::string& candidateId) {
    return
        candidateId == "renderedLocalBrightenMids" ||
        candidateId == "renderedLocalShadowOpening" ||
        candidateId == "renderedLocalHighlightRestraint" ||
        candidateId == "renderedLocalContrastShape" ||
        candidateId == "renderedLocalCleanShadows" ||
        candidateId == "renderedLocalPreserveTexture";
}

bool IsDevelopCleanupProbeCandidateId(const std::string& candidateId) {
    return
        candidateId == "cleanShadows" ||
        candidateId == "preserveTexture" ||
        candidateId == "renderedLocalCleanShadows" ||
        candidateId == "renderedLocalPreserveTexture";
}

bool IsDevelopModeNeighborCandidateId(const std::string& candidateId) {
    return candidateId.rfind("modeNeighbor", 0) == 0;
}

bool IsActionableDevelopContinuationStage(const std::string& stage) {
    return
        stage == "rawGlobal" ||
        stage == "scenePrep" ||
        stage == "finishTone" ||
        stage == "rawCleanup" ||
        stage == "multiStage";
}

bool DevelopCandidateMatchesRenderedRefineIntent(
    const std::string& candidateId,
    const std::string& refineIntent) {
    if (refineIntent == "brightenMids") {
        return
            candidateId == "brighterMids" ||
            candidateId == "subjectReadableMids" ||
            candidateId == "renderedLocalBrightenMids" ||
            candidateId == "modeNeighborNaturalBrighterMids" ||
            candidateId == "modeNeighborDarkReadableMids";
    }
    if (refineIntent == "openShadows") {
        return
            candidateId == "maximumRange" ||
            candidateId == "haloSafeLocalRange" ||
            candidateId == "localRangeGuard" ||
            candidateId == "shadowReadabilityLift" ||
            candidateId == "shadowNoiseFloor" ||
            candidateId == "subjectReadableMids" ||
            candidateId == "sceneMoodPreservation" ||
            candidateId == "renderedLocalShadowOpening" ||
            candidateId == "modeNeighborNaturalMoreRange" ||
            candidateId == "modeNeighborDarkReadableMids";
    }
    if (refineIntent == "protectHighlights") {
        return
            candidateId == "protectHighlights" ||
            candidateId == "highlightProtectedMids" ||
            candidateId == "broadHighlightGuard" ||
            candidateId == "haloSafeLocalRange" ||
            candidateId == "localRangeGuard" ||
            candidateId == "renderedLocalHighlightRestraint" ||
            candidateId == "toneSofterRolloff" ||
            candidateId == "brightHighlightRolloff" ||
            candidateId == "luminousHighlightAnchor" ||
            candidateId == "specularHighlightTolerance" ||
            candidateId == "modeNeighborNaturalMoreRange" ||
            candidateId == "modeNeighborBrightHighlightSafe" ||
            candidateId == "modeNeighborPunchySaferRange";
    }
    if (refineIntent == "addContrast") {
        return
            candidateId == "strongerContrast" ||
            candidateId == "naturalContrastGuard" ||
            candidateId == "luminousHighlightAnchor" ||
            candidateId == "tonePunchierShape" ||
            candidateId == "renderedLocalContrastShape" ||
            candidateId == "modeNeighborNaturalPunchier" ||
            candidateId == "modeNeighborRangeNaturalShape";
    }
    if (refineIntent == "cleanShadows") {
        return candidateId == "cleanShadows" || candidateId == "renderedLocalCleanShadows" || candidateId == "shadowNoiseFloor";
    }
    if (refineIntent == "preserveTexture") {
        return candidateId == "preserveTexture" || candidateId == "renderedLocalPreserveTexture";
    }
    return false;
}

DevelopContinuationCandidateBiasProfile ResolveDevelopContinuationCandidateBiasProfile(
    const nlohmann::json& previousToneJson) {
    DevelopContinuationCandidateBiasProfile profile;
    const nlohmann::json continuationPolicy =
        previousToneJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    if (!continuationPolicy.is_object()) {
        return profile;
    }

    profile.decision = continuationPolicy.value("decision", std::string());
    if (profile.decision != "continue") {
        const bool carryPendingBias =
            profile.decision == "waitForRenderedMetrics" &&
            previousToneJson.value("autoCandidateContinuationBiasActive", false);
        if (!carryPendingBias) {
            return profile;
        }

        // The same bias must remain active while the biased solve is waiting
        // for rendered metrics, otherwise the ready metrics no longer match
        // the candidate-solve fingerprint they were rendered from.
        profile.decision = previousToneJson.value(
            "autoCandidateContinuationBiasDecision",
            std::string("continue"));
        profile.reason = previousToneJson.value(
            "autoCandidateContinuationBiasReason",
            std::string("responsibleStage"));
        profile.stageFocus = previousToneJson.value(
            "autoCandidateContinuationBiasStage",
            std::string());
        profile.refineIntent = previousToneJson.value(
            "autoCandidateContinuationBiasRefineIntent",
            std::string());
        if (!profile.refineIntent.empty() &&
            !IsActionableDevelopContinuationStage(profile.stageFocus)) {
            profile.stageFocus = DevelopRenderedRevisionStageForRefineIntent(profile.refineIntent);
        }
        profile.active = IsActionableDevelopContinuationStage(profile.stageFocus) ||
            !profile.refineIntent.empty();
        return profile;
    }

    profile.stageFocus = continuationPolicy.value(
        "stageFocus",
        previousToneJson.value("autoCandidateRenderedRevisionStage", std::string()));
    if (profile.stageFocus.empty()) {
        profile.stageFocus = previousToneJson.value("autoCandidateRenderedRevisionStage", std::string());
    }
    profile.refineIntent =
        previousToneJson.value("autoCandidateRenderedRefineIntent", std::string());
    if (profile.refineIntent.empty()) {
        profile.refineIntent =
            previousToneJson.value("autoCandidateRenderedFeedbackRefineIntent", std::string());
    }
    if (!profile.refineIntent.empty() &&
        !IsActionableDevelopContinuationStage(profile.stageFocus)) {
        profile.stageFocus = DevelopRenderedRevisionStageForRefineIntent(profile.refineIntent);
    }

    if (!IsActionableDevelopContinuationStage(profile.stageFocus) &&
        profile.refineIntent.empty()) {
        return profile;
    }

    profile.reason = !profile.refineIntent.empty()
        ? std::string("activeRefineIntent")
        : std::string("responsibleStage");
    profile.active = true;
    return profile;
}

float DevelopContinuationCandidateBiasBonus(
    const DevelopContinuationCandidateBiasProfile& profile,
    const std::string& candidateId) {
    if (!profile.active || candidateId == "base") {
        return 0.0f;
    }

    float bonus = 0.0f;
    const std::string preferredRefineCandidate =
        PreferredRenderedRefineCandidateId(profile.refineIntent);
    if (!preferredRefineCandidate.empty() && candidateId == preferredRefineCandidate) {
        // A rendered-local candidate directly answers a measured mismatch from
        // the previous render pass, so it gets the strongest small nudge.
        bonus += 0.090f;
    } else if (!profile.refineIntent.empty() &&
        DevelopCandidateMatchesRenderedRefineIntent(candidateId, profile.refineIntent)) {
        // Generic parameter families can still answer the same visual intent
        // when the exact rendered-local family is unavailable or clustered.
        bonus += 0.060f;
    }

    const std::string candidateStage = DevelopRenderedRevisionStageForCandidateId(candidateId);
    if (IsActionableDevelopContinuationStage(profile.stageFocus)) {
        if (candidateStage == profile.stageFocus) {
            bonus += 0.050f;
        } else if (profile.stageFocus == "multiStage" &&
            candidateStage != "multiStage") {
            bonus += 0.025f;
        }
    }

    return std::min(0.120f, bonus);
}

bool ApplyDevelopContinuationCandidateBias(
    DevelopAutoCandidateSolve& candidate,
    const DevelopContinuationCandidateBiasProfile& profile) {
    const float bonus = DevelopContinuationCandidateBiasBonus(profile, candidate.id);
    if (bonus <= 0.0f) {
        return false;
    }

    candidate.continuationBiasActive = true;
    candidate.continuationBiasBonus = bonus;
    candidate.continuationBiasReason = profile.reason;
    candidate.continuationBiasStage = profile.stageFocus;
    candidate.continuationBiasRefineIntent = profile.refineIntent;
    candidate.score = SaturateFloat(candidate.score + bonus);
    return true;
}

float DevelopAutoCandidateModeIntentFit(
    const std::string& candidateId,
    EditorNodeGraph::DevelopAutoIntent intent) {
    const bool naturalIntent = intent == EditorNodeGraph::DevelopAutoIntent::NaturalFinished;
    const bool cleanIntent = intent == EditorNodeGraph::DevelopAutoIntent::CleanBase;
    const bool flatIntent = intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
    const bool brightIntent = intent == EditorNodeGraph::DevelopAutoIntent::BrightNatural;
    const bool darkIntent = intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural;
    const bool punchyIntent = intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;
    const bool rangeIntent = intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;

    float fit = 0.48f;
    if (candidateId == "base") {
        fit = naturalIntent ? 0.72f : 0.60f;
    } else if (candidateId == "protectHighlights" || candidateId == "renderedLocalHighlightRestraint") {
        fit = 0.56f + (rangeIntent ? 0.20f : 0.0f) + (darkIntent ? 0.08f : 0.0f) - (brightIntent ? 0.06f : 0.0f);
    } else if (candidateId == "highlightProtectedMids") {
        fit = 0.57f + (rangeIntent ? 0.18f : 0.0f) + (flatIntent ? 0.10f : 0.0f) + (darkIntent ? 0.06f : 0.0f) - (brightIntent ? 0.05f : 0.0f);
    } else if (candidateId == "broadHighlightGuard") {
        fit = 0.54f + (rangeIntent ? 0.18f : 0.0f) + (flatIntent ? 0.10f : 0.0f) + (cleanIntent ? 0.07f : 0.0f) + (naturalIntent ? 0.05f : 0.0f) - (punchyIntent ? 0.06f : 0.0f) - (brightIntent ? 0.03f : 0.0f);
    } else if (candidateId == "brighterMids" || candidateId == "renderedLocalBrightenMids") {
        fit = 0.52f + (brightIntent ? 0.22f : 0.0f) + (flatIntent ? 0.10f : 0.0f) - (darkIntent ? 0.12f : 0.0f);
    } else if (candidateId == "maximumRange") {
        fit = 0.54f + (rangeIntent ? 0.24f : 0.0f) + (flatIntent ? 0.12f : 0.0f) - (punchyIntent ? 0.08f : 0.0f);
    } else if (candidateId == "preserveMood") {
        fit = 0.52f + (darkIntent ? 0.24f : 0.0f) + (naturalIntent ? 0.08f : 0.0f) - (brightIntent ? 0.12f : 0.0f);
    } else if (candidateId == "naturalContrastGuard") {
        fit = 0.52f + (naturalIntent ? 0.14f : 0.0f) + (punchyIntent ? 0.16f : 0.0f) + (darkIntent ? 0.06f : 0.0f) + (brightIntent ? 0.04f : 0.0f) - (rangeIntent ? 0.08f : 0.0f) - (flatIntent ? 0.10f : 0.0f);
    } else if (candidateId == "strongerContrast" || candidateId == "renderedLocalContrastShape") {
        fit = 0.50f + (punchyIntent ? 0.26f : 0.0f) + (naturalIntent ? 0.08f : 0.0f) - ((rangeIntent || flatIntent) ? 0.08f : 0.0f);
    } else if (candidateId == "toneSofterRolloff") {
        fit = 0.52f + (rangeIntent ? 0.16f : 0.0f) + (flatIntent ? 0.10f : 0.0f) + (brightIntent ? 0.06f : 0.0f) - (punchyIntent ? 0.08f : 0.0f);
    } else if (candidateId == "brightHighlightRolloff") {
        fit = 0.54f + (naturalIntent ? 0.10f : 0.0f) + (brightIntent ? 0.14f : 0.0f) + (punchyIntent ? 0.10f : 0.0f) + (rangeIntent ? 0.04f : 0.0f) - (darkIntent ? 0.04f : 0.0f);
    } else if (candidateId == "luminousHighlightAnchor") {
        fit = 0.53f + (naturalIntent ? 0.12f : 0.0f) + (brightIntent ? 0.16f : 0.0f) + (punchyIntent ? 0.10f : 0.0f) - (rangeIntent ? 0.06f : 0.0f) - (flatIntent ? 0.04f : 0.0f) - (darkIntent ? 0.04f : 0.0f);
    } else if (candidateId == "specularHighlightTolerance") {
        fit = 0.52f + (naturalIntent ? 0.12f : 0.0f) + (brightIntent ? 0.14f : 0.0f) + (punchyIntent ? 0.14f : 0.0f) - ((rangeIntent || flatIntent) ? 0.10f : 0.0f) - (darkIntent ? 0.04f : 0.0f);
    } else if (candidateId == "tonePunchierShape") {
        fit = 0.50f + (punchyIntent ? 0.24f : 0.0f) + (naturalIntent ? 0.08f : 0.0f) - ((rangeIntent || flatIntent) ? 0.10f : 0.0f);
    } else if (candidateId == "toneFlatterEditing") {
        fit = 0.50f + (flatIntent ? 0.28f : 0.0f) + (rangeIntent ? 0.12f : 0.0f) + (cleanIntent ? 0.06f : 0.0f) - (punchyIntent ? 0.12f : 0.0f);
    } else if (candidateId == "toneDarkerToe") {
        fit = 0.50f + (darkIntent ? 0.26f : 0.0f) + (naturalIntent ? 0.06f : 0.0f) + (cleanIntent ? 0.04f : 0.0f) - (brightIntent ? 0.12f : 0.0f);
    } else if (candidateId == "cleanShadows" || candidateId == "renderedLocalCleanShadows") {
        fit = 0.50f + (cleanIntent ? 0.26f : 0.0f) + (darkIntent ? 0.08f : 0.0f) - (rangeIntent ? 0.06f : 0.0f);
    } else if (candidateId == "preserveTexture" || candidateId == "renderedLocalPreserveTexture") {
        fit = 0.52f + (naturalIntent ? 0.10f : 0.0f) + (punchyIntent ? 0.10f : 0.0f) + (rangeIntent ? 0.06f : 0.0f) - (cleanIntent ? 0.08f : 0.0f);
    } else if (candidateId == "wbDaylightCorrection") {
        fit = 0.52f + (cleanIntent ? 0.12f : 0.0f) + (flatIntent ? 0.10f : 0.0f) + (rangeIntent ? 0.08f : 0.0f) - (darkIntent ? 0.06f : 0.0f);
    } else if (candidateId == "wbNeutralCorrection") {
        fit = 0.50f + (cleanIntent ? 0.18f : 0.0f) + (flatIntent ? 0.14f : 0.0f) + (rangeIntent ? 0.06f : 0.0f) - ((darkIntent || punchyIntent) ? 0.06f : 0.0f);
    } else if (candidateId == "wbCameraMood") {
        fit = 0.52f + (naturalIntent ? 0.10f : 0.0f) + (darkIntent ? 0.16f : 0.0f) - ((cleanIntent || flatIntent) ? 0.06f : 0.0f);
    } else if (candidateId == "localRangeGuard") {
        fit = 0.54f + (rangeIntent ? 0.12f : 0.0f) + (flatIntent ? 0.08f : 0.0f) + (naturalIntent ? 0.06f : 0.0f) - (punchyIntent ? 0.04f : 0.0f);
    } else if (candidateId == "haloSafeLocalRange") {
        fit = 0.54f + (naturalIntent ? 0.08f : 0.0f) + (cleanIntent ? 0.08f : 0.0f) + (rangeIntent ? 0.08f : 0.0f) + (flatIntent ? 0.06f : 0.0f) - (punchyIntent ? 0.05f : 0.0f);
    } else if (candidateId == "shadowReadabilityLift") {
        fit = 0.53f + (brightIntent ? 0.14f : 0.0f) + (flatIntent ? 0.12f : 0.0f) + (rangeIntent ? 0.10f : 0.0f) + (naturalIntent ? 0.07f : 0.0f) - (darkIntent ? 0.08f : 0.0f) - (punchyIntent ? 0.04f : 0.0f);
    } else if (candidateId == "subjectReadableMids") {
        fit = 0.54f + (brightIntent ? 0.14f : 0.0f) + (flatIntent ? 0.12f : 0.0f) + (rangeIntent ? 0.08f : 0.0f) + (naturalIntent ? 0.08f : 0.0f) - (darkIntent ? 0.06f : 0.0f);
    } else if (candidateId == "sceneMoodPreservation") {
        fit = 0.53f + (darkIntent ? 0.18f : 0.0f) + (naturalIntent ? 0.08f : 0.0f) + (cleanIntent ? 0.06f : 0.0f) - (brightIntent ? 0.10f : 0.0f) - ((rangeIntent || flatIntent) ? 0.06f : 0.0f);
    } else if (candidateId == "shadowNoiseFloor") {
        fit = 0.52f + (darkIntent ? 0.20f : 0.0f) + (naturalIntent ? 0.08f : 0.0f) + (cleanIntent ? 0.08f : 0.0f) - (brightIntent ? 0.12f : 0.0f) - ((rangeIntent || flatIntent) ? 0.08f : 0.0f);
    } else if (IsDevelopModeNeighborCandidateId(candidateId)) {
        fit = 0.62f;
    } else if (IsRenderedLocalRefineCandidateId(candidateId)) {
        fit = 0.58f;
    } else if (candidateId == "mergedAutoPick" ||
        candidateId == "renderedFeedbackMerge" ||
        candidateId == "renderedFeedbackPairMerge" ||
        candidateId == "renderedFeedbackEnsembleMerge") {
        fit = 0.64f;
    }
    return SaturateFloat(fit);
}

float DevelopAutoCandidateNearestSurvivorDistance(
    const DevelopAutoCandidateSolveResult& result,
    std::size_t candidateIndex) {
    if (candidateIndex >= result.candidates.size()) {
        return 0.0f;
    }
    const DevelopAutoCandidateSolve& candidate = result.candidates[candidateIndex];
    bool compared = false;
    float nearest = 4.0f;
    for (std::size_t i = 0; i < result.candidates.size(); ++i) {
        if (i == candidateIndex || result.candidates[i].rejected) {
            continue;
        }
        compared = true;
        nearest = std::min(
            nearest,
            DevelopAutoCandidateDistance(candidate.guidance, result.candidates[i].guidance));
    }
    return compared ? nearest : 1.0f;
}

bool RejectDevelopAutoCandidateForDamage(
    DevelopAutoCandidateSolve& candidate,
    const EditorNodeGraph::DevelopAutoGuidance& base,
    const DevelopToneAutoStats& stats,
    EditorNodeGraph::DevelopAutoIntent intent) {
    if (candidate.id == "base" || !stats.valid) {
        return false;
    }

    const float exposureDelta = candidate.guidance.exposureBias - base.exposureBias;
    const float shadowDelta = candidate.guidance.shadowLift - base.shadowLift;
    const float rangeDelta = candidate.guidance.dynamicRange - base.dynamicRange;
    const float contrastDelta = candidate.guidance.contrastBias - base.contrastBias;
    const float highlightGuardDelta = candidate.guidance.highlightGuard - base.highlightGuard;

    const float highlightDamageRisk =
        stats.highlightPressure +
        stats.clippingRatio * 3.0f +
        std::max(0.0f, exposureDelta) * 0.42f -
        std::max(0.0f, highlightGuardDelta) * 0.30f -
        std::max(0.0f, rangeDelta) * 0.08f;
    if (highlightDamageRisk > 1.10f) {
        candidate.rejected = true;
        candidate.rejectReason = "Rejected because the brighter placement risks new broad highlight damage.";
        return true;
    }

    const float shadowNoiseRisk =
        stats.noiseRisk +
        std::max(0.0f, shadowDelta) * 0.42f +
        std::max(0.0f, rangeDelta) * 0.12f -
        std::max(0.0f, -shadowDelta) * 0.12f;
    if (shadowNoiseRisk > 1.12f) {
        candidate.rejected = true;
        candidate.rejectReason = "Rejected because extra shadow lift is likely to reveal noisy shadows.";
        return true;
    }

    const bool flatteningAllowed =
        intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase ||
        intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;
    const float flatteningRisk =
        std::max(0.0f, rangeDelta) * 0.22f +
        std::max(0.0f, -contrastDelta) * 0.22f +
        stats.hdrSpreadEv * 0.01f -
        (flatteningAllowed ? 0.18f : 0.0f);
    if (flatteningRisk > 0.44f && candidate.score < 0.54f) {
        candidate.rejected = true;
        candidate.rejectReason = "Rejected because the range move is likely to look washed out for this intent.";
        return true;
    }

    return false;
}

std::uint64_t BuildDevelopAutoCandidateFingerprint(
    const DevelopAutoCandidateSolveResult& result,
    const DevelopToneAutoStats& stats) {
    std::uint64_t hash = 1469598103934665603ull;
    auto addValue = [&](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    for (unsigned char ch : result.selectedId) {
        addValue(static_cast<std::uint64_t>(ch));
    }
    auto addFloat = [&](float value, float scale) {
        const int quantized = static_cast<int>(std::lround(value * scale));
        addValue(static_cast<std::uint64_t>(static_cast<std::int64_t>(quantized)));
    };
    addFloat(result.authoredGuidance.autoStrength, 100.0f);
    addFloat(result.authoredGuidance.exposureBias, 100.0f);
    addFloat(result.authoredGuidance.dynamicRange, 100.0f);
    addFloat(result.authoredGuidance.shadowLift, 100.0f);
    addFloat(result.authoredGuidance.highlightGuard, 100.0f);
    addFloat(result.authoredGuidance.highlightCharacter, 100.0f);
    addFloat(result.authoredGuidance.contrastBias, 100.0f);
    addFloat(result.authoredGuidance.subjectSceneBias, 100.0f);
    addFloat(result.authoredGuidance.moodReadabilityBias, 100.0f);
    addValue(result.authoredWhiteBalanceProbe ? 1ull : 0ull);
    addValue(static_cast<std::uint64_t>(result.authoredWhiteBalanceMode));
    addValue(stats.valid ? 1ull : 0ull);
    addValue(static_cast<std::uint64_t>(std::max(0, stats.sceneProfile)));
    addFloat(stats.highlightPressure, 100.0f);
    addFloat(stats.noiseRisk, 100.0f);
    addFloat(stats.hdrSpreadEv, 20.0f);
    return hash;
}

} // namespace Stack::Editor::DevelopCandidateScoring
