#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "ThirdParty/json.hpp"

#include <string>

namespace EditorNodeGraph {

std::string ScopeKindToString(ScopeKind kind);
ScopeKind ScopeKindFromString(const std::string& value);
std::string MaskGeneratorKindToString(MaskGeneratorKind kind);
MaskGeneratorKind MaskGeneratorKindFromString(const std::string& value);
std::string MaskUtilityKindToString(MaskUtilityKind kind);
MaskUtilityKind MaskUtilityKindFromString(const std::string& value);
std::string MaskCombineModeToString(MaskCombineMode mode);
MaskCombineMode MaskCombineModeFromString(const std::string& value);
std::string ImageGeneratorKindToString(ImageGeneratorKind kind);
ImageGeneratorKind ImageGeneratorKindFromString(const std::string& value);
nlohmann::json SerializeMaskUtilitySettings(const MaskUtilitySettings& settings);
MaskUtilitySettings DeserializeMaskUtilitySettings(const nlohmann::json& value);
nlohmann::json SerializeImageToMaskSettings(const ImageToMaskSettings& settings);
ImageToMaskSettings DeserializeImageToMaskSettings(const nlohmann::json& value);
nlohmann::json SerializeImageGeneratorSettings(const ImageGeneratorSettings& settings);
ImageGeneratorSettings DeserializeImageGeneratorSettings(const nlohmann::json& value);
nlohmann::json SerializeMaskSettings(const MaskGeneratorSettings& settings);
MaskGeneratorSettings DeserializeMaskSettings(const nlohmann::json& value);
std::string MixBlendModeToString(MixBlendMode mode);
MixBlendMode MixBlendModeFromString(const std::string& value);
std::string DataMathModeToString(DataMathMode mode);
DataMathMode DataMathModeFromString(const std::string& value);
nlohmann::json SerializeDataMathSettings(const DataMathSettings& settings);
DataMathSettings DeserializeDataMathSettings(const nlohmann::json& value);
std::string ImageToMaskKindToString(ImageToMaskKind kind);
ImageToMaskKind ImageToMaskKindFromString(const std::string& value);

} // namespace EditorNodeGraph
