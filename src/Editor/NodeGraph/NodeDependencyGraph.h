#pragma once
#include "EditorNodeGraph.h"

namespace EditorNodeGraph {
// A render-only graph. Authored nodes and connections remain untouched.
Graph BuildNodeDependencyGraph(const Graph& graph, int outputNodeId);
}
