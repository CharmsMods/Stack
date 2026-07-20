#include "Editor/EditorModule.h"

#include "Editor/Layers/LayerBase.h"
#include "Editor/Timeline/TimelineFrameProducer.h"
#include "Renderer/MaskRenderTypes.h"
#include "NodeMath/SemanticSpine.h"
#include "NodeMath/FirstClassValue.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
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
    case RenderGraphNodeKind::Lut:
    case RenderGraphNodeKind::Layer:
    case RenderGraphNodeKind::Output:
    case RenderGraphNodeKind::Mix:
    case RenderGraphNodeKind::ImageGenerator:
    case RenderGraphNodeKind::ChannelCombine:
    case RenderGraphNodeKind::DataMath:
    case RenderGraphNodeKind::TechnicalImage:
    case RenderGraphNodeKind::FrequencyFft:
    case RenderGraphNodeKind::FrequencyIfft:
    case RenderGraphNodeKind::SpectrumView:
    case RenderGraphNodeKind::SpectrumMath:
    case RenderGraphNodeKind::MagnitudePhase:
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

    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
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
                    if (graph.TryResolveUniformScalarInput(
                            node.id,
                            EditorNodeGraph::kExposureValueInputSocketId,
                            connectedExposure,
                            nullptr)) {
                        renderNode.technicalExposureValue = static_cast<float>(connectedExposure);
                    }
                }
                break;
            case EditorNodeGraph::NodeKind::FrequencyFft:
                renderNode.kind = RenderGraphNodeKind::FrequencyFft;
                renderNode.frequencyFftSettings.luminanceOnly = node.frequencyFftSettings.luminanceOnly;
                break;
            case EditorNodeGraph::NodeKind::FrequencyIfft:
                renderNode.kind = RenderGraphNodeKind::FrequencyIfft;
                renderNode.frequencyIfftSettings.luminanceOnly = node.frequencyIfftSettings.luminanceOnly;
                break;
            case EditorNodeGraph::NodeKind::SpectrumView:
                renderNode.kind = RenderGraphNodeKind::SpectrumView;
                renderNode.spectrumViewSettings.lut = static_cast<RenderSpectrumViewLut>(node.spectrumViewSettings.lut);
                renderNode.spectrumViewSettings.exposure = node.spectrumViewSettings.exposure;
                renderNode.spectrumViewSettings.gamma = node.spectrumViewSettings.gamma;
                renderNode.spectrumViewSettings.centerDc = node.spectrumViewSettings.centerDc;
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
                break;
            case EditorNodeGraph::NodeKind::ChannelSplit:
                renderNode.kind = RenderGraphNodeKind::ChannelSplit;
                break;
            case EditorNodeGraph::NodeKind::ChannelCombine:
                renderNode.kind = RenderGraphNodeKind::ChannelCombine;
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

    for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
        if (graph.GetLinkRole(link) == EditorNodeGraph::LinkRole::Scope) {
            continue;
        }
        const EditorNodeGraph::Node* source = graph.FindNode(link.fromNodeId);
        const EditorNodeGraph::Node* destination = graph.FindNode(link.toNodeId);
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

    std::vector<Stack::NodeMath::SemanticImageNode> semanticNodes;
    std::map<int, const RenderGraphNode*> renderNodeById;
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
        } else if (node.kind == RenderGraphNodeKind::RawDevelopment ||
                   node.kind == RenderGraphNodeKind::RawDecode ||
                   node.kind == RenderGraphNodeKind::RawDevelop ||
                   node.kind == RenderGraphNodeKind::ImageGenerator ||
                   node.kind == RenderGraphNodeKind::ChannelCombine) {
            semantic.kind = Stack::NodeMath::SemanticImageNodeKind::Source;
            semantic.sourceDescriptor = UnknownLiveImageDescriptor(
                node.kind == RenderGraphNodeKind::ImageGenerator
                    ? "source.generated-image" : "source.opaque-specialized");
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
            semantic.kind = Stack::NodeMath::SemanticImageNodeKind::DirectOutput;
        }
        semanticNodes.push_back(std::move(semantic));
    }

    std::vector<Stack::NodeMath::SemanticImageEdge> semanticEdges;
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
    for (RenderGraphNode& node : snapshot.nodes) {
        if (const auto* output = Stack::NodeMath::FindSemanticNodeOutput(
                semantic, SemanticNodeIdentity(node.nodeId))) {
            node.semanticDescriptor = output->descriptor;
            node.semanticDescriptorIdentity = output->descriptorIdentity;
        }
    }
    for (RenderGraphLink& link : snapshot.links) {
        if (const auto* edge = Stack::NodeMath::FindSemanticEdgeState(
                semantic, SemanticLinkIdentity(link))) {
            link.semanticDescriptor = edge->descriptor;
            link.semanticDescriptorIdentity = edge->descriptorIdentity;
        } else if (const auto source = renderNodeById.find(link.fromNodeId);
                   source != renderNodeById.end()) {
            const auto* output = Stack::NodeMath::FindSemanticNodeOutput(
                semantic, SemanticNodeIdentity(link.fromNodeId));
            if (output) {
                link.semanticDescriptor = output->descriptor;
                link.semanticDescriptorIdentity = output->descriptorIdentity;
            }
        }
    }
    if (const auto* output = Stack::NodeMath::FindSemanticNodeOutput(
            semantic, SemanticNodeIdentity(snapshot.outputNodeId))) {
        snapshot.outputDescriptor = output->descriptor;
        snapshot.outputDescriptorIdentity = output->descriptorIdentity;
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
