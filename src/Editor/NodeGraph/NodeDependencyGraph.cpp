#include "NodeDependencyGraph.h"
#include <algorithm>
#include <unordered_set>

namespace EditorNodeGraph {
Graph BuildNodeDependencyGraph(const Graph& graph, int outputNodeId) {
    std::unordered_set<int> retained;
    std::vector<int> pending { outputNodeId };
    while (!pending.empty()) {
        const int node = pending.back();
        pending.pop_back();
        if (!retained.insert(node).second) continue;
        for (const auto& link : graph.GetLinks())
            if (link.toNodeId == node) pending.push_back(link.fromNodeId);
    }
    Graph result = graph;
    auto& nodes = result.EditNodes();
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
        [&](const auto& node) { return !retained.count(node.id); }), nodes.end());
    auto& links = result.EditLinks();
    links.erase(std::remove_if(links.begin(), links.end(),
        [&](const auto& link) { return !retained.count(link.fromNodeId) || !retained.count(link.toNodeId); }), links.end());
    return result;
}
}
