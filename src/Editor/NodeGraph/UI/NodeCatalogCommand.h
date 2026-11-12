#pragma once

#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"

class EditorModule;

namespace Stack::Editor {
const std::vector<EditorNodeGraphDefinitions::NodeCatalogEntry>& RegisteredNodeCatalog();
int AddNodeFromBrowserEntry(EditorModule* editor,
    const EditorNodeGraphDefinitions::NodeCatalogEntry& entry, const EditorNodeGraph::Vec2& position);
}
