#include "Editor/EditorRenderWorker.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Renderer/RenderPipeline.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <string>
#include <unordered_map>

namespace {

std::string TrimDevelopProgressText(std::string text, std::size_t maxLength) {
    if (text.size() <= maxLength) {
        return text;
    }
    if (maxLength <= 3) {
        return text.substr(0, maxLength);
    }
    return text.substr(0, maxLength - 3) + "...";
}

std::string BuildDevelopCandidateProgressLabel(
    const std::string& candidateLabel,
    const std::string& candidateRevisionStage,
    int candidateIndex,
    int candidateCount) {
    const int visibleIndex = std::max(1, candidateIndex + 1);
    const int visibleCount = std::max(visibleIndex, candidateCount);
    std::string label =
        "Measuring Develop feedback " +
        std::to_string(visibleIndex) +
        "/" +
        std::to_string(visibleCount);
    if (!candidateLabel.empty()) {
        label += ": " + TrimDevelopProgressText(candidateLabel, 48);
    }
    if (!candidateRevisionStage.empty()) {
        label += " [" + TrimDevelopProgressText(candidateRevisionStage, 24) + "]";
    }
    label += "...";
    return label;
}

float MillisecondsBetween(
    std::chrono::steady_clock::time_point begin,
    std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<float, std::milli>(end - begin).count();
}

struct PreparedDevelopCandidateGraph {
    RenderGraphSnapshot graph;
    std::size_t developNodeIndex = std::numeric_limits<std::size_t>::max();
    int syntheticOutputId = -1;
    int syntheticPreFinishOutputId = -1;
};

void PopulateCandidateResultFromRequest(
    const EditorRenderWorker::DevelopCandidateRenderRequest& request,
    EditorRenderWorker::DevelopCandidateRenderResult& candidateResult) {
    candidateResult.developNodeId = request.developNodeId;
    candidateResult.candidateId = request.candidateId;
    candidateResult.candidateLabel = request.candidateLabel;
    candidateResult.candidateRevisionStage = request.candidateRevisionStage;
    candidateResult.activeRevisionStage = request.activeRevisionStage;
    candidateResult.activeRefineIntent = request.activeRefineIntent;
    candidateResult.stageSchedulerExpectedDirtyBoundary = request.stageSchedulerExpectedDirtyBoundary;
    candidateResult.stageSchedulerReason = request.stageSchedulerReason;
    candidateResult.dirtyGeneration = request.dirtyGeneration;
    candidateResult.solveFingerprint = request.solveFingerprint;
    candidateResult.rawDevelopInteractionSerial = request.rawDevelopInteractionSerial;
    candidateResult.guidanceFingerprint = request.guidanceFingerprint;
    candidateResult.solveScore = request.solveScore;
    candidateResult.stageSchedulerOrder = request.stageSchedulerOrder;
    candidateResult.stageSchedulerRank = request.stageSchedulerRank;
    candidateResult.adaptiveRenderBudget = request.adaptiveRenderBudget;
    candidateResult.adaptiveRenderBudgetVersion = request.adaptiveRenderBudgetVersion;
    candidateResult.adaptiveRenderBudgetReason = request.adaptiveRenderBudgetReason;
    candidateResult.adaptiveRenderBudgetContinuationDecision =
        request.adaptiveRenderBudgetContinuationDecision;
    candidateResult.adaptiveRenderBudgetConvergenceState =
        request.adaptiveRenderBudgetConvergenceState;
    candidateResult.adaptiveRenderBudgetConvergenceDecision =
        request.adaptiveRenderBudgetConvergenceDecision;
    candidateResult.adaptiveRenderBudgetConvergenceReason =
        request.adaptiveRenderBudgetConvergenceReason;
    candidateResult.activeStageMatch = request.activeStageMatch;
    candidateResult.stageReservedRequest = request.stageReservedRequest;
    candidateResult.activeRefineIntentMatch = request.activeRefineIntentMatch;
    candidateResult.refineIntentReservedRequest = request.refineIntentReservedRequest;
    candidateResult.adaptiveRenderBudgetExpanded = request.adaptiveRenderBudgetExpanded;
    candidateResult.adaptiveRenderBudgetNarrowed = request.adaptiveRenderBudgetNarrowed;
    candidateResult.metricReadbackMaxDimension = request.metricReadbackMaxDimension;
}

} // namespace

void EditorRenderWorker::RenderDevelopCandidateRequests(
    const Snapshot& snapshot,
    RenderPipeline& pipeline,
    Result& result,
    int totalProgressSteps,
    int& progressCompleted) {
    if (snapshot.developCandidateRenders.empty()) {
        return;
    }

    auto reportProgress = [&](std::string label) {
        SetProgress(progressCompleted, totalProgressSteps, std::move(label));
    };
    auto finishProgressStep = [&](std::string label = {}) {
        progressCompleted = std::min(totalProgressSteps, progressCompleted + 1);
        SetProgress(progressCompleted, totalProgressSteps, std::move(label));
    };
    auto shouldAbortStaleWork = [&]() {
        return ShouldAbortStaleSnapshot(snapshot.generation);
    };
    auto reportSupersededWork = [&]() {
        reportProgress("Newer render queued; skipping stale feedback...");
    };

    if (shouldAbortStaleWork()) {
        reportSupersededWork();
        return;
    }

    result.developCandidateRenders.reserve(snapshot.developCandidateRenders.size());
    int candidateIndex = 0;
    const int candidateCount = static_cast<int>(snapshot.developCandidateRenders.size());

    if (snapshot.width <= 0 || snapshot.height <= 0) {
        for (const DevelopCandidateRenderRequest& request : snapshot.developCandidateRenders) {
            if (shouldAbortStaleWork()) {
                reportSupersededWork();
                break;
            }

            reportProgress(BuildDevelopCandidateProgressLabel(
                request.candidateLabel,
                request.candidateRevisionStage,
                candidateIndex,
                candidateCount));
            DevelopCandidateRenderResult candidateResult;
            PopulateCandidateResultFromRequest(request, candidateResult);
            candidateResult.error = "No source image for candidate render.";
            result.developCandidateRenders.push_back(std::move(candidateResult));
            ++candidateIndex;
            finishProgressStep("Develop feedback measured.");
        }
        return;
    }

    std::unordered_map<int, std::size_t> developNodeIndices;
    developNodeIndices.reserve(snapshot.graph.nodes.size());
    for (std::size_t i = 0; i < snapshot.graph.nodes.size(); ++i) {
        const RenderGraphNode& node = snapshot.graph.nodes[i];
        if (node.kind == RenderGraphNodeKind::RawDevelop) {
            developNodeIndices.emplace(node.nodeId, i);
        }
    }

    std::unordered_map<int, PreparedDevelopCandidateGraph> preparedGraphs;
    preparedGraphs.reserve(developNodeIndices.size());

    auto getOrCreatePreparedGraph =
        [&](const DevelopCandidateRenderRequest& request) -> PreparedDevelopCandidateGraph* {
            auto preparedIt = preparedGraphs.find(request.developNodeId);
            if (preparedIt != preparedGraphs.end()) {
                return &preparedIt->second;
            }

            const auto nodeIndexIt = developNodeIndices.find(request.developNodeId);
            if (nodeIndexIt == developNodeIndices.end()) {
                return nullptr;
            }

            PreparedDevelopCandidateGraph prepared;
            prepared.graph = snapshot.graph;
            prepared.developNodeIndex = nodeIndexIt->second;
            prepared.syntheticOutputId = -200000 - request.developNodeId * 2;
            prepared.syntheticPreFinishOutputId = prepared.syntheticOutputId - 1;

            RenderGraphNode finalOutputNode;
            finalOutputNode.nodeId = prepared.syntheticOutputId;
            finalOutputNode.kind = RenderGraphNodeKind::Output;
            prepared.graph.nodes.push_back(std::move(finalOutputNode));
            prepared.graph.links.push_back(RenderGraphLink{
                request.developNodeId,
                EditorNodeGraph::kImageOutputSocketId,
                prepared.syntheticOutputId,
                EditorNodeGraph::kImageInputSocketId
            });

            RenderGraphNode preFinishOutputNode;
            preFinishOutputNode.nodeId = prepared.syntheticPreFinishOutputId;
            preFinishOutputNode.kind = RenderGraphNodeKind::Output;
            prepared.graph.nodes.push_back(std::move(preFinishOutputNode));
            prepared.graph.links.push_back(RenderGraphLink{
                request.developNodeId,
                EditorNodeGraph::kPreFinishImageOutputSocketId,
                prepared.syntheticPreFinishOutputId,
                EditorNodeGraph::kImageInputSocketId
            });
            prepared.graph.outputSocketId.clear();

            auto [it, inserted] = preparedGraphs.emplace(request.developNodeId, std::move(prepared));
            (void)inserted;
            return &it->second;
        };

    for (const DevelopCandidateRenderRequest& request : snapshot.developCandidateRenders) {
        if (shouldAbortStaleWork()) {
            reportSupersededWork();
            break;
        }

        reportProgress(BuildDevelopCandidateProgressLabel(
            request.candidateLabel,
            request.candidateRevisionStage,
            candidateIndex,
            candidateCount));

        DevelopCandidateRenderResult candidateResult;
        PopulateCandidateResultFromRequest(request, candidateResult);

        PreparedDevelopCandidateGraph* preparedGraph = getOrCreatePreparedGraph(request);
        if (!preparedGraph ||
            preparedGraph->developNodeIndex >= preparedGraph->graph.nodes.size()) {
            candidateResult.error = "Develop node was not present in the render snapshot.";
            result.developCandidateRenders.push_back(std::move(candidateResult));
            ++candidateIndex;
            finishProgressStep("Develop feedback measured.");
            continue;
        }

        RenderGraphNode& candidateNode = preparedGraph->graph.nodes[preparedGraph->developNodeIndex];
        candidateNode.rawDevelop = request.rawDevelop;
        candidateNode.requestRevision = std::max<std::uint64_t>(1, request.dirtyGeneration);

        auto renderCandidateSocket = [&](int syntheticOutputId,
                                         int& outWidth,
                                         int& outHeight,
                                         bool* outRawBaseCacheHit = nullptr,
                                         bool* outPreFinishCacheHit = nullptr,
                                         int metricReadbackMaxDimension = 0,
                                         float* outGraphMs = nullptr,
                                         float* outReadbackMs = nullptr) {
            preparedGraph->graph.outputNodeId = syntheticOutputId;

            pipeline.LoadSourceFromSharedPixels(
                snapshot.sourcePixels,
                snapshot.width,
                snapshot.height,
                snapshot.channels);
            const auto graphBegin = std::chrono::steady_clock::now();
            pipeline.ExecuteGraph(preparedGraph->graph);
            const auto graphEnd = std::chrono::steady_clock::now();
            if (outGraphMs) {
                *outGraphMs = MillisecondsBetween(graphBegin, graphEnd);
            }
            if (outRawBaseCacheHit) {
                *outRawBaseCacheHit = pipeline.WasGraphImageCacheHit(
                    request.developNodeId,
                    "__rawDevelopBase");
            }
            if (outPreFinishCacheHit) {
                *outPreFinishCacheHit = pipeline.WasGraphImageCacheHit(
                    request.developNodeId,
                    EditorNodeGraph::kPreFinishImageOutputSocketId);
            }
            const auto readbackBegin = std::chrono::steady_clock::now();
            std::vector<unsigned char> pixels = metricReadbackMaxDimension > 0
                ? pipeline.GetOutputPixels(outWidth, outHeight, metricReadbackMaxDimension)
                : pipeline.GetOutputPixels(outWidth, outHeight);
            const auto readbackEnd = std::chrono::steady_clock::now();
            if (outReadbackMs) {
                *outReadbackMs = MillisecondsBetween(readbackBegin, readbackEnd);
            }
            return pixels;
        };

        const auto candidateBegin = std::chrono::steady_clock::now();
        std::vector<unsigned char> candidatePixels = renderCandidateSocket(
            preparedGraph->syntheticOutputId,
            candidateResult.width,
            candidateResult.height,
            &candidateResult.rawBaseCacheHitDuringFinalRender,
            &candidateResult.preFinishCacheHitDuringFinalRender,
            request.metricReadbackMaxDimension,
            &candidateResult.finalGraphMs,
            &candidateResult.finalReadbackMs);
        candidateResult.success =
            !candidatePixels.empty() &&
            candidateResult.width > 0 &&
            candidateResult.height > 0;
        candidateResult.metricsReadbackDownsampled =
            candidateResult.success &&
            request.metricReadbackMaxDimension > 0 &&
            std::max(snapshot.width, snapshot.height) > request.metricReadbackMaxDimension;
        if (candidateResult.success) {
            const auto analysisBegin = std::chrono::steady_clock::now();
            candidateResult.metrics = AnalyzeDevelopCandidatePixelsForValidation(
                candidatePixels,
                candidateResult.width,
                candidateResult.height,
                request.subjectSampling);
            const auto analysisEnd = std::chrono::steady_clock::now();
            candidateResult.finalAnalysisMs =
                MillisecondsBetween(analysisBegin, analysisEnd);
        } else {
            candidateResult.error = "Candidate render produced no pixels.";
        }

        if (request.measurePreFinish && !shouldAbortStaleWork()) {
            const auto preFinishReadbackBegin = std::chrono::steady_clock::now();
            std::vector<unsigned char> preFinishPixels =
                pipeline.GetCachedGraphImagePixels(
                    request.developNodeId,
                    EditorNodeGraph::kPreFinishImageOutputSocketId,
                    candidateResult.preFinishWidth,
                    candidateResult.preFinishHeight,
                    request.metricReadbackMaxDimension);
            const auto preFinishReadbackEnd = std::chrono::steady_clock::now();
            candidateResult.preFinishReadbackMs =
                MillisecondsBetween(preFinishReadbackBegin, preFinishReadbackEnd);
            candidateResult.preFinishReusedFromFinalRender = !preFinishPixels.empty();
            if (preFinishPixels.empty()) {
                preFinishPixels = renderCandidateSocket(
                    preparedGraph->syntheticPreFinishOutputId,
                    candidateResult.preFinishWidth,
                    candidateResult.preFinishHeight,
                    nullptr,
                    nullptr,
                    request.metricReadbackMaxDimension,
                    &candidateResult.preFinishGraphMs,
                    &candidateResult.preFinishReadbackMs);
            }
            candidateResult.preFinishSuccess =
                !preFinishPixels.empty() &&
                candidateResult.preFinishWidth > 0 &&
                candidateResult.preFinishHeight > 0;
            candidateResult.preFinishMetricsReadbackDownsampled =
                candidateResult.preFinishSuccess &&
                request.metricReadbackMaxDimension > 0 &&
                std::max(snapshot.width, snapshot.height) > request.metricReadbackMaxDimension;
            if (candidateResult.preFinishSuccess) {
                const auto preFinishAnalysisBegin = std::chrono::steady_clock::now();
                candidateResult.preFinishMetrics = AnalyzeDevelopCandidatePixelsForValidation(
                    preFinishPixels,
                    candidateResult.preFinishWidth,
                    candidateResult.preFinishHeight,
                    request.subjectSampling);
                const auto preFinishAnalysisEnd = std::chrono::steady_clock::now();
                candidateResult.preFinishAnalysisMs =
                    MillisecondsBetween(preFinishAnalysisBegin, preFinishAnalysisEnd);
            }
        }

        const auto candidateEnd = std::chrono::steady_clock::now();
        candidateResult.totalElapsedMs =
            MillisecondsBetween(candidateBegin, candidateEnd);
        result.developCandidateRenders.push_back(std::move(candidateResult));
        ++candidateIndex;
        finishProgressStep("Develop feedback measured.");
    }
}
