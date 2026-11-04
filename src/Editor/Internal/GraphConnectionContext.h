#pragma once

#include "Editor/EditorModule.h"

namespace Stack::Editor {
// A candidate may contain new nodes whose IDs overlap internal render nodes.
// Only borrow published descriptions for matching authored instances.
inline EditorNodeGraph::GraphOutputContext PublishedConnectionContext(const EditorModule& editor) {
    EditorNodeGraph::GraphOutputContext context;
    context.resolveOutput = [&editor](const auto& node, const auto& socket)
        -> std::optional<EditorNodeGraph::GraphOutputDescription> {
        const auto* current = editor.GetNodeGraph().FindNode(node.id);
        if (current && current->instanceUuid == node.instanceUuid)
            if (const auto* value = editor.FindPublishedGraphDescription(node.id, socket)) return *value;
        return std::nullopt;
    };
    return context;
}
}
