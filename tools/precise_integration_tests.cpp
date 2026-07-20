#include "Raw/RawPreciseIntegration.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using namespace Stack;

void Require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "Phase 06 precise integration test failed: " << message << "\n";
    std::exit(1);
}

RawRecipe::RawDevelopmentRecipe MakeRecipe() {
    RawRecipe::RawDevelopmentRecipe recipe =
        RawRecipe::MakeDefaultRecipe("fixture.dng", "fixture.dng");
    recipe.source.relativePathKey = "fixture.dng";
    recipe.source.fingerprint = "source-sha";
    recipe.source.fileSizeBytes = 123456;
    recipe.source.modifiedTimeTicks = 42;
    recipe.cropRotation.cropEnabled = true;
    recipe.cropRotation.cropX = 0.10f;
    recipe.cropRotation.cropWidth = 0.80f;
    recipe.whiteBalance.mode = RawRecipe::WhiteBalanceMode::CustomMultipliers;
    recipe.whiteBalance.hasMultipliers = true;
    recipe.whiteBalance.multipliers = { 1.25f, 1.0f, 1.10f };
    return recipe;
}

PreciseIntegration::VerifiedCandidate MakeVerified(
    const PreciseIntegration::SolveIdentity& solve,
    const RawRecipe::RawDevelopmentRecipe& base) {
    PreciseRaw::CandidateParameterVector parameters =
        PreciseRaw::ExtractParameters(base);
    parameters.rawExposureEv = 0.75;
    parameters.localTargetEv = -3.0;
    parameters.localDeltaEv = 0.5;
    parameters.localWidthEv = 1.0;
    parameters.localFeather = 0.45;
    parameters.finishY1 = 0.30;
    parameters.finishY2 = 0.54;
    parameters.finishY3 = 0.79;
    parameters.displayBlackEv = -5.0;
    parameters.displayWhiteEv = 4.5;
    parameters.displayMiddleGrey = 0.18;
    parameters.displayShoulder = 0.40;
    parameters.displayToe = 0.25;

    PreciseRaw::CandidateIdentityContext identity;
    identity.sourceIdentity = solve.sourceIdentity;
    identity.decodeIdentity = "decode";
    identity.rawEvidenceIdentity = "raw-evidence";
    identity.baseRecipeIdentity = PreciseRaw::RecipeIdentity(base);
    identity.rendererIdentity = "renderer";
    identity.proxyIdentity = "full";
    identity.featureVersion = RenderedFeatures::kRenderedFeatureVersion;
    identity.budgetIdentity = "budget";
    identity.ownershipIdentity = "explicit-precise-rebuild-visible-groups-v1";
    identity.generation = solve.generation;

    PreciseIntegration::VerifiedCandidate result;
    result.valid = true;
    result.identity = solve;
    result.integrationVersion = PreciseIntegration::kIntegrationVersion;
    result.solverVersion = PreciseDryRun::kDryRunSolverVersion;
    result.solveStatus = RawOptimizer::SolveStatus::SafeImprovementBudgetExhausted;
    result.fullVerification = RawOptimizer::FullVerificationDisposition::Accepted;
    result.selected = PreciseRaw::BuildCandidateProposal(
        base,
        parameters,
        identity,
        {},
        "Phase 06 fixture candidate");
    result.fullEvaluation.status = PreciseRaw::EvaluationStatus::Complete;
    result.candidateEligibleForApply = true;
    result.candidateRecipeRoundTripExact = true;
    result.candidateVisibleParametersRoundTripExact = true;
    result.allFiveFullResolutionStages = true;
    result.trueFullResolution = true;
    result.fullWidth = 6048;
    result.fullHeight = 4024;
    return result;
}

PreciseIntegration::ApplyContext MakeContext(
    const PreciseIntegration::SolveIdentity& identity,
    const RawRecipe::RawDevelopmentRecipe& recipe) {
    PreciseIntegration::ApplyContext context;
    context.expected = identity;
    context.activeSourceKey = identity.sourceKey;
    context.selectedSourceKey = identity.sourceKey;
    context.activeSourceHash = identity.sourceHash;
    context.projectActive = true;
    context.recipeBacked = true;
    context.currentRecipe = recipe;
    return context;
}

} // namespace

int main() {
    using namespace Stack;

    RawRecipe::RawDevelopmentRecipe base = MakeRecipe();
    PreciseIntegration::IntegrationState state;
    const PreciseIntegration::SolveIdentity identity =
        PreciseIntegration::BeginSolve(
            state,
            "fixture.dng",
            7711,
            "source-sha",
            base);
    Require(state.active && state.state == PreciseIntegration::LifecycleState::Queued,
        "explicit start should queue one native solve");
    Require(PreciseRaw::RecipeIdentity(state.originalRecipe) == PreciseRaw::RecipeIdentity(base),
        "begin should retain the exact pre-action recipe");
    PreciseIntegration::MarkRunning(state, 9);
    Require(state.state == PreciseIntegration::LifecycleState::Running &&
            state.identity.generation == 9,
        "submission should bind the native generation");

    PreciseIntegration::VerifiedCandidate verified = MakeVerified(state.identity, base);
    PreciseIntegration::ApplyContext context = MakeContext(state.identity, base);
    PreciseIntegration::ApplyDecision decision =
        PreciseIntegration::ValidateForAtomicApply(verified, context);
    if (!decision.allowed) std::cerr << "Apply rejection: " << decision.reason << "\n";
    Require(decision.allowed && decision.changesRecipe,
        "full-resolution verified candidate should authorize one final apply");
    Require(decision.recipe.cropRotation.cropX == base.cropRotation.cropX &&
            decision.recipe.whiteBalance.multipliers == base.whiteBalance.multipliers,
        "precise projection should preserve visible controls outside the 13-field solver space");

    const PreciseIntegration::ProjectionSummary summary =
        PreciseIntegration::SummarizeProjection(base, decision.recipe);
    Require(summary.changedGroups.size() == 4,
        "fixture should expose all four visible solver groups as changed");
    Require(summary.controlStatus.find("RAW Exposure: changed") != std::string::npos &&
            summary.controlStatus.find("Display Fit: changed") != std::string::npos,
        "projection summary should explain visible ownership");

    RawRecipe::RawDevelopmentRecipe live = base;
    const RawRecipe::RawDevelopmentRecipe oneUndoSnapshot = live;
    live = decision.recipe;
    Require(PreciseIntegration::VisibleProjectionMatches(decision.recipe, live),
        "applied sliders and graphs should exactly equal the selected candidate");
    const RawRecipe::RawDevelopmentRecipe reopened =
        RawRecipe::DeserializeRecipe(RawRecipe::SerializeRecipe(live));
    Require(PreciseIntegration::VisibleProjectionMatches(live, reopened),
        "save/load should recreate the applied recipe without rerunning the solver");
    live = oneUndoSnapshot;
    Require(PreciseIntegration::VisibleProjectionMatches(base, live),
        "one Undo snapshot should restore the exact original recipe");

    PreciseIntegration::ApplyContext sourceChanged = context;
    sourceChanged.activeSourceHash++;
    Require(!PreciseIntegration::ValidateForAtomicApply(verified, sourceChanged).allowed,
        "same-key source identity change should reject stale output");
    PreciseIntegration::ApplyContext recipeChanged = context;
    recipeChanged.currentRecipe.preToneExposureEv += 0.1f;
    Require(!PreciseIntegration::ValidateForAtomicApply(verified, recipeChanged).allowed,
        "manual recipe edit should reject stale output");
    PreciseIntegration::VerifiedCandidate fullRejected = verified;
    fullRejected.fullVerification = RawOptimizer::FullVerificationDisposition::Rejected;
    Require(!PreciseIntegration::ValidateForAtomicApply(fullRejected, context).allowed,
        "full-resolution rejection should never apply");
    PreciseIntegration::VerifiedCandidate prematureMutation = verified;
    prematureMutation.recipeAppliedDuringSolve = true;
    Require(!PreciseIntegration::ValidateForAtomicApply(prematureMutation, context).allowed,
        "search-time mutation should invalidate the handoff");
    PreciseIntegration::ApplyContext managed = context;
    managed.recipeBacked = false;
    Require(!PreciseIntegration::ValidateForAtomicApply(verified, managed).allowed,
        "managed/custom graph should be preserved by the first integration version");

    PreciseIntegration::IntegrationState cancelState;
    PreciseIntegration::BeginSolve(cancelState, "fixture.dng", 7711, "source-sha", base);
    Require(PreciseIntegration::Cancel(cancelState, "user canceled") &&
            cancelState.state == PreciseIntegration::LifecycleState::Canceled &&
            !cancelState.active,
        "cancel should terminate without an applyable active request");
    Require(!PreciseIntegration::Cancel(cancelState, "again"),
        "terminal cancellation should be idempotent");

    PreciseIntegration::VerifiedCandidate noChange = MakeVerified(identity, base);
    noChange.selected = PreciseRaw::BuildCandidateProposal(
        base,
        PreciseRaw::ExtractParameters(base),
        noChange.selected.identities,
        {},
        "no change");
    PreciseIntegration::ApplyDecision noChangeDecision =
        PreciseIntegration::ValidateForAtomicApply(noChange, MakeContext(identity, base));
    Require(noChangeDecision.allowed && !noChangeDecision.changesRecipe,
        "well-exposed verified no-op should not create a needless recipe write");

    std::cout << "All Phase 06 precise integration fixtures passed.\n";
    return 0;
}
