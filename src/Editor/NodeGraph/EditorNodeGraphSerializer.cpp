#include "EditorNodeGraphSerializer.h"

#include "EditorNodeGraphDefinitions.h"
#include "UnifiedNodeDefinitionRegistry.h"
#include "Serialization/EditorNodeGraphCustomMaskSerialization.h"
#include "Serialization/EditorNodeGraphDevelopSerialization.h"
#include "Serialization/EditorNodeGraphImageSerialization.h"
#include "Serialization/EditorNodeGraphLutSerialization.h"
#include "Serialization/EditorNodeGraphRawSerialization.h"
#include "Serialization/EditorNodeGraphUtilitySerialization.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace EditorNodeGraph {
namespace {

std::string NodeKindToString(NodeKind kind) {
    switch (kind) {
        case NodeKind::Image: return "Image";
        case NodeKind::RawSource: return "RawSource";
        case NodeKind::RawDevelopment: return "RawDevelopment";
        case NodeKind::RawNeuralDenoise: return "RawNeuralDenoise";
        case NodeKind::RawDecode: return "RawDecode";
        case NodeKind::RawDevelop: return "RawDevelop";
        case NodeKind::RawDetailAutoMask: return "RawDetailAutoMask";
        case NodeKind::RawDetailFusion: return "RawDetailFusion";
        case NodeKind::HdrMerge: return "HdrMerge";
        case NodeKind::Mfsr: return "MFSR";
        case NodeKind::RawProjectFrame: return "RawProjectFrame";
        case NodeKind::MultiFrameDenoise: return "MultiFrameDenoise";
        case NodeKind::RawProjectSourceSet: return "RawProjectSourceSet";
        case NodeKind::Lut: return "Lut";
        case NodeKind::Layer: return "Layer";
        case NodeKind::Output: return "Output";
        case NodeKind::Composite: return "Composite";
        case NodeKind::Scope: return "Scope";
        case NodeKind::MaskGenerator: return "MaskGenerator";
        case NodeKind::MaskCombine: return "MaskCombine";
        case NodeKind::Mix: return "Mix";
        case NodeKind::Preview: return "Preview";
        case NodeKind::MaskUtility: return "MaskUtility";
        case NodeKind::ImageToMask: return "ImageToMask";
        case NodeKind::ImageGenerator: return "ImageGenerator";
        case NodeKind::ChannelSplit: return "ChannelSplit";
        case NodeKind::ChannelCombine: return "ChannelCombine";
        case NodeKind::ConstantChannel: return "ConstantChannel";
        case NodeKind::CustomMask: return "CustomMask";
        case NodeKind::DataMath: return "DataMath";
        case NodeKind::Value: return "Value";
        case NodeKind::FieldMean: return "FieldMean";
        case NodeKind::Reformat: return "Reformat";
        case NodeKind::TechnicalImage: return "TechnicalImage";
        case NodeKind::Compound: return "Compound";
        case NodeKind::FrequencyFilter: return "FrequencyFilter";
        case NodeKind::FrequencyResponse: return "FrequencyResponse";
        case NodeKind::FrequencyFft: return "FrequencyFft";
        case NodeKind::FrequencyIfft: return "FrequencyIfft";
        case NodeKind::SpectrumView: return "SpectrumView";
        case NodeKind::ApplyFrequencyResponse: return "ApplyFrequencyResponse";
        case NodeKind::CombineSpectra: return "CombineSpectra";
        case NodeKind::SpectrumSeparate: return "SpectrumSeparate";
        case NodeKind::SpectrumRecombine: return "SpectrumRecombine";
        case NodeKind::FrequencyMask: return "FrequencyMask";
        case NodeKind::SpectrumMath: return "SpectrumMath";
        case NodeKind::MagnitudePhase: return "MagnitudePhase";
        case NodeKind::SpectrumAnalyzer: return "SpectrumAnalyzer";
    }
    return "Layer";
}

nlohmann::json SerializeMfsrSettings(const Stack::Mfsr::MfsrSettings& settings) {
    return {
        { "schemaVersion", settings.schemaVersion },
        { "algorithmVersion", settings.algorithmVersion },
        { "scalePreset", Stack::Mfsr::MfsrScalePresetStableString(settings.scalePreset) },
        { "qualityPreset", Stack::Mfsr::MfsrQualityPresetStableString(settings.qualityPreset) },
        { "preferRawMosaicPath", settings.preferRawMosaicPath },
        { "maxInputFrames", settings.maxInputFrames }
    };
}

Stack::Mfsr::MfsrSettings DeserializeMfsrSettings(const nlohmann::json& value) {
    Stack::Mfsr::MfsrSettings settings;
    if (!value.is_object()) {
        return settings;
    }
    settings.schemaVersion = value.value("schemaVersion", settings.schemaVersion);
    settings.algorithmVersion = value.value("algorithmVersion", settings.algorithmVersion);
    settings.scalePreset = Stack::Mfsr::MfsrScalePresetFromStableString(
        value.value("scalePreset", std::string(Stack::Mfsr::MfsrScalePresetStableString(settings.scalePreset))));
    settings.qualityPreset = Stack::Mfsr::MfsrQualityPresetFromStableString(
        value.value("qualityPreset", std::string(Stack::Mfsr::MfsrQualityPresetStableString(settings.qualityPreset))));
    settings.preferRawMosaicPath = value.value("preferRawMosaicPath", settings.preferRawMosaicPath);
    settings.maxInputFrames = value.value("maxInputFrames", settings.maxInputFrames);
    return settings;
}

bool NodeUsuallyProducesFullImageForAverageMigration(const Node& node, const std::string& socketId) {
    if (socketId != kImageOutputSocketId) {
        return false;
    }
    switch (node.kind) {
        case NodeKind::Image:
        case NodeKind::RawDevelopment:
        case NodeKind::RawDecode:
        case NodeKind::RawDevelop:
        case NodeKind::RawDetailFusion:
        case NodeKind::HdrMerge:
        case NodeKind::Mfsr:
        case NodeKind::Lut:
        case NodeKind::Layer:
        case NodeKind::Mix:
        case NodeKind::ImageGenerator:
        case NodeKind::ChannelCombine:
        case NodeKind::FrequencyFft:
        case NodeKind::FrequencyIfft:
        case NodeKind::SpectrumView:
        case NodeKind::SpectrumMath:
            return true;
        case NodeKind::MagnitudePhase:
            return true;
        case NodeKind::DataMath:
            return node.dataMathMode != DataMathMode::Average;
        case NodeKind::TechnicalImage:
        case NodeKind::Reformat:
        case NodeKind::Compound:
            return true;
        default:
            return false;
    }
}

bool IsLegacyOutputComponentSocket(const std::string& socketId) {
    return socketId == "r" ||
        socketId == "g" ||
        socketId == "b" ||
        socketId == "a";
}

nlohmann::json SerializeOutputSettings(const OutputSettings& settings) {
    return {
        { "schemaVersion", OutputSettings::kSchemaVersion },
        { "channelViewMode",
          Stack::NodeMath::OutputChannelViewModeToken(
              settings.channelViewMode) }
    };
}

OutputSettings DeserializeOutputSettings(const nlohmann::json& value) {
    OutputSettings settings;
    if (!value.is_object()) {
        return settings;
    }
    Stack::NodeMath::OutputChannelViewMode parsed =
        Stack::NodeMath::OutputChannelViewMode::Neutral;
    if (Stack::NodeMath::ParseOutputChannelViewMode(
            value.value("channelViewMode", std::string("neutral")),
            parsed)) {
        settings.channelViewMode = parsed;
    }
    return settings;
}

nlohmann::json SerializeConstantChannelSettings(
    const ConstantChannelSettings& settings) {
    return {
        { "schemaVersion", ConstantChannelSettings::kSchemaVersion },
        { "value", std::isfinite(settings.value) ? settings.value : 1.0f },
        { "generatedOpaqueAlpha", settings.generatedOpaqueAlpha }
    };
}

ConstantChannelSettings DeserializeConstantChannelSettings(
    const nlohmann::json& value) {
    ConstantChannelSettings settings;
    if (!value.is_object()) {
        return settings;
    }
    const float parsed =
        value.contains("value") &&
        value["value"].is_number()
            ? value["value"].get<float>()
            : settings.value;
    settings.value = std::isfinite(parsed) ? parsed : 1.0f;
    settings.generatedOpaqueAlpha =
        value.value("generatedOpaqueAlpha", false);
    return settings;
}

nlohmann::json SerializeImageCombineSettings(
    const ImageCombineSettings& settings) {
    return {
        { "schemaVersion", ImageCombineSettings::kSchemaVersion },
        { "autoAlphaSuppressed", settings.autoAlphaSuppressed }
    };
}

ImageCombineSettings DeserializeImageCombineSettings(
    const nlohmann::json& value) {
    ImageCombineSettings settings;
    if (value.is_object()) {
        settings.autoAlphaSuppressed =
            value.value("autoAlphaSuppressed", false);
    }
    return settings;
}

std::vector<unsigned char> BuildImagePayloadStoragePngBytes(const ImagePayload& image) {
    if (!image.pngBytes.empty()) {
        return image.pngBytes;
    }
    return EncodeImagePayloadPngForStorage(image.pixels, image.width, image.height, image.channels);
}

} // namespace

nlohmann::json ExtractLayerArray(const nlohmann::json& pipelineData) {
    if (pipelineData.is_array()) {
        return pipelineData;
    }
    if (pipelineData.is_object()) {
        const nlohmann::json layers = pipelineData.value("layers", nlohmann::json::array());
        return layers.is_array() ? layers : nlohmann::json::array();
    }
    return nlohmann::json::array();
}

nlohmann::json SerializeGraphPayload(const nlohmann::json& layerArray, const Graph& graph) {
    nlohmann::json root = nlohmann::json::object();
    root["layers"] = layerArray.is_array() ? layerArray : nlohmann::json::array();

    nlohmann::json graphJson = nlohmann::json::object();
    graphJson["version"] = 8;
    graphJson["allowNoOutput"] = graph.AllowsNoOutput();
    graphJson["nextNodeId"] = graph.GetNextNodeId();
    graphJson["nextGroupId"] = graph.GetNextGroupId();
    graphJson["selectedNodeId"] = graph.GetSelectedNodeId();
    graphJson["activeImageNodeId"] = graph.GetActiveImageNodeId();
    graphJson["outputNodeId"] = graph.GetOutputNodeId();
    graphJson["outputNodeIds"] = graph.GetOutputNodeIds();

    nlohmann::json nodesJson = nlohmann::json::array();
    for (const Node& node : graph.GetNodes()) {
        nlohmann::json item = nlohmann::json::object();
        item["id"] = node.id;
        item["instanceUuid"] = node.instanceUuid;
        item["kind"] = NodeKindToString(node.kind);
        item["layerIndex"] = node.layerIndex;
        item["typeId"] = node.typeId;
        item["title"] = node.title;
        item["x"] = node.position.x;
        item["y"] = node.position.y;
        item["expanded"] = node.expanded;
        item["scopeKind"] = ScopeKindToString(node.scopeKind);
        item["maskKind"] = MaskGeneratorKindToString(node.maskKind);
        item["maskSettings"] = SerializeMaskSettings(node.maskSettings);
        item["maskCombineMode"] = MaskCombineModeToString(node.maskCombineMode);
        item["maskUtilityKind"] = MaskUtilityKindToString(node.maskUtilityKind);
        item["maskUtilitySettings"] = SerializeMaskUtilitySettings(node.maskUtilitySettings);
        item["imageToMaskKind"] = ImageToMaskKindToString(node.imageToMaskKind);
        item["imageToMaskSettings"] = SerializeImageToMaskSettings(node.imageToMaskSettings);
        item["imageGeneratorKind"] = ImageGeneratorKindToString(node.imageGeneratorKind);
        item["imageGeneratorSettings"] = SerializeImageGeneratorSettings(node.imageGeneratorSettings);
        item["mixBlendMode"] = MixBlendModeToString(node.mixBlendMode);
        item["mixFactor"] = node.mixFactor;
        item["dataMathMode"] = DataMathModeToString(node.dataMathMode);
        item["dataMathSettings"] = SerializeDataMathSettings(node.dataMathSettings);
        item["technicalImageSettings"] = SerializeTechnicalImageSettings(node.technicalImageSettings);
        item["reformatSettings"] = SerializeReformatSettings(node.reformatSettings);
        item["frequencyFilterSettings"] = SerializeFrequencyFilterSettings(node.frequencyFilterSettings);
        item["frequencyResponseSettings"] = SerializeFrequencyResponseSettings(node.frequencyResponseSettings);
        item["frequencyFftSettings"] = SerializeFrequencyFftSettings(node.frequencyFftSettings);
        item["frequencyIfftSettings"] = SerializeFrequencyFftSettings(node.frequencyIfftSettings);
        item["spectrumViewSettings"] = SerializeSpectrumViewSettings(node.spectrumViewSettings);
        item["applyFrequencyResponseSettings"] =
            SerializeApplyFrequencyResponseSettings(node.applyFrequencyResponseSettings);
        item["combineSpectraSettings"] =
            SerializeCombineSpectraSettings(node.combineSpectraSettings);
        item["frequencyMaskShape"] = FrequencyMaskShapeToString(node.frequencyMaskShape);
        item["frequencyMaskSettings"] = SerializeFrequencyMaskSettings(node.frequencyMaskSettings);
        item["spectrumMathMode"] = SpectrumMathModeToString(node.spectrumMathMode);
        item["spectrumMathSettings"] = SerializeSpectrumMathSettings(node.spectrumMathSettings);
        item["magnitudePhaseMode"] = MagnitudePhaseModeToString(node.magnitudePhaseMode);
        item["magnitudePhaseSettings"] = SerializeMagnitudePhaseSettings(node.magnitudePhaseSettings);
        item["spectrumAnalyzerMode"] = SpectrumAnalyzerModeToString(node.spectrumAnalyzerMode);
        item["spectrumAnalyzerSettings"] = SerializeSpectrumAnalyzerSettings(node.spectrumAnalyzerSettings);
        item["exposedParameterIds"] = node.exposedParameterIds;
        item["outputEnabled"] = node.outputEnabled;
        if (node.kind == NodeKind::Output) {
            item["outputSettings"] =
                SerializeOutputSettings(node.outputSettings);
        } else if (node.kind == NodeKind::ConstantChannel) {
            item["constantChannelSettings"] =
                SerializeConstantChannelSettings(
                    node.constantChannelSettings);
        } else if (node.kind == NodeKind::ChannelCombine) {
            item["imageCombineSettings"] =
                SerializeImageCombineSettings(
                    node.imageCombineSettings);
        }
        item["definition"] = {
            { "id", node.definitionId },
            { "version", node.definitionVersion },
            { "contentHash", node.definitionHash }
        };

        if (node.kind == NodeKind::Image) {
            item["label"] = node.image.label;
            item["sourcePath"] = node.image.sourcePath;
            item["width"] = node.image.width;
            item["height"] = node.image.height;
            item["channels"] = node.image.channels;
            item["originalChannels"] = node.image.originalChannels;
            item["pngBytes"] = nlohmann::json::binary(BuildImagePayloadStoragePngBytes(node.image));
            item["sourceColorMetadata"] =
                Stack::NodeMath::SerializeSourceColorMetadata(node.image.sourceColorMetadata);
        } else if (node.kind == NodeKind::Value) {
            item["value"] = Stack::NodeMath::SerializeFirstClassValue(node.value.value);
        } else if (node.kind == NodeKind::Compound) {
            item["compoundInstance"] = Stack::NodeMath::SerializeCompoundInstance(
                node.compound.instance);
        } else if (node.kind == NodeKind::RawSource) {
            item["label"] = node.rawSource.label;
            item["sourcePath"] = node.rawSource.sourcePath;
            item["rawMetadata"] = SerializeRawMetadata(node.rawSource.metadata);
        } else if (node.kind == NodeKind::RawDevelopment) {
            item["rawRecipe"] = Stack::RawRecipe::SerializeRecipe(node.rawDevelopment.recipe);
            item["rawProjectStatus"] = node.rawDevelopment.projectStatus;
            item["rawEdited"] = node.rawDevelopment.edited;
            item["rawAutosaved"] = node.rawDevelopment.autosaved;
        } else if (node.kind == NodeKind::RawNeuralDenoise) {
            item["neuralDenoiseSettings"] = NeuralDenoise::SerializeSettings(node.rawNeuralDenoise.settings);
        } else if (node.kind == NodeKind::RawDecode) {
            item["rawSettings"] = SerializeRawSettings(node.rawDecode.settings);
        } else if (node.kind == NodeKind::RawDevelop) {
            item["rawSettings"] = SerializeRawSettings(node.rawDevelop.settings);
            item["scenePrepEnabled"] = node.rawDevelop.scenePrepEnabled;
            item["scenePrepSettings"] = SerializeRawDetailFusionSettings(node.rawDevelop.scenePrepSettings);
            item["integratedToneEnabled"] = node.rawDevelop.integratedToneEnabled;
            item["integratedToneLayer"] = node.rawDevelop.integratedToneLayerJson;
            item["developSubjectImportance"] =
                SerializeDevelopSubjectImportanceMap(node.rawDevelop.subjectImportance);
            item["developAutoGuidance"] = {
                { "autoIntent", EditorNodeGraph::DevelopAutoIntentStableString(node.rawDevelop.autoGuidance.intent) },
                { "autoStrength", node.rawDevelop.autoGuidance.autoStrength },
                { "exposureBias", node.rawDevelop.autoGuidance.exposureBias },
                { "dynamicRange", node.rawDevelop.autoGuidance.dynamicRange },
                { "shadowLift", node.rawDevelop.autoGuidance.shadowLift },
                { "highlightGuard", node.rawDevelop.autoGuidance.highlightGuard },
                { "highlightCharacter", node.rawDevelop.autoGuidance.highlightCharacter },
                { "contrastBias", node.rawDevelop.autoGuidance.contrastBias },
                { "subjectSceneBias", node.rawDevelop.autoGuidance.subjectSceneBias },
                { "moodReadabilityBias", node.rawDevelop.autoGuidance.moodReadabilityBias }
            };
            item["uiMode"] =
                node.rawDevelop.uiMode == EditorNodeGraph::RawDevelopUiMode::Manual ? "Manual" : "Auto";
        } else if (node.kind == NodeKind::RawDetailAutoMask) {
            item["rawDetailAutoMaskSettings"] = SerializeRawDetailFusionSettings(node.rawDetailAutoMask.settings);
        } else if (node.kind == NodeKind::RawDetailFusion) {
            item["rawDetailFusionSettings"] = SerializeRawDetailFusionSettings(node.rawDetailFusion.settings);
        } else if (node.kind == NodeKind::HdrMerge) {
            item["hdrMergeSettings"] = SerializeHdrMergeSettings(node.hdrMerge.settings);
        } else if (node.kind == NodeKind::Mfsr) {
            item["mfsrSettings"] = SerializeMfsrSettings(node.mfsr.settings);
            item["mfsrHasPlaceholderCachedOutput"] = node.mfsr.hasPlaceholderCachedOutput;
            item["mfsrPlaceholderStatus"] = node.mfsr.placeholderStatus;
            item["mfsrError"] = node.mfsr.errorMessage;
        } else if (node.kind == NodeKind::RawProjectFrame) {
            item["sourceSetId"] = node.rawProjectFrame.sourceSetId;
            item["frameId"] = node.rawProjectFrame.frameId;
            item["assetId"] = node.rawProjectFrame.assetId;
            item["frameLabel"] = node.rawProjectFrame.displayLabel;
            item["frameCompatibilityStatus"] =
                node.rawProjectFrame.compatibilityStatus;
            item["frameEnabled"] = node.rawProjectFrame.enabled;
            item["frameReference"] = node.rawProjectFrame.reference;
            item["frameManaged"] = node.rawProjectFrame.managed;
            item["frameQuarantined"] = node.rawProjectFrame.quarantined;
        } else if (node.kind == NodeKind::MultiFrameDenoise) {
            item["sourceSetId"] = node.multiFrameDenoise.sourceSetId;
            item["mfdStatus"] = node.multiFrameDenoise.presentationStatus;
            item["mfdResultState"] = node.multiFrameDenoise.resultState;
            item["mfdInternalViewTransformEnabled"] =
                node.multiFrameDenoise.internalViewTransformEnabled;
            item["mfdManaged"] = node.multiFrameDenoise.managed;
            item["mfdQuarantined"] = node.multiFrameDenoise.quarantined;
            item["mfdFrameBindings"] = nlohmann::json::array();
            for (const MfdFrameBinding& binding :
                 node.multiFrameDenoise.frameBindings) {
                item["mfdFrameBindings"].push_back({
                    { "frameId", binding.frameId },
                    { "socketId", binding.socketId },
                    { "label", binding.label },
                    { "enabled", binding.enabled },
                    { "reference", binding.reference }
                });
            }
        } else if (node.kind == NodeKind::RawProjectSourceSet) {
            item["sourceSetId"] = node.rawProjectSourceSet.sourceSetId;
            item["sourceSetStatus"] = node.rawProjectSourceSet.presentationStatus;
            item["sourceSetManaged"] = node.rawProjectSourceSet.managed;
            item["sourceSetQuarantined"] = node.rawProjectSourceSet.quarantined;
        } else if (node.kind == NodeKind::Lut) {
            item["lut"] = SerializeLutPayload(node.lut);
        } else if (node.kind == NodeKind::CustomMask) {
            item["customMask"] = SerializeCustomMaskPayload(node.customMask);
        }

        nodesJson.push_back(std::move(item));
    }
    graphJson["nodes"] = std::move(nodesJson);

    nlohmann::json linksJson = nlohmann::json::array();
    for (const Link& link : graph.GetLinks()) {
        linksJson.push_back({
            { "fromNodeId", link.fromNodeId },
            { "fromSocket", link.fromSocketId },
            { "toNodeId", link.toNodeId },
            { "toSocket", link.toSocketId },
            { "ownership", link.ownership == Link::Ownership::ManagedSourceBinding
                ? "managed-source-binding"
                : "user" },
            { "bindingId", link.bindingId }
        });
    }
    graphJson["links"] = std::move(linksJson);

    nlohmann::json groupsJson = nlohmann::json::array();
    for (const NodeGroup& group : graph.GetGroups()) {
        groupsJson.push_back({
            { "id", group.id },
            { "title", group.title },
            { "x", group.position.x },
            { "y", group.position.y },
            { "width", group.size.x },
            { "height", group.size.y }
        });
    }
    graphJson["groups"] = std::move(groupsJson);

    nlohmann::json compoundDefinitions = nlohmann::json::array();
    for (const Stack::NodeMath::CompoundDefinition& definition : graph.GetCompoundDefinitions()) {
        compoundDefinitions.push_back(Stack::NodeMath::SerializeCompoundDefinition(definition));
    }
    graphJson["compoundDefinitions"] = std::move(compoundDefinitions);

    root["nodeGraph"] = std::move(graphJson);
    return root;
}

void RemoveGraphLayoutFromPayload(nlohmann::json& pipelineData) {
    if (!pipelineData.is_object()) {
        return;
    }

    auto graphIt = pipelineData.find("nodeGraph");
    if (graphIt == pipelineData.end() || !graphIt->is_object()) {
        return;
    }

    nlohmann::json& graphJson = *graphIt;
    auto nodesIt = graphJson.find("nodes");
    if (nodesIt != graphJson.end() && nodesIt->is_array()) {
        for (nlohmann::json& nodeJson : *nodesIt) {
            if (!nodeJson.is_object()) {
                continue;
            }
            nodeJson.erase("x");
            nodeJson.erase("y");
        }
    }

    // Groups are canvas-only organization and cannot be represented without
    // their position and bounds.
    graphJson.erase("groups");
    graphJson.erase("nextGroupId");
    graphJson.erase("selectedNodeId");
}

void DeserializeGraphPayload(
    const nlohmann::json& pipelineData,
    Graph& graph,
    int layerCount,
    const std::vector<unsigned char>& fallbackSourcePixels,
    int fallbackSourceWidth,
    int fallbackSourceHeight,
    int fallbackSourceChannels) {

    graph.Clear();

    const bool hasFallbackSource =
        !fallbackSourcePixels.empty() && fallbackSourceWidth > 0 && fallbackSourceHeight > 0;

    if (!pipelineData.is_object() || !pipelineData.contains("nodeGraph")) {
        graph.ResetFromLayers(layerCount, hasFallbackSource);
        if (hasFallbackSource) {
            if (Node* imageNode = graph.FindNode(graph.GetActiveImageNodeId())) {
                imageNode->image.label = "Image";
                imageNode->image.width = fallbackSourceWidth;
                imageNode->image.height = fallbackSourceHeight;
                imageNode->image.channels = std::max(1, fallbackSourceChannels);
                imageNode->image.pixels = fallbackSourcePixels;
                imageNode->image.pngBytes = EncodeImagePayloadPngForStorage(
                    fallbackSourcePixels,
                    fallbackSourceWidth,
                    fallbackSourceHeight,
                    std::max(1, fallbackSourceChannels));
                InvalidateImagePayloadRuntime(imageNode->image);
            }
        }
        return;
    }

    const nlohmann::json graphJson = pipelineData.value("nodeGraph", nlohmann::json::object());
    const int graphVersion = graphJson.value("version", 0);
    graph.SetAllowNoOutput(graphJson.value("allowNoOutput", false));
    const nlohmann::json compoundDefinitionsJson =
        graphJson.value("compoundDefinitions", nlohmann::json::array());
    if (compoundDefinitionsJson.is_array()) {
        for (const nlohmann::json& item : compoundDefinitionsJson) {
            Stack::NodeMath::CompoundDefinition definition;
            if (Stack::NodeMath::DeserializeCompoundDefinition(item, definition, nullptr)) {
                graph.GetCompoundDefinitions().push_back(std::move(definition));
            }
        }
    }
    const nlohmann::json nodesJson = graphJson.value("nodes", nlohmann::json::array());
    const nlohmann::json linksJson = graphJson.value("links", nlohmann::json::array());
    std::unordered_map<int, int> legacyOutputComponentCounts;
    if (linksJson.is_array()) {
        for (const nlohmann::json& item : linksJson) {
            if (!item.is_object()) continue;
            const std::string toSocket =
                item.value("toSocket", std::string());
            if (IsLegacyOutputComponentSocket(toSocket)) {
                ++legacyOutputComponentCounts[
                    item.value("toNodeId", item.value("to", 0))];
            }
        }
    }

    int maxNodeId = 0;
    for (const nlohmann::json& item : nodesJson) {
        if (!item.is_object()) continue;

        Node node;
        bool compoundInstanceValid = true;
        node.id = item.value("id", 0);
        node.instanceUuid = item.value("instanceUuid", std::string());
        if (!Stack::NodeMath::IsValidCanonicalUuid(node.instanceUuid)) {
            node.instanceUuid = Stack::NodeMath::GenerateCanonicalUuid();
        }
        node.layerIndex = item.value("layerIndex", -1);
        node.typeId = item.value("typeId", std::string());
        node.title = item.value("title", std::string());
        node.position.x = item.value("x", 0.0f);
        node.position.y = item.value("y", 0.0f);
        node.expanded = item.value("expanded", false);
        node.outputEnabled = item.value("outputEnabled", true);
        const nlohmann::json savedDefinition = item.value("definition", nlohmann::json::object());
        const std::string savedDefinitionId = savedDefinition.value("id", std::string());
        const std::string savedDefinitionVersion = savedDefinition.value("version", std::string());
        const std::string savedDefinitionHash = savedDefinition.value("contentHash", std::string());

        const std::string kind = item.value("kind", std::string("Layer"));
        if (kind == "ExportBoundsSettings") {
            continue;
        }

        if (kind == "Image") {
            node.kind = NodeKind::Image;
            node.image.label = item.value("label", node.title.empty() ? std::string("Image") : node.title);
            node.image.sourcePath = item.value("sourcePath", std::string());
            DecodeImagePayloadPngBytes(ReadBinaryJsonBytes(item.value("pngBytes", nlohmann::json())), node.image);
            node.image.originalChannels = item.value("originalChannels", node.image.originalChannels);
            if (item.contains("sourceColorMetadata")) {
                std::vector<Stack::NodeMath::ContractIssue> issues;
                Stack::NodeMath::ParseSourceColorMetadata(
                    item["sourceColorMetadata"], node.image.sourceColorMetadata, issues);
            } else {
                node.image.sourceColorMetadata = Stack::NodeMath::InspectSourceColorMetadata(
                    node.image.pngBytes, node.image.width, node.image.height,
                    node.image.originalChannels, Stack::NodeMath::LogicalPrecision::UInt8,
                    node.image.sourcePath.empty() ? node.image.label : node.image.sourcePath);
            }
            if (node.title.empty()) node.title = node.image.label.empty() ? "Image" : node.image.label;
        } else if (kind == "RawSource") {
            node.kind = NodeKind::RawSource;
            node.rawSource.label = item.value("label", node.title.empty() ? std::string("RAW") : node.title);
            node.rawSource.sourcePath = item.value("sourcePath", std::string());
            node.rawSource.metadata = DeserializeRawMetadata(item.value("rawMetadata", nlohmann::json::object()));
            if (node.rawSource.metadata.sourcePath.empty()) {
                node.rawSource.metadata.sourcePath = node.rawSource.sourcePath;
            }
            if (node.title.empty()) node.title = node.rawSource.label.empty() ? "RAW" : node.rawSource.label;
        } else if (kind == "RawDevelopment") {
            node.kind = NodeKind::RawDevelopment;
            node.rawDevelopment.recipe =
                Stack::RawRecipe::DeserializeRecipe(item.value("rawRecipe", nlohmann::json::object()));
            node.rawDevelopment.projectStatus = item.value("rawProjectStatus", std::string("Unknown"));
            node.rawDevelopment.edited = item.value("rawEdited", false);
            node.rawDevelopment.autosaved = item.value("rawAutosaved", false);
            if (node.title.empty()) node.title = "RAW Development";
        } else if (kind == "RawNeuralDenoise") {
            node.kind = NodeKind::RawNeuralDenoise;
            node.rawNeuralDenoise.settings = NeuralDenoise::DeserializeSettings(item.value("neuralDenoiseSettings", nlohmann::json::object()));
            if (node.title.empty()) node.title = "RAW/CFA Neural Denoise";
        } else if (kind == "RawDecode") {
            node.kind = NodeKind::RawDecode;
            node.rawDecode.settings = DeserializeRawSettings(item.value("rawSettings", nlohmann::json::object()));
            if (node.title.empty() || node.title == "RAW Decode") node.title = "RAW Decode";
        } else if (kind == "RawDevelop") {
            node.kind = NodeKind::RawDevelop;
            node.rawDevelop.settings = DeserializeRawSettings(item.value("rawSettings", nlohmann::json::object()));
            node.rawDevelop.scenePrepEnabled = item.value("scenePrepEnabled", node.rawDevelop.scenePrepEnabled);
            node.rawDevelop.scenePrepSettings = DeserializeRawDetailFusionSettings(item.value("scenePrepSettings", nlohmann::json::object()));
            node.rawDevelop.integratedToneEnabled = item.value("integratedToneEnabled", true);
            node.rawDevelop.integratedToneLayerJson = item.value("integratedToneLayer", nlohmann::json::object());
            node.rawDevelop.subjectImportance =
                DeserializeDevelopSubjectImportanceMap(
                    item.value("developSubjectImportance", nlohmann::json::object()));
            const nlohmann::json autoGuidance = item.value("developAutoGuidance", nlohmann::json::object());
            node.rawDevelop.autoGuidance.intent = EditorNodeGraph::DevelopAutoIntentFromStableString(
                autoGuidance.value("autoIntent", std::string("NaturalFinished")));
            node.rawDevelop.autoGuidance.autoStrength = autoGuidance.value("autoStrength", node.rawDevelop.autoGuidance.autoStrength);
            node.rawDevelop.autoGuidance.exposureBias = autoGuidance.value("exposureBias", node.rawDevelop.autoGuidance.exposureBias);
            node.rawDevelop.autoGuidance.dynamicRange = autoGuidance.value("dynamicRange", node.rawDevelop.autoGuidance.dynamicRange);
            node.rawDevelop.autoGuidance.shadowLift = autoGuidance.value("shadowLift", node.rawDevelop.autoGuidance.shadowLift);
            node.rawDevelop.autoGuidance.highlightGuard = autoGuidance.value("highlightGuard", node.rawDevelop.autoGuidance.highlightGuard);
            node.rawDevelop.autoGuidance.highlightCharacter = autoGuidance.value("highlightCharacter", node.rawDevelop.autoGuidance.highlightCharacter);
            node.rawDevelop.autoGuidance.contrastBias = autoGuidance.value("contrastBias", node.rawDevelop.autoGuidance.contrastBias);
            node.rawDevelop.autoGuidance.subjectSceneBias = autoGuidance.value("subjectSceneBias", node.rawDevelop.autoGuidance.subjectSceneBias);
            node.rawDevelop.autoGuidance.moodReadabilityBias = autoGuidance.value("moodReadabilityBias", node.rawDevelop.autoGuidance.moodReadabilityBias);
            // Auto mode is archived. Preserve its authored settings and open
            // every Develop node in the manual editor.
            node.rawDevelop.uiMode = EditorNodeGraph::RawDevelopUiMode::Manual;
            if (node.title.empty() || node.title == "RAW Develop") node.title = "Develop";
        } else if (kind == "RawDetailAutoMask") {
            node.kind = NodeKind::RawDetailAutoMask;
            node.rawDetailAutoMask.settings = DeserializeRawDetailFusionSettings(item.value("rawDetailAutoMaskSettings", nlohmann::json::object()));
            if (node.title.empty()) node.title = "RAW Detail Auto Mask";
        } else if (kind == "RawDetailFusion") {
            node.kind = NodeKind::RawDetailFusion;
            node.rawDetailFusion.settings = DeserializeRawDetailFusionSettings(item.value("rawDetailFusionSettings", nlohmann::json::object()));
            if (node.title.empty() || node.title == "RAW Detail Fusion" || node.title == "Auto Gain") node.title = "Pre-Local Exposure";
        } else if (kind == "HdrMerge") {
            node.kind = NodeKind::HdrMerge;
            node.hdrMerge.settings = DeserializeHdrMergeSettings(item.value("hdrMergeSettings", nlohmann::json::object()));
            if (node.title.empty()) node.title = "HDR Merge";
        } else if (kind == "MFSR" || kind == "Mfsr") {
            node.kind = NodeKind::Mfsr;
            node.mfsr.settings = DeserializeMfsrSettings(item.value("mfsrSettings", nlohmann::json::object()));
            node.mfsr.hasPlaceholderCachedOutput = item.value("mfsrHasPlaceholderCachedOutput", false);
            node.mfsr.placeholderStatus = item.value("mfsrPlaceholderStatus", std::string(kMfsrPhase2PlaceholderStatus));
            node.mfsr.errorMessage = item.value("mfsrError", std::string());
            if (node.title.empty() || node.title == "Multi-Frame Super Resolution") node.title = "MFSR";
        } else if (kind == "RawProjectFrame") {
            node.kind = NodeKind::RawProjectFrame;
            node.rawProjectFrame.sourceSetId = item.value("sourceSetId", std::string());
            node.rawProjectFrame.frameId = item.value("frameId", std::string());
            node.rawProjectFrame.assetId = item.value("assetId", std::string());
            node.rawProjectFrame.displayLabel = item.value("frameLabel", std::string());
            node.rawProjectFrame.compatibilityStatus =
                item.value("frameCompatibilityStatus", std::string());
            node.rawProjectFrame.enabled = item.value("frameEnabled", true);
            node.rawProjectFrame.reference = item.value("frameReference", false);
            node.rawProjectFrame.managed = item.value("frameManaged", true);
            node.rawProjectFrame.quarantined = item.value("frameQuarantined", false);
            if (node.title.empty()) node.title = node.rawProjectFrame.displayLabel.empty()
                ? "RAW Frame"
                : node.rawProjectFrame.displayLabel;
        } else if (kind == "MultiFrameDenoise") {
            node.kind = NodeKind::MultiFrameDenoise;
            node.multiFrameDenoise.sourceSetId = item.value("sourceSetId", std::string());
            node.multiFrameDenoise.presentationStatus = item.value(
                "mfdStatus", std::string(kMfdAwaitingProcessingStatus));
            node.multiFrameDenoise.resultState =
                item.value("mfdResultState", std::string("unavailable"));
            node.multiFrameDenoise.internalViewTransformEnabled =
                item.value("mfdInternalViewTransformEnabled", true);
            node.multiFrameDenoise.managed = item.value("mfdManaged", true);
            node.multiFrameDenoise.quarantined = item.value("mfdQuarantined", false);
            const nlohmann::json bindings = item.value(
                "mfdFrameBindings", nlohmann::json::array());
            if (bindings.is_array()) {
                for (const nlohmann::json& bindingValue : bindings) {
                    if (!bindingValue.is_object()) continue;
                    MfdFrameBinding binding;
                    binding.frameId = bindingValue.value("frameId", std::string());
                    binding.socketId = bindingValue.value(
                        "socketId", MfdFrameInputSocketId(binding.frameId));
                    binding.label = bindingValue.value("label", std::string());
                    binding.enabled = bindingValue.value("enabled", true);
                    binding.reference = bindingValue.value("reference", false);
                    node.multiFrameDenoise.frameBindings.push_back(std::move(binding));
                }
            }
            if (node.title.empty()) node.title = "MFD";
        } else if (kind == "RawProjectSourceSet") {
            node.kind = NodeKind::RawProjectSourceSet;
            node.rawProjectSourceSet.sourceSetId =
                item.value("sourceSetId", std::string());
            node.rawProjectSourceSet.presentationStatus = item.value(
                "sourceSetStatus",
                std::string(kRawProjectSourceSetUnavailableStatus));
            node.rawProjectSourceSet.managed = item.value("sourceSetManaged", true);
            node.rawProjectSourceSet.quarantined =
                item.value("sourceSetQuarantined", false);
            if (node.title.empty()) node.title = "RAW Project Source Set";
        } else if (kind == "Lut" || kind == "LUT") {
            node.kind = NodeKind::Lut;
            node.lut = DeserializeLutPayload(item.value("lut", nlohmann::json::object()));
            if (node.title.empty()) node.title = "LUT";
        } else if (kind == "Output") {
            node.kind = NodeKind::Output;
            node.outputSettings = DeserializeOutputSettings(
                item.value("outputSettings", nlohmann::json::object()));
            if (node.title.empty()) node.title = "Output";
        } else if (kind == "Composite") {
            node.kind = NodeKind::Composite;
            if (node.title.empty()) node.title = "Composite";
        } else if (kind == "Scope") {
            node.kind = NodeKind::Scope;
            node.scopeKind = ScopeKindFromString(item.value("scopeKind", std::string("Histogram")));
            if (node.title.empty()) {
                node.title = ScopeKindToString(node.scopeKind);
            }
        } else if (kind == "MaskGenerator") {
            node.kind = NodeKind::MaskGenerator;
            node.maskKind = MaskGeneratorKindFromString(item.value("maskKind", std::string("Solid")));
            node.maskSettings = DeserializeMaskSettings(item.value("maskSettings", nlohmann::json::object()));
            if (node.title.empty()) {
                node.title = node.maskKind == MaskGeneratorKind::Solid ? "Solid Mask" :
                    (node.maskKind == MaskGeneratorKind::LinearGradient ? "Linear Gradient Mask" :
                    (node.maskKind == MaskGeneratorKind::RadialGradient ? "Radial Gradient Mask" : "Noise Mask"));
            }
        } else if (kind == "MaskCombine") {
            node.kind = NodeKind::MaskCombine;
            node.maskCombineMode = MaskCombineModeFromString(item.value("maskCombineMode", std::string("Intersect")));
            if (node.title.empty() ||
                node.title == "Add Mask" ||
                node.title == "Subtract Mask" ||
                node.title == "Intersect Mask" ||
                node.title == "Exclude Mask" ||
                node.title == "Add Scalars" ||
                node.title == "Subtract Scalars" ||
                node.title == "Intersect Scalars" ||
                node.title == "Difference Scalars" ||
                node.title == "Difference Mask") {
                EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            }
        } else if (kind == "CustomMask") {
            node.kind = NodeKind::CustomMask;
            node.customMask = DeserializeCustomMaskPayload(item.value("customMask", nlohmann::json::object()));
            if (node.title.empty()) node.title = "Custom Mask";
        } else if (kind == "Mix") {
            node.kind = NodeKind::Mix;
            node.mixBlendMode = MixBlendModeFromString(item.value("mixBlendMode", std::string("Normal")));
            node.mixFactor = item.value("mixFactor", 0.5f);
            if (node.title.empty()) {
                node.title = "Blend Images";
            }
        } else if (kind == "DataMath") {
            node.kind = NodeKind::DataMath;
            node.dataMathMode = DataMathModeFromString(item.value("dataMathMode", std::string("Clamp")));
            node.dataMathSettings = DeserializeDataMathSettings(item.value("dataMathSettings", nlohmann::json::object()));
            if (node.title.empty() ||
                node.title == "Clamp Data" ||
                node.title == "Add Data" ||
                node.title == "Subtract Data" ||
                node.title == "Multiply Data" ||
                node.title == "Divide Data" ||
                node.title == "Average Data" ||
                node.title == "Average Images Data" ||
                node.title == "Minimum Data" ||
                node.title == "Maximum Data" ||
                node.title == "Difference Data" ||
                node.title == "Remap Data") {
                EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            }
        } else if (kind == "Value") {
            node.kind = NodeKind::Value;
            const nlohmann::json savedValue = item.value("value", nlohmann::json::object());
            std::string valueError;
            if (!Stack::NodeMath::DeserializeFirstClassValue(savedValue, node.value.value, &valueError)) {
                nlohmann::json missingValue = savedValue.is_object()
                    ? savedValue : nlohmann::json::object();
                missingValue["schemaVersion"] = Stack::NodeMath::kFirstClassValueSchemaVersion;
                missingValue["availability"] = "missing";
                missingValue["message"] = valueError.empty() ? "Saved value is invalid." : valueError;
                missingValue.erase("payload");
                if (!Stack::NodeMath::DeserializeFirstClassValue(missingValue, node.value.value, nullptr)) {
                    node.value.value = Stack::NodeMath::MakeMissingValue(
                        Stack::NodeMath::LogicalValueType::Invalid,
                        Stack::NodeMath::ValueStorageClass::Uniform,
                        valueError.empty() ? "Saved value is invalid." : valueError);
                }
            }
            EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
        } else if (kind == "FieldMean") {
            node.kind = NodeKind::FieldMean;
            EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
        } else if (kind == "Reformat") {
            node.kind = NodeKind::Reformat;
            node.reformatSettings = DeserializeReformatSettings(
                item.value("reformatSettings", nlohmann::json::object()));
            EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
        } else if (kind == "TechnicalImage") {
            node.kind = NodeKind::TechnicalImage;
            node.technicalImageSettings = DeserializeTechnicalImageSettings(
                item.value("technicalImageSettings", nlohmann::json::object()));
            EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
        } else if (kind == "Compound") {
            node.kind = NodeKind::Compound;
            std::string compoundError;
            if (!Stack::NodeMath::DeserializeCompoundInstance(
                    item.value("compoundInstance", nlohmann::json::object()),
                    node.compound.instance,
                    &compoundError)) {
                compoundInstanceValid = false;
                node.compound.instance.instanceUuid = node.instanceUuid;
                const std::optional<Stack::NodeMath::SemanticVersion> savedVersion =
                    Stack::NodeMath::ParseSemanticVersion(savedDefinitionVersion);
                node.compound.instance.definition.id = savedDefinitionId;
                node.compound.instance.definition.version = savedVersion.value_or(
                    Stack::NodeMath::SemanticVersion{});
                node.compound.instance.definition.contentHash = savedDefinitionHash;
                node.compound.instance.resolution = Stack::NodeMath::CompoundResolutionStatus::InvalidDefinition;
                node.compound.instance.resolutionError = compoundError.empty()
                    ? "Saved compound instance is invalid." : compoundError;
            }
            node.compound.instance.instanceUuid = node.instanceUuid;
            if (node.title.empty()) node.title = "Compound";
        } else if (kind == "FrequencyFilter") {
            node.kind = NodeKind::FrequencyFilter;
            node.frequencyFilterSettings = DeserializeFrequencyFilterSettings(
                item.value("frequencyFilterSettings", nlohmann::json::object()));
            if (node.title.empty()) EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
        } else if (kind == "FrequencyResponse") {
            node.kind = NodeKind::FrequencyResponse;
            node.frequencyResponseSettings = DeserializeFrequencyResponseSettings(
                item.value("frequencyResponseSettings", nlohmann::json::object()));
            if (node.title.empty()) EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
        } else if (kind == "FrequencyFft") {
            node.kind = NodeKind::FrequencyFft;
            node.frequencyFftSettings =
                DeserializeFrequencyFftSettings(item.value("frequencyFftSettings", nlohmann::json::object()));
            if (node.title.empty()) {
                EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            }
        } else if (kind == "FrequencyIfft") {
            node.kind = NodeKind::FrequencyIfft;
            node.frequencyIfftSettings =
                DeserializeFrequencyFftSettings(item.value("frequencyIfftSettings", nlohmann::json::object()));
            if (node.title.empty()) {
                EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            }
        } else if (kind == "SpectrumView") {
            node.kind = NodeKind::SpectrumView;
            node.spectrumViewSettings =
                DeserializeSpectrumViewSettings(item.value("spectrumViewSettings", nlohmann::json::object()));
            if (node.title.empty()) {
                EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            }
        } else if (kind == "ApplyFrequencyResponse") {
            node.kind = NodeKind::ApplyFrequencyResponse;
            node.applyFrequencyResponseSettings = DeserializeApplyFrequencyResponseSettings(
                item.value("applyFrequencyResponseSettings", nlohmann::json::object()));
            if (node.title.empty()) EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
        } else if (kind == "CombineSpectra") {
            node.kind = NodeKind::CombineSpectra;
            node.combineSpectraSettings = DeserializeCombineSpectraSettings(
                item.value("combineSpectraSettings", nlohmann::json::object()));
            if (node.title.empty()) EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
        } else if (kind == "SpectrumSeparate") {
            node.kind = NodeKind::SpectrumSeparate;
            if (node.title.empty()) EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
        } else if (kind == "SpectrumRecombine") {
            node.kind = NodeKind::SpectrumRecombine;
            if (node.title.empty()) EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
        } else if (kind == "FrequencyMask") {
            node.kind = NodeKind::FrequencyMask;
            node.frequencyMaskSettings =
                DeserializeFrequencyMaskSettings(item.value("frequencyMaskSettings", nlohmann::json::object()));
            node.frequencyMaskShape = FrequencyMaskShapeFromString(
                item.value("frequencyMaskShape", FrequencyMaskShapeToString(node.frequencyMaskSettings.shape)));
            node.frequencyMaskSettings.shape = node.frequencyMaskShape;
            if (node.title.empty()) {
                EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            }
        } else if (kind == "SpectrumMath") {
            node.kind = NodeKind::SpectrumMath;
            node.spectrumMathMode =
                SpectrumMathModeFromString(item.value("spectrumMathMode", std::string("Multiply")));
            node.spectrumMathSettings =
                DeserializeSpectrumMathSettings(item.value("spectrumMathSettings", nlohmann::json::object()));
            if (node.title.empty()) {
                EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            }
        } else if (kind == "MagnitudePhase") {
            node.kind = NodeKind::MagnitudePhase;
            node.magnitudePhaseMode =
                MagnitudePhaseModeFromString(item.value("magnitudePhaseMode", std::string("Magnitude")));
            node.magnitudePhaseSettings =
                DeserializeMagnitudePhaseSettings(item.value("magnitudePhaseSettings", nlohmann::json::object()));
            if (node.title.empty()) {
                EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            }
        } else if (kind == "SpectrumAnalyzer") {
            node.kind = NodeKind::SpectrumAnalyzer;
            node.spectrumAnalyzerMode =
                SpectrumAnalyzerModeFromString(item.value("spectrumAnalyzerMode", std::string("RadialEnergy")));
            node.spectrumAnalyzerSettings =
                DeserializeSpectrumAnalyzerSettings(item.value("spectrumAnalyzerSettings", nlohmann::json::object()));
            if (node.title.empty()) {
                EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            }
        } else if (kind == "Preview") {
            node.kind = NodeKind::Preview;
            if (node.title.empty()) {
                node.title = "Preview";
            }
        } else if (kind == "MaskUtility") {
            node.kind = NodeKind::MaskUtility;
            node.maskUtilityKind = MaskUtilityKindFromString(item.value("maskUtilityKind", std::string("Invert")));
            node.maskUtilitySettings = DeserializeMaskUtilitySettings(item.value("maskUtilitySettings", nlohmann::json::object()));
            if (node.title.empty() ||
                node.title == "Invert Mask" ||
                node.title == "Levels Mask" ||
                node.title == "Threshold Mask" ||
                node.title == "Invert Scalar" ||
                node.title == "Remap Scalar" ||
                node.title == "Threshold Scalar" ||
                node.title == "Remap Mask") {
                EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            }
        } else if (kind == "ImageToMask") {
            node.kind = NodeKind::ImageToMask;
            node.imageToMaskKind = ImageToMaskKindFromString(item.value("imageToMaskKind", std::string("Luminance")));
            node.imageToMaskSettings = DeserializeImageToMaskSettings(item.value("imageToMaskSettings", nlohmann::json::object()));
            if (node.title.empty() ||
                node.title == "Luminance Mask" ||
                node.title == "Sampled Range Mask" ||
                node.title == "Image To Scalar" ||
                node.title == "Sampled Range Scalar" ||
                node.title == "Image To Mask") {
                EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            }
        } else if (kind == "ImageGenerator") {
            node.kind = NodeKind::ImageGenerator;
            node.imageGeneratorKind = ImageGeneratorKindFromString(item.value("imageGeneratorKind", std::string("SolidColor")));
            node.imageGeneratorSettings = DeserializeImageGeneratorSettings(item.value("imageGeneratorSettings", nlohmann::json::object()));
            if (node.title.empty()) {
                switch (node.imageGeneratorKind) {
                    case ImageGeneratorKind::SolidColor: node.title = "Solid Color Image"; break;
                    case ImageGeneratorKind::ColorGradient: node.title = "Color Gradient Image"; break;
                    case ImageGeneratorKind::Square: node.title = "Square"; break;
                    case ImageGeneratorKind::Circle: node.title = "Circle"; break;
                    case ImageGeneratorKind::Text: node.title = "Text"; break;
                }
            }
        } else if (kind == "ChannelSplit") {
            node.kind = NodeKind::ChannelSplit;
            if (node.title.empty()) {
                node.title = "Channel Split";
            }
        } else if (kind == "ChannelCombine") {
            node.kind = NodeKind::ChannelCombine;
            node.imageCombineSettings =
                DeserializeImageCombineSettings(
                    item.value(
                        "imageCombineSettings",
                        nlohmann::json::object()));
            if (node.title.empty() ||
                node.title == "Channel Combine") {
                node.title = "Image Combine";
            }
        } else if (kind == "ConstantChannel") {
            node.kind = NodeKind::ConstantChannel;
            node.constantChannelSettings =
                DeserializeConstantChannelSettings(
                    item.value(
                        "constantChannelSettings",
                        nlohmann::json::object()));
            if (node.title.empty()) {
                node.title = "Constant Channel";
            }
        } else {
            node.kind = NodeKind::Layer;
            const LayerDescriptor* descriptor = LayerRegistry::FindDescriptorByTypeId(node.typeId);
            if (descriptor) {
                node.layerType = descriptor->type;
                if (node.title.empty()) node.title = descriptor->displayName;
            } else if (node.title.empty()) {
                node.title = "Layer";
            }
        }

        node.exposedParameterIds =
            item.value("exposedParameterIds", std::vector<std::string>{});

        if (node.kind == NodeKind::Compound) {
            node.definitionId = node.compound.instance.definition.id;
            node.definitionVersion = Stack::NodeMath::ToString(node.compound.instance.definition.version);
            node.definitionHash = node.compound.instance.definition.contentHash;
            if (compoundInstanceValid) {
                Stack::NodeMath::ResolveCompoundInstance(
                    node.compound.instance, graph.GetCompoundDefinitions());
            }
            node.definitionResolved = node.compound.instance.resolution ==
                Stack::NodeMath::CompoundResolutionStatus::Exact;
            node.definitionResolutionError = node.compound.instance.resolutionError;
            if (const Stack::NodeMath::CompoundDefinition* definition =
                    graph.FindCompoundDefinition(node.compound.instance.definition)) {
                node.title = definition->label;
            }
        } else if (graphVersion >= 5 &&
                   graphVersion < 8 &&
                   node.kind == NodeKind::Output &&
                   legacyOutputComponentCounts[node.id] <= 1) {
            // Output v2 is an intentional schema migration. A single legacy
            // component link has an unambiguous Channel meaning and is moved
            // to Result. Multi-component constructions are preserved below
            // with their old exact identity and remain unresolved.
            EditorNodeGraphDefinitions::ApplyLiveDefinitionIdentity(node);
        } else if (
            graphVersion >= 5 &&
            node.kind == NodeKind::ChannelCombine &&
            savedDefinitionId == "stack:graph/channel-combine" &&
            savedDefinitionVersion == "1.0.0") {
            // Image Combine v2 adds only the persisted automatic-alpha
            // suppression state. A v1 node has the exact v2 default
            // (not suppressed), so this upgrade is unambiguous.
            EditorNodeGraphDefinitions::ApplyLiveDefinitionIdentity(node);
        } else if (
            graphVersion >= 5 &&
            (node.kind == NodeKind::RawProjectFrame ||
             node.kind == NodeKind::MultiFrameDenoise ||
             node.kind == NodeKind::RawProjectSourceSet) &&
            savedDefinitionId.empty() &&
            savedDefinitionVersion.empty() &&
            savedDefinitionHash.empty()) {
            // Source-set graph nodes were introduced before their protected
            // internal definitions were registered. Their kind and bindings
            // are owned by the project manifest, so the previously empty
            // identity has one unambiguous migration. A partial or mismatched
            // identity still remains unresolved below.
            EditorNodeGraphDefinitions::ApplyLiveDefinitionIdentity(node);
        } else if (graphVersion >= 5) {
            EditorNodeGraphDefinitions::ResolveSavedLiveDefinition(
                node,
                savedDefinitionId,
                savedDefinitionVersion,
                savedDefinitionHash,
                nullptr);
        } else {
            // Forward rewrite identity begins with schema 5. Older documents are
            // loaded through the existing reader but acquire the current exact
            // identity only in memory; this is not a compatibility guarantee.
            EditorNodeGraphDefinitions::ApplyLiveDefinitionIdentity(node);
        }
        if (graphVersion < 7 &&
            (node.kind == NodeKind::FrequencyFft ||
             node.kind == NodeKind::FrequencyIfft ||
             node.kind == NodeKind::SpectrumView ||
             node.kind == NodeKind::FrequencyMask ||
             node.kind == NodeKind::SpectrumMath ||
             node.kind == NodeKind::MagnitudePhase ||
             node.kind == NodeKind::SpectrumAnalyzer)) {
            node.definitionResolved = false;
            node.definitionResolutionError =
                "Legacy frequency nodes are intentionally not reinterpreted. "
                "Replace this node with a Channel-First Frequency node.";
        }

        maxNodeId = std::max(maxNodeId, node.id);
        graph.EditNodes().push_back(std::move(node));
    }

    for (const Node& node : graph.GetNodes()) {
        if (node.kind == NodeKind::Compound &&
            node.compound.instance.resolution !=
                Stack::NodeMath::CompoundResolutionStatus::InvalidDefinition) {
            graph.ResolveCompoundNode(node.id);
        }
    }

    const nlohmann::json outputNodeIdsJson = graphJson.value("outputNodeIds", nlohmann::json::array());
    if (outputNodeIdsJson.is_array()) {
        for (const nlohmann::json& outputIdJson : outputNodeIdsJson) {
            const int outputId = outputIdJson.is_number_integer() ? outputIdJson.get<int>() : -1;
            const Node* outputNode = graph.FindNode(outputId);
            if (outputNode && outputNode->kind == NodeKind::Output) {
                graph.SetOutputNodeId(outputId);
                break;
            }
        }
    }
    if (graph.GetOutputNodeId() <= 0) {
        const int legacyOutputNodeId = graphJson.value("outputNodeId", -1);
        const Node* outputNode = graph.FindNode(legacyOutputNodeId);
        if (outputNode && outputNode->kind == NodeKind::Output) {
            graph.SetOutputNodeId(legacyOutputNodeId);
        }
    }

    graph.SetNextNodeId(std::max(maxNodeId + 1, graphJson.value("nextNodeId", maxNodeId + 1)));
    graph.SelectNode(graphJson.value("selectedNodeId", -1));
    graph.SetActiveImageNodeId(graphJson.value("activeImageNodeId", -1));

    if (linksJson.is_array()) {
        for (const nlohmann::json& item : linksJson) {
            if (!item.is_object()) continue;
            const int from = item.value("fromNodeId", item.value("from", 0));
            const int to = item.value("toNodeId", item.value("to", 0));
            const std::string toSocket = item.value("toSocket", std::string());
            if (from <= 0 || to <= 0 || !IsDataMathInputSocketId(toSocket)) {
                continue;
            }
            const Node* fromNode = graph.FindNode(from);
            Node* toNode = graph.FindNode(to);
            if (fromNode &&
                toNode &&
                toNode->kind == NodeKind::DataMath &&
                toNode->dataMathMode == DataMathMode::Average &&
                NodeUsuallyProducesFullImageForAverageMigration(
                    *fromNode,
                    item.value("fromSocket", graph.DefaultOutputSocket(*fromNode)))) {
                graph.SetDataMathMode(
                    toNode->id,
                    DataMathMode::ImageAverage);
            }
        }
    }
    for (const nlohmann::json& item : linksJson) {
        if (!item.is_object()) continue;
        const int from = item.value("fromNodeId", item.value("from", 0));
        const int to = item.value("toNodeId", item.value("to", 0));
        if (from <= 0 || to <= 0) {
            continue;
        }

        const Node* fromNode = graph.FindNode(from);
        const Node* toNode = graph.FindNode(to);
        if (!fromNode || !toNode) {
            continue;
        }

        const std::string fromSocket = item.value("fromSocket", graph.DefaultOutputSocket(*fromNode));
        std::string toSocket = item.value("toSocket", graph.DefaultInputSocket(*toNode));
        const bool legacyOutputComponent =
            toNode->kind == NodeKind::Output &&
            IsLegacyOutputComponentSocket(toSocket);
        if (legacyOutputComponent && !toNode->definitionResolved) {
            // Preserve authored multi-component legacy state losslessly. It
            // intentionally does not become an executable render link until
            // the user replaces it with an explicit Image Combine.
            graph.EditLinks().push_back(
                Link{ from, fromSocket, to, toSocket });
            continue;
        }
        if (legacyOutputComponent &&
            graphVersion < 8 &&
            legacyOutputComponentCounts[to] == 1) {
            toSocket = kImageInputSocketId;
        }
        if (!fromSocket.empty() && !toSocket.empty() && !graph.HasLink(from, fromSocket, to, toSocket)) {
            if (graph.TryConnectSockets(from, fromSocket, to, toSocket)) {
                for (Link& link : graph.EditLinks()) {
                    if (link.fromNodeId == from && link.fromSocketId == fromSocket &&
                        link.toNodeId == to && link.toSocketId == toSocket) {
                        link.ownership = item.value("ownership", std::string()) ==
                                "managed-source-binding"
                            ? Link::Ownership::ManagedSourceBinding
                            : Link::Ownership::User;
                        link.bindingId = item.value("bindingId", std::string());
                        break;
                    }
                }
            }
        }
    }

    if (graph.GetLinks().empty() && graph.GetActiveImageNodeId() > 0) {
        graph.RebuildLinks();
    }

    const nlohmann::json groupsJson = graphJson.value("groups", nlohmann::json::array());
    int maxGroupId = 0;
    if (groupsJson.is_array()) {
        for (const nlohmann::json& item : groupsJson) {
            if (!item.is_object()) continue;
            NodeGroup group;
            group.id = item.value("id", 0);
            group.title = item.value("title", "New Group");
            group.position.x = item.value("x", 0.0f);
            group.position.y = item.value("y", 0.0f);
            group.size.x = item.value("width", 200.0f);
            group.size.y = item.value("height", 150.0f);
            maxGroupId = std::max(maxGroupId, group.id);
            graph.GetGroups().push_back(std::move(group));
        }
    }
    graph.SetNextGroupId(std::max(maxGroupId + 1, graphJson.value("nextGroupId", maxGroupId + 1)));

    if (!graph.AllowsNoOutput()) graph.EnsureOutputNode();
    graph.SyncLayerNodes(layerCount);
}

} // namespace EditorNodeGraph
