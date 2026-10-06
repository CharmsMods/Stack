#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Project/GraphSnapshotBuilder.h"

#include "Project/GraphImagePayload.h"
#include "Project/GraphImageContracts.h"
#include "Project/GraphSnapshotConversions.h"
#include "Project/GraphSnapshotLookup.h"
#include "Persistence/RawProjectModel.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include "Editor/NodeGraph/NodeDependencyGraph.h"
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
#include <filesystem>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Stack::Project {
using namespace GraphSnapshotInternal;

namespace {

std::string SemanticNodeIdentity(int nodeId) {
    return "node-" + std::to_string(nodeId);
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

} // namespace

GraphSnapshotResult BuildGraphSnapshot(const GraphSnapshotInputs& inputs) {
    GraphSnapshotResult result;
    auto& snapshot = result.snapshot;
    const int stageOutputNodeId = inputs.stageOutputNodeId;
    EditorNodeGraph::Graph expandedGraph;
    EditorNodeGraph::CompoundExpansionResult expansion;
    EditorNodeGraph::Graph stageGraph;
    if (stageOutputNodeId > 0) stageGraph = EditorNodeGraph::BuildNodeDependencyGraph(inputs.graph, stageOutputNodeId);
    const EditorNodeGraph::Graph* renderGraph = stageOutputNodeId > 0 ? &stageGraph : &inputs.graph;
    const bool hasCompoundNode = std::any_of(
        renderGraph->GetNodes().begin(),
        renderGraph->GetNodes().end(),
        [](const EditorNodeGraph::Node& node) {
            return node.kind == EditorNodeGraph::NodeKind::Compound;
        });
    if (hasCompoundNode) {
        if (!renderGraph->ExpandAllCompoundNodes(expandedGraph, &expansion)) {
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
            return result;
        }
        renderGraph = &expandedGraph;
    }
    const EditorNodeGraph::Graph& graph = *renderGraph;
    const std::vector<EditorNodeGraph::Node>& graphNodes = graph.GetNodes();
    const std::vector<EditorNodeGraph::Link>& graphLinks = graph.GetLinks();
    const GraphSnapshotInternal::Lookup graphLookup(graph);
    snapshot.outputNodeId = stageOutputNodeId > 0 ? stageOutputNodeId : graph.ResolvePreviewOutputNodeId();
    if (stageOutputNodeId > 0) snapshot.outputSocketId = EditorNodeGraph::kImageOutputSocketId;
    snapshot.executionInspectionEnabled = inputs.executionInspectionEnabled;
    const Stack::Timeline::TimelineFrameEvaluation frameEvaluation =
        Stack::Timeline::BuildTimelineFrameEvaluation(
            inputs.timeline,
            Stack::Timeline::NormalizeTimelineFrameRequest(
                inputs.frame.frame,
                inputs.frame.durationFrames,
                inputs.frame.framesPerSecond));
    Stack::Timeline::FrameEvaluationContext frameContext = frameEvaluation.frameContext;
    if (inputs.liveEditPreviewFrame == frameEvaluation.request.frame) {
        for (const Stack::Timeline::AnimatableParameterTarget& target : inputs.liveEditPreviewTargets) {
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
        renderNode.role = node.role;
        renderNode.reference = node.reference;
        renderNode.publishedType = node.role == Stack::GraphModel::NodeRole::Reference
            ? node.referenceType : node.outputSettings.publishedType;
        if (const auto* definition = EditorNodeGraphDefinitions::FindLiveNodeDefinition(node))
            renderNode.outputDependencies = definition->outputDependencies;
        if (node.kind == EditorNodeGraph::NodeKind::RawOperation)
            for (auto& dependency : renderNode.outputDependencies)
                if (dependency.output == "measurementImageOut")
                    dependency.inputs = {graph.FindInputLink(node.id, "referenceIn") ? "referenceIn" : "imageIn"};
        const auto dirty = inputs.nodeDirtyGenerations.find(node.id);
        renderNode.requestRevision = dirty == inputs.nodeDirtyGenerations.end()
            ? 1 : std::max<std::uint64_t>(1, dirty->second);
        renderNode.definitionId = node.definitionId;
        renderNode.definitionVersion = node.definitionVersion;
        renderNode.definitionHash = node.definitionHash;
        switch (node.kind) {
            case EditorNodeGraph::NodeKind::RawOperation:
                renderNode.kind = RenderGraphNodeKind::RawOperation;
                renderNode.rawOperation = node.rawOperation;
                if (const auto* definition = EditorNodeGraphDefinitions::FindLiveNodeDefinition(node))
                    for (const auto& parameter : definition->parameters) {
                        float value;
                        if (parameter.hasNumericDomain && Stack::Timeline::TryGetFrameParameterValue(frameContext,
                                {node.id, parameter.id, inputs.graphId, node.instanceUuid}, value))
                            renderNode.rawOperation.parameters[nlohmann::json::json_pointer(parameter.storageKey)] =
                                std::clamp(double(value), parameter.minimum, parameter.maximum);
                    }
                renderNode.rawWorkingSpace = inputs.rawRecipe.technical.workingSpace;
                if (inputs.singleRawSource) {
                    renderNode.nativeWidth = Raw::DisplayWidth(inputs.singleRawSource->metadata);
                    renderNode.nativeHeight = Raw::DisplayHeight(inputs.singleRawSource->metadata);
                }
                break;
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
                if (inputs.singleRawSource) {
                    const auto& nodeSource =
                        node.rawDevelopment.recipe.source;
                    const auto& activeSource =
                        inputs.rawRecipe.source;
                    const bool sameFingerprint =
                        !nodeSource.fingerprint.empty() &&
                        nodeSource.fingerprint == activeSource.fingerprint;
                    const bool samePath =
                        !nodeSource.sourcePath.empty() &&
                        !activeSource.sourcePath.empty() &&
                        std::filesystem::path(nodeSource.sourcePath)
                                .lexically_normal() ==
                            std::filesystem::path(activeSource.sourcePath)
                                .lexically_normal();
                    if (sameFingerprint || samePath) {
                        renderNode.rawDevelopment.embeddedRawData =
                            inputs.singleRawSource;
                    }
                }
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
                if (inputs.rawProject) {
                    const Stack::Project::MultiFrameSourceSet* sourceSet =
                        Stack::Project::FindSourceSet(
                            *inputs.rawProject,
                            node.multiFrameDenoise.sourceSetId);
                    const bool adopted =
                        sourceSet != nullptr &&
                        inputs.mfdResult &&
                        inputs.mfdResult->rawData &&
                        inputs.mfdResult->projectId ==
                            inputs.rawProject->projectId &&
                        inputs.mfdResult->sourceSetId ==
                            sourceSet->sourceSetId &&
                        inputs.mfdResult->inputRevision ==
                            inputs.rawProject->mfdInputRevision;
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
                                      inputs.rawProject->projectId +
                                      "/" + sourceSet->sourceSetId,
                                  sourceSet->name + " developed result");
                        recipe.technical.processingVersion =
                            Raw::RawProcessingVersion::TruthfulV2;
                        if (!sourceSet->settings.contains("bracketing"))
                            recipe.technical.mosaicDenoise.enabled = false;
                        const nlohmann::json storedPreRecipe =
                            sourceSet->settings.value(
                                "sharedPreMfdRecipe",
                                nlohmann::json::object());
                        if (storedPreRecipe.is_object() &&
                            storedPreRecipe.contains("rawRecipeVersion")) {
                            recipe.cropRotation =
                                Stack::RawRecipe::DeserializeRecipe(
                                    storedPreRecipe).cropRotation;
                        }
                        recipe.source.sourcePath =
                            "mfd://" +
                            inputs.rawProject->projectId + "/" +
                            sourceSet->sourceSetId;
                        recipe.source.relativePathKey =
                            recipe.source.sourcePath;
                        recipe.source.fingerprint = std::to_string(
                            inputs.mfdResult->contentHash);
                        recipe.source.fileSizeBytes =
                            static_cast<std::uint64_t>(
                                inputs.mfdResult->rawData
                                    ->normalizedMosaicBuffer->size()) *
                            sizeof(float);
                        recipe.source.modifiedTimeTicks =
                            static_cast<std::int64_t>(
                                inputs.mfdResult->inputRevision);
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
                            inputs.mfdResult->rawData;
                        renderNode.rawProjectSourceSet.inputRevision =
                            inputs.mfdResult->inputRevision;
                        renderNode.rawProjectSourceSet.postRecipeRevision =
                            inputs.rawProject
                                ->postRecipeRevision;
                        renderNode.rawProjectSourceSet.contentHash =
                            inputs.mfdResult->contentHash;
                        renderNode.rawProjectSourceSet.resultAvailable =
                            true;
                    }
                }
                break;
            case EditorNodeGraph::NodeKind::MultiFrameHdr:
                renderNode.kind = RenderGraphNodeKind::RawProjectSourceSet;
                renderNode.rawProjectSourceSet.sourceSetId =
                    node.multiFrameHdr.sourceSetId;
                renderNode.rawProjectSourceSet.unavailableStatus =
                    node.multiFrameHdr.presentationStatus;
                renderNode.rawProjectSourceSet.quarantined =
                    node.multiFrameHdr.quarantined;
                if (inputs.rawProject) {
                    const Stack::Project::MultiFrameSourceSet* sourceSet =
                        Stack::Project::FindSourceSet(
                            *inputs.rawProject,
                            node.multiFrameHdr.sourceSetId);
                    const bool adopted = sourceSet != nullptr &&
                        inputs.hdrResult && inputs.hdrResult->rawData &&
                        inputs.hdrResult->projectId ==
                            inputs.rawProject->projectId &&
                        inputs.hdrResult->sourceSetId == sourceSet->sourceSetId &&
                        (inputs.hdrResult->inputRevision ==
                            inputs.rawProject->hdrInputRevision ||
                         sourceSet->settings.contains("bracketing"));
                    if (adopted) {
                        const nlohmann::json storedRecipe = sourceSet->settings.value(
                            "sharedPostHdrRecipe", nlohmann::json::object());
                        Stack::RawRecipe::RawDevelopmentRecipe recipe =
                            storedRecipe.is_object() &&
                                storedRecipe.contains("rawRecipeVersion")
                            ? Stack::RawRecipe::DeserializeRecipe(storedRecipe)
                            : Stack::RawRecipe::MakeDefaultRecipe(
                                "hdr://" + inputs.rawProject->projectId +
                                    "/" + sourceSet->sourceSetId,
                                sourceSet->name + " HDR result");
                        recipe.technical.processingVersion =
                            Raw::RawProcessingVersion::TruthfulV2;
                        if (!sourceSet->settings.contains("bracketing"))
                            recipe.technical.mosaicDenoise.enabled = false;
                        // A virtual HDR anchor has no meaningful DNG
                        // BaselineExposure of its own. The saved Exposure and
                        // View controls are the complete, visible display path.
                        recipe.technical.applyBaselineExposure = false;
                        if (sourceSet->settings.value(
                                "viewTransformPlacement",
                                std::string("internal")) == "graph") {
                            recipe.viewTransform.layerJson["enabled"] = false;
                        }
                        recipe.source.sourcePath = "hdr://" +
                            inputs.rawProject->projectId + "/" +
                            sourceSet->sourceSetId;
                        recipe.source.relativePathKey = recipe.source.sourcePath;
                        recipe.source.fingerprint = std::to_string(
                            inputs.hdrResult->contentHash);
                        const auto& hdrPixels=*inputs.hdrResult->rawData;
                        recipe.source.fileSizeBytes = static_cast<std::uint64_t>(
                            hdrPixels.normalizedMosaicBuffer?hdrPixels.normalizedMosaicBuffer->size():
                                hdrPixels.linearFloatBuffer.size()) * sizeof(float);
                        recipe.source.modifiedTimeTicks = static_cast<std::int64_t>(
                            inputs.hdrResult->inputRevision);
                        recipe.source.displayName = sourceSet->name + " HDR result";
                        renderNode.rawDevelopment.recipe = std::move(recipe);
                        renderNode.rawDevelopment.embeddedRawData =
                            inputs.hdrResult->rawData;
                        renderNode.rawProjectSourceSet.inputRevision =
                            inputs.hdrResult->inputRevision;
                        renderNode.rawProjectSourceSet.postRecipeRevision =
                            inputs.rawProject->postRecipeRevision;
                        renderNode.rawProjectSourceSet.contentHash =
                            inputs.hdrResult->contentHash;
                        renderNode.rawProjectSourceSet.resultAvailable = true;
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
                if (inputs.layerSettings && node.layerIndex >= 0 &&
                    node.layerIndex < static_cast<int>(inputs.layerSettings->size())) {
                    renderNode.layerJson = (*inputs.layerSettings)[node.layerIndex];
                } else if (node.layerIndex >= 0 && node.layerIndex < static_cast<int>(inputs.layers.size()) && inputs.layers[node.layerIndex]) {
                    renderNode.layerJson = inputs.layers[node.layerIndex]->Serialize();
                }
                Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(frameContext, node.id, node.layerType,
                    renderNode.layerJson, inputs.graphId, node.instanceUuid);
                break;
            case EditorNodeGraph::NodeKind::Output:
                renderNode.kind = RenderGraphNodeKind::Output;
                renderNode.outputChannelViewMode =
                    node.outputSettings.channelViewMode;
                break;
            case EditorNodeGraph::NodeKind::MaskGenerator:
                renderNode.kind = RenderGraphNodeKind::MaskGenerator;
                renderNode.maskKind = ToRenderMaskKind(node.maskKind);
                renderNode.rawCoverage = node.rawCoverage;
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
                if (node.value.value.logicalType != NodeMath::LogicalValueType::Scalar ||
                    node.value.value.availability != NodeMath::ValueAvailability::Known ||
                    !std::holds_alternative<double>(node.value.value.payload)) continue;
                renderNode.kind = RenderGraphNodeKind::Value;
                renderNode.scalarValue = std::get<double>(node.value.value.payload);
                break;
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

    for (const auto& binding : expansion.outputBindings) {
        const auto* authored = inputs.graph.FindNode(binding.authoredNodeId);
        if (authored && renderNodeIds.count(binding.expandedNodeId))
            snapshot.authoredOutputAliases.push_back({
                {inputs.graphId, authored->instanceUuid, binding.authoredSocketId},
                binding.expandedNodeId, binding.expandedSocketId});
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
        snapshot.links.push_back(RenderGraphLink{
            link.fromNodeId,
            link.fromSocketId,
            link.toNodeId,
            link.toSocketId
        });
    }

    EditorNodeGraph::GraphOutputContext outputContext;
    for (const RenderGraphNode& node : snapshot.nodes) {
        auto descriptor = UnknownLiveImageDescriptor("source.render");
        bool source = false;
        if (node.kind == RenderGraphNodeKind::Image) {
            if (HasValidImageDescriptor(node.image.sourceDescriptor)) {
                descriptor = node.image.sourceDescriptor;
                source = true;
            }
        } else if (node.kind == RenderGraphNodeKind::RawDevelopment ||
                   node.kind == RenderGraphNodeKind::RawProjectSourceSet) {
            source = true;
            if(node.kind == RenderGraphNodeKind::RawProjectSourceSet && !node.rawProjectSourceSet.resultAvailable) {
                outputContext.sourceDescriptors[EditorNodeGraph::GraphOutputIdentity(node.nodeId,
                    EditorNodeGraph::kImageOutputSocketId)] = std::move(descriptor);
                continue;
            }
            if (Stack::RawRecipe::IsViewTransformEnabled(node.rawDevelopment.recipe)) {
                DeclareRawDisplayOutput(descriptor,
                    node.rawDevelopment.recipe.viewTransform.layerJson.is_object()
                        ? node.rawDevelopment.recipe.viewTransform.layerJson.value("encodeSrgbOutput",node.rawDevelopment.recipe.technical.encodeSrgbOutput)
                        : node.rawDevelopment.recipe.technical.encodeSrgbOutput, "raw.display-output");
            } else {
                DeclareRawSceneOutput(descriptor,
                    node.rawDevelopment.recipe.technical.workingSpace, "raw.scene-output");
            }
            if (node.rawDevelopment.embeddedRawData) {
                const auto& metadata = node.rawDevelopment.embeddedRawData->metadata;
                Stack::NodeMath::SpatialDescriptor spatial;
                spatial.kind = Stack::NodeMath::SpatialExtentKind::Finite;
                spatial.fullWindow = spatial.dataWindow = { 0, 0,
                    Raw::DisplayWidth(metadata), Raw::DisplayHeight(metadata) };
                spatial.rasterOrigin = Stack::NodeMath::RasterOrigin::BottomLeft;
                spatial.pixelAspect = 1.0;
                descriptor.spatial = Stack::NodeMath::SemanticField<Stack::NodeMath::SpatialDescriptor>::Known(spatial);
            }
        } else if (node.kind == RenderGraphNodeKind::RawDecode || node.kind == RenderGraphNodeKind::RawDevelop) {
            source = true;
            DeclareRawSceneOutput(descriptor,
                node.kind == RenderGraphNodeKind::RawDecode ? node.rawDecode.settings.workingSpace : node.rawDevelop.settings.workingSpace,
                "raw.scene-output");
        } else if (node.kind == RenderGraphNodeKind::Layer && node.layerJson.is_object() &&
                   node.layerJson.value("type", std::string()) == "ViewTransform") {
            source = true;
            DeclareRawDisplayOutput(descriptor, node.layerJson.value("encodeSrgbOutput", false), "view-transform.display-output");
        }
        if (source) outputContext.sourceDescriptors[EditorNodeGraph::GraphOutputIdentity(node.nodeId,
            EditorNodeGraph::kImageOutputSocketId)] = std::move(descriptor);
    }
    if (inputs.outputContextOverrides) {
        for (const auto& entry : inputs.outputContextOverrides->sourceDescriptors)
            outputContext.sourceDescriptors[entry.first] = entry.second;
    }
    auto outputs = EditorNodeGraph::DescribeGraphOutputs(graph, outputContext);
    std::vector<std::string> fingerprints;
    for (RenderGraphNode& node : snapshot.nodes) {
        const auto* authored = graph.FindNode(node.nodeId);
        const auto socket = authored ? EditorNodeGraphDefinitions::DefaultOutputSocket(*authored) : EditorNodeGraph::kImageOutputSocketId;
        const auto output = outputs.find(EditorNodeGraph::GraphOutputIdentity(node.nodeId, socket));
        if (output != outputs.end()) {
            node.semanticDescriptor = output->second.descriptor;
            node.semanticDescriptorIdentity = Stack::NodeMath::DescriptorContentIdentity(node.semanticDescriptor);
        }
    }
    for (RenderGraphLink& link : snapshot.links) {
        const auto output = outputs.find(EditorNodeGraph::GraphOutputIdentity(link.fromNodeId, link.fromSocketId));
        if (output == outputs.end()) continue;
        link.semanticDescriptor = output->second.descriptor;
        link.semanticDescriptorIdentity = Stack::NodeMath::DescriptorContentIdentity(link.semanticDescriptor);
    }
    std::unordered_set<std::string> inspectedOutputs;
    for (const auto& link : snapshot.links)
        inspectedOutputs.insert(EditorNodeGraph::GraphOutputIdentity(link.fromNodeId, link.fromSocketId));
    inspectedOutputs.insert(EditorNodeGraph::GraphOutputIdentity(snapshot.outputNodeId, EditorNodeGraph::kImageOutputSocketId));
    for (const auto& [identity, output] : outputs) {
        fingerprints.push_back(identity + Stack::NodeMath::DescriptorContentIdentity(output.descriptor));
        if (!inspectedOutputs.count(identity)) continue;
        for (const auto& diagnostic : output.diagnostics) {
            if (std::none_of(snapshot.semanticDiagnostics.begin(), snapshot.semanticDiagnostics.end(), [&](const auto& existing) {
                return existing.semanticFingerprint == diagnostic.semanticFingerprint;
            })) snapshot.semanticDiagnostics.push_back(diagnostic);
        }
    }
    std::sort(fingerprints.begin(), fingerprints.end());
    std::string fingerprint;
    for (const auto& part : fingerprints) fingerprint += part + "\n";
    snapshot.semanticFingerprint = Stack::NodeMath::Sha256ContentIdentity(fingerprint);
    const auto output = outputs.find(EditorNodeGraph::GraphOutputIdentity(snapshot.outputNodeId, EditorNodeGraph::kImageOutputSocketId));
    if (output != outputs.end()) {
        snapshot.outputDescriptor = output->second.descriptor;
        snapshot.outputDescriptorIdentity = Stack::NodeMath::DescriptorContentIdentity(snapshot.outputDescriptor);
    }
    BindGraphImageContracts(snapshot);
    if (stageOutputNodeId > 0) return result;
    result.outputDescriptions = hasCompoundNode
        ? EditorNodeGraph::DescribeGraphOutputs(inputs.graph, outputContext) : std::move(outputs);
    result.publishSemantics = true;
    return result;
}

} // namespace Stack::Project
