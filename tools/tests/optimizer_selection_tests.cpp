#include "Raw/RawOptimizerSelection.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace Stack::RawOptimizer;

int g_Failures = 0;

void Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_Failures;
    }
}

std::string PointId(const std::vector<double>& point) {
    std::ostringstream stream;
    stream.precision(8);
    for (double value : point) stream << value << '|';
    return stream.str();
}

MeritComponent Component(
    const char* id,
    MeritTier tier,
    MeritRole role,
    double value,
    double meaningfulDelta = 0.0) {
    MeritComponent result;
    result.id = id;
    result.tier = tier;
    result.role = role;
    result.valid = true;
    result.value = value;
    result.meaningfulDelta = meaningfulDelta;
    result.units = "normalized-loss";
    return result;
}

SearchEvaluation CompleteEvaluation(
    const std::vector<double>& point,
    double technicalLoss,
    double editLoss,
    double meaningfulDelta = 1.0e-6) {
    SearchEvaluation result;
    result.candidateId = PointId(point);
    result.disposition = EvaluationDisposition::Complete;
    result.point = point;
    result.hardConstraintsPassed = true;
    result.merit = {
        Component("identity.violation", MeritTier::Tier0IdentityState,
            MeritRole::ConstraintViolation, 0.0),
        Component("raw.violation", MeritTier::Tier1RawArtifact,
            MeritRole::ConstraintViolation, 0.0),
        Component("technical.goal", MeritTier::Tier2Technical,
            MeritRole::GoalViolation, technicalLoss, meaningfulDelta),
        Component("edit.distance", MeritTier::Tier5TieBreaker,
            MeritRole::TieBreaker, editLoss, meaningfulDelta)
    };
    return result;
}

SearchProblem TwoDimensionalProblem(std::vector<double> warm = { 0.8, -0.6 }) {
    SearchProblem problem;
    problem.id = "two-dimensional-fixture";
    problem.dimensions = {
        { "x", -1.0, 1.0, true, 0, {} },
        { "y", -1.0, 1.0, true, 1, {} }
    };
    problem.warmStart = std::move(warm);
    problem.interactionPairs = { { 0, 1 } };
    return problem;
}

SearchBudget TestBudget() {
    SearchBudget budget;
    budget.maxUniqueEvaluations = 80;
    budget.maxAcceptedIterations = 24;
    budget.stableIterationsRequired = 2;
    budget.maxRuntimeMs = 5000.0;
    budget.initialNormalizedRadius = 0.25;
    budget.minimumNormalizedRadius = 0.015625;
    budget.maximumNormalizedRadius = 0.5;
    return budget;
}

void TestLexicographicComparison() {
    SearchEvaluation safe = CompleteEvaluation({ 0.0 }, 1.0, 0.0);
    SearchEvaluation unsafe = safe;
    unsafe.disposition = EvaluationDisposition::Rejected;
    unsafe.hardConstraintsPassed = false;
    unsafe.merit[1].value = 0.01;
    unsafe.merit[2].value = 0.0;
    Check(CompareEvaluations(safe, unsafe) == ComparisonResult::Better,
        "complete safe candidate beats lower technical loss with Tier 1 rejection");
    Check(CompareEvaluations(unsafe, safe) == ComparisonResult::Worse,
        "Tier 1 rejection cannot buy lower-tier quality");

    SearchEvaluation tiny = safe;
    tiny.merit[2].value = 0.9995;
    safe.merit[2].meaningfulDelta = 0.001;
    tiny.merit[2].meaningfulDelta = 0.001;
    Check(CompareEvaluations(tiny, safe) == ComparisonResult::Equivalent,
        "gain below declared meaningful delta does not displace incumbent");
}

void TestPatternSearchAndDeterminism() {
    const SearchProblem problem = TwoDimensionalProblem();
    const std::vector<double> warm = problem.warmStart;
    SearchCallbacks callbacks;
    callbacks.evaluate = [&](const std::vector<double>& point) {
        const double loss = std::pow(point[0] - 0.10, 2.0) +
            0.5 * std::pow(point[1] + 0.20, 2.0);
        const double edit = std::abs(point[0] - warm[0]) + std::abs(point[1] - warm[1]);
        return CompleteEvaluation(point, loss, edit, 1.0e-5);
    };
    const SearchResult first = RunStageOrderedPatternSearch(problem, TestBudget(), callbacks);
    const SearchResult second = RunStageOrderedPatternSearch(problem, TestBudget(), callbacks);
    Check(first.status == SolveStatus::Converged ||
          first.status == SolveStatus::SafeImprovementBudgetExhausted,
        "pattern search finds a meaningful safe quadratic improvement");
    Check(CompareEvaluations(first.selected, first.warmStart) == ComparisonResult::Better,
        "pattern result beats warm start lexicographically");
    Check(first.selected.point == second.selected.point &&
          first.status == second.status &&
          first.uniqueEvaluations == second.uniqueEvaluations,
        "pattern search is exactly deterministic for fixed input and budget");
    Check(problem.warmStart == warm,
        "pattern search never mutates the problem warm-start vector");
}

void TestTrustRegionAndCache() {
    const SearchProblem problem = TwoDimensionalProblem({ 0.9, 0.8 });
    const std::vector<double> warm = problem.warmStart;
    SearchCallbacks callbacks;
    callbacks.evaluate = [&](const std::vector<double>& point) {
        const double u = point[0] + point[1] - 0.25;
        const double v = point[0] - point[1] + 0.10;
        return CompleteEvaluation(point, u * u + 0.15 * v * v,
            std::abs(point[0] - warm[0]) + std::abs(point[1] - warm[1]), 1.0e-5);
    };
    const SearchResult result = RunDiagonalQuadraticTrustRegion(problem, TestBudget(), callbacks);
    Check(result.status == SolveStatus::Converged ||
          result.status == SolveStatus::SafeImprovementBudgetExhausted,
        "diagonal quadratic trust-region comparator improves smooth coupled case");
    Check(CompareEvaluations(result.selected, result.warmStart) == ComparisonResult::Better,
        "trust-region selected record beats warm start");
    Check(result.cacheHits > 0,
        "trust-region interpolation reuses exact point evaluations");
}

void TestFeasibilityRestoration() {
    SearchProblem problem;
    problem.id = "unsafe-warm-feasibility";
    problem.dimensions = { { "raw-exposure", -1.0, 0.5, true, 0, {} } };
    problem.warmStart = { 0.5 };
    SearchCallbacks callbacks;
    callbacks.evaluate = [](const std::vector<double>& point) {
        const double violation = std::max(0.0, point[0] + 0.20);
        SearchEvaluation result = CompleteEvaluation(point, std::abs(point[0] + 0.40),
            std::abs(point[0] - 0.5), 1.0e-4);
        result.merit[1].value = violation;
        if (violation > 0.0) {
            result.disposition = EvaluationDisposition::Rejected;
            result.hardConstraintsPassed = false;
            result.reason = "WB-scaled headroom violation";
        }
        return result;
    };
    const SearchResult pattern = RunStageOrderedPatternSearch(problem, TestBudget(), callbacks);
    const SearchResult trust = RunDiagonalQuadraticTrustRegion(problem, TestBudget(), callbacks);
    Check(pattern.selected.disposition == EvaluationDisposition::Complete &&
          pattern.selected.point[0] <= -0.20,
        "pattern search restores feasibility from unsafe warm start");
    Check(trust.selected.disposition == EvaluationDisposition::Complete &&
          trust.selected.point[0] <= -0.20,
        "trust-region comparator restores feasibility from unsafe warm start");
}

void TestWarmRetentionAndFailureStates() {
    const SearchProblem problem = TwoDimensionalProblem({ 0.0, 0.0 });
    SearchCallbacks flat;
    flat.evaluate = [](const std::vector<double>& point) {
        return CompleteEvaluation(point, 1.0, std::abs(point[0]) + std::abs(point[1]), 0.01);
    };
    const SearchResult retained = RunStageOrderedPatternSearch(problem, TestBudget(), flat);
    Check(retained.status == SolveStatus::WarmStartRetained &&
          retained.selected.point == problem.warmStart,
        "flat/no-meaningful-improvement case retains warm start");

    SearchCallbacks failed;
    failed.evaluate = [](const std::vector<double>& point) {
        SearchEvaluation result;
        result.point = point;
        result.candidateId = PointId(point);
        result.disposition = EvaluationDisposition::Failed;
        result.reason = "injected render failure";
        return result;
    };
    Check(RunStageOrderedPatternSearch(problem, TestBudget(), failed).status ==
          SolveStatus::CandidateRenderFailed,
        "failed warm render has a distinct failed-render state");

    bool cancel = true;
    SearchCallbacks canceled = flat;
    canceled.shouldCancel = [&]() { return cancel; };
    Check(RunStageOrderedPatternSearch(problem, TestBudget(), canceled).status == SolveStatus::Canceled,
        "generation cancellation has a distinct canceled state");
}

void TestFullResolutionFallbackAndSerialization() {
    SearchProblem problem;
    problem.id = "full-resolution-fallback";
    problem.dimensions = { { "x", -1.0, 1.0, true, 0, {} } };
    problem.warmStart = { 0.5 };
    SearchBudget budget = TestBudget();
    budget.requireFullResolutionVerification = true;
    SearchCallbacks callbacks;
    callbacks.evaluate = [](const std::vector<double>& point) {
        return CompleteEvaluation(point, std::pow(point[0] + 0.5, 2.0),
            std::abs(point[0] - 0.5), 1.0e-5);
    };
    callbacks.verifyFullResolution = [&](const SearchEvaluation& evaluation) {
        return evaluation.point == problem.warmStart
            ? FullVerificationDisposition::Accepted
            : FullVerificationDisposition::Rejected;
    };
    const SearchResult result = RunStageOrderedPatternSearch(problem, budget, callbacks);
    Check(result.status == SolveStatus::FullResolutionVerificationRejected &&
          result.fallbackToWarmStart && result.selected.point == problem.warmStart,
        "full-resolution rejection falls back without applying proxy selection");
    const nlohmann::json serialized = SerializeSearchResult(result);
    Check(serialized.at("combinedTotalScore").is_null() &&
          serialized.at("selected").at("combinedTotalScore").is_null(),
        "optimizer record retains individual lexicographic merit without aggregate score");
    Check(serialized.at("currentRecipeUnchanged").get<bool>() &&
          serialized.at("undoHistoryUnchanged").get<bool>() &&
          serialized.at("projectDirtyStateUnchanged").get<bool>(),
        "non-mutating benchmark record preserves recipe/undo/dirty assertions");
}

void TestBoundaryOscillationShrinksRadius() {
    SearchProblem problem;
    problem.id = "boundary-oscillation";
    problem.dimensions = { { "x", -1.0, 1.0, true, 0, {} } };
    problem.warmStart = { 0.10 };
    SearchBudget budget = TestBudget();
    budget.maximumBoundaryOscillations = 2;
    SearchCallbacks callbacks;
    callbacks.evaluate = [](const std::vector<double>& point) {
        SearchEvaluation result = CompleteEvaluation(point,
            std::abs(point[0] + 0.10), std::abs(point[0] - 0.10), 1.0e-5);
        if (point[0] > 0.0) {
            result.disposition = EvaluationDisposition::Rejected;
            result.hardConstraintsPassed = false;
            result.merit[1].value = point[0];
        }
        return result;
    };
    const SearchResult result =
        RunDiagonalQuadraticTrustRegion(problem, budget, callbacks);
    const bool recordedShrink = std::any_of(
        result.trace.begin(), result.trace.end(), [](const SearchTraceEvent& event) {
            return event.kind == "boundary-radius-shrink";
        });
    Check(result.boundaryOscillations >= budget.maximumBoundaryOscillations && recordedShrink,
        "constraint-boundary oscillation is counted and forces an explicit radius shrink");
    Check(result.selected.disposition == EvaluationDisposition::Complete &&
          result.selected.hardConstraintsPassed,
        "boundary shrink retains the last complete safe accepted state");
}

void TestDiscreteNeighborRestoresFeasibility() {
    SearchProblem problem;
    problem.id = "discrete-feasibility";
    problem.dimensions = { { "raw-exposure", -0.75, 0.0, true, 0, { -0.75, 0.0 } } };
    problem.warmStart = { 0.0 };
    SearchCallbacks callbacks;
    callbacks.evaluate = [](const std::vector<double>& point) {
        SearchEvaluation result = CompleteEvaluation(point, 0.0, std::abs(point[0]));
        if (point[0] > -0.5) {
            result.disposition = EvaluationDisposition::Rejected;
            result.hardConstraintsPassed = false;
            result.merit[1].value = point[0] + 0.5;
        }
        return result;
    };
    const SearchResult pattern = RunStageOrderedPatternSearch(problem, TestBudget(), callbacks);
    const SearchResult trust = RunDiagonalQuadraticTrustRegion(problem, TestBudget(), callbacks);
    Check(pattern.selected.point == std::vector<double>{ -0.75 } &&
          pattern.selected.hardConstraintsPassed,
        "pattern search probes the adjacent discrete value independent of continuous radius");
    Check(trust.selected.point == std::vector<double>{ -0.75 } &&
          trust.selected.hardConstraintsPassed,
        "trust-region comparator includes adjacent discrete interpolation sites");
}

} // namespace

int main() {
    TestLexicographicComparison();
    TestPatternSearchAndDeterminism();
    TestTrustRegionAndCache();
    TestFeasibilityRestoration();
    TestWarmRetentionAndFailureStates();
    TestFullResolutionFallbackAndSerialization();
    TestBoundaryOscillationShrinksRadius();
    TestDiscreteNeighborRestoresFeasibility();
    if (g_Failures != 0) {
        std::cerr << g_Failures << " Phase 04 optimizer fixture assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All Phase 04 optimizer selection fixtures passed.\n";
    return 0;
}
