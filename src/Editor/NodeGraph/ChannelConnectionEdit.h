#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"

namespace EditorNodeGraph {
struct GraphOutputContext;
struct GraphOutputDescription;

// Resolve the output of the ordinary Combine node for this prospective edit.
bool DescribeRgbAdapter(const Graph& graph, const Node& target, const std::string& input,
                        GraphOutputDescription& source);

// Pin feedback and node-browser probes use the same routing decision as a drop.
inline bool CanAuthorConnection(const Graph& graph, int from, const std::string& output,
                                int to, const std::string& input) {
    bool insertRgbCombine;
    return graph.CanConnectSockets(from, output, to, input, nullptr, nullptr, false, &insertRgbCombine);
}

// The caller owns a disposable edit candidate. Commit only on success so a
// replacement, the visible adapter and all four wires share one history entry.
bool ConnectAuthoredSockets(Graph& candidate, int from, const std::string& output,
                            int to, const std::string& input, std::string* error = nullptr,
                            int* adapterNode = nullptr, const GraphOutputContext* published = nullptr);
}
