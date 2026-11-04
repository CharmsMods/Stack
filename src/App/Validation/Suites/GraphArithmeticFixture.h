#pragma once
#include "Editor/Internal/GraphEditorCommands.h"

namespace Stack::Validation {
inline int AddUniformOperand(EditorNodeGraph::Graph& graph, float value, EditorNodeGraph::Vec2 position) {
    auto& node = *graph.AddDataMathNode(EditorNodeGraph::DataMathMode::Add, position);
    node.dataMathSettings.domain = NodeMath::ArithmeticDomain::Values;
    node.dataMathSettings.constantA = value;
    node.dataMathSettings.constantB = 0.0f;
    return node.id;
}

inline int AddUniformOperand(EditorModule& editor, float value, EditorNodeGraph::Vec2 position) {
    return Editor::AddGraphNode(editor, [&](auto& graph) {
        return graph.FindNode(AddUniformOperand(graph, value, position));
    });
}
}
