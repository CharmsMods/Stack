#include "Editor/EditorModule.h"

#include "Editor/Internal/EditorGraphSnapshotLookup.h"
#include "Editor/Layers/LayerBase.h"
#include "Editor/Timeline/TimelineFrameProducer.h"
#include "Renderer/MaskRenderTypes.h"
#include "NodeMath/ChannelImageSemantics.h"
#include "NodeMath/DescriptorSerialization.h"
#include "NodeMath/SemanticSpine.h"
#include "NodeMath/FirstClassValue.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

std::string SemanticNodeIdentity(int nodeId) {
    return "node-" + std::to_string(nodeId);
}

std::string SemanticLinkIdentity(const RenderGraphLink& link) {
    return "link-" + std::to_string(link.fromNodeId) + "-" + link.fromSocketId +
        "-to-" + std::to_string(link.toNodeId) + "-" + link.toSocketId;
}

std::string SemanticLinkIdentity(const EditorNodeGraph::Link& link) {
    return "link-" + std::to_string(link.fromNodeId) + "-" + link.fromSocketId +
        "-to-" + std::to_string(link.toNodeId) + "-" + link.toSocketId;
}

bool HasValidImageDescriptor(const Stack::NodeMath::ValueDescriptor& descriptor) {
    return descriptor.logicalType == Stack::NodeMath::LogicalValueType::ColorImage &&
        Stack::NodeMath::ValidateDescriptor(descriptor).empty();
}

Stack::NodeMath::ValueDescriptor UnknownLiveImageDescriptor(const std::string& operation) {
    Stack::NodeMath::ValueDescriptor descriptor =
        Stack::NodeMath::MakeUnknownDescriptor(Stack::NodeMath::LogicalValueType::ColorImage);
    descriptor.provenance = Stack::NodeMath::SemanticField<Stack::NodeMath::ProvenanceDescriptor>::Known({
        Stack::NodeMath::ProvenanceKind::Generated, {}, operation
    });
    return descriptor;
}

Stack::NodeMath::ValueDescriptor UnknownLiveChannelDescriptor(
    std::string role,
    const std::string& operation) {
    Stack::NodeMath::ValueDescriptor descriptor =
        Stack::NodeMath::MakeUnknownDescriptor(
            Stack::NodeMath::LogicalValueType::Channel);
    if (role.empty()) {
        role = "value";
    } else if (role == "r") {
        role = "R";
    } else if (role == "g") {
        role = "G";
    } else if (role == "b") {
        role = "B";
    } else if (role == "a") {
        role = "A";
    }
    descriptor.channels =
        Stack::NodeMath::SemanticField<
            Stack::NodeMath::ChannelDescriptor>::Known({
                Stack::NodeMath::ChannelLayout::Gray,
                { std::move(role) }
            });
    descriptor.units =
        Stack::NodeMath::SemanticField<
            Stack::NodeMath::UnitDescriptor>::Known({
                Stack::NodeMath::UnitKind::Unitless,
                {}
            });
    descriptor.provenance =
        Stack::NodeMath::SemanticField<
            Stack::NodeMath::ProvenanceDescriptor>::Known({
                Stack::NodeMath::ProvenanceKind::Derived,
                {},
                operation
            });
    return descriptor;
}

void DeclareRawSceneOutput(
    Stack::NodeMath::ValueDescriptor& descriptor,
    Raw::RawWorkingSpace workingSpace,
    const char* operation) {
    descriptor.color =
        Stack::NodeMath::SemanticField<Stack::NodeMath::ColorIdentity>::Known({
            workingSpace == Raw::RawWorkingSpace::LinearRec2020D65
                ? "rec2020-d65"
                : "srgb-d65",
            {},
            Stack::NodeMath::ColorRelation::Standard
        });
    descriptor.transfer =
        Stack::NodeMath::SemanticField<Stack::NodeMath::TransferDescriptor>::Known({
            Stack::NodeMath::TransferKind::Linear, 0.0, {}
        });
    descriptor.reference =
        Stack::NodeMath::SemanticField<Stack::NodeMath::ReferenceState>::Known(
            Stack::NodeMath::ReferenceState::Scene);
    descriptor.alpha =
        Stack::NodeMath::SemanticField<Stack::NodeMath::AlphaMode>::Known(
            Stack::NodeMath::AlphaMode::Opaque);
    descriptor.precision =
        Stack::NodeMath::SemanticField<Stack::NodeMath::LogicalPrecision>::Known(
            Stack::NodeMath::LogicalPrecision::Float32);
    descriptor.provenance =
        Stack::NodeMath::SemanticField<Stack::NodeMath::ProvenanceDescriptor>::Known({
            Stack::NodeMath::ProvenanceKind::RawDeveloped, {}, operation
        });
}

void DeclareRawDisplayOutput(
    Stack::NodeMath::ValueDescriptor& descriptor,
    bool encodeSrgbOutput,
    const char* operation) {
    descriptor.color =
        Stack::NodeMath::SemanticField<Stack::NodeMath::ColorIdentity>::Known({
            "srgb-d65", {}, Stack::NodeMath::ColorRelation::Standard
        });
    descriptor.transfer =
        Stack::NodeMath::SemanticField<Stack::NodeMath::TransferDescriptor>::Known({
            encodeSrgbOutput
                ? Stack::NodeMath::TransferKind::Srgb
                : Stack::NodeMath::TransferKind::Linear,
            0.0,
            {}
        });
    descriptor.reference =
        Stack::NodeMath::SemanticField<Stack::NodeMath::ReferenceState>::Known(
            Stack::NodeMath::ReferenceState::Display);
    descriptor.alpha =
        Stack::NodeMath::SemanticField<Stack::NodeMath::AlphaMode>::Known(
            Stack::NodeMath::AlphaMode::Opaque);
    descriptor.range =
        Stack::NodeMath::SemanticField<Stack::NodeMath::NumericRange>::Known({
            0.0, 1.0, false, false, Stack::NodeMath::NonFinitePolicy::Forbidden
        });
    descriptor.precision =
        Stack::NodeMath::SemanticField<Stack::NodeMath::LogicalPrecision>::Known(
            Stack::NodeMath::LogicalPrecision::Float32);
    descriptor.provenance =
        Stack::NodeMath::SemanticField<Stack::NodeMath::ProvenanceDescriptor>::Known({
            Stack::NodeMath::ProvenanceKind::RawDeveloped, {}, operation
        });
}

bool HasSemanticImageOutput(RenderGraphNodeKind kind) {
    switch (kind) {
    case RenderGraphNodeKind::Image:
    case RenderGraphNodeKind::RawDevelopment:
    case RenderGraphNodeKind::RawDecode:
    case RenderGraphNodeKind::RawDevelop:
    case RenderGraphNodeKind::RawDetailAutoMask:
    case RenderGraphNodeKind::RawDetailFusion:
    case RenderGraphNodeKind::HdrMerge:
    case RenderGraphNodeKind::Mfsr:
    case RenderGraphNodeKind::RawProjectSourceSet:
    case RenderGraphNodeKind::Lut:
    case RenderGraphNodeKind::Layer:
    case RenderGraphNodeKind::Output:
    case RenderGraphNodeKind::Mix:
    case RenderGraphNodeKind::ImageGenerator:
    case RenderGraphNodeKind::ChannelCombine:
    case RenderGraphNodeKind::DataMath:
    case RenderGraphNodeKind::TechnicalImage:
    case RenderGraphNodeKind::SpectrumView:
    case RenderGraphNodeKind::Reformat:
        return true;
    default:
        return false;
    }
}

RenderMaskGeneratorKind ToRenderMaskKind(EditorNodeGraph::MaskGeneratorKind kind) {
    switch (kind) {
        case EditorNodeGraph::MaskGeneratorKind::Solid: return RenderMaskGeneratorKind::Solid;
        case EditorNodeGraph::MaskGeneratorKind::LinearGradient: return RenderMaskGeneratorKind::LinearGradient;
        case EditorNodeGraph::MaskGeneratorKind::RadialGradient: return RenderMaskGeneratorKind::RadialGradient;
        case EditorNodeGraph::MaskGeneratorKind::Noise: return RenderMaskGeneratorKind::Noise;
    }
    return RenderMaskGeneratorKind::Solid;
}

RenderMaskSettings ToRenderMaskSettings(const EditorNodeGraph::MaskGeneratorSettings& settings) {
    RenderMaskSettings result;
    result.value = settings.value;
    result.angle = settings.angle;
    result.offset = settings.offset;
    result.scale = settings.scale;
    result.centerX = settings.centerX;
    result.centerY = settings.centerY;
    result.radius = settings.radius;
    result.feather = settings.feather;
    result.invert = settings.invert;
    return result;
}

RenderMixBlendMode ToRenderMixBlendMode(EditorNodeGraph::MixBlendMode mode) {
    switch (mode) {
        case EditorNodeGraph::MixBlendMode::Normal: return RenderMixBlendMode::Normal;
        case EditorNodeGraph::MixBlendMode::Average: return RenderMixBlendMode::Average;
        case EditorNodeGraph::MixBlendMode::Add: return RenderMixBlendMode::Add;
        case EditorNodeGraph::MixBlendMode::Multiply: return RenderMixBlendMode::Multiply;
        case EditorNodeGraph::MixBlendMode::Screen: return RenderMixBlendMode::Screen;
        case EditorNodeGraph::MixBlendMode::StraightSourceOver: return RenderMixBlendMode::StraightSourceOver;
        case EditorNodeGraph::MixBlendMode::PremultipliedSourceOver: return RenderMixBlendMode::PremultipliedSourceOver;
    }
    return RenderMixBlendMode::Normal;
}

RenderMaskUtilityKind ToRenderMaskUtilityKind(EditorNodeGraph::MaskUtilityKind kind) {
    switch (kind) {
        case EditorNodeGraph::MaskUtilityKind::Invert: return RenderMaskUtilityKind::Invert;
        case EditorNodeGraph::MaskUtilityKind::Levels: return RenderMaskUtilityKind::Levels;
        case EditorNodeGraph::MaskUtilityKind::Threshold: return RenderMaskUtilityKind::Threshold;
    }
    return RenderMaskUtilityKind::Invert;
}

RenderMaskCombineMode ToRenderMaskCombineMode(EditorNodeGraph::MaskCombineMode mode) {
    switch (mode) {
        case EditorNodeGraph::MaskCombineMode::Add: return RenderMaskCombineMode::Add;
        case EditorNodeGraph::MaskCombineMode::Subtract: return RenderMaskCombineMode::Subtract;
        case EditorNodeGraph::MaskCombineMode::Intersect: return RenderMaskCombineMode::Intersect;
        case EditorNodeGraph::MaskCombineMode::Exclude: return RenderMaskCombineMode::Exclude;
    }
    return RenderMaskCombineMode::Intersect;
}

RenderCustomMaskObjectType ToRenderCustomMaskObjectType(EditorNodeGraph::CustomMaskObjectType type) {
    switch (type) {
        case EditorNodeGraph::CustomMaskObjectType::Rectangle: return RenderCustomMaskObjectType::Rectangle;
        case EditorNodeGraph::CustomMaskObjectType::Ellipse: return RenderCustomMaskObjectType::Ellipse;
        case EditorNodeGraph::CustomMaskObjectType::Polygon: return RenderCustomMaskObjectType::Polygon;
        case EditorNodeGraph::CustomMaskObjectType::FreeformPath: return RenderCustomMaskObjectType::FreeformPath;
    }
    return RenderCustomMaskObjectType::Rectangle;
}

RenderCustomMaskOperation ToRenderCustomMaskOperation(EditorNodeGraph::CustomMaskOperation operation) {
    switch (operation) {
        case EditorNodeGraph::CustomMaskOperation::Add: return RenderCustomMaskOperation::Add;
        case EditorNodeGraph::CustomMaskOperation::Subtract: return RenderCustomMaskOperation::Subtract;
        case EditorNodeGraph::CustomMaskOperation::Intersect: return RenderCustomMaskOperation::Intersect;
        case EditorNodeGraph::CustomMaskOperation::Exclude: return RenderCustomMaskOperation::Exclude;
    }
    return RenderCustomMaskOperation::Add;
}

RenderCustomMaskPayload ToRenderCustomMaskPayload(const EditorNodeGraph::CustomMaskPayload& payload) {
    RenderCustomMaskPayload result;
    result.width = std::max(1, payload.width);
    result.height = std::max(1, payload.height);
    result.rasterLayer = payload.rasterLayer;
    result.invert = payload.invert;
    result.blurRadius = payload.blurRadius;
    result.expandContract = payload.expandContract;
    result.objects.reserve(payload.objects.size());
    for (const EditorNodeGraph::CustomMaskObject& object : payload.objects) {
        RenderCustomMaskObject renderObject;
        renderObject.id = object.id;
        renderObject.type = ToRenderCustomMaskObjectType(object.type);
        renderObject.operation = ToRenderCustomMaskOperation(object.operation);
        renderObject.enabled = object.enabled;
        renderObject.invert = object.invert;
        renderObject.strength = object.strength;
        renderObject.feather = object.feather;
        renderObject.blur = object.blur;
        renderObject.points.reserve(object.points.size());
        for (const EditorNodeGraph::Vec2& point : object.points) {
            renderObject.points.push_back(RenderCustomMaskPoint{ point.x, point.y });
        }
        result.objects.push_back(std::move(renderObject));
    }
    return result;
}

RenderImageToMaskKind ToRenderImageToMaskKind(EditorNodeGraph::ImageToMaskKind kind) {
    switch (kind) {
        case EditorNodeGraph::ImageToMaskKind::Luminance: return RenderImageToMaskKind::Luminance;
        case EditorNodeGraph::ImageToMaskKind::SampledRange: return RenderImageToMaskKind::SampledRange;
    }
    return RenderImageToMaskKind::Luminance;
}

RenderImageGeneratorKind ToRenderImageGeneratorKind(EditorNodeGraph::ImageGeneratorKind kind) {
    switch (kind) {
        case EditorNodeGraph::ImageGeneratorKind::SolidColor: return RenderImageGeneratorKind::SolidColor;
        case EditorNodeGraph::ImageGeneratorKind::ColorGradient: return RenderImageGeneratorKind::ColorGradient;
        case EditorNodeGraph::ImageGeneratorKind::Square: return RenderImageGeneratorKind::Square;
        case EditorNodeGraph::ImageGeneratorKind::Circle: return RenderImageGeneratorKind::Circle;
        case EditorNodeGraph::ImageGeneratorKind::Text: return RenderImageGeneratorKind::Text;
    }
    return RenderImageGeneratorKind::SolidColor;
}

RenderMaskUtilitySettings ToRenderMaskUtilitySettings(const EditorNodeGraph::MaskUtilitySettings& settings) {
    RenderMaskUtilitySettings result;
    result.blackPoint = settings.blackPoint;
    result.whitePoint = settings.whitePoint;
    result.gamma = settings.gamma;
    result.threshold = settings.threshold;
    result.softness = settings.softness;
    result.enabled = settings.enabled;
    result.invert = settings.invert;
    return result;
}

RenderImageToMaskSettings ToRenderImageToMaskSettings(const EditorNodeGraph::ImageToMaskSettings& settings) {
    RenderImageToMaskSettings result;
    result.low = settings.low;
    result.high = settings.high;
    result.softness = settings.softness;
    result.invert = settings.invert;
    result.sampleCount = std::clamp(settings.sampleCount, 1, 5);
    result.sampleRgb[0] = settings.sampleRgb[0];
    result.sampleRgb[1] = settings.sampleRgb[1];
    result.sampleRgb[2] = settings.sampleRgb[2];
    result.sampleLuma = settings.sampleLuma;
    for (int i = 0; i < 4; ++i) {
        result.extraSampleRgb[i][0] = settings.extraSampleRgb[i][0];
        result.extraSampleRgb[i][1] = settings.extraSampleRgb[i][1];
        result.extraSampleRgb[i][2] = settings.extraSampleRgb[i][2];
        result.extraSampleLuma[i] = settings.extraSampleLuma[i];
    }
    result.sampleU = settings.sampleU;
    result.sampleV = settings.sampleV;
    result.toneSimilarity = settings.toneSimilarity;
    result.colorSimilarity = settings.colorSimilarity;
    result.regionRadius = settings.regionRadius;
    result.regionFeather = settings.regionFeather;
    result.edgeSensitivity = settings.edgeSensitivity;
    result.localCoherence = settings.localCoherence;
    return result;
}

RenderImageGeneratorSettings ToRenderImageGeneratorSettings(const EditorNodeGraph::ImageGeneratorSettings& settings) {
    RenderImageGeneratorSettings result;
    for (int i = 0; i < 4; ++i) {
        result.colorA[i] = settings.colorA[i];
        result.colorB[i] = settings.colorB[i];
    }
    result.angle = settings.angle;
    result.offset = settings.offset;
    result.text = settings.text;
    result.fontSize = settings.fontSize;
    result.textBackdropBlur = settings.textBackdropBlur;
    result.textBackdropOpacity = settings.textBackdropOpacity;
    result.textBackdropPadding = settings.textBackdropPadding;
    return result;
}

RenderFrequencyResponseSettings ToRenderFrequencyResponseSettings(
    const EditorNodeGraph::FrequencyResponseSettings& settings) {
    RenderFrequencyResponseSettings result;
    result.mode = static_cast<RenderFrequencyFilterMode>(settings.mode);
    result.profile = static_cast<RenderFrequencyTransitionProfile>(settings.profile);
    result.lowCutoff = settings.lowCutoff;
    result.highCutoff = settings.highCutoff;
    result.transitionWidth = settings.transitionWidth;
    result.butterworthOrder = settings.butterworthOrder;
    result.notches.reserve(settings.notches.size());
    for (const EditorNodeGraph::FrequencyNotch& notch : settings.notches) {
        result.notches.push_back({
            notch.id,
            notch.frequency,
            notch.directionDegrees,
            notch.width
        });
    }
    return result;
}

} // namespace

std::vector<std::shared_ptr<LayerBase>> EditorModule::BuildGraphRenderLayers() const {
    std::vector<std::shared_ptr<LayerBase>> renderLayers;
    for (int index : m_NodeGraph.GetRenderLayerIndexPath()) {
        if (index >= 0 && index < static_cast<int>(m_Layers.size())) {
            renderLayers.push_back(m_Layers[index]);
        }
    }
    return renderLayers;
}

std::vector<RenderLayerStep> EditorModule::BuildGraphRenderSteps() const {
    std::vector<RenderLayerStep> steps;
    for (int nodeId : m_NodeGraph.GetRenderLayerNodePath()) {
        const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
        if (!node || node->kind != EditorNodeGraph::NodeKind::Layer ||
            node->layerIndex < 0 || node->layerIndex >= static_cast<int>(m_Layers.size())) {
            continue;
        }

        RenderLayerStep step;
        step.layer = m_Layers[node->layerIndex];
        if (const EditorNodeGraph::Link* maskLink = m_NodeGraph.FindAnyInputLink(node->id, EditorNodeGraph::kMaskInputSocketId)) {
            const EditorNodeGraph::Node* maskNode = m_NodeGraph.FindNode(maskLink->fromNodeId);
            if (maskNode && maskNode->kind == EditorNodeGraph::NodeKind::MaskGenerator) {
                step.maskNodeId = maskNode->id;
            }
        }
        steps.push_back(std::move(step));
    }
    return steps;
}

std::vector<RenderMaskSource> EditorModule::BuildGraphRenderMasks() const {
    std::vector<int> usedMaskNodeIds;
    for (const RenderLayerStep& step : BuildGraphRenderSteps()) {
        if (step.maskNodeId > 0 &&
            std::find(usedMaskNodeIds.begin(), usedMaskNodeIds.end(), step.maskNodeId) == usedMaskNodeIds.end()) {
            usedMaskNodeIds.push_back(step.maskNodeId);
        }
    }

    std::vector<RenderMaskSource> masks;
    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::MaskGenerator) {
            continue;
        }
        if (std::find(usedMaskNodeIds.begin(), usedMaskNodeIds.end(), node.id) == usedMaskNodeIds.end()) {
            continue;
        }

        RenderMaskSource mask;
        mask.nodeId = node.id;
        mask.kind = ToRenderMaskKind(node.maskKind);
        mask.settings = ToRenderMaskSettings(node.maskSettings);
        masks.push_back(mask);
    }
    return masks;
}

RenderGraphSnapshot EditorModule::BuildGraphSnapshot() const {
    return BuildGraphSnapshotForTimelineFrame(m_TimelineUi.currentFrame);
}

RenderGraphSnapshot EditorModule::BuildGraphSnapshotForTimelineFrame(int timelineFrame) const {
    RenderGraphSnapshot snapshot;
    EditorNodeGraph::Graph expandedGraph;
    const EditorNodeGraph::Graph* renderGraph = &m_NodeGraph;
    const bool hasCompoundNode = std::any_of(
        m_NodeGraph.GetNodes().begin(),
        m_NodeGraph.GetNodes().end(),
        [](const EditorNodeGraph::Node& node) {
            return node.kind == EditorNodeGraph::NodeKind::Compound;
        });
    if (hasCompoundNode) {
        EditorNodeGraph::CompoundExpansionResult expansion;
        if (!m_NodeGraph.ExpandAllCompoundNodes(expandedGraph, &expansion)) {
            Stack::NodeMath::Diagnostic diagnostic;
            diagnostic.ruleId = "NMR-COMPOUND-UNRESOLVED";
            diagnostic.stage = Stack::NodeMath::DiagnosticStage::Lowering;
            diagnostic.severity = Stack::NodeMath::DiagnosticSeverity::HardError;
            diagnostic.authoredSourceIdentity = "graph";
            diagnostic.affectedIdentity = expansion.authoredCompoundNodeIds.empty()
                ? "compound"
                : SemanticNodeIdentity(expansion.authoredCompoundNodeIds.back());
            diagnostic.message = expansion.error.empty()
                ? "A compound node could not be expanded for rendering."
                : expansion.error;
            diagnostic.suggestedRepair =
                "Restore the exact embedded definition or choose a deliberate replacement version.";
            snapshot.semanticDiagnostics.push_back(std::move(diagnostic));
            return snapshot;
        }
        renderGraph = &expandedGraph;
    }
    const EditorNodeGraph::Graph& graph = *renderGraph;
    const std::vector<EditorNodeGraph::Node>& graphNodes = graph.GetNodes();
    const std::vector<EditorNodeGraph::Link>& graphLinks = graph.GetLinks();
    const EditorGraphSnapshotInternal::Lookup graphLookup(graph);
    snapshot.outputNodeId = graph.ResolvePreviewOutputNodeId();
    snapshot.executionInspectionEnabled = m_ShowGraphPerformancePopup;
    const Stack::Timeline::TimelineFrameEvaluation frameEvaluation =
        Stack::Timeline::BuildTimelineFrameEvaluation(
            m_TimelineAnimation,
            Stack::Timeline::NormalizeTimelineFrameRequest(
                timelineFrame,
                m_TimelineUi.durationFrames,
                m_TimelineUi.framesPerSecond));
    Stack::Timeline::FrameEvaluationContext frameContext = frameEvaluation.frameContext;
    if (m_TimelineUi.liveEditPreviewFrame == frameEvaluation.request.frame) {
        for (const Stack::Timeline::AnimatableParameterTarget& target : m_TimelineUi.liveEditPreviewTargets) {
            Stack::Timeline::RemoveFrameParameterValue(frameContext, target);
        }
    }

    snapshot.nodes.reserve(graphNodes.size());
    for (const EditorNodeGraph::Node& node : graphNodes) {
        if (!node.definitionResolved) {
            continue;
        }
        RenderGraphNode renderNode;
        renderNode.nodeId = node.id;
        renderNode.requestRevision = std::max<std::uint64_t>(1, GetNodeDirtyGeneration(node.id));
        renderNode.definitionId = node.definitionId;
        renderNode.definitionVersion = node.definitionVersion;
        renderNode.definitionHash = node.definitionHash;
        switch (node.kind) {
            case EditorNodeGraph::NodeKind::Image:
                renderNode.kind = RenderGraphNodeKind::Image;
                renderNode.image = BuildRenderImagePayload(node.image);
                break;
            case EditorNodeGraph::NodeKind::RawSource:
                renderNode.kind = RenderGraphNodeKind::RawSource;
                renderNode.rawSource.sourcePath = node.rawSource.sourcePath;
                renderNode.rawSource.metadata = node.rawSource.metadata;
                break;
            case EditorNodeGraph::NodeKind::RawDevelopment:
                renderNode.kind = RenderGraphNodeKind::RawDevelopment;
                renderNode.rawDevelopment.recipe = node.rawDevelopment.recipe;
                break;
            case EditorNodeGraph::NodeKind::RawNeuralDenoise:
                renderNode.kind = RenderGraphNodeKind::RawNeuralDenoise;
                renderNode.rawNeuralDenoise.settings = node.rawNeuralDenoise.settings;
                break;
            case EditorNodeGraph::NodeKind::RawDecode:
                renderNode.kind = RenderGraphNodeKind::RawDecode;
                renderNode.rawDecode.settings = node.rawDecode.settings;
                break;
            case EditorNodeGraph::NodeKind::RawDevelop:
                renderNode.kind = RenderGraphNodeKind::RawDevelop;
                renderNode.rawDevelop.settings = node.rawDevelop.settings;
                renderNode.rawDevelop.scenePrepEnabled = true;
                renderNode.rawDevelop.scenePrepSettings = node.rawDevelop.scenePrepSettings;
                renderNode.rawDevelop.integratedToneEnabled = true;
                renderNode.rawDevelop.integratedToneLayerJson = node.rawDevelop.integratedToneLayerJson;
                break;
            case EditorNodeGraph::NodeKind::RawDetailAutoMask:
                renderNode.kind = RenderGraphNodeKind::RawDetailAutoMask;
                renderNode.rawDetailAutoMask.settings = node.rawDetailAutoMask.settings;
                break;
            case EditorNodeGraph::NodeKind::RawDetailFusion:
                renderNode.kind = RenderGraphNodeKind::RawDetailFusion;
                renderNode.rawDetailFusion.settings = node.rawDetailFusion.settings;
                break;
            case EditorNodeGraph::NodeKind::HdrMerge:
                renderNode.kind = RenderGraphNodeKind::HdrMerge;
                renderNode.hdrMerge.settings = node.hdrMerge.settings;
                break;
            case EditorNodeGraph::NodeKind::Mfsr:
                renderNode.kind = RenderGraphNodeKind::Mfsr;
                renderNode.mfsr.settings = node.mfsr.settings;
                renderNode.mfsr.diagnostics = node.mfsr.diagnostics;
                renderNode.mfsr.cacheKey = node.mfsr.cacheKey;
                renderNode.mfsr.hasPlaceholderCachedOutput = node.mfsr.hasPlaceholderCachedOutput;
                renderNode.mfsr.placeholderStatus = node.mfsr.placeholderStatus;
                renderNode.mfsr.errorMessage = node.mfsr.errorMessage;
                break;
            case EditorNodeGraph::NodeKind::RawProjectFrame:
                renderNode.kind = RenderGraphNodeKind::RawProjectSourceSet;
                renderNode.rawProjectSourceSet.sourceSetId =
                    node.rawProjectFrame.sourceSetId;
                renderNode.rawProjectSourceSet.unavailableStatus =
                    "Embedded MFD mosaic frame; decoded lazily in RAW Lab.";
                renderNode.rawProjectSourceSet.quarantined =
                    node.rawProjectFrame.quarantined;
                break;
            case EditorNodeGraph::NodeKind::MultiFrameDenoise:
                renderNode.kind = RenderGraphNodeKind::RawProjectSourceSet;
                renderNode.rawProjectSourceSet.sourceSetId =
                    node.multiFrameDenoise.sourceSetId;
                renderNode.rawProjectSourceSet.unavailableStatus =
                    node.multiFrameDenoise.presentationStatus;
                renderNode.rawProjectSourceSet.quarantined =
                    node.multiFrameDenoise.quarantined;
                if (m_ActiveRawProjectSnapshot) {
                    const Stack::Project::MultiFrameSourceSet* sourceSet =
                        Stack::Project::FindSourceSet(
                            *m_ActiveRawProjectSnapshot,
                            node.multiFrameDenoise.sourceSetId);
                    const bool adopted =
                        sourceSet != nullptr &&
                        m_MfdAdoptedRawResult &&
                        m_MfdAdoptedRawResult->rawData &&
                        m_MfdAdoptedRawResult->projectId ==
                            m_ActiveRawProjectSnapshot->projectId &&
                        m_MfdAdoptedRawResult->sourceSetId ==
                            sourceSet->sourceSetId &&
                        m_MfdAdoptedRawResult->inputRevision ==
                            m_ActiveRawProjectSnapshot->mfdInputRevision;
                    if (adopted) {
                        const nlohmann::json storedRecipe =
                            sourceSet->settings.value(
                                "sharedPostMfdRecipe",
                                nlohmann::json::object());
                        Stack::RawRecipe::RawDevelopmentRecipe recipe =
                            storedRecipe.is_object() &&
                                storedRecipe.contains("rawRecipeVersion")
                            ? Stack::RawRecipe::DeserializeRecipe(
                                  storedRecipe)
                            : Stack::RawRecipe::MakeDefaultRecipe(
                                  "mfd://" +
                                      m_ActiveRawProjectSnapshot->projectId +
                                      "/" + sourceSet->sourceSetId,
                                  sourceSet->name + " developed result");
                        recipe.technical.processingVersion =
                            Raw::RawProcessingVersion::TruthfulV1;
                        recipe.technical.mosaicDenoise.enabled = false;
                        recipe.source.sourcePath =
                            "mfd://" +
                            m_ActiveRawProjectSnapshot->projectId + "/" +
                            sourceSet->sourceSetId;
                        recipe.source.relativePathKey =
                            recipe.source.sourcePath;
                        recipe.source.fingerprint = std::to_string(
                            m_MfdAdoptedRawResult->contentHash);
                        recipe.source.fileSizeBytes =
                            static_cast<std::uint64_t>(
                                m_MfdAdoptedRawResult->rawData
                                    ->normalizedMosaicBuffer->size()) *
                            sizeof(float);
                        recipe.source.modifiedTimeTicks =
                            static_cast<std::int64_t>(
                                m_MfdAdoptedRawResult->inputRevision);
                        recipe.source.displayName =
                            sourceSet->name + " developed result";
                        if (sourceSet->settings.value(
                                "viewTransformPlacement",
                                std::string("internal")) == "graph") {
                            recipe.viewTransform.layerJson["enabled"] =
                                false;
                        }
                        renderNode.rawDevelopment.recipe =
                            std::move(recipe);
                        renderNode.rawDevelopment.embeddedRawData =
                            m_MfdAdoptedRawResult->rawData;
                        renderNode.rawProjectSourceSet.inputRevision =
                            m_MfdAdoptedRawResult->inputRevision;
                        renderNode.rawProjectSourceSet.postRecipeRevision =
                            m_ActiveRawProjectSnapshot
                                ->postRecipeRevision;
                        renderNode.rawProjectSourceSet.contentHash =
                            m_MfdAdoptedRawResult->contentHash;
                        renderNode.rawProjectSourceSet.resultAvailable =
                            true;
                    }
                }
                break;
            case EditorNodeGraph::NodeKind::RawProjectSourceSet:
                renderNode.kind = RenderGraphNodeKind::RawProjectSourceSet;
                renderNode.rawProjectSourceSet.sourceSetId =
                    node.rawProjectSourceSet.sourceSetId;
                renderNode.rawProjectSourceSet.unavailableStatus =
                    node.rawProjectSourceSet.presentationStatus;
                renderNode.rawProjectSourceSet.quarantined =
                    node.rawProjectSourceSet.quarantined;
                break;
            case EditorNodeGraph::NodeKind::Lut:
                renderNode.kind = RenderGraphNodeKind::Lut;
                renderNode.lut = node.lut;
                break;
            case EditorNodeGraph::NodeKind::Layer:
                renderNode.kind = RenderGraphNodeKind::Layer;
                if (node.layerIndex >= 0 && node.layerIndex < static_cast<int>(m_Layers.size()) && m_Layers[node.layerIndex]) {
                    renderNode.layerJson = m_Layers[node.layerIndex]->Serialize();
                    Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(
                        frameContext,
                        node.id,
                        node.layerType,
                        renderNode.layerJson);
                }
                break;
            case EditorNodeGraph::NodeKind::Output:
                renderNode.kind = RenderGraphNodeKind::Output;
                renderNode.outputChannelViewMode =
                    node.outputSettings.channelViewMode;
                break;
            case EditorNodeGraph::NodeKind::MaskGenerator:
                renderNode.kind = RenderGraphNodeKind::MaskGenerator;
                renderNode.maskKind = ToRenderMaskKind(node.maskKind);
                renderNode.maskSettings = ToRenderMaskSettings(node.maskSettings);
                break;
            case EditorNodeGraph::NodeKind::MaskCombine:
                renderNode.kind = RenderGraphNodeKind::MaskCombine;
                renderNode.maskCombineMode = ToRenderMaskCombineMode(node.maskCombineMode);
                break;
            case EditorNodeGraph::NodeKind::CustomMask:
                renderNode.kind = RenderGraphNodeKind::CustomMask;
                renderNode.customMask = ToRenderCustomMaskPayload(node.customMask);
                break;
            case EditorNodeGraph::NodeKind::MaskUtility:
                renderNode.kind = RenderGraphNodeKind::MaskUtility;
                renderNode.maskUtilityKind = ToRenderMaskUtilityKind(node.maskUtilityKind);
                renderNode.maskUtilitySettings = ToRenderMaskUtilitySettings(node.maskUtilitySettings);
                break;
            case EditorNodeGraph::NodeKind::ImageToMask:
                renderNode.kind = RenderGraphNodeKind::ImageToMask;
                renderNode.imageToMaskKind = ToRenderImageToMaskKind(node.imageToMaskKind);
                renderNode.imageToMaskSettings = ToRenderImageToMaskSettings(node.imageToMaskSettings);
                break;
            case EditorNodeGraph::NodeKind::ImageGenerator:
                renderNode.kind = RenderGraphNodeKind::ImageGenerator;
                renderNode.imageGeneratorKind = ToRenderImageGeneratorKind(node.imageGeneratorKind);
                renderNode.imageGeneratorSettings = ToRenderImageGeneratorSettings(node.imageGeneratorSettings);
                break;
            case EditorNodeGraph::NodeKind::Mix:
                renderNode.kind = RenderGraphNodeKind::Mix;
                renderNode.mixBlendMode = ToRenderMixBlendMode(node.mixBlendMode);
                renderNode.mixFactor = node.mixFactor;
                break;
            case EditorNodeGraph::NodeKind::DataMath:
                renderNode.kind = RenderGraphNodeKind::DataMath;
                renderNode.dataMathMode = static_cast<RenderDataMathMode>(node.dataMathMode);
                renderNode.dataMathSettings.constantA = node.dataMathSettings.constantA;
                renderNode.dataMathSettings.constantB = node.dataMathSettings.constantB;
                renderNode.dataMathSettings.minValue = node.dataMathSettings.minValue;
                renderNode.dataMathSettings.maxValue = node.dataMathSettings.maxValue;
                renderNode.dataMathSettings.outMin = node.dataMathSettings.outMin;
                renderNode.dataMathSettings.outMax = node.dataMathSettings.outMax;
                break;
            case EditorNodeGraph::NodeKind::TechnicalImage:
                renderNode.kind = RenderGraphNodeKind::TechnicalImage;
                renderNode.technicalImageOperation = node.technicalImageSettings.operation;
                renderNode.technicalExposureValue = node.technicalImageSettings.exposureValue;
                if (node.technicalImageSettings.operation == Stack::NodeMath::TechnicalImageOperation::Exposure) {
                    double connectedExposure = 0.0;
                    if (graphLookup.TryResolveUniformScalarInput(
                            node.id,
                            EditorNodeGraph::kExposureValueInputSocketId,
                            connectedExposure)) {
                        renderNode.technicalExposureValue = static_cast<float>(connectedExposure);
                    }
                }
                break;
            case EditorNodeGraph::NodeKind::FrequencyFilter: {
                renderNode.kind = RenderGraphNodeKind::FrequencyFilter;
                renderNode.frequencyFilterSettings.localResponse =
                    ToRenderFrequencyResponseSettings(node.frequencyFilterSettings.localResponse);
                renderNode.frequencyFilterSettings.edgePolicy =
                    static_cast<RenderFrequencyEdgePolicy>(node.frequencyFilterSettings.edgePolicy);
                renderNode.frequencyFilterSettings.strength =
                    node.frequencyFilterSettings.strength;
                double connected = 0.0;
                if (graphLookup.TryResolveUniformScalarInput(
                        node.id,
                        EditorNodeGraph::ParameterInputSocketId(
                            EditorNodeGraph::kStrengthParameterId),
                        connected)) {
                    renderNode.frequencyFilterSettings.strength =
                        std::clamp(static_cast<float>(connected), 0.0f, 1.0f);
                }
                break;
            }
            case EditorNodeGraph::NodeKind::FrequencyResponse: {
                renderNode.kind = RenderGraphNodeKind::FrequencyResponse;
                renderNode.frequencyResponseSettings =
                    ToRenderFrequencyResponseSettings(node.frequencyResponseSettings);
                const auto resolve = [&](const char* parameterId, float& target, float minimum, float maximum) {
                    double connected = 0.0;
                    if (graphLookup.TryResolveUniformScalarInput(
                            node.id,
                            EditorNodeGraph::ParameterInputSocketId(parameterId),
                            connected)) {
                        target = std::clamp(static_cast<float>(connected), minimum, maximum);
                    }
                };
                resolve(EditorNodeGraph::kLowCutoffParameterId,
                    renderNode.frequencyResponseSettings.lowCutoff, 0.0f, 0.5f);
                resolve(EditorNodeGraph::kHighCutoffParameterId,
                    renderNode.frequencyResponseSettings.highCutoff, 0.0f, 0.5f);
                resolve(EditorNodeGraph::kTransitionWidthParameterId,
                    renderNode.frequencyResponseSettings.transitionWidth, 0.0f, 0.5f);
                resolve(EditorNodeGraph::kButterworthOrderParameterId,
                    renderNode.frequencyResponseSettings.butterworthOrder, 1.0f, 12.0f);
                for (std::size_t notchIndex = 0;
                     notchIndex < renderNode.frequencyResponseSettings.notches.size();
                     ++notchIndex) {
                    RenderFrequencyNotch& notch =
                        renderNode.frequencyResponseSettings.notches[notchIndex];
                    const auto resolveNotch = [&](const char* field,
                                                  float& target,
                                                  float minimum,
                                                  float maximum) {
                        const std::string parameterId =
                            EditorNodeGraph::FrequencyNotchParameterId(
                                node.frequencyResponseSettings.notches[notchIndex].id,
                                field);
                        double connected = 0.0;
                        if (graphLookup.TryResolveUniformScalarInput(
                                node.id,
                                EditorNodeGraph::ParameterInputSocketId(parameterId),
                                connected)) {
                            target = std::clamp(
                                static_cast<float>(connected), minimum, maximum);
                        }
                    };
                    resolveNotch(
                        "frequency", notch.frequency, 0.0f, 0.5f);
                    resolveNotch(
                        "direction", notch.directionDegrees, -180.0f, 180.0f);
                    resolveNotch(
                        "width", notch.width, 0.001f, 0.25f);
                }
                break;
            }
            case EditorNodeGraph::NodeKind::FrequencyFft:
                renderNode.kind = RenderGraphNodeKind::FrequencyFft;
                renderNode.frequencyFftSettings.edgePolicy =
                    static_cast<RenderFrequencyEdgePolicy>(node.frequencyFftSettings.edgePolicy);
                break;
            case EditorNodeGraph::NodeKind::FrequencyIfft:
                renderNode.kind = RenderGraphNodeKind::FrequencyIfft;
                renderNode.frequencyIfftSettings.edgePolicy =
                    static_cast<RenderFrequencyEdgePolicy>(node.frequencyIfftSettings.edgePolicy);
                break;
            case EditorNodeGraph::NodeKind::SpectrumView:
                renderNode.kind = RenderGraphNodeKind::SpectrumView;
                renderNode.spectrumViewSettings.mode =
                    static_cast<RenderSpectrumViewMode>(node.spectrumViewSettings.mode);
                renderNode.spectrumViewSettings.lut = static_cast<RenderSpectrumViewLut>(node.spectrumViewSettings.lut);
                renderNode.spectrumViewSettings.exposure = node.spectrumViewSettings.exposure;
                renderNode.spectrumViewSettings.gamma = node.spectrumViewSettings.gamma;
                renderNode.spectrumViewSettings.centerDc = node.spectrumViewSettings.centerDc;
                break;
            case EditorNodeGraph::NodeKind::ApplyFrequencyResponse: {
                renderNode.kind = RenderGraphNodeKind::ApplyFrequencyResponse;
                renderNode.applyFrequencyResponseSettings.strength =
                    node.applyFrequencyResponseSettings.strength;
                double connected = 0.0;
                if (graphLookup.TryResolveUniformScalarInput(
                        node.id,
                        EditorNodeGraph::ParameterInputSocketId(
                            EditorNodeGraph::kStrengthParameterId),
                        connected)) {
                    renderNode.applyFrequencyResponseSettings.strength =
                        std::clamp(static_cast<float>(connected), 0.0f, 1.0f);
                }
                break;
            }
            case EditorNodeGraph::NodeKind::CombineSpectra:
                renderNode.kind = RenderGraphNodeKind::CombineSpectra;
                renderNode.combineSpectraSettings.mode =
                    static_cast<RenderSpectrumCombineMode>(node.combineSpectraSettings.mode);
                break;
            case EditorNodeGraph::NodeKind::SpectrumSeparate:
                renderNode.kind = RenderGraphNodeKind::SpectrumSeparate;
                break;
            case EditorNodeGraph::NodeKind::SpectrumRecombine:
                renderNode.kind = RenderGraphNodeKind::SpectrumRecombine;
                break;
            case EditorNodeGraph::NodeKind::FrequencyMask:
                renderNode.kind = RenderGraphNodeKind::FrequencyMask;
                renderNode.frequencyMaskSettings.shape = static_cast<RenderFrequencyMaskShape>(node.frequencyMaskShape);
                renderNode.frequencyMaskSettings.cutoff = node.frequencyMaskSettings.cutoff;
                renderNode.frequencyMaskSettings.width = node.frequencyMaskSettings.width;
                renderNode.frequencyMaskSettings.feather = node.frequencyMaskSettings.feather;
                renderNode.frequencyMaskSettings.order = node.frequencyMaskSettings.order;
                renderNode.frequencyMaskSettings.centerX = node.frequencyMaskSettings.centerX;
                renderNode.frequencyMaskSettings.centerY = node.frequencyMaskSettings.centerY;
                renderNode.frequencyMaskSettings.invert = node.frequencyMaskSettings.invert;
                break;
            case EditorNodeGraph::NodeKind::SpectrumMath:
                renderNode.kind = RenderGraphNodeKind::SpectrumMath;
                renderNode.spectrumMathMode = static_cast<RenderSpectrumMathMode>(node.spectrumMathMode);
                renderNode.spectrumMathSettings.amount = node.spectrumMathSettings.amount;
                break;
            case EditorNodeGraph::NodeKind::MagnitudePhase:
                renderNode.kind = RenderGraphNodeKind::MagnitudePhase;
                renderNode.magnitudePhaseMode = static_cast<RenderMagnitudePhaseMode>(node.magnitudePhaseMode);
                renderNode.magnitudePhaseSettings.exposure = node.magnitudePhaseSettings.exposure;
                renderNode.magnitudePhaseSettings.gamma = node.magnitudePhaseSettings.gamma;
                break;
            case EditorNodeGraph::NodeKind::SpectrumAnalyzer:
                renderNode.kind = RenderGraphNodeKind::SpectrumAnalyzer;
                renderNode.spectrumAnalyzerMode = static_cast<RenderSpectrumAnalyzerMode>(node.spectrumAnalyzerMode);
                renderNode.spectrumAnalyzerSettings.innerRadius = node.spectrumAnalyzerSettings.innerRadius;
                renderNode.spectrumAnalyzerSettings.outerRadius = node.spectrumAnalyzerSettings.outerRadius;
                renderNode.spectrumAnalyzerSettings.excludeDc = node.spectrumAnalyzerSettings.excludeDc;
                {
                    double connected = 0.0;
                    if (graphLookup.TryResolveUniformScalarInput(
                            node.id,
                            EditorNodeGraph::ParameterInputSocketId(
                                EditorNodeGraph::kAnalyzerLowParameterId),
                            connected)) {
                        renderNode.spectrumAnalyzerSettings.innerRadius =
                            std::clamp(static_cast<float>(connected), 0.0f, 0.70710678f);
                    }
                    if (graphLookup.TryResolveUniformScalarInput(
                            node.id,
                            EditorNodeGraph::ParameterInputSocketId(
                                EditorNodeGraph::kAnalyzerHighParameterId),
                            connected)) {
                        renderNode.spectrumAnalyzerSettings.outerRadius =
                            std::clamp(static_cast<float>(connected), 0.0f, 0.70710678f);
                    }
                }
                break;
            case EditorNodeGraph::NodeKind::ChannelSplit:
                renderNode.kind = RenderGraphNodeKind::ChannelSplit;
                break;
            case EditorNodeGraph::NodeKind::ChannelCombine:
                renderNode.kind = RenderGraphNodeKind::ChannelCombine;
                break;
            case EditorNodeGraph::NodeKind::ConstantChannel:
                renderNode.kind =
                    RenderGraphNodeKind::ConstantChannel;
                renderNode.constantChannelValue =
                    std::isfinite(
                        node.constantChannelSettings.value)
                        ? node.constantChannelSettings.value
                        : 1.0f;
                break;
            case EditorNodeGraph::NodeKind::FieldMean:
                renderNode.kind = RenderGraphNodeKind::FieldMean;
                break;
            case EditorNodeGraph::NodeKind::Reformat:
                renderNode.kind = RenderGraphNodeKind::Reformat;
                renderNode.reformatSettings = node.reformatSettings;
                break;
            case EditorNodeGraph::NodeKind::Value:
            case EditorNodeGraph::NodeKind::Composite:
            case EditorNodeGraph::NodeKind::Scope:
            case EditorNodeGraph::NodeKind::Preview:
            case EditorNodeGraph::NodeKind::Compound:
                continue;
        }
        snapshot.nodes.push_back(std::move(renderNode));
    }

    std::unordered_set<int> renderNodeIds;
    renderNodeIds.reserve(snapshot.nodes.size());
    for (const RenderGraphNode& node : snapshot.nodes) {
        renderNodeIds.insert(node.nodeId);
    }

    snapshot.links.reserve(graphLinks.size());
    for (const EditorNodeGraph::Link& link : graphLinks) {
        if (graphLookup.IsAnalysisLink(link)) {
            continue;
        }
        if (renderNodeIds.count(link.fromNodeId) == 0 ||
            renderNodeIds.count(link.toNodeId) == 0) {
            continue;
        }
        const EditorNodeGraph::Node* source =
            graphLookup.FindNode(link.fromNodeId);
        const EditorNodeGraph::Node* destination =
            graphLookup.FindNode(link.toNodeId);
        if ((source && source->kind == EditorNodeGraph::NodeKind::Value) ||
            (destination && destination->kind == EditorNodeGraph::NodeKind::Value)) {
            continue;
        }
        snapshot.links.push_back(RenderGraphLink{
            link.fromNodeId,
            link.fromSocketId,
            link.toNodeId,
            link.toSocketId
        });
    }

    std::unordered_map<int, Stack::NodeMath::ValueDescriptor>
        channelOutputDescriptors;
    for (const RenderGraphNode& node : snapshot.nodes) {
        if (node.kind != RenderGraphNodeKind::Output ||
            !m_NodeGraph.IsOutputChannelInspection(node.nodeId)) {
            continue;
        }
        const EditorNodeGraph::Link* input =
            m_NodeGraph.FindInputLink(
                node.nodeId,
                EditorNodeGraph::kImageInputSocketId);
        const std::string role = input
            ? m_NodeGraph.ResolveSocketChannel(
                input->fromNodeId,
                input->fromSocketId)
            : std::string();
        channelOutputDescriptors.emplace(
            node.nodeId,
            UnknownLiveChannelDescriptor(
                role,
                "output.channel-inspection.v1"));
    }

    std::vector<Stack::NodeMath::SemanticImageNode> semanticNodes;
    semanticNodes.reserve(snapshot.nodes.size());
    std::unordered_map<int, const RenderGraphNode*> renderNodeById;
    renderNodeById.reserve(snapshot.nodes.size());
    for (const RenderGraphNode& node : snapshot.nodes) {
        renderNodeById[node.nodeId] = &node;
        if (!HasSemanticImageOutput(node.kind)) continue;
        Stack::NodeMath::SemanticImageNode semantic;
        semantic.identity = SemanticNodeIdentity(node.nodeId);
        semantic.kind = Stack::NodeMath::SemanticImageNodeKind::Identity;
        if (node.kind == RenderGraphNodeKind::Image) {
            semantic.kind = Stack::NodeMath::SemanticImageNodeKind::Source;
            semantic.sourceDescriptor = HasValidImageDescriptor(node.image.sourceDescriptor)
                ? node.image.sourceDescriptor
                : UnknownLiveImageDescriptor("source.image.unknown");
        } else if (node.kind == RenderGraphNodeKind::RawDevelopment) {
            semantic.kind = Stack::NodeMath::SemanticImageNodeKind::Source;
            semantic.sourceDescriptor = UnknownLiveImageDescriptor(
                "source.raw-development");
            if (Stack::RawRecipe::IsViewTransformEnabled(
                    node.rawDevelopment.recipe)) {
                const bool encodeSrgb =
                    node.rawDevelopment.recipe.viewTransform.layerJson.value(
                        "encodeSrgbOutput",
                        node.rawDevelopment.recipe.technical.encodeSrgbOutput);
                DeclareRawDisplayOutput(
                    semantic.sourceDescriptor,
                    encodeSrgb,
                    "raw.development.display-output.v1");
            } else {
                DeclareRawSceneOutput(
                    semantic.sourceDescriptor,
                    node.rawDevelopment.recipe.technical.workingSpace,
                    "raw.development.scene-output.v1");
            }
        } else if (node.kind ==
                   RenderGraphNodeKind::RawProjectSourceSet) {
            semantic.kind =
                Stack::NodeMath::SemanticImageNodeKind::Source;
            semantic.sourceDescriptor = UnknownLiveImageDescriptor(
                "source.mfd-raw-development");
            if (node.rawProjectSourceSet.resultAvailable) {
                if (Stack::RawRecipe::IsViewTransformEnabled(
                        node.rawDevelopment.recipe)) {
                    const bool encodeSrgb =
                        node.rawDevelopment.recipe.viewTransform.layerJson
                            .value(
                                "encodeSrgbOutput",
                                node.rawDevelopment.recipe.technical
                                    .encodeSrgbOutput);
                    DeclareRawDisplayOutput(
                        semantic.sourceDescriptor,
                        encodeSrgb,
                        "mfd.raw-development.display-output.v1");
                } else {
                    DeclareRawSceneOutput(
                        semantic.sourceDescriptor,
                        node.rawDevelopment.recipe.technical.workingSpace,
                        "mfd.raw-development.scene-output.v1");
                }
            }
        } else if (node.kind == RenderGraphNodeKind::RawDecode) {
            semantic.kind = Stack::NodeMath::SemanticImageNodeKind::Source;
            semantic.sourceDescriptor = UnknownLiveImageDescriptor("source.raw-decode");
            DeclareRawSceneOutput(
                semantic.sourceDescriptor,
                node.rawDecode.settings.workingSpace,
                "raw.decode.scene-output.v1");
        } else if (node.kind == RenderGraphNodeKind::RawDevelop) {
            semantic.kind = Stack::NodeMath::SemanticImageNodeKind::Source;
            semantic.sourceDescriptor = UnknownLiveImageDescriptor("source.raw-develop");
            DeclareRawSceneOutput(
                semantic.sourceDescriptor,
                node.rawDevelop.settings.workingSpace,
                "raw.develop.scene-output.v1");
        } else if (node.kind == RenderGraphNodeKind::ImageGenerator ||
                   node.kind == RenderGraphNodeKind::ChannelCombine) {
            semantic.kind = Stack::NodeMath::SemanticImageNodeKind::Source;
            if (node.kind == RenderGraphNodeKind::ImageGenerator) {
                semantic.sourceDescriptor =
                    UnknownLiveImageDescriptor("source.generated-image");
            } else {
                Stack::NodeMath::ImageComponentSet presentComponents;
                for (const RenderGraphLink& link : snapshot.links) {
                    if (link.toNodeId != node.nodeId) {
                        continue;
                    }
                    if (link.toSocketId == "r") {
                        Stack::NodeMath::AddImageComponent(
                            presentComponents,
                            Stack::NodeMath::ImageComponent::Red);
                    } else if (link.toSocketId == "g") {
                        Stack::NodeMath::AddImageComponent(
                            presentComponents,
                            Stack::NodeMath::ImageComponent::Green);
                    } else if (link.toSocketId == "b") {
                        Stack::NodeMath::AddImageComponent(
                            presentComponents,
                            Stack::NodeMath::ImageComponent::Blue);
                    } else if (link.toSocketId == "a") {
                        Stack::NodeMath::AddImageComponent(
                            presentComponents,
                            Stack::NodeMath::ImageComponent::Alpha);
                    }
                }
                semantic.sourceDescriptor = presentComponents.bits != 0
                    ? Stack::NodeMath::MakePartialColorImageDescriptor(
                        presentComponents,
                        "image.combine.v2")
                    : UnknownLiveImageDescriptor("image.combine.v2");
            }
        } else if (node.kind == RenderGraphNodeKind::Layer &&
                   node.layerJson.value("type", std::string()) == "ViewTransform") {
            semantic.kind = Stack::NodeMath::SemanticImageNodeKind::DeclaredColorOutput;
            semantic.declaredColor = {
                "srgb-d65", {}, Stack::NodeMath::ColorRelation::Standard
            };
            semantic.declaredTransfer = {
                node.layerJson.value("encodeSrgbOutput", false)
                    ? Stack::NodeMath::TransferKind::Srgb
                    : Stack::NodeMath::TransferKind::Linear,
                0.0,
                {}
            };
            semantic.declaredReference = Stack::NodeMath::ReferenceState::Display;
            semantic.declaredOperationIdentity = "view-transform.display-output.v1";
        } else if (node.kind == RenderGraphNodeKind::TechnicalImage) {
            semantic.kind = Stack::NodeMath::SemanticImageNodeKind::TechnicalOperation;
            semantic.technicalOperation = node.technicalImageOperation;
            semantic.exposureValue = node.technicalExposureValue;
        } else if (node.kind == RenderGraphNodeKind::Reformat) {
            semantic.kind = Stack::NodeMath::SemanticImageNodeKind::Geometry;
            semantic.geometryOutputSpatial.kind = Stack::NodeMath::SpatialExtentKind::Finite;
            semantic.geometryOutputSpatial.fullWindow = {
                0, 0, node.reformatSettings.width, node.reformatSettings.height };
            semantic.geometryOutputSpatial.dataWindow = semantic.geometryOutputSpatial.fullWindow;
            semantic.geometryOutputSpatial.rasterOrigin = Stack::NodeMath::RasterOrigin::BottomLeft;
            semantic.geometryOutputSpatial.pixelAspect = 1.0;
        } else if (node.kind == RenderGraphNodeKind::Mix &&
                   node.mixBlendMode == RenderMixBlendMode::StraightSourceOver) {
            semantic.kind = Stack::NodeMath::SemanticImageNodeKind::StraightSourceOver;
        } else if (node.kind == RenderGraphNodeKind::Mix &&
                   node.mixBlendMode == RenderMixBlendMode::PremultipliedSourceOver) {
            semantic.kind = Stack::NodeMath::SemanticImageNodeKind::PremultipliedSourceOver;
        } else if (node.kind == RenderGraphNodeKind::Output) {
            const auto channelDescriptor =
                channelOutputDescriptors.find(node.nodeId);
            if (channelDescriptor != channelOutputDescriptors.end()) {
                // Channel inspection is a viewport materialization boundary,
                // not an image conversion. Model it as the unchanged Channel
                // value so no PNG policy or hidden color meaning is applied.
                semantic.kind =
                    Stack::NodeMath::SemanticImageNodeKind::Source;
                semantic.sourceDescriptor = channelDescriptor->second;
            } else {
                semantic.kind =
                    Stack::NodeMath::SemanticImageNodeKind::DirectOutput;
            }
        }
        semanticNodes.push_back(std::move(semantic));
    }

    std::vector<Stack::NodeMath::SemanticImageEdge> semanticEdges;
    semanticEdges.reserve(snapshot.links.size());
    for (const RenderGraphLink& link : snapshot.links) {
        const auto source = renderNodeById.find(link.fromNodeId);
        const auto destination = renderNodeById.find(link.toNodeId);
        if (source == renderNodeById.end() || destination == renderNodeById.end() ||
            !HasSemanticImageOutput(source->second->kind) ||
            !HasSemanticImageOutput(destination->second->kind)) {
            continue;
        }
        std::string port = "secondary";
        if (destination->second->kind == RenderGraphNodeKind::Mix &&
            (destination->second->mixBlendMode == RenderMixBlendMode::StraightSourceOver ||
             destination->second->mixBlendMode == RenderMixBlendMode::PremultipliedSourceOver)) {
            port = link.toSocketId == EditorNodeGraph::kMixInputBSocketId
                ? "source" : "backdrop";
        } else if (link.toSocketId == EditorNodeGraph::kImageInputSocketId ||
                   link.toSocketId == EditorNodeGraph::kMixInputASocketId ||
                   link.toSocketId == EditorNodeGraph::kHdrMergeInput1SocketId ||
                   link.toSocketId == EditorNodeGraph::kMfsrReferenceInputSocketId) {
            port = "image";
        }
        semanticEdges.push_back({
            SemanticLinkIdentity(link), SemanticNodeIdentity(link.fromNodeId),
            SemanticNodeIdentity(link.toNodeId), port
        });
    }

    const Stack::NodeMath::SemanticAnalysisResult semantic =
        Stack::NodeMath::AnalyzeSemanticImageGraph(semanticNodes, semanticEdges);
    snapshot.semanticFingerprint = semantic.semanticFingerprint;
    snapshot.semanticDiagnostics = semantic.diagnostics;
    std::unordered_map<std::string_view, const Stack::NodeMath::SemanticNodeOutput*>
        semanticOutputByIdentity;
    semanticOutputByIdentity.reserve(semantic.nodeOutputs.size());
    for (const Stack::NodeMath::SemanticNodeOutput& output : semantic.nodeOutputs) {
        semanticOutputByIdentity.emplace(output.nodeIdentity, &output);
    }
    std::unordered_map<std::string_view, const Stack::NodeMath::SemanticEdgeState*>
        semanticEdgeByIdentity;
    semanticEdgeByIdentity.reserve(semantic.edges.size());
    for (const Stack::NodeMath::SemanticEdgeState& edge : semantic.edges) {
        semanticEdgeByIdentity.emplace(edge.edge.identity, &edge);
    }
    for (RenderGraphNode& node : snapshot.nodes) {
        const std::string identity = SemanticNodeIdentity(node.nodeId);
        const auto output = semanticOutputByIdentity.find(identity);
        if (output != semanticOutputByIdentity.end()) {
            node.semanticDescriptor = output->second->descriptor;
            node.semanticDescriptorIdentity = output->second->descriptorIdentity;
        }
    }
    for (RenderGraphLink& link : snapshot.links) {
        const std::string identity = SemanticLinkIdentity(link);
        const auto edge = semanticEdgeByIdentity.find(identity);
        const auto channelOutput =
            channelOutputDescriptors.find(link.toNodeId);
        if (channelOutput != channelOutputDescriptors.end() &&
            link.toSocketId == EditorNodeGraph::kImageInputSocketId) {
            const std::string outputIdentity =
                SemanticNodeIdentity(link.toNodeId);
            const auto output =
                semanticOutputByIdentity.find(outputIdentity);
            link.semanticDescriptor = output !=
                    semanticOutputByIdentity.end()
                ? output->second->descriptor
                : channelOutput->second;
            link.semanticDescriptorIdentity =
                Stack::NodeMath::DescriptorContentIdentity(
                    link.semanticDescriptor);
        } else if (edge != semanticEdgeByIdentity.end()) {
            link.semanticDescriptor = edge->second->descriptor;
            link.semanticDescriptorIdentity = edge->second->descriptorIdentity;
        } else if (const auto source = renderNodeById.find(link.fromNodeId);
                   source != renderNodeById.end()) {
            const std::string sourceIdentity = SemanticNodeIdentity(link.fromNodeId);
            const auto output = semanticOutputByIdentity.find(sourceIdentity);
            if (output != semanticOutputByIdentity.end()) {
                link.semanticDescriptor = output->second->descriptor;
                link.semanticDescriptorIdentity = output->second->descriptorIdentity;
            }
        }
    }
    const std::string outputIdentity = SemanticNodeIdentity(snapshot.outputNodeId);
    const auto semanticOutput = semanticOutputByIdentity.find(outputIdentity);
    if (semanticOutput != semanticOutputByIdentity.end()) {
        snapshot.outputDescriptor = semanticOutput->second->descriptor;
        snapshot.outputDescriptorIdentity = semanticOutput->second->descriptorIdentity;
    }
    m_LastGraphOutputSemanticDescriptor = snapshot.outputDescriptor;
    m_LastGraphOutputSemanticDescriptorIdentity = snapshot.outputDescriptorIdentity;
    m_LastGraphSemanticDiagnostics = snapshot.semanticDiagnostics;
    m_LastGraphLinkSemanticDescriptors.clear();
    for (const RenderGraphLink& link : snapshot.links) {
        if (!link.semanticDescriptorIdentity.empty()) {
            m_LastGraphLinkSemanticDescriptors[SemanticLinkIdentity(link)] = link.semanticDescriptor;
        }
    }
    return snapshot;
}

bool EditorModule::TryGetGraphOutputSemanticDescriptor(
    Stack::NodeMath::ValueDescriptor& descriptor) const {
    if (m_LastGraphOutputSemanticDescriptorIdentity.empty()) return false;
    descriptor = m_LastGraphOutputSemanticDescriptor;
    return true;
}

bool EditorModule::TryGetGraphLinkSemanticDescriptor(
    const EditorNodeGraph::Link& link,
    Stack::NodeMath::ValueDescriptor& descriptor) const {
    const auto found = m_LastGraphLinkSemanticDescriptors.find(SemanticLinkIdentity(link));
    if (found == m_LastGraphLinkSemanticDescriptors.end()) return false;
    descriptor = found->second;
    return true;
}

bool EditorModule::TryGetGraphLinkWireReadoutInput(
    const EditorNodeGraph::Link& link,
    EditorNodeGraph::WireReadout::Input& input) const {
    input = {};
    const EditorNodeGraph::Node* source = m_NodeGraph.FindNode(link.fromNodeId);
    if (source == nullptr) return false;

    if (!m_NodeGraph.FindSocket(link.fromNodeId, link.fromSocketId, &input.sourceSocket)) {
        input.sourceSocket.id = link.fromSocketId;
        input.sourceSocket.nodeId = link.fromNodeId;
        input.sourceSocket.direction = EditorNodeGraph::SocketDirection::Output;
        input.sourceSocket.label = "Unknown";
        EditorNodeGraph::SocketPresentation::NormalizeSocketDefinition(
            source->kind,
            input.sourceSocket);
    }

    input.hasDescriptor = TryGetGraphLinkSemanticDescriptor(link, input.descriptor);

    // A Value node owns a declared uniform payload, so presenting it needs no
    // evaluation. Other known values are copied only from an accepted current
    // render result; stale or absent results deliberately fall back to type.
    if (source->kind == EditorNodeGraph::NodeKind::Value &&
        link.fromSocketId == EditorNodeGraph::kValueOutputSocketId) {
        input.value = source->value.value;
    } else if (!m_RenderDirty &&
               m_LastGraphUniformOutputGeneration == m_LastCompletedRenderGeneration) {
        const auto value = m_LastGraphUniformOutputValues.find(
            EditorNodeGraph::WireReadout::OutputIdentity(
                link.fromNodeId,
                link.fromSocketId));
        if (value != m_LastGraphUniformOutputValues.end()) {
            input.value = value->second;
        }
    }

    // Semantic diagnostics currently identify node outputs. Attribute one to
    // a wire only when that node has exactly one output, preventing a node-wide
    // diagnostic from being shown on an arbitrary sibling output.
    int outputCount = 0;
    for (const EditorNodeGraph::SocketDefinition& socket : m_NodeGraph.GetSockets(*source)) {
        if (socket.direction == EditorNodeGraph::SocketDirection::Output) ++outputCount;
    }
    if (outputCount == 1) {
        input.sourceDiagnostics =
            EditorNodeGraph::WireReadout::FilterSourceOutputDiagnostics(
                m_LastGraphSemanticDiagnostics,
                SemanticNodeIdentity(link.fromNodeId),
                true);
    }
    return true;
}
