#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <functional>
#include <set>

namespace Stack::Renderer::GraphExecution {

std::vector<DataMathInputLinkInfo> CollectDataMathAverageInputs(
    const GraphExecutionContext& executionContext,
    int nodeId) {
    std::vector<DataMathInputLinkInfo> inputs;
    inputs.reserve(EditorNodeGraph::kMaxDataMathInputCount);
    for (int inputIndex = 0; inputIndex < EditorNodeGraph::kMaxDataMathInputCount; ++inputIndex) {
        const std::string socketId = EditorNodeGraph::DataMathInputSocketId(inputIndex);
        if (const RenderGraphLink* input = executionContext.FindInputLink(nodeId, socketId)) {
            inputs.push_back(DataMathInputLinkInfo{ input, socketId });
        }
    }
    return inputs;
}

const RenderGraphLink* FindFirstDataMathAverageInput(const GraphExecutionContext& executionContext, int nodeId) {
    for (const DataMathInputLinkInfo& input : CollectDataMathAverageInputs(executionContext, nodeId)) {
        return input.link;
    }
    return nullptr;
}

bool IsChannelSocketId(std::string_view socketId) {
    return socketId == "r" || socketId == "g" || socketId == "b" || socketId == "a";
}

bool IsScalarRenderSocket(const GraphExecutionContext& executionContext, int nodeId, std::string_view socketId) {
    std::set<std::string> visiting;
    std::function<bool(int, std::string_view)> isScalar = [&](int currentNodeId, std::string_view currentSocketId) -> bool {
        const std::string key = MakeNodeSocketKey(currentNodeId, currentSocketId);
        if (!visiting.insert(key).second) {
            return false;
        }
        auto finish = [&](bool result) {
            visiting.erase(key);
            return result;
        };

        const auto nodeIt = executionContext.nodes.find(currentNodeId);
        if (nodeIt == executionContext.nodes.end() || !nodeIt->second) {
            return finish(false);
        }

        const RenderGraphNode& node = *nodeIt->second;
        auto inputIsScalar = [&](std::string_view inputSocketId) {
            const RenderGraphLink* input = executionContext.FindInputLink(node.nodeId, inputSocketId);
            return input ? isScalar(input->fromNodeId, input->fromSocketId) : false;
        };

        bool result = false;
        if (IsChannelSocketId(currentSocketId)) {
            result = true;
        } else {
            switch (node.kind) {
                case RenderGraphNodeKind::MaskGenerator:
                case RenderGraphNodeKind::MaskCombine:
                case RenderGraphNodeKind::MaskUtility:
                case RenderGraphNodeKind::CustomMask:
                case RenderGraphNodeKind::ImageToMask:
                    result = currentSocketId == "maskOut";
                    break;
                case RenderGraphNodeKind::RawDetailAutoMask:
                case RenderGraphNodeKind::RawDetailFusion:
                    result = currentSocketId == "maskOut" || (currentSocketId == "imageOut" && inputIsScalar("imageIn"));
                    break;
                case RenderGraphNodeKind::Layer:
                case RenderGraphNodeKind::Lut:
                    result = currentSocketId == "imageOut" && inputIsScalar("imageIn");
                    break;
                case RenderGraphNodeKind::Mix: {
                    const RenderGraphLink* inputA = executionContext.FindInputLink(node.nodeId, "imageA");
                    const RenderGraphLink* inputB = executionContext.FindInputLink(node.nodeId, "imageB");
                    const bool hasA = inputA != nullptr;
                    const bool hasB = inputB != nullptr;
                    result = currentSocketId == "imageOut" &&
                        (hasA || hasB) &&
                        (!hasA || isScalar(inputA->fromNodeId, inputA->fromSocketId)) &&
                        (!hasB || isScalar(inputB->fromNodeId, inputB->fromSocketId));
                    break;
                }
                case RenderGraphNodeKind::DataMath: {
                    if (currentSocketId == "imageOut" && node.dataMathMode == RenderDataMathMode::Average) {
                        return finish(FindFirstDataMathAverageInput(executionContext, node.nodeId) != nullptr);
                    }
                    if (currentSocketId == "imageOut" && node.dataMathMode == RenderDataMathMode::ImageAverage) {
                        return finish(false);
                    }
                    const auto inputs = CollectDataMathAverageInputs(executionContext, node.nodeId);
                    bool allScalar = !inputs.empty();
                    for (const DataMathInputLinkInfo& input : inputs) {
                        allScalar = allScalar && isScalar(input.link->fromNodeId, input.link->fromSocketId);
                        if (!allScalar) {
                            break;
                        }
                    }
                    if (allScalar) {
                        if (const RenderGraphLink* baseInput = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kDataMathBaseInputSocketId)) {
                            allScalar = isScalar(baseInput->fromNodeId, baseInput->fromSocketId);
                        }
                    }
                    if (allScalar) {
                        if (const RenderGraphLink* maskInput = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kMaskInputSocketId)) {
                            allScalar = isScalar(maskInput->fromNodeId, maskInput->fromSocketId);
                        }
                    }
                    result = currentSocketId == "imageOut" && allScalar;
                    break;
                }
                case RenderGraphNodeKind::ChannelSplit:
                    result = IsChannelSocketId(currentSocketId);
                    break;
                default:
                    result = false;
                    break;
            }
        }
        return finish(result);
    };

    return isScalar(nodeId, socketId);
}

} // namespace Stack::Renderer::GraphExecution
