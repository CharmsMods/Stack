#pragma once

#include "EditorNodeGraph.h"

#include <cstdint>
#include <vector>

namespace EditorNodeGraphDefinitions {

enum class NodeCatalogPreviewStrategy {
    Auto,
    FallbackOnly,
    NoPreview
};

struct NodeCatalogEntry {
    EditorNodeGraph::NodeKind kind = EditorNodeGraph::NodeKind::Layer;
    int value = 0;
    std::string label;
    std::string category;
    std::string previewKey;
    std::uint32_t previewRecipeVersion = 1;
    NodeCatalogPreviewStrategy previewStrategy = NodeCatalogPreviewStrategy::Auto;
};

void ApplyNodeMetadata(EditorNodeGraph::Node& node);
std::vector<EditorNodeGraph::SocketDefinition> BuildSockets(const EditorNodeGraph::Node& node, bool visibleOnly);
std::string DefaultInputSocket(const EditorNodeGraph::Node& node);
std::string DefaultOutputSocket(const EditorNodeGraph::Node& node);
std::vector<NodeCatalogEntry> BuildNodeCatalogEntries();
EditorNodeGraph::Node BuildPrototypeNode(const NodeCatalogEntry& entry);

} // namespace EditorNodeGraphDefinitions
