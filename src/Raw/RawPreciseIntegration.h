#pragma once

#include "Raw/RawPreciseDryRun.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Stack::PreciseIntegration {

inline constexpr const char* kIntegrationVersion = "raw-precise-integration-v1";

enum class ProductMode {
    Precise,
    Fast
};

enum class LifecycleState {
    Idle,
    Queued,
    Running,
    Applying,
    Applied,
    AppliedNoChange,
    Blocked,
    Canceled,
    Failed
};

struct SolveIdentity {
    std::uint64_t requestId = 0;
    std::uint64_t generation = 0;
    std::string sourceKey;
    std::uint64_t sourceHash = 0;
    std::string sourceIdentity;
    std::string inputRecipeIdentity;
};

struct VerifiedCandidate {
    bool valid = false;
    SolveIdentity identity;
    std::string integrationVersion = kIntegrationVersion;
    std::string solverVersion;
    RawOptimizer::SolveStatus solveStatus =
        RawOptimizer::SolveStatus::CandidateRenderFailed;
    RawOptimizer::FullVerificationDisposition fullVerification =
        RawOptimizer::FullVerificationDisposition::NotRequested;
    PreciseRaw::CandidateProposal selected;
    PreciseRaw::CandidateEvaluationRecord fullEvaluation;
    bool candidateEligibleForApply = false;
    bool candidateRecipeRoundTripExact = false;
    bool candidateVisibleParametersRoundTripExact = false;
    bool allFiveFullResolutionStages = false;
    bool trueFullResolution = false;
    bool recipeAppliedDuringSolve = false;
    bool appliedPreviewTextureCreated = false;
    int proxyRenderCount = 0;
    int proxyCacheHits = 0;
    int fullWidth = 0;
    int fullHeight = 0;
    std::string reason;
    std::vector<std::string> warnings;
};

struct IntegrationState {
    ProductMode mode = ProductMode::Precise;
    LifecycleState state = LifecycleState::Idle;
    bool active = false;
    std::uint64_t nextRequestId = 1;
    SolveIdentity identity;
    RawRecipe::RawDevelopmentRecipe originalRecipe;
    std::string statusText;
    std::string terminalReason;
};

struct ApplyContext {
    SolveIdentity expected;
    std::string activeSourceKey;
    std::string selectedSourceKey;
    std::uint64_t activeSourceHash = 0;
    bool projectActive = false;
    bool recipeBacked = false;
    RawRecipe::RawDevelopmentRecipe currentRecipe;
};

struct ApplyDecision {
    bool allowed = false;
    bool changesRecipe = false;
    RawRecipe::RawDevelopmentRecipe recipe;
    std::string reason;
};

struct ProjectionSummary {
    std::vector<std::string> changedGroups;
    std::vector<std::string> unchangedGroups;
    std::string changedValues;
    std::string controlStatus;
};

const char* ProductModeName(ProductMode mode);
const char* LifecycleStateName(LifecycleState state);
bool IsRunning(LifecycleState state);
bool IsTerminal(LifecycleState state);
bool IsSuccessfulSolveStatus(RawOptimizer::SolveStatus status);

SolveIdentity BeginSolve(
    IntegrationState& state,
    const std::string& sourceKey,
    std::uint64_t sourceHash,
    const std::string& sourceIdentity,
    const RawRecipe::RawDevelopmentRecipe& currentRecipe);
void MarkRunning(IntegrationState& state, std::uint64_t generation);
bool Cancel(IntegrationState& state, std::string reason);
void MarkTerminal(
    IntegrationState& state,
    LifecycleState terminalState,
    std::string reason);

ApplyDecision ValidateForAtomicApply(
    const VerifiedCandidate& candidate,
    const ApplyContext& context);
bool VisibleProjectionMatches(
    const RawRecipe::RawDevelopmentRecipe& candidate,
    const RawRecipe::RawDevelopmentRecipe& applied);
ProjectionSummary SummarizeProjection(
    const RawRecipe::RawDevelopmentRecipe& before,
    const RawRecipe::RawDevelopmentRecipe& after);

} // namespace Stack::PreciseIntegration
