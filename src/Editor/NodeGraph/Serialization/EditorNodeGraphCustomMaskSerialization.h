#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "ThirdParty/json.hpp"

namespace EditorNodeGraph {

nlohmann::json SerializeCustomMaskPayload(const CustomMaskPayload& payload, bool includeRaster = true);
CustomMaskPayload DeserializeCustomMaskPayload(const nlohmann::json& value);

} // namespace EditorNodeGraph
