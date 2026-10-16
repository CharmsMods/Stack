#pragma once
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "ThirdParty/json.hpp"

namespace EditorNodeGraph::Serialization {
// Internal parser for the current model. Callers install only its completed,
// validated candidate through the public read transaction.
void ReadCurrentGraphPayload(const nlohmann::json& payload, Graph& candidate, int layerCount);
}
