#pragma once

#include "Editor/Internal/EditorModuleDevelopDynamicRangeStrategy.h"
#include "Editor/Internal/EditorModuleDevelopSubjectImportance.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Raw/RawImageData.h"
#include "ThirdParty/json.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Stack::Editor::DevelopCandidateScoring {

struct DevelopAutoCandidateSolve {
    std::string id;
    std::string label;
    std::string reason;
    EditorNodeGraph::DevelopAutoGuidance guidance;
    std::uint64_t guidanceFingerprint = 0;
    float score = 0.0f;
    nlohmann::json scoreComponents = nlohmann::json::object();
    bool continuationBiasActive = false;
    float continuationBiasBonus = 0.0f;
    std::string continuationBiasReason;
    std::string continuationBiasStage;
    std::string continuationBiasRefineIntent;
    bool continuationExpansionCandidate = false;
    std::string continuationExpansionReason;
    std::string continuationExpansionStage;
    std::string continuationExpansionRefineIntent;
    bool rejected = false;
    bool duplicate = false;
    bool rememberedRejection = false;
    bool renderedMemoryRejected = false;
    bool whiteBalanceProbe = false;
    Raw::WhiteBalanceMode whiteBalanceMode = Raw::WhiteBalanceMode::AsShot;
    std::string rejectReason;
};

struct DevelopAutoCandidateSolveResult {
    std::vector<DevelopAutoCandidateSolve> candidates;
    EditorNodeGraph::DevelopAutoGuidance authoredGuidance;
    std::string selectedId = "base";
    std::string selectedLabel = "Base Solve";
    float selectedScore = 0.0f;
    std::string selectionSource = "parameterScore";
    bool mergeApplied = false;
    std::string mergeFirstId;
    std::string mergeSecondId;
    std::string mergeThirdId;
    float mergeFirstWeight = 1.0f;
    float mergeSecondWeight = 0.0f;
    float mergeThirdWeight = 0.0f;
    bool renderedFeedbackApplied = false;
    std::uint64_t renderedFeedbackSourceFingerprint = 0;
    int renderedFeedbackPass = 0;
    std::string renderedFeedbackPreviousSelectedId;
    float renderedFeedbackPreviousSelectedScore = 0.0f;
    std::string renderedFeedbackBestId;
    float renderedFeedbackBestScore = 0.0f;
    std::string renderedFeedbackAction;
    std::string renderedFeedbackRefineIntent;
    std::string renderedFeedbackRefineReason;
    std::string renderedFeedbackRevisionStage;
    std::string renderedFeedbackRevisionReason;
    std::string renderedFeedbackStopReason;
    bool renderedFeedbackStopIsConverged = false;
    float renderedFeedbackImprovement = -1.0f;
    float renderedFeedbackAdmissionBaseMinimumImprovement = 0.025f;
    float renderedFeedbackAdmissionMinimumImprovement = 0.025f;
    bool renderedFeedbackAdmissionTightened = false;
    std::string renderedFeedbackAdmissionReason = "base";
    std::string renderedFeedbackAdmissionEvidenceState;
    std::string renderedFeedbackAdmissionEvidenceDecision;
    int renderedFeedbackAdmissionEvidencePass = 0;
    float renderedFeedbackStabilityDistance = -1.0f;
    float renderedFeedbackStabilityScoreDelta = -1.0f;
    std::string renderedFeedbackStabilityReferenceId;
    int renderedFeedbackTrendHistoryCount = 0;
    int renderedFeedbackTrendSameBestCount = 0;
    float renderedFeedbackTrendScoreSpread = -1.0f;
    float renderedFeedbackTrendNearestDistance = -1.0f;
    std::string renderedFeedbackTrendReferenceId;
    std::string renderedFeedbackMonotonicMetric;
    float renderedFeedbackMonotonicPreviousValue = -1.0f;
    float renderedFeedbackMonotonicCurrentValue = -1.0f;
    std::string renderedFeedbackMonotonicReferenceId;
    int renderedFeedbackCarriedForwardCount = 0;
    std::uint64_t candidateContextFingerprint = 0;
    bool continuationBiasActive = false;
    std::string continuationBiasReason;
    std::string continuationBiasDecision;
    std::string continuationBiasStage;
    std::string continuationBiasRefineIntent;
    int continuationBiasAppliedCount = 0;
    bool continuationExpansionEligible = false;
    std::string continuationExpansionReason;
    std::string continuationExpansionStage;
    std::string continuationExpansionRefineIntent;
    int continuationExpansionAddedCount = 0;
    int rejectedMemorySuppressionCount = 0;
    int renderedRejectedMemorySuppressionCount = 0;
    nlohmann::json dynamicRangeStrategy = nlohmann::json::object();
    nlohmann::json subjectSceneIntent = nlohmann::json::object();
    bool authoredWhiteBalanceProbe = false;
    Raw::WhiteBalanceMode authoredWhiteBalanceMode = Raw::WhiteBalanceMode::AsShot;
    bool converged = false;
    int convergencePass = 0;
    std::uint64_t fingerprint = 0;
};

bool TryResolveDevelopWhiteBalanceProbeMode(
    const std::string& candidateId,
    Raw::WhiteBalanceMode& outMode);
bool IsDevelopWhiteBalanceProbeCandidateId(const std::string& candidateId);
std::string PreferredRenderedRefineCandidateId(const std::string& refineIntent);
bool IsRenderedLocalRefineCandidateId(const std::string& candidateId);
bool IsDevelopCleanupProbeCandidateId(const std::string& candidateId);
bool IsDevelopModeNeighborCandidateId(const std::string& candidateId);

float DevelopAutoCandidateDistance(
    const EditorNodeGraph::DevelopAutoGuidance& a,
    const EditorNodeGraph::DevelopAutoGuidance& b);

std::string DevelopRenderedRevisionStageForRefineIntent(const std::string& refineIntent);
bool IsDevelopFinishToneProbeCandidateId(const std::string& candidateId);
bool IsDevelopSubjectIntentCandidateId(const std::string& candidateId);
std::string DevelopRenderedRevisionStageForCandidateId(const std::string& candidateId);
std::string DevelopRenderedRevisionStageForGuidanceDelta(
    const EditorNodeGraph::DevelopAutoGuidance& from,
    const EditorNodeGraph::DevelopAutoGuidance& to,
    const std::string& candidateId);
std::string DevelopRenderedRevisionStageReason(
    const std::string& stage,
    const std::string& fallback);

std::uint64_t BuildDevelopAutoCandidateContextFingerprint(
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopSubjectImportanceMap& subjectImportance,
    const DevelopDynamicRange::DevelopToneAutoStats& stats);
std::uint64_t BuildDevelopAutoCandidateGuidanceFingerprint(
    const std::string& candidateId,
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    const EditorNodeGraph::DevelopSubjectImportanceMap* subjectImportance = nullptr);

float ScoreDevelopAutoCandidate(
    const std::string& id,
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopAutoGuidance& base,
    const DevelopDynamicRange::DevelopToneAutoStats& stats,
    const DevelopDynamicRange::DevelopDynamicRangeRegionEvidence& regionEvidence,
    const DevelopDynamicRange::DevelopDynamicRangeStrategy& dynamicRangeStrategy,
    const DevelopSubjectImportance::DevelopSubjectSceneIntent& subjectSceneIntent,
    float darkness,
    float shadowRescueNeed,
    float hdrNeed,
    float flatSceneNeed,
    float underBrightBroadHighlightEv);

float DevelopAutoCandidateModeIntentFit(
    const std::string& candidateId,
    EditorNodeGraph::DevelopAutoIntent intent);

float DevelopAutoCandidateNearestSurvivorDistance(
    const DevelopAutoCandidateSolveResult& result,
    std::size_t candidateIndex);

bool RejectDevelopAutoCandidateForDamage(
    DevelopAutoCandidateSolve& candidate,
    const EditorNodeGraph::DevelopAutoGuidance& base,
    const DevelopDynamicRange::DevelopToneAutoStats& stats,
    EditorNodeGraph::DevelopAutoIntent intent);

std::uint64_t BuildDevelopAutoCandidateFingerprint(
    const DevelopAutoCandidateSolveResult& result,
    const DevelopDynamicRange::DevelopToneAutoStats& stats);

struct DevelopContinuationCandidateBiasProfile {
    bool active = false;
    std::string decision;
    std::string reason;
    std::string stageFocus;
    std::string refineIntent;
};

DevelopContinuationCandidateBiasProfile ResolveDevelopContinuationCandidateBiasProfile(
    const nlohmann::json& previousToneJson);
bool ApplyDevelopContinuationCandidateBias(
    DevelopAutoCandidateSolve& candidate,
    const DevelopContinuationCandidateBiasProfile& profile);

} // namespace Stack::Editor::DevelopCandidateScoring
