#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "ThirdParty/json.hpp"

namespace EditorNodeGraph {

nlohmann::json SerializeCustomMaskPayload(const CustomMaskPayload& payload);
CustomMaskPayload DeserializeCustomMaskPayload(const nlohmann::json& value);

} // namespace EditorNodeGraph
