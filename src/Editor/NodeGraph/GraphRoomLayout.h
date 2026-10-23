#pragma once

#include "GraphExpansionLayout.h"
#include <memory>
#include <unordered_map>

namespace Stack::GraphLayout {

struct NodePlacement { int id; std::string uuid; EditorNodeGraph::Vec2 before, after; };
struct GroupPlacement { int id; EditorNodeGraph::GraphRect before, after; };
struct PlacementEdit {
    std::vector<NodePlacement> nodes;
    std::vector<GroupPlacement> groups;
    bool Empty() const { return nodes.empty() && groups.empty(); }
};

// Geometry-only gesture state. Measurement and membership preparation happen
// once. Changed input solves a bounded edit; no graph/image copies or UI services.
class RoomGesture {
public:
    RoomGesture();
    ~RoomGesture();
    RoomGesture(RoomGesture&&) noexcept;
    RoomGesture& operator=(RoomGesture&&) noexcept;
    bool Begin(const EditorNodeGraph::Graph& graph, const std::vector<int>& held,
        const MeasureNode& measure, std::string& error,
        const std::unordered_map<int,EditorNodeGraph::Vec2>& heldStarts = {},
        const std::unordered_map<int,EditorNodeGraph::GraphRect>& heldGroupStarts = {}, bool preserveFlow = false,
        const std::unordered_map<int,EditorNodeGraph::Vec2>& restingPositions = {},
        const std::unordered_map<int,EditorNodeGraph::GraphRect>& restingGroups = {});
    bool Move(EditorNodeGraph::Vec2 offset, PlacementEdit& edit, std::string& error);
    PlacementEdit Restore() const;
private:
    struct State;
    std::unique_ptr<State> m_State;
};

// Validate every identity/expected position before applying any placement.
bool ApplyPlacement(EditorNodeGraph::Graph& graph, const PlacementEdit& edit, std::string& error);

// A new wire establishes a local flow preference. Align its unbranched chain,
// then use the same measured room solver for surrounding nodes and frames.
bool ArrangeConnection(EditorNodeGraph::Graph& graph, int from, int to,
    const MeasureNode& measure, std::string& error);

} // namespace Stack::GraphLayout
