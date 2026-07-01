#pragma once

#include "Editor/EditorRenderWorker.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "ThirdParty/json.hpp"

#include <string>

namespace Stack::Editor::DevelopDynamicRange {

inline constexpr const char* kDevelopDynamicRangeStrategyVersion = "DynamicRangeStrategyV1";
inline constexpr const char* kDevelopDynamicRangeStrategyMapVersion = "DynamicRangeStrategyMapV1";
inline constexpr const char* kDevelopLocalExposureStrategyVersion = "LocalExposureStrategyV1";
inline constexpr const char* kDevelopDynamicRangeRegionEvidenceVersion =
    "DynamicRangeRegionEvidenceV1";

struct DevelopToneAutoStats {
    bool valid = false;
    float shadowPercentile = 0.02f;
    float midtonePercentile = 0.18f;
    float highlightPercentile = 0.85f;
    float clippingRatio = 0.0f;
    float noiseRisk = 0.0f;
    float highlightPressure = 0.0f;
    float textureConfidence = 0.5f;
    float hdrSpreadEv = 0.0f;
    float recommendedBaseEv = 0.0f;
    float recommendedLocalStrength = 1.05f;
    float recommendedShadowOpening = 1.20f;
    float recommendedHighlightCompression = 1.25f;
    int sceneProfile = 0;
};

struct DevelopDynamicRangeRegionEvidence {
    bool valid = false;
    std::string source = "unavailable";
    std::string candidateId;
    float renderScore = -1.0f;
    float localHighlightPressure = 0.0f;
    float localShadowPressure = 0.0f;
    float localDamageRiskMean = 0.0f;
    float localDamageRiskPeak = 0.0f;
    float localLumaSpread = 0.0f;
    float localEvSpreadStops = 0.0f;
    float localEvConflict = 0.0f;
    float localContrastPeak = 0.0f;
    float centerShadowFraction = 0.0f;
    float centerHighlightFraction = 0.0f;
    float clippedFraction = 0.0f;
    float highlightFraction = 0.0f;
    float shadowFraction = 0.0f;
    float shadowTextureRisk = 0.0f;
    float haloRiskFraction = 0.0f;
    float lowSaturationFraction = 0.0f;
    float highlightBandFraction = 0.0f;
    float highlightMeanLuma = 0.0f;
    float highlightLowSaturationFraction = 0.0f;
    float highlightGrayRisk = 0.0f;
    float highlightTileCoverage = 0.0f;
    float highlightStructureScore = 0.0f;
    float meaningfulHighlightPressure = 0.0f;
    float contrastSpan = 0.0f;
    int peakTile = -1;
    float broadHighlightPressure = 0.0f;
    float localHighlightHotspotRisk = 0.0f;
    float localShadowHotspotRisk = 0.0f;
    float shadowNoiseLiftRisk = 0.0f;
    float localHaloRisk = 0.0f;
    float flatGrayRisk = 0.0f;
    float localRangeConflict = 0.0f;
    float brightnessHierarchyRisk = 0.0f;
    float localExposureHighlightCrowding = 0.0f;
    float localExposureShadowCrowding = 0.0f;
    float localExposureHaloStress = 0.0f;
    float localExposureFlatnessRisk = 0.0f;
    float localExposureDamageRisk = 0.0f;
    float subjectCenterPrior = 0.0f;
    float subjectReadabilityPressure = 0.0f;
    float subjectProtectionPressure = 0.0f;
    float subjectMoodPreservationPressure = 0.0f;
    float subjectImportanceConfidence = 0.0f;
    bool smallSpecularLikely = false;
};

struct DevelopDynamicRangeStrategy {
    std::string id = "balancedRange";
    std::string label = "Balanced Range";
    std::string reason =
        "The selected intent can use the balanced RAW, Scene Prep, and finish-tone range strategy.";
    std::string highlightPolicy =
        "Preserve meaningful highlights while allowing normal tiny specular clipping.";
    std::string shadowPolicy =
        "Open meaningful shadows only when noise and mode intent allow it.";
    std::string strategyMapReason =
        "Internal solver coordinates are balanced until rendered analysis has enough evidence.";
    std::string localExposureStrategyId = "balancedLocalPrep";
    std::string localExposureStrategyLabel = "Balanced Local Prep";
    std::string localExposureStrategyReason =
        "Use moderate local exposure shaping with existing halo, noise, and texture guardrails.";
    float highlightImportance = 0.0f;
    float shadowReadability = 0.0f;
    float noiseConstraint = 0.0f;
    float rangeCompression = 0.0f;
    float brightnessHierarchyRisk = 0.0f;
    float meaningfulHighlightPressure = 0.0f;
    float naturalContrastGuardNeed = 0.0f;
    float brightHighlightRolloffNeed = 0.0f;
    float highlightBrightnessAnchorNeed = 0.0f;
    float broadHighlightGuardNeed = 0.0f;
    float specularHighlightToleranceNeed = 0.0f;
    float shadowReadabilityLiftNeed = 0.0f;
    float shadowNoiseFloorNeed = 0.0f;
    float localHighlightHotspotRisk = 0.0f;
    float localShadowHotspotRisk = 0.0f;
    float localRangeConflict = 0.0f;
    float localEvConflict = 0.0f;
    float localHaloRisk = 0.0f;
    float localHaloGuardNeed = 0.0f;
    float flatGrayRisk = 0.0f;
    float highlightGrayRisk = 0.0f;
    float strategyMapHighlightShadowAxis = 0.0f;
    float strategyMapContrastRangeAxis = 0.0f;
    float strategyMapHighlightPriority = 0.5f;
    float strategyMapShadowVisibility = 0.5f;
    float strategyMapNaturalContrast = 0.5f;
    float strategyMapVisibleRange = 0.5f;
    float localExposureRangeRedistribution = 0.0f;
    float localExposureHighlightCompression = 0.0f;
    float localExposureShadowOpening = 0.0f;
    float localExposureNoiseGuard = 0.0f;
    float localExposureHaloGuard = 0.0f;
    float localExposureTextureGuard = 0.0f;
    float localExposureShadowEvBudget = 0.0f;
    float localExposureHighlightEvBudget = 0.0f;
    float localExposureStrengthTarget = 0.5f;
    float localExposureHighlightCrowding = 0.0f;
    float localExposureShadowCrowding = 0.0f;
    float localExposureHaloStress = 0.0f;
    float localExposureFlatnessRisk = 0.0f;
    float localExposureDamageRisk = 0.0f;
    bool smallSpecularClippingAllowed = false;
    nlohmann::json regionEvidence = nlohmann::json::object();
};

DevelopToneAutoStats ReadDevelopToneAutoStats(const nlohmann::json& toneJson);

DevelopDynamicRangeRegionEvidence BuildDevelopDynamicRangeRegionEvidenceFromMetrics(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
    const std::string& source,
    const std::string& candidateId,
    float renderScore);

nlohmann::json DevelopDynamicRangeRegionEvidenceToJson(
    const DevelopDynamicRangeRegionEvidence& evidence);

DevelopDynamicRangeStrategy ResolveDevelopDynamicRangeStrategy(
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    const DevelopToneAutoStats& stats,
    const DevelopDynamicRangeRegionEvidence& regionEvidence,
    float darkness,
    float shadowRescueNeed,
    float hdrNeed,
    float flatSceneNeed,
    float tinySpecularAllowance);

nlohmann::json DevelopDynamicRangeStrategyToJson(const DevelopDynamicRangeStrategy& strategy);

} // namespace Stack::Editor::DevelopDynamicRange
