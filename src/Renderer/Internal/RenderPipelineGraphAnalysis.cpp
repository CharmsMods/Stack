#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <cstddef>
#include <unordered_set>

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
    struct ScalarPlan {
        bool immediate = true;
        bool result = false;
        std::vector<const RenderGraphLink*> dependencies;
    };
    struct ScalarFrame {
        std::string key;
        int nodeId = -1;
        std::string socketId;
        ScalarPlan plan;
        std::size_t nextDependency = 0;
        bool allDependenciesScalar = true;
        bool initialized = false;
    };

    const std::string rootKey = MakeNodeSocketKey(nodeId, socketId);
    if (const auto cached = executionContext.scalarSocketCache.find(rootKey);
        cached != executionContext.scalarSocketCache.end()) {
        return cached->second;
    }

    const auto makePlan = [&](int currentNodeId, std::string_view currentSocketId) {
        ScalarPlan plan;
        const auto nodeIt = executionContext.nodes.find(currentNodeId);
        if (nodeIt == executionContext.nodes.end() || !nodeIt->second) {
            return plan;
        }

        if (IsChannelSocketId(currentSocketId) ||
            currentSocketId == EditorNodeGraph::kChannelOutputSocketId ||
            currentSocketId == EditorNodeGraph::kMaskOutputSocketId) {
            plan.result = true;
            return plan;
        }

        const RenderGraphNode& node = *nodeIt->second;
        const auto requireInput = [&](std::string_view inputSocketId) {
            const RenderGraphLink* input =
                executionContext.FindInputLink(node.nodeId, inputSocketId);
            if (!input) {
                return false;
            }
            plan.dependencies.push_back(input);
            return true;
        };
        const auto optionalInput = [&](std::string_view inputSocketId) {
            if (const RenderGraphLink* input =
                    executionContext.FindInputLink(node.nodeId, inputSocketId)) {
                plan.dependencies.push_back(input);
                return true;
            }
            return false;
        };

        switch (node.kind) {
            case RenderGraphNodeKind::RawDetailAutoMask:
            case RenderGraphNodeKind::RawDetailFusion:
            case RenderGraphNodeKind::Layer:
            case RenderGraphNodeKind::Lut:
            case RenderGraphNodeKind::TechnicalImage:
            case RenderGraphNodeKind::Reformat:
                if (currentSocketId == EditorNodeGraph::kImageOutputSocketId &&
                    requireInput(EditorNodeGraph::kImageInputSocketId)) {
                    plan.immediate = false;
                }
                break;
            case RenderGraphNodeKind::Mix:
                if (currentSocketId == EditorNodeGraph::kImageOutputSocketId) {
                    const bool hasImageInput =
                        optionalInput(EditorNodeGraph::kMixInputASocketId) |
                        optionalInput(EditorNodeGraph::kMixInputBSocketId);
                    if (hasImageInput) {
                        plan.immediate = false;
                    }
                }
                break;
            case RenderGraphNodeKind::DataMath:
                if (currentSocketId != EditorNodeGraph::kImageOutputSocketId) {
                    break;
                }
                if (node.dataMathMode == RenderDataMathMode::Average) {
                    plan.result =
                        FindFirstDataMathAverageInput(
                            executionContext,
                            node.nodeId) != nullptr;
                    break;
                }
                if (node.dataMathMode == RenderDataMathMode::ImageAverage) {
                    break;
                }
                for (const DataMathInputLinkInfo& input :
                        CollectDataMathAverageInputs(
                            executionContext,
                            node.nodeId)) {
                    plan.dependencies.push_back(input.link);
                }
                if (!plan.dependencies.empty()) {
                    optionalInput(EditorNodeGraph::kDataMathBaseInputSocketId);
                    optionalInput(EditorNodeGraph::kMaskInputSocketId);
                    plan.immediate = false;
                }
                break;
            default:
                break;
        }
        return plan;
    };

    std::unordered_set<std::string> visiting;
    visiting.reserve(executionContext.nodes.size());
    std::vector<ScalarFrame> pending;
    pending.push_back(
        ScalarFrame{
            rootKey,
            nodeId,
            std::string(socketId)
        });

    while (!pending.empty()) {
        ScalarFrame& frame = pending.back();
        if (!frame.initialized) {
            if (executionContext.scalarSocketCache.count(frame.key) > 0) {
                pending.pop_back();
                continue;
            }
            visiting.insert(frame.key);
            frame.plan = makePlan(frame.nodeId, frame.socketId);
            frame.initialized = true;
            if (frame.plan.immediate) {
                executionContext.scalarSocketCache[frame.key] =
                    frame.plan.result;
                visiting.erase(frame.key);
                pending.pop_back();
                continue;
            }
        }

        if (frame.nextDependency < frame.plan.dependencies.size()) {
            const RenderGraphLink& dependency =
                *frame.plan.dependencies[frame.nextDependency];
            const std::string dependencyKey =
                MakeNodeSocketKey(
                    dependency.fromNodeId,
                    dependency.fromSocketId);
            if (const auto cached =
                    executionContext.scalarSocketCache.find(dependencyKey);
                cached != executionContext.scalarSocketCache.end()) {
                frame.allDependenciesScalar =
                    frame.allDependenciesScalar && cached->second;
                ++frame.nextDependency;
                continue;
            }
            if (visiting.count(dependencyKey) > 0) {
                frame.allDependenciesScalar = false;
                ++frame.nextDependency;
                continue;
            }
            pending.push_back(
                ScalarFrame{
                    dependencyKey,
                    dependency.fromNodeId,
                    dependency.fromSocketId
                });
            continue;
        }

        executionContext.scalarSocketCache[frame.key] =
            frame.allDependenciesScalar;
        visiting.erase(frame.key);
        pending.pop_back();
    }

    const auto resolved =
        executionContext.scalarSocketCache.find(rootKey);
    return resolved != executionContext.scalarSocketCache.end() &&
        resolved->second;
}

} // namespace Stack::Renderer::GraphExecution
