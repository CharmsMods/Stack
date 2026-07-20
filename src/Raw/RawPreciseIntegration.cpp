#include "Raw/RawPreciseIntegration.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace Stack::PreciseIntegration {
namespace {

bool SameSourceReference(
    const RawRecipe::RawSourceReference& a,
    const RawRecipe::RawSourceReference& b) {
    return a.sourcePath == b.sourcePath &&
        a.relativePathKey == b.relativePathKey &&
        a.fingerprint == b.fingerprint &&
        a.fileSizeBytes == b.fileSizeBytes &&
        a.modifiedTimeTicks == b.modifiedTimeTicks;
}

bool SameParameter(double a, double b) {
    return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= 1.0e-6;
}

bool SameParameters(
    const PreciseRaw::CandidateParameterVector& a,
    const PreciseRaw::CandidateParameterVector& b) {
    for (int i = 0; i < static_cast<int>(PreciseRaw::ParameterId::Count); ++i) {
        const auto id = static_cast<PreciseRaw::ParameterId>(i);
        if (!SameParameter(PreciseRaw::GetParameter(a, id), PreciseRaw::GetParameter(b, id))) {
            return false;
        }
    }
    return true;
}

bool CandidateRoundTrips(const PreciseRaw::CandidateProposal& candidate) {
    if (!candidate.valid) return false;
    const RawRecipe::RawDevelopmentRecipe decoded =
        RawRecipe::DeserializeRecipe(RawRecipe::SerializeRecipe(candidate.recipe));
    return PreciseRaw::CanonicalRecipeBytes(candidate.recipe) ==
            PreciseRaw::CanonicalRecipeBytes(decoded) &&
        SameParameters(candidate.parameters, PreciseRaw::ExtractParameters(candidate.recipe)) &&
        SameParameters(candidate.parameters, PreciseRaw::ExtractParameters(decoded));
}

bool SameGroup(
    const PreciseRaw::CandidateParameterVector& a,
    const PreciseRaw::CandidateParameterVector& b,
    PreciseDryRun::StageGroup group) {
    switch (group) {
        case PreciseDryRun::StageGroup::RawExposure:
            return SameParameter(a.rawExposureEv, b.rawExposureEv);
        case PreciseDryRun::StageGroup::LocalRange:
            return SameParameter(a.localTargetEv, b.localTargetEv) &&
                SameParameter(a.localDeltaEv, b.localDeltaEv) &&
                SameParameter(a.localWidthEv, b.localWidthEv) &&
                SameParameter(a.localFeather, b.localFeather);
        case PreciseDryRun::StageGroup::FinishTone:
            return SameParameter(a.finishY1, b.finishY1) &&
                SameParameter(a.finishY2, b.finishY2) &&
                SameParameter(a.finishY3, b.finishY3);
        case PreciseDryRun::StageGroup::DisplayFit:
            return SameParameter(a.displayBlackEv, b.displayBlackEv) &&
                SameParameter(a.displayWhiteEv, b.displayWhiteEv) &&
                SameParameter(a.displayMiddleGrey, b.displayMiddleGrey) &&
                SameParameter(a.displayShoulder, b.displayShoulder) &&
                SameParameter(a.displayToe, b.displayToe);
        default:
            return false;
    }
}

std::string FormatSigned(double value) {
    std::ostringstream stream;
    stream << std::showpos << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

std::string Join(const std::vector<std::string>& parts) {
    std::string result;
    for (const std::string& part : parts) {
        if (part.empty()) continue;
        if (!result.empty()) result += ", ";
        result += part;
    }
    return result.empty() ? std::string("None") : result;
}

} // namespace

const char* ProductModeName(ProductMode mode) {
    switch (mode) {
        case ProductMode::Precise: return "Precise";
        case ProductMode::Fast: return "Fast";
        default: return "Precise";
    }
}

const char* LifecycleStateName(LifecycleState state) {
    switch (state) {
        case LifecycleState::Idle: return "idle";
        case LifecycleState::Queued: return "queued";
        case LifecycleState::Running: return "running";
        case LifecycleState::Applying: return "applying-visible-recipe";
        case LifecycleState::Applied: return "applied";
        case LifecycleState::AppliedNoChange: return "verified-no-change";
        case LifecycleState::Blocked: return "blocked";
        case LifecycleState::Canceled: return "canceled";
        case LifecycleState::Failed: return "failed-safely";
        default: return "unknown";
    }
}

bool IsRunning(LifecycleState state) {
    return state == LifecycleState::Queued ||
        state == LifecycleState::Running ||
        state == LifecycleState::Applying;
}

bool IsTerminal(LifecycleState state) {
    return state == LifecycleState::Applied ||
        state == LifecycleState::AppliedNoChange ||
        state == LifecycleState::Blocked ||
        state == LifecycleState::Canceled ||
        state == LifecycleState::Failed;
}

bool IsSuccessfulSolveStatus(RawOptimizer::SolveStatus status) {
    return status == RawOptimizer::SolveStatus::Converged ||
        status == RawOptimizer::SolveStatus::SafeImprovementBudgetExhausted ||
        status == RawOptimizer::SolveStatus::WarmStartRetained;
}

SolveIdentity BeginSolve(
    IntegrationState& state,
    const std::string& sourceKey,
    std::uint64_t sourceHash,
    const std::string& sourceIdentity,
    const RawRecipe::RawDevelopmentRecipe& currentRecipe) {
    const ProductMode mode = state.mode;
    const std::uint64_t requestId = std::max<std::uint64_t>(1, state.nextRequestId);
    state = IntegrationState();
    state.mode = mode;
    state.nextRequestId = requestId + 1;
    state.state = LifecycleState::Queued;
    state.active = true;
    state.identity.requestId = requestId;
    state.identity.sourceKey = sourceKey;
    state.identity.sourceHash = sourceHash;
    state.identity.sourceIdentity = sourceIdentity;
    state.identity.inputRecipeIdentity = PreciseRaw::RecipeIdentity(currentRecipe);
    state.originalRecipe = currentRecipe;
    state.statusText = "Analyzing raw evidence...";
    return state.identity;
}

void MarkRunning(IntegrationState& state, std::uint64_t generation) {
    if (!state.active || !IsRunning(state.state)) return;
    state.state = LifecycleState::Running;
    state.identity.generation = generation;
    state.statusText = "Evaluating rendered candidates...";
}

bool Cancel(IntegrationState& state, std::string reason) {
    if (!state.active || !IsRunning(state.state)) return false;
    if (reason.empty()) reason = "Canceled by the user.";
    state.active = false;
    state.state = LifecycleState::Canceled;
    state.statusText = "Precise Starting Point canceled.";
    state.terminalReason = std::move(reason);
    return true;
}

void MarkTerminal(
    IntegrationState& state,
    LifecycleState terminalState,
    std::string reason) {
    state.active = false;
    state.state = terminalState;
    state.terminalReason = std::move(reason);
    state.statusText = state.terminalReason.empty()
        ? LifecycleStateName(terminalState)
        : state.terminalReason;
}

ApplyDecision ValidateForAtomicApply(
    const VerifiedCandidate& candidate,
    const ApplyContext& context) {
    ApplyDecision decision;
    const auto reject = [&](std::string reason) {
        decision.reason = std::move(reason);
        return decision;
    };
    if (!context.projectActive || !context.recipeBacked) {
        return reject("Precise apply requires an active recipe-backed RAW project.");
    }
    if (context.expected.requestId == 0 ||
        candidate.identity.requestId != context.expected.requestId) {
        return reject("Precise result request identity is stale.");
    }
    if (context.activeSourceKey.empty() ||
        context.activeSourceKey != context.selectedSourceKey ||
        context.activeSourceKey != context.expected.sourceKey ||
        candidate.identity.sourceKey != context.expected.sourceKey) {
        return reject("RAW source changed before precise apply.");
    }
    if (context.activeSourceHash == 0 ||
        context.activeSourceHash != context.expected.sourceHash ||
        candidate.identity.sourceHash != context.expected.sourceHash) {
        return reject("RAW source identity changed before precise apply.");
    }
    if (!context.expected.sourceIdentity.empty() &&
        candidate.identity.sourceIdentity != context.expected.sourceIdentity) {
        return reject("RAW content identity changed before precise apply.");
    }
    const std::string currentIdentity = PreciseRaw::RecipeIdentity(context.currentRecipe);
    if (currentIdentity != context.expected.inputRecipeIdentity ||
        candidate.identity.inputRecipeIdentity != context.expected.inputRecipeIdentity) {
        return reject("RAW recipe changed before precise apply.");
    }
    if (!candidate.valid || candidate.integrationVersion != kIntegrationVersion ||
        candidate.solverVersion != PreciseDryRun::kDryRunSolverVersion) {
        return reject("Precise result version or validity check failed.");
    }
    if (!IsSuccessfulSolveStatus(candidate.solveStatus)) {
        return reject(candidate.reason.empty()
            ? "Precise solve did not reach an applyable terminal state."
            : candidate.reason);
    }
    if (candidate.fullVerification != RawOptimizer::FullVerificationDisposition::Accepted ||
        candidate.fullEvaluation.status != PreciseRaw::EvaluationStatus::Complete ||
        !candidate.candidateEligibleForApply ||
        !candidate.allFiveFullResolutionStages ||
        !candidate.trueFullResolution) {
        return reject("Full-resolution verification did not authorize apply.");
    }
    if (candidate.recipeAppliedDuringSolve || candidate.appliedPreviewTextureCreated) {
        return reject("Precise search mutated state before final apply.");
    }
    if (!candidate.selected.valid ||
        candidate.selected.identities.sourceIdentity != candidate.identity.sourceIdentity ||
        candidate.selected.candidateRecipeIdentity !=
            PreciseRaw::RecipeIdentity(candidate.selected.recipe)) {
        return reject("Selected candidate identity does not match its visible recipe.");
    }
    if (!candidate.candidateRecipeRoundTripExact ||
        !candidate.candidateVisibleParametersRoundTripExact ||
        !CandidateRoundTrips(candidate.selected) ||
        !SameSourceReference(context.currentRecipe.source, candidate.selected.recipe.source)) {
        return reject("Selected candidate cannot round-trip exactly through the visible recipe.");
    }
    decision.allowed = true;
    decision.recipe = candidate.selected.recipe;
    decision.changesRecipe = !VisibleProjectionMatches(context.currentRecipe, decision.recipe);
    decision.reason = decision.changesRecipe
        ? "Full-resolution-verified visible recipe is ready for one atomic apply."
        : "Full-resolution verification retained the current visible recipe.";
    return decision;
}

bool VisibleProjectionMatches(
    const RawRecipe::RawDevelopmentRecipe& candidate,
    const RawRecipe::RawDevelopmentRecipe& applied) {
    return PreciseRaw::CanonicalRecipeBytes(candidate) ==
        PreciseRaw::CanonicalRecipeBytes(applied) &&
        SameParameters(
            PreciseRaw::ExtractParameters(candidate),
            PreciseRaw::ExtractParameters(applied));
}

ProjectionSummary SummarizeProjection(
    const RawRecipe::RawDevelopmentRecipe& before,
    const RawRecipe::RawDevelopmentRecipe& after) {
    ProjectionSummary result;
    const PreciseRaw::CandidateParameterVector a = PreciseRaw::ExtractParameters(before);
    const PreciseRaw::CandidateParameterVector b = PreciseRaw::ExtractParameters(after);
    struct Group {
        PreciseDryRun::StageGroup id;
        const char* label;
    };
    for (const Group group : {
             Group { PreciseDryRun::StageGroup::RawExposure, "RAW Exposure" },
             Group { PreciseDryRun::StageGroup::LocalRange, "Local Range" },
             Group { PreciseDryRun::StageGroup::FinishTone, "Finish Tone" },
             Group { PreciseDryRun::StageGroup::DisplayFit, "Display Fit" } }) {
        if (SameGroup(a, b, group.id)) result.unchangedGroups.push_back(group.label);
        else result.changedGroups.push_back(group.label);
    }
    std::vector<std::string> values;
    if (!SameGroup(a, b, PreciseDryRun::StageGroup::RawExposure)) {
        values.push_back("RAW Exposure " + FormatSigned(b.rawExposureEv) + " EV");
    }
    if (!SameGroup(a, b, PreciseDryRun::StageGroup::LocalRange)) {
        values.push_back(
            "Local Range target " + FormatSigned(b.localTargetEv) +
            " EV, delta " + FormatSigned(b.localDeltaEv) + " EV");
    }
    if (!SameGroup(a, b, PreciseDryRun::StageGroup::FinishTone)) {
        values.push_back(
            "Finish Tone [" + FormatSigned(b.finishY1) + ", " +
            FormatSigned(b.finishY2) + ", " + FormatSigned(b.finishY3) + "]");
    }
    if (!SameGroup(a, b, PreciseDryRun::StageGroup::DisplayFit)) {
        values.push_back(
            "Display Fit black " + FormatSigned(b.displayBlackEv) +
            " EV, white " + FormatSigned(b.displayWhiteEv) + " EV");
    }
    result.changedValues = Join(values);
    std::vector<std::string> statuses;
    for (const std::string& changed : result.changedGroups) {
        statuses.push_back(changed + ": changed by Precise solve");
    }
    for (const std::string& unchanged : result.unchangedGroups) {
        statuses.push_back(unchanged + ": verified unchanged");
    }
    result.controlStatus = Join(statuses);
    return result;
}

} // namespace Stack::PreciseIntegration
