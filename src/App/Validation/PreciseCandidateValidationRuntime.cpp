#include "App/Validation/PreciseCandidateValidationRuntime.h"

#include "Editor/EditorRenderWorker.h"
#include "Raw/RawAutoBase.h"
#include "Raw/RawAutoStartPoint.h"
#include "Raw/RawImageAnalysis.h"
#include "Raw/RenderedFeatureEvidence.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

namespace Stack::Validation::PreciseCandidateRuntime {
namespace {

using RawAutoStartPoint::RawAutoStartPointStage;
using RawAutoStartPoint::RawAutoStartPointStageImage;
using RenderedFeatures::FeatureContext;
using RenderedFeatures::LinearRgbImage;

std::string Sha256Text(const std::string& text) {
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    return RawEvidence::ComputeSourceIdentity(bytes).sha256;
}

RawAnalysis::CurrentFrameInputStats ToCurrentFrameStats(const RenderTextureStats& stats) {
    RawAnalysis::CurrentFrameInputStats result;
    result.valid = stats.valid;
    result.p001Luma = stats.p001Luma;
    result.p01Luma = stats.p01Luma;
    result.p05Luma = stats.p05Luma;
    result.p50Luma = stats.p50Luma;
    result.p95Luma = stats.p95Luma;
    result.p99Luma = stats.p99Luma;
    result.p999Luma = stats.p999Luma;
    result.logAverageLuma = stats.logAverageLuma;
    result.dynamicRangeEv = stats.dynamicRangeEv;
    result.validPixelPercent = stats.validPixelPercent;
    result.hdrPixelPercent = stats.hdrPixelPercent;
    result.displayClipPercent = stats.displayClipPercent;
    return result;
}

void SetGraphRecipe(RenderGraphSnapshot& graph, const RawRecipe::RawDevelopmentRecipe& recipe) {
    for (RenderGraphNode& node : graph.nodes) {
        if (node.kind == RenderGraphNodeKind::RawDevelopment) {
            node.rawDevelopment.recipe = recipe;
            ++node.requestRevision;
            return;
        }
    }
}

bool SameVisibleRecipe(
    const RawRecipe::RawDevelopmentRecipe& a,
    const RawRecipe::RawDevelopmentRecipe& b) {
    return PreciseRaw::CanonicalRecipeBytes(a) == PreciseRaw::CanonicalRecipeBytes(b);
}

const char* StageName(RawAutoStartPointStage stage) {
    return RawAutoStartPoint::StageStableString(stage);
}

LinearRgbImage ToLinearImage(const RawAutoStartPointStageImage& image) {
    LinearRgbImage result;
    result.width = image.width;
    result.height = image.height;
    result.pixels = image.pixels;
    return result;
}

const RawAutoStartPointStageImage* FindImage(
    const std::vector<RawAutoStartPointStageImage>& images,
    RawAutoStartPointStage stage) {
    const auto found = std::find_if(images.begin(), images.end(), [&](const auto& image) {
        return image.valid && image.stage == stage;
    });
    return found == images.end() ? nullptr : &*found;
}

FeatureContext BuildFeatureContext(
    const PreciseRaw::CandidateProposal& proposal,
    const RawAutoStartPointStageImage& image,
    const Raw::RawMetadata& metadata) {
    FeatureContext context;
    context.sourceIdentity = proposal.identities.sourceIdentity;
    context.recipeIdentity = proposal.candidateRecipeIdentity;
    context.stage = StageName(image.stage);
    context.colorSpace = image.measurementDomain.empty()
        ? "stack-linear-srgb-working-output"
        : image.measurementDomain;
    context.colorTransformIdentity = "stack-raw-gpu-linear-srgb-to-xyz-d65-v1";
    context.transferFunction = "linear";
    context.workingToXyz = {
        0.4124564, 0.3575761, 0.1804375,
        0.2126729, 0.7151522, 0.0721750,
        0.0193339, 0.1191920, 0.9503041
    };
    context.referenceGrey = 0.18;
    context.rawEvidenceIdentity = proposal.identities.rawEvidenceIdentity;
    context.cropIdentity = "stack-raw-visible-area-and-recipe-crop-v1";
    context.orientationNormalized = true;
    context.sourceWidth = metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
    context.sourceHeight = metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight;
    context.globalLiftEv = proposal.parameters.rawExposureEv;
    context.maximumLocalLiftEv = std::max(0.0, proposal.parameters.localDeltaEv);
    return context;
}

} // namespace

RenderGraphSnapshot BuildGraph(const RawRecipe::RawDevelopmentRecipe& recipe) {
    RenderGraphSnapshot graph;
    graph.outputNodeId = 2;
    RenderGraphNode rawDevelopment;
    rawDevelopment.nodeId = 1;
    rawDevelopment.requestRevision = 1;
    rawDevelopment.kind = RenderGraphNodeKind::RawDevelopment;
    rawDevelopment.rawDevelopment.recipe = recipe;
    graph.nodes.push_back(std::move(rawDevelopment));
    RenderGraphNode output;
    output.nodeId = 2;
    output.requestRevision = 1;
    output.kind = RenderGraphNodeKind::Output;
    graph.nodes.push_back(std::move(output));
    graph.links.push_back(RenderGraphLink { 1, "imageOut", 2, "imageIn" });
    return graph;
}

WarmStartResult BuildPass94WarmStart(
    const RawRecipe::RawDevelopmentRecipe& baseRecipe,
    const Raw::RawMetadata& metadata,
    const std::string& sourceKey,
    int maxDimension,
    const std::function<bool()>& shouldCancel) {
    WarmStartResult result;
    result.recipe = baseRecipe;
    const auto canceled = [&]() { return shouldCancel && shouldCancel(); };
    if (canceled()) {
        result.reason = "Pass 94 warm start canceled before rendering.";
        return result;
    }
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.SetPreviewMaxDimension(maxDimension);
    pipeline.Resize(maxDimension, maxDimension);
    RenderGraphSnapshot graph = BuildGraph(result.recipe);
    RawAnalysis::RawImageAnalysis latestAnalysis;
    for (int upstreamPass = 0; upstreamPass < 4; ++upstreamPass) {
        if (canceled()) {
            result.reason = "Pass 94 warm start canceled during upstream search.";
            return result;
        }
        SetGraphRecipe(graph, result.recipe);
        pipeline.ExecuteGraph(graph);
        if (pipeline.GetOutputTexture() == 0) {
            result.reason = "Pass 94 warm-start render failed.";
            return result;
        }
        RawAutoStartPoint::RawAutoStartPointDiagnostics diagnostics =
            pipeline.BuildRawDevelopmentStartPointDiagnostics(sourceKey);
        latestAnalysis = RawAnalysis::BuildCurrentFrameAnalysisFromCurrentFrameStats(
            ToCurrentFrameStats(pipeline.GetRawDevelopmentViewTransformInputStats()), sourceKey);
        latestAnalysis.metadata = RawAnalysis::BuildRawMetadataSummary(metadata);
        const RawAutoBase::AutoBaseRecommendations recommendations =
            RawAutoBase::BuildAutoBaseRecommendations(
                latestAnalysis, result.recipe, nullptr,
                &pipeline.GetRawDevelopmentLocalSuggestionImage());
        diagnostics = RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            std::move(diagnostics), result.recipe, latestAnalysis, recommendations);
        std::vector<RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest> requests =
            RawAutoStartPoint::CollectCandidateRenderRequests(diagnostics);
        for (int candidatePass = 0; candidatePass < 4 && !requests.empty(); ++candidatePass) {
            if (canceled()) {
                result.reason = "Pass 94 warm start canceled during candidate rendering.";
                return result;
            }
            const auto rendered = EditorRenderWorker::RenderRawWorkspaceStartPointCandidateRequests(
                pipeline, graph, sourceKey, requests);
            const bool anySuccess = std::any_of(rendered.begin(), rendered.end(), [](const auto& item) {
                return item.success;
            });
            diagnostics = RawAutoStartPoint::MergeCandidateRenderResults(
                std::move(diagnostics), rendered, result.recipe, latestAnalysis, recommendations);
            requests = RawAutoStartPoint::CollectCandidateRenderRequests(diagnostics);
            if (!anySuccess) break;
        }
        const RawAutoStartPoint::RawAutoStartPointConservativePlan plan =
            RawAutoStartPoint::BuildConservativeStartingPointPlan(
                result.recipe, latestAnalysis, recommendations, diagnostics);
        result.passSummaries.push_back(plan.summary);
        if (!plan.valid || !plan.hasUpstreamRecipeChanges ||
            SameVisibleRecipe(result.recipe, plan.upstreamRecipe)) {
            break;
        }
        result.recipe = plan.upstreamRecipe;
        ++result.upstreamPassCount;
    }
    if (canceled()) {
        result.reason = "Pass 94 warm start canceled before final fit.";
        return result;
    }
    SetGraphRecipe(graph, result.recipe);
    pipeline.ExecuteGraph(graph);
    if (pipeline.GetOutputTexture() == 0) {
        result.reason = "Final Pass 94 warm-start analysis render failed.";
        return result;
    }
    latestAnalysis = RawAnalysis::BuildCurrentFrameAnalysisFromCurrentFrameStats(
        ToCurrentFrameStats(pipeline.GetRawDevelopmentViewTransformInputStats()), sourceKey);
    latestAnalysis.metadata = RawAnalysis::BuildRawMetadataSummary(metadata);
    const RawAutoBase::ViewFitDecision fit =
        RawAutoBase::BuildAutoBaseViewFitDecision(latestAnalysis, result.recipe);
    if (fit.canApply) {
        RawAutoBase::ApplyViewTransformFitToRecipe(result.recipe, fit.fit);
        result.displayFitApplied = true;
    } else {
        result.passSummaries.push_back(
            fit.reason.empty() ? "Pass 94 Display Fit unavailable." : fit.reason);
    }
    result.valid = true;
    result.reason = "Pass 94 bounded heuristic reproduced in isolated validation state.";
    return result;
}

RenderedEvaluation RenderAndEvaluate(
    RenderPipeline& pipeline,
    const RenderGraphSnapshot& graph,
    const PreciseRaw::CandidateProposal& proposal,
    const PreciseRaw::ParameterSpace& parameterSpace,
    const RawEvidence::RawTechnicalEvidenceRecord& rawEvidence,
    const Raw::RawMetadata& metadata,
    const std::vector<RawAutoStartPointStageImage>* warmImages,
    const std::string& proxyIdentity,
    bool fullResolution,
    int featureMaxDimension,
    const RawRecipe::RawDevelopmentRecipe& solverBaseRecipe,
    const std::string& requestReason,
    const std::string& renderRequestTag) {
    RenderedEvaluation output;
    RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest request;
    request.valid = proposal.valid;
    request.id = proposal.candidateId + renderRequestTag;
    request.stage = RawAutoStartPointStage::FinishToneCandidate;
    request.hasRecipe = true;
    request.recipe = proposal.recipe;
    request.featureReadbackMaxDimension = featureMaxDimension;
    request.reason = requestReason;
    const auto featureBegin = std::chrono::steady_clock::now();
    const auto results = EditorRenderWorker::RenderRawWorkspaceStartPointCandidateRequests(
        pipeline, graph, proposal.identities.sourceIdentity, { request });
    PreciseRaw::CandidateRenderEvidence render;
    render.attempted = true;
    render.fullResolution = fullResolution;
    render.proxyIdentity = proxyIdentity;
    render.renderIdentity = Sha256Text(
        proposal.identities.rendererIdentity + "|" + proposal.candidateId + "|" + proxyIdentity);
    if (results.empty()) {
        render.error = "Candidate renderer returned no result.";
    } else {
        const auto& candidate = results.front();
        render.success = candidate.success;
        render.error = candidate.error;
        render.renderWidth = candidate.renderWidth;
        render.renderHeight = candidate.renderHeight;
        render.renderRuntimeMs = candidate.renderMs;
        render.imageCacheHits = candidate.imageCacheHits;
        render.imageCacheMisses = candidate.imageCacheMisses;
        render.rawStageCacheHits = candidate.rawStageCacheHits;
        render.rawStageCacheMisses = candidate.rawStageCacheMisses;
        output.images = candidate.stageImageReadbacks;
        for (const RawAutoStartPointStageImage& image : output.images) {
            if (!image.valid) continue;
            const LinearRgbImage current = ToLinearImage(image);
            if (!current.Valid()) continue;
            FeatureContext context = BuildFeatureContext(proposal, image, metadata);
            PreciseRaw::StageFeatureEvidence stage;
            stage.stage = image.stage;
            stage.features = image.displayMappedLinearRgb
                ? RenderedFeatures::AnalyzeDisplayMapped(current, context)
                : RenderedFeatures::AnalyzeSceneLinear(current, context, &rawEvidence);
            const RawAutoStartPointStageImage* warm = warmImages
                ? FindImage(*warmImages, image.stage)
                : nullptr;
            const LinearRgbImage reference = warm ? ToLinearImage(*warm) : current;
            stage.comparisonToWarm = RenderedFeatures::CompareRenderedImages(
                reference, current, context);
            render.featureWidth = std::max(render.featureWidth, image.width);
            render.featureHeight = std::max(render.featureHeight, image.height);
            render.stages.push_back(std::move(stage));
        }
    }
    render.featureRuntimeMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - featureBegin).count() - render.renderRuntimeMs;
    render.featureRuntimeMs = std::max(0.0, render.featureRuntimeMs);
    output.evaluation = PreciseRaw::EvaluateCandidate(
        proposal,
        parameterSpace,
        &rawEvidence,
        std::move(render),
        solverBaseRecipe,
        solverBaseRecipe,
        0,
        0,
        false,
        false);
    return output;
}

bool HasRequiredStages(const RenderedEvaluation& rendered) {
    for (const RawAutoStartPointStage stage : {
             RawAutoStartPointStage::NeutralScene,
             RawAutoStartPointStage::RawPlacement,
             RawAutoStartPointStage::LocalCandidate,
             RawAutoStartPointStage::FinishToneCandidate,
             RawAutoStartPointStage::DisplayCandidate }) {
        if (!FindImage(rendered.images, stage)) return false;
    }
    return true;
}

} // namespace Stack::Validation::PreciseCandidateRuntime
