#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/Internal/RenderPipelineGraphSchedule.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionRuntime.h"
#include "Renderer/RenderTiling.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "NodeMath/ReductionMath.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawLoader.h"
#include "Utils/PixelBufferUtils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

using namespace Stack::Renderer::GraphExecution;

namespace {

struct ScopedGraphExecutionState {
    Stack::Renderer::GLState::FramebufferState framebuffer{ true };
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean stencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean blend = glIsEnabled(GL_BLEND);

    ~ScopedGraphExecutionState() {
        Restore();
    }

    void Restore() const {
        framebuffer.Restore(true);
        SetCapability(GL_SCISSOR_TEST, scissor);
        SetCapability(GL_DEPTH_TEST, depth);
        SetCapability(GL_STENCIL_TEST, stencil);
        SetCapability(GL_BLEND, blend);
    }

private:
    static void SetCapability(GLenum capability, GLboolean enabled) {
        if (enabled == GL_TRUE) {
            glEnable(capability);
        } else {
            glDisable(capability);
        }
    }
};

bool TryResolveIntrinsicGraphCanvas(
    const RenderGraphSnapshot& graph,
    const GraphTopologyIndex& topology,
    int& outWidth,
    int& outHeight,
    std::string& outError) {
    outWidth = 0;
    outHeight = 0;
    outError.clear();

    auto acceptRawMetadata = [&](const Raw::RawMetadata& metadata) {
        if (metadata.rawWidth <= 0 && metadata.visibleWidth <= 0) {
            return false;
        }
        if (metadata.rawHeight <= 0 && metadata.visibleHeight <= 0) {
            return false;
        }
        outWidth = Raw::DisplayWidth(metadata);
        outHeight = Raw::DisplayHeight(metadata);
        return outWidth > 0 && outHeight > 0;
    };

    std::vector<int> pending { graph.outputNodeId };
    std::unordered_set<int> visited;
    visited.reserve(topology.nodes.size());
    while (!pending.empty()) {
        const int nodeId = pending.back();
        pending.pop_back();
        if (!visited.insert(nodeId).second) {
            continue;
        }

        const auto nodeIt = topology.nodes.find(nodeId);
        if (nodeIt == topology.nodes.end() || nodeIt->second == nullptr) {
            continue;
        }
        const RenderGraphNode& node = *nodeIt->second;
        if (node.kind == RenderGraphNodeKind::Image &&
            node.image.width > 0 && node.image.height > 0) {
            outWidth = node.image.width;
            outHeight = node.image.height;
            return true;
        }
        if (node.kind == RenderGraphNodeKind::RawSource &&
            acceptRawMetadata(node.rawSource.metadata)) {
            return true;
        }
        if (node.kind == RenderGraphNodeKind::RawDevelopment ||
            (node.kind == RenderGraphNodeKind::RawProjectSourceSet && node.rawProjectSourceSet.resultAvailable)) {
            if (node.rawDevelopment.embeddedRawData &&
                acceptRawMetadata(
                    node.rawDevelopment.embeddedRawData->metadata)) {
                return true;
            }

            const std::string& sourcePath =
                node.rawDevelopment.recipe.source.sourcePath;
            if (!sourcePath.empty()) {
                Raw::RawMetadata metadata;
                if (Raw::RawLoader::LoadMetadata(sourcePath, metadata) &&
                    acceptRawMetadata(metadata)) {
                    return true;
                }
                outError = metadata.error.empty()
                    ? "The RAW source metadata could not be read from " +
                        sourcePath + "."
                    : metadata.error;
            }
        }

        const auto inputsIt = topology.inputsByNode.find(nodeId);
        if (inputsIt == topology.inputsByNode.end()) {
            continue;
        }
        for (const RenderGraphLink* link : inputsIt->second) {
            if (link != nullptr && link->fromNodeId > 0) {
                pending.push_back(link->fromNodeId);
            }
        }
    }

    if (outError.empty()) {
        outError =
            "The active graph has no decoded base image or self-sizing source.";
    }
    return false;
}

} // namespace

void RenderPipeline::SetGraphCacheBudget(
    std::uint64_t totalCacheBudgetBytes,
    std::uint64_t minimumRawStageCacheBytes, bool reserveRawStages) {
    const std::uint64_t proportionalRawBudget =
        reserveRawStages ? (totalCacheBudgetBytes / 20u) * 9u : 0;
    // A percentage-only split can leave the RAW cache a few bytes short of
    // one native RGBA16F boundary. In that state every downstream adjustment
    // must repeat sensor-domain work even though the combined cache allowance
    // can retain it. Reserve one boundary first, then preserve the established
    // 8:3 persistent/transient split in the remaining budget.
    m_RawDevelopStageCacheBudgetBytes = std::min(
        totalCacheBudgetBytes,
        std::max(proportionalRawBudget, minimumRawStageCacheBytes));
    const std::uint64_t nonRawBudget =
        totalCacheBudgetBytes - m_RawDevelopStageCacheBudgetBytes;
    m_GraphPersistentCacheBudgetBytes =
        (nonRawBudget / 11u) * 8u;
    m_GraphTransientTargetBudgetBytes =
        totalCacheBudgetBytes -
        m_RawDevelopStageCacheBudgetBytes -
        m_GraphPersistentCacheBudgetBytes;

    std::uint64_t dependencyBytes = 0;
    for (const auto& [key, entries] : m_RawDevelopStageImageCache)
        for (const auto& entry : entries)
            if (entry.viewportNativeDependency) dependencyBytes += RawDevelopStageCacheEntryBytes(entry);
    if (dependencyBytes > m_RawDevelopStageCacheBudgetBytes)
        for (auto& [key, entries] : m_RawDevelopStageImageCache)
            for (auto& entry : entries) entry.viewportNativeDependency = false;

    (void)TrimRawDevelopStageCacheToBudget(
        RawDevelopStageCacheTotalBytes(),
        m_RawDevelopStageCacheBudgetBytes);
    TrimGraphPersistentCachesToBudget();
    TrimGraphTransientTargetsToBudget();
}

void RenderPipeline::ExecuteGraphImpl(
    const RenderGraphSnapshot& graph,
    const GraphTopologyIndex& topology) {
    const bool sceneRaw = graph.rawLayerBackgroundNodeId > 0 ||
        std::any_of(graph.nodes.begin(),graph.nodes.end(),[](const auto& node) {
            return node.kind == RenderGraphNodeKind::RawDevelopment;
        });
    // Keep near-black and extended scene values through the same precision
    // path on Background and layers. Display encoding is the final boundary.
    if (sceneRaw) EnsureGraphFloat32Targets();
    m_GraphSourceTexture = 0;
    m_GraphSourceWidth = 0;
    m_GraphSourceHeight = 0;
    if (m_BaseCanvasWidth > 0 && m_BaseCanvasHeight > 0) {
        m_Width = m_BaseCanvasWidth;
        m_Height = m_BaseCanvasHeight;
    }
    m_LastGraphImageCacheHits.clear();
    m_LastGraphExecutionStats = {};
    m_LastGraphExecutionStats.rawStageCacheBudgetBytes =
        m_RawDevelopStageCacheBudgetBytes;
    m_LastGraphExecutionStats.persistentCacheBudgetBytes =
        m_GraphPersistentCacheBudgetBytes;
    m_LastGraphExecutionStats.transientPoolBudgetBytes =
        m_GraphTransientTargetBudgetBytes;
    m_AutoGainSceneStatsCache.clear();
    m_PreLocalExposureSummaries.clear();
    m_ToneCurveAutoRewriteFeedback.clear();
    ClearRawDevelopmentStageStatsReadbacks();
    ClearRawDevelopmentLocalRangeOverlay();
    ClearRawDevelopmentLocalRangeTargetSample();
    m_RawDevelopmentLocalSuggestionImage = {};
    m_RawDevelopmentLocalRangeOverlayRequestMode = graph.rawWorkspaceLocalRangeOverlayMode;
    m_RawDevelopmentLocalRangeTargetPreviewRequest =
        graph.rawWorkspaceLocalRangeTargetPreview;
    m_RawDevelopmentLocalRangeTargetSampleRequested =
        graph.rawWorkspaceLocalRangeTargetSampleRequested;
    m_RawDevelopmentLocalRangeTargetSampleRequestU =
        std::clamp(graph.rawWorkspaceLocalRangeTargetSampleU, 0.0f, 1.0f);
    m_RawDevelopmentLocalRangeTargetSampleRequestV =
        std::clamp(graph.rawWorkspaceLocalRangeTargetSampleV, 0.0f, 1.0f);
    if (!topology.IsBoundTo(graph) || !topology.valid) {
        m_OutputTexture = 0;
        m_LastGraphExecutionStats.lastSpecializedFailureNodeId =
            graph.outputNodeId;
        m_LastGraphExecutionStats.lastSpecializedFailure =
            !topology.IsBoundTo(graph)
                ? "Graph scheduling failed: the render topology index is not bound to this graph snapshot."
                : "Graph scheduling failed: " +
                    (topology.error.empty()
                        ? std::string("the render topology index is invalid.")
                        : topology.error);
        return;
    }
    const bool hasOutputNode =
        topology.nodes.count(graph.outputNodeId) != 0;
    if (!hasOutputNode) {
        m_OutputTexture = 0;
        m_LastGraphExecutionStats.lastSpecializedFailureNodeId = graph.outputNodeId;
        m_LastGraphExecutionStats.lastSpecializedFailure =
            "The selected graph output is missing from the render snapshot.";
        return;
    }
    if (m_Width <= 0 || m_Height <= 0) {
        int intrinsicWidth = 0;
        int intrinsicHeight = 0;
        std::string intrinsicError;
        if (!TryResolveIntrinsicGraphCanvas(
                graph,
                topology,
                intrinsicWidth,
                intrinsicHeight,
                intrinsicError) ||
            !Resize(intrinsicWidth, intrinsicHeight)) {
            m_OutputTexture = 0;
            m_LastGraphExecutionStats.lastSpecializedFailureNodeId =
                graph.outputNodeId;
            m_LastGraphExecutionStats.lastSpecializedFailure =
                intrinsicError.empty()
                    ? "The active graph source could not establish its canvas dimensions."
                    : intrinsicError;
            return;
        }
    }

    const bool rawDevelopmentSideEffectsRequested =
        graph.rawWorkspaceLocalRangeTargetSampleRequested ||
        (!graph.rawWorkspaceLocalRangeOverlayMode.empty() &&
         graph.rawWorkspaceLocalRangeOverlayMode != "none");
    std::vector<ScheduledGraphOutput> scheduleExtraRoots;
    if (graph.rawLayerScopeNodeId > 0 && m_RawDevelopmentGraphScopeStage != RawDevelopmentGraphScopeStage::None)
        scheduleExtraRoots.push_back({graph.rawLayerScopeNodeId, graph.rawLayerScopeSocketId});
    if (rawDevelopmentSideEffectsRequested) {
        for (const RenderGraphNode& node : graph.nodes) {
            if (node.kind == RenderGraphNodeKind::RawDevelopment) {
                scheduleExtraRoots.push_back(
                    ScheduledGraphOutput{
                        node.nodeId,
                        EditorNodeGraph::kImageOutputSocketId
                    });
            }
        }
    }
    const GraphEvaluationSchedule evaluationSchedule =
        BuildGraphEvaluationSchedule(
            graph,
            topology,
            ScheduledGraphOutput{
                graph.outputNodeId,
                graph.outputSocketId
            },
            scheduleExtraRoots);
    if (!evaluationSchedule.valid) {
        m_OutputTexture = 0;
        m_LastGraphExecutionStats.lastSpecializedFailureNodeId =
            graph.outputNodeId;
        m_LastGraphExecutionStats.lastSpecializedFailure =
            "Graph scheduling failed: " + evaluationSchedule.error;
        return;
    }

    const RenderGraphRegionPlan regionPlan =
        RenderTiling::PlanGraphRegions(graph, m_Width, m_Height);
    if (!regionPlan.valid) {
        m_OutputTexture = 0;
        m_LastGraphExecutionStats.lastSpecializedFailureNodeId = graph.outputNodeId;
        m_LastGraphExecutionStats.lastSpecializedFailure =
            "Graph planning failed: " + regionPlan.reason;
        return;
    }

    const ScopedGraphExecutionState savedExecutionState;

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, m_Width, m_Height);

    if (m_ExternalOutputTexture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
        m_ExternalOutputTexture = 0;
    }

    GraphExecutionRuntime executionRuntime(
        *this,
        graph,
        topology,
        evaluationSchedule,
        rawDevelopmentSideEffectsRequested);
    executionRuntime.Execute();

    savedExecutionState.Restore();
}
