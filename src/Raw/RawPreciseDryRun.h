#pragma once

#include "Raw/RawOptimizerSelection.h"
#include "Raw/RawPreciseCandidateEngine.h"
#include "ThirdParty/json.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace Stack::PreciseDryRun {

inline constexpr int kDryRunSchemaVersion = 1;
inline constexpr const char* kDryRunSolverVersion = "raw-precise-dry-run-v1";
inline constexpr const char* kDryRunReportVersion = "raw-precise-dry-run-report-v1";
inline constexpr const char* kDryRunObjectivePolicyVersion =
    "raw-range-goal-policy-v1";

enum class StageGroup {
    RawExposure,
    LocalRange,
    FinishTone,
    DisplayFit
};

struct ObjectiveGoal {
    std::string termId;
    RawOptimizer::MeritTier tier = RawOptimizer::MeritTier::Tier2Technical;
    double lower = 0.0;
    double upper = 0.0;
    double meaningfulDelta = 0.0;
    bool required = true;
    StageGroup owningGroup = StageGroup::RawExposure;
    std::string units;
    std::string reason;
};

struct StagePolicy {
    StageGroup group = StageGroup::RawExposure;
    bool active = true;
    int maxUniqueEvaluations = 8;
    int maxAcceptedIterations = 4;
    std::string reason;
};

struct DryRunRequest {
    std::string solveId;
    RawRecipe::RawDevelopmentRecipe solverBaseRecipe;
    PreciseRaw::ParameterSpace parameterSpace;
    PreciseRaw::CandidateIdentityContext proxyIdentity;
    PreciseRaw::CandidateIdentityContext fullResolutionIdentity;
    PreciseRaw::CandidateOwnership ownership;
    RawOptimizer::SearchBudget totalBudget;
    std::array<StagePolicy, 4> stagePolicies;
    std::vector<ObjectiveGoal> objectiveGoals;
};

struct DryRunCallbacks {
    std::function<PreciseRaw::CandidateEvaluationRecord(
        const PreciseRaw::CandidateProposal& proposal)> evaluateProxy;
    std::function<PreciseRaw::CandidateEvaluationRecord(
        const PreciseRaw::CandidateProposal& proposal)> evaluateFullResolution;
    std::function<bool()> shouldCancel;
};

struct ParameterDecision {
    PreciseRaw::ParameterId id = PreciseRaw::ParameterId::RawExposureEv;
    StageGroup group = StageGroup::RawExposure;
    bool active = false;
    std::string visibleOwner;
    std::string reason;
};

struct StageSearchRecord {
    StageGroup group = StageGroup::RawExposure;
    bool attempted = false;
    bool skipped = false;
    std::vector<PreciseRaw::ParameterId> dimensions;
    RawOptimizer::SearchResult search;
    std::string reason;
};

struct DryRunResult {
    int schemaVersion = kDryRunSchemaVersion;
    std::string solverVersion = kDryRunSolverVersion;
    std::string reportVersion = kDryRunReportVersion;
    std::string objectivePolicyVersion = kDryRunObjectivePolicyVersion;
    std::string solveId;
    RawOptimizer::SolveStatus status = RawOptimizer::SolveStatus::CandidateRenderFailed;
    RawOptimizer::FullVerificationDisposition fullVerification =
        RawOptimizer::FullVerificationDisposition::NotRequested;
    PreciseRaw::CandidateProposal warmStart;
    PreciseRaw::CandidateProposal proxyFinalist;
    PreciseRaw::CandidateProposal selected;
    PreciseRaw::CandidateEvaluationRecord warmEvaluation;
    PreciseRaw::CandidateEvaluationRecord proxyFinalistEvaluation;
    PreciseRaw::CandidateEvaluationRecord fullResolutionEvaluation;
    PreciseRaw::FullResolutionPromotionRecord promotion;
    std::vector<ObjectiveGoal> objectiveGoals;
    std::vector<ParameterDecision> parameterDecisions;
    std::vector<StageSearchRecord> stages;
    std::vector<PreciseRaw::CandidateEvaluationRecord> proxyEvaluations;
    int proposedEvaluations = 0;
    int optimizerUniqueEvaluations = 0;
    int uniqueProxyEvaluations = 0;
    int proxyCacheHits = 0;
    int actualProxyRenderCount = 0;
    int acceptedIterations = 0;
    int rejectedCandidates = 0;
    int failedCandidates = 0;
    double proxySearchRuntimeMs = 0.0;
    bool budgetExhausted = false;
    bool meaningfulImprovement = false;
    bool fallbackToWarmStart = false;
    bool candidateRecipeRoundTripExact = false;
    bool candidateVisibleParametersRoundTripExact = false;
    bool currentRecipeUnchanged = true;
    bool undoHistoryUnchanged = true;
    bool projectDirtyStateUnchanged = true;
    bool recipeApplied = false;
    bool appliedPreviewTextureCreated = false;
    bool candidateEligibleForFutureApply = false;
    std::string reason;
    std::vector<std::string> warnings;
};

const char* StageGroupName(StageGroup group);
std::array<StagePolicy, 4> DefaultStagePolicies();

bool ValidateRequest(const DryRunRequest& request, std::string& reason);
RawOptimizer::SearchEvaluation BuildSearchEvaluation(
    const PreciseRaw::CandidateEvaluationRecord& evaluation,
    const std::vector<double>& point,
    const std::vector<ObjectiveGoal>& goals);
DryRunResult RunPreciseDryRun(
    const DryRunRequest& request,
    const DryRunCallbacks& callbacks);

nlohmann::json SerializeObjectiveGoal(const ObjectiveGoal& goal);
nlohmann::json SerializeStagePolicy(const StagePolicy& policy);
nlohmann::json SerializeParameterDecision(const ParameterDecision& decision);
nlohmann::json SerializeStageSearch(const StageSearchRecord& stage);
nlohmann::json SerializeDryRunResult(const DryRunResult& result);

} // namespace Stack::PreciseDryRun
