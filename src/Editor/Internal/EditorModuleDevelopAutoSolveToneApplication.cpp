#include "Editor/Internal/EditorModuleDevelopAutoSolveApplicationContext.h"

#include "Editor/Internal/EditorModuleDevelopAutoSolveDiagnostics.h"
#include "Editor/Internal/EditorModuleDevelopDynamicRangeStrategy.h"
#include "Editor/Internal/EditorModuleDevelopSubjectImportance.h"

#include <cmath>
#include <cstdint>
#include <string>

using namespace Stack::Editor::DevelopAutoSolveDiagnostics;
using namespace Stack::Editor::DevelopSubjectImportance;

namespace Stack::Editor::DevelopAutoSolveApplication {

namespace {

void ApplyDevelopToneGuidanceToJson(
    nlohmann::json& toneJson,
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    EditorNodeGraph::DevelopAutoIntent intent) {
    toneJson["autoIntent"] = EditorNodeGraph::DevelopAutoIntentStableString(intent);
    toneJson["autoSceneAssistStrength"] = guidance.autoStrength;
    toneJson["autoBrightnessIntent"] = guidance.exposureBias;
    toneJson["autoRawExposurePreferenceEv"] = guidance.exposureBias * 2.0f;
    toneJson["autoDynamicRange"] = guidance.dynamicRange;
    toneJson["autoShadowBias"] = guidance.shadowLift;
    toneJson["autoHighlightBias"] = guidance.highlightGuard;
    toneJson["autoHighlightCharacter"] = guidance.highlightCharacter;
    toneJson["autoContrastBias"] = guidance.contrastBias;
    toneJson["autoSubjectSceneBias"] = guidance.subjectSceneBias;
    toneJson["autoMoodReadabilityBias"] = guidance.moodReadabilityBias;
}

} // namespace

void WriteDevelopSelectedSolveTonePreamble(
    nlohmann::json& toneJson,
    const EditorNodeGraph::RawDevelopPayload& payload,
    const DevelopCandidateScoring::DevelopAutoCandidateSolveResult& candidateSolve,
    const EditorNodeGraph::DevelopAutoGuidance& modeGuidance) {
    const EditorNodeGraph::DevelopAutoGuidance& solveGuidance = candidateSolve.authoredGuidance;
    ApplyDevelopToneGuidanceToJson(toneJson, solveGuidance, payload.autoGuidance.intent);
    toneJson["autoRequestedSceneAssistStrength"] = payload.autoGuidance.autoStrength;
    toneJson["autoRequestedBrightnessIntent"] = payload.autoGuidance.exposureBias;
    toneJson["autoRequestedRawExposurePreferenceEv"] = payload.autoGuidance.exposureBias * 2.0f;
    toneJson["autoRequestedDynamicRange"] = payload.autoGuidance.dynamicRange;
    toneJson["autoRequestedShadowBias"] = payload.autoGuidance.shadowLift;
    toneJson["autoRequestedHighlightBias"] = payload.autoGuidance.highlightGuard;
    toneJson["autoRequestedHighlightCharacter"] = payload.autoGuidance.highlightCharacter;
    toneJson["autoRequestedContrastBias"] = payload.autoGuidance.contrastBias;
    toneJson["autoRequestedSubjectSceneBias"] = payload.autoGuidance.subjectSceneBias;
    toneJson["autoRequestedMoodReadabilityBias"] = payload.autoGuidance.moodReadabilityBias;
    const DevelopSubjectImportanceSummary requestedImportance =
        SummarizeDevelopSubjectImportance(payload.subjectImportance);
    toneJson["autoRequestedSubjectImportanceEnabled"] = payload.subjectImportance.enabled;
    toneJson["autoRequestedSubjectImportanceRegionCount"] =
        requestedImportance.activeRegionCount;
    toneJson["autoRequestedSubjectImportanceStrokeCount"] =
        requestedImportance.activeStrokeCount;
    toneJson["autoRequestedSubjectImportanceStrength"] = requestedImportance.strength;
    const DevelopSubjectImportanceInterpretation requestedImportanceMap =
        InterpretDevelopSubjectImportanceMap(payload.subjectImportance);
    toneJson["autoRequestedSubjectImportanceMapVersion"] =
        kDevelopSubjectImportanceMapVersion;
    toneJson["autoRequestedSubjectImportanceMapStatus"] =
        requestedImportanceMap.status;
    toneJson["autoRequestedSubjectImportanceMapCoverage"] =
        requestedImportanceMap.coverage;
    toneJson["autoRequestedSubjectImportanceMapLowPriorityCoverage"] =
        requestedImportanceMap.lowPriorityCoverage;
    toneJson["autoRequestedSubjectImportanceMapConfidence"] =
        requestedImportanceMap.mapConfidence;
    WriteDevelopAutoCandidateSolveDiagnostics(toneJson, candidateSolve, modeGuidance);
}

void WriteDevelopSelectedSolveToneDiagnostics(
    nlohmann::json& toneJson,
    const Raw::RawMetadata& metadata,
    const EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawDetailFusionSettings& prepSettings,
    const DevelopDynamicRange::DevelopToneAutoStats& stats,
    const DevelopCandidateScoring::DevelopAutoCandidateSolveResult& candidateSolve,
    const DevelopSelectedAutoSolveContext& context) {
    toneJson["autoExposureDiagnosticStatsValid"] = stats.valid;
    toneJson["autoAuthoredRawExposureEv"] = payload.settings.exposureStops;
    toneJson["autoAuthoredRawExposureScale"] = std::exp2(payload.settings.exposureStops);
    toneJson["autoAuthoredWhiteBalanceProbe"] = candidateSolve.authoredWhiteBalanceProbe;
    toneJson["autoAuthoredWhiteBalanceMode"] = Raw::WhiteBalanceModeName(payload.settings.whiteBalanceMode);
    toneJson["autoAuthoredLocalMinEvBias"] = prepSettings.minEvBias;
    toneJson["autoAuthoredLocalMaxEvBias"] = prepSettings.maxEvBias;
    toneJson["autoAuthoredLocalExposureStrategyVersion"] =
        context.localExposureStrategy.value(
            "version",
            std::string(DevelopDynamicRange::kDevelopLocalExposureStrategyVersion));
    toneJson["autoAuthoredLocalExposureStrategyId"] =
        context.localExposureStrategy.value("id", std::string("balancedLocalPrep"));
    toneJson["autoAuthoredLocalExposureStrategyLabel"] =
        context.localExposureStrategy.value("label", std::string("Balanced Local Prep"));
    toneJson["autoAuthoredLocalExposureRangeRedistribution"] =
        context.localExposureRangeRedistribution;
    toneJson["autoAuthoredLocalExposureHighlightCompression"] =
        context.localExposureHighlightCompression;
    toneJson["autoAuthoredLocalExposureShadowOpening"] =
        context.localExposureShadowOpening;
    toneJson["autoAuthoredLocalExposureNoiseGuard"] =
        context.localExposureNoiseGuard;
    toneJson["autoAuthoredLocalExposureHaloGuard"] =
        context.localExposureHaloGuard;
    toneJson["autoAuthoredLocalExposureTextureGuard"] =
        context.localExposureTextureGuard;
    toneJson["autoAuthoredLocalExposureShadowEvBudget"] =
        context.localExposureShadowEvBudget;
    toneJson["autoAuthoredLocalExposureHighlightEvBudget"] =
        context.localExposureHighlightEvBudget;
    toneJson["autoAuthoredLocalExposureStrengthTarget"] =
        context.localExposureStrengthTarget;
    toneJson["autoExposureDiagnosticClippingRatio"] = stats.clippingRatio;
    toneJson["autoExposureDiagnosticHighlightPressure"] = stats.highlightPressure;
    toneJson["autoExposureDiagnosticNoiseRisk"] = stats.noiseRisk;
    toneJson["autoExposureDiagnosticHdrSpreadEv"] = stats.hdrSpreadEv;
    toneJson["autoExposureDiagnosticRecommendedBaseEv"] = stats.recommendedBaseEv;
    WriteDevelopAutoStageSolveDiagnostics(
        toneJson,
        metadata,
        payload.settings,
        prepSettings,
        candidateSolve.authoredGuidance,
        candidateSolve,
        stats);
}

void QueueDevelopToneCalibration(nlohmann::json& toneJson) {
    const std::uint64_t requestId = toneJson.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0));
    toneJson["autoCalibratePending"] = true;
    toneJson["autoCalibrateVariant"] = 0;
    toneJson["autoCalibrateRequestId"] = requestId + 1;
}

} // namespace Stack::Editor::DevelopAutoSolveApplication
