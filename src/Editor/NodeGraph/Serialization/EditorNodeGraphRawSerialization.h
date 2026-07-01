#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "ThirdParty/json.hpp"

namespace EditorNodeGraph {

nlohmann::json SerializeRawMetadata(const Raw::RawMetadata& metadata);
Raw::RawMetadata DeserializeRawMetadata(const nlohmann::json& value);
nlohmann::json SerializeRawSettings(const Raw::RawDevelopSettings& settings);
Raw::RawDevelopSettings DeserializeRawSettings(const nlohmann::json& value);
nlohmann::json SerializeRawDetailFusionSettings(const Raw::RawDetailFusionSettings& settings);
Raw::RawDetailFusionSettings DeserializeRawDetailFusionSettings(const nlohmann::json& value);
nlohmann::json SerializeHdrMergeSettings(const Raw::HdrMergeSettings& settings);
Raw::HdrMergeSettings DeserializeHdrMergeSettings(const nlohmann::json& value);

} // namespace EditorNodeGraph