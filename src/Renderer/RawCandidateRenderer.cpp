#include "Renderer/RawCandidateRenderer.h"
#include "Renderer/RenderPipeline.h"
#include "Raw/RawAutoBase.h"

#include <algorithm>
#include <chrono>

namespace Stack::Renderer {

Stack::RawAnalysis::CurrentFrameInputStats BuildCurrentFrameInputStats(const RenderTextureStats& stats) {
    Stack::RawAnalysis::CurrentFrameInputStats rawStats;
    rawStats.valid = stats.valid;
    rawStats.p001Luma = stats.p001Luma;
    rawStats.p01Luma = stats.p01Luma;
    rawStats.p05Luma = stats.p05Luma;
    rawStats.p50Luma = stats.p50Luma;
    rawStats.p95Luma = stats.p95Luma;
    rawStats.p99Luma = stats.p99Luma;
    rawStats.p999Luma = stats.p999Luma;
    rawStats.logAverageLuma = stats.logAverageLuma;
    rawStats.dynamicRangeEv = stats.dynamicRangeEv;
    rawStats.validPixelPercent = stats.validPixelPercent;
    rawStats.hdrPixelPercent = stats.hdrPixelPercent;
    rawStats.displayClipPercent = stats.displayClipPercent;
    return rawStats;
}

namespace {
float ElapsedMilliseconds(
    std::chrono::steady_clock::time_point begin,
    std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<float, std::milli>(end - begin).count();
}

std::size_t FindRawDevelopmentNodeForCandidate(
    const RenderGraphSnapshot& graph,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    std::size_t firstRawDevelopment = graph.nodes.size();
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        const RenderGraphNode& node = graph.nodes[i];
        if (node.kind != RenderGraphNodeKind::RawDevelopment) {
            continue;
        }
        if (firstRawDevelopment == graph.nodes.size()) {
            firstRawDevelopment = i;
        }
        if (!recipe.source.sourcePath.empty() &&
            node.rawDevelopment.recipe.source.sourcePath == recipe.source.sourcePath) {
            return i;
        }
    }
    return firstRawDevelopment;
}
} // namespace

std::vector<RawAutoStartPoint::RawAutoStartPointCandidateRenderResult>
RenderRawCandidates(
    RenderPipeline& pipeline,
    const RenderGraphSnapshot& graph,
    const std::string& sourceKey,
    const std::vector<Stack::RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest>& requests) {
    std::vector<RawAutoStartPoint::RawAutoStartPointCandidateRenderResult> results;
    results.reserve(requests.size());
    if (requests.empty()) {
        return results;
    }

    pipeline.SetRawDevelopmentAnalysisEnabled(true);
    int requestIndex = 0;
    for (const Stack::RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest& request : requests) {
        RawAutoStartPoint::RawAutoStartPointCandidateRenderResult result;
        result.request = request;
        result.attempted = true;
        if (!request.valid || !request.hasRecipe) {
            result.error = "Candidate render request did not include a valid visible recipe.";
            results.push_back(std::move(result));
            ++requestIndex;
            continue;
        }

        RenderGraphSnapshot candidateGraph = graph;
        candidateGraph.rawWorkspaceLocalRangeOverlayMode.clear();
        candidateGraph.rawWorkspaceLocalRangeTargetSampleRequested = false;
        const std::size_t rawDevelopmentIndex =
            FindRawDevelopmentNodeForCandidate(candidateGraph, request.recipe);
        if (rawDevelopmentIndex >= candidateGraph.nodes.size()) {
            result.error = "No RAW Development node was present for the candidate render.";
            results.push_back(std::move(result));
            ++requestIndex;
            continue;
        }

        RenderGraphNode& rawDevelopmentNode = candidateGraph.nodes[rawDevelopmentIndex];
        rawDevelopmentNode.rawDevelopment.recipe = request.recipe;
        rawDevelopmentNode.requestRevision =
            std::max<std::uint64_t>(
                rawDevelopmentNode.requestRevision + 1,
                static_cast<std::uint64_t>(requestIndex + 1));

        pipeline.SetRawDevelopmentStageImageReadbackMaxDimension(
            request.featureReadbackMaxDimension);
        pipeline.SetRawDevelopmentGraphScopeReadbackRequest(
            RawDevelopmentGraphScopeStage::None,
            0);

        auto executeCandidateGraph =
            [&](std::vector<RawDevelopmentStageStatsReadback>& outReadbacks) {
                const auto renderBegin = std::chrono::steady_clock::now();
                pipeline.ExecuteGraph(candidateGraph);
                result.renderMs +=
                    ElapsedMilliseconds(renderBegin, std::chrono::steady_clock::now());
                result.renderWidth = pipeline.GetCanvasWidth();
                result.renderHeight = pipeline.GetCanvasHeight();
                outReadbacks = pipeline.GetRawDevelopmentStageStatsReadbacks();
                result.stageImageReadbacks = pipeline.GetRawDevelopmentStageImageReadbacks();
                const GraphExecutionStats& graphStats = pipeline.GetLastGraphExecutionStats();
                result.imageCacheHits += graphStats.imageCacheHits;
                result.imageCacheMisses += graphStats.imageCacheMisses;
                result.rawStageCacheHits += graphStats.rawStageCacheHits;
                result.rawStageCacheMisses += graphStats.rawStageCacheMisses;
                result.diagnostics = pipeline.BuildRawDevelopmentStartPointDiagnostics(sourceKey);
            };
        auto hasCompleteRequestedStage =
            [&](const std::vector<RawDevelopmentStageStatsReadback>& readbacks) {
                return std::any_of(
                    readbacks.begin(),
                    readbacks.end(),
                    [&](const RawDevelopmentStageStatsReadback& readback) {
                        return readback.valid &&
                            readback.stage == request.stage &&
                            readback.status ==
                                Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
                    });
            };

        std::vector<RawDevelopmentStageStatsReadback> stageStatsReadbacks;
        executeCandidateGraph(stageStatsReadbacks);
        if (request.stage == Stack::RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate) {
            const RenderTextureStats viewTransformInputStats =
                pipeline.GetRawDevelopmentViewTransformInputStats();
            const Stack::RawAnalysis::RawImageAnalysis candidateAnalysis =
                Stack::RawAnalysis::BuildCurrentFrameAnalysisFromCurrentFrameStats(
                    BuildCurrentFrameInputStats(viewTransformInputStats),
                    sourceKey);
            const Stack::RawAutoBase::ViewFitDecision fitDecision =
                Stack::RawAutoBase::BuildAutoBaseViewFitDecision(
                    candidateAnalysis,
                    request.recipe);
            if (!fitDecision.canApply) {
                result.error = fitDecision.reason.empty()
                    ? "Display Candidate render could not fit View Transform from the post-edit analysis."
                    : fitDecision.reason;
                pipeline.SetRawDevelopmentStageImageReadbackMaxDimension(0);
                results.push_back(std::move(result));
                ++requestIndex;
                continue;
            }

            Stack::RawRecipe::RawDevelopmentRecipe fittedRecipe = request.recipe;
            Stack::RawAutoBase::ApplyViewTransformFitToRecipe(fittedRecipe, fitDecision.fit);
            result.hasRenderedRecipe = true;
            result.renderedRecipe = fittedRecipe;
            rawDevelopmentNode.rawDevelopment.recipe = std::move(fittedRecipe);
            rawDevelopmentNode.requestRevision =
                std::max<std::uint64_t>(
                    rawDevelopmentNode.requestRevision + 1,
                    static_cast<std::uint64_t>(requestIndex + 1001));
            executeCandidateGraph(stageStatsReadbacks);
        }

        result.success =
            result.diagnostics.valid &&
            hasCompleteRequestedStage(stageStatsReadbacks);
        if (!result.success) {
            result.error =
                "Candidate render completed without a complete readback for the requested Starting Point stage.";
        } else if (!result.hasRenderedRecipe && request.hasRecipe) {
            result.hasRenderedRecipe = true;
            result.renderedRecipe = request.recipe;
        }
        pipeline.SetRawDevelopmentStageImageReadbackMaxDimension(0);
        results.push_back(std::move(result));
        ++requestIndex;
    }
    return results;
}

} // namespace Stack::Renderer
