#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <algorithm>
#include <functional>
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

std::vector<CompletedChainInfo> Graph::GetCompletedChains() const {
    if (m_CompletedChainsCacheRevision == m_StructureRevision) {
        return m_CompletedChainsCache;
    }

    std::vector<CompletedChainInfo> chains;

    auto collectChain = [&](int outputNodeId, auto&& collectChainRef) -> CompletedChainInfo {
        CompletedChainInfo chain;
        chain.outputNodeId = outputNodeId;

        const Link* outputInput = FindInputLink(outputNodeId, kImageInputSocketId);
        const Link* linkR = FindInputLink(outputNodeId, "r");
        const Link* linkG = FindInputLink(outputNodeId, "g");
        const Link* linkB = FindInputLink(outputNodeId, "b");
        const Link* linkA = FindInputLink(outputNodeId, "a");

        if (!outputInput && !linkR && !linkG && !linkB && !linkA) {
            return chain;
        }

        std::unordered_set<int> visiting;
        std::unordered_set<int> added;
        std::function<bool(int)> visit = [&](int nodeId) -> bool {
            if (!visiting.insert(nodeId).second) {
                return false;
            }
            const Node* node = FindNode(nodeId);
            if (!node) {
                visiting.erase(nodeId);
                return false;
            }
            if (added.insert(nodeId).second) {
                chain.nodeIds.push_back(nodeId);
            }

            bool valid = false;
            switch (node->kind) {
                case NodeKind::Image:
                case NodeKind::RawSource:
                case NodeKind::ImageGenerator:
                case NodeKind::MaskGenerator:
                case NodeKind::CustomMask:
                    if (chain.sourceNodeId <= 0) {
                        chain.sourceNodeId = nodeId;
                    }
                    valid = true;
                    break;
                case NodeKind::RawDevelop: {
                    const Link* upstream = FindInputLink(nodeId, kRawInputSocketId);
                    valid = upstream ? visit(upstream->fromNodeId) : false;
                    break;
                }
                case NodeKind::RawNeuralDenoise: {
                    const Link* upstream = FindInputLink(nodeId, kRawInputSocketId);
                    valid = upstream ? visit(upstream->fromNodeId) : false;
                    break;
                }
                case NodeKind::Layer: {
                    const Link* upstream = FindInputLink(nodeId, kImageInputSocketId);
                    valid = upstream ? visit(upstream->fromNodeId) : false;
                    break;
                }
                case NodeKind::RawDetailFusion: {
                    const Link* upstream = FindInputLink(nodeId, kImageInputSocketId);
                    valid = upstream ? visit(upstream->fromNodeId) : false;
                    break;
                }
                case NodeKind::HdrMerge: {
                    const Link* input1 = FindInputLink(nodeId, kHdrMergeInput1SocketId);
                    const Link* input2 = FindInputLink(nodeId, kHdrMergeInput2SocketId);
                    const Link* input3 = FindInputLink(nodeId, kHdrMergeInput3SocketId);
                    valid = input1 && input2 && visit(input1->fromNodeId);
                    if (valid && input2) {
                        valid = visit(input2->fromNodeId);
                    }
                    if (valid && input3) {
                        valid = visit(input3->fromNodeId);
                    }
                    break;
                }
                case NodeKind::RawDetailAutoMask: {
                    const Link* upstream = FindInputLink(nodeId, kImageInputSocketId);
                    valid = upstream ? visit(upstream->fromNodeId) : false;
                    break;
                }
                case NodeKind::Mix: {
                    const Link* inputA = FindInputLink(nodeId, kMixInputASocketId);
                    const Link* inputB = FindInputLink(nodeId, kMixInputBSocketId);
                    valid = inputA && inputB &&
                        visit(inputA->fromNodeId) &&
                        visit(inputB->fromNodeId);
                    break;
                }
                case NodeKind::DataMath: {
                    const Link* inputA = FindInputLink(nodeId, kMixInputASocketId);
                    const Link* inputB = FindInputLink(nodeId, kMixInputBSocketId);
                    valid = inputA && visit(inputA->fromNodeId);
                    if (valid && inputB) {
                        valid = visit(inputB->fromNodeId);
                    }
                    break;
                }
                case NodeKind::ChannelSplit: {
                    const Link* upstream = FindInputLink(nodeId, kImageInputSocketId);
                    valid = upstream ? visit(upstream->fromNodeId) : false;
                    break;
                }
                case NodeKind::ChannelCombine: {
                    const Link* upstreamR = FindInputLink(nodeId, "r");
                    const Link* upstreamG = FindInputLink(nodeId, "g");
                    const Link* upstreamB = FindInputLink(nodeId, "b");
                    const Link* upstreamA = FindInputLink(nodeId, "a");

                    bool hasConnection = false;
                    bool allValid = true;

                    if (upstreamR) {
                        hasConnection = true;
                        if (!visit(upstreamR->fromNodeId)) allValid = false;
                    }
                    if (upstreamG) {
                        hasConnection = true;
                        if (!visit(upstreamG->fromNodeId)) allValid = false;
                    }
                    if (upstreamB) {
                        hasConnection = true;
                        if (!visit(upstreamB->fromNodeId)) allValid = false;
                    }
                    if (upstreamA) {
                        hasConnection = true;
                        if (!visit(upstreamA->fromNodeId)) allValid = false;
                    }

                    valid = hasConnection && allValid;
                    break;
                }
                case NodeKind::ImageToMask: {
                    const Link* upstream = FindInputLink(nodeId, kImageToMaskInputSocketId);
                    valid = upstream ? visit(upstream->fromNodeId) : false;
                    break;
                }
                case NodeKind::MaskCombine: {
                    const Link* inputA = FindInputLink(nodeId, kMaskCombineInputASocketId);
                    const Link* inputB = FindInputLink(nodeId, kMaskCombineInputBSocketId);
                    valid = inputA && inputB &&
                        visit(inputA->fromNodeId) &&
                        visit(inputB->fromNodeId);
                    break;
                }
                case NodeKind::MaskUtility: {
                    const Link* upstream = FindInputLink(nodeId, kMaskUtilityInputSocketId);
                    valid = upstream ? visit(upstream->fromNodeId) : false;
                    break;
                }
                case NodeKind::Output:
                case NodeKind::Composite:
                case NodeKind::Scope:
                case NodeKind::Preview:
                    valid = false;
                    break;
            }

            visiting.erase(nodeId);
            return valid;
        };

        if (outputInput) {
            chain.terminalNodeId = outputInput->fromNodeId;
            if (!visit(chain.terminalNodeId)) {
                chain.terminalNodeId = -1;
                chain.sourceNodeId = -1;
                chain.nodeIds.clear();
            }
        } else {
            bool allChannelsValid = true;
            int firstTerminalNodeId = -1;

            if (linkR) {
                if (firstTerminalNodeId == -1) firstTerminalNodeId = linkR->fromNodeId;
                if (!visit(linkR->fromNodeId)) allChannelsValid = false;
            }
            if (linkG) {
                if (firstTerminalNodeId == -1) firstTerminalNodeId = linkG->fromNodeId;
                if (!visit(linkG->fromNodeId)) allChannelsValid = false;
            }
            if (linkB) {
                if (firstTerminalNodeId == -1) firstTerminalNodeId = linkB->fromNodeId;
                if (!visit(linkB->fromNodeId)) allChannelsValid = false;
            }
            if (linkA) {
                if (firstTerminalNodeId == -1) firstTerminalNodeId = linkA->fromNodeId;
                if (!visit(linkA->fromNodeId)) allChannelsValid = false;
            }

            if (allChannelsValid && firstTerminalNodeId != -1) {
                chain.terminalNodeId = firstTerminalNodeId;
            } else {
                chain.terminalNodeId = -1;
                chain.sourceNodeId = -1;
                chain.nodeIds.clear();
            }
        }

        if (chain.terminalNodeId > 0) {
            const int referenceSourceNodeId = ResolveReferenceSourceNodeIdForOutput(outputNodeId);
            if (referenceSourceNodeId > 0) {
                chain.sourceNodeId = referenceSourceNodeId;
            }
        }

        return chain;
    };

    for (int outputNodeId : GetOutputNodeIds()) {
        const Node* outputNode = FindNode(outputNodeId);
        if (!outputNode || !outputNode->outputEnabled) {
            continue;
        }
        CompletedChainInfo chain = collectChain(outputNodeId, collectChain);
        if (chain.outputNodeId > 0 && chain.terminalNodeId > 0 && !chain.nodeIds.empty()) {
            chains.push_back(std::move(chain));
        }
    }
    m_CompletedChainsCache = chains;
    m_CompletedChainsCacheRevision = m_StructureRevision;
    return m_CompletedChainsCache;
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

        for (const Link& link : m_Links) {
            if (!IsRenderLink(link) || link.fromNodeId != current) {
                continue;
            }
            stack.push_back(link.toNodeId);
        }
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
            case NodeKind::RawDevelop:
            case NodeKind::RawNeuralDenoise:
            case NodeKind::RawDetailAutoMask:
            case NodeKind::RawDetailFusion:
            case NodeKind::HdrMerge:
            case NodeKind::Layer:
            case NodeKind::Mix:
            case NodeKind::DataMath:
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
        auto addInputCandidate = [&](const char* socketId) {
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
            case NodeKind::Output:
            case NodeKind::RawDetailAutoMask:
            case NodeKind::RawDetailFusion:
                addInputCandidate(kImageInputSocketId);
                break;
            case NodeKind::HdrMerge:
                addInputCandidate(kHdrMergeInput1SocketId);
                addInputCandidate(kHdrMergeInput2SocketId);
                addInputCandidate(kHdrMergeInput3SocketId);
                break;
            case NodeKind::RawDevelop:
            case NodeKind::RawNeuralDenoise:
                addInputCandidate(kRawInputSocketId);
                break;
            case NodeKind::Mix:
            case NodeKind::DataMath:
                addInputCandidate(kMixInputASocketId);
                addInputCandidate(kMixInputBSocketId);
                break;
            default:
                break;
        }

        return chooseClosestCandidate(candidates, true);
    }

    std::vector<int> candidates;
    for (const Link& link : m_Links) {
        if (!IsRenderLink(link) || link.fromNodeId != nodeId) {
            continue;
        }
        const Node* downstream = FindNode(link.toNodeId);
        if (!downstream || !isMainChainNodeKind(downstream->kind)) {
            continue;
        }
        if (std::find(candidates.begin(), candidates.end(), link.toNodeId) == candidates.end()) {
            candidates.push_back(link.toNodeId);
        }
    }
    return chooseClosestCandidate(candidates, false);
}

} // namespace EditorNodeGraph
