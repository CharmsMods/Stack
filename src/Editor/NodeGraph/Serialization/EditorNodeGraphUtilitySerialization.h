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
std::string TechnicalImageOperationToString(Stack::NodeMath::TechnicalImageOperation operation);
Stack::NodeMath::TechnicalImageOperation TechnicalImageOperationFromString(const std::string& value);
nlohmann::json SerializeTechnicalImageSettings(const TechnicalImageSettings& settings);
TechnicalImageSettings DeserializeTechnicalImageSettings(const nlohmann::json& value);
std::string ReconstructionFilterToString(Stack::NodeMath::ReconstructionFilter filter);
Stack::NodeMath::ReconstructionFilter ReconstructionFilterFromString(const std::string& value);
nlohmann::json SerializeReformatSettings(const ReformatSettings& settings);
ReformatSettings DeserializeReformatSettings(const nlohmann::json& value);
std::string SpectrumViewLutToString(SpectrumViewLut lut);
SpectrumViewLut SpectrumViewLutFromString(const std::string& value);
std::string FrequencyMaskShapeToString(FrequencyMaskShape shape);
FrequencyMaskShape FrequencyMaskShapeFromString(const std::string& value);
std::string SpectrumMathModeToString(SpectrumMathMode mode);
SpectrumMathMode SpectrumMathModeFromString(const std::string& value);
std::string MagnitudePhaseModeToString(MagnitudePhaseMode mode);
MagnitudePhaseMode MagnitudePhaseModeFromString(const std::string& value);
std::string SpectrumAnalyzerModeToString(SpectrumAnalyzerMode mode);
SpectrumAnalyzerMode SpectrumAnalyzerModeFromString(const std::string& value);
nlohmann::json SerializeFrequencyFftSettings(const FrequencyFftSettings& settings);
FrequencyFftSettings DeserializeFrequencyFftSettings(const nlohmann::json& value);
nlohmann::json SerializeSpectrumViewSettings(const SpectrumViewSettings& settings);
SpectrumViewSettings DeserializeSpectrumViewSettings(const nlohmann::json& value);
nlohmann::json SerializeFrequencyMaskSettings(const FrequencyMaskSettings& settings);
FrequencyMaskSettings DeserializeFrequencyMaskSettings(const nlohmann::json& value);
nlohmann::json SerializeSpectrumMathSettings(const SpectrumMathSettings& settings);
SpectrumMathSettings DeserializeSpectrumMathSettings(const nlohmann::json& value);
nlohmann::json SerializeMagnitudePhaseSettings(const MagnitudePhaseSettings& settings);
MagnitudePhaseSettings DeserializeMagnitudePhaseSettings(const nlohmann::json& value);
nlohmann::json SerializeSpectrumAnalyzerSettings(const SpectrumAnalyzerSettings& settings);
SpectrumAnalyzerSettings DeserializeSpectrumAnalyzerSettings(const nlohmann::json& value);
std::string ImageToMaskKindToString(ImageToMaskKind kind);
ImageToMaskKind ImageToMaskKindFromString(const std::string& value);

} // namespace EditorNodeGraph
