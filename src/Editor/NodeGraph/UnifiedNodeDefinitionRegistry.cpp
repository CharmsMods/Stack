#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Graph/GraphImageRules.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include "Raw/RawGraphParameters.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"

#include "Editor/LayerRegistry.h"
#include "Editor/Timeline/TimelineAnimation.h"
#include "NodeMath/FirstClassValue.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

namespace EditorNodeGraphDefinitions {
namespace {

using EditorNodeGraph::Node;
using EditorNodeGraph::NodeKind;
using Stack::NodeMath::LogicalValueType;

std::string StableToken(std::string value) {
    std::string result;
    result.reserve(value.size());
    bool separator = false;
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            if (separator && !result.empty() && result.back() != '/') result.push_back('-');
            result.push_back(static_cast<char>(std::tolower(ch)));
            separator = false;
        } else if (ch == ':' || ch == '/') {
            while (!result.empty() && (result.back() == '-' || result.back() == '/')) result.pop_back();
            if (!result.empty()) result.push_back('/');
            separator = false;
        } else {
            separator = true;
        }
    }
    while (!result.empty() && (result.back() == '-' || result.back() == '/')) result.pop_back();
    return result.empty() ? "node" : result;
}

int VariantForNode(const Node& node) {
    switch (node.kind) {
        case NodeKind::Layer: return static_cast<int>(node.layerType);
        case NodeKind::Scope: return static_cast<int>(node.scopeKind);
        case NodeKind::MaskGenerator: return static_cast<int>(node.maskKind);
        case NodeKind::MaskCombine: return static_cast<int>(node.maskCombineMode);
        case NodeKind::MaskUtility: return static_cast<int>(node.maskUtilityKind);
        case NodeKind::ImageToMask: return static_cast<int>(node.imageToMaskKind);
        case NodeKind::ImageGenerator: return static_cast<int>(node.imageGeneratorKind);
        case NodeKind::DataMath: return static_cast<int>(node.dataMathMode);
        case NodeKind::Value: return static_cast<int>(node.value.value.logicalType);
        case NodeKind::RawOperation: return static_cast<int>(node.rawOperation.kind);
        case NodeKind::TechnicalImage: return static_cast<int>(node.technicalImageSettings.operation);
        case NodeKind::FrequencyFilter: return 0;
        case NodeKind::FrequencyMask: return static_cast<int>(node.frequencyMaskShape);
        case NodeKind::SpectrumMath: return static_cast<int>(node.spectrumMathMode);
        case NodeKind::MagnitudePhase: return static_cast<int>(node.magnitudePhaseMode);
        case NodeKind::SpectrumAnalyzer: return static_cast<int>(node.spectrumAnalyzerMode);
        default: return 0;
    }
}

Node Prototype(NodeKind kind, int variant) {
    Node node;
    node.kind = kind;
    switch (kind) {
        case NodeKind::Layer: node.layerType = static_cast<LayerType>(variant); break;
        case NodeKind::Scope: node.scopeKind = static_cast<EditorNodeGraph::ScopeKind>(variant); break;
        case NodeKind::MaskGenerator: node.maskKind = static_cast<EditorNodeGraph::MaskGeneratorKind>(variant); break;
        case NodeKind::MaskCombine: node.maskCombineMode = static_cast<EditorNodeGraph::MaskCombineMode>(variant); break;
        case NodeKind::MaskUtility: node.maskUtilityKind = static_cast<EditorNodeGraph::MaskUtilityKind>(variant); break;
        case NodeKind::ImageToMask: node.imageToMaskKind = static_cast<EditorNodeGraph::ImageToMaskKind>(variant); break;
        case NodeKind::ImageGenerator: node.imageGeneratorKind = static_cast<EditorNodeGraph::ImageGeneratorKind>(variant); break;
        case NodeKind::DataMath: node.dataMathMode = static_cast<EditorNodeGraph::DataMathMode>(variant); break;
        case NodeKind::Value:
            node.value.value = BuildDefaultFirstClassValue(static_cast<LogicalValueType>(variant));
            break;
        case NodeKind::RawOperation:
            node.rawOperation = Stack::RawRecipe::MakeGraphOperation(static_cast<Stack::RawRecipe::GraphOperationKind>(variant));
            break;
        case NodeKind::TechnicalImage:
            node.technicalImageSettings.operation = static_cast<Stack::NodeMath::TechnicalImageOperation>(variant);
            break;
        case NodeKind::FrequencyFilter:
            node.frequencyFilterSettings.localResponse.mode =
                static_cast<EditorNodeGraph::FrequencyFilterMode>(variant);
            break;
        case NodeKind::FrequencyMask:
            node.frequencyMaskShape = static_cast<EditorNodeGraph::FrequencyMaskShape>(variant);
            node.frequencyMaskSettings.shape = node.frequencyMaskShape;
            break;
        case NodeKind::SpectrumMath: node.spectrumMathMode = static_cast<EditorNodeGraph::SpectrumMathMode>(variant); break;
        case NodeKind::MagnitudePhase: node.magnitudePhaseMode = static_cast<EditorNodeGraph::MagnitudePhaseMode>(variant); break;
        case NodeKind::SpectrumAnalyzer: node.spectrumAnalyzerMode = static_cast<EditorNodeGraph::SpectrumAnalyzerMode>(variant); break;
        default: break;
    }
    return node;
}

LiveParameterDefinition NumericParameter(
    std::string id,
    std::string label,
    double defaultValue,
    double minimum,
    double maximum,
    std::string units = "unitless",
    LiveAnimationPolicy animation = LiveAnimationPolicy::Linear,
    std::string storageKey = {}) {
    LiveParameterDefinition parameter;
    parameter.id = std::move(id);
    parameter.label = std::move(label);
    parameter.logicalType = LogicalValueType::Scalar;
    parameter.units = std::move(units);
    parameter.defaultValue = defaultValue;
    parameter.hasNumericDomain = true;
    parameter.minimum = minimum;
    parameter.maximum = maximum;
    parameter.uiHint = "slider";
    parameter.animation = animation;
    parameter.storageKey = std::move(storageKey);
    return parameter;
}

LiveParameterDefinition OpaqueSettingsParameter(
    std::string id,
    std::string label,
    std::string storageKey) {
    LiveParameterDefinition parameter;
    parameter.id = std::move(id);
    parameter.label = std::move(label);
    parameter.logicalType = LogicalValueType::Metadata;
    parameter.units = "not-applicable";
    parameter.defaultValue = nlohmann::json::object();
    parameter.uiHint = "specialized-panel";
    parameter.animation = LiveAnimationPolicy::NotAnimatable;
    parameter.storageKey = std::move(storageKey);
    return parameter;
}

LiveParameterDefinition BooleanParameter(
    std::string id,
    std::string label,
    bool defaultValue,
    std::string storageKey) {
    LiveParameterDefinition parameter;
    parameter.id = std::move(id);
    parameter.label = std::move(label);
    parameter.logicalType = LogicalValueType::Boolean;
    parameter.units = "not-applicable";
    parameter.defaultValue = defaultValue;
    parameter.uiHint = "checkbox";
    parameter.animation = LiveAnimationPolicy::Hold;
    parameter.storageKey = std::move(storageKey);
    return parameter;
}

std::vector<LiveParameterDefinition> BuildParameters(const Node& node) {
    std::vector<LiveParameterDefinition> parameters;
    if (node.kind == NodeKind::RawOperation) {
        for (const auto& source : Stack::RawRecipe::GraphParameters(node.rawOperation.kind)) {
            auto parameter = NumericParameter(source.id, source.label, source.initial, source.minimum, source.maximum,
                source.units, LiveAnimationPolicy::Linear, source.path);
            parameter.graphInputCapable = true;
            parameters.push_back(std::move(parameter));
        }
        return parameters;
    }
    if (node.kind == NodeKind::Layer) {
        for (const Stack::Timeline::AnimatableParameterDefinition& source :
             Stack::Timeline::DescribeAnimatableParametersForLayer(node.layerType)) {
            LiveParameterDefinition parameter;
            parameter.id = source.target.parameterId;
            parameter.label = source.parameterLabel;
            parameter.logicalType = source.valueType == Stack::Timeline::AnimatableValueType::Boolean
                ? LogicalValueType::Boolean
                : source.valueType == Stack::Timeline::AnimatableValueType::Integer ||
                  source.valueType == Stack::Timeline::AnimatableValueType::Enum
                    ? LogicalValueType::Integer : LogicalValueType::Scalar;
            parameter.units = "unspecified";
            parameter.defaultValue = source.valueType == Stack::Timeline::AnimatableValueType::Boolean
                ? nlohmann::json(source.defaultValue != 0.0f)
                : source.valueType == Stack::Timeline::AnimatableValueType::Integer ||
                  source.valueType == Stack::Timeline::AnimatableValueType::Enum
                    ? nlohmann::json(static_cast<int>(source.defaultValue))
                    : nlohmann::json(source.defaultValue);
            parameter.hasNumericDomain = true;
            parameter.minimum = source.minValue;
            parameter.maximum = source.maxValue;
            parameter.uiHint = source.valueType == Stack::Timeline::AnimatableValueType::Boolean
                ? "checkbox" : source.valueType == Stack::Timeline::AnimatableValueType::Enum
                    ? "enum" : "slider";
            parameter.animation = source.valueType == Stack::Timeline::AnimatableValueType::Boolean ||
                                  source.valueType == Stack::Timeline::AnimatableValueType::Enum
                ? LiveAnimationPolicy::Hold : LiveAnimationPolicy::Linear;
            parameter.storageKey = source.storageKey;
            parameters.push_back(std::move(parameter));
        }
        parameters.push_back(OpaqueSettingsParameter(
            "layer-settings", "Complete Layer Settings", "layer-json"));
        return parameters;
    }

    switch (node.kind) {
        case NodeKind::Value: {
            LiveParameterDefinition parameter;
            parameter.id = "value";
            parameter.label = "Value";
            parameter.logicalType = node.value.value.logicalType;
            parameter.units = "declared-on-value";
            parameter.defaultValue = Stack::NodeMath::SerializeFirstClassValue(node.value.value);
            parameter.uiHint = node.value.value.logicalType == LogicalValueType::Curve1D ? "curve" : "numeric";
            parameter.animation = node.value.value.logicalType == LogicalValueType::Boolean ||
                                  node.value.value.logicalType == LogicalValueType::Integer
                ? LiveAnimationPolicy::Hold
                : (node.value.value.logicalType == LogicalValueType::Scalar ||
                   node.value.value.logicalType == LogicalValueType::Vector2 ||
                   node.value.value.logicalType == LogicalValueType::Vector3 ||
                   node.value.value.logicalType == LogicalValueType::Vector4 ||
                   node.value.value.logicalType == LogicalValueType::Coordinate2) &&
                  node.value.value.storage == Stack::NodeMath::ValueStorageClass::Uniform
                    ? LiveAnimationPolicy::Linear : LiveAnimationPolicy::NotAnimatable;
            parameters.push_back(std::move(parameter));
            break;
        }
        case NodeKind::TechnicalImage:
            if (node.technicalImageSettings.operation == Stack::NodeMath::TechnicalImageOperation::Exposure) {
                parameters.push_back(NumericParameter(
                    "exposure-fallback", "Fallback EV", 0.0, -24.0, 24.0, "EV", LiveAnimationPolicy::Linear,
                    "technicalImageSettings.exposureValue"));
            }
            break;
        case NodeKind::Reformat:
            parameters.push_back(NumericParameter(
                "width", "Width", 1920.0, 1.0,
                static_cast<double>(Stack::NodeMath::kMaximumReformatDimension),
                "pixels", LiveAnimationPolicy::NotAnimatable, "reformatSettings.width"));
            parameters.push_back(NumericParameter(
                "height", "Height", 1080.0, 1.0,
                static_cast<double>(Stack::NodeMath::kMaximumReformatDimension),
                "pixels", LiveAnimationPolicy::NotAnimatable, "reformatSettings.height"));
            parameters.push_back(OpaqueSettingsParameter(
                "reconstruction", "Reconstruction", "reformatSettings.filter"));
            break;
        case NodeKind::Mix:
            parameters.push_back(NumericParameter("factor", "Factor", 0.5, 0.0, 1.0, "normalized", LiveAnimationPolicy::Linear, "mixFactor"));
            break;
        case NodeKind::DataMath:
            parameters.push_back(NumericParameter("constant-a", "Constant A", 0.0, -65504.0, 65504.0, "inherits-input", LiveAnimationPolicy::Linear, "dataMathSettings.constantA"));
            parameters.push_back(NumericParameter("constant-b", "Constant B", 1.0, -65504.0, 65504.0, "inherits-input", LiveAnimationPolicy::Linear, "dataMathSettings.constantB"));
            parameters.push_back(NumericParameter("minimum", "Minimum", 0.0, -65504.0, 65504.0, "inherits-input", LiveAnimationPolicy::Linear, "dataMathSettings.minValue"));
            parameters.push_back(NumericParameter("maximum", "Maximum", 1.0, -65504.0, 65504.0, "inherits-input", LiveAnimationPolicy::Linear, "dataMathSettings.maxValue"));
            break;
        case NodeKind::MaskGenerator:
            parameters.push_back(NumericParameter("value", "Value", 1.0, 0.0, 1.0, "normalized", LiveAnimationPolicy::Linear, "maskSettings.value"));
            parameters.push_back(NumericParameter("angle", "Angle", 0.0, -360.0, 360.0, "degrees", LiveAnimationPolicy::Linear, "maskSettings.angle"));
            parameters.push_back(NumericParameter("offset", "Offset", 0.0, -2.0, 2.0, "normalized-extent", LiveAnimationPolicy::Linear, "maskSettings.offset"));
            parameters.push_back(NumericParameter("scale", "Scale", 1.0, 0.001, 32.0, "ratio", LiveAnimationPolicy::Linear, "maskSettings.scale"));
            parameters.push_back(NumericParameter("center-x", "Center X", 0.5, -2.0, 3.0, "normalized-coordinate", LiveAnimationPolicy::Linear, "maskSettings.centerX"));
            parameters.push_back(NumericParameter("center-y", "Center Y", 0.5, -2.0, 3.0, "normalized-coordinate", LiveAnimationPolicy::Linear, "maskSettings.centerY"));
            parameters.push_back(NumericParameter("radius", "Radius", 0.35, 0.0, 4.0, "normalized-extent", LiveAnimationPolicy::Linear, "maskSettings.radius"));
            parameters.push_back(NumericParameter("radius-y", "Radius Y", 0.45, 0.001, 4.0, "normalized-extent", LiveAnimationPolicy::Linear, "maskSettings.radiusY"));
            parameters.push_back(NumericParameter("feather", "Feather", 0.1, 0.0, 1.0, "normalized", LiveAnimationPolicy::Linear, "maskSettings.feather"));
            parameters.push_back(BooleanParameter("invert", "Invert", false, "maskSettings.invert"));
            break;
        case NodeKind::MaskUtility:
            parameters.push_back(NumericParameter("low", "Low", 0.0, 0.0, 1.0, "normalized", LiveAnimationPolicy::Linear, "maskUtilitySettings.low"));
            parameters.push_back(NumericParameter("high", "High", 1.0, 0.0, 1.0, "normalized", LiveAnimationPolicy::Linear, "maskUtilitySettings.high"));
            parameters.push_back(NumericParameter("gamma", "Gamma", 1.0, 0.01, 16.0, "unitless", LiveAnimationPolicy::Linear, "maskUtilitySettings.gamma"));
            parameters.push_back(NumericParameter("threshold", "Threshold", 0.5, 0.0, 1.0, "normalized", LiveAnimationPolicy::Linear, "maskUtilitySettings.threshold"));
            parameters.push_back(BooleanParameter("invert", "Invert", false, "maskUtilitySettings.invert"));
            break;
        case NodeKind::ImageToMask:
            parameters.push_back(NumericParameter("low", "Low", 0.25, 0.0, 16.0, "scene-value", LiveAnimationPolicy::Linear, "imageToMaskSettings.low"));
            parameters.push_back(NumericParameter("high", "High", 0.75, 0.0, 16.0, "scene-value", LiveAnimationPolicy::Linear, "imageToMaskSettings.high"));
            parameters.push_back(NumericParameter("softness", "Softness", 0.1, 0.0, 16.0, "scene-value", LiveAnimationPolicy::Linear, "imageToMaskSettings.softness"));
            parameters.push_back(BooleanParameter("invert", "Invert", false, "imageToMaskSettings.invert"));
            parameters.push_back(OpaqueSettingsParameter("samples", "Sample Set", "imageToMaskSettings.sampleRgb"));
            break;
        case NodeKind::ImageGenerator:
            parameters.push_back(OpaqueSettingsParameter("generator-settings", "Generator Settings", "imageGeneratorSettings"));
            break;
        case NodeKind::FrequencyFilter: {
            LiveParameterDefinition strength = NumericParameter(
                EditorNodeGraph::kStrengthParameterId, "Strength", 1.0, 0.0, 1.0,
                "normalized", LiveAnimationPolicy::Linear, "frequencyFilterSettings.strength");
            strength.graphInputCapable = true;
            parameters.push_back(std::move(strength));
            parameters.push_back(OpaqueSettingsParameter(
                "local-response", "Local Response", "frequencyFilterSettings.localResponse"));
            parameters.push_back(OpaqueSettingsParameter(
                "edge-policy", "Edge Policy", "frequencyFilterSettings.edgePolicy"));
            break;
        }
        case NodeKind::FrequencyResponse: {
            auto addFrequencyParameter = [&](const char* id, const char* label, double defaultValue,
                                             double minimum, double maximum, const char* storageKey) {
                LiveParameterDefinition parameter = NumericParameter(
                    id, label, defaultValue, minimum, maximum, "cycles-per-pixel",
                    LiveAnimationPolicy::Linear, storageKey);
                parameter.graphInputCapable = true;
                parameters.push_back(std::move(parameter));
            };
            addFrequencyParameter(EditorNodeGraph::kLowCutoffParameterId, "Low Cutoff", 0.08, 0.0, 0.5,
                "frequencyResponseSettings.lowCutoff");
            addFrequencyParameter(EditorNodeGraph::kHighCutoffParameterId, "High Cutoff", 0.25, 0.0, 0.5,
                "frequencyResponseSettings.highCutoff");
            addFrequencyParameter(EditorNodeGraph::kTransitionWidthParameterId, "Transition", 0.025, 0.0, 0.5,
                "frequencyResponseSettings.transitionWidth");
            LiveParameterDefinition order = NumericParameter(
                EditorNodeGraph::kButterworthOrderParameterId, "Order", 2.0, 1.0, 16.0,
                "unitless", LiveAnimationPolicy::Linear, "frequencyResponseSettings.butterworthOrder");
            order.graphInputCapable = true;
            parameters.push_back(std::move(order));
            parameters.push_back(OpaqueSettingsParameter(
                "response-shape", "Response Shape", "frequencyResponseSettings"));
            break;
        }
        case NodeKind::ApplyFrequencyResponse: {
            LiveParameterDefinition strength = NumericParameter(
                EditorNodeGraph::kStrengthParameterId, "Strength", 1.0, 0.0, 1.0,
                "normalized", LiveAnimationPolicy::Linear, "applyFrequencyResponseSettings.strength");
            strength.graphInputCapable = true;
            parameters.push_back(std::move(strength));
            break;
        }
        case NodeKind::CombineSpectra:
            parameters.push_back(OpaqueSettingsParameter(
                "operation", "Operation", "combineSpectraSettings.mode"));
            break;
        case NodeKind::FrequencyFft:
        case NodeKind::FrequencyIfft:
            parameters.push_back(OpaqueSettingsParameter("edge-policy", "Edge Policy",
                node.kind == NodeKind::FrequencyFft ? "frequencyFftSettings.edgePolicy" : "frequencyIfftSettings.edgePolicy"));
            break;
        case NodeKind::FrequencyMask:
            parameters.push_back(NumericParameter("cutoff", "Cutoff", 0.25, 0.0, 1.0, "normalized-frequency", LiveAnimationPolicy::Linear, "frequencyMaskSettings.cutoff"));
            parameters.push_back(NumericParameter("width", "Width", 0.1, 0.0, 1.0, "normalized-frequency", LiveAnimationPolicy::Linear, "frequencyMaskSettings.width"));
            parameters.push_back(NumericParameter("feather", "Feather", 0.05, 0.0, 1.0, "normalized-frequency", LiveAnimationPolicy::Linear, "frequencyMaskSettings.feather"));
            parameters.push_back(NumericParameter("order", "Order", 2.0, 1.0, 12.0, "unitless", LiveAnimationPolicy::Linear, "frequencyMaskSettings.order"));
            parameters.push_back(NumericParameter("center-x", "Center X", 0.5, 0.0, 1.0, "normalized-frequency", LiveAnimationPolicy::Linear, "frequencyMaskSettings.centerX"));
            parameters.push_back(NumericParameter("center-y", "Center Y", 0.5, 0.0, 1.0, "normalized-frequency", LiveAnimationPolicy::Linear, "frequencyMaskSettings.centerY"));
            parameters.push_back(BooleanParameter("invert", "Invert", false, "frequencyMaskSettings.invert"));
            break;
        case NodeKind::SpectrumView:
            parameters.push_back(NumericParameter("exposure", "Exposure", 1.0, 0.01, 32.0, "display-gain", LiveAnimationPolicy::Linear, "spectrumViewSettings.exposure"));
            parameters.push_back(NumericParameter("gamma", "Gamma", 1.0, 0.1, 4.0, "unitless", LiveAnimationPolicy::Linear, "spectrumViewSettings.gamma"));
            parameters.push_back(BooleanParameter("center-dc", "Center DC", true, "spectrumViewSettings.centerDc"));
            break;
        case NodeKind::SpectrumMath:
            parameters.push_back(NumericParameter("amount", "Amount", 1.0, 0.0, 4.0, "unitless", LiveAnimationPolicy::Linear, "spectrumMathSettings.amount"));
            break;
        case NodeKind::MagnitudePhase:
            parameters.push_back(NumericParameter("exposure", "Exposure", 1.0, 0.01, 32.0, "display-gain", LiveAnimationPolicy::Linear, "magnitudePhaseSettings.exposure"));
            parameters.push_back(NumericParameter("gamma", "Gamma", 1.0, 0.1, 4.0, "unitless", LiveAnimationPolicy::Linear, "magnitudePhaseSettings.gamma"));
            break;
        case NodeKind::SpectrumAnalyzer:
            parameters.push_back(NumericParameter(EditorNodeGraph::kAnalyzerLowParameterId, "Band Low", 0.0, 0.0, 0.5, "cycles-per-pixel", LiveAnimationPolicy::Linear, "spectrumAnalyzerSettings.innerRadius"));
            parameters.back().graphInputCapable = true;
            parameters.push_back(NumericParameter(EditorNodeGraph::kAnalyzerHighParameterId, "Band High", 0.5, 0.0, 0.5, "cycles-per-pixel", LiveAnimationPolicy::Linear, "spectrumAnalyzerSettings.outerRadius"));
            parameters.back().graphInputCapable = true;
            break;
        case NodeKind::Output:
            parameters.push_back(OpaqueSettingsParameter(
                "inspection", "Channel Inspection", "outputSettings"));
            break;
        case NodeKind::ConstantChannel:
            parameters.push_back(NumericParameter(
                "value",
                "Value",
                1.0,
                -65504.0,
                65504.0,
                "unitless",
                LiveAnimationPolicy::Linear,
                "constantChannelSettings.value"));
            parameters.push_back(OpaqueSettingsParameter(
                "purpose",
                "Generated Purpose",
                "constantChannelSettings.generatedOpaqueAlpha"));
            break;
        case NodeKind::ChannelCombine:
            parameters.push_back(OpaqueSettingsParameter(
                "auto-alpha",
                "Automatic Alpha",
                "imageCombineSettings"));
            break;
        default:
            break;
    }
    if (parameters.empty()) {
        switch (node.kind) {
            case NodeKind::Image:
            case NodeKind::RawSource:
            case NodeKind::Composite:
            case NodeKind::Scope:
            case NodeKind::Preview:
            case NodeKind::ChannelSplit:
            case NodeKind::FieldMean:
                break;
            default:
                parameters.push_back(OpaqueSettingsParameter(
                    "settings", "Complete Node Settings", "node-payload"));
                break;
        }
    }
    return parameters;
}

NodeCatalogEntry Synthetic(NodeKind kind, const char* label, const char* key) {
    NodeCatalogEntry entry;
    entry.kind = kind;
    entry.label = label;
    entry.category = "Internal";
    entry.previewKey = key;
    entry.previewStrategy = NodeCatalogPreviewStrategy::NoPreview;
    return entry;
}

bool Contains(const std::vector<NodeCatalogEntry>& entries, NodeKind kind, int variant) {
    return std::any_of(entries.begin(), entries.end(), [&](const NodeCatalogEntry& entry) {
        return entry.kind == kind && entry.value == variant;
    });
}

std::vector<NodeCatalogEntry> AllEntries() {
    std::vector<NodeCatalogEntry> entries = BuildNodeCatalogEntries();
    for (const LayerDescriptor& descriptor : LayerRegistry::GetAllDescriptors()) {
        const int variant = static_cast<int>(descriptor.type);
        if (!Contains(entries, NodeKind::Layer, variant)) {
            NodeCatalogEntry entry = Synthetic(NodeKind::Layer,
                descriptor.displayName ? descriptor.displayName : "Layer", "layer");
            entry.value = variant;
            entry.category = descriptor.categoryName ? descriptor.categoryName : "Layers";
            entry.previewKey = std::string("layer:") + (descriptor.typeId ? descriptor.typeId : "unknown");
            entries.push_back(std::move(entry));
        }
    }
    const NodeCatalogEntry synthetic[] = {
        Synthetic(NodeKind::Image, "Image Source", "source:image"),
        Synthetic(NodeKind::RawSource, "RAW Source", "source:raw"),
        Synthetic(NodeKind::RawProjectFrame,
            "RAW Project Frame", "managed:raw-project-frame"),
        Synthetic(NodeKind::MultiFrameDenoise,
            "Multi-Frame Denoise", "managed:multi-frame-denoise"),
        Synthetic(NodeKind::MultiFrameHdr,
            "Multi-Frame HDR", "managed:multi-frame-hdr"),
        Synthetic(NodeKind::RawProjectSourceSet,
            "RAW Project Source Set", "managed:raw-project-source-set"),
        Synthetic(NodeKind::RawDetailAutoMask, "RAW Detail Auto Mask", "raw-detail:auto-mask"),
        Synthetic(NodeKind::RawDetailFusion, "Pre-Local Exposure", "raw-detail:fusion"),
        Synthetic(NodeKind::Composite, "Composite", "composite"),
    };
    for (const NodeCatalogEntry& entry : synthetic) {
        if (!Contains(entries, entry.kind, entry.value)) entries.push_back(entry);
    }
    return entries;
}

std::string DefinitionId(const NodeCatalogEntry& entry) {
    if (entry.kind == NodeKind::FieldMean) {
        return "stack:analysis/field-mean";
    }
    if (entry.kind == NodeKind::Reformat) {
        return "stack:geometry/reformat";
    }
    if (entry.kind == NodeKind::Layer) {
        const LayerDescriptor* descriptor = LayerRegistry::GetDescriptor(static_cast<LayerType>(entry.value));
        return std::string("stack:layer/") + StableToken(descriptor && descriptor->typeId ? descriptor->typeId : entry.label);
    }
    return std::string("stack:graph/") + StableToken(entry.previewKey.empty() ? entry.label : entry.previewKey);
}

std::string VersionString(const Stack::NodeMath::SemanticVersion& version) {
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
}

Stack::NodeMath::Inspectability InspectabilityFor(NodeKind kind) {
    switch (kind) {
        case NodeKind::RawSource:
        case NodeKind::RawProjectFrame:
        case NodeKind::MultiFrameDenoise:
        case NodeKind::MultiFrameHdr:
        case NodeKind::RawProjectSourceSet:
        case NodeKind::RawDevelopment:
        case NodeKind::RawNeuralDenoise:
        case NodeKind::RawDecode:
        case NodeKind::RawDevelop:
        case NodeKind::RawDetailAutoMask:
        case NodeKind::RawDetailFusion:
            return Stack::NodeMath::Inspectability::OpaqueSpecialized;
        default:
            return Stack::NodeMath::Inspectability::TransparentGraph;
    }
}

} // namespace

std::string ComputeLiveNodeDefinitionHash(const LiveNodeDefinition& definition) {
    nlohmann::json canonical = {
        { "schema", "stack.live-node-definition.v1" },
        { "id", definition.identity.id },
        { "version", VersionString(definition.identity.version) },
        { "inspectability", static_cast<int>(definition.inspectability) },
        { "kind", static_cast<int>(definition.kind) },
        { "variant", definition.variant },
        { "label", definition.label },
        { "category", definition.category },
        { "searchAliases", definition.searchAliases },
        { "previewKey", definition.previewKey },
        { "previewRecipeVersion", definition.previewRecipeVersion },
        { "previewStrategy", static_cast<int>(definition.previewStrategy) },
        { "visibleInBrowser", definition.visibleInBrowser }
    };
    canonical["ports"] = nlohmann::json::array();
    for (const EditorNodeGraph::SocketDefinition& socket : definition.sockets) {
        canonical["ports"].push_back({
            { "id", socket.id },
            { "direction", static_cast<int>(socket.direction) },
            { "type", static_cast<int>(socket.type) },
            { "label", socket.label },
            { "optional", socket.optional },
            { "visible", socket.visible }
        });
    }
    canonical["executable"] = definition.executable;
    canonical["graphRoles"] = definition.graphRoles;
    canonical["requiresSceneLinearRgb"] = definition.requiresSceneLinearRgb;
    canonical["bypass"] = definition.bypassBindings;
    canonical["outputDependencies"] = nlohmann::json::array();
    for (const auto& rule : definition.outputDependencies)
        canonical["outputDependencies"].push_back({{"output", rule.output}, {"inputs", rule.inputs}});
    canonical["parameters"] = nlohmann::json::array();
    for (const LiveParameterDefinition& parameter : definition.parameters) {
        canonical["parameters"].push_back({
            { "id", parameter.id }, { "label", parameter.label },
            { "type", static_cast<int>(parameter.logicalType) }, { "units", parameter.units },
            { "default", parameter.defaultValue }, { "hasDomain", parameter.hasNumericDomain },
            { "minimum", parameter.minimum }, { "maximum", parameter.maximum },
            { "uiHint", parameter.uiHint }, { "serialized", parameter.serialized },
            { "graphInputCapable", parameter.graphInputCapable },
            { "animation", static_cast<int>(parameter.animation) }, { "storageKey", parameter.storageKey }
        });
    }
    return Stack::NodeMath::Sha256ContentIdentity(canonical.dump());
}

const std::vector<LiveNodeDefinition>& GetUnifiedNodeDefinitionRegistry() {
    static const std::vector<LiveNodeDefinition> registry = [] {
        std::vector<LiveNodeDefinition> definitions;
        const std::vector<NodeCatalogEntry> browserEntries = BuildNodeCatalogEntries();
        for (const NodeCatalogEntry& entry : AllEntries()) {
            if (entry.kind == NodeKind::FrequencyFilter &&
                entry.value != static_cast<int>(EditorNodeGraph::FrequencyFilterMode::AllPass)) {
                continue;
            }
            Node prototype = Prototype(entry.kind, entry.value);
            LiveNodeDefinition definition;
            definition.kind = entry.kind;
            definition.variant = entry.value;
            definition.identity.id = DefinitionId(entry);
            const bool frequencyV2 =
                entry.kind == NodeKind::FrequencyFilter ||
                entry.kind == NodeKind::FrequencyResponse ||
                entry.kind == NodeKind::FrequencyFft ||
                entry.kind == NodeKind::FrequencyIfft ||
                entry.kind == NodeKind::SpectrumView ||
                entry.kind == NodeKind::ApplyFrequencyResponse ||
                entry.kind == NodeKind::CombineSpectra ||
                entry.kind == NodeKind::SpectrumSeparate ||
                entry.kind == NodeKind::SpectrumRecombine ||
                entry.kind == NodeKind::SpectrumAnalyzer;
            const bool contractV2 =
                entry.kind == NodeKind::Output ||
                entry.kind == NodeKind::ChannelCombine;
            definition.identity.version = (frequencyV2 || contractV2)
                ? Stack::NodeMath::SemanticVersion{ 2, 0, 0 }
                : Stack::NodeMath::SemanticVersion{ 1, 0, 0 };
            definition.inspectability = InspectabilityFor(entry.kind);
            definition.label = entry.label;
            definition.category = entry.category;
            definition.searchAliases = entry.searchAliases;
            definition.previewKey = entry.previewKey;
            definition.previewRecipeVersion = entry.previewRecipeVersion;
            definition.previewStrategy = entry.previewStrategy;
            definition.visibleInBrowser = Contains(browserEntries, entry.kind, entry.value);
            definition.sockets = BuildSockets(prototype, false);
            definition.parameters = BuildParameters(prototype);
            definition.requiresSceneLinearRgb = entry.kind == NodeKind::RawOperation;
            switch (entry.kind) {
                case NodeKind::RawOperation: case NodeKind::Layer: case NodeKind::Lut:
                case NodeKind::TechnicalImage: case NodeKind::Reformat:
                case NodeKind::RawDetailFusion: case NodeKind::RawDetailAutoMask:
                    definition.bypassBindings.push_back({"imageOut", "imageIn"}); break;
                case NodeKind::MaskUtility:
                    definition.bypassBindings.push_back({"maskOut", "maskIn"}); break;
                case NodeKind::RawNeuralDenoise:
                    definition.bypassBindings.push_back({"rawOut", "rawIn"}); break;
                case NodeKind::MaskCombine:
                    definition.bypassBindings = {{"maskOut", "maskA"}, {"maskOut", "maskB"}}; break;
                case NodeKind::Mix:
                    definition.bypassBindings = {{"imageOut", "imageA"}, {"imageOut", "imageB"}}; break;
                case NodeKind::DataMath:
                    for (int i = 0; i < EditorNodeGraph::kMaxDataMathInputCount; ++i)
                        definition.bypassBindings.push_back({"imageOut", EditorNodeGraph::DataMathInputSocketId(i)});
                    break;
                default: break;
            }

            if (entry.kind == NodeKind::RawOperation) {
                definition.outputDependencies.push_back({"inputImageOut", {"imageIn"}});
                definition.outputDependencies.push_back({"measurementImageOut", {"imageIn", "referenceIn"}});
            }
            if (entry.kind == NodeKind::RawDevelop)
                definition.outputDependencies.push_back({EditorNodeGraph::kPreFinishImageOutputSocketId,
                    {EditorNodeGraph::kRawInputSocketId}});
            definition.identity.contentHash = ComputeLiveNodeDefinitionHash(definition);
            definitions.push_back(std::move(definition));
        }
        return definitions;
    }();
    return registry;
}

std::vector<NodeCatalogEntry> BuildRegisteredNodeCatalogEntries() {
    std::vector<NodeCatalogEntry> entries;
    for (const LiveNodeDefinition& definition : GetUnifiedNodeDefinitionRegistry()) {
        if (!definition.visibleInBrowser) continue;
        NodeCatalogEntry entry;
        entry.kind = definition.kind;
        entry.value = definition.variant;
        entry.label = definition.label;
        entry.category = definition.category;
        entry.searchAliases = definition.searchAliases;
        entry.previewKey = definition.previewKey;
        entry.previewRecipeVersion = definition.previewRecipeVersion;
        entry.previewStrategy = definition.previewStrategy;
        entries.push_back(std::move(entry));
    }
    for (const NodeCatalogEntry& preset : BuildNodeCatalogEntries()) {
        if (preset.kind == NodeKind::FrequencyFilter &&
            preset.value != static_cast<int>(EditorNodeGraph::FrequencyFilterMode::AllPass)) {
            entries.push_back(preset);
        }
    }
    const auto& compounds = GetShippedCompoundTemplates();
    for (std::size_t index = 0; index < compounds.size(); ++index) {
        NodeCatalogEntry entry;
        entry.kind = NodeKind::Compound;
        entry.value = static_cast<int>(index);
        entry.label = compounds[index].label;
        entry.category = "Compounds";
        entry.previewKey = "compound/" + compounds[index].identity.id;
        entry.previewRecipeVersion = 1;
        entry.previewStrategy = NodeCatalogPreviewStrategy::FallbackOnly;
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<EditorNodeGraph::SocketDefinition> BuildRegisteredSockets(
    const Node& node,
    bool visibleOnly) {
    // MFD input sockets are stable per-frame bindings generated from the
    // project manifest. The live definition establishes the node identity,
    // while the instance remains authoritative for its dynamic RAW inputs.
    if (node.kind == NodeKind::RawOperation) {
        auto sockets = BuildSockets(node, visibleOnly);
        for (const auto& parameter : Stack::RawRecipe::GraphParameters(node.rawOperation.kind)) {
            EditorNodeGraph::SocketDefinition socket{EditorNodeGraph::ParameterInputSocketId(parameter.id), node.id,
                EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Scalar, parameter.label, true, true};
            socket.logicalType = LogicalValueType::Scalar;
            sockets.push_back(std::move(socket));
        }
        return sockets;
    }
    if (node.kind == NodeKind::MultiFrameDenoise ||
        node.kind == NodeKind::MultiFrameHdr) {
        return BuildSockets(node, visibleOnly);
    }
    const LiveNodeDefinition* definition = FindLiveNodeDefinition(node);
    if (!definition) return BuildSockets(node, visibleOnly);
    std::vector<EditorNodeGraph::SocketDefinition> sockets;
    for (EditorNodeGraph::SocketDefinition socket : definition->sockets) {
        if (visibleOnly && !socket.visible) continue;
        socket.nodeId = node.id;
        sockets.push_back(std::move(socket));
    }
    return sockets;
}

const LiveNodeDefinition* FindLiveNodeDefinition(NodeKind kind, int variant) {
    const auto& registry = GetUnifiedNodeDefinitionRegistry();
    const auto it = std::find_if(registry.begin(), registry.end(), [&](const LiveNodeDefinition& definition) {
        return definition.kind == kind && definition.variant == variant;
    });
    return it == registry.end() ? nullptr : &(*it);
}

const LiveNodeDefinition* FindLiveNodeDefinition(const Node& node) {
    return FindLiveNodeDefinition(node.kind, VariantForNode(node));
}

void ApplyLiveDefinitionIdentity(Node& node) {
    const LiveNodeDefinition* definition = FindLiveNodeDefinition(node);
    if (!definition) {
        node.definitionId.clear();
        node.definitionVersion.clear();
        node.definitionHash.clear();
        node.definitionResolved = false;
        node.definitionResolutionError = "No live definition is registered for this node variant.";
        return;
    }
    node.definitionId = definition->identity.id;
    node.definitionVersion = VersionString(definition->identity.version);
    node.definitionHash = definition->identity.contentHash;
    node.definitionResolved = true;
    node.definitionResolutionError.clear();
}

bool ResolveSavedLiveDefinition(
    Node& node,
    const std::string& savedId,
    const std::string& savedVersion,
    const std::string& savedHash,
    std::string* error) {
    const LiveNodeDefinition* definition = FindLiveNodeDefinition(node);
    const std::string currentVersion = definition ? VersionString(definition->identity.version) : std::string();
    if (!definition) {
        node.definitionResolved = false;
        node.definitionResolutionError = "Saved node variant has no registered definition.";
    } else if (savedId.empty() || savedVersion.empty() || savedHash.empty()) {
        node.definitionResolved = false;
        node.definitionResolutionError = "Saved node is missing its exact definition ID, version, or content hash.";
    } else if (savedId != definition->identity.id) {
        node.definitionResolved = false;
        node.definitionResolutionError = "Saved definition ID is not installed for this node variant.";
    } else if (savedVersion != currentVersion) {
        node.definitionResolved = false;
        node.definitionResolutionError = "Saved definition version does not match the installed exact version.";
    } else if (savedHash != definition->identity.contentHash) {
        node.definitionResolved = false;
        node.definitionResolutionError = "Saved definition content hash does not match the installed definition.";
    } else {
        node.definitionResolved = true;
        node.definitionResolutionError.clear();
    }
    node.definitionId = savedId;
    node.definitionVersion = savedVersion;
    node.definitionHash = savedHash;
    if (error) *error = node.definitionResolutionError;
    return node.definitionResolved;
}

bool ValidateUnifiedNodeDefinitionRegistry(std::vector<std::string>* errors) {
    std::vector<std::string> local;
    std::set<std::string> identities;
    std::set<std::pair<int, int>> variants;
    for (const LiveNodeDefinition& definition : GetUnifiedNodeDefinitionRegistry()) {
        if (!Stack::NodeMath::IsValidDefinitionId(definition.identity.id)) {
            local.push_back("Invalid definition ID: " + definition.identity.id);
        }
        if (!Stack::NodeMath::IsValidContentHash(definition.identity.contentHash) ||
            definition.identity.contentHash != ComputeLiveNodeDefinitionHash(definition)) {
            local.push_back("Invalid definition content hash: " + definition.identity.id);
        }
        if (!identities.insert(definition.identity.id).second) {
            local.push_back("Duplicate definition ID: " + definition.identity.id);
        }
        if (!variants.insert({ static_cast<int>(definition.kind), definition.variant }).second) {
            local.push_back("Duplicate node kind/variant: " + definition.identity.id);
        }
        std::set<std::string> portIds;
        for (const auto& socket : definition.sockets) {
            if (socket.id.empty() || !portIds.insert(socket.id + ":" + std::to_string(static_cast<int>(socket.direction))).second) {
                local.push_back("Duplicate or empty port identity: " + definition.identity.id);
            }
        }
        std::set<std::string> parameterIds;
        for (const LiveParameterDefinition& parameter : definition.parameters) {
            if (parameter.id.empty() || !parameterIds.insert(parameter.id).second) {
                local.push_back("Duplicate or empty parameter identity: " + definition.identity.id);
            }
            if (parameter.hasNumericDomain && parameter.minimum > parameter.maximum) {
                local.push_back("Invalid parameter domain: " + definition.identity.id + "/" + parameter.id);
            }
        }
    }
    if (errors) *errors = local;
    return local.empty();
}

bool DefinitionSupportsGraphRole(const LiveNodeDefinition& definition, LiveGraphRole role) {
    return definition.executable && (definition.graphRoles & static_cast<unsigned>(role)) != 0;
}

std::optional<LivePortContract> GetLivePortContract(const EditorNodeGraph::Graph& graph,
    const Node& node, const std::string& socketId) {
    using namespace Stack::NodeMath;
    EditorNodeGraph::SocketDefinition socket;
    if (!graph.FindSocket(node.id, socketId, &socket)) return std::nullopt;
    LivePortContract result;
    result.port.id = socket.id;
    result.port.direction = socket.direction == EditorNodeGraph::SocketDirection::Input ? PortDirection::Input : PortDirection::Output;
    result.port.logicalType = socket.logicalType;
    result.port.acceptedLogicalTypes = {socket.logicalType};
    result.port.optional = socket.optional;
    result.port.minimumConnections = socket.optional ? 0 : 1;
    result.port.maximumConnections = 1;
    result.requiredSemantics = MakeUnknownDescriptor(socket.logicalType);
    if (socket.type == EditorNodeGraph::SocketType::ImageOrChannel ||
        (socket.type == EditorNodeGraph::SocketType::Image && node.kind == NodeKind::Layer))
        result.port.acceptedLogicalTypes = {LogicalValueType::ColorImage,LogicalValueType::Channel,LogicalValueType::ScalarField};
    if (socket.type == EditorNodeGraph::SocketType::Mask || socket.type == EditorNodeGraph::SocketType::Channel ||
        socket.type == EditorNodeGraph::SocketType::ScalarField)
        result.port.acceptedLogicalTypes = {LogicalValueType::Channel,LogicalValueType::ScalarField,LogicalValueType::Mask};
    const auto* definition = FindLiveNodeDefinition(node);
    const bool sceneInput = (definition && definition->requiresSceneLinearRgb &&
        (socketId == "imageIn" || socketId == "referenceIn")) ||
        (node.role == Stack::GraphModel::NodeRole::LayerResult && socketId == "imageIn");
    if (sceneInput) {
        result.port.acceptedLogicalTypes = {LogicalValueType::ColorImage};
        result.port.semanticRequirement = RequirementPolicy::Strict;
        result.port.requirementDescription = "Scene-linear RGB with straight or opaque alpha";
        result.requiredSemantics.transfer = SemanticField<TransferDescriptor>::Known({TransferKind::Linear,0,{}});
        result.requiredSemantics.reference = SemanticField<ReferenceState>::Known(ReferenceState::Scene);
        result.acceptedAlpha = {AlphaMode::Absent,AlphaMode::Straight,AlphaMode::Opaque};
    }
    if (socketId == "maskIn") result.defaultValue = 1.0;
    if (socketId == "referenceIn") result.defaultInput = "imageIn";
    if (socketId.rfind("area:",0) == 0 || socketId.rfind("gradient:",0) == 0) result.defaultValue = 0.0;
    if (socketId.rfind("param:",0) == 0 && definition) {
        for (const auto& parameter : definition->parameters) if (socketId.substr(6) == parameter.id) {
            result.defaultValue = parameter.defaultValue;
            UnitDescriptor unit; unit.kind = parameter.units == "unitless" ? UnitKind::Unitless : UnitKind::Custom; unit.customKey = parameter.units == "unitless" ? std::string{} : parameter.units;
            result.port.units = SemanticField<UnitDescriptor>::Known(unit);
        }
    }
    if (socket.type == EditorNodeGraph::SocketType::Mask) {
        const auto* conversion = FindLiveNodeDefinition(NodeKind::ImageToMask, static_cast<int>(EditorNodeGraph::ImageToMaskKind::Luminance));
        if (conversion) result.explicitConversions.push_back(conversion->identity);
    }
    return result;
}

bool AcceptsTypedParameterInput(const Node& node, const std::string& socketId, EditorNodeGraph::SocketType type) {
    if (type != EditorNodeGraph::SocketType::Scalar) return false;
    if (node.kind == NodeKind::TechnicalImage) return socketId == EditorNodeGraph::kExposureValueInputSocketId;
    if (socketId.rfind("param:", 0) != 0) return false;
    const auto id = socketId.substr(6);
    if (node.kind == NodeKind::RawOperation) {
        const auto* definition = FindLiveNodeDefinition(node);
        if (!definition) return false;
        return std::any_of(definition->parameters.begin(), definition->parameters.end(), [&](const auto& p) {
            return p.id == id && p.graphInputCapable;
        });
    }
    return std::find(node.exposedParameterIds.begin(), node.exposedParameterIds.end(), id) != node.exposedParameterIds.end();
}

bool OutputDependsOnInput(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Node& node,
    const std::string& output, const std::string& input) {
    if (node.kind == NodeKind::RawOperation && output == "measurementImageOut")
        return input == (graph.FindInputLink(node.id, "referenceIn") ? "referenceIn" : "imageIn");
    if (node.kind == EditorNodeGraph::NodeKind::Compound) {
        std::vector<std::string> dependencies;
        if (graph.ResolveCompoundOutputInputDependencies(node.id, output, dependencies))
            return std::find(dependencies.begin(), dependencies.end(), input) != dependencies.end();
    }
    return OutputDependsOnInput(node, output, input);
}

bool OutputDependsOnInput(const EditorNodeGraph::Node& node,
    const std::string& output, const std::string& input) {
    const auto* definition = FindLiveNodeDefinition(node);
    return !definition || Stack::GraphModel::OutputDependsOnInput(
        definition->outputDependencies, output, input);
}

bool ValidateInputDescriptor(const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node, const std::string& socketId,
    const Stack::NodeMath::ValueDescriptor& value, std::string& error) {
    using namespace Stack::NodeMath;
    const bool single = value.logicalType == LogicalValueType::Channel || value.logicalType == LogicalValueType::Mask || value.logicalType == LogicalValueType::ScalarField;
    if (node.kind == NodeKind::Mix && (socketId == "imageA" || socketId == "imageB")) {
        const auto* other = graph.FindInputLink(node.id,socketId == "imageA" ? "imageB" : "imageA");
        if (other && !Stack::GraphModel::ValidateImageCombination(value,
                EditorNodeGraph::DescribeGraphOutput(graph,other->fromNodeId,other->fromSocketId).descriptor,error)) return false;
    }
    const bool photo = (node.kind == NodeKind::RawOperation && (socketId == "imageIn" || socketId == "referenceIn")) ||
        (node.role == Stack::GraphModel::NodeRole::LayerResult && socketId == "imageIn");
    const bool coverage = socketId == "maskIn" || (node.kind == NodeKind::Output && node.outputSettings.maskOutput && socketId == "imageIn");
    if (photo && value.logicalType != LogicalValueType::Invalid && value.logicalType != LogicalValueType::ColorImage) {
        error = "Photo operations require a full scene-linear color image."; return false;
    }
    if (coverage && value.logicalType == LogicalValueType::ColorImage) {
        error = "Coverage needs one channel. Select a channel or convert the image to a mask."; return false;
    }
    const auto contract = GetLivePortContract(graph, node, socketId);
    if (!contract || contract->port.semanticRequirement != RequirementPolicy::Strict || single) return true;
    const auto& required = contract->requiredSemantics;
    if ((required.transfer.state == KnowledgeState::Known && value.transfer.state == KnowledgeState::Known && value.transfer.value.kind != required.transfer.value.kind) ||
        (required.reference.state == KnowledgeState::Known && value.reference.state == KnowledgeState::Known && value.reference.value != required.reference.value)) {
        error = "This input requires " + contract->port.requirementDescription + ". Add an explicit conversion."; return false;
    }
    if (!contract->acceptedAlpha.empty() && value.alpha.state == KnowledgeState::Known &&
        std::find(contract->acceptedAlpha.begin(),contract->acceptedAlpha.end(),value.alpha.value) == contract->acceptedAlpha.end()) {
        error = "This input requires straight or opaque RGB. Unpremultiply alpha explicitly first."; return false;
    }
    if (!contract->acceptsUnknownSemantics && value.logicalType != LogicalValueType::Invalid &&
        ((required.transfer.state == KnowledgeState::Known && value.transfer.state != KnowledgeState::Known) ||
         (required.reference.state == KnowledgeState::Known && value.reference.state != KnowledgeState::Known))) {
        error = "This input needs known " + contract->port.requirementDescription + ". Declare or convert the source explicitly."; return false;
    }
    return true;
}

} // namespace EditorNodeGraphDefinitions
