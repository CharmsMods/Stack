#include "Raw/RawOptimizerSelection.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <utility>

namespace Stack::RawOptimizer {
namespace {

constexpr double kEpsilon = 1.0e-12;

bool Finite(double value) {
    return std::isfinite(value);
}

int TierRank(MeritTier tier) {
    switch (tier) {
        case MeritTier::Tier0IdentityState: return 0;
        case MeritTier::Tier1RawArtifact: return 1;
        case MeritTier::Tier2Technical: return 2;
        case MeritTier::Tier3Display: return 3;
        case MeritTier::Tier5TieBreaker: return 5;
    }
    return 99;
}

int DispositionRank(EvaluationDisposition disposition) {
    switch (disposition) {
        case EvaluationDisposition::Complete: return 0;
        case EvaluationDisposition::Rejected: return 1;
        case EvaluationDisposition::MissingEvidence: return 2;
        case EvaluationDisposition::Failed: return 3;
        case EvaluationDisposition::Canceled: return 4;
    }
    return 5;
}

double ComparisonTolerance(const MeritComponent& a, const MeritComponent& b) {
    return std::max({ 0.0, a.meaningfulDelta, b.meaningfulDelta, a.uncertainty, b.uncertainty });
}

std::vector<std::size_t> OrderedMeritIndices(const SearchEvaluation& evaluation) {
    std::vector<std::size_t> indices(evaluation.merit.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::stable_sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
        const int tierA = TierRank(evaluation.merit[a].tier);
        const int tierB = TierRank(evaluation.merit[b].tier);
        if (tierA != tierB) return tierA < tierB;
        if (evaluation.merit[a].role != evaluation.merit[b].role) {
            return static_cast<int>(evaluation.merit[a].role) < static_cast<int>(evaluation.merit[b].role);
        }
        return evaluation.merit[a].id < evaluation.merit[b].id;
    });
    return indices;
}

std::string PointKey(const std::vector<double>& point) {
    std::ostringstream stream;
    stream << std::hexfloat;
    for (double value : point) stream << value << '|';
    return stream.str();
}

double SnapValue(double value, const DimensionSpec& dimension) {
    value = std::clamp(value, dimension.lower, dimension.upper);
    if (dimension.discreteValues.empty()) return value;
    double best = dimension.discreteValues.front();
    double bestDistance = std::abs(value - best);
    for (double candidate : dimension.discreteValues) {
        const double distance = std::abs(value - candidate);
        if (distance < bestDistance - kEpsilon ||
            (std::abs(distance - bestDistance) <= kEpsilon && candidate < best)) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return std::clamp(best, dimension.lower, dimension.upper);
}

std::vector<double> NormalizePoint(const SearchProblem& problem, std::vector<double> point) {
    if (point.size() != problem.dimensions.size()) return {};
    for (std::size_t i = 0; i < point.size(); ++i) {
        point[i] = SnapValue(point[i], problem.dimensions[i]);
    }
    return point;
}

double PhysicalStep(const DimensionSpec& dimension, double normalizedRadius) {
    return std::max(0.0, dimension.upper - dimension.lower) * normalizedRadius;
}

double OffsetDimensionValue(
    double current,
    const DimensionSpec& dimension,
    int direction,
    double normalizedRadius,
    int discreteSteps = 1) {
    if (dimension.discreteValues.empty()) {
        return current + direction * PhysicalStep(dimension, normalizedRadius);
    }
    std::vector<double> values = dimension.discreteValues;
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end(), [](double a, double b) {
        return std::abs(a - b) <= kEpsilon;
    }), values.end());
    const double snapped = SnapValue(current, dimension);
    std::size_t nearest = 0;
    for (std::size_t i = 1; i < values.size(); ++i) {
        if (std::abs(values[i] - snapped) < std::abs(values[nearest] - snapped) - kEpsilon) {
            nearest = i;
        }
    }
    const int requested = static_cast<int>(nearest) + direction * std::max(1, discreteSteps);
    const int bounded = std::clamp(requested, 0, static_cast<int>(values.size()) - 1);
    return values[static_cast<std::size_t>(bounded)];
}

bool SamePoint(const std::vector<double>& a, const std::vector<double>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](double x, double y) {
        return std::abs(x - y) <= kEpsilon;
    });
}

int ActiveModelComponent(const SearchEvaluation& evaluation) {
    const std::vector<std::size_t> order = OrderedMeritIndices(evaluation);
    int firstGoal = -1;
    for (std::size_t index : order) {
        const MeritComponent& component = evaluation.merit[index];
        if (!component.valid) continue;
        if (firstGoal < 0 && component.role == MeritRole::GoalViolation) {
            firstGoal = static_cast<int>(index);
        }
        const double tolerance = std::max(component.meaningfulDelta, component.uncertainty);
        if ((component.role == MeritRole::ConstraintViolation || component.role == MeritRole::GoalViolation) &&
            component.value > tolerance + kEpsilon) {
            return static_cast<int>(index);
        }
    }
    return firstGoal;
}

double MeritValue(const SearchEvaluation& evaluation, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= evaluation.merit.size()) {
        return std::numeric_limits<double>::infinity();
    }
    const MeritComponent& component = evaluation.merit[static_cast<std::size_t>(index)];
    return component.valid && Finite(component.value)
        ? component.value
        : std::numeric_limits<double>::infinity();
}

class EvaluationRunner {
public:
    EvaluationRunner(
        const SearchProblem& problem,
        const SearchBudget& budget,
        const SearchCallbacks& callbacks,
        SearchResult& result)
        : m_Problem(problem), m_Budget(budget), m_Callbacks(callbacks), m_Result(result),
          m_Started(std::chrono::steady_clock::now()) {}

    bool Canceled() const {
        return m_Callbacks.shouldCancel && m_Callbacks.shouldCancel();
    }

    bool BudgetAvailable() const {
        if (m_Result.uniqueEvaluations >= m_Budget.maxUniqueEvaluations) return false;
        return ElapsedMs() < m_Budget.maxRuntimeMs;
    }

    double ElapsedMs() const {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - m_Started).count();
    }

    bool Evaluate(const std::vector<double>& requested, SearchEvaluation& output, bool& cacheHit) {
        cacheHit = false;
        if (Canceled()) return false;
        const std::vector<double> point = NormalizePoint(m_Problem, requested);
        if (point.empty()) return false;
        ++m_Result.proposedEvaluations;
        const std::string key = PointKey(point);
        const auto cached = m_Cache.find(key);
        if (cached != m_Cache.end()) {
            output = cached->second;
            output.fromCache = true;
            cacheHit = true;
            ++m_Result.cacheHits;
            return true;
        }
        if (!BudgetAvailable() || !m_Callbacks.evaluate) return false;
        output = m_Callbacks.evaluate(point);
        output.point = point;
        output.fromCache = false;
        ++m_Result.uniqueEvaluations;
        if (output.disposition == EvaluationDisposition::Rejected) ++m_Result.rejectedEvaluations;
        if (output.disposition == EvaluationDisposition::Failed) ++m_Result.failedEvaluations;
        m_Cache.emplace(key, output);
        return true;
    }

private:
    const SearchProblem& m_Problem;
    const SearchBudget& m_Budget;
    const SearchCallbacks& m_Callbacks;
    SearchResult& m_Result;
    std::chrono::steady_clock::time_point m_Started;
    std::map<std::string, SearchEvaluation> m_Cache;
};

void AddTrace(
    SearchResult& result,
    const char* kind,
    const SearchEvaluation& evaluation,
    ComparisonResult comparison,
    double radius,
    bool accepted,
    bool cacheHit,
    std::string reason,
    int activeMerit = -1,
    double predicted = 0.0,
    double actual = 0.0,
    double ratio = 0.0) {
    SearchTraceEvent event;
    event.sequence = static_cast<int>(result.trace.size());
    event.kind = kind;
    event.candidateId = evaluation.candidateId;
    event.point = evaluation.point;
    event.comparison = comparison;
    event.normalizedRadius = radius;
    event.activeMeritIndex = activeMerit;
    event.predictedReduction = predicted;
    event.actualReduction = actual;
    event.trustRatio = ratio;
    event.accepted = accepted;
    event.cacheHit = cacheHit;
    event.reason = std::move(reason);
    result.trace.push_back(std::move(event));
}

bool UsableWarmStart(const SearchEvaluation& evaluation) {
    return evaluation.disposition == EvaluationDisposition::Complete ||
        evaluation.disposition == EvaluationDisposition::Rejected;
}

bool BoundaryState(const SearchEvaluation& evaluation, bool& safe) {
    if (evaluation.disposition != EvaluationDisposition::Complete &&
        evaluation.disposition != EvaluationDisposition::Rejected) {
        return false;
    }
    safe = evaluation.disposition == EvaluationDisposition::Complete &&
        evaluation.hardConstraintsPassed;
    return true;
}

void ObserveConstraintBoundary(
    const SearchEvaluation& evaluation,
    SearchResult& result,
    bool& hasPreviousState,
    bool& previousSafe,
    int& oscillationsAtRadius) {
    bool safe = false;
    if (!BoundaryState(evaluation, safe)) return;
    if (hasPreviousState && safe != previousSafe) {
        ++result.boundaryOscillations;
        ++oscillationsAtRadius;
    }
    previousSafe = safe;
    hasPreviousState = true;
}

SolveStatus FailureStatus(const SearchEvaluation& evaluation) {
    switch (evaluation.disposition) {
        case EvaluationDisposition::Canceled: return SolveStatus::Canceled;
        case EvaluationDisposition::MissingEvidence: return SolveStatus::BlockedByMissingEvidence;
        case EvaluationDisposition::Failed: return SolveStatus::CandidateRenderFailed;
        case EvaluationDisposition::Complete:
        case EvaluationDisposition::Rejected: break;
    }
    return SolveStatus::CandidateRenderFailed;
}

void FinalizeSelection(
    SearchResult& result,
    const SearchCallbacks& callbacks,
    bool budgetExhausted,
    bool converged) {
    const ComparisonResult versusWarm = CompareEvaluations(result.selected, result.warmStart);
    const bool safeSelected = result.selected.disposition == EvaluationDisposition::Complete &&
        result.selected.hardConstraintsPassed;
    if (!safeSelected) {
        result.selected = result.warmStart;
        result.fallbackToWarmStart = true;
        result.status = result.warmStart.disposition == EvaluationDisposition::MissingEvidence
            ? SolveStatus::BlockedByMissingEvidence
            : SolveStatus::WarmStartRetained;
        result.reason = "No complete safe candidate was found; warm start retained as the explicit fallback record.";
        return;
    }

    if (result.budget.requireFullResolutionVerification) {
        if (!callbacks.verifyFullResolution) {
            result.status = SolveStatus::BlockedByMissingEvidence;
            result.reason = "Full-resolution verification is required but no verifier was supplied.";
            return;
        }
        result.fullVerification = callbacks.verifyFullResolution(result.selected);
        if (result.fullVerification != FullVerificationDisposition::Accepted) {
            const SearchEvaluation rejectedProxy = result.selected;
            (void)rejectedProxy;
            result.selected = result.warmStart;
            result.fallbackToWarmStart = true;
            result.status = result.fullVerification == FullVerificationDisposition::Canceled
                ? SolveStatus::Canceled
                : SolveStatus::FullResolutionVerificationRejected;
            result.reason = "Proxy selection failed full-resolution verification; warm start retained.";
            return;
        }
    }

    if (versusWarm == ComparisonResult::Better) {
        result.status = budgetExhausted
            ? SolveStatus::SafeImprovementBudgetExhausted
            : SolveStatus::Converged;
        result.reason = budgetExhausted
            ? "A lexicographically safe improvement was found before the evaluation/time budget ended. Budget exhaustion is not convergence."
            : "The selected candidate is a lexicographically meaningful safe improvement and the search radius/stability criteria passed.";
    } else {
        result.selected = result.warmStart;
        result.fallbackToWarmStart = true;
        result.status = SolveStatus::WarmStartRetained;
        result.reason = converged
            ? "No lexicographically meaningful improvement exceeded declared component uncertainty; warm start retained."
            : "No safe meaningful improvement was found within budget; warm start retained.";
    }
}

SearchResult BeginResult(
    SearchMethod method,
    const SearchProblem& problem,
    const SearchBudget& budget) {
    SearchResult result;
    result.method = method;
    result.problemId = problem.id;
    result.budget = budget;
    result.finalNormalizedRadius = budget.initialNormalizedRadius;
    return result;
}

bool InitializeWarm(
    const SearchProblem& problem,
    const SearchCallbacks& callbacks,
    EvaluationRunner& runner,
    SearchResult& result) {
    bool cacheHit = false;
    if (!runner.Evaluate(problem.warmStart, result.warmStart, cacheHit)) {
        result.status = runner.Canceled() ? SolveStatus::Canceled : SolveStatus::CandidateRenderFailed;
        result.reason = runner.Canceled()
            ? "Search canceled before warm-start evaluation."
            : "Warm-start evaluation could not run within the declared budget.";
        return false;
    }
    result.selected = result.warmStart;
    AddTrace(result, "warm-start", result.warmStart, ComparisonResult::Equivalent,
        result.budget.initialNormalizedRadius, true, cacheHit, "Frozen warm-start evaluation.");
    if (!UsableWarmStart(result.warmStart)) {
        result.status = FailureStatus(result.warmStart);
        result.reason = result.warmStart.reason;
        return false;
    }
    (void)callbacks;
    return true;
}

std::vector<std::size_t> OrderedDimensions(const SearchProblem& problem) {
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < problem.dimensions.size(); ++i) {
        if (problem.dimensions[i].active) result.push_back(i);
    }
    std::stable_sort(result.begin(), result.end(), [&](std::size_t a, std::size_t b) {
        if (problem.dimensions[a].stageOrder != problem.dimensions[b].stageOrder) {
            return problem.dimensions[a].stageOrder < problem.dimensions[b].stageOrder;
        }
        return a < b;
    });
    return result;
}

} // namespace

const char* SearchMethodName(SearchMethod method) {
    switch (method) {
        case SearchMethod::WarmStartOnly: return "warm-start-only";
        case SearchMethod::StageOrderedPatternSearch: return "stage-ordered-pattern-search";
        case SearchMethod::DiagonalQuadraticTrustRegion: return "diagonal-quadratic-trust-region";
    }
    return "unknown";
}

const char* MeritTierName(MeritTier tier) {
    switch (tier) {
        case MeritTier::Tier0IdentityState: return "tier-0-identity-state";
        case MeritTier::Tier1RawArtifact: return "tier-1-raw-artifact";
        case MeritTier::Tier2Technical: return "tier-2-technical";
        case MeritTier::Tier3Display: return "tier-3-display";
        case MeritTier::Tier5TieBreaker: return "tier-5-tie-breaker";
    }
    return "unknown";
}

const char* MeritRoleName(MeritRole role) {
    switch (role) {
        case MeritRole::ConstraintViolation: return "constraint-violation";
        case MeritRole::GoalViolation: return "goal-violation";
        case MeritRole::TieBreaker: return "tie-breaker";
    }
    return "unknown";
}

const char* EvaluationDispositionName(EvaluationDisposition disposition) {
    switch (disposition) {
        case EvaluationDisposition::Complete: return "complete";
        case EvaluationDisposition::Rejected: return "rejected";
        case EvaluationDisposition::Failed: return "failed";
        case EvaluationDisposition::Canceled: return "canceled";
        case EvaluationDisposition::MissingEvidence: return "missing-evidence";
    }
    return "unknown";
}

const char* ComparisonResultName(ComparisonResult result) {
    switch (result) {
        case ComparisonResult::Better: return "better";
        case ComparisonResult::Equivalent: return "equivalent";
        case ComparisonResult::Worse: return "worse";
        case ComparisonResult::Incomparable: return "incomparable";
    }
    return "unknown";
}

const char* SolveStatusName(SolveStatus status) {
    switch (status) {
        case SolveStatus::Converged: return "converged";
        case SolveStatus::SafeImprovementBudgetExhausted: return "safe-improvement-budget-exhausted";
        case SolveStatus::WarmStartRetained: return "warm-start-retained";
        case SolveStatus::BlockedByMissingEvidence: return "blocked-by-missing-evidence";
        case SolveStatus::Canceled: return "canceled";
        case SolveStatus::CandidateRenderFailed: return "candidate-render-failed";
        case SolveStatus::FullResolutionVerificationRejected: return "full-resolution-verification-rejected";
    }
    return "unknown";
}

const char* FullVerificationDispositionName(FullVerificationDisposition disposition) {
    switch (disposition) {
        case FullVerificationDisposition::NotRequested: return "not-requested";
        case FullVerificationDisposition::Accepted: return "accepted";
        case FullVerificationDisposition::Rejected: return "rejected";
        case FullVerificationDisposition::Failed: return "failed";
        case FullVerificationDisposition::Canceled: return "canceled";
    }
    return "unknown";
}

bool ValidateProblem(const SearchProblem& problem, std::string& reason) {
    if (problem.id.empty() || problem.dimensions.empty() ||
        problem.warmStart.size() != problem.dimensions.size()) {
        reason = "Problem identity, dimensions, or warm start is incomplete.";
        return false;
    }
    for (std::size_t i = 0; i < problem.dimensions.size(); ++i) {
        const DimensionSpec& dimension = problem.dimensions[i];
        if (dimension.id.empty() || !Finite(dimension.lower) || !Finite(dimension.upper) ||
            dimension.upper < dimension.lower || !Finite(problem.warmStart[i]) ||
            problem.warmStart[i] < dimension.lower - kEpsilon ||
            problem.warmStart[i] > dimension.upper + kEpsilon) {
            reason = "Dimension or warm-start bound is invalid at index " + std::to_string(i) + ".";
            return false;
        }
        if (!dimension.discreteValues.empty()) {
            for (double value : dimension.discreteValues) {
                if (!Finite(value) || value < dimension.lower - kEpsilon || value > dimension.upper + kEpsilon) {
                    reason = "Discrete dimension value is outside its bound.";
                    return false;
                }
            }
        }
    }
    for (const auto& pair : problem.interactionPairs) {
        if (pair.first >= problem.dimensions.size() || pair.second >= problem.dimensions.size() ||
            pair.first == pair.second) {
            reason = "Interaction pair index is invalid.";
            return false;
        }
    }
    return true;
}

ComparisonResult CompareEvaluations(
    const SearchEvaluation& candidate,
    const SearchEvaluation& incumbent) {
    if (candidate.disposition == EvaluationDisposition::Canceled ||
        candidate.disposition == EvaluationDisposition::Failed ||
        candidate.disposition == EvaluationDisposition::MissingEvidence) {
        return ComparisonResult::Worse;
    }
    if (incumbent.disposition == EvaluationDisposition::Canceled ||
        incumbent.disposition == EvaluationDisposition::Failed ||
        incumbent.disposition == EvaluationDisposition::MissingEvidence) {
        return ComparisonResult::Better;
    }
    if (candidate.disposition != incumbent.disposition) {
        return DispositionRank(candidate.disposition) < DispositionRank(incumbent.disposition)
            ? ComparisonResult::Better
            : ComparisonResult::Worse;
    }
    if (candidate.merit.size() != incumbent.merit.size()) return ComparisonResult::Incomparable;
    const std::vector<std::size_t> order = OrderedMeritIndices(candidate);
    for (std::size_t index : order) {
        const MeritComponent& a = candidate.merit[index];
        const MeritComponent& b = incumbent.merit[index];
        if (a.id != b.id || a.tier != b.tier || a.role != b.role) {
            return ComparisonResult::Incomparable;
        }
        if (a.valid != b.valid) return a.valid ? ComparisonResult::Better : ComparisonResult::Worse;
        if (!a.valid) continue;
        if (!Finite(a.value) || !Finite(b.value)) return ComparisonResult::Incomparable;
        const double tolerance = ComparisonTolerance(a, b);
        if (a.value < b.value - tolerance) return ComparisonResult::Better;
        if (a.value > b.value + tolerance) return ComparisonResult::Worse;
    }
    return ComparisonResult::Equivalent;
}

SearchResult RunWarmStartOnly(
    const SearchProblem& problem,
    const SearchBudget& budget,
    const SearchCallbacks& callbacks) {
    SearchResult result = BeginResult(SearchMethod::WarmStartOnly, problem, budget);
    std::string validationReason;
    if (!ValidateProblem(problem, validationReason)) {
        result.reason = validationReason;
        return result;
    }
    EvaluationRunner runner(problem, budget, callbacks, result);
    if (!InitializeWarm(problem, callbacks, runner, result)) {
        result.runtimeMs = runner.ElapsedMs();
        return result;
    }
    result.finalNormalizedRadius = 0.0;
    FinalizeSelection(result, callbacks, false, true);
    result.runtimeMs = runner.ElapsedMs();
    return result;
}

SearchResult RunStageOrderedPatternSearch(
    const SearchProblem& problem,
    const SearchBudget& budget,
    const SearchCallbacks& callbacks) {
    SearchResult result = BeginResult(SearchMethod::StageOrderedPatternSearch, problem, budget);
    std::string validationReason;
    if (!ValidateProblem(problem, validationReason)) {
        result.reason = validationReason;
        return result;
    }
    EvaluationRunner runner(problem, budget, callbacks, result);
    if (!InitializeWarm(problem, callbacks, runner, result)) {
        result.runtimeMs = runner.ElapsedMs();
        return result;
    }

    const std::vector<std::size_t> dimensions = OrderedDimensions(problem);
    double radius = std::clamp(
        budget.initialNormalizedRadius,
        budget.minimumNormalizedRadius,
        budget.maximumNormalizedRadius);
    bool budgetExhausted = false;
    bool converged = false;
    int stableSweeps = 0;
    std::vector<int> lastDirections(problem.dimensions.size(), 0);
    bool hasBoundaryState = false;
    bool previousBoundarySafe = false;
    int oscillationsAtRadius = 0;
    ObserveConstraintBoundary(result.warmStart, result, hasBoundaryState,
        previousBoundarySafe, oscillationsAtRadius);

    while (!dimensions.empty()) {
        if (runner.Canceled()) {
            result.status = SolveStatus::Canceled;
            result.reason = "Pattern search canceled by generation/source callback.";
            break;
        }
        if (!runner.BudgetAvailable() || result.acceptedIterations >= budget.maxAcceptedIterations) {
            budgetExhausted = true;
            break;
        }
        bool sweepImproved = false;
        bool boundaryShrinkRequested = false;
        for (std::size_t dimensionIndex : dimensions) {
            const DimensionSpec& dimension = problem.dimensions[dimensionIndex];
            for (int direction : { -1, 1 }) {
                std::vector<double> point = result.selected.point;
                point[dimensionIndex] = OffsetDimensionValue(
                    point[dimensionIndex], dimension, direction, radius);
                point = NormalizePoint(problem, std::move(point));
                if (SamePoint(point, result.selected.point)) continue;
                SearchEvaluation evaluation;
                bool cacheHit = false;
                if (!runner.Evaluate(point, evaluation, cacheHit)) {
                    budgetExhausted = !runner.Canceled();
                    break;
                }
                ObserveConstraintBoundary(evaluation, result, hasBoundaryState,
                    previousBoundarySafe, oscillationsAtRadius);
                const ComparisonResult comparison = CompareEvaluations(evaluation, result.selected);
                const bool accepted = comparison == ComparisonResult::Better;
                AddTrace(result, "coordinate", evaluation, comparison, radius, accepted, cacheHit,
                    accepted ? "Lexicographically meaningful coordinate step accepted."
                             : "Coordinate step rejected or within declared uncertainty.");
                if (accepted) {
                    result.selected = evaluation;
                    ++result.acceptedIterations;
                    sweepImproved = true;
                    lastDirections[dimensionIndex] = direction;

                    std::vector<double> patternPoint = result.selected.point;
                    patternPoint[dimensionIndex] = OffsetDimensionValue(
                        patternPoint[dimensionIndex], dimension, direction, radius);
                    patternPoint = NormalizePoint(problem, std::move(patternPoint));
                    if (!SamePoint(patternPoint, result.selected.point) && runner.BudgetAvailable()) {
                        SearchEvaluation patternEvaluation;
                        bool patternCache = false;
                        if (runner.Evaluate(patternPoint, patternEvaluation, patternCache)) {
                            ObserveConstraintBoundary(patternEvaluation, result, hasBoundaryState,
                                previousBoundarySafe, oscillationsAtRadius);
                            const ComparisonResult patternComparison =
                                CompareEvaluations(patternEvaluation, result.selected);
                            const bool patternAccepted = patternComparison == ComparisonResult::Better;
                            AddTrace(result, "pattern-extension", patternEvaluation, patternComparison,
                                radius, patternAccepted, patternCache,
                                patternAccepted ? "Same-direction pattern extension accepted."
                                                : "Pattern extension rejected; accepted coordinate state retained.");
                            if (patternAccepted) {
                                result.selected = patternEvaluation;
                                ++result.acceptedIterations;
                            }
                        }
                    }
                    boundaryShrinkRequested = oscillationsAtRadius >=
                        budget.maximumBoundaryOscillations;
                    break;
                }
                boundaryShrinkRequested = oscillationsAtRadius >=
                    budget.maximumBoundaryOscillations;
                if (boundaryShrinkRequested) break;
            }
            if (boundaryShrinkRequested || budgetExhausted || runner.Canceled() ||
                result.acceptedIterations >= budget.maxAcceptedIterations) break;
        }

        if (!sweepImproved && !boundaryShrinkRequested && runner.BudgetAvailable()) {
            for (const auto& interaction : problem.interactionPairs) {
                if (!problem.dimensions[interaction.first].active ||
                    !problem.dimensions[interaction.second].active) continue;
                bool interactionAccepted = false;
                for (const auto signs : { std::pair<int, int>{ -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } }) {
                    std::vector<double> point = result.selected.point;
                    point[interaction.first] = OffsetDimensionValue(
                        point[interaction.first], problem.dimensions[interaction.first], signs.first, radius);
                    point[interaction.second] = OffsetDimensionValue(
                        point[interaction.second], problem.dimensions[interaction.second], signs.second, radius);
                    point = NormalizePoint(problem, std::move(point));
                    if (SamePoint(point, result.selected.point)) continue;
                    SearchEvaluation evaluation;
                    bool cacheHit = false;
                    if (!runner.Evaluate(point, evaluation, cacheHit)) {
                        budgetExhausted = !runner.Canceled();
                        break;
                    }
                    ObserveConstraintBoundary(evaluation, result, hasBoundaryState,
                        previousBoundarySafe, oscillationsAtRadius);
                    const ComparisonResult comparison = CompareEvaluations(evaluation, result.selected);
                    const bool accepted = comparison == ComparisonResult::Better;
                    AddTrace(result, "interaction", evaluation, comparison, radius, accepted, cacheHit,
                        accepted ? "Declared pair-direction interaction accepted."
                                 : "Declared interaction did not improve the lexicographic record.");
                    if (accepted) {
                        result.selected = evaluation;
                        ++result.acceptedIterations;
                        sweepImproved = true;
                        interactionAccepted = true;
                        break;
                    }
                    boundaryShrinkRequested = oscillationsAtRadius >=
                        budget.maximumBoundaryOscillations;
                    if (boundaryShrinkRequested) break;
                }
                if (interactionAccepted || boundaryShrinkRequested || budgetExhausted) break;
            }
        }

        if (boundaryShrinkRequested) {
            stableSweeps = sweepImproved ? 0 : stableSweeps + 1;
            radius *= budget.shrinkFactor;
            oscillationsAtRadius = 0;
            AddTrace(result, "boundary-radius-shrink", result.selected,
                ComparisonResult::Equivalent, radius, false, true,
                "Safe/rejected boundary oscillation limit reached; the normalized radius shrank while the last safe accepted state was retained.");
        } else if (sweepImproved) {
            stableSweeps = 0;
        } else {
            ++stableSweeps;
            radius *= budget.shrinkFactor;
            AddTrace(result, "radius-shrink", result.selected, ComparisonResult::Equivalent,
                radius, false, true, "No meaningful accepted step; normalized pattern radius shrank.");
        }
        result.stableIterations = stableSweeps;
        result.finalNormalizedRadius = radius;
        if (radius < budget.minimumNormalizedRadius - kEpsilon &&
            stableSweeps >= budget.stableIterationsRequired) {
            converged = true;
            break;
        }
        if (budgetExhausted) break;
    }

    if (runner.Canceled()) {
        result.status = SolveStatus::Canceled;
        result.reason = "Pattern search canceled; no candidate applied.";
    } else {
        FinalizeSelection(result, callbacks, budgetExhausted, converged);
    }
    result.runtimeMs = runner.ElapsedMs();
    return result;
}

SearchResult RunDiagonalQuadraticTrustRegion(
    const SearchProblem& problem,
    const SearchBudget& budget,
    const SearchCallbacks& callbacks) {
    SearchResult result = BeginResult(SearchMethod::DiagonalQuadraticTrustRegion, problem, budget);
    std::string validationReason;
    if (!ValidateProblem(problem, validationReason)) {
        result.reason = validationReason;
        return result;
    }
    EvaluationRunner runner(problem, budget, callbacks, result);
    if (!InitializeWarm(problem, callbacks, runner, result)) {
        result.runtimeMs = runner.ElapsedMs();
        return result;
    }

    const std::vector<std::size_t> dimensions = OrderedDimensions(problem);
    double radius = std::clamp(
        budget.initialNormalizedRadius,
        budget.minimumNormalizedRadius,
        budget.maximumNormalizedRadius);
    bool budgetExhausted = false;
    bool converged = false;
    int stableIterations = 0;
    bool hasBoundaryState = false;
    bool previousBoundarySafe = false;
    int oscillationsAtRadius = 0;
    ObserveConstraintBoundary(result.warmStart, result, hasBoundaryState,
        previousBoundarySafe, oscillationsAtRadius);

    while (!dimensions.empty()) {
        if (runner.Canceled()) {
            result.status = SolveStatus::Canceled;
            result.reason = "Trust-region benchmark canceled by generation/source callback.";
            break;
        }
        if (!runner.BudgetAvailable() || result.acceptedIterations >= budget.maxAcceptedIterations) {
            budgetExhausted = true;
            break;
        }

        const SearchEvaluation center = result.selected;
        const int activeMerit = ActiveModelComponent(center);
        if (activeMerit < 0) {
            ++stableIterations;
            radius *= budget.shrinkFactor;
            result.stableIterations = stableIterations;
            result.finalNormalizedRadius = radius;
            if (radius < budget.minimumNormalizedRadius - kEpsilon &&
                stableIterations >= budget.stableIterationsRequired) {
                converged = true;
                break;
            }
            continue;
        }
        const double f0 = MeritValue(center, activeMerit);
        std::vector<double> gradient(problem.dimensions.size(), 0.0);
        std::vector<double> curvature(problem.dimensions.size(), 0.0);
        bool modelAvailable = false;
        SearchEvaluation bestSite = center;
        bool boundaryShrinkRequested = false;

        for (std::size_t index : dimensions) {
            const double step = PhysicalStep(problem.dimensions[index], radius);
            if (step <= kEpsilon) continue;
            SearchEvaluation minus;
            SearchEvaluation plus;
            bool minusValid = false;
            bool plusValid = false;
            for (int direction : { -1, 1 }) {
                std::vector<double> point = center.point;
                point[index] = OffsetDimensionValue(
                    point[index], problem.dimensions[index], direction, radius);
                point = NormalizePoint(problem, std::move(point));
                if (SamePoint(point, center.point)) continue;
                SearchEvaluation evaluation;
                bool cacheHit = false;
                if (!runner.Evaluate(point, evaluation, cacheHit)) {
                    budgetExhausted = !runner.Canceled();
                    break;
                }
                ObserveConstraintBoundary(evaluation, result, hasBoundaryState,
                    previousBoundarySafe, oscillationsAtRadius);
                const ComparisonResult comparison = CompareEvaluations(evaluation, bestSite);
                if (comparison == ComparisonResult::Better) bestSite = evaluation;
                AddTrace(result, "model-site", evaluation,
                    CompareEvaluations(evaluation, center), radius, false, cacheHit,
                    "Interpolation site measured; acceptance remains lexicographic.", activeMerit);
                if (direction < 0) {
                    minus = evaluation;
                    minusValid = Finite(MeritValue(evaluation, activeMerit));
                } else {
                    plus = evaluation;
                    plusValid = Finite(MeritValue(evaluation, activeMerit));
                }
                boundaryShrinkRequested = oscillationsAtRadius >=
                    budget.maximumBoundaryOscillations;
                if (boundaryShrinkRequested) break;
            }
            if (boundaryShrinkRequested || budgetExhausted) break;
            if (minusValid && plusValid) {
                const double fm = MeritValue(minus, activeMerit);
                const double fp = MeritValue(plus, activeMerit);
                gradient[index] = (fp - fm) / (2.0 * step);
                curvature[index] = (fp - 2.0 * f0 + fm) / (step * step);
                modelAvailable = true;
            } else if (plusValid) {
                gradient[index] = (MeritValue(plus, activeMerit) - f0) / step;
                modelAvailable = true;
            } else if (minusValid) {
                gradient[index] = (f0 - MeritValue(minus, activeMerit)) / step;
                modelAvailable = true;
            }
        }
        if (budgetExhausted) break;

        if (boundaryShrinkRequested) {
            const bool acceptedSite =
                CompareEvaluations(bestSite, result.selected) == ComparisonResult::Better;
            if (acceptedSite) {
                result.selected = bestSite;
                ++result.acceptedIterations;
                stableIterations = 0;
            } else {
                ++stableIterations;
            }
            radius *= budget.shrinkFactor;
            oscillationsAtRadius = 0;
            AddTrace(result, "boundary-radius-shrink", result.selected,
                ComparisonResult::Equivalent, radius, acceptedSite, true,
                "Safe/rejected interpolation sites reached the boundary oscillation limit; the radius shrank and the last safe accepted state was retained.",
                activeMerit);
            result.stableIterations = stableIterations;
            result.finalNormalizedRadius = radius;
            if (radius < budget.minimumNormalizedRadius - kEpsilon &&
                stableIterations >= budget.stableIterationsRequired) {
                converged = true;
                break;
            }
            continue;
        }

        if (CompareEvaluations(bestSite, result.selected) == ComparisonResult::Better) {
            result.selected = bestSite;
            ++result.acceptedIterations;
            stableIterations = 0;
            AddTrace(result, "model-site-accepted", bestSite, ComparisonResult::Better,
                radius, true, true, "Best interpolation site accepted before model trial.", activeMerit);
            continue;
        }

        if (!modelAvailable) {
            ++stableIterations;
            radius *= budget.shrinkFactor;
            result.stableIterations = stableIterations;
            result.finalNormalizedRadius = radius;
            if (radius < budget.minimumNormalizedRadius - kEpsilon &&
                stableIterations >= budget.stableIterationsRequired) converged = true;
            if (converged) break;
            continue;
        }

        std::vector<double> trialPoint = center.point;
        std::vector<double> normalizedStep(problem.dimensions.size(), 0.0);
        for (std::size_t index : dimensions) {
            const double span = problem.dimensions[index].upper - problem.dimensions[index].lower;
            if (span <= kEpsilon) continue;
            double physical = 0.0;
            if (curvature[index] > 1.0e-9) {
                physical = -gradient[index] / curvature[index];
            } else if (std::abs(gradient[index]) > 1.0e-12) {
                physical = -std::copysign(PhysicalStep(problem.dimensions[index], radius), gradient[index]);
            }
            normalizedStep[index] = std::clamp(physical / span, -radius, radius);
        }
        double norm = 0.0;
        for (double value : normalizedStep) norm += value * value;
        norm = std::sqrt(norm);
        if (norm > radius && norm > kEpsilon) {
            const double scale = radius / norm;
            for (double& value : normalizedStep) value *= scale;
        }
        for (std::size_t index : dimensions) {
            const double span = problem.dimensions[index].upper - problem.dimensions[index].lower;
            trialPoint[index] += normalizedStep[index] * span;
        }
        trialPoint = NormalizePoint(problem, std::move(trialPoint));
        if (SamePoint(trialPoint, center.point)) {
            ++stableIterations;
            radius *= budget.shrinkFactor;
            result.stableIterations = stableIterations;
            result.finalNormalizedRadius = radius;
            if (radius < budget.minimumNormalizedRadius - kEpsilon &&
                stableIterations >= budget.stableIterationsRequired) converged = true;
            if (converged) break;
            continue;
        }

        double predictedReduction = 0.0;
        for (std::size_t index : dimensions) {
            const double span = problem.dimensions[index].upper - problem.dimensions[index].lower;
            const double physical = normalizedStep[index] * span;
            predictedReduction -= gradient[index] * physical +
                0.5 * curvature[index] * physical * physical;
        }
        SearchEvaluation trial;
        bool cacheHit = false;
        if (!runner.Evaluate(trialPoint, trial, cacheHit)) {
            budgetExhausted = !runner.Canceled();
            break;
        }
        ObserveConstraintBoundary(trial, result, hasBoundaryState,
            previousBoundarySafe, oscillationsAtRadius);
        const double actualReduction = f0 - MeritValue(trial, activeMerit);
        const double trustRatio = predictedReduction > kEpsilon
            ? actualReduction / predictedReduction
            : -std::numeric_limits<double>::infinity();
        const ComparisonResult comparison = CompareEvaluations(trial, result.selected);
        const bool accepted = comparison == ComparisonResult::Better;
        AddTrace(result, "model-trial", trial, comparison, radius, accepted, cacheHit,
            accepted ? "Model trial passed lexicographic acceptance."
                     : "Model trial rejected; radius will shrink.",
            activeMerit, predictedReduction, actualReduction, trustRatio);
        if (accepted) {
            result.selected = trial;
            ++result.acceptedIterations;
            stableIterations = 0;
            if (trustRatio > 0.75 && norm >= 0.80 * radius) {
                radius = std::min(budget.maximumNormalizedRadius, radius * budget.expandFactor);
            }
        } else {
            ++stableIterations;
            radius *= budget.shrinkFactor;
        }
        if (oscillationsAtRadius >= budget.maximumBoundaryOscillations) {
            radius *= budget.shrinkFactor;
            oscillationsAtRadius = 0;
            AddTrace(result, "boundary-radius-shrink", result.selected,
                ComparisonResult::Equivalent, radius, false, true,
                "The model trial crossed the constraint boundary repeatedly; the trust radius shrank.",
                activeMerit);
        }
        result.stableIterations = stableIterations;
        result.finalNormalizedRadius = radius;
        if (radius < budget.minimumNormalizedRadius - kEpsilon &&
            stableIterations >= budget.stableIterationsRequired) {
            converged = true;
            break;
        }
    }

    if (runner.Canceled()) {
        result.status = SolveStatus::Canceled;
        result.reason = "Trust-region benchmark canceled; no candidate applied.";
    } else {
        FinalizeSelection(result, callbacks, budgetExhausted, converged);
    }
    result.runtimeMs = runner.ElapsedMs();
    return result;
}

nlohmann::json SerializeDimension(const DimensionSpec& dimension) {
    return {
        { "id", dimension.id }, { "lower", dimension.lower }, { "upper", dimension.upper },
        { "active", dimension.active }, { "stageOrder", dimension.stageOrder },
        { "discreteValues", dimension.discreteValues }
    };
}

nlohmann::json SerializeMerit(const MeritComponent& merit) {
    return {
        { "id", merit.id }, { "tier", MeritTierName(merit.tier) },
        { "role", MeritRoleName(merit.role) }, { "valid", merit.valid },
        { "value", merit.valid ? nlohmann::json(merit.value) : nlohmann::json(nullptr) },
        { "meaningfulDelta", merit.meaningfulDelta }, { "uncertainty", merit.uncertainty },
        { "units", merit.units }, { "reason", merit.reason }
    };
}

nlohmann::json SerializeEvaluation(const SearchEvaluation& evaluation) {
    nlohmann::json merit = nlohmann::json::array();
    for (const MeritComponent& component : evaluation.merit) merit.push_back(SerializeMerit(component));
    return {
        { "candidateId", evaluation.candidateId },
        { "disposition", EvaluationDispositionName(evaluation.disposition) },
        { "point", evaluation.point }, { "merit", std::move(merit) },
        { "hardConstraintsPassed", evaluation.hardConstraintsPassed },
        { "fromCache", evaluation.fromCache }, { "runtimeMs", evaluation.runtimeMs },
        { "combinedTotalScore", nullptr }, { "reason", evaluation.reason }
    };
}

nlohmann::json SerializeBudget(const SearchBudget& budget) {
    return {
        { "version", budget.version }, { "maxUniqueEvaluations", budget.maxUniqueEvaluations },
        { "maxAcceptedIterations", budget.maxAcceptedIterations },
        { "stableIterationsRequired", budget.stableIterationsRequired },
        { "maxRuntimeMs", budget.maxRuntimeMs },
        { "initialNormalizedRadius", budget.initialNormalizedRadius },
        { "minimumNormalizedRadius", budget.minimumNormalizedRadius },
        { "maximumNormalizedRadius", budget.maximumNormalizedRadius },
        { "shrinkFactor", budget.shrinkFactor }, { "expandFactor", budget.expandFactor },
        { "maximumBoundaryOscillations", budget.maximumBoundaryOscillations },
        { "requireFullResolutionVerification", budget.requireFullResolutionVerification }
    };
}

nlohmann::json SerializeTraceEvent(const SearchTraceEvent& event) {
    return {
        { "sequence", event.sequence }, { "kind", event.kind },
        { "candidateId", event.candidateId }, { "point", event.point },
        { "comparison", ComparisonResultName(event.comparison) },
        { "normalizedRadius", event.normalizedRadius },
        { "activeMeritIndex", event.activeMeritIndex },
        { "predictedReduction", event.predictedReduction },
        { "actualReduction", event.actualReduction }, { "trustRatio", event.trustRatio },
        { "accepted", event.accepted }, { "cacheHit", event.cacheHit },
        { "reason", event.reason }
    };
}

nlohmann::json SerializeSearchResult(const SearchResult& result) {
    nlohmann::json trace = nlohmann::json::array();
    for (const SearchTraceEvent& event : result.trace) trace.push_back(SerializeTraceEvent(event));
    return {
        { "schemaVersion", result.schemaVersion }, { "benchmarkVersion", result.benchmarkVersion },
        { "convergenceVersion", result.convergenceVersion },
        { "method", SearchMethodName(result.method) }, { "status", SolveStatusName(result.status) },
        { "budget", SerializeBudget(result.budget) }, { "problemId", result.problemId },
        { "warmStart", SerializeEvaluation(result.warmStart) },
        { "selected", SerializeEvaluation(result.selected) },
        { "fullVerification", FullVerificationDispositionName(result.fullVerification) },
        { "proposedEvaluations", result.proposedEvaluations },
        { "uniqueEvaluations", result.uniqueEvaluations }, { "cacheHits", result.cacheHits },
        { "acceptedIterations", result.acceptedIterations },
        { "rejectedEvaluations", result.rejectedEvaluations },
        { "failedEvaluations", result.failedEvaluations },
        { "boundaryOscillations", result.boundaryOscillations },
        { "stableIterations", result.stableIterations },
        { "finalNormalizedRadius", result.finalNormalizedRadius },
        { "runtimeMs", result.runtimeMs }, { "deterministicOrder", result.deterministicOrder },
        { "currentRecipeUnchanged", result.currentRecipeUnchanged },
        { "undoHistoryUnchanged", result.undoHistoryUnchanged },
        { "projectDirtyStateUnchanged", result.projectDirtyStateUnchanged },
        { "fallbackToWarmStart", result.fallbackToWarmStart },
        { "combinedTotalScore", nullptr }, { "trace", std::move(trace) },
        { "reason", result.reason }
    };
}

} // namespace Stack::RawOptimizer
