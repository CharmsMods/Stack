#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "ThirdParty/json.hpp"

#include <cstdint>
#include <string>
#include <vector>

class EditorModule;

namespace EditorNodeGraphSelectionExport {

struct BoundarySocketSummary {
    std::string nodeTitle;
    std::string socketLabel;
    std::string direction;
    std::string type;
};

struct ExportResult {
    nlohmann::json clipboardPayload = nlohmann::json::object();
    EditorNodeGraph::Graph exportedGraph;
    std::vector<BoundarySocketSummary> boundarySockets;
    std::uint32_t nodeCount = 0;
};

ExportResult BuildExport(EditorModule* editor, const std::vector<int>& nodeIds, bool includeState, bool wholeGraph);

} // namespace EditorNodeGraphSelectionExport
