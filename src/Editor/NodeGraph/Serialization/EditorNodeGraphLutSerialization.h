#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "ThirdParty/json.hpp"

namespace EditorNodeGraph {

nlohmann::json SerializeLutPayload(const ColorLut::LutPayload& payload);
ColorLut::LutPayload DeserializeLutPayload(const nlohmann::json& value);

} // namespace EditorNodeGraph
