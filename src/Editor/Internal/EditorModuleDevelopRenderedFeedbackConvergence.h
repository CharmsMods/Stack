#pragma once

#include "Editor/Internal/EditorModuleDevelopCandidateScoring.h"
#include "Editor/EditorRenderWorker.h"
#include "ThirdParty/json.hpp"

#include <cstdint>
#include <string>

namespace Stack::Editor::DevelopRenderedFeedback {

inline constexpr int kDevelopRenderedFeedbackMaxPasses = 3;
inline constexpr const char* kDevelopRenderedFeedbackLoopVersion = "RenderedFeedbackLoopV1";
inline constexpr const char* kDevelopRenderedContinuationVersion = "RenderedContinuationV1";
inline constexpr const char* kDevelopConvergenceEvidenceVersion = "ConvergenceEvidenceV1";
inline constexpr const char* kDevelopConvergenceAdmissionVersion = "ConvergenceAdmissionV1";

struct DevelopConvergenceAdmissionPolicy {
    float baseMinimumImprovement = 0.025f;
    float minimumImprovement = 0.025f;
    bool tightened = false;
    std::string reason = "base";
    std::string evidenceState;
    std::string evidenceDecision;
    int evidencePass = 0;
};

struct DevelopRenderedFeedbackTrend {
    int historyCount = 0;
    int sameBestCount = 0;
    float scoreSpread = -1.0f;
    float nearestMetricDistance = -1.0f;
    std::string referenceId;
    std::string stopReason;
};

struct DevelopRenderedMonotonicGuardDecision {
    bool stop = false;
    std::string reason;
    std::string metric;
    float previousValue = -1.0f;
    float currentValue = -1.0f;
    std::string referenceId;
};

DevelopConvergenceAdmissionPolicy ResolveDevelopConvergenceAdmissionPolicy(
    const nlohmann::json& previousToneJson,
    int previousPass,
    bool hasRefineIntent);

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
    const std::string& monotonicGuardStatus);

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
    int failureCount);

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
    const nlohmann::json& toneJson);

bool TryReadRenderedCandidateScore(
    const nlohmann::json& renderedSolves,
    const std::string& candidateId,
    float& outScore);

bool ReadDevelopRenderedMetricsFromJson(
    const nlohmann::json& value,
    EditorRenderWorker::DevelopCandidateRenderMetrics& outMetrics);

bool TryReadRenderedCandidateMetrics(
    const nlohmann::json& renderedSolves,
    const std::string& candidateId,
    EditorRenderWorker::DevelopCandidateRenderMetrics& outMetrics,
    float& outScore);

bool TryReadLastRenderedHistoryMetrics(
    const nlohmann::json& toneJson,
    std::uint64_t excludedFingerprint,
    EditorRenderWorker::DevelopCandidateRenderMetrics& outMetrics,
    float& outScore,
    std::string& outBestId);

DevelopRenderedFeedbackTrend EvaluateDevelopRenderedFeedbackTrend(
    const nlohmann::json& toneJson,
    std::uint64_t excludedFingerprint,
    const std::string& currentBestId,
    float currentBestScore,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& currentBestMetrics,
    float selectedRenderedScore,
    bool selectedRendered,
    int previousPass,
    bool refineFeedback);

DevelopRenderedMonotonicGuardDecision EvaluateRenderedRefineMonotonicGuard(
    const nlohmann::json& toneJson,
    std::uint64_t excludedFingerprint,
    const std::string& refineIntent,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& currentSelectedMetrics,
    float currentSelectedScore,
    bool currentSelectedScoreValid,
    int previousPass);

bool WouldRenderedFeedbackReverseRecentAdoption(
    const nlohmann::json& toneJson,
    const std::string& selectedCandidateId,
    const std::string& bestCandidateId);

std::string RepeatedRenderedChoiceStopReason(
    const nlohmann::json& toneJson,
    const std::string& selectedCandidateId,
    const std::string& bestCandidateId,
    float selectedRenderedScore,
    bool selectedRendered,
    float bestRenderedScore);

bool WouldRepeatUnhelpfulRenderedRefinement(
    const nlohmann::json& toneJson,
    const std::string& refineIntent,
    float selectedRenderedScore,
    bool selectedRendered);

bool IsDevelopRenderedFeedbackStopConvergedReason(const std::string& stopReason);

} // namespace Stack::Editor::DevelopRenderedFeedback
