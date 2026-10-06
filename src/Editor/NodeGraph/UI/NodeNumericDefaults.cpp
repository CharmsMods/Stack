#include "Editor/NodeGraph/UI/NodeNumericDefaults.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include <cstring>
#include <cmath>

namespace Stack::Editor::NodeGraphUIVisuals {
namespace {
std::optional<double> ResponseDefault(const EditorNodeGraph::FrequencyResponseSettings& response, const float* value) {
    const EditorNodeGraph::FrequencyResponseSettings defaults;
    if (value == &response.lowCutoff) return defaults.lowCutoff;
    if (value == &response.highCutoff) return defaults.highCutoff;
    if (value == &response.transitionWidth) return defaults.transitionWidth;
    if (value == &response.butterworthOrder) return defaults.butterworthOrder;
    const EditorNodeGraph::FrequencyNotch defaultNotch;
    for (const auto& notch : response.notches) {
        if (value == &notch.frequency) return defaultNotch.frequency;
        if (value == &notch.directionDegrees) return defaultNotch.directionDegrees;
        if (value == &notch.width) return defaultNotch.width;
    }
    return std::nullopt;
}
}

std::optional<double> NodeNumericDefault(const EditorNodeGraph::Node& node, const float* value) {
    // Bind the actual field, not its display label or current saved value.
    static const EditorNodeGraph::Node defaults;
    if (value == &node.applyFrequencyResponseSettings.strength) return defaults.applyFrequencyResponseSettings.strength;
    if (value == &node.dataMathSettings.maxValue) return defaults.dataMathSettings.maxValue;
    if (value == &node.dataMathSettings.minValue) return defaults.dataMathSettings.minValue;
    if (value == &node.dataMathSettings.outMax) return defaults.dataMathSettings.outMax;
    if (value == &node.dataMathSettings.outMin) return defaults.dataMathSettings.outMin;
    if (value == &node.frequencyFilterSettings.strength) return defaults.frequencyFilterSettings.strength;
    if (value == &node.frequencyMaskSettings.centerX) return defaults.frequencyMaskSettings.centerX;
    if (value == &node.frequencyMaskSettings.centerY) return defaults.frequencyMaskSettings.centerY;
    if (value == &node.frequencyMaskSettings.cutoff) return defaults.frequencyMaskSettings.cutoff;
    if (value == &node.frequencyMaskSettings.feather) return defaults.frequencyMaskSettings.feather;
    if (value == &node.frequencyMaskSettings.order) return defaults.frequencyMaskSettings.order;
    if (value == &node.frequencyMaskSettings.width) return defaults.frequencyMaskSettings.width;
    if (value == &node.imageGeneratorSettings.angle) return defaults.imageGeneratorSettings.angle;
    if (value == &node.imageGeneratorSettings.fontSize) return defaults.imageGeneratorSettings.fontSize;
    if (value == &node.imageGeneratorSettings.offset) return defaults.imageGeneratorSettings.offset;
    if (value == &node.imageGeneratorSettings.textBackdropBlur) return defaults.imageGeneratorSettings.textBackdropBlur;
    if (value == &node.imageGeneratorSettings.textBackdropOpacity) return defaults.imageGeneratorSettings.textBackdropOpacity;
    if (value == &node.imageGeneratorSettings.textBackdropPadding) return defaults.imageGeneratorSettings.textBackdropPadding;
    if (value == &node.imageToMaskSettings.colorSimilarity) return defaults.imageToMaskSettings.colorSimilarity;
    if (value == &node.imageToMaskSettings.edgeSensitivity) return defaults.imageToMaskSettings.edgeSensitivity;
    if (value == &node.imageToMaskSettings.high) return defaults.imageToMaskSettings.high;
    if (value == &node.imageToMaskSettings.localCoherence) return defaults.imageToMaskSettings.localCoherence;
    if (value == &node.imageToMaskSettings.low) return defaults.imageToMaskSettings.low;
    if (value == &node.imageToMaskSettings.regionFeather) return defaults.imageToMaskSettings.regionFeather;
    if (value == &node.imageToMaskSettings.regionRadius) return defaults.imageToMaskSettings.regionRadius;
    if (value == &node.imageToMaskSettings.softness) return defaults.imageToMaskSettings.softness;
    if (value == &node.imageToMaskSettings.toneSimilarity) return defaults.imageToMaskSettings.toneSimilarity;
    if (value == &node.magnitudePhaseSettings.exposure) return defaults.magnitudePhaseSettings.exposure;
    if (value == &node.magnitudePhaseSettings.gamma) return defaults.magnitudePhaseSettings.gamma;
    if (value == &node.maskSettings.angle) return defaults.maskSettings.angle;
    if (value == &node.maskSettings.centerX) return defaults.maskSettings.centerX;
    if (value == &node.maskSettings.centerY) return defaults.maskSettings.centerY;
    if (value == &node.maskSettings.feather) return defaults.maskSettings.feather;
    if (value == &node.maskSettings.offset) return defaults.maskSettings.offset;
    if (value == &node.maskSettings.radius) return defaults.maskSettings.radius;
    if (value == &node.maskSettings.radiusY) return defaults.maskSettings.radiusY;
    if (value == &node.maskSettings.scale) return defaults.maskSettings.scale;
    if (value == &node.maskSettings.value) return defaults.maskSettings.value;
    if (value == &node.maskUtilitySettings.blackPoint) return defaults.maskUtilitySettings.blackPoint;
    if (value == &node.maskUtilitySettings.gamma) return defaults.maskUtilitySettings.gamma;
    if (value == &node.maskUtilitySettings.softness) return defaults.maskUtilitySettings.softness;
    if (value == &node.maskUtilitySettings.threshold) return defaults.maskUtilitySettings.threshold;
    if (value == &node.maskUtilitySettings.whitePoint) return defaults.maskUtilitySettings.whitePoint;
    if (value == &node.mixFactor) return defaults.mixFactor;
    if (value == &node.spectrumAnalyzerSettings.innerRadius) return defaults.spectrumAnalyzerSettings.innerRadius;
    if (value == &node.spectrumAnalyzerSettings.outerRadius) return defaults.spectrumAnalyzerSettings.outerRadius;
    if (value == &node.spectrumMathSettings.amount) return defaults.spectrumMathSettings.amount;
    if (value == &node.spectrumViewSettings.exposure) return defaults.spectrumViewSettings.exposure;
    if (value == &node.spectrumViewSettings.gamma) return defaults.spectrumViewSettings.gamma;
    if (value == &node.technicalImageSettings.exposureValue) return defaults.technicalImageSettings.exposureValue;
    if (auto result = ResponseDefault(node.frequencyResponseSettings, value)) return result;
    return ResponseDefault(node.frequencyFilterSettings.localResponse, value);
}

std::optional<double> LayerNumericDefault(const EditorNodeGraph::Node& node,
    const char* label, double minimum, double maximum, const char* format) {
    if (node.kind != EditorNodeGraph::NodeKind::Layer) return std::nullopt;
    const auto* definition = EditorNodeGraphDefinitions::FindLiveNodeDefinition(node);
    if (!definition) return std::nullopt;
    std::optional<double> result;
    for (const auto& parameter : definition->parameters) {
        if (parameter.label != label || !parameter.defaultValue.is_number() || !parameter.hasNumericDomain) continue;
        double scale = 1.0;
        if (format && std::strstr(format, "%%") && parameter.minimum >= 0.0 && parameter.maximum <= 1.0 && maximum > 1.0)
            scale = 100.0;
        // Only reuse metadata expressed in the same display domain.
        if (std::abs(parameter.minimum * scale - minimum) > 0.0001 ||
            std::abs(parameter.maximum * scale - maximum) > 0.0001) continue;
        if (result) return std::nullopt; // An ambiguous label must not reset another parameter.
        result = parameter.defaultValue.get<double>() * scale;
    }
    return result;
}

std::optional<float> CompactControlNodeWidth(const EditorNodeGraph::Node& node) {
    if (SharesIdentityWithParameter(node)) return 144.0f;
    if (node.kind == EditorNodeGraph::NodeKind::MaskUtility &&
        node.maskUtilityKind == EditorNodeGraph::MaskUtilityKind::Invert) return 152.0f;
    if (node.kind == EditorNodeGraph::NodeKind::Layer) return 232.0f;
    return std::nullopt;
}

bool SharesIdentityWithParameter(const EditorNodeGraph::Node& node) {
    if (!node.expanded) return false;
    const auto* definition = EditorNodeGraphDefinitions::FindLiveNodeDefinition(node);
    if (!definition || (!node.title.empty() && node.title != definition->label)) return false;
    if (node.kind == EditorNodeGraph::NodeKind::TechnicalImage)
        return node.technicalImageSettings.operation == Stack::NodeMath::TechnicalImageOperation::Exposure;
    if (node.kind != EditorNodeGraph::NodeKind::Layer) return false;
    const EditorNodeGraphDefinitions::LiveParameterDefinition* single = nullptr;
    for (const auto& parameter : definition->parameters) {
        if (parameter.uiHint == "specialized-panel") continue;
        if (single) return false;
        single = &parameter;
    }
    return single && single->label == definition->label && single->defaultValue.is_number();
}
} // namespace Stack::Editor::NodeGraphUIVisuals
