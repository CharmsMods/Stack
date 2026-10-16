#include "Renderer/Internal/RenderPipelineGraphExecutionRuntime.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "NodeMath/ReductionMath.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Renderer/RawPreviewProxy.h"
#include "Utils/PixelBufferUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Stack::Renderer::GraphExecution {

std::pair<int,int> GraphExecutionRuntime::ProjectMaskCanvasExtent(const RenderGraphNode& node) const {
    const auto& spatial = node.semanticDescriptor.spatial;
    if (node.kind == RenderGraphNodeKind::CustomMask && node.maskCanvasSourceNodeId<=0)
        return RawPreviewProxy::FitPreviewExtent(node.customMask.width,node.customMask.height,pipeline.m_PreviewMaxDimension);
    if (spatial.state != Stack::NodeMath::KnowledgeState::Known) return {0,0};
    return RawPreviewProxy::FitPreviewExtent(spatial.value.nativeWidth,spatial.value.nativeHeight,pipeline.m_PreviewMaxDimension);
}

GraphExecutionRuntime::GraphExecutionRuntime(
    RenderPipeline& pipeline,
    const RenderGraphSnapshot& graph,
    const GraphTopologyIndex& topology,
    const GraphEvaluationSchedule& evaluationSchedule,
    bool rawDevelopmentSideEffectsRequested)
    : pipeline(pipeline)
    , graph(graph)
    , topology(topology)
    , evaluationSchedule(evaluationSchedule)
    , rawDevelopmentSideEffectsRequested(rawDevelopmentSideEffectsRequested)
    , executionContext(
        graph,
        topology,
        std::max<std::size_t>(1u, evaluationSchedule.outputs.size()))
    , nodes(executionContext.nodes)
    , imageCache(executionContext.imageCache)
    , maskCache(executionContext.maskCache)
    , imageFingerprintCache(executionContext.imageFingerprintCache)
    , maskFingerprintCache(executionContext.maskFingerprintCache)
    , visitingImages(executionContext.visitingImages)
    , visitingMasks(executionContext.visitingMasks)
    , fingerprintingImages(executionContext.fingerprintingImages)
    , fingerprintingMasks(executionContext.fingerprintingMasks) {
    const std::size_t executionEntryCapacity =
        std::max<std::size_t>(1u, evaluationSchedule.outputs.size());
    scalarCache.reserve(executionEntryCapacity);
    localTextureExtents.reserve(executionEntryCapacity);
    scalarSampleCounts.reserve(executionEntryCapacity);
    scalarFingerprintCache.reserve(executionEntryCapacity);
    visitingScalars.reserve(executionEntryCapacity);
    fingerprintingScalars.reserve(executionEntryCapacity);
    pointwiseFusionDisabledNodes.reserve(graph.nodes.size());
    std::vector<RenderGraphViewportEndpoint> completeConsumers;
    if (graph.rawLayerScopeNodeId > 0 && pipeline.m_RawDevelopmentGraphScopeStage != RawDevelopmentGraphScopeStage::None)
        completeConsumers.push_back({graph.rawLayerScopeNodeId, graph.rawLayerScopeSocketId});
    if (rawDevelopmentSideEffectsRequested)
        for (const auto& node : graph.nodes) if (node.kind == RenderGraphNodeKind::RawDevelopment)
            completeConsumers.push_back({node.nodeId, "imageOut"});
    viewportPlan = RenderTiling::PlanGraphViewport(graph, pipeline.m_BaseCanvasWidth, pipeline.m_BaseCanvasHeight,
        pipeline.m_PreviewMaxDimension, pipeline.m_GraphViewportRequest, completeConsumers);
    if (pipeline.m_GraphViewportRequest.enabled && !pipeline.m_GraphViewportRequest.empty && !viewportPlan.valid)
        throw std::runtime_error(viewportPlan.reason);
    if (viewportPlan.valid) {
        pipeline.m_LastGraphExecutionStats.viewportCompleteDependencyPixels = viewportPlan.fullInputPixels;
        pipeline.m_LastGraphExecutionStats.viewportWorkingBytes = viewportPlan.workingBytes;
        pipeline.m_LastGraphExecutionStats.viewportPlanFingerprint = viewportPlan.fingerprint;
    }

    BindMaskFingerprint();
    BindScalarFingerprint();
    BindImageFingerprint();
    BindMaskEvaluation();
    BindScalarEvaluation();
    BindImageEvaluation();
    BindViewportEvaluation();
}

void GraphExecutionRuntime::Execute() {
    for (const ScheduledGraphOutput& scheduled :
            evaluationSchedule.outputs) {
        const auto scheduledNode = nodes.find(scheduled.nodeId);
        if (scheduledNode == nodes.end() || !scheduledNode->second) {
            continue;
        }
        const RenderGraphNode& node = *scheduledNode->second;
        const bool scalarValueOutput = ((node.kind == RenderGraphNodeKind::FieldMean || node.kind == RenderGraphNodeKind::Value) &&
            scheduled.socketId == EditorNodeGraph::kValueOutputSocketId) ||
            (node.kind == RenderGraphNodeKind::DataMath && node.dataMathSettings.domain == Stack::NodeMath::ArithmeticDomain::Values);
        if (scalarValueOutput) {
            double ignoredValue = 0.0;
            (void)evalScalar(
                scheduled.nodeId,
                scheduled.socketId,
                ignoredValue);
            continue;
        }
        if (IsScalarRenderSocket(
                executionContext,
                scheduled.nodeId,
                scheduled.socketId)) {
            (void)evalMask(
                scheduled.nodeId,
                scheduled.socketId);
        } else {
            (void)evalImage(
                scheduled.nodeId,
                scheduled.socketId);
        }
    }

    if (rawDevelopmentSideEffectsRequested) {
        // Target samples and diagnostic overlays are auxiliary outputs. Force
        // the active RAW Development node to execute even when the visible
        // Output pixels are cacheable and unchanged.
        for (const auto& [nodeId, node] : nodes) {
            if (node != nullptr &&
                node->kind == RenderGraphNodeKind::RawDevelopment &&
                executionContext.IsActiveNode(nodeId)) {
                (void)evalImage(
                    nodeId,
                    EditorNodeGraph::kImageOutputSocketId);
            }
        }
    }

    unsigned int finalTexture = 0;
    const auto outputIt = nodes.find(graph.outputNodeId);
    if (outputIt != nodes.end() &&
        (outputIt->second->kind == RenderGraphNodeKind::MaskGenerator ||
         outputIt->second->kind == RenderGraphNodeKind::MaskUtility ||
         outputIt->second->kind == RenderGraphNodeKind::ImageToMask ||
         outputIt->second->kind == RenderGraphNodeKind::MaskCombine ||
         outputIt->second->kind == RenderGraphNodeKind::CustomMask ||
         outputIt->second->kind == RenderGraphNodeKind::ChannelSplit ||
         outputIt->second->kind == RenderGraphNodeKind::ConstantChannel ||
         (outputIt->second->kind == RenderGraphNodeKind::DataMath && IsScalarRenderSocket(executionContext, graph.outputNodeId, graph.outputSocketId)) ||
         (outputIt->second->kind == RenderGraphNodeKind::RawDetailAutoMask && graph.outputSocketId == "maskOut") ||
         (outputIt->second->kind == RenderGraphNodeKind::RawDetailFusion && graph.outputSocketId == "maskOut"))) {
        finalTexture = evalMask(graph.outputNodeId, graph.outputSocketId);
    } else {
        finalTexture = evalImage(graph.outputNodeId, graph.outputSocketId);
    }
    const auto finalRegion = pipeline.m_RawViewportAppliedRegion;
    const auto finalGeometry = pipeline.m_CurrentGraphImageGeometry;
    const int finalWidth = pipeline.m_Width;
    const int finalHeight = pipeline.m_Height;
    if (graph.rawLayerScopeNodeId > 0 && pipeline.m_RawDevelopmentGraphScopeStage != RawDevelopmentGraphScopeStage::None) {
        const auto scopeTexture = evalImage(graph.rawLayerScopeNodeId, graph.rawLayerScopeSocketId);
        if (pipeline.m_RawDevelopmentGraphScopeStage == RawDevelopmentGraphScopeStage::LocalRangeInput)
            pipeline.CaptureRawLayerLocalScope(executionContext, graph.rawLayerScopeOperationId, scopeTexture, evalMask);
        else {
            const auto& descriptor=graph.rawLayerScopeDescriptor;
            const bool linear=descriptor.transfer.state==Stack::NodeMath::KnowledgeState::Known &&
                descriptor.transfer.value.kind==Stack::NodeMath::TransferKind::Linear;
            pipeline.CaptureRawDevelopmentGraphScopeReadback(pipeline.m_RawDevelopmentGraphScopeStage,
                scopeTexture, pipeline.m_Width, pipeline.m_Height, "authored-node-input-values", linear);
            auto& readback = pipeline.m_RawDevelopmentGraphScopeReadback;
            if (readback.valid) readback.graphInputFingerprint=fingerprintImage(graph.rawLayerScopeNodeId,graph.rawLayerScopeSocketId);
            if (readback.valid && readback.stage == RawDevelopmentGraphScopeStage::ColorWarpInput) {
                const auto native = pipeline.ResolveGraphNativeExtent(executionContext,
                    graph.rawLayerScopeNodeId, graph.rawLayerScopeSocketId);
                if (native.first > 0 && native.second > 0) {
                    readback.sourceWidth = native.first;
                    readback.sourceHeight = native.second;
                }
                readback.graphInputFingerprint = fingerprintImage(
                    graph.rawLayerScopeNodeId, graph.rawLayerScopeSocketId);
            }
        }
    }
    pipeline.m_OutputTexture = finalTexture ? finalTexture : 0;
    pipeline.m_GraphSourceTexture = 0;
    const int referenceSourceNodeId = pipeline.FindReferenceSourceNode(executionContext, graph.outputNodeId);
    if (referenceSourceNodeId > 0) {
        const auto referenceIt = nodes.find(referenceSourceNodeId);
        if (referenceIt != nodes.end() &&
            referenceIt->second &&
            referenceIt->second->kind == RenderGraphNodeKind::RawSource) {
            pipeline.m_GraphSourceTexture = pipeline.m_SourceTexture;
            pipeline.m_GraphSourceWidth = pipeline.m_BaseCanvasWidth;
            pipeline.m_GraphSourceHeight = pipeline.m_BaseCanvasHeight;
        } else {
            pipeline.m_GraphSourceTexture = evalImage(referenceSourceNodeId, "imageOut");
            if (pipeline.m_GraphSourceTexture != 0) {
                pipeline.m_GraphSourceWidth = pipeline.m_Width;
                pipeline.m_GraphSourceHeight = pipeline.m_Height;
            }
        }
    }
    pipeline.m_RawViewportAppliedRegion = finalRegion;
    pipeline.m_GraphViewportGeometry = finalGeometry;
    // Input crops are owned by this execution until publication. A pass-through
    // output can itself be a crop, so transfer that one to the pipeline owner.
    for (auto& temporary : viewportTemporaryTextures)
        if (temporary && temporary == finalTexture) {
            pipeline.m_ExternalOutputTexture = temporary;
            temporary = 0;
            break;
        }
    if (finalTexture != 0 && finalWidth > 0 && finalHeight > 0) {
        pipeline.m_Width = finalWidth;
        pipeline.m_Height = finalHeight;
    }

    pipeline.PruneInactiveGraphCache(pipeline.m_GraphImageCache, executionContext);
    pipeline.PruneInactiveGraphCache(pipeline.m_GraphMaskCache, executionContext);
    for (auto scalarIt = pipeline.m_GraphScalarCache.begin(); scalarIt != pipeline.m_GraphScalarCache.end(); ) {
        if (!executionContext.IsActiveNode(ExtractNodeIdFromCacheKey(scalarIt->first))) {
            scalarIt = pipeline.m_GraphScalarCache.erase(scalarIt);
        } else {
            ++scalarIt;
        }
    }
    pipeline.PruneInactiveLutTextureCache(executionContext);
    pipeline.PruneInactiveRawDevelopStageCache(executionContext);
    pipeline.TrimGraphPersistentCachesToBudget();
    pipeline.TrimGraphTransientTargetsToBudget();
    pipeline.m_LastGraphExecutionStats.rawStageCacheBytes =
        pipeline.RawDevelopStageCacheTotalBytes();
    pipeline.m_LastGraphExecutionStats.persistentCacheBytes = pipeline.GraphPersistentCacheBytes();
    pipeline.m_LastGraphExecutionStats.transientPoolBytes = pipeline.GraphTransientTargetBytes();

}

} // namespace Stack::Renderer::GraphExecution
