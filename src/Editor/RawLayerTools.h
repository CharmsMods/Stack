#pragma once
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include <vector>
#include <algorithm>
#include <unordered_set>

namespace Stack::Editor {
// View editors follow real editable library instances, including mask branches.
inline std::vector<const EditorNodeGraph::Node*> RawLayerToolNodes(const EditorNodeGraph::Graph& graph) {
    std::vector<const EditorNodeGraph::Node*> result;
    for (const auto& node : graph.GetNodes()) {
        const auto* definition = EditorNodeGraphDefinitions::FindLiveNodeDefinition(node);
        if (node.role == GraphModel::NodeRole::Ordinary && node.definitionResolved &&
            definition && !definition->parameters.empty()) result.push_back(&node);
    }
    return result;
}

inline std::vector<std::string> RawLayerToolLabels(const std::vector<const EditorNodeGraph::Node*>& tools) {
    std::vector<std::string> labels;
    labels.reserve(tools.size());
    for (std::size_t i = 0; i < tools.size(); ++i) {
        const auto& selected = *tools[i];
        int ordinal = 1;
        for (std::size_t j = 0; j < i; ++j)
            if (tools[j]->definitionId == selected.definitionId && tools[j]->title == selected.title) ++ordinal;
        labels.push_back(ordinal > 1 ? selected.title + " " + std::to_string(ordinal) : selected.title);
    }
    return labels;
}

inline std::string RawLayerToolLabel(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Node& selected) {
    int ordinal = 0;
    for (const auto* node : RawLayerToolNodes(graph)) {
        if (node->definitionId != selected.definitionId || node->title != selected.title) continue;
        ++ordinal;
        if (node->instanceUuid == selected.instanceUuid) break;
    }
    return ordinal > 1 ? selected.title + " " + std::to_string(ordinal) : selected.title;
}
}
