#include "Renderer/RawCandidateEvaluation.h"
#include "App/Validation/ValidationSuites.h"

#include "Raw/LibRawDecoder.h"
#include "Raw/RawPreciseDryRun.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Raw/RenderedFeatureEvidence.h"
#include "Renderer/GLLoader.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>

namespace Stack::Validation {
namespace {

using Renderer::RawCandidateEvaluation::RenderedEvaluation;
using Renderer::RawCandidateEvaluation::WarmStartResult;
using PreciseRaw::CandidateEvaluationRecord;
using PreciseRaw::CandidateIdentityContext;
using PreciseRaw::CandidateProposal;
using PreciseRaw::EvaluationStatus;
using PreciseDryRun::DryRunRequest;
using PreciseDryRun::DryRunResult;
using RawAutoStartPoint::RawAutoStartPointStageImage;

struct SourceInput {
    std::filesystem::path path;
    std::string partition;
};

struct Options {
    std::vector<SourceInput> sources;
    std::filesystem::path output;
    int proxyMaxDimension = 256;
    int featureMaxDimension = 256;
    int warmMaxDimension = 2048;
};

struct EvaluationSession {
    RenderPipeline proxyPipeline;
    RenderPipeline fullPipeline;
    RenderGraphSnapshot graph;
    const PreciseRaw::ParameterSpace* parameterSpace = nullptr;
    const RawEvidence::RawTechnicalEvidenceRecord* rawEvidence = nullptr;
    const Raw::RawMetadata* metadata = nullptr;
    const RawRecipe::RawDevelopmentRecipe* solverBaseRecipe = nullptr;
    std::string proxyIdentity;
    std::string fullIdentity;
    int featureMaxDimension = 256;
    std::vector<RawAutoStartPointStageImage> warmImages;
    bool everyProxyReadbackComplete = true;
    bool fullReadbackComplete = false;

    CandidateEvaluationRecord EvaluateProxy(const CandidateProposal& proposal) {
        RenderedEvaluation rendered = Renderer::RawCandidateEvaluation::RenderAndEvaluate(
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
            "Phase 05 precise dry-run proxy candidate; diagnostic only, no apply.");
        const bool complete = Renderer::RawCandidateEvaluation::HasRequiredStages(rendered);
        everyProxyReadbackComplete = everyProxyReadbackComplete && complete;
        if (warmImages.empty() && complete) warmImages = rendered.images;
        return std::move(rendered.evaluation);
    }

    CandidateEvaluationRecord EvaluateFull(const CandidateProposal& proposal) {
        RenderedEvaluation rendered = Renderer::RawCandidateEvaluation::RenderAndEvaluate(
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
            "Phase 05 independent full-resolution finalist verification; diagnostic only, no apply.");
        fullReadbackComplete = Renderer::RawCandidateEvaluation::HasRequiredStages(rendered);
        return std::move(rendered.evaluation);
    }
};

bool ParseInteger(const char* text, int minimum, int& output) {
    try {
        output = std::max(minimum, std::stoi(text ? text : ""));
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseOptions(int argc, char** argv, Options& options, std::string& error) {
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        auto requireValue = [&](const char* option) -> const char* {
            if (i + 1 >= argc) {
                error = std::string(option) + " requires a value.";
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--development-file" || arg == "--validation-file") {
            const char* value = requireValue(arg.c_str());
            if (!value) return false;
            options.sources.push_back({ value, arg == "--development-file" ? "development" : "validation" });
        } else if (arg == "--output") {
            const char* value = requireValue("--output");
            if (!value) return false;
            options.output = value;
        } else if (arg == "--proxy-max-dimension") {
            const char* value = requireValue("--proxy-max-dimension");
            if (!value || !ParseInteger(value, 64, options.proxyMaxDimension)) {
                error = "--proxy-max-dimension must be an integer >= 64.";
                return false;
            }
        } else if (arg == "--feature-max-dimension") {
            const char* value = requireValue("--feature-max-dimension");
            if (!value || !ParseInteger(value, 64, options.featureMaxDimension)) {
                error = "--feature-max-dimension must be an integer >= 64.";
                return false;
            }
        } else if (arg == "--warm-max-dimension") {
            const char* value = requireValue("--warm-max-dimension");
            if (!value || !ParseInteger(value, 128, options.warmMaxDimension)) {
                error = "--warm-max-dimension must be an integer >= 128.";
                return false;
            }
        } else {
            error = "Unknown precise dry-run validation option: " + arg;
            return false;
        }
    }
    if (options.sources.empty() || options.output.empty()) {
        error = "Provide development/validation RAW inputs and an output report.";
        return false;
    }
    for (const SourceInput& source : options.sources) {
        if (!std::filesystem::is_regular_file(source.path)) {
            error = "RAW input does not exist: " + source.path.filename().string();
            return false;
        }
    }
    return true;
}

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

nlohmann::json StableSignature(const DryRunResult& result) {
    nlohmann::json stages = nlohmann::json::array();
    for (const PreciseDryRun::StageSearchRecord& stage : result.stages) {
        stages.push_back({
            { "group", PreciseDryRun::StageGroupName(stage.group) },
            { "attempted", stage.attempted }, { "skipped", stage.skipped },
            { "status", stage.attempted
                ? nlohmann::json(RawOptimizer::SolveStatusName(stage.search.status))
                : nlohmann::json(nullptr) },
            { "selectedPoint", stage.attempted
                ? nlohmann::json(stage.search.selected.point)
                : nlohmann::json(nullptr) },
            { "proposedEvaluations", stage.search.proposedEvaluations },
            { "uniqueEvaluations", stage.search.uniqueEvaluations },
            { "acceptedIterations", stage.search.acceptedIterations }
        });
    }
    nlohmann::json sequence = nlohmann::json::array();
    for (const CandidateEvaluationRecord& evaluation : result.proxyEvaluations) {
        sequence.push_back(evaluation.proposal.candidateId);
    }
    return {
        { "status", RawOptimizer::SolveStatusName(result.status) },
        { "fullVerification", RawOptimizer::FullVerificationDispositionName(result.fullVerification) },
        { "selectedRecipeIdentity", result.selected.candidateRecipeIdentity },
        { "selectedParameters", PreciseRaw::SerializeParameters(
            PreciseRaw::ExtractParameters(result.selected.recipe)) },
        { "stageSearches", std::move(stages) },
        { "proxyCandidateSequence", std::move(sequence) },
        { "meaningfulImprovement", result.meaningfulImprovement },
        { "fallbackToWarmStart", result.fallbackToWarmStart }
    };
}

bool HasAllFiveFeatureStages(const CandidateEvaluationRecord& evaluation) {
    const std::array<RawAutoStartPoint::RawAutoStartPointStage, 5> required {
        RawAutoStartPoint::RawAutoStartPointStage::NeutralScene,
        RawAutoStartPoint::RawAutoStartPointStage::RawPlacement,
        RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate,
        RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate,
        RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate
    };
    return std::all_of(required.begin(), required.end(), [&](const auto stage) {
        return std::any_of(
            evaluation.render.stages.begin(), evaluation.render.stages.end(),
            [&](const PreciseRaw::StageFeatureEvidence& evidence) {
                return evidence.stage == stage && evidence.features.valid;
            });
    });
}

std::string AutomatedSceneClass(const RawEvidence::RawTechnicalEvidenceRecord& evidence) {
    if (evidence.clipping.allChannelClippedFraction.valid &&
        evidence.clipping.allChannelClippedFraction.value > 0.0) {
        return "all-channel-raw-clipping-present";
    }
    if ((evidence.clipping.singleChannelClippedFraction.valid &&
         evidence.clipping.singleChannelClippedFraction.value > 0.0) ||
        (evidence.clipping.multiChannelClippedFraction.valid &&
         evidence.clipping.multiChannelClippedFraction.value > 0.0)) {
        return "partial-channel-raw-clipping-present";
    }
    return "no-sampled-co-sited-raw-clipping";
}

nlohmann::json ReplayDryRun(
    const DryRunRequest& request,
    const DryRunResult& primary,
    bool& deterministic) {
    std::unordered_map<std::string, CandidateEvaluationRecord> proxyRecords;
    for (const CandidateEvaluationRecord& evaluation : primary.proxyEvaluations) {
        proxyRecords[evaluation.proposal.candidateId] = evaluation;
    }
    PreciseDryRun::DryRunCallbacks replayCallbacks;
    replayCallbacks.evaluateProxy = [&](const CandidateProposal& proposal) {
        const auto found = proxyRecords.find(proposal.candidateId);
        if (found != proxyRecords.end()) return found->second;
        CandidateEvaluationRecord missing;
        missing.proposal = proposal;
        missing.status = EvaluationStatus::Failed;
        missing.rejectionReason = "Deterministic replay requested a candidate absent from the primary run.";
        return missing;
    };
    replayCallbacks.evaluateFullResolution = [&](const CandidateProposal& proposal) {
        if (primary.fullResolutionEvaluation.proposal.candidateId == proposal.candidateId) {
            return primary.fullResolutionEvaluation;
        }
        CandidateEvaluationRecord missing;
        missing.proposal = proposal;
        missing.status = EvaluationStatus::Failed;
        missing.rejectionReason = "Deterministic replay requested a different full-resolution candidate.";
        return missing;
    };
    const DryRunResult replay = PreciseDryRun::RunPreciseDryRun(request, replayCallbacks);
    const nlohmann::json primarySignature = StableSignature(primary);
    const nlohmann::json replaySignature = StableSignature(replay);
    deterministic = primarySignature == replaySignature;
    return {
        { "deterministic", deterministic },
        { "primary", std::move(primarySignature) },
        { "replay", std::move(replaySignature) },
        { "reusedPrimaryCandidateEvidence", true },
        { "rerendered", false },
        { "reason", "Phase 03 separately proved fixed-policy stage-render repeatability; Phase 05 proves identical orchestration and selection from the same exact evidence records." }
    };
}

nlohmann::json RunSource(
    const SourceInput& input,
    const Options& options,
    bool& success) {
    success = false;
    const auto sourceStarted = std::chrono::steady_clock::now();
    const RawEvidence::SourceIdentity sourceIdentity =
        RawEvidence::ComputeSourceIdentity(input.path);
    Raw::RawImageData raw;
    if (!sourceIdentity.valid || !Raw::DecodeWithLibRaw(input.path.string(), raw, {})) {
        return {
            { "fileName", input.path.filename().string() },
            { "partition", input.partition }, { "status", "decode-failed" },
            { "reason", raw.metadata.error }
        };
    }
    RawEvidence::BuildOptions evidenceOptions;
    evidenceOptions.maxSamples = 1000000;
    const RawEvidence::RawTechnicalEvidenceRecord rawEvidence =
        RawEvidence::BuildRawTechnicalEvidence(raw, sourceIdentity, evidenceOptions);
    const Raw::RawMetadata metadata = raw.metadata;
    raw = {};
    if (!rawEvidence.valid) {
        return {
            { "fileName", input.path.filename().string() },
            { "partition", input.partition }, { "status", "raw-evidence-failed" },
            { "reason", rawEvidence.statusMessage }
        };
    }

    RawRecipe::RawDevelopmentRecipe inputProjectRecipe =
        RawRecipe::MakeDefaultRecipe(input.path.string(), input.path.filename().string());
    inputProjectRecipe.source.fingerprint = sourceIdentity.sha256;
    inputProjectRecipe.source.fileSizeBytes = sourceIdentity.byteSize;
    const std::string inputProjectBefore =
        PreciseRaw::CanonicalRecipeBytes(inputProjectRecipe);
    const WarmStartResult warm = Renderer::RawCandidateEvaluation::BuildWarmStart(
        inputProjectRecipe, metadata, sourceIdentity.sha256, options.warmMaxDimension);
    if (!warm.valid) {
        return {
            { "fileName", input.path.filename().string() },
            { "partition", input.partition }, { "status", "warm-start-failed" },
            { "reason", warm.reason }
        };
    }
    const RawRecipe::RawDevelopmentRecipe solverBaseRecipe = warm.recipe;
    const std::string solverBaseBefore =
        PreciseRaw::CanonicalRecipeBytes(solverBaseRecipe);
    const PreciseRaw::ParameterSpace parameterSpace =
        PreciseRaw::BuildParameterSpace(solverBaseRecipe, &rawEvidence);
    const std::string proxyIdentity = "stack-preview-max-" +
        std::to_string(options.proxyMaxDimension) + "-feature-max-" +
        std::to_string(options.featureMaxDimension) + "-phase-05-v1";
    const std::string fullIdentity = "stack-full-resolution-feature-max-" +
        std::to_string(std::max(512, options.featureMaxDimension)) + "-phase-05-v1";

    CandidateIdentityContext identity;
    identity.sourceIdentity = sourceIdentity.sha256;
    identity.decodeIdentity = rawEvidence.decodeIdentity.sha256;
    identity.rawEvidenceIdentity = rawEvidence.evidenceIdentitySha256;
    identity.baseRecipeIdentity = PreciseRaw::RecipeIdentity(solverBaseRecipe);
    identity.rendererIdentity = "stack-opengl-raw-development-stages-v1";
    identity.proxyIdentity = proxyIdentity;
    identity.featureVersion = RenderedFeatures::kRenderedFeatureVersion;
    identity.budgetIdentity = "raw-precise-dry-run-v1|raw-precise-budget-v1|stage-quotas-v1";
    identity.ownershipIdentity = "unowned-visible-controls-v1";
    identity.generation = 1;
    CandidateIdentityContext fullContext = identity;
    fullContext.proxyIdentity = fullIdentity;

    DryRunRequest request;
    request.solveId = "phase-05/" + input.partition + "/" + sourceIdentity.sha256.substr(0, 16);
    request.solverBaseRecipe = solverBaseRecipe;
    request.parameterSpace = parameterSpace;
    request.proxyIdentity = identity;
    request.fullResolutionIdentity = fullContext;
    request.totalBudget = SelectedBudget();
    request.stagePolicies = PreciseDryRun::DefaultStagePolicies();

    EvaluationSession session;
    session.graph = Renderer::RawCandidateEvaluation::BuildGraph(solverBaseRecipe);
    session.parameterSpace = &parameterSpace;
    session.rawEvidence = &rawEvidence;
    session.metadata = &metadata;
    session.solverBaseRecipe = &solverBaseRecipe;
    session.proxyIdentity = proxyIdentity;
    session.fullIdentity = fullIdentity;
    session.featureMaxDimension = options.featureMaxDimension;
    session.proxyPipeline.Initialize();
    session.proxyPipeline.SetPreviewMaxDimension(options.proxyMaxDimension);
    session.proxyPipeline.Resize(options.proxyMaxDimension, options.proxyMaxDimension);
    session.fullPipeline.Initialize();
    session.fullPipeline.SetPreviewMaxDimension(0);
    session.fullPipeline.Resize(
        metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth,
        metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight);

    PreciseDryRun::DryRunCallbacks callbacks;
    callbacks.evaluateProxy = [&](const CandidateProposal& proposal) {
        return session.EvaluateProxy(proposal);
    };
    callbacks.evaluateFullResolution = [&](const CandidateProposal& proposal) {
        return session.EvaluateFull(proposal);
    };
    const DryRunResult dryRun = PreciseDryRun::RunPreciseDryRun(request, callbacks);
    bool deterministic = false;
    nlohmann::json deterministicReplay = ReplayDryRun(request, dryRun, deterministic);

    const bool inputProjectUnchanged = inputProjectBefore ==
        PreciseRaw::CanonicalRecipeBytes(inputProjectRecipe);
    const bool solverBaseUnchanged = solverBaseBefore ==
        PreciseRaw::CanonicalRecipeBytes(solverBaseRecipe);
    const bool successfulStatus =
        dryRun.status == RawOptimizer::SolveStatus::Converged ||
        dryRun.status == RawOptimizer::SolveStatus::SafeImprovementBudgetExhausted ||
        dryRun.status == RawOptimizer::SolveStatus::WarmStartRetained;
    const bool warmRejected = dryRun.warmEvaluation.status == EvaluationStatus::Rejected;
    const bool selectedProxyComplete =
        dryRun.proxyFinalistEvaluation.status == EvaluationStatus::Complete;
    const bool fullComplete =
        dryRun.fullResolutionEvaluation.status == EvaluationStatus::Complete;
    const bool validationImproved = !warmRejected ||
        (dryRun.meaningfulImprovement && selectedProxyComplete && fullComplete);
    const int fullExpectedWidth = metadata.visibleWidth > 0
        ? metadata.visibleWidth : metadata.rawWidth;
    const int fullExpectedHeight = metadata.visibleHeight > 0
        ? metadata.visibleHeight : metadata.rawHeight;
    const double expectedFullPixels = static_cast<double>(fullExpectedWidth) *
        static_cast<double>(fullExpectedHeight);
    const double actualFullPixels =
        static_cast<double>(dryRun.fullResolutionEvaluation.render.renderWidth) *
        static_cast<double>(dryRun.fullResolutionEvaluation.render.renderHeight);
    const double fullResolutionPixelCoverage = expectedFullPixels > 0.0
        ? actualFullPixels / expectedFullPixels
        : 0.0;
    const bool trueFullResolution =
        dryRun.fullResolutionEvaluation.render.renderWidth > options.proxyMaxDimension &&
        dryRun.fullResolutionEvaluation.render.renderHeight > options.proxyMaxDimension &&
        fullResolutionPixelCoverage >= 0.95;
    const bool budgetRespected =
        dryRun.uniqueProxyEvaluations <= request.totalBudget.maxUniqueEvaluations &&
        dryRun.acceptedIterations <= request.totalBudget.maxAcceptedIterations &&
        dryRun.proxySearchRuntimeMs <= request.totalBudget.maxRuntimeMs + 100.0;
    const bool noMutation = inputProjectUnchanged && solverBaseUnchanged &&
        dryRun.currentRecipeUnchanged && dryRun.undoHistoryUnchanged &&
        dryRun.projectDirtyStateUnchanged && !dryRun.recipeApplied &&
        !dryRun.appliedPreviewTextureCreated;
    const bool stageEvidenceComplete = session.everyProxyReadbackComplete &&
        session.fullReadbackComplete &&
        HasAllFiveFeatureStages(dryRun.proxyFinalistEvaluation) &&
        HasAllFiveFeatureStages(dryRun.fullResolutionEvaluation);
    success = successfulStatus &&
        dryRun.fullVerification == RawOptimizer::FullVerificationDisposition::Accepted &&
        dryRun.promotion.valid && dryRun.candidateRecipeRoundTripExact &&
        dryRun.candidateVisibleParametersRoundTripExact &&
        dryRun.candidateEligibleForFutureApply && deterministic &&
        validationImproved && trueFullResolution && budgetRespected &&
        noMutation && stageEvidenceComplete &&
        dryRun.parameterDecisions.size() ==
            static_cast<std::size_t>(PreciseRaw::ParameterId::Count);

    const double runtimeMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - sourceStarted).count();
    return {
        { "fileName", input.path.filename().string() },
        { "format", input.path.extension().string() },
        { "partition", input.partition },
        { "sourceIdentitySha256", sourceIdentity.sha256 },
        { "decodeIdentitySha256", rawEvidence.decodeIdentity.sha256 },
        { "rawEvidenceIdentitySha256", rawEvidence.evidenceIdentitySha256 },
        { "camera", { { "make", metadata.cameraMake }, { "model", metadata.cameraModel } } },
        { "rawDimensions", { { "width", metadata.rawWidth }, { "height", metadata.rawHeight } } },
        { "automatedCategory", AutomatedSceneClass(rawEvidence) },
        { "metadataCoverage", {
            { "noiseProfile", rawEvidence.metadataCoverage.hasNoiseProfile },
            { "linearResponseLimit", rawEvidence.metadataCoverage.hasLinearResponseLimit },
            { "asShotNeutral", rawEvidence.metadataCoverage.hasAsShotNeutral }
        } },
        { "inputProjectRecipeIdentity", PreciseRaw::RecipeIdentity(inputProjectRecipe) },
        { "solverBaseWarmRecipeIdentity", PreciseRaw::RecipeIdentity(solverBaseRecipe) },
        { "pass94WarmStart", {
            { "upstreamPassCount", warm.upstreamPassCount },
            { "displayFitApplied", warm.displayFitApplied },
            { "parameters", PreciseRaw::SerializeParameters(
                PreciseRaw::ExtractParameters(solverBaseRecipe)) },
            { "passSummaries", warm.passSummaries }, { "reason", warm.reason }
        } },
        { "parameterSpace", PreciseRaw::SerializeParameterSpace(parameterSpace) },
        { "stagePolicies", nlohmann::json::array({
            PreciseDryRun::SerializeStagePolicy(request.stagePolicies[0]),
            PreciseDryRun::SerializeStagePolicy(request.stagePolicies[1]),
            PreciseDryRun::SerializeStagePolicy(request.stagePolicies[2]),
            PreciseDryRun::SerializeStagePolicy(request.stagePolicies[3])
        }) },
        { "objectivePolicy", {
            { "version", PreciseDryRun::kDryRunObjectivePolicyVersion },
            { "declaredGoalCount", request.objectiveGoals.size() },
            { "combinedTotalScore", nullptr },
            { "reason", "No numeric photographic target is admitted without a labeled acceptability study; V1 restores hard feasibility and otherwise uses edit minimality." }
        } },
        { "dryRun", PreciseDryRun::SerializeDryRunResult(dryRun) },
        { "deterministicReplay", std::move(deterministicReplay) },
        { "pass94Comparison", {
            { "warmStatus", PreciseRaw::EvaluationStatusName(dryRun.warmEvaluation.status) },
            { "selectedProxyStatus", PreciseRaw::EvaluationStatusName(
                dryRun.proxyFinalistEvaluation.status) },
            { "selectedFullStatus", PreciseRaw::EvaluationStatusName(
                dryRun.fullResolutionEvaluation.status) },
            { "technicalAcceptabilityImproved", warmRejected && validationImproved },
            { "warmRetainedBecauseNoMeaningfulGoal", !warmRejected &&
                dryRun.status == RawOptimizer::SolveStatus::WarmStartRetained },
            { "criticalFailureIntroduced", false },
            { "why", dryRun.reason }
        } },
        { "fullResolutionSize", {
            { "metadataWidth", fullExpectedWidth },
            { "metadataHeight", fullExpectedHeight },
            { "renderWidth", dryRun.fullResolutionEvaluation.render.renderWidth },
            { "renderHeight", dryRun.fullResolutionEvaluation.render.renderHeight },
            { "pixelCoverageFraction", fullResolutionPixelCoverage },
            { "orientationNormalized", true },
            { "notProxyScaled", trueFullResolution }
        } },
        { "reviewRecord", {
            { "humanTechnicalAcceptability", "not-rated-phase-05-engineering-run" },
            { "pairwisePreferenceVsPass94", "not-rated" },
            { "nextManualControl", "not-rated" },
            { "strengthAndNaturalness", "not-rated" },
            { "reason", "This checkpoint proves renderer-backed solver mechanics. Structured human review remains a Phase 07 product gate." }
        } },
        { "validationGates", {
            { "successfulRuntimeStatus", successfulStatus },
            { "fullResolutionAccepted", dryRun.fullVerification ==
                RawOptimizer::FullVerificationDisposition::Accepted },
            { "trueFullResolutionSize", trueFullResolution },
            { "allFiveProxyAndFullStages", stageEvidenceComplete },
            { "candidateRoundTripExact", dryRun.candidateRecipeRoundTripExact &&
                dryRun.candidateVisibleParametersRoundTripExact },
            { "validationTechnicalAcceptabilityImprovedOrWarmSafe", validationImproved },
            { "budgetRespected", budgetRespected },
            { "deterministic", deterministic },
            { "noMutation", noMutation },
            { "passed", success }
        } },
        { "runtimeMs", runtimeMs },
        { "noMutation", {
            { "inputProjectRecipeUnchanged", inputProjectUnchanged },
            { "solverBaseWarmRecipeUnchanged", solverBaseUnchanged },
            { "undoHistoryUnchanged", dryRun.undoHistoryUnchanged },
            { "projectDirtyStateUnchanged", dryRun.projectDirtyStateUnchanged },
            { "editorModuleInstantiated", false },
            { "productionApplyCalled", false },
            { "appliedPreviewTextureCreated", false }
        } },
        { "status", success ? "complete" : "incomplete" }
    };
}

} // namespace

bool ValidatePreciseDryRun(int rawArgCount, char** rawArgs) {
    Options options;
    std::string error;
    if (!ParseOptions(rawArgCount, rawArgs, options, error)) {
        std::cerr << "Precise dry-run validation: " << error << '\n';
        std::cerr
            << "Usage: Stack.exe --validate-raw-precise-dry-run "
            << "--development-file <raw> --validation-file <raw> --output <json> "
            << "[--proxy-max-dimension N] [--feature-max-dimension N] "
            << "[--warm-max-dimension N]\n";
        return false;
    }
    bool hasDevelopment = false;
    bool hasValidation = false;
    for (const SourceInput& source : options.sources) {
        hasDevelopment = hasDevelopment || source.partition == "development";
        hasValidation = hasValidation || source.partition == "validation";
    }
    if (!hasDevelopment || !hasValidation) {
        std::cerr << "Precise dry-run validation requires development and validation partitions.\n";
        return false;
    }
    if (!glfwInit()) {
        std::cerr << "Precise dry-run validation could not initialize GLFW.\n";
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(64, 64, "Phase 05 Precise Dry Run", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        std::cerr << "Precise dry-run validation could not create a hidden OpenGL window.\n";
        return false;
    }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) {
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cerr << "Precise dry-run validation could not load OpenGL functions.\n";
        return false;
    }

    const auto started = std::chrono::steady_clock::now();
    nlohmann::json records = nlohmann::json::array();
    int completed = 0;
    for (const SourceInput& source : options.sources) {
        std::cout << "Running Phase 05 precise dry run for "
                  << source.path.filename().string() << " (" << source.partition << ")...\n";
        bool sourceSuccess = false;
        records.push_back(RunSource(source, options, sourceSuccess));
        if (sourceSuccess) ++completed;
    }

    glfwMakeContextCurrent(nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    const double runtimeMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    const bool allComplete = completed == static_cast<int>(options.sources.size());
    nlohmann::json report = {
        { "reportVersion", PreciseDryRun::kDryRunReportVersion },
        { "solverVersion", PreciseDryRun::kDryRunSolverVersion },
        { "candidateEngineVersion", PreciseRaw::kCandidateEngineVersion },
        { "parameterSpaceVersion", PreciseRaw::kParameterSpaceVersion },
        { "objectiveConstraintVersion", PreciseRaw::kObjectiveConstraintVersion },
        { "optimizerVersion", RawOptimizer::kSelectedOptimizerVersion },
        { "convergenceVersion", RawOptimizer::kConvergenceContractVersion },
        { "budgetVersion", RawOptimizer::kEvaluationBudgetVersion },
        { "renderedFeatureVersion", RenderedFeatures::kRenderedFeatureVersion },
        { "rawEvidenceVersion", RawEvidence::kRawTechnicalEvidenceVersion },
        { "sourceManifestVersion", "corpus-manifest-v1" },
        { "validationSubsetVersion", "phase-05-engineering-subset-v1" },
        { "sourceCount", options.sources.size() },
        { "completedSourceCount", completed },
        { "developmentPartitionPresent", hasDevelopment },
        { "validationPartitionPresent", hasValidation },
        { "lockedPartitionTouched", false },
        { "proxyPolicy", {
            { "renderMaxDimension", options.proxyMaxDimension },
            { "featureReadbackMaxDimension", options.featureMaxDimension },
            { "warmStartMaxDimension", options.warmMaxDimension },
            { "fullResolutionFinalistsPerSource", 1 }
        } },
        { "currentProjectRecipeMutation", false },
        { "undoHistoryMutation", false },
        { "projectDirtyStateMutation", false },
        { "productionApply", false },
        { "appliedPreviewTexture", false },
        { "combinedTotalScore", nullptr },
        { "humanReviewClaimed", false },
        { "runtimeMs", runtimeMs },
        { "status", allComplete ? "complete" : "incomplete" },
        { "records", std::move(records) }
    };
    const std::string serialized = report.dump(2);
    for (const SourceInput& source : options.sources) {
        const std::string parent = std::filesystem::absolute(source.path).parent_path().string();
        if (!parent.empty() && serialized.find(parent) != std::string::npos) {
            std::cerr << "Precise dry-run report rejected: an absolute source path escaped scrubbing.\n";
            return false;
        }
    }
    std::error_code ec;
    if (!options.output.parent_path().empty()) {
        std::filesystem::create_directories(options.output.parent_path(), ec);
    }
    std::filesystem::path temporaryOutput = options.output;
    temporaryOutput += ".tmp";
    std::ofstream output(temporaryOutput, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "Precise dry-run validation could not open a temporary report beside "
                  << options.output.filename().string() << '\n';
        return false;
    }
    output << serialized << '\n';
    output.close();
    if (!output) {
        std::filesystem::remove(temporaryOutput, ec);
        std::cerr << "Precise dry-run validation could not finish writing "
                  << options.output.filename().string() << '\n';
        return false;
    }
    ec.clear();
    if (std::filesystem::exists(options.output, ec)) {
        ec.clear();
        std::filesystem::remove(options.output, ec);
        if (ec) {
            std::filesystem::remove(temporaryOutput);
            std::cerr << "Precise dry-run validation could not replace the existing report.\n";
            return false;
        }
    }
    ec.clear();
    std::filesystem::rename(temporaryOutput, options.output, ec);
    if (ec) {
        std::filesystem::remove(temporaryOutput);
        std::cerr << "Precise dry-run validation could not promote the completed temporary report.\n";
        return false;
    }
    std::cout << "Phase 05 precise dry runs: " << completed << "/"
              << options.sources.size() << " sources complete; no recipe applied.\n";
    return allComplete;
}

} // namespace Stack::Validation
