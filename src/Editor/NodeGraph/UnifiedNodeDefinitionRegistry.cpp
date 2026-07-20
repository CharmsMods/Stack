#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
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
        case NodeKind::TechnicalImage: return static_cast<int>(node.technicalImageSettings.operation);
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
        case NodeKind::TechnicalImage:
            node.technicalImageSettings.operation = static_cast<Stack::NodeMath::TechnicalImageOperation>(variant);
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
        case NodeKind::FrequencyFft:
        case NodeKind::FrequencyIfft:
            parameters.push_back(BooleanParameter("luminance-only", "Luminance Only", false,
                node.kind == NodeKind::FrequencyFft ? "frequencyFftSettings.luminanceOnly" : "frequencyIfftSettings.luminanceOnly"));
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
            parameters.push_back(NumericParameter("inner-radius", "Inner Radius", 0.0, 0.0, 1.0, "normalized-frequency", LiveAnimationPolicy::Linear, "spectrumAnalyzerSettings.innerRadius"));
            parameters.push_back(NumericParameter("outer-radius", "Outer Radius", 1.0, 0.0, 1.0, "normalized-frequency", LiveAnimationPolicy::Linear, "spectrumAnalyzerSettings.outerRadius"));
            break;
        default:
            break;
    }
    if (parameters.empty()) {
        switch (node.kind) {
            case NodeKind::Image:
            case NodeKind::RawSource:
            case NodeKind::Output:
            case NodeKind::Composite:
            case NodeKind::Scope:
            case NodeKind::Preview:
            case NodeKind::ChannelSplit:
            case NodeKind::ChannelCombine:
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
    canonical["parameters"] = nlohmann::json::array();
    for (const LiveParameterDefinition& parameter : definition.parameters) {
        canonical["parameters"].push_back({
            { "id", parameter.id }, { "label", parameter.label },
            { "type", static_cast<int>(parameter.logicalType) }, { "units", parameter.units },
            { "default", parameter.defaultValue }, { "hasDomain", parameter.hasNumericDomain },
            { "minimum", parameter.minimum }, { "maximum", parameter.maximum },
            { "uiHint", parameter.uiHint }, { "serialized", parameter.serialized },
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
            Node prototype = Prototype(entry.kind, entry.value);
            LiveNodeDefinition definition;
            definition.kind = entry.kind;
            definition.variant = entry.value;
            definition.identity.id = DefinitionId(entry);
            definition.identity.version = { 1, 0, 0 };
            definition.inspectability = InspectabilityFor(entry.kind);
            definition.label = entry.label;
            definition.category = entry.category;
            definition.previewKey = entry.previewKey;
            definition.previewRecipeVersion = entry.previewRecipeVersion;
            definition.previewStrategy = entry.previewStrategy;
            definition.visibleInBrowser = Contains(browserEntries, entry.kind, entry.value);
            definition.sockets = BuildSockets(prototype, false);
            definition.parameters = BuildParameters(prototype);
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
        entry.previewKey = definition.previewKey;
        entry.previewRecipeVersion = definition.previewRecipeVersion;
        entry.previewStrategy = definition.previewStrategy;
        entries.push_back(std::move(entry));
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

} // namespace EditorNodeGraphDefinitions
