#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/Model/EditorNodeGraphConnectionRules.h"
#include "Editor/NodeGraph/Model/EditorNodeGraphLookupCache.h"

#include <algorithm>
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

bool IsUniformOrResourceSocketType(SocketType type) {
    switch (type) {
        case SocketType::Boolean:
        case SocketType::Integer:
        case SocketType::Scalar:
        case SocketType::Vector2:
        case SocketType::Vector3:
        case SocketType::Vector4:
        case SocketType::Matrix3:
        case SocketType::Matrix4:
        case SocketType::Curve:
        case SocketType::Coordinate:
        case SocketType::Histogram:
        case SocketType::Statistics:
        case SocketType::Metadata:
        case SocketType::Handle:
        case SocketType::Value:
            return true;
        default:
            return false;
    }
}

bool IsSpecializedFrequencySocketType(SocketType type) {
    return type == SocketType::Spectrum ||
        type == SocketType::FrequencyResponse ||
        type == SocketType::SpectrumMagnitude ||
        type == SocketType::SpectrumPhase;
}


bool IsDataMathImageInputSocketId(const std::string& socketId) {
    return EditorNodeGraph::IsDataMathInputSocketId(socketId) ||
        socketId == EditorNodeGraph::kDataMathBaseInputSocketId;
}

bool IsScalarAverageNode(const Node& node) {
    return node.kind == NodeKind::DataMath &&
        node.dataMathMode == DataMathMode::Average;
}

bool IsImageAverageNode(const Node& node) {
    return node.kind == NodeKind::DataMath &&
        node.dataMathMode == DataMathMode::ImageAverage;
}

bool DataMathAllowsScalarToImageTarget(const Node& node, const std::string& socketId) {
    return node.kind == NodeKind::DataMath &&
        !IsScalarAverageNode(node) &&
        !IsImageAverageNode(node) &&
        IsDataMathImageInputSocketId(socketId);
}

bool DataMathAllowsFullImageTarget(const Node& node, const std::string& socketId) {
    if (node.kind != NodeKind::DataMath || IsScalarAverageNode(node)) {
        return false;
    }
    if (IsImageAverageNode(node)) {
        return EditorNodeGraph::IsDataMathInputSocketId(socketId);
    }
    return IsDataMathImageInputSocketId(socketId);
}

bool ContainsNodeId(const std::vector<Node>& nodes, int id) {
    return std::any_of(nodes.begin(), nodes.end(), [id](const Node& node) {
        return node.id == id;
    });
}

bool IsSourceLikeNode(const Node& node) {
    return node.kind == NodeKind::Image ||
        node.kind == NodeKind::RawSource ||
        node.kind == NodeKind::RawDevelopment ||
        node.kind == NodeKind::ImageGenerator ||
        node.kind == NodeKind::Value ||
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
        case NodeKind::RawDevelopment:
        case NodeKind::RawProjectSourceSet:
        case NodeKind::ImageGenerator:
            return 0;
        case NodeKind::MaskGenerator:
        case NodeKind::CustomMask:
        case NodeKind::Value:
            return 1;
        case NodeKind::RawNeuralDenoise:
        case NodeKind::RawDecode:
        case NodeKind::RawDevelop:
            return 2;
        case NodeKind::Layer:
        case NodeKind::Lut:
        case NodeKind::RawDetailAutoMask:
        case NodeKind::RawDetailFusion:
        case NodeKind::Mfsr:
        case NodeKind::ImageToMask:
        case NodeKind::MaskUtility:
        case NodeKind::Mix:
        case NodeKind::DataMath:
        case NodeKind::ChannelSplit:
        case NodeKind::ChannelCombine:
        case NodeKind::ConstantChannel:
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
    std::set<std::string> instanceUuids;
    int outputCount = 0;
    std::set<int> outgoingImages;

    for (const Node& node : m_Nodes) {
        if (node.id <= 0 || !ids.insert(node.id).second) {
            result.valid = false;
            result.messages.push_back("Duplicate or invalid node id.");
        }
        if (!Stack::NodeMath::IsValidCanonicalUuid(node.instanceUuid) ||
            !instanceUuids.insert(node.instanceUuid).second) {
            result.valid = false;
            result.messages.push_back("Duplicate or invalid node instance UUID.");
        }
        if (node.kind == NodeKind::Output) {
            ++outputCount;
        }
        if (node.kind == NodeKind::Layer && node.layerIndex < 0) {
            result.valid = false;
            result.messages.push_back("Layer node has no layer reference.");
        }
        if (node.kind == NodeKind::Value) {
            for (const Stack::NodeMath::ContractIssue& issue :
                    Stack::NodeMath::ValidateFirstClassValue(
                        node.value.value)) {
                result.valid = false;
                result.messages.push_back(
                    "Value node " + std::to_string(node.id) +
                    " is invalid: " + issue.message);
            }
        }
        if (!node.definitionResolutionError.empty()) {
            result.valid = false;
            result.messages.push_back("Node " + std::to_string(node.id) +
                " has an unresolved definition: " + node.definitionResolutionError);
        }
    }

    for (const Stack::NodeMath::ContractIssue& issue :
            Stack::NodeMath::ValidateCompoundCatalog(m_CompoundDefinitions)) {
        result.valid = false;
        result.messages.push_back("Compound catalog: " + issue.message);
    }

    if (outputCount < 1) {
        result.messages.push_back(m_Nodes.empty()
            ? "Graph is empty. Press Tab or drop an image to begin."
            : "Add an output node or drop an image to start rendering.");
    }

    std::set<std::tuple<int, std::string, int, std::string>> seenLinks;
    std::set<std::pair<int, std::string>> occupiedInputs;
    for (const Link& link : m_Links) {
        if (!ContainsNodeId(m_Nodes, link.fromNodeId) || !ContainsNodeId(m_Nodes, link.toNodeId)) {
            result.valid = false;
            result.messages.push_back("Link references a missing node.");
        }
        if (!seenLinks.insert({ link.fromNodeId, link.fromSocketId, link.toNodeId, link.toSocketId }).second) {
            result.valid = false;
            result.messages.push_back("Duplicate graph socket link.");
        }
        if (!occupiedInputs.insert({ link.toNodeId, link.toSocketId }).second) {
            result.valid = false;
            result.messages.push_back(
                "An input socket has more than one incoming link.");
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
        if (from->kind == NodeKind::Image ||
            from->kind == NodeKind::RawDevelopment ||
            from->kind == NodeKind::RawDecode ||
            from->kind == NodeKind::RawDevelop ||
            from->kind == NodeKind::MultiFrameDenoise ||
            from->kind == NodeKind::MultiFrameHdr) {
            outgoingImages.insert(from->id);
        }

        std::string normalized;
        std::string reason;
        if (!CanConnectSockets(link.fromNodeId, link.fromSocketId,
                link.toNodeId, link.toSocketId, &normalized, &reason) || normalized != link.toSocketId) {
            result.valid = false;
            result.messages.push_back(reason.empty() ? "Connection requires an explicit socket conversion." : reason);
        }
    }

    result.outputConnected = IsOutputConnected();
    if (!result.outputConnected) {
        result.messages.push_back("No completed output chains are connected.");
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
        if (from->kind == NodeKind::Image ||
            from->kind == NodeKind::RawDevelopment ||
            from->kind == NodeKind::RawDecode ||
            from->kind == NodeKind::RawDevelop ||
            from->kind == NodeKind::Mfsr ||
            from->kind == NodeKind::MultiFrameDenoise ||
            from->kind == NodeKind::MultiFrameHdr ||
            from->kind == NodeKind::RawProjectSourceSet) {
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
        node.kind == NodeKind::RawDevelopment ||
        node.kind == NodeKind::RawNeuralDenoise ||
        node.kind == NodeKind::RawDecode ||
        node.kind == NodeKind::RawDevelop ||
        node.kind == NodeKind::RawDetailAutoMask ||
        node.kind == NodeKind::RawDetailFusion ||
        node.kind == NodeKind::HdrMerge ||
        node.kind == NodeKind::Mfsr ||
        node.kind == NodeKind::RawProjectFrame ||
        node.kind == NodeKind::MultiFrameDenoise ||
        node.kind == NodeKind::MultiFrameHdr ||
        node.kind == NodeKind::RawProjectSourceSet ||
        node.kind == NodeKind::Lut ||
        node.kind == NodeKind::ImageGenerator ||
        node.kind == NodeKind::Layer ||
        node.kind == NodeKind::Output ||
        node.kind == NodeKind::Mix ||
        node.kind == NodeKind::DataMath ||
        node.kind == NodeKind::RawOperation ||
        node.kind == NodeKind::TechnicalImage ||
        node.kind == NodeKind::FieldMean ||
        node.kind == NodeKind::Reformat ||
        node.kind == NodeKind::Compound ||
        node.kind == NodeKind::FrequencyFilter ||
        node.kind == NodeKind::FrequencyResponse ||
        node.kind == NodeKind::FrequencyFft ||
        node.kind == NodeKind::FrequencyIfft ||
        node.kind == NodeKind::SpectrumView ||
        node.kind == NodeKind::ApplyFrequencyResponse ||
        node.kind == NodeKind::CombineSpectra ||
        node.kind == NodeKind::SpectrumSeparate ||
        node.kind == NodeKind::SpectrumRecombine ||
        node.kind == NodeKind::FrequencyMask ||
        node.kind == NodeKind::SpectrumMath ||
        node.kind == NodeKind::MagnitudePhase ||
        node.kind == NodeKind::SpectrumAnalyzer ||
        node.kind == NodeKind::ImageToMask ||
        node.kind == NodeKind::CustomMask ||
        node.kind == NodeKind::ChannelSplit ||
        node.kind == NodeKind::ChannelCombine ||
        node.kind == NodeKind::ConstantChannel;
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

    if (to->kind == NodeKind::Output && to->outputSettings.maskOutput &&
        link.toSocketId == kImageInputSocketId) {
        return fromSocket.type == SocketType::Mask ||
            fromSocket.type == SocketType::ScalarField || fromSocket.type == SocketType::Channel ||
            (fromSocket.type == SocketType::Image && IsScalarSocketStream(link.fromNodeId, link.fromSocketId));
    }

    if (fromSocket.type == SocketType::Raw && toSocket.type == SocketType::Raw) {
        return true;
    }
    if (ConnectionRules::IsChannelProcessingBridge(
            *this,
            link.fromNodeId,
            link.fromSocketId,
            fromSocket,
            *to,
            link.toSocketId,
            toSocket)) {
        return true;
    }
    if ((fromSocket.type == SocketType::Channel ||
         IsSpecializedFrequencySocketType(fromSocket.type)) &&
        fromSocket.type == toSocket.type) {
        return true;
    }
    if (fromSocket.type == SocketType::Mask && toSocket.type == SocketType::Mask) {
        return true;
    }
    if ((fromSocket.type == SocketType::Mask || fromSocket.type == SocketType::ScalarField) &&
        (toSocket.type == SocketType::Mask || toSocket.type == SocketType::ScalarField)) {
        return true;
    }
    if (IsUniformOrResourceSocketType(fromSocket.type) &&
        fromSocket.type == toSocket.type) {
        return true;
    }
    if ((fromSocket.type == SocketType::Mask ||
         fromSocket.type == SocketType::ScalarField) &&
        toSocket.type == SocketType::Image) {
        return true;
    }
    if (fromSocket.type == SocketType::Image &&
        (toSocket.type == SocketType::Mask ||
         toSocket.type == SocketType::ScalarField) &&
        IsScalarSocketStream(link.fromNodeId, link.fromSocketId)) {
        return true;
    }
    if (fromSocket.type == SocketType::Image && toSocket.type == SocketType::Image) {
        return true;
    }

    return false;
}

const Link* Graph::FindInputLink(int nodeId, const std::string& socketId) const {
    if (EnsureLookupCache()) {
        const auto incoming = m_LookupCache->inputLinksByNode.find(nodeId);
        if (incoming == m_LookupCache->inputLinksByNode.end()) {
            return nullptr;
        }
        for (const Link* link : incoming->second) {
            if (link->toSocketId == socketId && IsRenderLink(*link)) {
                return link;
            }
        }
        return nullptr;
    }
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [this, nodeId, &socketId](const Link& link) {
        return link.toNodeId == nodeId && link.toSocketId == socketId && IsRenderLink(link);
    });
    return it != m_Links.end() ? &(*it) : nullptr;
}

const Link* Graph::FindAnyInputLink(int nodeId, const std::string& socketId) const {
    if (EnsureLookupCache()) {
        const auto incoming = m_LookupCache->inputLinksByNode.find(nodeId);
        if (incoming == m_LookupCache->inputLinksByNode.end()) {
            return nullptr;
        }
        for (const Link* link : incoming->second) {
            if (link->toSocketId == socketId) {
                return link;
            }
        }
        return nullptr;
    }
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [nodeId, &socketId](const Link& link) {
        return link.toNodeId == nodeId && link.toSocketId == socketId;
    });
    return it != m_Links.end() ? &(*it) : nullptr;
}

const Link* Graph::FindOutputLink(int nodeId, const std::string& socketId) const {
    if (EnsureLookupCache()) {
        const auto outgoing = m_LookupCache->outputLinksByNode.find(nodeId);
        if (outgoing == m_LookupCache->outputLinksByNode.end()) {
            return nullptr;
        }
        for (const Link* link : outgoing->second) {
            if (link->fromSocketId == socketId && IsRenderLink(*link)) {
                return link;
            }
        }
        return nullptr;
    }
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [this, nodeId, &socketId](const Link& link) {
        return link.fromNodeId == nodeId && link.fromSocketId == socketId && IsRenderLink(link);
    });
    return it != m_Links.end() ? &(*it) : nullptr;
}

const Link* Graph::FindScopeInputLink(int nodeId) const {
    if (EnsureLookupCache()) {
        const auto incoming = m_LookupCache->inputLinksByNode.find(nodeId);
        if (incoming == m_LookupCache->inputLinksByNode.end()) {
            return nullptr;
        }
        for (const Link* link : incoming->second) {
            if (GetLinkRole(*link) == LinkRole::Scope) {
                return link;
            }
        }
        return nullptr;
    }
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [this, nodeId](const Link& link) {
        return link.toNodeId == nodeId && GetLinkRole(link) == LinkRole::Scope;
    });
    return it != m_Links.end() ? &(*it) : nullptr;
}

void Graph::ForEachIncomingRenderLink(
    int nodeId,
    const std::function<void(const Link&)>& visitor) const {
    if (!visitor) {
        return;
    }
    if (EnsureLookupCache()) {
        const auto incoming = m_LookupCache->inputLinksByNode.find(nodeId);
        if (incoming == m_LookupCache->inputLinksByNode.end()) {
            return;
        }
        for (const Link* link : incoming->second) {
            if (IsRenderLink(*link)) {
                visitor(*link);
            }
        }
        return;
    }
    for (const Link& link : m_Links) {
        if (link.toNodeId == nodeId && IsRenderLink(link)) {
            visitor(link);
        }
    }
}

void Graph::ForEachIncomingLink(
    int nodeId,
    const std::function<void(const Link&)>& visitor) const {
    if (!visitor) {
        return;
    }
    if (EnsureLookupCache()) {
        const auto incoming = m_LookupCache->inputLinksByNode.find(nodeId);
        if (incoming == m_LookupCache->inputLinksByNode.end()) {
            return;
        }
        for (const Link* link : incoming->second) {
            visitor(*link);
        }
        return;
    }
    for (const Link& link : m_Links) {
        if (link.toNodeId == nodeId) {
            visitor(link);
        }
    }
}

void Graph::ForEachOutgoingLink(
    int nodeId,
    const std::function<void(const Link&)>& visitor) const {
    if (!visitor) {
        return;
    }
    if (EnsureLookupCache()) {
        const auto outgoing = m_LookupCache->outputLinksByNode.find(nodeId);
        if (outgoing == m_LookupCache->outputLinksByNode.end()) {
            return;
        }
        for (const Link* link : outgoing->second) {
            visitor(*link);
        }
        return;
    }
    for (const Link& link : m_Links) {
        if (link.fromNodeId == nodeId) {
            visitor(link);
        }
    }
}

void Graph::ForEachOutgoingRenderLink(
    int nodeId,
    const std::function<void(const Link&)>& visitor) const {
    if (!visitor) {
        return;
    }
    if (EnsureLookupCache()) {
        const auto outgoing = m_LookupCache->outputLinksByNode.find(nodeId);
        if (outgoing == m_LookupCache->outputLinksByNode.end()) {
            return;
        }
        for (const Link* link : outgoing->second) {
            if (IsRenderLink(*link)) {
                visitor(*link);
            }
        }
        return;
    }
    for (const Link& link : m_Links) {
        if (link.fromNodeId == nodeId && IsRenderLink(link)) {
            visitor(link);
        }
    }
}

} // namespace EditorNodeGraph
