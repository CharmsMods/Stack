#include "Renderer/Internal/RenderPipelineGraphSchedule.h"

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Stack::Renderer::GraphExecution {
namespace {

struct NodeSocketKey {
    int nodeId = -1;
    std::string socketId;

    bool operator==(const NodeSocketKey& other) const {
        return nodeId == other.nodeId && socketId == other.socketId;
    }
};

struct NodeSocketKeyHash {
    std::size_t operator()(const NodeSocketKey& key) const {
        const std::size_t nodeHash = std::hash<int>{}(key.nodeId);
        const std::size_t socketHash =
            std::hash<std::string>{}(key.socketId);
        return nodeHash ^
            (socketHash +
             0x9e3779b9u +
             (nodeHash << 6) +
             (nodeHash >> 2));
    }
};

std::string MakeOutputSocketKey(
    int nodeId,
    std::string_view socketId) {
    std::string key = std::to_string(nodeId);
    key.push_back(':');
    key.append(socketId.data(), socketId.size());
    return key;
}

} // namespace

bool GraphTopologyIndex::IsBoundTo(
    const RenderGraphSnapshot& candidate) const noexcept {
    return graph == &candidate &&
        nodeData == candidate.nodes.data() &&
        linkData == candidate.links.data() &&
        nodeCount == candidate.nodes.size() &&
        linkCount == candidate.links.size();
}

GraphTopologyIndex BuildGraphTopologyIndex(
    const RenderGraphSnapshot& graph) {
    GraphTopologyIndex topology;
    topology.graph = &graph;
    topology.nodeData = graph.nodes.data();
    topology.linkData = graph.links.data();
    topology.nodeCount = graph.nodes.size();
    topology.linkCount = graph.links.size();
    topology.nodes.reserve(graph.nodes.size());
    topology.inputLinks.reserve(graph.nodes.size());
    topology.inputsByNode.reserve(graph.nodes.size());
    topology.outputUseCounts.reserve(graph.links.size());

    for (const RenderGraphNode& node : graph.nodes) {
        if (!topology.nodes.emplace(node.nodeId, &node).second) {
            topology.error =
                "The render graph contains duplicate node identifiers.";
            return topology;
        }
    }

    for (const RenderGraphLink& link : graph.links) {
        if (topology.nodes.count(link.fromNodeId) == 0 ||
            topology.nodes.count(link.toNodeId) == 0) {
            topology.error =
                "The render graph contains a link to a missing node.";
            return topology;
        }
        auto& socketLinks = topology.inputLinks[link.toNodeId];
        if (!socketLinks.emplace(
                std::string_view(link.toSocketId), &link).second) {
            topology.error =
                "The render graph contains more than one link to the same input socket.";
            return topology;
        }
        topology.inputsByNode[link.toNodeId].push_back(&link);
        ++topology.outputUseCounts[
            MakeOutputSocketKey(link.fromNodeId, link.fromSocketId)];
    }

    topology.valid = true;
    return topology;
}

GraphEvaluationSchedule BuildGraphEvaluationSchedule(
    const RenderGraphSnapshot& graph,
    const ScheduledGraphOutput& root,
    const std::vector<ScheduledGraphOutput>& extraRoots) {
    const GraphTopologyIndex topology =
        BuildGraphTopologyIndex(graph);
    return BuildGraphEvaluationSchedule(
        graph,
        topology,
        root,
        extraRoots);
}

GraphEvaluationSchedule BuildGraphEvaluationSchedule(
    const RenderGraphSnapshot& graph,
    const GraphTopologyIndex& topology,
    const ScheduledGraphOutput& root,
    const std::vector<ScheduledGraphOutput>& extraRoots) {
    GraphEvaluationSchedule schedule;
    if (!topology.IsBoundTo(graph)) {
        schedule.error =
            "The render topology index is not bound to this graph snapshot.";
        return schedule;
    }
    if (!topology.valid) {
        schedule.error = topology.error.empty()
            ? "The render topology index is invalid."
            : topology.error;
        return schedule;
    }

    std::vector<ScheduledGraphOutput> pending;
    pending.reserve(graph.nodes.size());
    pending.push_back(root);
    pending.insert(
        pending.end(),
        extraRoots.begin(),
        extraRoots.end());

    std::unordered_set<NodeSocketKey, NodeSocketKeyHash> requiredOutputKeys;
    requiredOutputKeys.reserve(graph.links.size() + pending.size());
    std::unordered_set<int> expandedNodes;
    expandedNodes.reserve(graph.nodes.size());
    std::unordered_map<int, std::vector<std::string>> outputsByNode;
    outputsByNode.reserve(graph.nodes.size());

    for (std::size_t index = 0; index < pending.size(); ++index) {
        ScheduledGraphOutput request = std::move(pending[index]);
        if (topology.nodes.count(request.nodeId) == 0) {
            schedule.error =
                "The requested render output does not exist in the graph.";
            return schedule;
        }

        NodeSocketKey key{ request.nodeId, request.socketId };
        if (requiredOutputKeys.insert(key).second) {
            outputsByNode[request.nodeId].push_back(
                std::move(request.socketId));
        }
        if (!expandedNodes.insert(request.nodeId).second) {
            continue;
        }

        const auto inputIt = topology.inputsByNode.find(request.nodeId);
        if (inputIt == topology.inputsByNode.end()) {
            continue;
        }
        for (const RenderGraphLink* input : inputIt->second) {
            pending.push_back(
                ScheduledGraphOutput{
                    input->fromNodeId,
                    input->fromSocketId
                });
        }
    }

    std::unordered_map<int, std::size_t> indegree;
    std::unordered_map<int, std::vector<int>> outgoing;
    indegree.reserve(expandedNodes.size());
    outgoing.reserve(expandedNodes.size());
    for (int requiredNodeId : expandedNodes) {
        indegree.emplace(requiredNodeId, 0u);
        outgoing.emplace(requiredNodeId, std::vector<int>{});
    }
    for (int requiredNodeId : expandedNodes) {
        const auto inputIt = topology.inputsByNode.find(requiredNodeId);
        if (inputIt == topology.inputsByNode.end()) {
            continue;
        }
        for (const RenderGraphLink* input : inputIt->second) {
            if (expandedNodes.count(input->fromNodeId) == 0) {
                continue;
            }
            outgoing[input->fromNodeId].push_back(requiredNodeId);
            ++indegree[requiredNodeId];
        }
    }

    std::vector<int> ready;
    ready.reserve(expandedNodes.size());
    for (const RenderGraphNode& node : graph.nodes) {
        const auto degreeIt = indegree.find(node.nodeId);
        if (degreeIt != indegree.end() && degreeIt->second == 0u) {
            ready.push_back(node.nodeId);
        }
    }

    std::vector<int> orderedNodes;
    orderedNodes.reserve(expandedNodes.size());
    for (std::size_t index = 0; index < ready.size(); ++index) {
        const int currentNodeId = ready[index];
        orderedNodes.push_back(currentNodeId);
        for (int downstreamNodeId : outgoing[currentNodeId]) {
            auto downstreamDegree = indegree.find(downstreamNodeId);
            if (downstreamDegree != indegree.end() &&
                downstreamDegree->second > 0u &&
                --downstreamDegree->second == 0u) {
                ready.push_back(downstreamNodeId);
            }
        }
    }

    if (orderedNodes.size() != expandedNodes.size()) {
        schedule.error =
            "The render graph contains a cycle in the requested output path.";
        return schedule;
    }

    schedule.outputs.reserve(requiredOutputKeys.size());
    for (int orderedNodeId : orderedNodes) {
        const auto outputIt = outputsByNode.find(orderedNodeId);
        if (outputIt == outputsByNode.end()) {
            continue;
        }
        for (const std::string& outputSocketId : outputIt->second) {
            schedule.outputs.push_back(
                ScheduledGraphOutput{
                    orderedNodeId,
                    outputSocketId
                });
        }
    }
    schedule.valid = true;
    return schedule;
}

} // namespace Stack::Renderer::GraphExecution
