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
    RawGraphGroupOwners groupOwners;
    // Hidden ownership boundaries are reconstructed only during authored edits.
    bool hidesLayerStarts = false;
    struct HiddenStart {
        std::string owner;
        // Canvas destinations identify an ingress rewire without confusing
        // unrelated secondary image inputs with the owning layer input.
        std::vector<std::pair<int,std::string>> destinations;
        std::vector<int> groups;
    };
    std::vector<HiddenStart> hiddenStarts;
    int preparedSource = 0;
    int output = 0;
};

RawProjectGraph BuildRawProjectGraph(const RawLayerStackState& state, bool analysisOnly = false);
void RefreshRawProjectGraphNotes(const RawLayerStackState& state, const RawProjectGraph& projection,
    EditorNodeGraph::Graph& graph);
bool ApplyRawProjectGraph(const RawLayerStackState& source, const RawProjectGraph& projection,
    const EditorNodeGraph::Graph& edited, const nlohmann::json& settings,
    const std::string& insertionOwner, RawLayerStackState& result, std::string& error);

} // namespace Stack::Project
