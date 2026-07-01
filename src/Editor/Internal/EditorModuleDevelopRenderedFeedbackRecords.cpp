#include "Editor/Internal/EditorModuleDevelopRenderedFeedbackRecords.h"

#include "Editor/Internal/EditorModuleDevelopRenderedFeedbackConvergence.h"

#include <algorithm>
#include <functional>
#include <utility>

namespace Stack::Editor::DevelopRenderedFeedbackRecords {

std::size_t HashDevelopRenderedFeedbackJsonValue(const nlohmann::json& value) {
    return std::hash<std::string>{}(value.dump());
}

nlohmann::json DevelopCandidateRenderMetricsToJson(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics) {
    return {
        { "meanLuma", metrics.meanLuma },
        { "medianLuma", metrics.medianLuma },
        { "p10Luma", metrics.p10Luma },
        { "p90Luma", metrics.p90Luma },
        { "shadowFraction", metrics.shadowFraction },
        { "highlightFraction", metrics.highlightFraction },
        { "clippedFraction", metrics.clippedFraction },
        { "contrastSpan", metrics.contrastSpan },
        { "meanRed", metrics.meanRed },
        { "meanGreen", metrics.meanGreen },
        { "meanBlue", metrics.meanBlue },
        { "warmCoolBias", metrics.warmCoolBias },
        { "magentaGreenBias", metrics.magentaGreenBias },
        { "channelImbalance", metrics.channelImbalance },
        { "colorCastRisk", metrics.colorCastRisk },
        { "meanSaturation", metrics.meanSaturation },
        { "lowSaturationFraction", metrics.lowSaturationFraction },
        { "highlightBandFraction", metrics.highlightBandFraction },
        { "highlightMeanLuma", metrics.highlightMeanLuma },
        { "highlightLowSaturationFraction", metrics.highlightLowSaturationFraction },
        { "highlightGrayRisk", metrics.highlightGrayRisk },
        { "highlightTileCoverage", metrics.highlightTileCoverage },
        { "highlightStructureScore", metrics.highlightStructureScore },
        { "meaningfulHighlightPressure", metrics.meaningfulHighlightPressure },
        { "edgeContrast", metrics.edgeContrast },
        { "haloRiskFraction", metrics.haloRiskFraction },
        { "shadowTextureRisk", metrics.shadowTextureRisk },
        { "localMeanLuma3x3", metrics.localMeanLuma },
        { "localContrastSpan3x3", metrics.localContrastSpan },
        { "localDamageRiskScore3x3", metrics.localDamageRiskScore },
        { "localLumaSpread", metrics.localLumaSpread },
        { "localEvSpreadStops", metrics.localEvSpreadStops },
        { "localEvConflict", metrics.localEvConflict },
        { "localContrastPeak", metrics.localContrastPeak },
        { "localShadowPressure", metrics.localShadowPressure },
        { "localHighlightPressure", metrics.localHighlightPressure },
        { "localDamageRiskMean", metrics.localDamageRiskMean },
        { "localDamageRiskPeak", metrics.localDamageRiskPeak },
        { "localDamageRiskPeakTile", metrics.localDamageRiskPeakTile },
        { "localExposureHighlightCrowding", metrics.localExposureHighlightCrowding },
        { "localExposureShadowCrowding", metrics.localExposureShadowCrowding },
        { "localExposureHaloStress", metrics.localExposureHaloStress },
        { "localExposureFlatnessRisk", metrics.localExposureFlatnessRisk },
        { "localExposureDamageRisk", metrics.localExposureDamageRisk },
        { "subjectCenterPrior", metrics.subjectCenterPrior },
        { "subjectReadabilityPressure", metrics.subjectReadabilityPressure },
        { "subjectProtectionPressure", metrics.subjectProtectionPressure },
        { "subjectMoodPreservationPressure", metrics.subjectMoodPreservationPressure },
        { "subjectImportanceConfidence", metrics.subjectImportanceConfidence },
        { "centerMeanLuma", metrics.centerMeanLuma },
        { "centerShadowFraction", metrics.centerShadowFraction },
        { "centerHighlightFraction", metrics.centerHighlightFraction },
        { "subjectMarkedSampleCount", metrics.subjectMarkedSampleCount },
        { "subjectMarkedCoverage", metrics.subjectMarkedCoverage },
        { "subjectMarkedPositiveCoverage", metrics.subjectMarkedPositiveCoverage },
        { "subjectMarkedRevealCoverage", metrics.subjectMarkedRevealCoverage },
        { "subjectMarkedProtectCoverage", metrics.subjectMarkedProtectCoverage },
        { "subjectMarkedMoodCoverage", metrics.subjectMarkedMoodCoverage },
        { "subjectMarkedLowPriorityCoverage", metrics.subjectMarkedLowPriorityCoverage },
        { "subjectMarkedMeanLuma", metrics.subjectMarkedMeanLuma },
        { "subjectMarkedShadowFraction", metrics.subjectMarkedShadowFraction },
        { "subjectMarkedHighlightFraction", metrics.subjectMarkedHighlightFraction },
        { "subjectMarkedClippedFraction", metrics.subjectMarkedClippedFraction },
        { "subjectMarkedContrastSpan", metrics.subjectMarkedContrastSpan },
        { "subjectMarkedReadabilityScore", metrics.subjectMarkedReadabilityScore },
        { "subjectMarkedProtectionRisk", metrics.subjectMarkedProtectionRisk },
        { "subjectMarkedMoodPreservationScore", metrics.subjectMarkedMoodPreservationScore },
        { "subjectMarkedLowPriorityMeanLuma", metrics.subjectMarkedLowPriorityMeanLuma },
        { "subjectMarkedLowPriorityBrightFraction", metrics.subjectMarkedLowPriorityBrightFraction },
        { "subjectMarkedLowPriorityPressure", metrics.subjectMarkedLowPriorityPressure }
    };
}

void AppendDevelopCandidateRenderedFeedbackHistory(
    nlohmann::json& toneJson,
    std::uint64_t fingerprint,
    const std::string& selectedCandidateId,
    float selectedRenderScore,
    bool selectedRenderScoreValid,
    const std::string& bestCandidateId,
    float bestRenderScore,
    int successCount,
    int failureCount,
    const std::string& action,
    const std::string& stopReason,
    const std::string& refineIntent,
    const std::string& refineReason,
    const EditorRenderWorker::DevelopCandidateRenderMetrics* selectedMetrics,
    const EditorRenderWorker::DevelopCandidateRenderMetrics* bestMetrics) {
    nlohmann::json history =
        toneJson.value("autoCandidateRenderedFeedbackHistory", nlohmann::json::array());
    if (!history.is_array()) {
        history = nlohmann::json::array();
    }

    nlohmann::json entry;
    entry["fingerprint"] = fingerprint;
    entry["selectedId"] = selectedCandidateId;
    entry["selectedRenderScore"] = selectedRenderScoreValid ? selectedRenderScore : -1.0f;
    entry["selectedRenderScoreValid"] = selectedRenderScoreValid;
    entry["bestId"] = bestCandidateId;
    entry["bestRenderScore"] = std::max(0.0f, bestRenderScore);
    entry["successCount"] = successCount;
    entry["failureCount"] = failureCount;
    entry["action"] = action;
    entry["stopReason"] = stopReason;
    if (selectedMetrics) {
        entry["selectedMetrics"] = DevelopCandidateRenderMetricsToJson(*selectedMetrics);
    }
    if (bestMetrics) {
        entry["bestMetrics"] = DevelopCandidateRenderMetricsToJson(*bestMetrics);
    }
    if (!refineIntent.empty()) {
        entry["refineIntent"] = refineIntent;
        entry["refineReason"] = refineReason;
    }
    history.push_back(std::move(entry));

    constexpr std::size_t kMaxHistoryEntries = 8;
    while (history.size() > kMaxHistoryEntries) {
        history.erase(history.begin());
    }
    toneJson["autoCandidateRenderedFeedbackHistory"] = std::move(history);
}

void WriteDevelopCandidateRenderedFeedbackLoopRecord(
    nlohmann::json& toneJson,
    std::uint64_t solveFingerprint,
    std::uint64_t revision,
    const std::string& state,
    const std::string& action,
    const std::string& stopReason,
    const std::string& nextStep,
    bool requiresAutoSolve,
    bool requiresRenderedMetrics,
    const std::string& selectedCandidateId,
    float selectedRenderScore,
    bool selectedRenderScoreValid,
    const std::string& bestCandidateId,
    float bestRenderScore,
    int successCount,
    int failureCount) {
    const nlohmann::json renderedHistory =
        toneJson.value("autoCandidateRenderedFeedbackHistory", nlohmann::json::array());
    const int renderedHistoryCount =
        renderedHistory.is_array() ? static_cast<int>(renderedHistory.size()) : 0;
    const int currentPass =
        toneJson.value("autoCandidateRenderedFeedbackPass", 0);
    const nlohmann::json continuationPolicy =
        toneJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    toneJson["autoCandidateRenderedFeedbackLoopVersion"] =
        DevelopRenderedFeedback::kDevelopRenderedFeedbackLoopVersion;
    toneJson["autoCandidateRenderedFeedbackLoop"] = {
        { "version", DevelopRenderedFeedback::kDevelopRenderedFeedbackLoopVersion },
        { "state", state },
        { "action", action },
        { "stopReason", stopReason },
        { "nextStep", nextStep },
        { "requiresAutoSolve", requiresAutoSolve },
        { "requiresRenderedMetrics", requiresRenderedMetrics },
        { "pass", currentPass },
        { "nextPass", requiresAutoSolve ? currentPass + 1 : currentPass },
        { "maxPasses", DevelopRenderedFeedback::kDevelopRenderedFeedbackMaxPasses },
        { "solveFingerprint", solveFingerprint },
        { "renderedFingerprint", toneJson.value("autoCandidateRenderedFingerprint", static_cast<std::uint64_t>(0)) },
        { "renderedAtRevision", revision },
        { "renderMetricsStatus", toneJson.value("autoCandidateRenderMetricsStatus", std::string()) },
        { "selectedId", selectedCandidateId },
        { "selectedRenderScore", selectedRenderScoreValid ? selectedRenderScore : -1.0f },
        { "selectedRenderScoreValid", selectedRenderScoreValid },
        { "bestId", bestCandidateId },
        { "bestScore", std::max(0.0f, bestRenderScore) },
        { "successCount", successCount },
        { "failureCount", failureCount },
        { "revisionStage", toneJson.value("autoCandidateRenderedRevisionStage", std::string()) },
        { "revisionReason", toneJson.value("autoCandidateRenderedRevisionReason", std::string()) },
        { "historyCount", renderedHistoryCount },
        { "continuationPolicy", continuationPolicy }
    };
}

} // namespace Stack::Editor::DevelopRenderedFeedbackRecords
