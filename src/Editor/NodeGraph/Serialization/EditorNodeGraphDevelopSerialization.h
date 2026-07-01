#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "ThirdParty/json.hpp"

namespace EditorNodeGraph {

nlohmann::json SerializeDevelopSubjectImportanceMap(const DevelopSubjectImportanceMap& map);
DevelopSubjectImportanceMap DeserializeDevelopSubjectImportanceMap(const nlohmann::json& value);

} // namespace EditorNodeGraph
