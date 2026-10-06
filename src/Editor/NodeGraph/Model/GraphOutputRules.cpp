#include "Editor/NodeGraph/GraphOutputSemantics.h"

#include "Editor/NodeGraph/NodeGraphModelTypes.h"
#include "NodeMath/ChannelImageSemantics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>

namespace EditorNodeGraph::OutputRules {
using namespace Stack::NodeMath;
namespace {

void SetExtent(ValueDescriptor& descriptor, int width, int height) {
    if (width <= 0 || height <= 0) return;
    SpatialDescriptor spatial = descriptor.spatial.state == KnowledgeState::Known
        ? descriptor.spatial.value : SpatialDescriptor{};
    if (spatial.nativeWidth <= 0) spatial.nativeWidth = width;
    if (spatial.nativeHeight <= 0) spatial.nativeHeight = height;
    spatial.kind = SpatialExtentKind::Finite;
    spatial.fullWindow = spatial.dataWindow = { 0, 0, width, height };
    spatial.rasterOrigin = RasterOrigin::BottomLeft;
    spatial.pixelAspect = 1.0;
    descriptor.spatial = SemanticField<SpatialDescriptor>::Known(spatial);
}

void SetChannel(GraphOutputDescription& output, const std::string& role = "value") {
    const auto previous = output.descriptor;
    output.descriptor = MakeUnknownDescriptor(LogicalValueType::Channel);
    output.descriptor.channels = SemanticField<ChannelDescriptor>::Known({ ChannelLayout::Gray, { role } });
    output.descriptor.spatial = previous.spatial;
    output.descriptor.sampling = previous.sampling;
    output.descriptor.precision = previous.precision;
    output.descriptor.range = previous.range;
    output.descriptor.units = SemanticField<UnitDescriptor>::Known({ UnitKind::Unitless, {} });
    output.componentRole = role == "value" ? std::string() : role;
    if (role == "value") output.componentOrigin.clear();
}

void SetComponents(ValueDescriptor& descriptor, ImageComponentSet components) {
    descriptor.logicalType = LogicalValueType::ColorImage;
    descriptor.channels = SemanticField<ChannelDescriptor>::Known(MakeImageChannelDescriptor(components));
    descriptor.presentImageComponents = SemanticField<ImageComponentSet>::Known(components);
    descriptor.alpha = HasImageComponent(components, ImageComponent::Alpha)
        ? SemanticField<AlphaMode>::Unknown() : SemanticField<AlphaMode>::Known(AlphaMode::Absent);
}

void MergeDiagnostics(GraphOutputDescription& output, const GraphOutputDescription& input) {
    for (const auto& diagnostic : input.diagnostics) {
        if (std::none_of(output.diagnostics.begin(), output.diagnostics.end(), [&](const auto& existing) {
            return existing.affectedIdentity == diagnostic.affectedIdentity && existing.message == diagnostic.message;
        })) output.diagnostics.push_back(diagnostic);
    }
}

void AddDiagnostic(GraphOutputDescription& output, const Node& node, const std::string& socketId,
    std::string message, DiagnosticSeverity severity = DiagnosticSeverity::HardError) {
    Diagnostic diagnostic;
    diagnostic.ruleId = "nmr.output.description";
    diagnostic.stage = DiagnosticStage::Semantic;
    diagnostic.severity = severity;
    diagnostic.authoredSourceIdentity = "node-" + std::to_string(node.id);
    diagnostic.affectedIdentity = GraphOutputIdentity(node.id, socketId);
    diagnostic.message = (node.title.empty() ? "Node " + std::to_string(node.id) : node.title) + ": " + message;
    diagnostic.semanticFingerprint = diagnostic.affectedIdentity + ":" + message;
    output.diagnostics.push_back(std::move(diagnostic));
}

GraphOutputDescription MergeSignals(const std::vector<const GraphOutputDescription*>& signals,
    LogicalValueType defaultType, bool neutral) {
    GraphOutputDescription output;
    output.descriptor = MakeUnknownDescriptor(defaultType);
    if (signals.empty()) return output;
    const auto image = std::find_if(signals.begin(), signals.end(), [](const auto* signal) {
        return signal->descriptor.logicalType == LogicalValueType::ColorImage;
    });
    // A single channel broadcasts across an RGB operand. The resulting layout
    // belongs to that RGB value regardless of operand order.
    const auto* prototype = image == signals.end() ? signals.front() : *image;
    output = *prototype;
    bool singleChannel = true;
    bool sameComponent = !output.componentOrigin.empty();
    const auto initialComponents = output.descriptor.presentImageComponents;
    for (const auto* input : signals) {
        singleChannel &= IsSingleChannelValue(input->descriptor.logicalType);
        sameComponent &= input->componentOrigin == output.componentOrigin &&
            input->componentRole == output.componentRole;
        const bool broadcastChannel = image != signals.end() && IsSingleChannelValue(input->descriptor.logicalType);
        if (!broadcastChannel && input->descriptor.presentImageComponents != initialComponents) {
            output.descriptor.presentImageComponents = SemanticField<ImageComponentSet>::Unknown();
            output.descriptor.channels = SemanticField<ChannelDescriptor>::Unknown();
        }
        if (input->descriptor.spatial != prototype->descriptor.spatial) {
            output.descriptor.spatial = SemanticField<SpatialDescriptor>::Unknown();
        }
        if (!broadcastChannel && input->descriptor.color != prototype->descriptor.color) output.descriptor.color = SemanticField<ColorIdentity>::Unknown();
        if (!broadcastChannel && input->descriptor.transfer != prototype->descriptor.transfer) output.descriptor.transfer = SemanticField<TransferDescriptor>::Unknown();
        MergeDiagnostics(output, *input);
    }
    if (singleChannel) {
        const std::string role = !neutral && sameComponent && signals.front()->descriptor.channels.state == KnowledgeState::Known
            ? signals.front()->descriptor.channels.value.roles.front() : "value";
        SetChannel(output, role);
    }
    if (neutral || !sameComponent) {
        output.componentOrigin.clear();
        output.componentRole.clear();
    }
    output.descriptor.range = SemanticField<NumericRange>::Unknown();
    return output;
}
}

GraphOutputDescription Describe(const Node& node, const std::string& socketId, const Inputs& inputs,
    LogicalValueType declaredType, const GraphOutputContext& context) {
    if (node.role == Stack::GraphModel::NodeRole::Reference && node.referenceType != LogicalValueType::Invalid) {
        GraphOutputDescription reference;
        reference.descriptor = MakeUnknownDescriptor(node.referenceType);
        return reference;
    }
    GraphOutputDescription output;
    output.descriptor = MakeUnknownDescriptor(IsSingleChannelValue(declaredType) ? LogicalValueType::Channel : declaredType);
    const auto input = [&](const std::string& id) -> const GraphOutputDescription* {
        const auto found = inputs.find(id);
        return found == inputs.end() ? nullptr : found->second;
    };
    const auto use = [&](const std::string& id, bool required = true) {
        if (const auto* value = input(id)) { output = *value; return true; }
        if (required) {
            output.descriptor = MakeUnknownDescriptor(LogicalValueType::Invalid);
            AddDiagnostic(output, node, socketId, "Missing input " + id, DiagnosticSeverity::Information);
        }
        return false;
    };
    const auto signals = [&](const std::vector<std::string>& ids) {
        std::vector<const GraphOutputDescription*> values;
        for (const auto& id : ids) if (const auto* value = input(id)) values.push_back(value);
        return values;
    };
    bool rendered = true;
    switch (node.kind) {
    case NodeKind::Image: {
        output.descriptor = node.image.sourceColorMetadata.descriptor;
        if (output.descriptor.logicalType != LogicalValueType::ColorImage) {
            output.descriptor = MakeUnknownDescriptor(LogicalValueType::ColorImage);
            if (node.image.originalChannels >= 1 && node.image.originalChannels <= 4)
                SetComponents(output.descriptor, node.image.originalChannels == 2 || node.image.originalChannels == 4
                ? MakeImageComponentSet({ ImageComponent::Red, ImageComponent::Green, ImageComponent::Blue, ImageComponent::Alpha })
                : MakeImageComponentSet({ ImageComponent::Red, ImageComponent::Green, ImageComponent::Blue }));
        }
        SetExtent(output.descriptor, node.image.width, node.image.height);
        output.descriptor.precision = SemanticField<LogicalPrecision>::Known(LogicalPrecision::UInt8);
        if (node.image.isLoading) AddDiagnostic(output, node, socketId, "Loading", DiagnosticSeverity::Information);
        rendered = false;
        break;
    }
    case NodeKind::RawSource:
        output.descriptor = MakeUnknownDescriptor(LogicalValueType::Raw);
        SetExtent(output.descriptor, node.rawSource.metadata.rawWidth, node.rawSource.metadata.rawHeight);
        rendered = false;
        break;
    case NodeKind::Value:
        output.descriptor = MakeUnknownDescriptor(node.value.value.logicalType);
        if (output.descriptor.units.state != KnowledgeState::NotApplicable)
            output.descriptor.units = SemanticField<UnitDescriptor>::Known(node.value.value.units);
        if (output.descriptor.precision.state != KnowledgeState::NotApplicable && node.value.value.logicalType != LogicalValueType::Integer)
            output.descriptor.precision = SemanticField<LogicalPrecision>::Known(LogicalPrecision::Float64);
        rendered = false;
        break;
    case NodeKind::ConstantChannel:
        use(kMatchExtentInputSocketId);
        SetChannel(output, node.constantChannelSettings.generatedOpaqueAlpha ? "A" : "value");
        output.componentOrigin.clear();
        output.descriptor.range = SemanticField<NumericRange>::Known({ node.constantChannelSettings.value,
            node.constantChannelSettings.value, false, false, NonFinitePolicy::Forbidden });
        break;
    case NodeKind::ChannelSplit: {
        const bool connected = use(kImageInputSocketId);
        const auto image = output.descriptor;
        std::string role = socketId;
        std::transform(role.begin(), role.end(), role.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        SetChannel(output, role);
        output.componentOrigin = image.provenance.state == KnowledgeState::Known
            ? image.provenance.value.operationIdentity + "." + role
            : GraphOutputIdentity(node.id, socketId);
        if (connected) {
            const auto component = ParseImageComponentToken(role);
            if (image.logicalType != LogicalValueType::ColorImage) AddDiagnostic(output, node, socketId, "RGB components require an RGB value");
            else if (component && image.presentImageComponents.state == KnowledgeState::Known &&
                !HasImageComponent(image.presentImageComponents.value, *component)) {
                AddDiagnostic(output, node, socketId, role + " component is absent");
            }
        }
        break;
    }
    case NodeKind::ChannelCombine: {
        ImageComponentSet components;
        const std::array<const char*, 4> ports{ "r", "g", "b", "a" };
        const std::array<ImageComponent, 4> names{ ImageComponent::Red, ImageComponent::Green, ImageComponent::Blue, ImageComponent::Alpha };
        const GraphOutputDescription* first = nullptr;
        output.descriptor = MakeUnknownDescriptor(LogicalValueType::ColorImage);
        for (std::size_t i = 0; i < ports.size(); ++i) {
            const auto* value = input(ports[i]);
            if (!value) continue;
            AddImageComponent(components, names[i]);
            MergeDiagnostics(output, *value);
            if (!first) {
                first = value;
                output.descriptor.spatial = value->descriptor.spatial;
                output.descriptor.sampling = value->descriptor.sampling;
            } else if (first->descriptor.spatial != value->descriptor.spatial) {
                output.descriptor.spatial = SemanticField<SpatialDescriptor>::Unknown();
                AddDiagnostic(output, node, socketId, "Channel extents do not agree");
            }
            if (!IsSingleChannelValue(value->descriptor.logicalType)) AddDiagnostic(output, node, socketId, "Assembly requires one channel per component");
        }
        SetComponents(output.descriptor, components);
        if ((components.bits & 7) == 0) AddDiagnostic(output, node, socketId, "Connect at least one R, G, or B component");
        break;
    }
    case NodeKind::DataMath: {
        std::vector<std::string> ports;
        for (int i = 0; i < kMaxDataMathInputCount; ++i) ports.push_back(DataMathInputSocketId(i));
        ports.push_back(kDataMathBaseInputSocketId);
        auto values = signals(ports);
        output = MergeSignals(values, node.dataMathMode == DataMathMode::Average ? LogicalValueType::Channel : declaredType,
            node.dataMathMode == DataMathMode::Average);
        if (values.empty()) AddDiagnostic(output, node, socketId, "Connect a data input");
        if (node.dataMathMode == DataMathMode::Average) SetChannel(output);
        if (const auto* mask = input(kMaskInputSocketId)) MergeDiagnostics(output, *mask);
        break;
    }
    case NodeKind::Mix:
        output = MergeSignals(signals({ kMixInputASocketId, kMixInputBSocketId }), declaredType,
            node.mixBlendMode == MixBlendMode::Average);
        if (!input(kMixInputASocketId) && !input(kMixInputBSocketId)) AddDiagnostic(output, node, socketId, "Connect a data input");
        break;
    case NodeKind::MaskCombine:
        output = MergeSignals(signals({ kMaskCombineInputASocketId, kMaskCombineInputBSocketId }), LogicalValueType::Channel, true);
        SetChannel(output);
        if (!input(kMaskCombineInputASocketId) || !input(kMaskCombineInputBSocketId)) AddDiagnostic(output, node, socketId, "Connect both channel inputs");
        output.descriptor.range = SemanticField<NumericRange>::Known({ 0, 1, false, false, NonFinitePolicy::Forbidden });
        break;
    case NodeKind::MaskUtility:
        use(kMaskUtilityInputSocketId);
        if (node.maskUtilityKind == MaskUtilityKind::Threshold) SetChannel(output);
        if (node.maskUtilityKind == MaskUtilityKind::Invert) {
            if (node.maskUtilitySettings.enabled && output.descriptor.range.state == KnowledgeState::Known) {
                auto& range = output.descriptor.range.value;
                const double minimum = 1.0 - range.nominalMaximum;
                range.nominalMaximum = 1.0 - range.nominalMinimum;
                range.nominalMinimum = minimum;
                std::swap(range.allowsBelowNominal, range.allowsAboveNominal);
            }
        } else output.descriptor.range = SemanticField<NumericRange>::Known({ 0, 1, false, false, NonFinitePolicy::Forbidden });
        break;
    case NodeKind::ImageToMask:
        use(kImageToMaskInputSocketId);
        SetChannel(output);
        output.descriptor.range = SemanticField<NumericRange>::Known({ 0, 1, false, false, NonFinitePolicy::Forbidden });
        break;
    case NodeKind::MaskGenerator:
    case NodeKind::CustomMask:
        SetChannel(output);
        output.descriptor.spatial = input(kMatchExtentInputSocketId)
            ? input(kMatchExtentInputSocketId)->descriptor.spatial : context.canvasSpatial;
        if (node.kind == NodeKind::CustomMask && node.customMask.referenceMode == CustomMaskReferenceMode::CustomSize)
            SetExtent(output.descriptor, node.customMask.width, node.customMask.height);
        output.descriptor.range = SemanticField<NumericRange>::Known({ 0, 1, false, false, NonFinitePolicy::Forbidden });
        break;
    case NodeKind::ImageGenerator:
    case NodeKind::SpectrumView: {
        if (node.kind == NodeKind::SpectrumView) use(kSpectrumInputSocketId);
        const auto spatial = node.kind == NodeKind::SpectrumView ? output.descriptor.spatial : context.canvasSpatial;
        output.descriptor = MakeUnknownDescriptor(LogicalValueType::ColorImage);
        SetComponents(output.descriptor, MakeImageComponentSet({ ImageComponent::Red, ImageComponent::Green, ImageComponent::Blue, ImageComponent::Alpha }));
        output.descriptor.spatial = spatial;
        break;
    }
    case NodeKind::Layer:
    case NodeKind::Lut:
    case NodeKind::RawOperation:
    case NodeKind::TechnicalImage:
    case NodeKind::Reformat:
        if (node.kind == NodeKind::Lut && !input(kImageInputSocketId)) {
            output = MergeSignals(signals({ "r", "g", "b", "a" }), LogicalValueType::ColorImage, true);
            const auto spatial = output.descriptor.spatial;
            output.descriptor = MakeUnknownDescriptor(LogicalValueType::ColorImage);
            output.descriptor.spatial = spatial;
            ImageComponentSet components;
            for (const char* port : { "r", "g", "b", "a" })
                if (input(port)) {
                    const std::string role(1, static_cast<char>(std::toupper(port[0])));
                    if (const auto component = ParseImageComponentToken(role)) AddImageComponent(components, *component);
                }
            SetComponents(output.descriptor, components);
            if (inputs.empty()) AddDiagnostic(output, node, socketId, "Connect an input");
        } else use(node.kind == NodeKind::RawOperation && socketId == "measurementImageOut" && input("referenceIn")
            ? "referenceIn" : kImageInputSocketId);
        if (node.kind == NodeKind::RawOperation) output.descriptor.range = SemanticField<NumericRange>::Unknown();
        if (node.kind == NodeKind::Layer && node.layerType == LayerType::ViewTransform &&
            output.descriptor.logicalType == LogicalValueType::ColorImage) {
            output.descriptor.reference = SemanticField<ReferenceState>::Known(ReferenceState::Display);
            output.descriptor.color = SemanticField<ColorIdentity>::Known({"srgb-d65", {}, ColorRelation::Standard});
            // The instance's encoding setting is supplied by the snapshot owner.
            output.descriptor.transfer = SemanticField<TransferDescriptor>::Unknown();
        }
        if (node.kind == NodeKind::Lut && ColorLut::HasAnyLutData(node.lut)) {
            const auto spatial = output.descriptor.spatial;
            const bool alpha = output.descriptor.presentImageComponents.state == KnowledgeState::Known &&
                HasImageComponent(output.descriptor.presentImageComponents.value, ImageComponent::Alpha);
            output.descriptor = MakeUnknownDescriptor(LogicalValueType::ColorImage);
            output.descriptor.spatial = spatial;
            auto components = MakeImageComponentSet({ ImageComponent::Red, ImageComponent::Green, ImageComponent::Blue });
            if (alpha) AddImageComponent(components, ImageComponent::Alpha);
            SetComponents(output.descriptor, components);
            output.componentOrigin.clear();
            output.componentRole.clear();
        }
        if (node.kind == NodeKind::TechnicalImage && output.descriptor.logicalType == LogicalValueType::ColorImage) {
            std::vector<Diagnostic> diagnostics;
            auto described = DescribeTechnicalImageOutput(node.technicalImageSettings.operation, output.descriptor,
                node.technicalImageSettings.exposureValue, diagnostics);
            if (described.logicalType != LogicalValueType::Failure) output.descriptor = std::move(described);
            for (auto& diagnostic : diagnostics) {
                diagnostic.authoredSourceIdentity = "node-" + std::to_string(node.id);
                diagnostic.affectedIdentity = GraphOutputIdentity(node.id, socketId);
                output.diagnostics.push_back(std::move(diagnostic));
            }
        } else if (node.kind != NodeKind::Reformat) output.descriptor.range = SemanticField<NumericRange>::Unknown();
        if (node.kind == NodeKind::Reformat) SetExtent(output.descriptor, node.reformatSettings.width, node.reformatSettings.height);
        if (const auto* mask = input(kMaskInputSocketId)) MergeDiagnostics(output, *mask);
        break;
    case NodeKind::Output:
    case NodeKind::Preview:
    case NodeKind::Scope:
        use(node.kind == NodeKind::Preview ? kPreviewInputSocketId : node.kind == NodeKind::Scope ? kScopeInputSocketId : kImageInputSocketId);
        rendered = false;
        break;
    case NodeKind::FieldMean:
        if (const auto* value = input(kReductionFieldInputSocketId)) MergeDiagnostics(output, *value);
        else AddDiagnostic(output, node, socketId, "Connect a channel to measure");
        output.descriptor = MakeUnknownDescriptor(LogicalValueType::Scalar);
        output.descriptor.precision = SemanticField<LogicalPrecision>::Known(LogicalPrecision::Float64);
        rendered = false;
        break;
    case NodeKind::FrequencyFilter:
        use(kChannelInputSocketId);
        output.descriptor.range = SemanticField<NumericRange>::Unknown();
        break;
    case NodeKind::FrequencyFft:
    case NodeKind::FrequencyIfft:
    case NodeKind::ApplyFrequencyResponse:
    case NodeKind::CombineSpectra:
    case NodeKind::SpectrumSeparate:
    case NodeKind::SpectrumRecombine: {
        const auto port = node.kind == NodeKind::FrequencyFft ? kChannelInputSocketId :
            node.kind == NodeKind::CombineSpectra ? kSpectrumInputASocketId :
            node.kind == NodeKind::SpectrumRecombine ? kSpectrumMagnitudeInputSocketId : kSpectrumInputSocketId;
        use(port);
        const auto carried = output.descriptor;
        if (node.kind == NodeKind::CombineSpectra) output = MergeSignals(signals({ kSpectrumInputASocketId, kSpectrumInputBSocketId }), declaredType, false);
        if (node.kind == NodeKind::SpectrumRecombine) output = MergeSignals(signals({ kSpectrumMagnitudeInputSocketId, kSpectrumPhaseInputSocketId }), declaredType, false);
        output.descriptor = MakeUnknownDescriptor(declaredType);
        output.descriptor.spatial = carried.spatial;
        if (node.kind == NodeKind::FrequencyIfft) {
            const auto role = output.componentRole;
            SetChannel(output, role.empty() ? "value" : role);
        }
        break;
    }
    case NodeKind::RawDetailAutoMask:
    case NodeKind::RawDetailFusion:
        use(kImageInputSocketId);
        if (socketId == kMaskOutputSocketId) {
            SetChannel(output);
            output.descriptor.range = SemanticField<NumericRange>::Known({ 0, 1, false, false, NonFinitePolicy::Forbidden });
        }
        break;
    case NodeKind::HdrMerge:
    case NodeKind::Mfsr: {
        std::vector<const GraphOutputDescription*> values;
        for (const auto& pair : inputs) if (pair.second->descriptor.logicalType == LogicalValueType::ColorImage) values.push_back(pair.second);
        // Anchor extent follows the node's declared reference input.
        const auto* reference = input(node.kind == NodeKind::Mfsr ? kMfsrReferenceInputSocketId : kHdrMergeInput1SocketId);
        output = MergeSignals(values, LogicalValueType::ColorImage, true);
        if (reference) output.descriptor.spatial = reference->descriptor.spatial;
        if (node.kind == NodeKind::Mfsr) output.descriptor.spatial = SemanticField<SpatialDescriptor>::Unknown();
        if (values.empty()) AddDiagnostic(output, node, socketId, "Connect an image input");
        break;
    }
    case NodeKind::RawNeuralDenoise:
        use(kRawInputSocketId);
        rendered = false;
        break;
    case NodeKind::RawDevelopment:
    case NodeKind::RawDecode:
    case NodeKind::RawDevelop:
    case NodeKind::RawProjectSourceSet:
    case NodeKind::MultiFrameDenoise:
    case NodeKind::MultiFrameHdr:
        output.descriptor = MakeUnknownDescriptor(LogicalValueType::ColorImage);
        SetComponents(output.descriptor, MakeImageComponentSet({ ImageComponent::Red, ImageComponent::Green, ImageComponent::Blue }));
        output.descriptor.precision = SemanticField<LogicalPrecision>::Known(LogicalPrecision::Float16);
        for (const auto& pair : inputs) {
            if (pair.second->descriptor.spatial.state == KnowledgeState::Known) output.descriptor.spatial = pair.second->descriptor.spatial;
            MergeDiagnostics(output, *pair.second);
        }
        break;
    default:
        // An unimplemented metadata rule preserves declared shape without
        // inventing color identity, extent, availability, or a processing failure.
        break;
    }
    const auto source = context.sourceDescriptors.find(GraphOutputIdentity(node.id, socketId));
    if (source != context.sourceDescriptors.end()) {
        if (node.kind == NodeKind::Layer) {
            if (output.descriptor.logicalType == LogicalValueType::ColorImage) {
                output.descriptor.color = source->second.color;
                output.descriptor.transfer = source->second.transfer;
                output.descriptor.reference = source->second.reference;
                output.descriptor.range = source->second.range;
            }
        } else {
            auto previous = output.descriptor;
            output.descriptor = source->second;
            if (output.descriptor.spatial.state != KnowledgeState::Known) output.descriptor.spatial = previous.spatial;
            if (output.descriptor.presentImageComponents.state != KnowledgeState::Known) {
                output.descriptor.channels = previous.channels;
                output.descriptor.presentImageComponents = previous.presentImageComponents;
                output.descriptor.alpha = previous.alpha;
            }
        }
    }
    if (node.kind == NodeKind::Image)
        output.descriptor.precision = SemanticField<LogicalPrecision>::Known(LogicalPrecision::UInt8);
    if (rendered && IsImageLike(output.descriptor.logicalType)) {
        output.descriptor.precision = SemanticField<LogicalPrecision>::Known(
            node.kind == NodeKind::CustomMask ||
            output.descriptor.logicalType == LogicalValueType::ComplexSpectrum ||
            output.descriptor.logicalType == LogicalValueType::SpectrumMagnitude ||
            output.descriptor.logicalType == LogicalValueType::SpectrumPhase
                ? LogicalPrecision::Float32 : LogicalPrecision::Float16);
    }
    if (output.descriptor.logicalType == LogicalValueType::Channel && output.descriptor.channels.state == KnowledgeState::NotApplicable) {
        output.descriptor.channels = SemanticField<ChannelDescriptor>::Unknown();
    }
    if (output.descriptor.logicalType != LogicalValueType::Invalid)
        output.descriptor.provenance = SemanticField<ProvenanceDescriptor>::Known({
            ProvenanceKind::Derived, output.componentOrigin, GraphOutputIdentity(node.id, socketId) });
    return output;
}
} // namespace EditorNodeGraph::OutputRules
