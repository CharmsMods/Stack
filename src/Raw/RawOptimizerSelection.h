#pragma once

#include "ThirdParty/json.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Stack::RawOptimizer {

inline constexpr int kOptimizerBenchmarkSchemaVersion = 1;
inline constexpr const char* kOptimizerBenchmarkVersion = "raw-optimizer-benchmark-v1";
inline constexpr const char* kSelectedOptimizerVersion = "stage-ordered-pattern-search-v1";
inline constexpr const char* kTrustRegionBenchmarkVersion = "diagonal-quadratic-trust-region-v1";
inline constexpr const char* kConvergenceContractVersion = "raw-convergence-contract-v1";
inline constexpr const char* kEvaluationBudgetVersion = "raw-precise-budget-v1";

enum class SearchMethod {
    WarmStartOnly,
    StageOrderedPatternSearch,
    DiagonalQuadraticTrustRegion
};

enum class MeritTier {
    Tier0IdentityState,
    Tier1RawArtifact,
    Tier2Technical,
    Tier3Display,
    Tier5TieBreaker
};

enum class MeritRole {
    ConstraintViolation,
    GoalViolation,
    TieBreaker
};

enum class EvaluationDisposition {
    Complete,
    Rejected,
    Failed,
    Canceled,
    MissingEvidence
};

enum class ComparisonResult {
    Better,
    Equivalent,
    Worse,
    Incomparable
};

enum class SolveStatus {
    Converged,
    SafeImprovementBudgetExhausted,
    WarmStartRetained,
    BlockedByMissingEvidence,
    Canceled,
    CandidateRenderFailed,
    FullResolutionVerificationRejected
};

enum class FullVerificationDisposition {
    NotRequested,
    Accepted,
    Rejected,
    Failed,
    Canceled
};

struct DimensionSpec {
    std::string id;
    double lower = 0.0;
    double upper = 1.0;
    bool active = true;
    int stageOrder = 0;
    std::vector<double> discreteValues;
};

struct MeritComponent {
    std::string id;
    MeritTier tier = MeritTier::Tier2Technical;
    MeritRole role = MeritRole::GoalViolation;
    bool valid = false;
    double value = 0.0;
    double meaningfulDelta = 0.0;
    double uncertainty = 0.0;
    std::string units;
    std::string reason;
};

struct SearchEvaluation {
    std::string candidateId;
    EvaluationDisposition disposition = EvaluationDisposition::Failed;
    std::vector<double> point;
    std::vector<MeritComponent> merit;
    bool hardConstraintsPassed = false;
    bool fromCache = false;
    double runtimeMs = 0.0;
    std::string reason;
};

struct SearchBudget {
    std::string version = kEvaluationBudgetVersion;
    int maxUniqueEvaluations = 48;
    int maxAcceptedIterations = 16;
    int stableIterationsRequired = 2;
    double maxRuntimeMs = 60000.0;
    double initialNormalizedRadius = 0.25;
    double minimumNormalizedRadius = 0.03125;
    double maximumNormalizedRadius = 0.50;
    double shrinkFactor = 0.50;
    double expandFactor = 1.50;
    int maximumBoundaryOscillations = 4;
    bool requireFullResolutionVerification = false;
};

struct SearchProblem {
    std::string id;
    std::vector<DimensionSpec> dimensions;
    std::vector<double> warmStart;
    std::vector<std::pair<std::size_t, std::size_t>> interactionPairs;
};

struct SearchCallbacks {
    std::function<SearchEvaluation(const std::vector<double>& point)> evaluate;
    std::function<bool()> shouldCancel;
    std::function<FullVerificationDisposition(const SearchEvaluation& evaluation)> verifyFullResolution;
};

struct SearchTraceEvent {
    int sequence = 0;
    std::string kind;
    std::string candidateId;
    std::vector<double> point;
    ComparisonResult comparison = ComparisonResult::Incomparable;
    double normalizedRadius = 0.0;
    int activeMeritIndex = -1;
    double predictedReduction = 0.0;
    double actualReduction = 0.0;
    double trustRatio = 0.0;
    bool accepted = false;
    bool cacheHit = false;
    std::string reason;
};

struct SearchResult {
    int schemaVersion = kOptimizerBenchmarkSchemaVersion;
    std::string benchmarkVersion = kOptimizerBenchmarkVersion;
    std::string convergenceVersion = kConvergenceContractVersion;
    SearchMethod method = SearchMethod::WarmStartOnly;
    SolveStatus status = SolveStatus::CandidateRenderFailed;
    SearchBudget budget;
    std::string problemId;
    SearchEvaluation warmStart;
    SearchEvaluation selected;
    FullVerificationDisposition fullVerification = FullVerificationDisposition::NotRequested;
    int proposedEvaluations = 0;
    int uniqueEvaluations = 0;
    int cacheHits = 0;
    int acceptedIterations = 0;
    int rejectedEvaluations = 0;
    int failedEvaluations = 0;
    int boundaryOscillations = 0;
    int stableIterations = 0;
    double finalNormalizedRadius = 0.0;
    double runtimeMs = 0.0;
    bool deterministicOrder = true;
    bool currentRecipeUnchanged = true;
    bool undoHistoryUnchanged = true;
    bool projectDirtyStateUnchanged = true;
    bool fallbackToWarmStart = false;
    std::vector<SearchTraceEvent> trace;
    std::string reason;
};

const char* SearchMethodName(SearchMethod method);
const char* MeritTierName(MeritTier tier);
const char* MeritRoleName(MeritRole role);
const char* EvaluationDispositionName(EvaluationDisposition disposition);
const char* ComparisonResultName(ComparisonResult result);
const char* SolveStatusName(SolveStatus status);
const char* FullVerificationDispositionName(FullVerificationDisposition disposition);

bool ValidateProblem(const SearchProblem& problem, std::string& reason);
ComparisonResult CompareEvaluations(
    const SearchEvaluation& candidate,
    const SearchEvaluation& incumbent);

SearchResult RunWarmStartOnly(
    const SearchProblem& problem,
    const SearchBudget& budget,
    const SearchCallbacks& callbacks);
SearchResult RunStageOrderedPatternSearch(
    const SearchProblem& problem,
    const SearchBudget& budget,
    const SearchCallbacks& callbacks);
SearchResult RunDiagonalQuadraticTrustRegion(
    const SearchProblem& problem,
    const SearchBudget& budget,
    const SearchCallbacks& callbacks);

nlohmann::json SerializeDimension(const DimensionSpec& dimension);
nlohmann::json SerializeMerit(const MeritComponent& merit);
nlohmann::json SerializeEvaluation(const SearchEvaluation& evaluation);
nlohmann::json SerializeBudget(const SearchBudget& budget);
nlohmann::json SerializeTraceEvent(const SearchTraceEvent& event);
nlohmann::json SerializeSearchResult(const SearchResult& result);

} // namespace Stack::RawOptimizer
