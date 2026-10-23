#pragma once

#include "Editor/NodeGraph/NodeGraphModelTypes.h"

namespace EditorNodeGraph::CompoundInternal {

// Authored unpacking and execution lowering bind the same parameter contract.
bool BindPromotedParameter(Node& node, const std::string& parameterId,
    const Stack::NodeMath::ParameterValue& value);
// Image data is immutable/shared; encoded and presentation buffers are omitted.
Node CloneExecutionNode(const Node& source);

} // namespace EditorNodeGraph::CompoundInternal
