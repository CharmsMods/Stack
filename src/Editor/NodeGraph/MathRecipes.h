#pragma once
#include "Editor/NodeGraph/EditorNodeGraph.h"

namespace EditorNodeGraphDefinitions {
inline bool IsMathRecipe(const EditorNodeGraph::Node& node) {
    using namespace EditorNodeGraph;
    return node.kind == NodeKind::DataMath &&
        (node.dataMathMode == DataMathMode::Mix || node.dataMathMode == DataMathMode::Remap);
}
bool BindMathRecipe(EditorNodeGraph::Graph& graph, int nodeId, std::string& error);
}
