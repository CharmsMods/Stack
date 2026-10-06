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

    // Schedule output sockets, not whole nodes. An earlier output can feed
    // a mask for a later output of the same operation without feedback.
    std::vector<ScheduledGraphOutput> pending{root};
    pending.insert(pending.end(), extraRoots.begin(), extraRoots.end());
    std::unordered_map<NodeSocketKey, std::size_t, NodeSocketKeyHash> indices;
    std::vector<ScheduledGraphOutput> outputs;
    std::vector<std::vector<std::size_t>> dependencies;
    const auto add = [&](const ScheduledGraphOutput& output) {
        const NodeSocketKey key{output.nodeId, output.socketId};
        const auto inserted = indices.emplace(key, outputs.size());
        if (inserted.second) { outputs.push_back(output); dependencies.emplace_back(); }
        return inserted.first->second;
    };
    for (const auto& output : pending) add(output);
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        const auto request = outputs[i];
        const auto node = topology.nodes.find(request.nodeId);
        if (node == topology.nodes.end()) {
            schedule.error = "The requested render output does not exist in the graph.";
            return schedule;
        }
        const auto inputs = topology.inputsByNode.find(request.nodeId);
        if (inputs == topology.inputsByNode.end()) continue;
        for (const auto* input : inputs->second) {
            if (!Stack::GraphModel::OutputDependsOnInput(node->second->outputDependencies,
                    request.socketId, input->toSocketId)) continue;
            const auto upstream = add({input->fromNodeId, input->fromSocketId});
            dependencies[i].push_back(upstream);
        }
    }
    std::vector<std::size_t> indegree(outputs.size(), 0);
    std::vector<std::vector<std::size_t>> outgoing(outputs.size());
    std::vector<std::size_t> ready;
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        indegree[i] = dependencies[i].size();
        for (auto upstream : dependencies[i]) outgoing[upstream].push_back(i);
        if (indegree[i] == 0) ready.push_back(i);
    }
    for (std::size_t i = 0; i < ready.size(); ++i) {
        const auto current = ready[i];
        schedule.outputs.push_back(outputs[current]);
        for (auto downstream : outgoing[current])
            if (--indegree[downstream] == 0) ready.push_back(downstream);
    }
    if (schedule.outputs.size() != outputs.size()) {
        schedule.outputs.clear();
        schedule.error = "The render graph contains a cycle in the requested output path.";
        return schedule;
    }
    schedule.valid = true;
    return schedule;
}

} // namespace Stack::Renderer::GraphExecution
