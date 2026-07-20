#include "Raw/RawPreciseDryRun.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

namespace Stack::PreciseDryRun {
namespace {

using PreciseRaw::CandidateEvaluationRecord;
using PreciseRaw::CandidateParameterVector;
using PreciseRaw::CandidateProposal;
using PreciseRaw::ConstraintResult;
using PreciseRaw::ConstraintStatus;
using PreciseRaw::ConstraintTier;
using PreciseRaw::EvaluationStatus;
using PreciseRaw::ObjectiveTerm;
using PreciseRaw::ObjectiveTier;
using PreciseRaw::ParameterId;
using RawOptimizer::EvaluationDisposition;
using RawOptimizer::FullVerificationDisposition;
using RawOptimizer::MeritComponent;
using RawOptimizer::MeritRole;
using RawOptimizer::MeritTier;
using RawOptimizer::SearchEvaluation;
using RawOptimizer::SearchProblem;
using RawOptimizer::SolveStatus;

constexpr double kEpsilon = 1.0e-12;

std::size_t ParameterIndex(ParameterId id) {
    return static_cast<std::size_t>(id);
}

int GroupOrder(StageGroup group) {
    switch (group) {
        case StageGroup::RawExposure: return 0;
        case StageGroup::LocalRange: return 1;
        case StageGroup::FinishTone: return 2;
        case StageGroup::DisplayFit: return 3;
    }
    return 4;
}

MeritTier ConstraintMeritTier(ConstraintTier tier) {
    return tier == ConstraintTier::Tier0IdentityState
        ? MeritTier::Tier0IdentityState
        : MeritTier::Tier1RawArtifact;
}

MeritTier ObjectiveMeritTier(ObjectiveTier tier) {
    switch (tier) {
        case ObjectiveTier::Tier2Technical: return MeritTier::Tier2Technical;
        case ObjectiveTier::Tier3Display: return MeritTier::Tier3Display;
        case ObjectiveTier::Tier5TieBreaker: return MeritTier::Tier5TieBreaker;
    }
    return MeritTier::Tier2Technical;
}

int MeritTierOrder(MeritTier tier) {
    switch (tier) {
        case MeritTier::Tier0IdentityState: return 0;
        case MeritTier::Tier1RawArtifact: return 1;
        case MeritTier::Tier2Technical: return 2;
        case MeritTier::Tier3Display: return 3;
        case MeritTier::Tier5TieBreaker: return 5;
    }
    return 6;
}

EvaluationDisposition EvaluationDispositionFor(
    const CandidateEvaluationRecord& evaluation,
    bool requiredEvidenceMissing,
    bool uncertaintyAdjustedHardFailure) {
    switch (evaluation.status) {
        case EvaluationStatus::Complete:
            if (requiredEvidenceMissing) return EvaluationDisposition::MissingEvidence;
            return uncertaintyAdjustedHardFailure
                ? EvaluationDisposition::Rejected
                : EvaluationDisposition::Complete;
        case EvaluationStatus::Rejected: return EvaluationDisposition::Rejected;
        case EvaluationStatus::Failed: return EvaluationDisposition::Failed;
        case EvaluationStatus::Canceled:
        case EvaluationStatus::Stale: return EvaluationDisposition::Canceled;
        case EvaluationStatus::Pending: return EvaluationDisposition::MissingEvidence;
    }
    return EvaluationDisposition::Failed;
}

double ConstraintViolation(const ConstraintResult& constraint) {
    if (constraint.status == ConstraintStatus::Passed) return 0.0;
    if (constraint.status == ConstraintStatus::Unavailable) {
        return std::numeric_limits<double>::infinity();
    }
    if (std::isfinite(constraint.value) && std::isfinite(constraint.limit)) {
        if (constraint.id == "raw.wb_scaled_headroom") {
            const double uncertaintyAdjustedLimit =
                constraint.limit - std::max(0.0, constraint.uncertainty01);
            return std::max(0.0, constraint.value - uncertaintyAdjustedLimit);
        }
        const double difference = std::abs(constraint.value - constraint.limit);
        if (difference > kEpsilon) return difference;
    }
    return 1.0;
}

bool UncertaintyAdjustedHardFailure(const CandidateEvaluationRecord& evaluation) {
    for (const ConstraintResult& constraint : evaluation.constraints) {
        if (constraint.id != "raw.wb_scaled_headroom" ||
            constraint.status == ConstraintStatus::Unavailable ||
            !std::isfinite(constraint.value) || !std::isfinite(constraint.limit)) {
            continue;
        }
        const double adjustedLimit =
            constraint.limit - std::max(0.0, constraint.uncertainty01);
        if (constraint.value > adjustedLimit + kEpsilon) return true;
    }
    return false;
}

const ObjectiveTerm* FindTerm(
    const CandidateEvaluationRecord& evaluation,
    const std::string& id) {
    const auto found = std::find_if(
        evaluation.terms.begin(), evaluation.terms.end(), [&](const ObjectiveTerm& term) {
            return term.id == id;
        });
    return found == evaluation.terms.end() ? nullptr : &*found;
}

double GoalGap(double value, double lower, double upper) {
    if (value < lower) return lower - value;
    if (value > upper) return value - upper;
    return 0.0;
}

bool RequiredConstraintUnavailable(const CandidateEvaluationRecord& evaluation) {
    return std::any_of(
        evaluation.constraints.begin(), evaluation.constraints.end(),
        [](const ConstraintResult& constraint) {
            return constraint.status == ConstraintStatus::Unavailable;
        });
}

bool RequiredGoalMissing(
    const CandidateEvaluationRecord& evaluation,
    const std::vector<ObjectiveGoal>& goals) {
    for (const ObjectiveGoal& goal : goals) {
        if (!goal.required) continue;
        const ObjectiveTerm* term = FindTerm(evaluation, goal.termId);
        if (!term || !term->valid || !std::isfinite(term->value)) return true;
    }
    return false;
}

bool GoalsSatisfied(
    const CandidateEvaluationRecord& evaluation,
    const std::vector<ObjectiveGoal>& goals) {
    for (const ObjectiveGoal& goal : goals) {
        if (!goal.required) continue;
        const ObjectiveTerm* term = FindTerm(evaluation, goal.termId);
        if (!term || !term->valid || !std::isfinite(term->value)) return false;
        const double tolerance = std::max(goal.meaningfulDelta, term->uncertainty01);
        if (GoalGap(term->value, goal.lower, goal.upper) > tolerance + kEpsilon) return false;
    }
    return true;
}

std::vector<ParameterId> GroupDimensions(StageGroup group) {
    switch (group) {
        case StageGroup::RawExposure:
            return { ParameterId::RawExposureEv };
        case StageGroup::LocalRange:
            return {
                ParameterId::LocalTargetEv,
                ParameterId::LocalDeltaEv,
                ParameterId::LocalWidthEv,
                ParameterId::LocalFeather
            };
        case StageGroup::FinishTone:
            return { ParameterId::FinishY1, ParameterId::FinishY2, ParameterId::FinishY3 };
        case StageGroup::DisplayFit:
            return {
                ParameterId::DisplayBlackEv,
                ParameterId::DisplayWhiteEv,
                ParameterId::DisplayMiddleGrey,
                ParameterId::DisplayShoulder,
                ParameterId::DisplayToe
            };
    }
    return {};
}

bool GroupUserOwned(
    StageGroup group,
    const PreciseRaw::CandidateOwnership& ownership) {
    switch (group) {
        case StageGroup::RawExposure: return ownership.rawExposureUserOwned;
        case StageGroup::LocalRange: return ownership.localRangeUserOwned;
        case StageGroup::FinishTone: return ownership.finishToneUserOwned;
        case StageGroup::DisplayFit: return ownership.displayFitUserOwned;
    }
    return true;
}

std::string VisibleOwner(ParameterId id) {
    switch (id) {
        case ParameterId::RawExposureEv: return "RAW Exposure slider";
        case ParameterId::LocalTargetEv:
        case ParameterId::LocalDeltaEv:
        case ParameterId::LocalWidthEv:
        case ParameterId::LocalFeather: return "Local Range graph and luminance-range mask controls";
        case ParameterId::FinishY1:
        case ParameterId::FinishY2:
        case ParameterId::FinishY3: return "Finish Tone graph";
        case ParameterId::DisplayBlackEv:
        case ParameterId::DisplayWhiteEv:
        case ParameterId::DisplayMiddleGrey:
        case ParameterId::DisplayShoulder:
        case ParameterId::DisplayToe: return "View Transform / Display Fit controls";
        case ParameterId::Count: break;
    }
    return "unmapped";
}

StageGroup GroupFor(ParameterId id) {
    switch (id) {
        case ParameterId::RawExposureEv: return StageGroup::RawExposure;
        case ParameterId::LocalTargetEv:
        case ParameterId::LocalDeltaEv:
        case ParameterId::LocalWidthEv:
        case ParameterId::LocalFeather: return StageGroup::LocalRange;
        case ParameterId::FinishY1:
        case ParameterId::FinishY2:
        case ParameterId::FinishY3: return StageGroup::FinishTone;
        case ParameterId::DisplayBlackEv:
        case ParameterId::DisplayWhiteEv:
        case ParameterId::DisplayMiddleGrey:
        case ParameterId::DisplayShoulder:
        case ParameterId::DisplayToe: return StageGroup::DisplayFit;
        case ParameterId::Count: break;
    }
    return StageGroup::RawExposure;
}

const StagePolicy* FindPolicy(
    const std::array<StagePolicy, 4>& policies,
    StageGroup group) {
    const auto found = std::find_if(policies.begin(), policies.end(), [&](const StagePolicy& policy) {
        return policy.group == group;
    });
    return found == policies.end() ? nullptr : &*found;
}

bool SameParameters(
    const CandidateParameterVector& a,
    const CandidateParameterVector& b) {
    return PreciseRaw::SerializeParameters(a).dump() ==
        PreciseRaw::SerializeParameters(b).dump();
}

bool VisibleRoundTripExact(const CandidateProposal& proposal, bool& parameterExact) {
    parameterExact = false;
    if (!proposal.valid) return false;
    const nlohmann::json serialized = RawRecipe::SerializeRecipe(proposal.recipe);
    const RawRecipe::RawDevelopmentRecipe loaded = RawRecipe::DeserializeRecipe(serialized);
    const bool recipeExact = PreciseRaw::CanonicalRecipeBytes(loaded) ==
        PreciseRaw::CanonicalRecipeBytes(proposal.recipe);
    parameterExact = SameParameters(
        PreciseRaw::ExtractParameters(loaded),
        PreciseRaw::ExtractParameters(proposal.recipe));
    return recipeExact &&
        PreciseRaw::RecipeIdentity(loaded) == proposal.candidateRecipeIdentity;
}

SearchProblem BuildProblem(
    const DryRunRequest& request,
    StageGroup group,
    const CandidateParameterVector& warm,
    std::vector<ParameterId>& activeDimensions) {
    SearchProblem problem;
    problem.id = request.solveId + "/" + StageGroupName(group);
    for (ParameterId id : GroupDimensions(group)) {
        const PreciseRaw::ParameterRange& range =
            request.parameterSpace.ranges[ParameterIndex(id)];
        if (!range.active) continue;
        RawOptimizer::DimensionSpec dimension;
        dimension.id = PreciseRaw::ParameterStableString(id);
        dimension.lower = range.lower;
        dimension.upper = range.upper;
        dimension.active = true;
        dimension.stageOrder = GroupOrder(group);
        problem.dimensions.push_back(std::move(dimension));
        problem.warmStart.push_back(PreciseRaw::GetParameter(warm, id));
        activeDimensions.push_back(id);
    }
    auto localIndex = [&](ParameterId id) -> std::size_t {
        const auto found = std::find(activeDimensions.begin(), activeDimensions.end(), id);
        return found == activeDimensions.end()
            ? activeDimensions.size()
            : static_cast<std::size_t>(std::distance(activeDimensions.begin(), found));
    };
    auto addPair = [&](ParameterId a, ParameterId b) {
        const std::size_t ia = localIndex(a);
        const std::size_t ib = localIndex(b);
        if (ia < activeDimensions.size() && ib < activeDimensions.size()) {
            problem.interactionPairs.push_back({ ia, ib });
        }
    };
    if (group == StageGroup::LocalRange) {
        addPair(ParameterId::LocalTargetEv, ParameterId::LocalDeltaEv);
        addPair(ParameterId::LocalDeltaEv, ParameterId::LocalWidthEv);
        addPair(ParameterId::LocalDeltaEv, ParameterId::LocalFeather);
    } else if (group == StageGroup::FinishTone) {
        addPair(ParameterId::FinishY1, ParameterId::FinishY3);
    } else if (group == StageGroup::DisplayFit) {
        addPair(ParameterId::DisplayWhiteEv, ParameterId::DisplayShoulder);
        addPair(ParameterId::DisplayBlackEv, ParameterId::DisplayToe);
    }
    return problem;
}

RawOptimizer::SearchBudget StageBudget(
    const DryRunRequest& request,
    const StagePolicy& policy,
    double remainingRuntimeMs) {
    RawOptimizer::SearchBudget budget = request.totalBudget;
    budget.maxUniqueEvaluations = policy.maxUniqueEvaluations;
    budget.maxAcceptedIterations = policy.maxAcceptedIterations;
    budget.maxRuntimeMs = std::max(1.0, remainingRuntimeMs);
    budget.requireFullResolutionVerification = false;
    return budget;
}

bool TerminalFailure(SolveStatus status) {
    return status == SolveStatus::BlockedByMissingEvidence ||
        status == SolveStatus::Canceled ||
        status == SolveStatus::CandidateRenderFailed ||
        status == SolveStatus::FullResolutionVerificationRejected;
}

FullVerificationDisposition FullDisposition(
    const SearchEvaluation& evaluation) {
    switch (evaluation.disposition) {
        case EvaluationDisposition::Complete:
            return evaluation.hardConstraintsPassed
                ? FullVerificationDisposition::Accepted
                : FullVerificationDisposition::Rejected;
        case EvaluationDisposition::Rejected: return FullVerificationDisposition::Rejected;
        case EvaluationDisposition::Failed: return FullVerificationDisposition::Failed;
        case EvaluationDisposition::Canceled: return FullVerificationDisposition::Canceled;
        case EvaluationDisposition::MissingEvidence:
            return FullVerificationDisposition::Failed;
    }
    return FullVerificationDisposition::Failed;
}

nlohmann::json MeritDeltaByTier(
    const CandidateEvaluationRecord& warm,
    const CandidateEvaluationRecord& selected,
    const std::vector<ObjectiveGoal>& goals) {
    const SearchEvaluation a = BuildSearchEvaluation(warm, {}, goals);
    const SearchEvaluation b = BuildSearchEvaluation(selected, {}, goals);
    nlohmann::json result = nlohmann::json::object();
    if (a.merit.size() != b.merit.size()) return result;
    for (std::size_t i = 0; i < a.merit.size(); ++i) {
        if (a.merit[i].id != b.merit[i].id || !a.merit[i].valid || !b.merit[i].valid) continue;
        const std::string tier = RawOptimizer::MeritTierName(b.merit[i].tier);
        result[tier][b.merit[i].id] = {
            { "warm", a.merit[i].value },
            { "selected", b.merit[i].value },
            { "improvement", a.merit[i].value - b.merit[i].value },
            { "units", b.merit[i].units }
        };
    }
    return result;
}

} // namespace

const char* StageGroupName(StageGroup group) {
    switch (group) {
        case StageGroup::RawExposure: return "raw-exposure";
        case StageGroup::LocalRange: return "local-range";
        case StageGroup::FinishTone: return "finish-tone";
        case StageGroup::DisplayFit: return "display-fit";
    }
    return "unknown";
}

std::array<StagePolicy, 4> DefaultStagePolicies() {
    return {
        StagePolicy { StageGroup::RawExposure, true, 8, 4,
            "Frozen Phase 04 RAW Exposure quota." },
        StagePolicy { StageGroup::LocalRange, true, 16, 4,
            "Frozen Phase 04 Local Range quota." },
        StagePolicy { StageGroup::FinishTone, true, 12, 4,
            "Frozen Phase 04 Finish Tone quota." },
        StagePolicy { StageGroup::DisplayFit, true, 12, 4,
            "Frozen Phase 04 Display Fit quota." }
    };
}

bool ValidateRequest(const DryRunRequest& request, std::string& reason) {
    if (request.solveId.empty() ||
        request.parameterSpace.version != PreciseRaw::kParameterSpaceVersion ||
        request.totalBudget.version != RawOptimizer::kEvaluationBudgetVersion) {
        reason = "Solve identity, parameter-space version, or budget version is invalid.";
        return false;
    }
    const std::string baseIdentity = PreciseRaw::RecipeIdentity(request.solverBaseRecipe);
    const auto identityValid = [&](const PreciseRaw::CandidateIdentityContext& identity) {
        return identity.baseRecipeIdentity == baseIdentity &&
            !identity.sourceIdentity.empty() && !identity.decodeIdentity.empty() &&
            !identity.rawEvidenceIdentity.empty() && !identity.rendererIdentity.empty() &&
            !identity.proxyIdentity.empty() && !identity.budgetIdentity.empty() &&
            !identity.ownershipIdentity.empty() &&
            identity.featureVersion == RenderedFeatures::kRenderedFeatureVersion;
    };
    if (!identityValid(request.proxyIdentity) ||
        !identityValid(request.fullResolutionIdentity) ||
        request.proxyIdentity.sourceIdentity != request.fullResolutionIdentity.sourceIdentity ||
        request.proxyIdentity.decodeIdentity != request.fullResolutionIdentity.decodeIdentity ||
        request.proxyIdentity.rawEvidenceIdentity != request.fullResolutionIdentity.rawEvidenceIdentity ||
        request.proxyIdentity.baseRecipeIdentity != request.fullResolutionIdentity.baseRecipeIdentity ||
        request.proxyIdentity.rendererIdentity != request.fullResolutionIdentity.rendererIdentity ||
        request.proxyIdentity.featureVersion != request.fullResolutionIdentity.featureVersion ||
        request.proxyIdentity.budgetIdentity != request.fullResolutionIdentity.budgetIdentity ||
        request.proxyIdentity.ownershipIdentity != request.fullResolutionIdentity.ownershipIdentity ||
        request.proxyIdentity.generation != request.fullResolutionIdentity.generation ||
        request.proxyIdentity.proxyIdentity == request.fullResolutionIdentity.proxyIdentity) {
        reason = "Proxy/full identity contexts are incomplete, stale, or not independently versioned.";
        return false;
    }
    std::set<StageGroup> groups;
    int quota = 0;
    int acceptedQuota = 0;
    for (std::size_t i = 0; i < request.stagePolicies.size(); ++i) {
        const StagePolicy& policy = request.stagePolicies[i];
        if (GroupOrder(policy.group) != static_cast<int>(i)) {
            reason = "Stage policies must preserve RAW Exposure, Local Range, Finish Tone, Display Fit order.";
            return false;
        }
        groups.insert(policy.group);
        if (policy.maxUniqueEvaluations <= 0 || policy.maxAcceptedIterations <= 0) {
            reason = "Every stage policy requires positive evaluation and accepted-iteration quotas.";
            return false;
        }
        quota += policy.maxUniqueEvaluations;
        acceptedQuota += policy.maxAcceptedIterations;
    }
    if (groups.size() != 4 || quota > request.totalBudget.maxUniqueEvaluations ||
        acceptedQuota > request.totalBudget.maxAcceptedIterations) {
        reason = "Stage policies must cover each group once and remain inside the frozen total budget.";
        return false;
    }
    if (!request.totalBudget.requireFullResolutionVerification) {
        reason = "The selected Phase 05 budget must require independent full-resolution verification.";
        return false;
    }
    for (const ObjectiveGoal& goal : request.objectiveGoals) {
        if (goal.termId.empty() || !std::isfinite(goal.lower) || !std::isfinite(goal.upper) ||
            goal.upper < goal.lower || !std::isfinite(goal.meaningfulDelta) ||
            goal.meaningfulDelta < 0.0) {
            reason = "Objective goal identity, range, or meaningful delta is invalid.";
            return false;
        }
    }
    for (std::size_t i = 0; i < request.parameterSpace.ranges.size(); ++i) {
        const PreciseRaw::ParameterRange& range = request.parameterSpace.ranges[i];
        if (range.id != static_cast<ParameterId>(i) ||
            range.lower > range.upper || !std::isfinite(range.warm)) {
            reason = "Parameter-space ordering or bounds are invalid.";
            return false;
        }
    }
    return true;
}

SearchEvaluation BuildSearchEvaluation(
    const CandidateEvaluationRecord& evaluation,
    const std::vector<double>& point,
    const std::vector<ObjectiveGoal>& goals) {
    SearchEvaluation result;
    result.candidateId = evaluation.proposal.candidateId;
    result.point = point;
    result.runtimeMs = evaluation.render.renderRuntimeMs + evaluation.render.featureRuntimeMs;
    const bool requiredMissing = RequiredConstraintUnavailable(evaluation) ||
        RequiredGoalMissing(evaluation, goals);
    const bool uncertaintyAdjustedFailure =
        UncertaintyAdjustedHardFailure(evaluation);
    result.disposition = EvaluationDispositionFor(
        evaluation, requiredMissing, uncertaintyAdjustedFailure);
    result.hardConstraintsPassed = evaluation.status == EvaluationStatus::Complete &&
        !requiredMissing && !uncertaintyAdjustedFailure &&
        std::none_of(evaluation.constraints.begin(), evaluation.constraints.end(),
            [](const ConstraintResult& constraint) {
                return constraint.status == ConstraintStatus::Failed;
            });
    result.reason = evaluation.rejectionReason;
    for (const ConstraintResult& constraint : evaluation.constraints) {
        MeritComponent merit;
        merit.id = "constraint." + constraint.id;
        merit.tier = ConstraintMeritTier(constraint.tier);
        merit.role = MeritRole::ConstraintViolation;
        merit.valid = constraint.status != ConstraintStatus::Unavailable;
        merit.value = merit.valid ? ConstraintViolation(constraint) : 0.0;
        merit.uncertainty = constraint.uncertainty01;
        merit.units = constraint.units;
        merit.reason = constraint.reason;
        result.merit.push_back(std::move(merit));
    }
    for (const ObjectiveGoal& goal : goals) {
        const ObjectiveTerm* term = FindTerm(evaluation, goal.termId);
        MeritComponent merit;
        merit.id = "goal." + goal.termId;
        merit.tier = goal.tier;
        merit.role = MeritRole::GoalViolation;
        merit.valid = term && term->valid && std::isfinite(term->value);
        merit.value = merit.valid ? GoalGap(term->value, goal.lower, goal.upper) : 0.0;
        merit.meaningfulDelta = goal.meaningfulDelta;
        merit.uncertainty = term ? term->uncertainty01 : 1.0;
        merit.units = goal.units;
        merit.reason = goal.reason;
        result.merit.push_back(std::move(merit));
    }
    if (goals.empty()) {
        MeritComponent declaredNone;
        declaredNone.id = "goal.no-declared-photographic-target";
        declaredNone.tier = MeritTier::Tier2Technical;
        declaredNone.role = MeritRole::GoalViolation;
        declaredNone.valid = true;
        declaredNone.value = 0.0;
        declaredNone.reason =
            "Phase 05 may restore hard feasibility but does not invent a universal brightness or aesthetic target.";
        result.merit.push_back(std::move(declaredNone));
    }
    for (const ObjectiveTerm& term : evaluation.terms) {
        if (term.tier != ObjectiveTier::Tier5TieBreaker || !term.valid) continue;
        MeritComponent merit;
        merit.id = term.id;
        merit.tier = ObjectiveMeritTier(term.tier);
        merit.role = MeritRole::TieBreaker;
        merit.valid = std::isfinite(term.value);
        merit.value = term.value;
        merit.uncertainty = term.uncertainty01;
        merit.units = term.units;
        merit.reason = term.reason;
        result.merit.push_back(std::move(merit));
    }
    std::stable_sort(result.merit.begin(), result.merit.end(), [](const MeritComponent& a, const MeritComponent& b) {
        const int tierA = MeritTierOrder(a.tier);
        const int tierB = MeritTierOrder(b.tier);
        if (tierA != tierB) return tierA < tierB;
        if (a.role != b.role) return static_cast<int>(a.role) < static_cast<int>(b.role);
        return a.id < b.id;
    });
    return result;
}

DryRunResult RunPreciseDryRun(
    const DryRunRequest& request,
    const DryRunCallbacks& callbacks) {
    DryRunResult result;
    result.solveId = request.solveId;
    result.objectiveGoals = request.objectiveGoals;
    std::string validationReason;
    if (!ValidateRequest(request, validationReason)) {
        result.reason = validationReason;
        return result;
    }
    if (!callbacks.evaluateProxy || !callbacks.evaluateFullResolution) {
        result.status = SolveStatus::BlockedByMissingEvidence;
        result.reason = "Proxy and full-resolution evaluators are required.";
        return result;
    }
    if (callbacks.shouldCancel && callbacks.shouldCancel()) {
        result.status = SolveStatus::Canceled;
        result.reason = "Dry run canceled before the warm-start evaluation; no candidate was rendered or applied.";
        return result;
    }
    if (request.objectiveGoals.empty()) {
        result.warnings.push_back(
            "No accepted lower-tier photographic goal ranges are active; the dry run may restore hard feasibility and otherwise prefers the smallest visible edit.");
    }
    const auto searchStarted = std::chrono::steady_clock::now();
    auto elapsedSearchMs = [&]() {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - searchStarted).count();
    };

    for (int i = 0; i < static_cast<int>(ParameterId::Count); ++i) {
        const ParameterId id = static_cast<ParameterId>(i);
        const StageGroup group = GroupFor(id);
        const StagePolicy* policy = FindPolicy(request.stagePolicies, group);
        ParameterDecision decision;
        decision.id = id;
        decision.group = group;
        decision.visibleOwner = VisibleOwner(id);
        const bool userOwned = GroupUserOwned(group, request.ownership);
        const bool rangeActive = request.parameterSpace.ranges[ParameterIndex(id)].active;
        decision.active = policy && policy->active && rangeActive && !userOwned;
        if (userOwned) decision.reason = "Withheld because the visible control is user-owned.";
        else if (!rangeActive) decision.reason = request.parameterSpace.ranges[ParameterIndex(id)].reason;
        else if (!policy || !policy->active) decision.reason = policy ? policy->reason : "Stage policy is missing.";
        else decision.reason = policy->reason;
        result.parameterDecisions.push_back(std::move(decision));
    }

    std::unordered_map<std::string, std::size_t> cache;
    std::unordered_map<std::string, CandidateProposal> proposals;
    bool internalCanceled = false;
    auto evaluateParameters = [&](const CandidateParameterVector& parameters,
                                  const std::vector<double>& point,
                                  StageGroup group) -> SearchEvaluation {
        PreciseRaw::CandidateIdentityContext identity = request.proxyIdentity;
        const CandidateProposal proposal = PreciseRaw::BuildCandidateProposal(
            request.solverBaseRecipe,
            parameters,
            std::move(identity),
            request.ownership,
            std::string("Phase 05 non-applying ") + StageGroupName(group) + " candidate.");
        proposals[proposal.candidateId] = proposal;
        const auto cached = cache.find(proposal.candidateId);
        if (cached != cache.end()) {
            ++result.proxyCacheHits;
            CandidateEvaluationRecord evaluation = result.proxyEvaluations[cached->second];
            evaluation.cacheHit = true;
            SearchEvaluation search = BuildSearchEvaluation(evaluation, point, request.objectiveGoals);
            search.fromCache = true;
            return search;
        }
        CandidateEvaluationRecord evaluation = callbacks.evaluateProxy(proposal);
        if (evaluation.proposal.candidateId != proposal.candidateId) {
            evaluation.status = EvaluationStatus::Stale;
            evaluation.proposal = proposal;
            evaluation.rejectionReason = "Proxy evaluator returned a different candidate identity.";
        }
        const bool cacheable = evaluation.status != EvaluationStatus::Canceled &&
            evaluation.status != EvaluationStatus::Stale;
        result.proxyEvaluations.push_back(evaluation);
        const std::size_t index = result.proxyEvaluations.size() - 1;
        if (cacheable) cache[proposal.candidateId] = index;
        if (evaluation.render.attempted) ++result.actualProxyRenderCount;
        if (evaluation.status == EvaluationStatus::Rejected) ++result.rejectedCandidates;
        if (evaluation.status == EvaluationStatus::Failed) ++result.failedCandidates;
        if (evaluation.status == EvaluationStatus::Canceled ||
            evaluation.status == EvaluationStatus::Stale) {
            internalCanceled = true;
        }
        return BuildSearchEvaluation(evaluation, point, request.objectiveGoals);
    };

    CandidateParameterVector accepted = PreciseRaw::ExtractParameters(request.solverBaseRecipe);
    const SearchEvaluation warmSearch = evaluateParameters(
        accepted, { accepted.rawExposureEv }, StageGroup::RawExposure);
    result.warmStart = proposals[warmSearch.candidateId];
    const auto warmCached = cache.find(warmSearch.candidateId);
    if (warmCached != cache.end()) result.warmEvaluation = result.proxyEvaluations[warmCached->second];
    if (warmSearch.disposition == EvaluationDisposition::Canceled) {
        result.status = SolveStatus::Canceled;
        result.reason = "Warm-start evaluation was canceled or became stale.";
        return result;
    }
    if (warmSearch.disposition == EvaluationDisposition::Failed) {
        result.status = SolveStatus::CandidateRenderFailed;
        result.reason = "Pass 94 warm-start candidate render failed.";
        return result;
    }
    if (warmSearch.disposition == EvaluationDisposition::MissingEvidence) {
        result.status = SolveStatus::BlockedByMissingEvidence;
        result.reason = "Pass 94 warm-start candidate lacks required constraint or goal evidence.";
        return result;
    }

    bool terminal = false;
    for (const StagePolicy& policy : request.stagePolicies) {
        StageSearchRecord stage;
        stage.group = policy.group;
        const bool userOwned = GroupUserOwned(policy.group, request.ownership);
        if (!policy.active || userOwned) {
            stage.skipped = true;
            stage.reason = userOwned
                ? "Stage withheld because its visible control group is user-owned."
                : policy.reason;
            result.stages.push_back(std::move(stage));
            continue;
        }
        const double remainingRuntimeMs =
            request.totalBudget.maxRuntimeMs - elapsedSearchMs();
        if (remainingRuntimeMs <= 0.0) {
            stage.skipped = true;
            stage.reason = "Frozen total proxy-search wall-time budget exhausted before this stage.";
            result.budgetExhausted = true;
            result.stages.push_back(std::move(stage));
            continue;
        }
        SearchProblem problem = BuildProblem(
            request, policy.group, accepted, stage.dimensions);
        if (stage.dimensions.empty()) {
            stage.skipped = true;
            stage.reason = "No active bounded visible dimension is available in this stage.";
            result.stages.push_back(std::move(stage));
            continue;
        }
        stage.attempted = true;
        CandidateParameterVector stageBase = accepted;
        RawOptimizer::SearchCallbacks optimizerCallbacks;
        optimizerCallbacks.shouldCancel = [&]() {
            return internalCanceled || (callbacks.shouldCancel && callbacks.shouldCancel());
        };
        optimizerCallbacks.evaluate = [&](const std::vector<double>& point) {
            CandidateParameterVector parameters = stageBase;
            for (std::size_t i = 0; i < stage.dimensions.size(); ++i) {
                PreciseRaw::SetParameter(parameters, stage.dimensions[i], point[i]);
            }
            return evaluateParameters(parameters, point, policy.group);
        };
        stage.search = RawOptimizer::RunStageOrderedPatternSearch(
            problem, StageBudget(request, policy, remainingRuntimeMs), optimizerCallbacks);
        result.proposedEvaluations += stage.search.proposedEvaluations;
        result.optimizerUniqueEvaluations += stage.search.uniqueEvaluations;
        result.acceptedIterations += stage.search.acceptedIterations;
        result.budgetExhausted = result.budgetExhausted ||
            stage.search.status == SolveStatus::SafeImprovementBudgetExhausted;
        stage.reason = stage.search.reason;
        const auto selectedProposal = proposals.find(stage.search.selected.candidateId);
        if (selectedProposal != proposals.end() &&
            stage.search.selected.disposition == EvaluationDisposition::Complete &&
            stage.search.selected.hardConstraintsPassed) {
            accepted = PreciseRaw::ExtractParameters(selectedProposal->second.recipe);
        }
        if (TerminalFailure(stage.search.status)) {
            result.status = stage.search.status;
            result.reason = stage.search.reason;
            terminal = true;
        }
        result.stages.push_back(std::move(stage));
        if (terminal) break;
    }

    result.proxySearchRuntimeMs = elapsedSearchMs();

    result.uniqueProxyEvaluations = static_cast<int>(result.proxyEvaluations.size());
    result.currentRecipeUnchanged = std::all_of(
        result.proxyEvaluations.begin(), result.proxyEvaluations.end(),
        [](const CandidateEvaluationRecord& evaluation) {
            return evaluation.currentRecipeUnchanged;
        });
    result.undoHistoryUnchanged = std::all_of(
        result.proxyEvaluations.begin(), result.proxyEvaluations.end(),
        [](const CandidateEvaluationRecord& evaluation) {
            return evaluation.undoHistoryUnchanged;
        });
    result.projectDirtyStateUnchanged = std::all_of(
        result.proxyEvaluations.begin(), result.proxyEvaluations.end(),
        [](const CandidateEvaluationRecord& evaluation) {
            return evaluation.projectDirtyStateUnchanged;
        });
    if (terminal) return result;

    PreciseRaw::CandidateIdentityContext finalistIdentity = request.proxyIdentity;
    result.proxyFinalist = PreciseRaw::BuildCandidateProposal(
        request.solverBaseRecipe,
        accepted,
        std::move(finalistIdentity),
        request.ownership,
        "Phase 05 proxy finalist; no recipe was applied.");
    const auto finalistRecord = cache.find(result.proxyFinalist.candidateId);
    if (finalistRecord == cache.end()) {
        (void)evaluateParameters(accepted, {}, StageGroup::DisplayFit);
    }
    const auto finalistFound = cache.find(result.proxyFinalist.candidateId);
    if (finalistFound == cache.end()) {
        result.status = SolveStatus::BlockedByMissingEvidence;
        result.reason = "Proxy finalist evaluation is unavailable.";
        return result;
    }
    result.proxyFinalistEvaluation = result.proxyEvaluations[finalistFound->second];
    result.uniqueProxyEvaluations = static_cast<int>(result.proxyEvaluations.size());
    result.currentRecipeUnchanged = std::all_of(
        result.proxyEvaluations.begin(), result.proxyEvaluations.end(),
        [](const CandidateEvaluationRecord& evaluation) {
            return evaluation.currentRecipeUnchanged;
        });
    result.undoHistoryUnchanged = std::all_of(
        result.proxyEvaluations.begin(), result.proxyEvaluations.end(),
        [](const CandidateEvaluationRecord& evaluation) {
            return evaluation.undoHistoryUnchanged;
        });
    result.projectDirtyStateUnchanged = std::all_of(
        result.proxyEvaluations.begin(), result.proxyEvaluations.end(),
        [](const CandidateEvaluationRecord& evaluation) {
            return evaluation.projectDirtyStateUnchanged;
        });
    const SearchEvaluation proxyFinalistSearch = BuildSearchEvaluation(
        result.proxyFinalistEvaluation, {}, request.objectiveGoals);
    const SearchEvaluation warmFinalSearch = BuildSearchEvaluation(
        result.warmEvaluation, {}, request.objectiveGoals);
    result.meaningfulImprovement =
        RawOptimizer::CompareEvaluations(proxyFinalistSearch, warmFinalSearch) ==
        RawOptimizer::ComparisonResult::Better;

    PreciseRaw::CandidateIdentityContext fullIdentity = request.fullResolutionIdentity;
    const CandidateParameterVector visibleFinalist =
        PreciseRaw::ExtractParameters(result.proxyFinalist.recipe);
    const CandidateProposal fullProposal = PreciseRaw::BuildCandidateProposal(
        request.solverBaseRecipe,
        visibleFinalist,
        std::move(fullIdentity),
        request.ownership,
        "Phase 05 independent full-resolution verification; no recipe was applied.");
    result.fullResolutionEvaluation = callbacks.evaluateFullResolution(fullProposal);
    if (result.fullResolutionEvaluation.proposal.candidateId != fullProposal.candidateId) {
        result.fullResolutionEvaluation.status = EvaluationStatus::Stale;
        result.fullResolutionEvaluation.proposal = fullProposal;
        result.fullResolutionEvaluation.rejectionReason =
            "Full-resolution evaluator returned a different candidate identity.";
    }
    const SearchEvaluation fullSearch = BuildSearchEvaluation(
        result.fullResolutionEvaluation, {}, request.objectiveGoals);
    result.fullVerification = FullDisposition(fullSearch);
    result.promotion = PreciseRaw::CompareProxyAndFullResolution(
        result.proxyFinalist,
        result.proxyFinalistEvaluation.render,
        result.fullResolutionEvaluation.render);
    const bool sameRecipe = fullProposal.candidateRecipeIdentity ==
        result.proxyFinalist.candidateRecipeIdentity;
    const bool fullAccepted =
        proxyFinalistSearch.disposition == EvaluationDisposition::Complete &&
        proxyFinalistSearch.hardConstraintsPassed &&
        result.fullVerification == FullVerificationDisposition::Accepted &&
        fullSearch.hardConstraintsPassed && GoalsSatisfied(
            result.fullResolutionEvaluation, request.objectiveGoals) &&
        result.promotion.valid && sameRecipe;
    if (fullAccepted) {
        result.selected = result.proxyFinalist;
        result.proxyFinalistEvaluation = result.proxyEvaluations[finalistFound->second];
        if (result.meaningfulImprovement) {
            result.status = result.budgetExhausted
                ? SolveStatus::SafeImprovementBudgetExhausted
                : SolveStatus::Converged;
            result.reason = result.budgetExhausted
                ? "A full-resolution-verified safe improvement was found before a stage budget ended; budget exhaustion is not convergence."
                : "The complete candidate is a full-resolution-verified lexicographic improvement over Pass 94.";
        } else {
            result.status = SolveStatus::WarmStartRetained;
            result.reason = "No meaningful safe candidate displaced the full-resolution-verified Pass 94 warm start.";
        }
        result.candidateEligibleForFutureApply = true;
    } else {
        result.selected = result.warmStart;
        result.fallbackToWarmStart = true;
        result.candidateEligibleForFutureApply = false;
        if (fullSearch.disposition == EvaluationDisposition::Canceled) {
            result.status = SolveStatus::Canceled;
            result.reason = "Full-resolution verification was canceled; the current recipe remains unchanged.";
        } else if (fullSearch.disposition == EvaluationDisposition::MissingEvidence) {
            result.status = SolveStatus::BlockedByMissingEvidence;
            result.reason = "Full-resolution verification lacks required evidence; the warm record is retained without apply.";
        } else {
            result.status = SolveStatus::FullResolutionVerificationRejected;
            result.reason = "The proxy finalist failed independent full-resolution verification; the warm record is retained without apply.";
        }
    }
    result.candidateRecipeRoundTripExact = VisibleRoundTripExact(
        result.selected, result.candidateVisibleParametersRoundTripExact);
    if (!result.candidateRecipeRoundTripExact ||
        !result.candidateVisibleParametersRoundTripExact) {
        result.candidateEligibleForFutureApply = false;
        result.status = SolveStatus::BlockedByMissingEvidence;
        result.reason = "Selected candidate does not round-trip exactly through the visible recipe schema.";
    }
    result.currentRecipeUnchanged = result.currentRecipeUnchanged &&
        result.fullResolutionEvaluation.currentRecipeUnchanged;
    result.undoHistoryUnchanged = result.undoHistoryUnchanged &&
        result.fullResolutionEvaluation.undoHistoryUnchanged;
    result.projectDirtyStateUnchanged = result.projectDirtyStateUnchanged &&
        result.fullResolutionEvaluation.projectDirtyStateUnchanged;
    if (!result.currentRecipeUnchanged || !result.undoHistoryUnchanged ||
        !result.projectDirtyStateUnchanged) {
        result.candidateEligibleForFutureApply = false;
        result.status = SolveStatus::FullResolutionVerificationRejected;
        result.reason = "Dry-run state-integrity assertions failed; no candidate is eligible for integration.";
    }
    return result;
}

nlohmann::json SerializeObjectiveGoal(const ObjectiveGoal& goal) {
    return {
        { "termId", goal.termId }, { "tier", RawOptimizer::MeritTierName(goal.tier) },
        { "lower", goal.lower }, { "upper", goal.upper },
        { "meaningfulDelta", goal.meaningfulDelta }, { "required", goal.required },
        { "owningGroup", StageGroupName(goal.owningGroup) },
        { "units", goal.units }, { "reason", goal.reason }
    };
}

nlohmann::json SerializeStagePolicy(const StagePolicy& policy) {
    return {
        { "group", StageGroupName(policy.group) }, { "active", policy.active },
        { "maxUniqueEvaluations", policy.maxUniqueEvaluations },
        { "maxAcceptedIterations", policy.maxAcceptedIterations },
        { "reason", policy.reason }
    };
}

nlohmann::json SerializeParameterDecision(const ParameterDecision& decision) {
    return {
        { "id", PreciseRaw::ParameterStableString(decision.id) },
        { "group", StageGroupName(decision.group) }, { "active", decision.active },
        { "visibleOwner", decision.visibleOwner }, { "reason", decision.reason }
    };
}

nlohmann::json SerializeStageSearch(const StageSearchRecord& stage) {
    nlohmann::json dimensions = nlohmann::json::array();
    for (ParameterId id : stage.dimensions) {
        dimensions.push_back(PreciseRaw::ParameterStableString(id));
    }
    return {
        { "group", StageGroupName(stage.group) }, { "attempted", stage.attempted },
        { "skipped", stage.skipped }, { "dimensions", std::move(dimensions) },
        { "optimizer", stage.attempted
            ? RawOptimizer::SerializeSearchResult(stage.search)
            : nlohmann::json(nullptr) },
        { "reason", stage.reason }
    };
}

nlohmann::json SerializeDryRunResult(const DryRunResult& result) {
    nlohmann::json goals = nlohmann::json::array();
    for (const ObjectiveGoal& goal : result.objectiveGoals) {
        goals.push_back(SerializeObjectiveGoal(goal));
    }
    nlohmann::json decisions = nlohmann::json::array();
    for (const ParameterDecision& decision : result.parameterDecisions) {
        decisions.push_back(SerializeParameterDecision(decision));
    }
    nlohmann::json stages = nlohmann::json::array();
    for (const StageSearchRecord& stage : result.stages) {
        stages.push_back(SerializeStageSearch(stage));
    }
    nlohmann::json evaluations = nlohmann::json::array();
    for (const CandidateEvaluationRecord& evaluation : result.proxyEvaluations) {
        evaluations.push_back(PreciseRaw::SerializeEvaluation(evaluation));
    }
    const nlohmann::json improvement = MeritDeltaByTier(
        result.warmEvaluation, result.proxyFinalistEvaluation, result.objectiveGoals);
    return {
        { "schemaVersion", result.schemaVersion },
        { "solverVersion", result.solverVersion },
        { "reportVersion", result.reportVersion },
        { "objectivePolicyVersion", result.objectivePolicyVersion },
        { "solveId", result.solveId },
        { "status", RawOptimizer::SolveStatusName(result.status) },
        { "fullVerification", RawOptimizer::FullVerificationDispositionName(result.fullVerification) },
        { "warmStart", PreciseRaw::SerializeProposal(result.warmStart) },
        { "proxyFinalist", PreciseRaw::SerializeProposal(result.proxyFinalist) },
        { "selectedCandidate", PreciseRaw::SerializeProposal(result.selected) },
        { "warmEvaluation", PreciseRaw::SerializeEvaluation(result.warmEvaluation) },
        { "proxyFinalistEvaluation", PreciseRaw::SerializeEvaluation(result.proxyFinalistEvaluation) },
        { "fullResolutionEvaluation", PreciseRaw::SerializeEvaluation(result.fullResolutionEvaluation) },
        { "fullResolutionPromotion", PreciseRaw::SerializePromotion(result.promotion) },
        { "objectiveGoals", std::move(goals) },
        { "parameterDecisions", std::move(decisions) },
        { "stageSearches", std::move(stages) },
        { "proxyEvaluations", std::move(evaluations) },
        { "counts", {
            { "proposedEvaluations", result.proposedEvaluations },
            { "optimizerUniqueEvaluations", result.optimizerUniqueEvaluations },
            { "uniqueProxyEvaluations", result.uniqueProxyEvaluations },
            { "proxyCacheHits", result.proxyCacheHits },
            { "actualProxyRenderCount", result.actualProxyRenderCount },
            { "acceptedIterations", result.acceptedIterations },
            { "rejectedCandidates", result.rejectedCandidates },
            { "failedCandidates", result.failedCandidates }
        } },
        { "proxySearchRuntimeMs", result.proxySearchRuntimeMs },
        { "predictedImprovementByTier", improvement },
        { "budgetExhausted", result.budgetExhausted },
        { "meaningfulImprovement", result.meaningfulImprovement },
        { "fallbackToWarmStart", result.fallbackToWarmStart },
        { "candidateRecipeRoundTripExact", result.candidateRecipeRoundTripExact },
        { "candidateVisibleParametersRoundTripExact", result.candidateVisibleParametersRoundTripExact },
        { "stateIntegrity", {
            { "currentRecipeUnchanged", result.currentRecipeUnchanged },
            { "undoHistoryUnchanged", result.undoHistoryUnchanged },
            { "projectDirtyStateUnchanged", result.projectDirtyStateUnchanged }
        } },
        { "recipeApplied", result.recipeApplied },
        { "appliedPreviewTextureCreated", result.appliedPreviewTextureCreated },
        { "candidateEligibleForFutureApply", result.candidateEligibleForFutureApply },
        { "reportIntent", "diagnostic-visible-recipe-candidate-only" },
        { "loadableAsAppliedTexture", false },
        { "combinedTotalScore", nullptr },
        { "reason", result.reason }, { "warnings", result.warnings }
    };
}

} // namespace Stack::PreciseDryRun
