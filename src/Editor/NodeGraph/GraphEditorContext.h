#pragma once

#include "Graph/GraphDocumentRules.h"
#include "Editor/Timeline/TimelineAnimation.h"

namespace Stack::Editor {

// A canvas host supplies document operations explicitly. The canvas never
// infers its graph from the selected RAW tool or the visible workspace mode.
struct GraphEditorContext {
    std::string documentId;
    std::string graphId;
    std::uint64_t revision = 0;
    std::uint64_t documentRevision = 0;
    EditorNodeGraph::Graph* graph = nullptr;
    const Timeline::TimelineAnimationState* animation = nullptr;
    std::function<bool(GraphModel::EditProposal, nlohmann::json, Timeline::TimelineAnimationState, std::string&)> applyDocumentEdit;
    std::function<std::vector<EditorNodeGraphDefinitions::NodeCatalogEntry>(
        std::optional<std::pair<int, std::string>>)> queryAvailableNodes;
    std::function<bool(GraphModel::EditProposal, std::string&)> applyEdit;
    std::function<bool(int, const std::string&, int, const std::string&, std::string*)> canConnect;
    std::function<void(int)> selectNode;
    std::function<void(int)> inspectParameters;
    std::function<void(int, const std::string&)> requestPreview;
};

} // namespace Stack::Editor
