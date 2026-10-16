#pragma once

#include "Project/RawLayerStack.h"
#include <unordered_map>

namespace Stack::Project {

// One editing canvas for the owned graph records. This is a transactional
// projection, never a second independently saved copy of operation parameters.
struct RawProjectGraph {
    EditorNodeGraph::Graph graph;
    std::unordered_map<std::string, EditorNodeGraph::Vec2> origins;
    // References outside this canvas must survive unrelated edits unchanged.
    std::unordered_map<std::string, GraphModel::Endpoint> externalReferences;
    std::unordered_map<int, std::pair<std::string, int>> groupOwners;
    int preparedSource = 0;
    int output = 0;
};

RawProjectGraph BuildRawProjectGraph(const RawLayerStackState& state);
bool ApplyRawProjectGraph(const RawLayerStackState& source, const RawProjectGraph& projection,
    const EditorNodeGraph::Graph& edited, const nlohmann::json& settings,
    const std::string& insertionOwner, RawLayerStackState& result, std::string& error);

} // namespace Stack::Project
