#pragma once
#include "Editor/NodeGraph/NodeGraphTypes.h"
#include <unordered_map>
#include <vector>

namespace Stack::GraphLayout {
struct FlowSize { float width, height; };
// Port offsets are measured from the top of each rigid layout block.
struct FlowEdge { int from, to; float sourceY, targetY; };
// Finite layered placement, used only inside an explicit edit. Intermediate
// wire slots are layout reservations, never authored nodes or solid obstacles.
std::unordered_map<int, EditorNodeGraph::Vec2> ArrangeFlow(
    std::vector<std::vector<int>> columns,
    const std::unordered_map<int, FlowSize>& sizes,
    const std::vector<FlowEdge>& edges, EditorNodeGraph::Vec2 origin);
}
