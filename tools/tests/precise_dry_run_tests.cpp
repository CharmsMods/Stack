#include "Raw/RawPreciseDryRun.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace Stack;
using namespace Stack::PreciseDryRun;

int g_Failures = 0;

void Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_Failures;
    }
}

PreciseRaw::ConstraintResult Constraint(
    const char* id,
    PreciseRaw::ConstraintTier tier,
    PreciseRaw::ConstraintStatus status,
    double value = 0.0,
    double limit = 0.0) {
    PreciseRaw::ConstraintResult result;
    result.id = id;
    result.tier = tier;
    result.status = status;
    result.value = value;
    result.limit = limit;
    result.units = "fixture";
    result.uncertainty01 = 1.0e-6;
    result.reason = "controlled dry-run fixture";
    return result;
}

PreciseRaw::ObjectiveTerm Term(
    const char* id,
    PreciseRaw::ObjectiveTier tier,
    double value,
    const char* role) {
    PreciseRaw::ObjectiveTerm result;
    result.id = id;
    result.tier = tier;
    result.valid = true;
    result.value = value;
    result.units = "normalized-loss";
    result.uncertainty01 = 1.0e-7;
    result.stage = "fixture";
    result.role = role;
    result.reason = "controlled dry-run fixture";
    return result;
}

RenderedFeatures::FeatureRecord PromotionFeature(
    const PreciseRaw::CandidateProposal& proposal,
    const std::string& stage,
    double value) {
    RenderedFeatures::FeatureRecord record;
    record.valid = true;
    record.sourceIdentity = proposal.identities.sourceIdentity;
    record.recipeIdentity = proposal.candidateRecipeIdentity;
    record.stage = stage;
    record.colorSpace = "fixture-linear";
    record.colorTransformIdentity = "fixture-transform";
    record.transferFunction = "linear";
    record.referenceGrey = 0.18;
    record.rawEvidenceIdentity = proposal.identities.rawEvidenceIdentity;
    record.cropIdentity = "fixture-full";
    record.width = 256;
    record.height = 256;
    RenderedFeatures::FeatureValue feature;
    feature.id = "fixture.value";
    feature.disposition = RenderedFeatures::FeatureDisposition::Accepted;
    feature.valid = true;
    feature.value = value;
    feature.units = "fixture";
    feature.minimumWidth = 1;
    feature.minimumHeight = 1;
    record.values.push_back(std::move(feature));
    return record;
}

double NormalizedEdit(
    const PreciseRaw::CandidateParameterVector& candidate,
    const PreciseRaw::CandidateParameterVector& warm,
    const PreciseRaw::ParameterSpace& space) {
    double result = 0.0;
    for (int i = 0; i < static_cast<int>(PreciseRaw::ParameterId::Count); ++i) {
        const auto id = static_cast<PreciseRaw::ParameterId>(i);
        const auto& range = space.ranges[static_cast<std::size_t>(i)];
        result += std::abs(PreciseRaw::GetParameter(candidate, id) -
            PreciseRaw::GetParameter(warm, id)) /
            std::max(1.0e-9, range.upper - range.lower);
    }
    return result;
}

struct FixtureEvaluator {
    PreciseRaw::CandidateParameterVector warm;
    PreciseRaw::ParameterSpace space;
    bool requireNegativeExposure = false;
    bool missingConstraint = false;
    bool rejectFull = false;
    bool failWarm = false;
    bool failFull = false;
    bool staleAfterWarm = false;
    bool useTechnicalLoss = false;
    int proxyCalls = 0;
    int fullCalls = 0;

    double TechnicalLoss(const PreciseRaw::CandidateParameterVector& p) const {
        if (!useTechnicalLoss) return 0.0;
        return std::pow(p.localDeltaEv - 0.50, 2.0) +
            std::pow(p.finishY1 - 0.50, 2.0) +
            std::pow(p.finishY2 - 0.75, 2.0) +
            std::pow(p.displayWhiteEv - 8.0, 2.0);
    }

    PreciseRaw::CandidateEvaluationRecord Evaluate(
        const PreciseRaw::CandidateProposal& proposal,
        bool full) {
        if (full) ++fullCalls;
        else ++proxyCalls;
        PreciseRaw::CandidateEvaluationRecord result;
        result.proposal = proposal;
        result.currentRecipeUnchanged = true;
        result.undoHistoryUnchanged = true;
        result.projectDirtyStateUnchanged = true;
        result.render.attempted = true;
        result.render.success = true;
        result.render.fullResolution = full;
        result.render.proxyIdentity = proposal.identities.proxyIdentity;
        result.render.renderIdentity = proposal.candidateId + (full ? "-full" : "-proxy");
        result.render.renderWidth = full ? 2048 : 256;
        result.render.renderHeight = full ? 1536 : 192;
        result.render.featureWidth = 256;
        result.render.featureHeight = 192;
        PreciseRaw::StageFeatureEvidence stage;
        stage.stage = RawAutoStartPoint::RawAutoStartPointStage::RawPlacement;
        stage.features = PromotionFeature(proposal, "raw-placement", TechnicalLoss(proposal.parameters));
        result.render.stages.push_back(std::move(stage));
        result.constraints.push_back(Constraint(
            "identity.fixture", PreciseRaw::ConstraintTier::Tier0IdentityState,
            PreciseRaw::ConstraintStatus::Passed));
        if (missingConstraint) {
            result.constraints.push_back(Constraint(
                "raw.wb_scaled_headroom", PreciseRaw::ConstraintTier::Tier1RawArtifact,
                PreciseRaw::ConstraintStatus::Unavailable));
        } else {
            const double limit = requireNegativeExposure ? -0.20 : 2.0;
            const bool safe = !requireNegativeExposure || proposal.parameters.rawExposureEv <= limit;
            result.constraints.push_back(Constraint(
                "raw.wb_scaled_headroom", PreciseRaw::ConstraintTier::Tier1RawArtifact,
                safe ? PreciseRaw::ConstraintStatus::Passed : PreciseRaw::ConstraintStatus::Failed,
                proposal.parameters.rawExposureEv,
                limit));
            result.status = safe
                ? PreciseRaw::EvaluationStatus::Complete
                : PreciseRaw::EvaluationStatus::Rejected;
        }
        if (staleAfterWarm && !full && proxyCalls > 1) {
            result.status = PreciseRaw::EvaluationStatus::Stale;
            result.render.stale = true;
            result.rejectionReason = "injected source identity change";
        }
        if (full && rejectFull) {
            result.status = PreciseRaw::EvaluationStatus::Rejected;
            result.constraints.back().status = PreciseRaw::ConstraintStatus::Failed;
            result.rejectionReason = "injected full-resolution rejection";
        }
        if ((!full && failWarm && proxyCalls == 1) || (full && failFull)) {
            result.status = PreciseRaw::EvaluationStatus::Failed;
            result.render.success = false;
            result.rejectionReason = full
                ? "injected full-resolution render failure"
                : "injected warm render failure";
        }
        result.terms.push_back(Term(
            "technical.loss", PreciseRaw::ObjectiveTier::Tier2Technical,
            TechnicalLoss(proposal.parameters), "controlled goal"));
        result.terms.push_back(Term(
            "edit.normalized_l1", PreciseRaw::ObjectiveTier::Tier5TieBreaker,
            NormalizedEdit(proposal.parameters, warm, space), "tie-breaker only"));
        int changed = 0;
        for (int i = 0; i < static_cast<int>(PreciseRaw::ParameterId::Count); ++i) {
            const auto id = static_cast<PreciseRaw::ParameterId>(i);
            if (std::abs(PreciseRaw::GetParameter(proposal.parameters, id) -
                         PreciseRaw::GetParameter(warm, id)) > 1.0e-6) {
                ++changed;
            }
        }
        result.terms.push_back(Term(
            "edit.changed_parameter_count", PreciseRaw::ObjectiveTier::Tier5TieBreaker,
            changed, "tie-breaker only"));
        return result;
    }
};

PreciseDryRun::DryRunRequest MakeRequest() {
    DryRunRequest request;
    request.solveId = "phase-05-fixture";
    request.solverBaseRecipe = RawRecipe::MakeDefaultRecipe("fixture.raw", "fixture.raw");
    request.parameterSpace = PreciseRaw::BuildParameterSpace(request.solverBaseRecipe, nullptr);
    request.totalBudget = RawOptimizer::SearchBudget {};
    request.totalBudget.requireFullResolutionVerification = true;
    request.stagePolicies = DefaultStagePolicies();
    const std::string base = PreciseRaw::RecipeIdentity(request.solverBaseRecipe);
    request.proxyIdentity.sourceIdentity = std::string(64, 'a');
    request.proxyIdentity.decodeIdentity = std::string(64, 'b');
    request.proxyIdentity.rawEvidenceIdentity = std::string(64, 'c');
    request.proxyIdentity.baseRecipeIdentity = base;
    request.proxyIdentity.rendererIdentity = "fixture-renderer-v1";
    request.proxyIdentity.proxyIdentity = "fixture-proxy-v1";
    request.proxyIdentity.budgetIdentity = "raw-precise-budget-v1";
    request.proxyIdentity.ownershipIdentity = request.ownership.identity;
    request.proxyIdentity.generation = 7;
    request.fullResolutionIdentity = request.proxyIdentity;
    request.fullResolutionIdentity.proxyIdentity = "fixture-full-resolution-v1";
    return request;
}

DryRunResult RunFixture(DryRunRequest request, FixtureEvaluator& evaluator) {
    evaluator.warm = PreciseRaw::ExtractParameters(request.solverBaseRecipe);
    evaluator.space = request.parameterSpace;
    DryRunCallbacks callbacks;
    callbacks.evaluateProxy = [&](const PreciseRaw::CandidateProposal& proposal) {
        return evaluator.Evaluate(proposal, false);
    };
    callbacks.evaluateFullResolution = [&](const PreciseRaw::CandidateProposal& proposal) {
        return evaluator.Evaluate(proposal, true);
    };
    return RunPreciseDryRun(request, callbacks);
}

void TestWellExposedWarmRetentionAndNoMutation() {
    DryRunRequest request = MakeRequest();
    FixtureEvaluator evaluator;
    const DryRunResult result = RunFixture(request, evaluator);
    Check(result.status == RawOptimizer::SolveStatus::WarmStartRetained,
        "well-exposed/no-declared-goal fixture retains Pass 94 warm start");
    Check(result.fullVerification == RawOptimizer::FullVerificationDisposition::Accepted,
        "warm start is independently full-resolution verified");
    Check(result.candidateRecipeRoundTripExact &&
          result.candidateVisibleParametersRoundTripExact,
        "selected complete visible recipe and parameters round-trip exactly");
    Check(result.currentRecipeUnchanged && result.undoHistoryUnchanged &&
          result.projectDirtyStateUnchanged && !result.recipeApplied &&
          !result.appliedPreviewTextureCreated,
        "dry run preserves current recipe/undo/dirty state and creates no applied output");
    Check(result.proxyCacheHits >= 3,
        "stage handoffs reuse exact candidate records across ordered searches");
    const nlohmann::json serialized = SerializeDryRunResult(result);
    Check(serialized.at("combinedTotalScore").is_null() &&
          !serialized.at("recipeApplied").get<bool>() &&
          !serialized.at("loadableAsAppliedTexture").get<bool>(),
        "serialized dry run cannot masquerade as a scalar-scored applied texture");
}

void TestUnsafeWarmFeasibilityRestorationAndBudgetState() {
    DryRunRequest request = MakeRequest();
    for (StagePolicy& policy : request.stagePolicies) {
        if (policy.group != StageGroup::RawExposure) policy.active = false;
    }
    request.stagePolicies[0].maxUniqueEvaluations = 2;
    FixtureEvaluator evaluator;
    evaluator.requireNegativeExposure = true;
    const DryRunResult result = RunFixture(request, evaluator);
    Check(result.status == RawOptimizer::SolveStatus::SafeImprovementBudgetExhausted,
        "safe improvement with a two-evaluation RAW quota is not mislabeled converged");
    Check(result.selected.parameters.rawExposureEv <= -0.20 &&
          result.meaningfulImprovement,
        "unsafe warm start is replaced by a complete headroom-safe exposure candidate");
    Check(result.fullVerification == RawOptimizer::FullVerificationDisposition::Accepted,
        "feasibility-restored candidate passes independent full verification");
}

void TestAllVisibleGroupsAndDeterminism() {
    DryRunRequest request = MakeRequest();
    request.stagePolicies[0].active = false;
    ObjectiveGoal goal;
    goal.termId = "technical.loss";
    goal.tier = RawOptimizer::MeritTier::Tier2Technical;
    goal.lower = 0.0;
    goal.upper = 0.0;
    goal.meaningfulDelta = 1.0e-6;
    goal.owningGroup = StageGroup::LocalRange;
    goal.reason = "controlled multi-stage goal";
    request.objectiveGoals = { goal };
    FixtureEvaluator firstEvaluator;
    firstEvaluator.useTechnicalLoss = true;
    const DryRunResult first = RunFixture(request, firstEvaluator);
    FixtureEvaluator secondEvaluator;
    secondEvaluator.useTechnicalLoss = true;
    const DryRunResult second = RunFixture(request, secondEvaluator);
    if (!(first.meaningfulImprovement &&
          first.selected.parameters.localDeltaEv > 0.0 &&
          first.selected.parameters.finishY1 != secondEvaluator.warm.finishY1 &&
          first.selected.parameters.displayWhiteEv != secondEvaluator.warm.displayWhiteEv)) {
        std::cerr << "multi-stage values: status=" << RawOptimizer::SolveStatusName(first.status)
                  << " improve=" << first.meaningfulImprovement
                  << " local=" << first.selected.parameters.localDeltaEv
                  << " finish1=" << first.selected.parameters.finishY1
                  << " warmFinish1=" << secondEvaluator.warm.finishY1
                  << " displayWhite=" << first.selected.parameters.displayWhiteEv
                  << " warmDisplayWhite=" << secondEvaluator.warm.displayWhiteEv << '\n';
        for (const StageSearchRecord& stage : first.stages) {
            std::cerr << "  stage " << StageGroupName(stage.group)
                      << " status=" << RawOptimizer::SolveStatusName(stage.search.status)
                      << " accepted=" << stage.search.acceptedIterations
                      << " selected=";
            for (double value : stage.search.selected.point) std::cerr << value << ',';
            std::cerr << '\n';
        }
    }
    Check(first.meaningfulImprovement &&
          first.selected.parameters.localDeltaEv > 0.0 &&
          first.selected.parameters.finishY1 != secondEvaluator.warm.finishY1 &&
          first.selected.parameters.displayWhiteEv != secondEvaluator.warm.displayWhiteEv,
        "accepted range goal can move Local Range, Finish Tone, and Display Fit visible fields");
    Check(first.selected.recipe.localRange.points.size() >= 3 &&
          first.candidateRecipeRoundTripExact,
        "graph-authoring candidate remains a complete serializable visible recipe");
    Check(first.status == second.status &&
          first.selected.candidateRecipeIdentity == second.selected.candidateRecipeIdentity &&
          first.proposedEvaluations == second.proposedEvaluations &&
          first.acceptedIterations == second.acceptedIterations,
        "fixed evidence, identities, goals, and budget produce deterministic end-to-end selection");
}

void TestMissingCancellationAndFullFallback() {
    DryRunRequest missingRequest = MakeRequest();
    FixtureEvaluator missing;
    missing.missingConstraint = true;
    const DryRunResult blocked = RunFixture(missingRequest, missing);
    Check(blocked.status == RawOptimizer::SolveStatus::BlockedByMissingEvidence,
        "unavailable required raw constraint blocks instead of becoming zero evidence");

    DryRunRequest failedRequest = MakeRequest();
    FixtureEvaluator failed;
    failed.failWarm = true;
    const DryRunResult failedResult = RunFixture(failedRequest, failed);
    Check(failedResult.status == RawOptimizer::SolveStatus::CandidateRenderFailed,
        "warm candidate render failure remains distinct and applies nothing");

    DryRunRequest canceledRequest = MakeRequest();
    FixtureEvaluator unused;
    DryRunCallbacks canceledCallbacks;
    canceledCallbacks.shouldCancel = [] { return true; };
    canceledCallbacks.evaluateProxy = [&](const PreciseRaw::CandidateProposal& proposal) {
        return unused.Evaluate(proposal, false);
    };
    canceledCallbacks.evaluateFullResolution = [&](const PreciseRaw::CandidateProposal& proposal) {
        return unused.Evaluate(proposal, true);
    };
    const DryRunResult canceled = RunPreciseDryRun(canceledRequest, canceledCallbacks);
    Check(canceled.status == RawOptimizer::SolveStatus::Canceled && unused.proxyCalls == 0,
        "pre-start cancellation renders nothing and applies nothing");

    DryRunRequest staleRequest = MakeRequest();
    FixtureEvaluator stale;
    stale.staleAfterWarm = true;
    const DryRunResult staleResult = RunFixture(staleRequest, stale);
    Check(staleResult.status == RawOptimizer::SolveStatus::Canceled,
        "same-key/source identity staleness cancels an in-progress dry run");

    DryRunRequest rejectRequest = MakeRequest();
    FixtureEvaluator rejected;
    rejected.rejectFull = true;
    const DryRunResult fullRejected = RunFixture(rejectRequest, rejected);
    Check(fullRejected.status == RawOptimizer::SolveStatus::FullResolutionVerificationRejected &&
          fullRejected.fallbackToWarmStart &&
          fullRejected.selected.candidateRecipeIdentity == fullRejected.warmStart.candidateRecipeIdentity &&
          !fullRejected.candidateEligibleForFutureApply,
        "full-resolution rejection retains the warm record and never marks it apply-eligible");

    DryRunRequest fullFailRequest = MakeRequest();
    FixtureEvaluator fullFailed;
    fullFailed.failFull = true;
    const DryRunResult fullFailure = RunFixture(fullFailRequest, fullFailed);
    Check(fullFailure.status == RawOptimizer::SolveStatus::FullResolutionVerificationRejected &&
          fullFailure.fullVerification == RawOptimizer::FullVerificationDisposition::Failed,
        "full-resolution render failure is recorded without confusing it with proxy convergence");
}

void TestOwnershipAndRequestValidation() {
    DryRunRequest request = MakeRequest();
    request.ownership.localRangeUserOwned = true;
    request.ownership.identity = "local-user-owned-v1";
    request.proxyIdentity.ownershipIdentity = request.ownership.identity;
    request.fullResolutionIdentity.ownershipIdentity = request.ownership.identity;
    FixtureEvaluator evaluator;
    const DryRunResult result = RunFixture(request, evaluator);
    const auto local = std::find_if(result.stages.begin(), result.stages.end(), [](const auto& stage) {
        return stage.group == StageGroup::LocalRange;
    });
    Check(local != result.stages.end() && local->skipped,
        "user-owned Local Range group is explicitly withheld from search");
    const bool localDecisionsWithheld = std::all_of(
        result.parameterDecisions.begin(), result.parameterDecisions.end(), [](const auto& decision) {
            return decision.group != StageGroup::LocalRange || !decision.active;
        });
    Check(localDecisionsWithheld,
        "report maps every user-owned Local Range parameter to an inactive visible decision");

    DryRunRequest invalid = MakeRequest();
    invalid.totalBudget.requireFullResolutionVerification = false;
    std::string reason;
    Check(!ValidateRequest(invalid, reason),
        "Phase 05 request cannot disable the frozen full-resolution requirement");

    DryRunRequest editedBase = MakeRequest();
    editedBase.solverBaseRecipe.preToneExposureEv += 0.25f;
    Check(!ValidateRequest(editedBase, reason),
        "base-recipe edit invalidates the captured dry-run identity before rendering");
}

} // namespace

int main() {
    TestWellExposedWarmRetentionAndNoMutation();
    TestUnsafeWarmFeasibilityRestorationAndBudgetState();
    TestAllVisibleGroupsAndDeterminism();
    TestMissingCancellationAndFullFallback();
    TestOwnershipAndRequestValidation();
    if (g_Failures != 0) {
        std::cerr << g_Failures << " Phase 05 dry-run fixture assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All Phase 05 precise dry-run fixtures passed.\n";
    return 0;
}
