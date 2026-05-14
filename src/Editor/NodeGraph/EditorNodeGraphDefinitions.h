#pragma once

#include "EditorNodeGraph.h"

#include <vector>

namespace EditorNodeGraphDefinitions {

struct NodeCatalogEntry {
    EditorNodeGraph::NodeKind kind = EditorNodeGraph::NodeKind::Layer;
    int value = 0;
    std::string label;
    std::string category;
};

void ApplyNodeMetadata(EditorNodeGraph::Node& node);
std::vector<EditorNodeGraph::SocketDefinition> BuildSockets(const EditorNodeGraph::Node& node, bool visibleOnly);
std::string DefaultInputSocket(const EditorNodeGraph::Node& node);
std::string DefaultOutputSocket(const EditorNodeGraph::Node& node);
std::vector<NodeCatalogEntry> BuildNodeCatalogEntries();
EditorNodeGraph::Node BuildPrototypeNode(const NodeCatalogEntry& entry);

} // namespace EditorNodeGraphDefinitions
