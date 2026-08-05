#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace EditorNodeGraph {

std::vector<int> Graph::GetOutputNodeIds() const {
    std::vector<int> ids;
    ids.reserve(m_Nodes.size());
    for (const Node& node : m_Nodes) {
        if (node.kind == NodeKind::Output) {
            ids.push_back(node.id);
        }
    }
    return ids;
}

std::string Graph::GetOutputConnectionDiagnostic() const {
    (void)GetCompletedChains();
    return m_OutputConnectionDiagnosticCache;
}

std::vector<int> Graph::GetConnectedOutputNodeIds() const {
    std::vector<int> ids;
    for (const CompletedChainInfo& chain : GetCompletedChains()) {
        ids.push_back(chain.outputNodeId);
    }
    return ids;
}

std::vector<int> Graph::GetDownstreamRenderNodeIds(int nodeId) const {
    if (nodeId <= 0) {
        return {};
    }

    std::vector<int> ordered;
    std::unordered_set<int> visited;
    std::vector<int> stack { nodeId };
    while (!stack.empty()) {
        const int current = stack.back();
        stack.pop_back();
        if (!visited.insert(current).second) {
            continue;
        }
        ordered.push_back(current);

        ForEachOutgoingRenderLink(current, [&](const Link& link) {
            stack.push_back(link.toNodeId);
        });
    }
    return ordered;
}

std::vector<int> Graph::GetDownstreamOutputNodeIds(int nodeId) const {
    std::vector<int> outputs;
    for (int downstreamNodeId : GetDownstreamRenderNodeIds(nodeId)) {
        const Node* downstream = FindNode(downstreamNodeId);
        if (downstream && downstream->kind == NodeKind::Output) {
            outputs.push_back(downstreamNodeId);
        }
    }
    return outputs;
}

int Graph::ResolvePreviewOutputNodeId() const {
    const std::vector<int> connected = GetConnectedOutputNodeIds();
    if (connected.size() == 1) {
        return connected.front();
    }
    if (connected.empty()) {
        return m_OutputNodeId;
    }
    return -1;
}

int Graph::FindAdjacentMainChainNodeId(int nodeId, int direction) const {
    const Node* node = FindNode(nodeId);
    if (!node || direction == 0) {
        return -1;
    }

    auto isMainChainNodeKind = [](NodeKind kind) {
        switch (kind) {
            case NodeKind::Image:
            case NodeKind::RawSource:
            case NodeKind::RawDevelopment:
            case NodeKind::RawDecode:
            case NodeKind::RawDevelop:
            case NodeKind::RawNeuralDenoise:
            case NodeKind::RawDetailAutoMask:
            case NodeKind::RawDetailFusion:
            case NodeKind::HdrMerge:
            case NodeKind::Mfsr:
            case NodeKind::MultiFrameDenoise:
            case NodeKind::RawProjectSourceSet:
            case NodeKind::Lut:
            case NodeKind::Layer:
            case NodeKind::TechnicalImage:
            case NodeKind::Mix:
            case NodeKind::DataMath:
            case NodeKind::Compound:
            case NodeKind::ImageGenerator:
            case NodeKind::Output:
                return true;
            default:
                return false;
        }
    };

    auto chooseClosestCandidate = [&](const std::vector<int>& candidates, bool upstream) -> int {
        if (candidates.empty()) {
            return -1;
        }
        if (candidates.size() == 1) {
            return candidates.front();
        }

        int bestNodeId = -1;
        float bestPrimary = std::numeric_limits<float>::max();
        float bestDistanceSq = std::numeric_limits<float>::max();
        for (int candidateNodeId : candidates) {
            const Node* candidate = FindNode(candidateNodeId);
            if (!candidate) {
                continue;
            }
            const float deltaX = upstream
                ? (node->position.x - candidate->position.x)
                : (candidate->position.x - node->position.x);
            const float primary = deltaX >= 0.0f ? deltaX : (1000000.0f + std::abs(deltaX));
            const float dx = candidate->position.x - node->position.x;
            const float dy = candidate->position.y - node->position.y;
            const float distanceSq = dx * dx + dy * dy;
            if (primary < bestPrimary || (std::abs(primary - bestPrimary) < 0.001f && distanceSq < bestDistanceSq)) {
                bestPrimary = primary;
                bestDistanceSq = distanceSq;
                bestNodeId = candidateNodeId;
            }
        }
        return bestNodeId;
    };

    if (direction < 0) {
        std::vector<int> candidates;
        auto addInputCandidate = [&](const std::string& socketId) {
            if (const Link* upstream = FindInputLink(nodeId, socketId)) {
                if (const Node* upstreamNode = FindNode(upstream->fromNodeId)) {
                    if (isMainChainNodeKind(upstreamNode->kind)) {
                        candidates.push_back(upstream->fromNodeId);
                    }
                }
            }
        };

        switch (node->kind) {
            case NodeKind::Layer:
            case NodeKind::Lut:
            case NodeKind::TechnicalImage:
            case NodeKind::Output:
            case NodeKind::RawDetailAutoMask:
            case NodeKind::RawDetailFusion:
                addInputCandidate(kImageInputSocketId);
                if (node->kind == NodeKind::Lut && FindInputLink(nodeId, kImageInputSocketId) == nullptr) {
                    addInputCandidate("r");
                    addInputCandidate("g");
                    addInputCandidate("b");
                    addInputCandidate("a");
                }
                if (node->kind == NodeKind::Output && FindInputLink(nodeId, kImageInputSocketId) == nullptr) {
                    addInputCandidate("r");
                    addInputCandidate("g");
                    addInputCandidate("b");
                    addInputCandidate("a");
                }
                break;
            case NodeKind::HdrMerge:
                addInputCandidate(kHdrMergeInput1SocketId);
                addInputCandidate(kHdrMergeInput2SocketId);
                addInputCandidate(kHdrMergeInput3SocketId);
                break;
            case NodeKind::Mfsr:
                for (int inputIndex = 0; inputIndex < kMaxMfsrInputCount; ++inputIndex) {
                    addInputCandidate(MfsrInputSocketId(inputIndex));
                }
                break;
            case NodeKind::RawDecode:
            case NodeKind::RawDevelop:
            case NodeKind::RawNeuralDenoise:
                addInputCandidate(kRawInputSocketId);
                break;
            case NodeKind::Mix:
                addInputCandidate(kMixInputASocketId);
                addInputCandidate(kMixInputBSocketId);
                break;
            case NodeKind::DataMath:
                for (int inputIndex = 0; inputIndex < kMaxDataMathInputCount; ++inputIndex) {
                    addInputCandidate(DataMathInputSocketId(inputIndex));
                }
                break;
            default:
                break;
        }

        return chooseClosestCandidate(candidates, true);
    }

    std::vector<int> candidates;
    ForEachOutgoingRenderLink(nodeId, [&](const Link& link) {
        const Node* downstream = FindNode(link.toNodeId);
        if (!downstream || !isMainChainNodeKind(downstream->kind)) {
            return;
        }
        if (std::find(candidates.begin(), candidates.end(), link.toNodeId) == candidates.end()) {
            candidates.push_back(link.toNodeId);
        }
    });
    return chooseClosestCandidate(candidates, false);
}

ScenePathInfo AnalyzeScenePath(const Graph& graph, int nodeId) {
    ScenePathInfo state;
    std::unordered_set<int> visited;
    visited.reserve(graph.GetNodes().size());
    std::vector<int> pending{ nodeId };
    while (!pending.empty()) {
        const int currentNodeId = pending.back();
        pending.pop_back();
        if (!visited.insert(currentNodeId).second) {
            continue;
        }
        const Node* current = graph.FindNode(currentNodeId);
        if (!current) {
            continue;
        }
        const Node& node = *current;

        const auto addInput = [&](const std::string& socketId) {
            bool found = false;
            graph.ForEachIncomingLink(
                currentNodeId,
                [&](const Link& input) {
                    if (!found &&
                        input.toSocketId == socketId &&
                        graph.GetLinkRole(input) != LinkRole::Scope) {
                        pending.push_back(input.fromNodeId);
                        found = true;
                    }
                });
            return found;
        };

        switch (node.kind) {
            case NodeKind::RawSource:
            case NodeKind::RawProjectFrame:
            case NodeKind::RawProjectSourceSet:
                state.sceneReferred = true;
                break;
            case NodeKind::MultiFrameDenoise:
                state.sceneReferred =
                    !node.multiFrameDenoise.internalViewTransformEnabled;
                state.hasViewTransform =
                    node.multiFrameDenoise.internalViewTransformEnabled;
                break;
            case NodeKind::RawDevelopment:
                state.sceneReferred = true;
                if (Stack::RawRecipe::IsViewTransformEnabled(
                        node.rawDevelopment.recipe)) {
                    state.hasViewTransform = true;
                }
                break;
            case NodeKind::RawDecode:
            case NodeKind::RawDevelop:
                state.sceneReferred = true;
                addInput(kRawInputSocketId);
                break;
            case NodeKind::RawDetailFusion:
                state.sceneReferred = true;
                addInput(kImageInputSocketId);
                break;
            case NodeKind::HdrMerge:
                state.sceneReferred = true;
                addInput(kHdrMergeInput1SocketId);
                addInput(kHdrMergeInput2SocketId);
                addInput(kHdrMergeInput3SocketId);
                break;
            case NodeKind::Mfsr:
                state.sceneReferred = true;
                for (int inputIndex = 0;
                     inputIndex < kMaxMfsrInputCount;
                     ++inputIndex) {
                    addInput(MfsrInputSocketId(inputIndex));
                }
                break;
            case NodeKind::RawDetailAutoMask:
                state.sceneReferred = true;
                addInput(kImageInputSocketId);
                break;
            case NodeKind::RawNeuralDenoise:
                addInput(kRawInputSocketId);
                break;
            case NodeKind::Layer:
                if (node.layerType == LayerType::ViewTransform) {
                    state.hasViewTransform = true;
                }
                addInput(kImageInputSocketId);
                break;
            case NodeKind::TechnicalImage:
            case NodeKind::Reformat:
                addInput(kImageInputSocketId);
                break;
            case NodeKind::Compound: {
                std::vector<std::string> dependencies;
                std::string error;
                if (graph.ResolveCompoundOutputInputDependencies(
                        node.id,
                        graph.DefaultOutputSocket(node),
                        dependencies,
                        &error)) {
                    for (const std::string& dependency : dependencies) {
                        addInput(dependency);
                    }
                } else {
                    for (const SocketDefinition& socket :
                         graph.GetSockets(node, false)) {
                        if (socket.direction == SocketDirection::Input) {
                            addInput(socket.id);
                        }
                    }
                }
                break;
            }
            case NodeKind::Lut:
            case NodeKind::Output:
                if (!addInput(kImageInputSocketId)) {
                    addInput("r");
                    addInput("g");
                    addInput("b");
                    addInput("a");
                }
                break;
            case NodeKind::Mix:
                addInput(kMixInputASocketId);
                addInput(kMixInputBSocketId);
                break;
            case NodeKind::DataMath:
                for (int inputIndex = 0;
                     inputIndex < kMaxDataMathInputCount;
                     ++inputIndex) {
                    addInput(DataMathInputSocketId(inputIndex));
                }
                addInput(kDataMathBaseInputSocketId);
                break;
            case NodeKind::ChannelSplit:
                addInput(kImageInputSocketId);
                break;
            case NodeKind::ChannelCombine:
                addInput("r");
                addInput("g");
                addInput("b");
                addInput("a");
                break;
            case NodeKind::ConstantChannel:
                addInput(kMatchExtentInputSocketId);
                break;
            case NodeKind::FrequencyFilter:
            case NodeKind::FrequencyFft:
                addInput(kChannelInputSocketId);
                break;
            case NodeKind::FrequencyIfft:
            case NodeKind::SpectrumView:
            case NodeKind::SpectrumSeparate:
            case NodeKind::SpectrumAnalyzer:
                addInput(kSpectrumInputSocketId);
                break;
            case NodeKind::ApplyFrequencyResponse:
                addInput(kSpectrumInputSocketId);
                break;
            case NodeKind::CombineSpectra:
                addInput(kSpectrumInputASocketId);
                addInput(kSpectrumInputBSocketId);
                break;
            case NodeKind::SpectrumRecombine:
                addInput(kSpectrumMagnitudeInputSocketId);
                addInput(kSpectrumPhaseInputSocketId);
                break;
            case NodeKind::SpectrumMath:
                addInput(kMixInputASocketId);
                addInput(kMixInputBSocketId);
                break;
            case NodeKind::MagnitudePhase:
                if (node.magnitudePhaseMode == MagnitudePhaseMode::Recombine) {
                    addInput("magnitude");
                    addInput("phase");
                } else {
                    addInput(kImageInputSocketId);
                }
                break;
            case NodeKind::ImageToMask:
                addInput(kImageToMaskInputSocketId);
                break;
            case NodeKind::MaskCombine:
                addInput(kMaskCombineInputASocketId);
                addInput(kMaskCombineInputBSocketId);
                break;
            case NodeKind::MaskUtility:
                addInput(kMaskUtilityInputSocketId);
                break;
            case NodeKind::FieldMean:
                addInput(kReductionFieldInputSocketId);
                break;
            case NodeKind::Scope:
                addInput(kScopeInputSocketId);
                break;
            case NodeKind::Preview:
                addInput(kPreviewInputSocketId);
                break;
            case NodeKind::Image:
            case NodeKind::ImageGenerator:
            case NodeKind::MaskGenerator:
            case NodeKind::FrequencyResponse:
            case NodeKind::FrequencyMask:
            case NodeKind::CustomMask:
            case NodeKind::Value:
            case NodeKind::Composite:
                break;
        }
    }
    return state;
}

} // namespace EditorNodeGraph
