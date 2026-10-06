#pragma once

#include "Editor/EditorModule.h"
#include "Graph/GraphDocumentRules.h"

#include <stdexcept>

namespace Stack::Editor {

inline bool ApplyGraphCommand(EditorModule& editor, const std::function<void(EditorNodeGraph::Graph&)>& edit,
    std::string* error = nullptr) {
    auto context = editor.GetGraphEditorContext();
    auto proposal = Stack::GraphModel::ProposeEdit(*context.graph,context.revision,edit);
    std::string reason;
    const bool applied = context.applyEdit(std::move(proposal),reason);
    if (error) *error = std::move(reason);
    return applied;
}

template<class Factory>
int AddGraphNode(EditorModule& editor, Factory&& create) {
    int nodeId = 0;
    std::string error;
    if (!ApplyGraphCommand(editor,[&](auto& graph) {
            auto* node = create(graph);
            if (!node) throw std::runtime_error("The operation could not be created.");
            nodeId = node->id;
            graph.SelectNode(nodeId);
        },&error)) {
        editor.ShowUiNotification(UiNotificationSeverity::Error,error,"graph-add-operation");
        return 0;
    }
    editor.SelectGraphNode(nodeId);
    return nodeId;
}

} // namespace Stack::Editor
