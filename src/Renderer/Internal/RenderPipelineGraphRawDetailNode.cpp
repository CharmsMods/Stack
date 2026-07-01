#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <functional>
#include <string>
#include <string_view>

using namespace Stack::Renderer::GraphExecution;

int RenderPipeline::FindRawDetailAutoMaskSource(
    const GraphExecutionContext& executionContext,
    int nodeId,
    std::string_view socketId) {
    const auto nodeIt = executionContext.nodes.find(nodeId);
    if (nodeIt == executionContext.nodes.end() || !nodeIt->second) {
        return -1;
    }

    const RenderGraphNode& node = *nodeIt->second;
    if (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId == "maskOut") {
        return node.nodeId;
    }
    if (node.kind == RenderGraphNodeKind::MaskCombine && socketId == "maskOut") {
        if (const RenderGraphLink* inputA = executionContext.FindInputLink(node.nodeId, "maskA")) {
            const int source = FindRawDetailAutoMaskSource(executionContext, inputA->fromNodeId, inputA->fromSocketId);
            if (source > 0) {
                return source;
            }
        }
        if (const RenderGraphLink* inputB = executionContext.FindInputLink(node.nodeId, "maskB")) {
            return FindRawDetailAutoMaskSource(executionContext, inputB->fromNodeId, inputB->fromSocketId);
        }
    }
    if (node.kind == RenderGraphNodeKind::MaskUtility && socketId == "maskOut") {
        const RenderGraphLink* input = executionContext.FindInputLink(node.nodeId, "maskIn");
        return input ? FindRawDetailAutoMaskSource(executionContext, input->fromNodeId, input->fromSocketId) : -1;
    }
    return -1;
}

Raw::RawDetailFusionSettings RenderPipeline::ResolveRawDetailFusionApplySettings(
    const GraphExecutionContext& executionContext,
    const RenderGraphNode& node) {
    Raw::RawDetailFusionSettings settings = node.rawDetailFusion.settings;
    if (const RenderGraphLink* maskLink = executionContext.FindInputLink(node.nodeId, "maskIn")) {
        const int autoMaskNodeId = FindRawDetailAutoMaskSource(executionContext, maskLink->fromNodeId, maskLink->fromSocketId);
        const auto autoIt = executionContext.nodes.find(autoMaskNodeId);
        if (autoIt != executionContext.nodes.end() && autoIt->second) {
            const Raw::RawDetailFusionSettings& autoSettings = autoIt->second->rawDetailAutoMask.settings;
            settings.minEv = autoSettings.minEv;
            settings.maxEv = autoSettings.maxEv;
            settings.baseEv = autoSettings.baseEv;
        }
    }
    return settings;
}

RenderPipeline::GraphNodeRenderResult RenderPipeline::RenderRawDetailGraphNode(
    const GraphExecutionContext& executionContext,
    const RenderGraphNode& node,
    const std::string& socketId,
    const std::function<unsigned int(int, const std::string&)>& evalImage,
    const std::function<unsigned int(int, const std::string&)>& evalMask) {
    GraphNodeRenderResult result;

    if (node.kind == RenderGraphNodeKind::RawDetailAutoMask) {
        if (socketId != "maskOut") {
            return result;
        }
        const RenderGraphLink* input = executionContext.FindInputLink(node.nodeId, "imageIn");
        const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
        if (inputImage) {
            const bool debugPreview = executionContext.graph.autoGainMaskPreview &&
                executionContext.graph.outputNodeId == node.nodeId &&
                executionContext.graph.outputSocketId == "maskOut";
            result.texture = RenderRawDetailAutoMask(inputImage, node, 0, debugPreview);
            result.owned = result.texture != 0;
        }
        return result;
    }

    if (node.kind != RenderGraphNodeKind::RawDetailFusion) {
        return result;
    }

    const RenderGraphLink* input = executionContext.FindInputLink(node.nodeId, "imageIn");
    const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
    if (!inputImage) {
        return result;
    }

    const RenderGraphLink* maskLink = executionContext.FindInputLink(node.nodeId, "maskIn");
    if (socketId == "maskOut") {
        const unsigned int manualMask = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
        const bool debugPreview = executionContext.graph.autoGainMaskPreview &&
            executionContext.graph.outputNodeId == node.nodeId &&
            executionContext.graph.outputSocketId == "maskOut";
        result.texture = RenderRawDetailAutoMask(inputImage, node, manualMask, debugPreview);
        result.owned = result.texture != 0;
        return result;
    }

    const unsigned int generatedMask = evalMask(node.nodeId, "maskOut");
    const Raw::RawDetailFusionSettings applySettings = ResolveRawDetailFusionApplySettings(executionContext, node);
    m_PreLocalExposureSummaries[node.nodeId] = BuildPreLocalExposureSummary(
        inputImage,
        applySettings,
        maskLink != nullptr,
        !node.rawDetailFusion.settings.autoSafetyEnabled);
    result.texture = RenderRawDetailFusion(inputImage, generatedMask, applySettings);
    result.owned = result.texture != 0;
    return result;
}
