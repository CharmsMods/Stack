#include "Raw/RawPreciseNativeRuntime.h"

#include "Renderer/RawCandidateEvaluation.h"
#include "Raw/LibRawDecoder.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Raw/RenderedFeatureEvidence.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>
#include <vector>

namespace Stack::PreciseIntegration {
namespace {

using Renderer::RawCandidateEvaluation::RenderedEvaluation;
using Renderer::RawCandidateEvaluation::WarmStartResult;

RawOptimizer::SearchBudget SelectedBudget() {
    RawOptimizer::SearchBudget budget;
    budget.maxUniqueEvaluations = 48;
    budget.maxAcceptedIterations = 16;
    budget.stableIterationsRequired = 2;
    budget.maxRuntimeMs = 60000.0;
    budget.initialNormalizedRadius = 0.25;
    budget.minimumNormalizedRadius = 0.03125;
    budget.maximumNormalizedRadius = 0.50;
    budget.shrinkFactor = 0.50;
    budget.expandFactor = 1.50;
    budget.maximumBoundaryOscillations = 4;
    budget.requireFullResolutionVerification = true;
    return budget;
}

bool Canceled(const NativeSolveCallbacks& callbacks) {
    return callbacks.shouldCancel && callbacks.shouldCancel();
}

void Report(
    const NativeSolveCallbacks& callbacks,
    const std::string& label,
    int completed,
    int total = 52) {
    if (callbacks.reportProgress) callbacks.reportProgress(label, completed, total);
}

bool HasFiveStages(const PreciseRaw::CandidateEvaluationRecord& evaluation) {
    for (const RawAutoStartPoint::RawAutoStartPointStage stage : {
             RawAutoStartPoint::RawAutoStartPointStage::NeutralScene,
             RawAutoStartPoint::RawAutoStartPointStage::RawPlacement,
             RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate,
             RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate,
             RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate }) {
        const auto found = std::find_if(
            evaluation.render.stages.begin(),
            evaluation.render.stages.end(),
            [stage](const PreciseRaw::StageFeatureEvidence& item) {
                return item.stage == stage && item.features.valid;
            });
        if (found == evaluation.render.stages.end()) return false;
    }
    return true;
}

struct EvaluationSession {
    RenderPipeline proxyPipeline;
    RenderPipeline fullPipeline;
    RenderGraphSnapshot graph;
    const PreciseRaw::ParameterSpace* parameterSpace = nullptr;
    const RawEvidence::RawTechnicalEvidenceRecord* rawEvidence = nullptr;
    const Raw::RawMetadata* metadata = nullptr;
    const RawRecipe::RawDevelopmentRecipe* solverBaseRecipe = nullptr;
    const NativeSolveCallbacks* callbacks = nullptr;
    std::string proxyIdentity;
    std::string fullIdentity;
    int featureMaxDimension = 256;
    int proxyRenderCount = 0;
    std::vector<RawAutoStartPoint::RawAutoStartPointStageImage> warmImages;
    bool fullReadbackComplete = false;

    PreciseRaw::CandidateEvaluationRecord EvaluateProxy(
        const PreciseRaw::CandidateProposal& proposal) {
        ++proxyRenderCount;
        Report(
            *callbacks,
            "Evaluating candidates " + std::to_string(proxyRenderCount) + " / 48...",
            std::min(48, proxyRenderCount));
        RenderedEvaluation rendered =
            Renderer::RawCandidateEvaluation::RenderAndEvaluate(
                proxyPipeline,
                graph,
                proposal,
                *parameterSpace,
                *rawEvidence,
                *metadata,
                warmImages.empty() ? nullptr : &warmImages,
                proxyIdentity,
                false,
                featureMaxDimension,
                *solverBaseRecipe,
                "Phase 06 native precise proxy candidate; isolated, no apply.");
        const bool complete =
            Renderer::RawCandidateEvaluation::HasRequiredStages(rendered);
        if (warmImages.empty() && complete) warmImages = rendered.images;
        return std::move(rendered.evaluation);
    }

    PreciseRaw::CandidateEvaluationRecord EvaluateFull(
        const PreciseRaw::CandidateProposal& proposal) {
        Report(*callbacks, "Verifying full resolution...", 49);
        RenderedEvaluation rendered =
            Renderer::RawCandidateEvaluation::RenderAndEvaluate(
                fullPipeline,
                graph,
                proposal,
                *parameterSpace,
                *rawEvidence,
                *metadata,
                warmImages.empty() ? nullptr : &warmImages,
                fullIdentity,
                true,
                std::max(512, featureMaxDimension),
                *solverBaseRecipe,
                "Phase 06 independent full-resolution verification; isolated, no apply.");
        fullReadbackComplete =
            Renderer::RawCandidateEvaluation::HasRequiredStages(rendered);
        return std::move(rendered.evaluation);
    }
};

} // namespace

NativeSolveResult RunNativePreciseSolve(
    const NativeSolveRequest& request,
    const NativeSolveCallbacks& callbacks) {
    NativeSolveResult output;
    output.attempted = true;
    output.candidate.identity = request.identity;
    output.candidate.integrationVersion = kIntegrationVersion;
    output.candidate.solverVersion = PreciseDryRun::kDryRunSolverVersion;
    const auto fail = [&](std::string reason) {
        output.failed = !Canceled(callbacks);
        output.canceled = !output.failed;
        output.reason = std::move(reason);
        output.candidate.reason = output.reason;
        return output;
    };

    if (request.identity.requestId == 0 || request.identity.sourceKey.empty() ||
        request.identity.sourceHash == 0 || request.inputRecipe.source.sourcePath.empty()) {
        return fail("Precise solve request is missing native source or identity state.");
    }
    if (PreciseRaw::RecipeIdentity(request.inputRecipe) !=
        request.identity.inputRecipeIdentity) {
        return fail("Precise solve input recipe identity changed before start.");
    }
    if (Canceled(callbacks)) return fail("Precise solve canceled before raw analysis.");

    Report(callbacks, "Analyzing raw evidence...", 0);
    const std::filesystem::path sourcePath = request.inputRecipe.source.sourcePath;
    const RawEvidence::SourceIdentity sourceIdentity =
        RawEvidence::ComputeSourceIdentity(sourcePath);
    Raw::RawImageData raw;
    if (!sourceIdentity.valid ||
        !Raw::DecodeWithLibRaw(sourcePath.string(), raw, {})) {
        return fail(raw.metadata.error.empty()
            ? "Precise solve could not decode the RAW source."
            : raw.metadata.error);
    }
    if (!request.identity.sourceIdentity.empty() &&
        request.identity.sourceIdentity != sourceIdentity.sha256) {
        return fail("RAW content identity changed before precise analysis.");
    }
    output.candidate.identity.sourceIdentity = sourceIdentity.sha256;
    RawEvidence::BuildOptions evidenceOptions;
    evidenceOptions.maxSamples = 1000000;
    const RawEvidence::RawTechnicalEvidenceRecord rawEvidence =
        RawEvidence::BuildRawTechnicalEvidence(raw, sourceIdentity, evidenceOptions);
    const Raw::RawMetadata metadata = raw.metadata;
    raw = {};
    if (!rawEvidence.valid) {
        return fail(rawEvidence.statusMessage.empty()
            ? "Precise raw evidence is unavailable."
            : rawEvidence.statusMessage);
    }
    if (Canceled(callbacks)) return fail("Precise solve canceled after raw analysis.");

    Report(callbacks, "Measuring scene and building warm start...", 1);
    const WarmStartResult warm =
        Renderer::RawCandidateEvaluation::BuildWarmStart(
            request.inputRecipe,
            metadata,
            sourceIdentity.sha256,
            request.warmMaxDimension,
            callbacks.shouldCancel);
    if (!warm.valid) {
        return fail(warm.reason.empty()
            ? "Precise warm-start construction failed."
            : warm.reason);
    }
    if (Canceled(callbacks)) return fail("Precise solve canceled after scene measurement.");

    const RawRecipe::RawDevelopmentRecipe solverBaseRecipe = warm.recipe;
    const PreciseRaw::ParameterSpace parameterSpace =
        PreciseRaw::BuildParameterSpace(solverBaseRecipe, &rawEvidence);
    const std::string proxyIdentity = "stack-preview-max-" +
        std::to_string(request.proxyMaxDimension) + "-feature-max-" +
        std::to_string(request.featureMaxDimension) + "-phase-06-v1";
    const std::string fullIdentity = "stack-full-resolution-feature-max-" +
        std::to_string(std::max(512, request.featureMaxDimension)) + "-phase-06-v1";

    PreciseRaw::CandidateIdentityContext identity;
    identity.sourceIdentity = sourceIdentity.sha256;
    identity.decodeIdentity = rawEvidence.decodeIdentity.sha256;
    identity.rawEvidenceIdentity = rawEvidence.evidenceIdentitySha256;
    identity.baseRecipeIdentity = PreciseRaw::RecipeIdentity(solverBaseRecipe);
    identity.rendererIdentity = "stack-opengl-raw-development-stages-v1";
    identity.proxyIdentity = proxyIdentity;
    identity.featureVersion = RenderedFeatures::kRenderedFeatureVersion;
    identity.budgetIdentity =
        "raw-precise-dry-run-v1|raw-precise-budget-v1|stage-quotas-v1";
    identity.ownershipIdentity = "explicit-precise-rebuild-visible-groups-v1";
    identity.generation = request.identity.generation;
    PreciseRaw::CandidateIdentityContext fullContext = identity;
    fullContext.proxyIdentity = fullIdentity;

    PreciseDryRun::DryRunRequest dryRequest;
    dryRequest.solveId = "phase-06/native/" +
        std::to_string(request.identity.requestId) + "/" +
        sourceIdentity.sha256.substr(0, 16);
    dryRequest.solverBaseRecipe = solverBaseRecipe;
    dryRequest.parameterSpace = parameterSpace;
    dryRequest.proxyIdentity = identity;
    dryRequest.fullResolutionIdentity = fullContext;
    dryRequest.totalBudget = SelectedBudget();
    dryRequest.stagePolicies = PreciseDryRun::DefaultStagePolicies();

    EvaluationSession session;
    session.graph = Renderer::RawCandidateEvaluation::BuildGraph(solverBaseRecipe);
    session.parameterSpace = &parameterSpace;
    session.rawEvidence = &rawEvidence;
    session.metadata = &metadata;
    session.solverBaseRecipe = &solverBaseRecipe;
    session.callbacks = &callbacks;
    session.proxyIdentity = proxyIdentity;
    session.fullIdentity = fullIdentity;
    session.featureMaxDimension = request.featureMaxDimension;
    session.proxyPipeline.Initialize();
    session.proxyPipeline.SetPreviewMaxDimension(request.proxyMaxDimension);
    session.proxyPipeline.Resize(request.proxyMaxDimension, request.proxyMaxDimension);
    session.fullPipeline.Initialize();
    session.fullPipeline.SetPreviewMaxDimension(0);
    const int expectedWidth = metadata.visibleWidth > 0
        ? metadata.visibleWidth : metadata.rawWidth;
    const int expectedHeight = metadata.visibleHeight > 0
        ? metadata.visibleHeight : metadata.rawHeight;
    session.fullPipeline.Resize(expectedWidth, expectedHeight);

    PreciseDryRun::DryRunCallbacks dryCallbacks;
    dryCallbacks.shouldCancel = callbacks.shouldCancel;
    dryCallbacks.evaluateProxy = [&](const PreciseRaw::CandidateProposal& proposal) {
        return session.EvaluateProxy(proposal);
    };
    dryCallbacks.evaluateFullResolution =
        [&](const PreciseRaw::CandidateProposal& proposal) {
            return session.EvaluateFull(proposal);
        };
    const PreciseDryRun::DryRunResult dryRun =
        PreciseDryRun::RunPreciseDryRun(dryRequest, dryCallbacks);

    VerifiedCandidate& candidate = output.candidate;
    candidate.solveStatus = dryRun.status;
    candidate.fullVerification = dryRun.fullVerification;
    candidate.selected = dryRun.selected;
    candidate.fullEvaluation = dryRun.fullResolutionEvaluation;
    candidate.candidateEligibleForApply = dryRun.candidateEligibleForFutureApply;
    candidate.candidateRecipeRoundTripExact = dryRun.candidateRecipeRoundTripExact;
    candidate.candidateVisibleParametersRoundTripExact =
        dryRun.candidateVisibleParametersRoundTripExact;
    candidate.recipeAppliedDuringSolve = dryRun.recipeApplied;
    candidate.appliedPreviewTextureCreated = dryRun.appliedPreviewTextureCreated;
    candidate.proxyRenderCount = dryRun.actualProxyRenderCount;
    candidate.proxyCacheHits = dryRun.proxyCacheHits;
    candidate.fullWidth = dryRun.fullResolutionEvaluation.render.renderWidth;
    candidate.fullHeight = dryRun.fullResolutionEvaluation.render.renderHeight;
    candidate.allFiveFullResolutionStages =
        session.fullReadbackComplete && HasFiveStages(dryRun.fullResolutionEvaluation);
    const double expectedPixels = static_cast<double>(expectedWidth) *
        static_cast<double>(expectedHeight);
    const double actualPixels = static_cast<double>(candidate.fullWidth) *
        static_cast<double>(candidate.fullHeight);
    candidate.trueFullResolution =
        candidate.fullWidth > request.proxyMaxDimension &&
        candidate.fullHeight > request.proxyMaxDimension &&
        expectedPixels > 0.0 && actualPixels / expectedPixels >= 0.95;
    candidate.reason = dryRun.reason;
    candidate.warnings = dryRun.warnings;
    candidate.valid =
        IsSuccessfulSolveStatus(candidate.solveStatus) &&
        candidate.fullVerification ==
            RawOptimizer::FullVerificationDisposition::Accepted &&
        candidate.fullEvaluation.status == PreciseRaw::EvaluationStatus::Complete &&
        candidate.candidateEligibleForApply &&
        candidate.candidateRecipeRoundTripExact &&
        candidate.candidateVisibleParametersRoundTripExact &&
        candidate.allFiveFullResolutionStages &&
        candidate.trueFullResolution &&
        !candidate.recipeAppliedDuringSolve &&
        !candidate.appliedPreviewTextureCreated;

    if (Canceled(callbacks) || dryRun.status == RawOptimizer::SolveStatus::Canceled) {
        output.canceled = true;
        output.reason = "Precise solve canceled; current recipe was not changed.";
        candidate.valid = false;
        candidate.reason = output.reason;
        return output;
    }
    if (!candidate.valid) {
        output.failed = true;
        output.reason = candidate.reason.empty()
            ? "Precise solve failed its native full-resolution apply gates."
            : candidate.reason;
        return output;
    }
    Report(callbacks, "Ready to apply visible recipe...", 52);
    output.reason = candidate.reason;
    return output;
}

} // namespace Stack::PreciseIntegration
