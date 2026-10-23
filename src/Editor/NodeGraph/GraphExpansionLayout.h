#pragma once

#include "EditorNodeGraph.h"
#include <functional>
#include <optional>

namespace Stack::GraphLayout {

// Graph coordinates relative to the node origin. Includes persistent sockets
// and controls, never transient hover cards or camera-dependent hit padding.
using MeasureNode = std::function<EditorNodeGraph::GraphRect(
    const EditorNodeGraph::Graph&, const EditorNodeGraph::Node&)>;
using MeasureSocket = std::function<std::optional<EditorNodeGraph::Vec2>(
    const EditorNodeGraph::Graph&, const EditorNodeGraph::Node&, const std::string&,
    EditorNodeGraph::SocketDirection)>;

// Explicit edit operation; no timers, rendering services or active-project lookup.
// Call on the command's candidate, so failure discards topology and placement together.
bool ArrangeExpansion(EditorNodeGraph::Graph& graph, const std::vector<int>& exposed,
    EditorNodeGraph::Vec2 origin, const std::string& instanceUuid,
    const std::vector<int>& containingGroups, const MeasureNode& measure, std::string& error,
    const MeasureSocket& measureSocket = {});

} // namespace Stack::GraphLayout
