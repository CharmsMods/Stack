#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace EditorNodeGraph {
namespace {

bool IsChannelSocketId(const std::string& socketId) {
    return socketId == "r" || socketId == "g" || socketId == "b" || socketId == "a";
}

bool ContainsNodeId(const std::vector<Node>& nodes, int id) {
    return std::any_of(nodes.begin(), nodes.end(), [id](const Node& node) {
        return node.id == id;
    });
}

bool IsSourceLikeNode(const Node& node) {
    return node.kind == NodeKind::Image ||
        node.kind == NodeKind::RawSource ||
        node.kind == NodeKind::ImageGenerator ||
        node.kind == NodeKind::MaskGenerator ||
        node.kind == NodeKind::CustomMask;
}

bool IsEndpointLikeNode(const Node& node) {
    return node.kind == NodeKind::Output ||
        node.kind == NodeKind::Preview ||
        node.kind == NodeKind::Scope;
}

int LayoutKindPriority(const Node& node) {
    switch (node.kind) {
        case NodeKind::Image:
        case NodeKind::RawSource:
        case NodeKind::ImageGenerator:
            return 0;
        case NodeKind::MaskGenerator:
        case NodeKind::CustomMask:
            return 1;
        case NodeKind::RawNeuralDenoise:
        case NodeKind::RawDecode:
        case NodeKind::RawDevelop:
            return 2;
        case NodeKind::Layer:
        case NodeKind::Lut:
        case NodeKind::RawDetailAutoMask:
        case NodeKind::RawDetailFusion:
        case NodeKind::ImageToMask:
        case NodeKind::MaskUtility:
        case NodeKind::Mix:
        case NodeKind::DataMath:
        case NodeKind::ChannelSplit:
        case NodeKind::ChannelCombine:
            return 3;
        case NodeKind::Preview:
        case NodeKind::Scope:
            return 4;
        case NodeKind::Output:
            return 5;
        case NodeKind::Composite:
            return 6;
        default:
            return 7;
    }
}

bool NodeLayoutLess(const Node& a, const Node& b) {
    const int priorityA = LayoutKindPriority(a);
    const int priorityB = LayoutKindPriority(b);
    if (priorityA != priorityB) {
        return priorityA < priorityB;
    }
    if (a.title != b.title) {
        return a.title < b.title;
    }
    return a.id < b.id;
}

} // namespace

void Graph::AutoLayout() {
    EnsureOutputNode();
    if (m_Nodes.empty()) {
        return;
    }

    const float x0 = 40.0f;
    const float y0 = 130.0f;
    const float columnSpacing = 285.0f;
    const float rowSpacing = 168.0f;

    std::unordered_map<int, std::vector<int>> incoming;
    std::unordered_map<int, std::vector<int>> outgoing;
    std::unordered_map<int, int> indegree;
    std::unordered_map<int, int> layerByNodeId;
    std::unordered_map<int, float> rowOrderByNodeId;
    std::unordered_map<int, std::size_t> topoIndexByNodeId;
    std::unordered_map<int, const Node*> nodeById;
    std::vector<int> allNodeIds;
    allNodeIds.reserve(m_Nodes.size());

    for (const Node& node : m_Nodes) {
        nodeById[node.id] = &node;
        allNodeIds.push_back(node.id);
        indegree[node.id] = 0;
    }

    auto nodeIdLess = [&nodeById](const int lhs, const int rhs) {
        const Node* lhsNode = nodeById.count(lhs) ? nodeById.at(lhs) : nullptr;
        const Node* rhsNode = nodeById.count(rhs) ? nodeById.at(rhs) : nullptr;
        if (!lhsNode || !rhsNode) {
            return lhs < rhs;
        }
        return NodeLayoutLess(*lhsNode, *rhsNode);
    };

    for (const Link& link : m_Links) {
        if (!nodeById.count(link.fromNodeId) || !nodeById.count(link.toNodeId)) {
            continue;
        }
        outgoing[link.fromNodeId].push_back(link.toNodeId);
        incoming[link.toNodeId].push_back(link.fromNodeId);
        ++indegree[link.toNodeId];
    }

    for (auto& entry : outgoing) {
        std::sort(entry.second.begin(), entry.second.end(), nodeIdLess);
        entry.second.erase(std::unique(entry.second.begin(), entry.second.end()), entry.second.end());
    }
    for (auto& entry : incoming) {
        std::sort(entry.second.begin(), entry.second.end(), nodeIdLess);
        entry.second.erase(std::unique(entry.second.begin(), entry.second.end()), entry.second.end());
    }

    std::vector<int> ready;
    ready.reserve(allNodeIds.size());
    for (const int nodeId : allNodeIds) {
        if (indegree[nodeId] == 0) {
            ready.push_back(nodeId);
        }
    }
    std::sort(ready.begin(), ready.end(), nodeIdLess);

    std::vector<int> topoOrder;
    topoOrder.reserve(allNodeIds.size());
    while (!ready.empty()) {
        const int nodeId = ready.front();
        ready.erase(ready.begin());
        topoOrder.push_back(nodeId);
        for (const int downstreamId : outgoing[nodeId]) {
            auto indegreeIt = indegree.find(downstreamId);
            if (indegreeIt == indegree.end()) {
                continue;
            }
            indegreeIt->second = std::max(0, indegreeIt->second - 1);
            if (indegreeIt->second == 0) {
                ready.push_back(downstreamId);
                std::sort(ready.begin(), ready.end(), nodeIdLess);
            }
        }
    }

    for (const int nodeId : allNodeIds) {
        if (std::find(topoOrder.begin(), topoOrder.end(), nodeId) == topoOrder.end()) {
            topoOrder.push_back(nodeId);
        }
    }

    for (std::size_t index = 0; index < topoOrder.size(); ++index) {
        topoIndexByNodeId[topoOrder[index]] = index;
    }

    int maxLayer = 0;
    for (const int nodeId : topoOrder) {
        const Node* node = nodeById[nodeId];
        int layer = 0;
        auto incomingIt = incoming.find(nodeId);
        if (incomingIt != incoming.end()) {
            for (const int upstreamId : incomingIt->second) {
                layer = std::max(layer, layerByNodeId[upstreamId] + 1);
            }
        }
        if (IsSourceLikeNode(*node)) {
            layer = 0;
        }
        if (IsEndpointLikeNode(*node)) {
            layer = std::max(layer, 1);
        }
        layerByNodeId[nodeId] = layer;
        maxLayer = std::max(maxLayer, layer);
    }

    for (const Node& node : m_Nodes) {
        if (node.kind != NodeKind::MaskGenerator && node.kind != NodeKind::CustomMask) {
            continue;
        }
        auto outgoingIt = outgoing.find(node.id);
        if (outgoingIt == outgoing.end() || outgoingIt->second.empty()) {
            continue;
        }
        int minConsumerLayer = std::numeric_limits<int>::max();
        for (const int consumerId : outgoingIt->second) {
            auto layerIt = layerByNodeId.find(consumerId);
            if (layerIt != layerByNodeId.end()) {
                minConsumerLayer = std::min(minConsumerLayer, layerIt->second);
            }
        }
        if (minConsumerLayer != std::numeric_limits<int>::max()) {
            layerByNodeId[node.id] = std::max(0, minConsumerLayer - 1);
        }
    }

    std::unordered_map<int, std::vector<int>> layerBuckets;
    for (const int nodeId : topoOrder) {
        layerBuckets[layerByNodeId[nodeId]].push_back(nodeId);
        maxLayer = std::max(maxLayer, layerByNodeId[nodeId]);
    }

    auto barycentricSort = [&](std::vector<int>& bucket) {
        std::stable_sort(bucket.begin(), bucket.end(), [&](const int lhs, const int rhs) {
            auto barycenterFor = [&](const int nodeId) {
                const auto incomingIt = incoming.find(nodeId);
                if (incomingIt == incoming.end() || incomingIt->second.empty()) {
                    return std::pair<float, bool>{ static_cast<float>(topoIndexByNodeId[nodeId]), false };
                }
                float sum = 0.0f;
                int count = 0;
                for (const int upstreamId : incomingIt->second) {
                    auto orderIt = rowOrderByNodeId.find(upstreamId);
                    if (orderIt != rowOrderByNodeId.end()) {
                        sum += orderIt->second;
                        ++count;
                    }
                }
                if (count == 0) {
                    return std::pair<float, bool>{ static_cast<float>(topoIndexByNodeId[nodeId]), false };
                }
                return std::pair<float, bool>{ sum / static_cast<float>(count), true };
            };

            const auto lhsBary = barycenterFor(lhs);
            const auto rhsBary = barycenterFor(rhs);
            if (lhsBary.second != rhsBary.second) {
                return lhsBary.second > rhsBary.second;
            }
            if (std::abs(lhsBary.first - rhsBary.first) > 0.001f) {
                return lhsBary.first < rhsBary.first;
            }
            return nodeIdLess(lhs, rhs);
        });
    };

    for (int layer = 0; layer <= maxLayer; ++layer) {
        std::vector<int>& bucket = layerBuckets[layer];
        if (bucket.empty()) {
            continue;
        }
        barycentricSort(bucket);
        for (std::size_t row = 0; row < bucket.size(); ++row) {
            rowOrderByNodeId[bucket[row]] = static_cast<float>(row);
        }
    }

    for (int layer = 0; layer <= maxLayer; ++layer) {
        const std::vector<int>& bucket = layerBuckets[layer];
        for (std::size_t row = 0; row < bucket.size(); ++row) {
            if (Node* node = FindNode(bucket[row])) {
                node->position = {
                    x0 + static_cast<float>(layer) * columnSpacing,
                    y0 + static_cast<float>(row) * rowSpacing
                };
            }
        }
    }
}

ValidationResult Graph::Validate() const {
    ValidationResult result;
    std::set<int> ids;
    int outputCount = 0;
    std::set<int> outgoingImages;

    for (const Node& node : m_Nodes) {
        if (node.id <= 0 || !ids.insert(node.id).second) {
            result.valid = false;
            result.messages.push_back("Duplicate or invalid node id.");
        }
        if (node.kind == NodeKind::Output) {
            ++outputCount;
        }
        if (node.kind == NodeKind::Layer && node.layerIndex < 0) {
            result.valid = false;
            result.messages.push_back("Layer node has no layer reference.");
        }
    }

    if (outputCount < 1) {
        result.messages.push_back(m_Nodes.empty()
            ? "Graph is empty. Press Tab or drop an image to begin."
            : "Add an output node or drop an image to start rendering.");
    }

    std::set<std::tuple<int, std::string, int, std::string>> seenLinks;
    for (const Link& link : m_Links) {
        if (!ContainsNodeId(m_Nodes, link.fromNodeId) || !ContainsNodeId(m_Nodes, link.toNodeId)) {
            result.valid = false;
            result.messages.push_back("Link references a missing node.");
        }
        if (!seenLinks.insert({ link.fromNodeId, link.fromSocketId, link.toNodeId, link.toSocketId }).second) {
            result.valid = false;
            result.messages.push_back("Duplicate graph socket link.");
        }

        const Node* from = FindNode(link.fromNodeId);
        const Node* to = FindNode(link.toNodeId);
        SocketDefinition fromSocket;
        SocketDefinition toSocket;
        if (!from || !to) {
            continue;
        }
        if (!FindSocket(link.fromNodeId, link.fromSocketId, &fromSocket) ||
            !FindSocket(link.toNodeId, link.toSocketId, &toSocket)) {
            result.valid = false;
            result.messages.push_back("Link references a missing socket.");
            continue;
        }
        if (fromSocket.direction != SocketDirection::Output || toSocket.direction != SocketDirection::Input) {
            result.valid = false;
            result.messages.push_back("Link uses an invalid socket direction.");
        }
        if (from->kind == NodeKind::Image || from->kind == NodeKind::RawDecode || from->kind == NodeKind::RawDevelop) {
            outgoingImages.insert(from->id);
        }

        if (GetLinkRole(link) == LinkRole::Scope) {
            if (to->kind == NodeKind::Scope) {
                const bool validScopeInput = link.toSocketId == kScopeInputSocketId &&
                    (fromSocket.type == SocketType::Image ||
                     (fromSocket.type == SocketType::Mask && IsScalarSocketStream(link.fromNodeId, link.fromSocketId)));
                if (!validScopeInput) {
                    result.valid = false;
                    result.messages.push_back("Scope nodes can only analyze image or scalar outputs.");
                }
            } else if (to->kind == NodeKind::Preview) {
                const bool validPreview = link.toSocketId == kPreviewInputSocketId &&
                    (fromSocket.type == SocketType::Image ||
                     (fromSocket.type == SocketType::Mask && IsScalarSocketStream(link.fromNodeId, link.fromSocketId)));
                if (!validPreview) {
                    result.valid = false;
                    result.messages.push_back("Preview nodes can only inspect image or scalar outputs.");
                }
            } else {
                result.valid = false;
                result.messages.push_back("Invalid analysis link.");
            }
        } else if (fromSocket.type == SocketType::Raw || toSocket.type == SocketType::Raw) {
            const bool validRawLink =
                fromSocket.type == SocketType::Raw &&
                toSocket.type == SocketType::Raw &&
                (from->kind == NodeKind::RawSource || from->kind == NodeKind::RawNeuralDenoise) &&
                link.fromSocketId == kRawOutputSocketId &&
                (to->kind == NodeKind::RawNeuralDenoise || to->kind == NodeKind::RawDecode || to->kind == NodeKind::RawDevelop) &&
                link.toSocketId == kRawInputSocketId;
            if (!validRawLink) {
                result.valid = false;
                result.messages.push_back("Invalid RAW link.");
            }
        } else if (fromSocket.type == SocketType::Mask || toSocket.type == SocketType::Mask) {
            const bool fromIsScalarStream = IsScalarSocketStream(link.fromNodeId, link.fromSocketId);
            const bool validScalarSource =
                ((from->kind == NodeKind::MaskGenerator ||
                  from->kind == NodeKind::MaskCombine ||
                  from->kind == NodeKind::MaskUtility ||
                  from->kind == NodeKind::CustomMask ||
                  from->kind == NodeKind::ImageToMask ||
                  from->kind == NodeKind::RawDetailAutoMask ||
                  from->kind == NodeKind::RawDetailFusion) && link.fromSocketId == kMaskOutputSocketId) ||
                (from->kind == NodeKind::ChannelSplit && IsChannelSocketId(link.fromSocketId)) ||
                fromIsScalarStream;

            const bool isScalarToScalar = fromSocket.type == SocketType::Mask && toSocket.type == SocketType::Mask;
            const bool isScalarImageToScalar = fromSocket.type == SocketType::Image && toSocket.type == SocketType::Mask && fromIsScalarStream;
            const bool isScalarToImage = fromSocket.type == SocketType::Mask && toSocket.type == SocketType::Image;

            if (isScalarToScalar || isScalarImageToScalar) {
                const bool validScalarTarget =
                    (to->kind == NodeKind::RawDevelop && link.toSocketId == kMaskInputSocketId) ||
                    (to->kind == NodeKind::Layer && link.toSocketId == kMaskInputSocketId) ||
                    (to->kind == NodeKind::Lut && link.toSocketId == kMaskInputSocketId) ||
                    (to->kind == NodeKind::RawDetailFusion && link.toSocketId == kMaskInputSocketId) ||
                    (to->kind == NodeKind::Mix && link.toSocketId == kMixFactorSocketId) ||
                    (to->kind == NodeKind::MaskCombine &&
                        (link.toSocketId == kMaskCombineInputASocketId || link.toSocketId == kMaskCombineInputBSocketId)) ||
                    (to->kind == NodeKind::MaskUtility && link.toSocketId == kMaskUtilityInputSocketId) ||
                    (to->kind == NodeKind::Lut && IsChannelSocketId(link.toSocketId)) ||
                    (to->kind == NodeKind::ChannelCombine && IsChannelSocketId(link.toSocketId)) ||
                    (to->kind == NodeKind::Output && IsChannelSocketId(link.toSocketId));
                if (!validScalarSource || !validScalarTarget) {
                    result.valid = false;
                    result.messages.push_back("Invalid scalar link.");
                }
            } else if (isScalarToImage) {
                const bool validImageTarget =
                    (to->kind == NodeKind::Layer && link.toSocketId == kImageInputSocketId) ||
                    (to->kind == NodeKind::Lut && link.toSocketId == kImageInputSocketId) ||
                    (to->kind == NodeKind::RawDetailAutoMask && link.toSocketId == kImageInputSocketId) ||
                    (to->kind == NodeKind::RawDetailFusion && link.toSocketId == kImageInputSocketId) ||
                    (to->kind == NodeKind::Mix && (link.toSocketId == kMixInputASocketId || link.toSocketId == kMixInputBSocketId)) ||
                    (to->kind == NodeKind::Output && link.toSocketId == kImageInputSocketId) ||
                    (to->kind == NodeKind::ImageToMask && link.toSocketId == kImageToMaskInputSocketId) ||
                    (to->kind == NodeKind::ChannelSplit && link.toSocketId == kImageInputSocketId) ||
                    (to->kind == NodeKind::DataMath && (link.toSocketId == kMixInputASocketId || link.toSocketId == kMixInputBSocketId));
                if (!validScalarSource || !validImageTarget) {
                    result.valid = false;
                    result.messages.push_back("Invalid scalar-to-image link.");
                }
            } else {
                result.valid = false;
                result.messages.push_back("Cannot connect a full image output directly to a scalar input.");
            }
        } else {
            if (fromSocket.type != SocketType::Image || toSocket.type != SocketType::Image) {
                result.valid = false;
                result.messages.push_back("Render-chain links must connect image sockets.");
            }
            if (!IsRenderChainNode(*from) || !IsRenderChainNode(*to) || to->kind == NodeKind::Image || to->kind == NodeKind::RawSource || to->kind == NodeKind::RawNeuralDenoise || to->kind == NodeKind::RawDecode || to->kind == NodeKind::RawDevelop || from->kind == NodeKind::Output) {
                result.valid = false;
                result.messages.push_back("Invalid render-chain link.");
            }
        }
    }

    result.outputConnected = IsOutputConnected();
    if (!result.outputConnected) {
        result.messages.push_back("No completed output chains are connected.");
    }

    std::unordered_set<int> visiting;
    std::unordered_set<int> visited;
    std::function<bool(int)> hasCycle = [&](int nodeId) {
        if (visiting.count(nodeId)) return true;
        if (visited.count(nodeId)) return false;
        visiting.insert(nodeId);
        for (const Link& link : m_Links) {
            if (link.fromNodeId == nodeId && IsRenderLink(link)) {
                if (hasCycle(link.toNodeId)) return true;
            }
        }
        visiting.erase(nodeId);
        visited.insert(nodeId);
        return false;
    };
    for (const Node& node : m_Nodes) {
        if (IsRenderChainNode(node) && hasCycle(node.id)) {
            result.valid = false;
            result.messages.push_back("Render chain contains a cycle.");
            break;
        }
    }

    return result;
}

std::vector<int> Graph::GetRenderLayerNodePath() const {
    return GetRenderLayerNodePath(ResolvePreviewOutputNodeId());
}

std::vector<int> Graph::GetRenderLayerNodePath(int outputNodeId) const {
    std::vector<int> reversePath;
    if (outputNodeId <= 0) {
        return {};
    }

    const Link* input = FindInputLink(outputNodeId, kImageInputSocketId);
    std::unordered_set<int> visited;
    while (input) {
        const Node* from = FindNode(input->fromNodeId);
        if (!from || visited.count(from->id)) {
            return {};
        }
        visited.insert(from->id);
        if (from->kind == NodeKind::Image || from->kind == NodeKind::RawDecode || from->kind == NodeKind::RawDevelop) {
            std::reverse(reversePath.begin(), reversePath.end());
            return reversePath;
        }
        if (from->kind != NodeKind::Layer && from->kind != NodeKind::RawDetailFusion && from->kind != NodeKind::HdrMerge) {
            return {};
        }
        if (from->kind == NodeKind::Layer) {
            reversePath.push_back(from->id);
        }
        input = FindInputLink(from->id, from->kind == NodeKind::HdrMerge ? kHdrMergeInput1SocketId : kImageInputSocketId);
    }

    return {};
}

std::vector<int> Graph::GetRenderLayerIndexPath() const {
    return GetRenderLayerIndexPath(ResolvePreviewOutputNodeId());
}

std::vector<int> Graph::GetRenderLayerIndexPath(int outputNodeId) const {
    std::vector<int> indices;
    for (int nodeId : GetRenderLayerNodePath(outputNodeId)) {
        const Node* node = FindNode(nodeId);
        if (node && node->kind == NodeKind::Layer && node->layerIndex >= 0) {
            indices.push_back(node->layerIndex);
        }
    }
    return indices;
}

LinkRole Graph::GetLinkRole(const Link& link) const {
    const Node* to = FindNode(link.toNodeId);
    const bool analysisTarget = to &&
        ((to->kind == NodeKind::Scope && link.toSocketId == kScopeInputSocketId) ||
         (to->kind == NodeKind::Preview && link.toSocketId == kPreviewInputSocketId));
    return analysisTarget ? LinkRole::Scope : LinkRole::Render;
}

bool Graph::IsRenderChainNode(const Node& node) const {
    return node.kind == NodeKind::Image ||
        node.kind == NodeKind::RawSource ||
        node.kind == NodeKind::RawNeuralDenoise ||
        node.kind == NodeKind::RawDecode ||
        node.kind == NodeKind::RawDevelop ||
        node.kind == NodeKind::RawDetailAutoMask ||
        node.kind == NodeKind::RawDetailFusion ||
        node.kind == NodeKind::HdrMerge ||
        node.kind == NodeKind::Lut ||
        node.kind == NodeKind::ImageGenerator ||
        node.kind == NodeKind::Layer ||
        node.kind == NodeKind::Output ||
        node.kind == NodeKind::Mix ||
        node.kind == NodeKind::DataMath ||
        node.kind == NodeKind::ImageToMask ||
        node.kind == NodeKind::CustomMask ||
        node.kind == NodeKind::ChannelSplit ||
        node.kind == NodeKind::ChannelCombine;
}

bool Graph::IsRenderLink(const Link& link) const {
    if (GetLinkRole(link) != LinkRole::Render) {
        return false;
    }
    const Node* from = FindNode(link.fromNodeId);
    const Node* to = FindNode(link.toNodeId);
    if (!from || !to) {
        return false;
    }

    SocketDefinition fromSocket;
    SocketDefinition toSocket;
    if (!FindSocket(link.fromNodeId, link.fromSocketId, &fromSocket) ||
        !FindSocket(link.toNodeId, link.toSocketId, &toSocket)) {
        return false;
    }

    if (fromSocket.type == SocketType::Raw && toSocket.type == SocketType::Raw) {
        return true;
    }
    if (fromSocket.type == SocketType::Mask && toSocket.type == SocketType::Mask) {
        return true;
    }
    if (fromSocket.type == SocketType::Mask && toSocket.type == SocketType::Image) {
        return true;
    }
    if (fromSocket.type == SocketType::Image &&
        toSocket.type == SocketType::Mask &&
        IsScalarSocketStream(link.fromNodeId, link.fromSocketId)) {
        return true;
    }
    if (fromSocket.type == SocketType::Image && toSocket.type == SocketType::Image) {
        return true;
    }

    return false;
}

const Link* Graph::FindInputLink(int nodeId, const std::string& socketId) const {
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [this, nodeId, &socketId](const Link& link) {
        return link.toNodeId == nodeId && link.toSocketId == socketId && IsRenderLink(link);
    });
    return it != m_Links.end() ? &(*it) : nullptr;
}

const Link* Graph::FindAnyInputLink(int nodeId, const std::string& socketId) const {
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [nodeId, &socketId](const Link& link) {
        return link.toNodeId == nodeId && link.toSocketId == socketId;
    });
    return it != m_Links.end() ? &(*it) : nullptr;
}

const Link* Graph::FindOutputLink(int nodeId, const std::string& socketId) const {
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [this, nodeId, &socketId](const Link& link) {
        return link.fromNodeId == nodeId && link.fromSocketId == socketId && IsRenderLink(link);
    });
    return it != m_Links.end() ? &(*it) : nullptr;
}

const Link* Graph::FindScopeInputLink(int nodeId) const {
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [this, nodeId](const Link& link) {
        return link.toNodeId == nodeId && GetLinkRole(link) == LinkRole::Scope;
    });
    return it != m_Links.end() ? &(*it) : nullptr;
}

} // namespace EditorNodeGraph
