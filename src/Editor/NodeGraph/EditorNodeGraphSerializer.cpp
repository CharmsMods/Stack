#include "EditorNodeGraphSerializer.h"

#include "EditorNodeGraphDefinitions.h"
#include "Serialization/EditorNodeGraphCustomMaskSerialization.h"
#include "Serialization/EditorNodeGraphDevelopSerialization.h"
#include "Serialization/EditorNodeGraphImageSerialization.h"
#include "Serialization/EditorNodeGraphLutSerialization.h"
#include "Serialization/EditorNodeGraphRawSerialization.h"
#include "Serialization/EditorNodeGraphUtilitySerialization.h"
#include <algorithm>

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
        case NodeKind::CustomMask: return "CustomMask";
        case NodeKind::DataMath: return "DataMath";
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
            return true;
        case NodeKind::DataMath:
            return node.dataMathMode != DataMathMode::Average;
        default:
            return false;
    }
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
    graphJson["version"] = 3;
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
        item["outputEnabled"] = node.outputEnabled;

        if (node.kind == NodeKind::Image) {
            item["label"] = node.image.label;
            item["sourcePath"] = node.image.sourcePath;
            item["width"] = node.image.width;
            item["height"] = node.image.height;
            item["channels"] = node.image.channels;
            item["originalChannels"] = node.image.originalChannels;
            item["pngBytes"] = nlohmann::json::binary(node.image.pngBytes);
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
            { "toSocket", link.toSocketId }
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

    root["nodeGraph"] = std::move(graphJson);
    return root;
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
    const nlohmann::json nodesJson = graphJson.value("nodes", nlohmann::json::array());

    int maxNodeId = 0;
    for (const nlohmann::json& item : nodesJson) {
        if (!item.is_object()) continue;

        Node node;
        node.id = item.value("id", 0);
        node.layerIndex = item.value("layerIndex", -1);
        node.typeId = item.value("typeId", std::string());
        node.title = item.value("title", std::string());
        node.position.x = item.value("x", 0.0f);
        node.position.y = item.value("y", 0.0f);
        node.expanded = item.value("expanded", false);
        node.outputEnabled = item.value("outputEnabled", true);

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
            const std::string uiMode = item.value("uiMode", std::string("Auto"));
            node.rawDevelop.uiMode =
                (uiMode == "Manual" || uiMode == "Advanced")
                    ? EditorNodeGraph::RawDevelopUiMode::Manual
                    : EditorNodeGraph::RawDevelopUiMode::Auto;
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
        } else if (kind == "Lut" || kind == "LUT") {
            node.kind = NodeKind::Lut;
            node.lut = DeserializeLutPayload(item.value("lut", nlohmann::json::object()));
            if (node.title.empty()) node.title = "LUT";
        } else if (kind == "Output") {
            node.kind = NodeKind::Output;
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
            if (node.title.empty()) {
                node.title = "Channel Combine";
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

        maxNodeId = std::max(maxNodeId, node.id);
        graph.GetNodes().push_back(std::move(node));
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

    const nlohmann::json linksJson = graphJson.value("links", nlohmann::json::array());
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
                toNode->dataMathMode = DataMathMode::ImageAverage;
                EditorNodeGraphDefinitions::ApplyNodeMetadata(*toNode);
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
        const std::string toSocket = item.value("toSocket", graph.DefaultInputSocket(*toNode));
        if (!fromSocket.empty() && !toSocket.empty() && !graph.HasLink(from, fromSocket, to, toSocket)) {
            graph.TryConnectSockets(from, fromSocket, to, toSocket);
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

    graph.EnsureOutputNode();
    graph.SyncLayerNodes(layerCount);
}

} // namespace EditorNodeGraph
