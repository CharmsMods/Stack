#pragma once

#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include <optional>

namespace EditorNodeGraph { struct GraphOutputContext; }

namespace Stack::GraphModel {

struct CatalogInsertionTarget {
    int nodeId = 0;
    std::string socket;
    EditorNodeGraph::SocketDirection direction = EditorNodeGraph::SocketDirection::Input;
    std::optional<EditorNodeGraph::Link> wire;
};

struct CatalogInsertion {
    EditorNodeGraph::Graph graph;
    int nodeId;
    std::string input, output;
};

// Build on a private candidate. The document owner still validates and commits
// the finished graph, including dependencies outside this canvas.
std::optional<CatalogInsertion> PrepareCatalogInsertion(const EditorNodeGraph::Graph& source,
    const CatalogInsertionTarget& target, const EditorNodeGraphDefinitions::NodeCatalogEntry& entry,
    EditorNodeGraph::Vec2 position, std::string& error,
    const EditorNodeGraph::GraphOutputContext* published = nullptr);

}
