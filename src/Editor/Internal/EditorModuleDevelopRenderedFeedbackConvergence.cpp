#include "Editor/Internal/EditorModuleDevelopRenderedFeedbackConvergence.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

void ReadDevelopMetricArray(
    const nlohmann::json& value,
    const char* key,
    std::array<float, 9>& outValues) {
    const nlohmann::json items = value.value(key, nlohmann::json::array());
    if (!items.is_array()) {
        return;
    }
    const std::size_t count = std::min<std::size_t>(items.size(), outValues.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (items[i].is_number()) {
            outValues[i] = items[i].get<float>();
        }
    }
}

bool TryReadLastSameIntentRefineMetrics(
    const nlohmann::json& toneJson,
    std::uint64_t excludedFingerprint,
    const std::string& refineIntent,
    EditorRenderWorker::DevelopCandidateRenderMetrics& outMetrics,
    float& outScore,
    std::string& outSelectedId) {
    if (refineIntent.empty()) {
        return false;
    }

    const nlohmann::json history =
        toneJson.value("autoCandidateRenderedFeedbackHistory", nlohmann::json::array());
    if (!history.is_array()) {
        return false;
    }

    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (!it->is_object() ||
            it->value("fingerprint", static_cast<std::uint64_t>(0)) == excludedFingerprint ||
            it->value("refineIntent", std::string()) != refineIntent) {
            continue;
        }
        if (!Stack::Editor::DevelopRenderedFeedback::ReadDevelopRenderedMetricsFromJson(
                it->value("selectedMetrics", nlohmann::json::object()),
                outMetrics)) {
            continue;
        }
        outScore = it->value("selectedRenderScore", -1.0f);
        outSelectedId = it->value("selectedId", std::string());
        return outScore >= 0.0f;
    }
    return false;
}

bool IsRenderedFeedbackMergeCandidateId(const std::string& candidateId) {
    return
        candidateId == "renderedFeedbackMerge" ||
        candidateId == "renderedFeedbackPairMerge" ||
        candidateId == "renderedFeedbackEnsembleMerge";
}

} // namespace

namespace Stack::Editor::DevelopRenderedFeedback {

DevelopConvergenceAdmissionPolicy ResolveDevelopConvergenceAdmissionPolicy(
    const nlohmann::json& previousToneJson,
    int previousPass,
    bool hasRefineIntent) {
    DevelopConvergenceAdmissionPolicy policy;
    policy.evidencePass = previousPass;
    const nlohmann::json evidence =
        previousToneJson.value("autoCandidateConvergenceEvidence", nlohmann::json::object());
    if (evidence.is_object()) {
        policy.evidenceState = evidence.value("state", std::string());
        policy.evidenceDecision = evidence.value("decision", std::string());
        policy.evidencePass = evidence.value("pass", previousPass);
    }
    const nlohmann::json continuationPolicy =
        previousToneJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    const std::string continuationDecision =
        continuationPolicy.is_object()
            ? continuationPolicy.value("decision", std::string())
            : std::string();
    const int continuationPass =
        continuationPolicy.is_object()
            ? continuationPolicy.value("pass", previousPass)
            : previousPass;

    if (hasRefineIntent) {
        policy.reason = "refineIntentUsesDedicatedOscillationGuards";
        return policy;
    }

    const int activePass =
        std::max({ previousPass, policy.evidencePass, continuationPass });
    const bool continuedFeedbackApplied =
        policy.evidenceState == "continuing" ||
        policy.evidenceDecision == "continue" ||
        continuationDecision == "continue";
    const bool continuedSolveAwaitingMetrics =
        activePass > 0 &&
        policy.evidenceState == "awaitingRenderedMetrics" &&
        policy.evidenceDecision == "waitForRenderedMetrics" &&
        continuationDecision == "waitForRenderedMetrics";
    if (continuedFeedbackApplied || continuedSolveAwaitingMetrics) {
        policy.tightened = true;
        if (activePass >= 2) {
            policy.minimumImprovement += 0.020f;
            policy.reason = "lateContinuationRequiresStrongerImprovement";
        } else {
            policy.minimumImprovement += 0.010f;
            policy.reason = continuedSolveAwaitingMetrics
                ? "continuedSolveMetricsRequireClearerImprovement"
                : "continuedRenderedFeedbackRequiresClearerImprovement";
        }
    }
    return policy;
}

nlohmann::json BuildDevelopRenderedContinuationPolicyRecord(
    const std::string& decision,
    const std::string& reason,
    const std::string& nextStep,
    bool requiresAutoSolve,
    bool requiresRenderedMetrics,
    int pass,
    int nextPass,
    const std::string& stageFocus,
    const std::string& stageReason,
    float improvement,
    const std::string& stabilityStatus,
    const std::string& trendStatus,
    const std::string& monotonicGuardStatus) {
    const int clampedPass =
        std::clamp(pass, 0, kDevelopRenderedFeedbackMaxPasses);
    const int clampedNextPass =
        std::clamp(nextPass, clampedPass, kDevelopRenderedFeedbackMaxPasses);
    return {
        { "version", kDevelopRenderedContinuationVersion },
        { "decision", decision },
        { "reason", reason },
        { "nextStep", nextStep },
        { "requiresAutoSolve", requiresAutoSolve },
        { "requiresRenderedMetrics", requiresRenderedMetrics },
        { "shouldContinue", decision == "continue" || decision == "waitForRenderedMetrics" },
        { "bounded", true },
        { "pass", clampedPass },
        { "nextPass", clampedNextPass },
        { "maxPasses", kDevelopRenderedFeedbackMaxPasses },
        { "remainingPasses", std::max(0, kDevelopRenderedFeedbackMaxPasses - clampedNextPass) },
        { "stageFocus", stageFocus },
        { "stageReason", stageReason },
        { "evidence", {
            { "improvement", improvement },
            { "stabilityStatus", stabilityStatus },
            { "trendStatus", trendStatus },
            { "monotonicGuardStatus", monotonicGuardStatus }
        } }
    };
}

nlohmann::json BuildDevelopRenderedContinuationPolicyRecord(
    const std::string& decision,
    const std::string& reason,
    const std::string& nextStep,
    bool requiresAutoSolve,
    bool requiresRenderedMetrics,
    int pass,
    int nextPass,
    const std::string& stageFocus,
    const std::string& stageReason,
    float improvement,
    const std::string& stageBoundarySignal,
    const std::string& relativeStatus,
    int successCount,
    int failureCount) {
    const int clampedPass =
        std::clamp(pass, 0, kDevelopRenderedFeedbackMaxPasses);
    const int clampedNextPass =
        std::clamp(nextPass, clampedPass, kDevelopRenderedFeedbackMaxPasses);
    return {
        { "version", kDevelopRenderedContinuationVersion },
        { "decision", decision },
        { "reason", reason },
        { "nextStep", nextStep },
        { "requiresAutoSolve", requiresAutoSolve },
        { "requiresRenderedMetrics", requiresRenderedMetrics },
        { "shouldContinue", decision == "continue" },
        { "bounded", true },
        { "pass", clampedPass },
        { "nextPass", clampedNextPass },
        { "maxPasses", kDevelopRenderedFeedbackMaxPasses },
        { "remainingPasses", std::max(0, kDevelopRenderedFeedbackMaxPasses - clampedNextPass) },
        { "stageFocus", stageFocus },
        { "stageReason", stageReason },
        { "evidence", {
            { "improvement", improvement },
            { "stageBoundarySignal", stageBoundarySignal },
            { "relativeStatus", relativeStatus },
            { "successCount", successCount },
            { "failureCount", failureCount }
        } }
    };
}

nlohmann::json BuildDevelopAutoConvergenceEvidenceRecord(
    const DevelopCandidateScoring::DevelopAutoCandidateSolveResult& result,
    bool renderedMetricsMatchCurrentSolve,
    bool renderedMetricsReadyForCurrentSolve,
    const std::string& currentRenderMetricsStatus,
    const std::string& loopState,
    const std::string& loopAction,
    const std::string& loopStopReason,
    const std::string& loopNextStep,
    bool loopRequiresAutoSolve,
    bool loopRequiresRenderedMetrics,
    int loopPass,
    int loopNextPass,
    int renderedHistoryCount,
    const nlohmann::json& continuationPolicy,
    const nlohmann::json& toneJson) {
    std::string state;
    std::string decision;
    std::string reason;
    if (result.renderedFeedbackApplied) {
        state = "continuing";
        decision = "continue";
        reason = result.renderedFeedbackAction.empty()
            ? std::string("renderedFeedbackApplied")
            : result.renderedFeedbackAction;
    } else if (renderedMetricsReadyForCurrentSolve &&
        !result.renderedFeedbackStopReason.empty()) {
        state = result.renderedFeedbackStopIsConverged ? "converged" : "stopped";
        decision = "stop";
        reason = result.renderedFeedbackStopReason;
    } else if (!renderedMetricsMatchCurrentSolve) {
        state = "awaitingRenderedMetrics";
        decision = "waitForRenderedMetrics";
        reason = "awaitingRenderedMetrics";
    } else if (result.converged) {
        state = "parameterStable";
        decision = "stop";
        reason = "candidateSolveFingerprintStable";
    } else {
        state = "evaluating";
        decision = continuationPolicy.value("decision", std::string("stop"));
        reason = loopStopReason.empty() ? std::string("renderedMetricsMeasured") : loopStopReason;
    }

    const bool shouldContinue =
        decision == "continue" ||
        decision == "waitForRenderedMetrics" ||
        continuationPolicy.value("shouldContinue", false);
    const nlohmann::json continuationEvidence =
        continuationPolicy.value("evidence", nlohmann::json::object());

    return {
        { "version", kDevelopConvergenceEvidenceVersion },
        { "state", state },
        { "decision", decision },
        { "reason", reason },
        { "shouldContinue", shouldContinue },
        { "bounded", true },
        { "pass", loopPass },
        { "nextPass", loopNextPass },
        { "maxPasses", kDevelopRenderedFeedbackMaxPasses },
        { "remainingPasses", continuationPolicy.value(
            "remainingPasses",
            std::max(0, kDevelopRenderedFeedbackMaxPasses - loopNextPass)) },
        { "nextStep", loopNextStep },
        { "requiresAutoSolve", loopRequiresAutoSolve },
        { "requiresRenderedMetrics", loopRequiresRenderedMetrics },
        { "parameter", {
            { "converged", result.converged },
            { "convergencePass", result.convergencePass },
            { "solveFingerprint", result.fingerprint },
            { "selectedId", result.selectedId },
            { "selectedScore", result.selectedScore }
        } },
        { "admission", {
            { "version", kDevelopConvergenceAdmissionVersion },
            { "baseMinimumImprovement", result.renderedFeedbackAdmissionBaseMinimumImprovement },
            { "minimumImprovement", result.renderedFeedbackAdmissionMinimumImprovement },
            { "tightened", result.renderedFeedbackAdmissionTightened },
            { "reason", result.renderedFeedbackAdmissionReason },
            { "evidenceState", result.renderedFeedbackAdmissionEvidenceState },
            { "evidenceDecision", result.renderedFeedbackAdmissionEvidenceDecision },
            { "evidencePass", result.renderedFeedbackAdmissionEvidencePass }
        } },
        { "rendered", {
            { "metricsStatus", currentRenderMetricsStatus },
            { "metricsMatchCurrentSolve", renderedMetricsMatchCurrentSolve },
            { "metricsReadyForCurrentSolve", renderedMetricsReadyForCurrentSolve },
            { "feedbackApplied", result.renderedFeedbackApplied },
            { "feedbackAction", result.renderedFeedbackAction },
            { "stopReason", result.renderedFeedbackStopReason },
            { "stopConverged", result.renderedFeedbackStopIsConverged },
            { "improvement", result.renderedFeedbackImprovement },
            { "stabilityStatus", toneJson.value("autoCandidateRenderedStabilityStatus", std::string()) },
            { "stabilityDistance", result.renderedFeedbackStabilityDistance },
            { "stabilityScoreDelta", result.renderedFeedbackStabilityScoreDelta },
            { "trendStatus", toneJson.value("autoCandidateRenderedTrendStatus", std::string()) },
            { "trendHistoryCount", result.renderedFeedbackTrendHistoryCount },
            { "trendSameBestCount", result.renderedFeedbackTrendSameBestCount },
            { "trendScoreSpread", result.renderedFeedbackTrendScoreSpread },
            { "trendNearestDistance", result.renderedFeedbackTrendNearestDistance },
            { "monotonicGuardStatus", toneJson.value("autoCandidateRenderedMonotonicGuardStatus", std::string()) },
            { "monotonicMetric", result.renderedFeedbackMonotonicMetric },
            { "revisionStage", toneJson.value("autoCandidateRenderedRevisionStage", std::string()) },
            { "revisionReason", toneJson.value("autoCandidateRenderedRevisionReason", std::string()) },
            { "historyCount", renderedHistoryCount }
        } },
        { "loop", {
            { "state", loopState },
            { "action", loopAction },
            { "stopReason", loopStopReason },
            { "nextStep", loopNextStep },
            { "requiresAutoSolve", loopRequiresAutoSolve },
            { "requiresRenderedMetrics", loopRequiresRenderedMetrics }
        } },
        { "continuation", {
            { "version", continuationPolicy.value("version", std::string()) },
            { "decision", continuationPolicy.value("decision", std::string()) },
            { "reason", continuationPolicy.value("reason", std::string()) },
            { "nextStep", continuationPolicy.value("nextStep", std::string()) },
            { "shouldContinue", continuationPolicy.value("shouldContinue", false) },
            { "stageFocus", continuationPolicy.value("stageFocus", std::string()) },
            { "stageReason", continuationPolicy.value("stageReason", std::string()) },
            { "evidence", continuationEvidence }
        } }
    };
}

bool TryReadRenderedCandidateScore(
    const nlohmann::json& renderedSolves,
    const std::string& candidateId,
    float& outScore) {
    if (!renderedSolves.is_array() || candidateId.empty()) {
        return false;
    }

    for (const nlohmann::json& rendered : renderedSolves) {
        if (!rendered.is_object() ||
            rendered.value("id", std::string()) != candidateId ||
            !rendered.value("success", false)) {
            continue;
        }
        outScore = rendered.value("renderScore", -1.0f);
        return outScore >= 0.0f;
    }
    return false;
}

bool ReadDevelopRenderedMetricsFromJson(
    const nlohmann::json& value,
    EditorRenderWorker::DevelopCandidateRenderMetrics& outMetrics) {
    if (!value.is_object()) {
        return false;
    }

    outMetrics.meanLuma = value.value("meanLuma", outMetrics.meanLuma);
    outMetrics.medianLuma = value.value("medianLuma", outMetrics.medianLuma);
    outMetrics.p10Luma = value.value("p10Luma", outMetrics.p10Luma);
    outMetrics.p90Luma = value.value("p90Luma", outMetrics.p90Luma);
    outMetrics.shadowFraction = value.value("shadowFraction", outMetrics.shadowFraction);
    outMetrics.highlightFraction = value.value("highlightFraction", outMetrics.highlightFraction);
    outMetrics.clippedFraction = value.value("clippedFraction", outMetrics.clippedFraction);
    outMetrics.contrastSpan = value.value("contrastSpan", outMetrics.contrastSpan);
    outMetrics.meanRed = value.value("meanRed", outMetrics.meanRed);
    outMetrics.meanGreen = value.value("meanGreen", outMetrics.meanGreen);
    outMetrics.meanBlue = value.value("meanBlue", outMetrics.meanBlue);
    outMetrics.warmCoolBias = value.value("warmCoolBias", outMetrics.warmCoolBias);
    outMetrics.magentaGreenBias = value.value("magentaGreenBias", outMetrics.magentaGreenBias);
    outMetrics.channelImbalance = value.value("channelImbalance", outMetrics.channelImbalance);
    outMetrics.colorCastRisk = value.value("colorCastRisk", outMetrics.colorCastRisk);
    outMetrics.meanSaturation = value.value("meanSaturation", outMetrics.meanSaturation);
    outMetrics.lowSaturationFraction = value.value("lowSaturationFraction", outMetrics.lowSaturationFraction);
    outMetrics.highlightBandFraction = value.value("highlightBandFraction", outMetrics.highlightBandFraction);
    outMetrics.highlightMeanLuma = value.value("highlightMeanLuma", outMetrics.highlightMeanLuma);
    outMetrics.highlightLowSaturationFraction =
        value.value("highlightLowSaturationFraction", outMetrics.highlightLowSaturationFraction);
    outMetrics.highlightGrayRisk = value.value("highlightGrayRisk", outMetrics.highlightGrayRisk);
    outMetrics.highlightTileCoverage = value.value("highlightTileCoverage", outMetrics.highlightTileCoverage);
    outMetrics.highlightStructureScore = value.value("highlightStructureScore", outMetrics.highlightStructureScore);
    outMetrics.meaningfulHighlightPressure =
        value.value("meaningfulHighlightPressure", outMetrics.meaningfulHighlightPressure);
    outMetrics.edgeContrast = value.value("edgeContrast", outMetrics.edgeContrast);
    outMetrics.haloRiskFraction = value.value("haloRiskFraction", outMetrics.haloRiskFraction);
    outMetrics.shadowTextureRisk = value.value("shadowTextureRisk", outMetrics.shadowTextureRisk);
    ReadDevelopMetricArray(value, "localMeanLuma3x3", outMetrics.localMeanLuma);
    ReadDevelopMetricArray(value, "localContrastSpan3x3", outMetrics.localContrastSpan);
    ReadDevelopMetricArray(value, "localDamageRiskScore3x3", outMetrics.localDamageRiskScore);
    outMetrics.localLumaSpread = value.value("localLumaSpread", outMetrics.localLumaSpread);
    outMetrics.localEvSpreadStops = value.value("localEvSpreadStops", outMetrics.localEvSpreadStops);
    outMetrics.localEvConflict = value.value("localEvConflict", outMetrics.localEvConflict);
    outMetrics.localContrastPeak = value.value("localContrastPeak", outMetrics.localContrastPeak);
    outMetrics.localShadowPressure = value.value("localShadowPressure", outMetrics.localShadowPressure);
    outMetrics.localHighlightPressure = value.value("localHighlightPressure", outMetrics.localHighlightPressure);
    outMetrics.localDamageRiskMean = value.value("localDamageRiskMean", outMetrics.localDamageRiskMean);
    outMetrics.localDamageRiskPeak = value.value("localDamageRiskPeak", outMetrics.localDamageRiskPeak);
    outMetrics.localDamageRiskPeakTile = value.value("localDamageRiskPeakTile", outMetrics.localDamageRiskPeakTile);
    outMetrics.localExposureHighlightCrowding =
        value.value("localExposureHighlightCrowding", outMetrics.localExposureHighlightCrowding);
    outMetrics.localExposureShadowCrowding =
        value.value("localExposureShadowCrowding", outMetrics.localExposureShadowCrowding);
    outMetrics.localExposureHaloStress =
        value.value("localExposureHaloStress", outMetrics.localExposureHaloStress);
    outMetrics.localExposureFlatnessRisk =
        value.value("localExposureFlatnessRisk", outMetrics.localExposureFlatnessRisk);
    outMetrics.localExposureDamageRisk =
        value.value("localExposureDamageRisk", outMetrics.localExposureDamageRisk);
    outMetrics.subjectCenterPrior = value.value("subjectCenterPrior", outMetrics.subjectCenterPrior);
    outMetrics.subjectReadabilityPressure =
        value.value("subjectReadabilityPressure", outMetrics.subjectReadabilityPressure);
    outMetrics.subjectProtectionPressure =
        value.value("subjectProtectionPressure", outMetrics.subjectProtectionPressure);
    outMetrics.subjectMoodPreservationPressure =
        value.value("subjectMoodPreservationPressure", outMetrics.subjectMoodPreservationPressure);
    outMetrics.subjectImportanceConfidence =
        value.value("subjectImportanceConfidence", outMetrics.subjectImportanceConfidence);
    outMetrics.centerMeanLuma = value.value("centerMeanLuma", outMetrics.centerMeanLuma);
    outMetrics.centerShadowFraction = value.value("centerShadowFraction", outMetrics.centerShadowFraction);
    outMetrics.centerHighlightFraction = value.value("centerHighlightFraction", outMetrics.centerHighlightFraction);
    outMetrics.subjectMarkedSampleCount =
        value.value("subjectMarkedSampleCount", outMetrics.subjectMarkedSampleCount);
    outMetrics.subjectMarkedCoverage = value.value("subjectMarkedCoverage", outMetrics.subjectMarkedCoverage);
    outMetrics.subjectMarkedPositiveCoverage =
        value.value("subjectMarkedPositiveCoverage", outMetrics.subjectMarkedPositiveCoverage);
    outMetrics.subjectMarkedRevealCoverage =
        value.value("subjectMarkedRevealCoverage", outMetrics.subjectMarkedRevealCoverage);
    outMetrics.subjectMarkedProtectCoverage =
        value.value("subjectMarkedProtectCoverage", outMetrics.subjectMarkedProtectCoverage);
    outMetrics.subjectMarkedMoodCoverage =
        value.value("subjectMarkedMoodCoverage", outMetrics.subjectMarkedMoodCoverage);
    outMetrics.subjectMarkedLowPriorityCoverage =
        value.value("subjectMarkedLowPriorityCoverage", outMetrics.subjectMarkedLowPriorityCoverage);
    outMetrics.subjectMarkedMeanLuma =
        value.value("subjectMarkedMeanLuma", outMetrics.subjectMarkedMeanLuma);
    outMetrics.subjectMarkedShadowFraction =
        value.value("subjectMarkedShadowFraction", outMetrics.subjectMarkedShadowFraction);
    outMetrics.subjectMarkedHighlightFraction =
        value.value("subjectMarkedHighlightFraction", outMetrics.subjectMarkedHighlightFraction);
    outMetrics.subjectMarkedClippedFraction =
        value.value("subjectMarkedClippedFraction", outMetrics.subjectMarkedClippedFraction);
    outMetrics.subjectMarkedContrastSpan =
        value.value("subjectMarkedContrastSpan", outMetrics.subjectMarkedContrastSpan);
    outMetrics.subjectMarkedReadabilityScore =
        value.value("subjectMarkedReadabilityScore", outMetrics.subjectMarkedReadabilityScore);
    outMetrics.subjectMarkedProtectionRisk =
        value.value("subjectMarkedProtectionRisk", outMetrics.subjectMarkedProtectionRisk);
    outMetrics.subjectMarkedMoodPreservationScore =
        value.value("subjectMarkedMoodPreservationScore", outMetrics.subjectMarkedMoodPreservationScore);
    outMetrics.subjectMarkedLowPriorityMeanLuma =
        value.value("subjectMarkedLowPriorityMeanLuma", outMetrics.subjectMarkedLowPriorityMeanLuma);
    outMetrics.subjectMarkedLowPriorityBrightFraction =
        value.value("subjectMarkedLowPriorityBrightFraction", outMetrics.subjectMarkedLowPriorityBrightFraction);
    outMetrics.subjectMarkedLowPriorityPressure =
        value.value("subjectMarkedLowPriorityPressure", outMetrics.subjectMarkedLowPriorityPressure);
    return true;
}

bool TryReadRenderedCandidateMetrics(
    const nlohmann::json& renderedSolves,
    const std::string& candidateId,
    EditorRenderWorker::DevelopCandidateRenderMetrics& outMetrics,
    float& outScore) {
    if (!renderedSolves.is_array() || candidateId.empty()) {
        return false;
    }

    for (const nlohmann::json& rendered : renderedSolves) {
        if (!rendered.is_object() ||
            rendered.value("id", std::string()) != candidateId ||
            !rendered.value("success", false)) {
            continue;
        }
        if (!ReadDevelopRenderedMetricsFromJson(rendered.value("metrics", nlohmann::json::object()), outMetrics)) {
            return false;
        }
        outScore = rendered.value("renderScore", -1.0f);
        return outScore >= 0.0f;
    }
    return false;
}

bool TryReadLastRenderedHistoryMetrics(
    const nlohmann::json& toneJson,
    std::uint64_t excludedFingerprint,
    EditorRenderWorker::DevelopCandidateRenderMetrics& outMetrics,
    float& outScore,
    std::string& outBestId) {
    const nlohmann::json history =
        toneJson.value("autoCandidateRenderedFeedbackHistory", nlohmann::json::array());
    if (!history.is_array()) {
        return false;
    }

    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (!it->is_object()) {
            continue;
        }
        if (it->value("fingerprint", static_cast<std::uint64_t>(0)) == excludedFingerprint) {
            continue;
        }
        if (!ReadDevelopRenderedMetricsFromJson(it->value("bestMetrics", nlohmann::json::object()), outMetrics)) {
            continue;
        }
        outScore = it->value("bestRenderScore", -1.0f);
        outBestId = it->value("bestId", std::string());
        return outScore >= 0.0f && !outBestId.empty();
    }
    return false;
}

DevelopRenderedFeedbackTrend EvaluateDevelopRenderedFeedbackTrend(
    const nlohmann::json& toneJson,
    std::uint64_t excludedFingerprint,
    const std::string& currentBestId,
    float currentBestScore,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& currentBestMetrics,
    float selectedRenderedScore,
    bool selectedRendered,
    int previousPass,
    bool refineFeedback) {
    DevelopRenderedFeedbackTrend trend;
    if (previousPass < 2 || currentBestId.empty() || currentBestScore < 0.0f) {
        return trend;
    }

    const nlohmann::json history =
        toneJson.value("autoCandidateRenderedFeedbackHistory", nlohmann::json::array());
    if (!history.is_array()) {
        return trend;
    }

    float minScore = currentBestScore;
    float maxScore = currentBestScore;
    for (auto it = history.rbegin(); it != history.rend() && trend.historyCount < 3; ++it) {
        if (!it->is_object() ||
            it->value("fingerprint", static_cast<std::uint64_t>(0)) == excludedFingerprint ||
            it->value("successCount", 0) <= 0) {
            continue;
        }

        const std::string historyBestId = it->value("bestId", std::string());
        const float historyBestScore = it->value("bestRenderScore", -1.0f);
        if (historyBestId.empty() || historyBestScore < 0.0f) {
            continue;
        }

        EditorRenderWorker::DevelopCandidateRenderMetrics historyMetrics;
        if (!ReadDevelopRenderedMetricsFromJson(it->value("bestMetrics", nlohmann::json::object()), historyMetrics)) {
            continue;
        }

        if (trend.referenceId.empty()) {
            trend.referenceId = historyBestId;
        }
        ++trend.historyCount;
        if (historyBestId == currentBestId) {
            ++trend.sameBestCount;
        }
        const float metricDistance =
            EditorRenderWorker::CompareDevelopCandidateRenderMetrics(currentBestMetrics, historyMetrics);
        trend.nearestMetricDistance = trend.nearestMetricDistance < 0.0f
            ? metricDistance
            : std::min(trend.nearestMetricDistance, metricDistance);
        minScore = std::min(minScore, historyBestScore);
        maxScore = std::max(maxScore, historyBestScore);
    }

    if (trend.historyCount <= 0) {
        return trend;
    }

    trend.scoreSpread = maxScore - minScore;
    const bool repeatedSameBest =
        trend.historyCount >= 2 &&
        trend.sameBestCount >= 2;
    const bool stableRenderedShape =
        trend.nearestMetricDistance >= 0.0f &&
        trend.nearestMetricDistance < 0.055f;
    const bool flatScoreTrend =
        trend.scoreSpread >= 0.0f &&
        trend.scoreSpread < 0.030f;
    const bool limitedCurrentGain =
        !selectedRendered ||
        currentBestScore < selectedRenderedScore + 0.060f;

    if (repeatedSameBest && stableRenderedShape && flatScoreTrend) {
        trend.stopReason = refineFeedback
            ? "renderedRefineNoImprovementTrend"
            : "renderedFeedbackNoImprovementTrend";
    } else if (trend.historyCount >= 2 && stableRenderedShape && flatScoreTrend && limitedCurrentGain) {
        trend.stopReason = "renderedFeedbackStableTrend";
    }

    return trend;
}

DevelopRenderedMonotonicGuardDecision EvaluateRenderedRefineMonotonicGuard(
    const nlohmann::json& toneJson,
    std::uint64_t excludedFingerprint,
    const std::string& refineIntent,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& currentSelectedMetrics,
    float currentSelectedScore,
    bool currentSelectedScoreValid,
    int previousPass) {
    DevelopRenderedMonotonicGuardDecision decision;
    if (refineIntent.empty() || previousPass <= 0) {
        return decision;
    }

    EditorRenderWorker::DevelopCandidateRenderMetrics previousSelectedMetrics;
    float previousSelectedScore = -1.0f;
    std::string previousSelectedId;
    if (!TryReadLastSameIntentRefineMetrics(
            toneJson,
            excludedFingerprint,
            refineIntent,
            previousSelectedMetrics,
            previousSelectedScore,
            previousSelectedId)) {
        return decision;
    }

    const bool scoreClearlyImproved =
        currentSelectedScoreValid &&
        previousSelectedScore >= 0.0f &&
        currentSelectedScore >= previousSelectedScore + 0.040f;
    auto stopIfWorse = [&](const char* reason,
                           const char* metric,
                           float previousValue,
                           float currentValue,
                           float allowedIncrease,
                           float floor,
                           float severeFloor) {
        if (decision.stop) {
            return;
        }
        const bool materiallyWorse =
            currentValue > previousValue + allowedIncrease &&
            currentValue > floor;
        if (!materiallyWorse) {
            return;
        }
        // Same-intent refinements get one chance to improve. If the protected
        // risk worsens without a clear score gain, stop instead of chasing the
        // same direction again.
        if (!scoreClearlyImproved || currentValue > severeFloor) {
            decision.stop = true;
            decision.reason = reason;
            decision.metric = metric;
            decision.previousValue = previousValue;
            decision.currentValue = currentValue;
            decision.referenceId = previousSelectedId;
        }
    };

    if (refineIntent == "openShadows" || refineIntent == "cleanShadows") {
        stopIfWorse(
            "renderedRefineMonotonicShadowRisk",
            "shadowTextureRisk",
            previousSelectedMetrics.shadowTextureRisk,
            currentSelectedMetrics.shadowTextureRisk,
            0.055f,
            0.52f,
            0.72f);
        stopIfWorse(
            "renderedRefineMonotonicShadowRisk",
            "localShadowPressure",
            previousSelectedMetrics.localShadowPressure,
            currentSelectedMetrics.localShadowPressure,
            0.080f,
            0.50f,
            0.76f);
        stopIfWorse(
            "renderedRefineMonotonicShadowRisk",
            "localDamageRiskPeak",
            previousSelectedMetrics.localDamageRiskPeak,
            currentSelectedMetrics.localDamageRiskPeak,
            0.100f,
            0.55f,
            0.76f);
    } else if (refineIntent == "protectHighlights" || refineIntent == "brightenMids") {
        const char* reason = refineIntent == "brightenMids"
            ? "renderedRefineMonotonicBrightnessRisk"
            : "renderedRefineMonotonicHighlightRisk";
        stopIfWorse(
            reason,
            "clippedFraction",
            previousSelectedMetrics.clippedFraction,
            currentSelectedMetrics.clippedFraction,
            0.004f,
            0.008f,
            0.018f);
        stopIfWorse(
            reason,
            "localHighlightPressure",
            previousSelectedMetrics.localHighlightPressure,
            currentSelectedMetrics.localHighlightPressure,
            0.080f,
            0.50f,
            0.70f);
        stopIfWorse(
            reason,
            "haloRiskFraction",
            previousSelectedMetrics.haloRiskFraction,
            currentSelectedMetrics.haloRiskFraction,
            0.040f,
            0.08f,
            0.16f);
        stopIfWorse(
            reason,
            "localDamageRiskPeak",
            previousSelectedMetrics.localDamageRiskPeak,
            currentSelectedMetrics.localDamageRiskPeak,
            0.100f,
            0.55f,
            0.76f);
    } else if (refineIntent == "addContrast") {
        stopIfWorse(
            "renderedRefineMonotonicContrastRisk",
            "haloRiskFraction",
            previousSelectedMetrics.haloRiskFraction,
            currentSelectedMetrics.haloRiskFraction,
            0.040f,
            0.08f,
            0.16f);
        stopIfWorse(
            "renderedRefineMonotonicContrastRisk",
            "localContrastPeak",
            previousSelectedMetrics.localContrastPeak,
            currentSelectedMetrics.localContrastPeak,
            0.100f,
            0.76f,
            0.90f);
        stopIfWorse(
            "renderedRefineMonotonicContrastRisk",
            "localDamageRiskPeak",
            previousSelectedMetrics.localDamageRiskPeak,
            currentSelectedMetrics.localDamageRiskPeak,
            0.100f,
            0.55f,
            0.76f);
    } else if (refineIntent == "preserveTexture") {
        const float edgeGain =
            currentSelectedMetrics.edgeContrast - previousSelectedMetrics.edgeContrast;
        if (edgeGain < 0.025f) {
            stopIfWorse(
                "renderedRefineMonotonicTextureRisk",
                "shadowTextureRisk",
                previousSelectedMetrics.shadowTextureRisk,
                currentSelectedMetrics.shadowTextureRisk,
                0.060f,
                0.48f,
                0.70f);
        }
        stopIfWorse(
            "renderedRefineMonotonicTextureRisk",
            "localDamageRiskPeak",
            previousSelectedMetrics.localDamageRiskPeak,
            currentSelectedMetrics.localDamageRiskPeak,
            0.100f,
            0.55f,
            0.76f);
    }

    return decision;
}

bool WouldRenderedFeedbackReverseRecentAdoption(
    const nlohmann::json& toneJson,
    const std::string& selectedCandidateId,
    const std::string& bestCandidateId) {
    const nlohmann::json history =
        toneJson.value("autoCandidateRenderedFeedbackHistory", nlohmann::json::array());
    if (!history.is_array() || selectedCandidateId.empty() || bestCandidateId.empty()) {
        return false;
    }

    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (!it->is_object()) {
            continue;
        }
        const std::string action = it->value("action", std::string());
        if (action != "adopted" && action != "merged" && action != "refined" && action != "solveRequested") {
            continue;
        }
        const std::string previousSelected = it->value("selectedId", std::string());
        const std::string previousBest = it->value("bestId", std::string());
        return previousSelected == bestCandidateId && previousBest == selectedCandidateId;
    }
    return false;
}

std::string RepeatedRenderedChoiceStopReason(
    const nlohmann::json& toneJson,
    const std::string& selectedCandidateId,
    const std::string& bestCandidateId,
    float selectedRenderedScore,
    bool selectedRendered,
    float bestRenderedScore) {
    if (!selectedRendered || selectedCandidateId.empty() || bestCandidateId.empty()) {
        return {};
    }

    const std::string previousAction =
        toneJson.value("autoCandidateRenderedFeedbackAction", std::string());
    const std::string previousBestId =
        toneJson.value("autoCandidateRenderedFeedbackBestId", std::string());
    if (previousBestId.empty() || previousBestId != bestCandidateId) {
        return {};
    }

    if (previousAction == "merged" && IsRenderedFeedbackMergeCandidateId(selectedCandidateId)) {
        const float currentGap = bestRenderedScore - selectedRenderedScore;
        if (currentGap <= 0.060f) {
            return "renderedMergeConverged";
        }

        const float previousBestScore =
            toneJson.value("autoCandidateRenderedFeedbackBestScore", -1.0f);
        const float previousSelectedScore =
            toneJson.value("autoCandidateRenderedFeedbackPreviousSelectedScore", -1.0f);
        if (previousBestScore >= 0.0f && previousSelectedScore >= 0.0f) {
            const float previousGap = previousBestScore - previousSelectedScore;
            if (currentGap >= previousGap - 0.010f) {
                return "renderedMergeDidNotImprove";
            }
        }
    }

    if (previousAction == "adopted" &&
        selectedCandidateId != bestCandidateId &&
        bestRenderedScore < selectedRenderedScore + 0.040f) {
        return "renderedAdoptionNoFurtherGain";
    }

    return {};
}

bool WouldRepeatUnhelpfulRenderedRefinement(
    const nlohmann::json& toneJson,
    const std::string& refineIntent,
    float selectedRenderedScore,
    bool selectedRendered) {
    if (refineIntent.empty()) {
        return false;
    }
    if (toneJson.value("autoCandidateRenderedFeedbackAction", std::string()) != "refined" ||
        toneJson.value("autoCandidateRenderedFeedbackRefineIntent", std::string()) != refineIntent) {
        return false;
    }

    if (!selectedRendered) {
        return toneJson.value("autoCandidateRenderedFeedbackPass", 0) > 0;
    }

    const float previousSelectedScore =
        toneJson.value("autoCandidateRenderedFeedbackPreviousSelectedScore", -1.0f);
    if (previousSelectedScore >= 0.0f && selectedRenderedScore < previousSelectedScore + 0.025f) {
        return true;
    }

    // Give a repeated same-direction refine one chance to prove improvement, then stop.
    return toneJson.value("autoCandidateRenderedFeedbackPass", 0) >= 2;
}

bool IsDevelopRenderedFeedbackStopConvergedReason(const std::string& stopReason) {
    if (stopReason == "renderedMetricsStable" ||
        stopReason == "renderedFeedbackNoImprovementTrend" ||
        stopReason == "renderedRefineNoImprovementTrend" ||
        stopReason == "renderedFeedbackStableTrend" ||
        stopReason == "selectedCandidateStillBest" ||
        stopReason == "noMeaningfulRenderedImprovement" ||
        stopReason == "convergenceAdmissionNoMeaningfulImprovement" ||
        stopReason == "renderedRefineDidNotImprove" ||
        stopReason == "renderedRefineRepeatedIntent" ||
        stopReason == "renderedMergeConverged" ||
        stopReason == "renderedMergeDidNotImprove" ||
        stopReason == "renderedAdoptionNoFurtherGain" ||
        stopReason == "renderedBestRelativeRegression") {
        return true;
    }
    return stopReason.rfind("renderedRefineMonotonic", 0) == 0;
}

} // namespace Stack::Editor::DevelopRenderedFeedback
