#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace Stack::Renderer::GraphExecution;

int RenderPipeline::FindRawDetailAutoMaskSource(
    const GraphExecutionContext& executionContext,
    int nodeId,
    std::string_view socketId) {
    struct PendingSocket {
        int nodeId = -1;
        std::string socketId;
    };
    std::vector<PendingSocket> pending{
        PendingSocket{ nodeId, std::string(socketId) }
    };
    std::unordered_set<std::string> visited;
    visited.reserve(executionContext.nodes.size());

    while (!pending.empty()) {
        PendingSocket current = std::move(pending.back());
        pending.pop_back();
        if (!visited.insert(
                MakeNodeSocketKey(
                    current.nodeId,
                    current.socketId)).second) {
            continue;
        }

        const auto nodeIt = executionContext.nodes.find(current.nodeId);
        if (nodeIt == executionContext.nodes.end() || !nodeIt->second) {
            continue;
        }

        const RenderGraphNode& node = *nodeIt->second;
        if (node.kind == RenderGraphNodeKind::RawDetailAutoMask &&
            current.socketId == EditorNodeGraph::kMaskOutputSocketId) {
            return node.nodeId;
        }
        if (node.kind == RenderGraphNodeKind::MaskUtility &&
            current.socketId == EditorNodeGraph::kMaskOutputSocketId) {
            if (const RenderGraphLink* input =
                    executionContext.FindInputLink(
                        node.nodeId,
                        EditorNodeGraph::kMaskUtilityInputSocketId)) {
                pending.push_back(
                    PendingSocket{
                        input->fromNodeId,
                        input->fromSocketId
                    });
            }
            continue;
        }
        if (node.kind == RenderGraphNodeKind::MaskCombine &&
            current.socketId == EditorNodeGraph::kMaskOutputSocketId) {
            if (const RenderGraphLink* inputB =
                    executionContext.FindInputLink(
                        node.nodeId,
                        EditorNodeGraph::kMaskCombineInputBSocketId)) {
                pending.push_back(
                    PendingSocket{
                        inputB->fromNodeId,
                        inputB->fromSocketId
                    });
            }
            if (const RenderGraphLink* inputA =
                    executionContext.FindInputLink(
                        node.nodeId,
                        EditorNodeGraph::kMaskCombineInputASocketId)) {
                pending.push_back(
                    PendingSocket{
                        inputA->fromNodeId,
                        inputA->fromSocketId
                    });
            }
        }
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
    const int inputWidth = m_Width;
    const int inputHeight = m_Height;

    const RenderGraphLink* maskLink = executionContext.FindInputLink(node.nodeId, "maskIn");
    if (socketId == "maskOut") {
        const unsigned int manualMask = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
        m_Width = inputWidth;
        m_Height = inputHeight;
        const bool debugPreview = executionContext.graph.autoGainMaskPreview &&
            executionContext.graph.outputNodeId == node.nodeId &&
            executionContext.graph.outputSocketId == "maskOut";
        result.texture = RenderRawDetailAutoMask(inputImage, node, manualMask, debugPreview);
        result.owned = result.texture != 0;
        return result;
    }

    const unsigned int generatedMask = evalMask(node.nodeId, "maskOut");
    m_Width = inputWidth;
    m_Height = inputHeight;
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
