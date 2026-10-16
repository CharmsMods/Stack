#include "Editor/NodeGraph/Definitions/OperationInventory.h"
#include "Editor/Timeline/TimelineAnimation.h"
#include "Raw/RawGraphParameters.h"

namespace EditorNodeGraphDefinitions {
namespace {
using EditorNodeGraph::Node;
using EditorNodeGraph::NodeKind;
using Stack::NodeMath::LogicalValueType;
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

} // namespace

std::vector<LiveParameterDefinition> BuildOperationParameters(const Node& node) {
    std::vector<LiveParameterDefinition> parameters;
    if (node.kind == NodeKind::RawOperation) {
        for (const auto& source : Stack::RawRecipe::GraphParameters(node.rawOperation)) {
            auto parameter = NumericParameter(source.id, source.label, source.initial, source.minimum, source.maximum,
                source.units, LiveAnimationPolicy::Linear, source.path);
            parameter.graphInputCapable = true;
            parameters.push_back(std::move(parameter));
        }
        parameters.push_back(OpaqueSettingsParameter(
            "operation-settings", "Complete Operation Settings", "parameters"));
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
            parameter.graphInputCapable = parameter.logicalType == LogicalValueType::Scalar &&
                (node.layerType == LayerType::Brightness || node.layerType == LayerType::Contrast ||
                 node.layerType == LayerType::Saturation || node.layerType == LayerType::Warmth ||
                 node.layerType == LayerType::Sharpen || node.layerType == LayerType::Noise);
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
            parameters.push_back(OpaqueSettingsParameter("framing", "Framing", "reformatSettings"));
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
            parameters.push_back(OpaqueSettingsParameter("domain", "Domain", "dataMathSettings.domain"));
            parameters.back().defaultValue = "spatial";
            parameters.back().uiHint = "choice";
            parameters.push_back(NumericParameter("constant-a", "Constant A", 0.0, -65504.0, 65504.0, "inherits-input", LiveAnimationPolicy::Linear, "dataMathSettings.constantA"));
            parameters.push_back(NumericParameter("constant-b", "Constant B", 1.0, -65504.0, 65504.0, "inherits-input", LiveAnimationPolicy::Linear, "dataMathSettings.constantB"));
            parameters.push_back(NumericParameter("minimum", "Minimum", 0.0, -65504.0, 65504.0, "inherits-input", LiveAnimationPolicy::Linear, "dataMathSettings.minValue"));
            parameters.push_back(NumericParameter("maximum", "Maximum", 1.0, -65504.0, 65504.0, "inherits-input", LiveAnimationPolicy::Linear, "dataMathSettings.maxValue"));
            parameters.push_back(NumericParameter("out-minimum", "Output Minimum", 0.0, -65504.0, 65504.0, "inherits-input", LiveAnimationPolicy::Linear, "dataMathSettings.outMin"));
            parameters.push_back(NumericParameter("out-maximum", "Output Maximum", 1.0, -65504.0, 65504.0, "inherits-input", LiveAnimationPolicy::Linear, "dataMathSettings.outMax"));
            break;
        case NodeKind::MaskTexture:
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

} // namespace EditorNodeGraphDefinitions
