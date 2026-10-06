#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraphSelectionExport.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Editor/Timeline/TimelinePersistence.h"
#include "ThirdParty/stb_image.h"
#include "Utils/PixelBufferUtils.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

void PostNodeGraphClipboardNotification(
    EditorModule* editor,
    UiNotificationSeverity severity,
    const std::string& message,
    const char* dedupeKey) {
    if (!editor || message.empty()) {
        return;
    }
    editor->ShowUiNotification(severity, message, dedupeKey ? dedupeKey : "");
}

EditorNodeGraph::Vec2 ToGraphVec2(const ImVec2& value) {
    return EditorNodeGraph::Vec2{ value.x, value.y };
}

nlohmann::json SerializeMaskSettings(const EditorNodeGraph::MaskGeneratorSettings& settings) {
    return {
        { "value", settings.value },
        { "angle", settings.angle },
        { "offset", settings.offset },
        { "scale", settings.scale },
        { "centerX", settings.centerX },
        { "centerY", settings.centerY },
        { "radius", settings.radius },
        { "radiusY", settings.radiusY },
        { "feather", settings.feather },
        { "invert", settings.invert }
    };
}

EditorNodeGraph::MaskGeneratorSettings DeserializeMaskSettings(const nlohmann::json& value) {
    EditorNodeGraph::MaskGeneratorSettings settings;
    if (!value.is_object()) return settings;
    settings.value = value.value("value", settings.value);
    settings.angle = value.value("angle", settings.angle);
    settings.offset = value.value("offset", settings.offset);
    settings.scale = value.value("scale", settings.scale);
    settings.centerX = value.value("centerX", settings.centerX);
    settings.centerY = value.value("centerY", settings.centerY);
    settings.radius = value.value("radius", settings.radius);
    settings.radiusY = value.value("radiusY", settings.radius);
    settings.feather = value.value("feather", settings.feather);
    settings.invert = value.value("invert", settings.invert);
    return settings;
}

nlohmann::json SerializeMaskUtilitySettings(const EditorNodeGraph::MaskUtilitySettings& settings) {
    return {
        { "blackPoint", settings.blackPoint },
        { "whitePoint", settings.whitePoint },
        { "gamma", settings.gamma },
        { "threshold", settings.threshold },
        { "softness", settings.softness },
        { "enabled", settings.enabled },
        { "invert", settings.invert }
    };
}

EditorNodeGraph::MaskUtilitySettings DeserializeMaskUtilitySettings(const nlohmann::json& value) {
    EditorNodeGraph::MaskUtilitySettings settings;
    if (!value.is_object()) return settings;
    settings.blackPoint = value.value("blackPoint", settings.blackPoint);
    settings.whitePoint = value.value("whitePoint", settings.whitePoint);
    settings.gamma = value.value("gamma", settings.gamma);
    settings.threshold = value.value("threshold", settings.threshold);
    settings.softness = value.value("softness", settings.softness);
    settings.enabled = value.value("enabled", settings.enabled);
    settings.invert = value.value("invert", settings.invert);
    return settings;
}

nlohmann::json SerializeImageToMaskSettings(const EditorNodeGraph::ImageToMaskSettings& settings) {
    return {
        { "low", settings.low },
        { "high", settings.high },
        { "softness", settings.softness },
        { "invert", settings.invert }
    };
}

EditorNodeGraph::ImageToMaskSettings DeserializeImageToMaskSettings(const nlohmann::json& value) {
    EditorNodeGraph::ImageToMaskSettings settings;
    if (!value.is_object()) return settings;
    settings.low = value.value("low", settings.low);
    settings.high = value.value("high", settings.high);
    settings.softness = value.value("softness", settings.softness);
    settings.invert = value.value("invert", settings.invert);
    return settings;
}

nlohmann::json SerializeImageGeneratorSettings(const EditorNodeGraph::ImageGeneratorSettings& settings) {
    return {
        { "colorA", { settings.colorA[0], settings.colorA[1], settings.colorA[2], settings.colorA[3] } },
        { "colorB", { settings.colorB[0], settings.colorB[1], settings.colorB[2], settings.colorB[3] } },
        { "angle", settings.angle },
        { "offset", settings.offset },
        { "text", settings.text },
        { "fontSize", settings.fontSize },
        { "textBackdropBlur", settings.textBackdropBlur },
        { "textBackdropOpacity", settings.textBackdropOpacity },
        { "textBackdropPadding", settings.textBackdropPadding }
    };
}

EditorNodeGraph::ImageGeneratorSettings DeserializeImageGeneratorSettings(const nlohmann::json& value) {
    EditorNodeGraph::ImageGeneratorSettings settings;
    if (!value.is_object()) return settings;
    const nlohmann::json colorA = value.value("colorA", nlohmann::json::array());
    const nlohmann::json colorB = value.value("colorB", nlohmann::json::array());
    for (int i = 0; i < 4; ++i) {
        if (colorA.is_array() && static_cast<int>(colorA.size()) > i) settings.colorA[i] = colorA[i].get<float>();
        if (colorB.is_array() && static_cast<int>(colorB.size()) > i) settings.colorB[i] = colorB[i].get<float>();
    }
    settings.angle = value.value("angle", settings.angle);
    settings.offset = value.value("offset", settings.offset);
    settings.text = value.value("text", settings.text);
    settings.fontSize = value.value("fontSize", settings.fontSize);
    settings.textBackdropBlur = std::clamp(value.value("textBackdropBlur", settings.textBackdropBlur), 0.0f, 128.0f);
    settings.textBackdropOpacity = std::clamp(value.value("textBackdropOpacity", settings.textBackdropOpacity), 0.0f, 1.0f);
    settings.textBackdropPadding = std::clamp(value.value("textBackdropPadding", settings.textBackdropPadding), 0.0f, 256.0f);
    return settings;
}

nlohmann::json SerializeRawDetailFusionSettings(const Raw::RawDetailFusionSettings& settings) {
    return {
        { "mode", static_cast<int>(settings.mode) },
        { "debugView", static_cast<int>(settings.debugView) },
        { "autoSafetyEnabled", settings.autoSafetyEnabled },
        { "overrideMinEv", settings.overrideMinEv },
        { "overrideMaxEv", settings.overrideMaxEv },
        { "overrideBaseEv", settings.overrideBaseEv },
        { "overrideNoiseProtection", settings.overrideNoiseProtection },
        { "overrideHighlightProtection", settings.overrideHighlightProtection },
        { "overrideShadowLiftLimit", settings.overrideShadowLiftLimit },
        { "overrideWellExposedTarget", settings.overrideWellExposedTarget },
        { "minEvBias", settings.minEvBias },
        { "maxEvBias", settings.maxEvBias },
        { "baseEvBias", settings.baseEvBias },
        { "noiseProtectionBias", settings.noiseProtectionBias },
        { "highlightProtectionBias", settings.highlightProtectionBias },
        { "shadowLiftLimitBias", settings.shadowLiftLimitBias },
        { "wellExposedTargetBias", settings.wellExposedTargetBias },
        { "minEv", settings.minEv },
        { "maxEv", settings.maxEv },
        { "baseEv", settings.baseEv },
        { "strength", settings.strength },
        { "sampleCount", settings.sampleCount },
        { "baseRadiusPercent", settings.baseRadiusPercent },
        { "highlightProtection", settings.highlightProtection },
        { "shadowLiftLimit", settings.shadowLiftLimit },
        { "noiseProtection", settings.noiseProtection },
        { "detailWeight", settings.detailWeight },
        { "wellExposedTarget", settings.wellExposedTarget },
        { "smoothGradientProtection", settings.smoothGradientProtection },
        { "textureSensitivity", settings.textureSensitivity },
        { "skyBias", settings.skyBias },
        { "invertMask", settings.invertMask },
        { "maskBlackPoint", settings.maskBlackPoint },
        { "maskWhitePoint", settings.maskWhitePoint },
        { "maskGamma", settings.maskGamma },
        { "smoothnessRadius", settings.smoothnessRadius },
        { "smoothAreaRadius", settings.smoothAreaRadius },
        { "edgeAwareness", settings.edgeAwareness },
        { "haloGuard", settings.haloGuard },
        { "maskDebandDither", settings.maskDebandDither },
        { "manualBlend", settings.manualBlend }
    };
}

Raw::RawDetailFusionSettings DeserializeRawDetailFusionSettings(const nlohmann::json& value) {
    Raw::RawDetailFusionSettings settings;
    if (!value.is_object()) return settings;
    settings.mode = static_cast<Raw::RawDetailFusionMode>(std::clamp(value.value("mode", static_cast<int>(settings.mode)), 0, 2));
    settings.debugView = static_cast<Raw::RawDetailFusionDebugView>(std::clamp(value.value("debugView", static_cast<int>(settings.debugView)), 0, 14));
    settings.autoSafetyEnabled = value.value("autoSafetyEnabled", settings.autoSafetyEnabled);
    settings.overrideMinEv = value.value("overrideMinEv", settings.overrideMinEv);
    settings.overrideMaxEv = value.value("overrideMaxEv", settings.overrideMaxEv);
    settings.overrideBaseEv = value.value("overrideBaseEv", settings.overrideBaseEv);
    settings.overrideNoiseProtection = value.value("overrideNoiseProtection", settings.overrideNoiseProtection);
    settings.overrideHighlightProtection = value.value("overrideHighlightProtection", settings.overrideHighlightProtection);
    settings.overrideShadowLiftLimit = value.value("overrideShadowLiftLimit", settings.overrideShadowLiftLimit);
    settings.overrideWellExposedTarget = value.value("overrideWellExposedTarget", settings.overrideWellExposedTarget);
    settings.minEvBias = value.value("minEvBias", settings.minEvBias);
    settings.maxEvBias = value.value("maxEvBias", settings.maxEvBias);
    settings.baseEvBias = value.value("baseEvBias", settings.baseEvBias);
    settings.noiseProtectionBias = value.value("noiseProtectionBias", settings.noiseProtectionBias);
    settings.highlightProtectionBias = value.value("highlightProtectionBias", settings.highlightProtectionBias);
    settings.shadowLiftLimitBias = value.value("shadowLiftLimitBias", settings.shadowLiftLimitBias);
    settings.wellExposedTargetBias = value.value("wellExposedTargetBias", settings.wellExposedTargetBias);
    settings.minEv = value.value("minEv", settings.minEv);
    settings.maxEv = value.value("maxEv", settings.maxEv);
    settings.baseEv = value.value("baseEv", settings.baseEv);
    settings.strength = value.value("strength", settings.strength);
    settings.sampleCount = value.value("sampleCount", settings.sampleCount);
    settings.baseRadiusPercent = value.value("baseRadiusPercent", settings.baseRadiusPercent);
    settings.highlightProtection = value.value("highlightProtection", settings.highlightProtection);
    settings.shadowLiftLimit = value.value("shadowLiftLimit", settings.shadowLiftLimit);
    settings.noiseProtection = value.value("noiseProtection", settings.noiseProtection);
    settings.detailWeight = value.value("detailWeight", settings.detailWeight);
    settings.wellExposedTarget = value.value("wellExposedTarget", settings.wellExposedTarget);
    settings.smoothGradientProtection = value.value("smoothGradientProtection", settings.smoothGradientProtection);
    settings.textureSensitivity = value.value("textureSensitivity", settings.textureSensitivity);
    settings.skyBias = value.value("skyBias", settings.skyBias);
    settings.invertMask = value.value("invertMask", settings.invertMask);
    settings.maskBlackPoint = value.value("maskBlackPoint", settings.maskBlackPoint);
    settings.maskWhitePoint = value.value("maskWhitePoint", settings.maskWhitePoint);
    settings.maskGamma = value.value("maskGamma", settings.maskGamma);
    settings.smoothnessRadius = value.value("smoothnessRadius", settings.smoothnessRadius);
    settings.smoothAreaRadius = value.value("smoothAreaRadius", settings.smoothAreaRadius);
    settings.edgeAwareness = value.value("edgeAwareness", settings.edgeAwareness);
    settings.haloGuard = value.value("haloGuard", settings.haloGuard);
    settings.maskDebandDither = value.value("maskDebandDither", settings.maskDebandDither);
    settings.manualBlend = value.value("manualBlend", settings.manualBlend);
    settings.minEv = std::clamp(settings.minEv, -2.5f, 0.5f);
    settings.maxEv = std::clamp(settings.maxEv, std::max(settings.minEv + 0.01f, 0.25f), 2.5f);
    settings.baseEv = std::clamp(settings.baseEv, -1.0f, 1.0f);
    settings.minEvBias = std::clamp(settings.minEvBias, -2.0f, 2.0f);
    settings.maxEvBias = std::clamp(settings.maxEvBias, -2.0f, 2.0f);
    settings.baseEvBias = std::clamp(settings.baseEvBias, -1.25f, 1.25f);
    settings.noiseProtectionBias = std::clamp(settings.noiseProtectionBias, -1.0f, 1.0f);
    settings.highlightProtectionBias = std::clamp(settings.highlightProtectionBias, -1.0f, 1.0f);
    settings.shadowLiftLimitBias = std::clamp(settings.shadowLiftLimitBias, -1.0f, 1.0f);
    settings.wellExposedTargetBias = std::clamp(settings.wellExposedTargetBias, -1.0f, 1.0f);
    settings.strength = std::clamp(settings.strength, 0.0f, 1.25f);
    settings.sampleCount = std::clamp(settings.sampleCount, 3, 33);
    settings.baseRadiusPercent = std::clamp(settings.baseRadiusPercent, 0.002f, 0.030f);
    settings.highlightProtection = std::clamp(settings.highlightProtection, 0.0f, 1.0f);
    settings.shadowLiftLimit = std::clamp(settings.shadowLiftLimit, 0.0f, 1.0f);
    settings.noiseProtection = std::clamp(settings.noiseProtection, 0.0f, 1.0f);
    settings.detailWeight = std::clamp(settings.detailWeight, 0.0f, 1.0f);
    settings.wellExposedTarget = std::clamp(settings.wellExposedTarget, 0.10f, 0.55f);
    settings.smoothGradientProtection = std::clamp(settings.smoothGradientProtection, 0.0f, 1.0f);
    settings.textureSensitivity = std::clamp(settings.textureSensitivity, 0.0f, 1.0f);
    settings.skyBias = std::clamp(settings.skyBias, 0.0f, 1.0f);
    settings.maskBlackPoint = std::clamp(settings.maskBlackPoint, 0.0f, 1.0f);
    settings.maskWhitePoint = std::clamp(settings.maskWhitePoint, settings.maskBlackPoint + 0.001f, 1.0f);
    settings.maskGamma = std::clamp(settings.maskGamma, 0.05f, 8.0f);
    settings.smoothnessRadius = std::clamp(settings.smoothnessRadius, 0, 16);
    settings.smoothAreaRadius = std::clamp(settings.smoothAreaRadius, 0, 32);
    settings.edgeAwareness = std::clamp(settings.edgeAwareness, 0.0f, 1.0f);
    settings.haloGuard = std::clamp(settings.haloGuard, 0.0f, 1.0f);
    settings.maskDebandDither = std::clamp(settings.maskDebandDither, 0.0f, 1.0f);
    settings.manualBlend = std::clamp(settings.manualBlend, 0.0f, 1.0f);
    return settings;
}

bool DecodePngBytesClipboard(const std::vector<unsigned char>& pngBytes, EditorNodeGraph::ImagePayload& payload) {
    if (pngBytes.empty()) {
        return false;
    }
    stbi_set_flip_vertically_on_load_thread(1);
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(pngBytes.data(), static_cast<int>(pngBytes.size()), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }
    payload.pngBytes = pngBytes;
    const bool copied = Stack::PixelBuffer::CopyInterleavedPixels(
        pixels, width, height, 4, payload.pixels);
    stbi_image_free(pixels);
    if (!copied) {
        return false;
    }
    payload.width = width;
    payload.height = height;
    payload.channels = 4;
    payload.originalChannels = channels;
    return true;
}

std::string BuildGraphText(const nlohmann::json& clipboardPayload) {
    std::ostringstream stream;
    stream << "STACK_NODE_GRAPH 1\n";
    stream << "scope: " << clipboardPayload.value("scope", std::string("selection")) << "\n";
    stream << "mode: " << clipboardPayload.value("mode", std::string("tree+state")) << "\n";
    stream << "layout: " << clipboardPayload.value("layout", std::string("preserved")) << "\n\n";
    stream << clipboardPayload.dump(2);
    return stream.str();
}

bool ParseGraphText(const std::string& text, nlohmann::json& outPayload, std::string& outError) {
    if (text.rfind("STACK_NODE_GRAPH 1", 0) != 0) {
        outError = "Clipboard does not contain Stack graph text.";
        return false;
    }

    const std::size_t jsonStart = text.find('{');
    if (jsonStart == std::string::npos) {
        outError = "Graph text is missing its payload block.";
        return false;
    }

    outPayload = nlohmann::json::parse(text.substr(jsonStart), nullptr, false);
    if (outPayload.is_discarded() || !outPayload.is_object()) {
        outError = "Graph text payload could not be parsed.";
        return false;
    }

    if (!outPayload.contains("payload") || !outPayload["payload"].is_object()) {
        outError = "Graph text payload is missing nodeGraph data.";
        return false;
    }

    return true;
}

enum class SystemClipboardGraphPayloadState {
    Missing,
    Invalid,
    Valid
};

SystemClipboardGraphPayloadState TryReadSystemClipboardGraphPayload(
    nlohmann::json& outPayload,
    std::string* outError = nullptr) {
    const char* clipboardText = ImGui::GetClipboardText();
    if (!clipboardText || clipboardText[0] == '\0') {
        if (outError) {
            *outError = "Clipboard is empty.";
        }
        return SystemClipboardGraphPayloadState::Missing;
    }

    const std::string text(clipboardText);
    if (text.rfind("STACK_NODE_GRAPH 1", 0) != 0) {
        return SystemClipboardGraphPayloadState::Missing;
    }

    std::string parseError;
    if (!ParseGraphText(text, outPayload, parseError)) {
        if (outError) {
            *outError = std::move(parseError);
        }
        return SystemClipboardGraphPayloadState::Invalid;
    }

    return SystemClipboardGraphPayloadState::Valid;
}

bool BuildImportedLayers(
    const nlohmann::json& layerArray,
    std::vector<std::shared_ptr<LayerBase>>& outLayers,
    std::vector<std::string>& warnings) {
    outLayers.clear();
    if (!layerArray.is_array()) {
        return true;
    }

    for (const auto& layerData : layerArray) {
        const std::string type = layerData.value("type", "");
        std::shared_ptr<LayerBase> newLayer = LayerRegistry::CreateLayerFromTypeId(type);
        if (!newLayer) {
            warnings.push_back(type.empty() ? "Skipped a layer with no type." : ("Skipped unsupported layer type: " + type));
            outLayers.push_back(nullptr);
            continue;
        }
        newLayer->InitializeGL();
        newLayer->Deserialize(layerData);
        outLayers.push_back(newLayer);
    }
    return true;
}

} // namespace

bool EditorNodeGraphUI::PasteClipboardPayload(EditorModule* editor, const nlohmann::json& clipboardPayload, std::string* outSummary) {
    if (!editor || !clipboardPayload.is_object()) {
        return false;
    }

    try {
    const auto context = editor->GetGraphEditorContext();
    const nlohmann::json serializerPayload = clipboardPayload.value("payload", nlohmann::json::object());
    if (!serializerPayload.is_object() || !serializerPayload.contains("nodeGraph")) {
        if (outSummary) *outSummary = "Graph payload is missing node data.";
        return false;
    }
    const nlohmann::json graphJson = serializerPayload.value("nodeGraph", nlohmann::json::object());
    const nlohmann::json serializedNodesJson = graphJson.value("nodes", nlohmann::json::array());
    std::unordered_set<int> serializedNodeIds;
    if (serializedNodesJson.is_array()) {
        for (const nlohmann::json& item : serializedNodesJson) {
            if (!item.is_object()) {
                continue;
            }
            const int nodeId = item.value("id", 0);
            if (nodeId > 0) {
                serializedNodeIds.insert(nodeId);
            }
        }
    }

    const nlohmann::json layerArray = EditorNodeGraph::ExtractLayerArray(serializerPayload);
    std::vector<std::string> warnings;
    if (!EditorNodeGraph::IsCurrentGraphPayload(serializerPayload) || !layerArray.is_array()) {
        if (outSummary) *outSummary = "This graph uses an unsupported document format.";
        return false;
    }

    EditorNodeGraph::Graph tempGraph;
    EditorNodeGraph::DeserializeGraphPayload(serializerPayload, tempGraph, static_cast<int>(layerArray.size()), {}, 0, 0, 0);

    std::vector<int> syntheticOutputNodeIds;
    for (const EditorNodeGraph::Node& node : tempGraph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::Output &&
            serializedNodeIds.find(node.id) == serializedNodeIds.end()) {
            syntheticOutputNodeIds.push_back(node.id);
        }
    }
    for (int nodeId : syntheticOutputNodeIds) {
        tempGraph.RemoveNode(nodeId);
    }

    if (tempGraph.GetNodes().size() != serializedNodeIds.size() ||
        tempGraph.GetLinks().size() != graphJson.at("links").size()) {
        if (outSummary) *outSummary = "The graph cannot be pasted without dropping nodes or connections.";
        return false;
    }
    for (const auto& node : tempGraph.GetNodes()) if (!node.definitionResolved) {
        if (outSummary) *outSummary = node.definitionResolutionError;
        return false;
    }
    if (clipboardPayload.value("layout", std::string("preserved")) == "omitted") {
        tempGraph.AutoLayout();
    }

    const auto& nodes = tempGraph.GetNodes();
    if (nodes.empty()) {
        if (outSummary) *outSummary = "Graph payload did not contain any nodes.";
        return false;
    }

    const int pasteCount = m_ClipboardPasteCount + 1;
    const float offsetX = pasteCount * 40.0f;
    const float offsetY = pasteCount * 40.0f;
    const float cursorOffsetX = std::max(0, pasteCount - 1) * 40.0f;
    const float cursorOffsetY = std::max(0, pasteCount - 1) * 40.0f;

    const ImVec2 mousePos = ImGui::GetMousePos();
    const bool mouseInsideCanvas =
        mousePos.x >= m_CanvasMin.x && mousePos.x <= m_CanvasMax.x &&
        mousePos.y >= m_CanvasMin.y && mousePos.y <= m_CanvasMax.y;
    const bool useCursorPos = mouseInsideCanvas || m_HasLastGraphMousePos;
    EditorNodeGraph::Vec2 cursorGraphPos = m_LastGraphMousePos;
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    for (const EditorNodeGraph::Node& node : nodes) {
        minX = std::min(minX, node.position.x);
        minY = std::min(minY, node.position.y);
    }
    if (useCursorPos) {
        if (mouseInsideCanvas) {
            cursorGraphPos = ScreenToGraph(ToGraphVec2(mousePos));
            m_LastGraphMousePos = cursorGraphPos;
            m_HasLastGraphMousePos = true;
        }
    }

    auto targetGraph = *context.graph;
    auto targetSettings = nlohmann::json::array();
    for (const auto& layer : editor->GetLayers()) targetSettings.push_back(layer->Serialize());
    auto animation = *context.animation;
    for (const auto& definition : tempGraph.GetCompoundDefinitions()) {
        std::string error;
        if (!targetGraph.AddCompoundDefinition(definition, &error) && !error.empty()) {
            if (outSummary) *outSummary = error;
            return false;
        }
    }
    std::unordered_map<int, int> layerIndexMap;
    for (int i = 0; i < static_cast<int>(layerArray.size()); ++i) {
        layerIndexMap[i] = static_cast<int>(targetSettings.size());
        targetSettings.push_back(layerArray[i]);
    }
    const auto sourceGraphId = serializerPayload.value("graphId", std::string("project"));
    std::unordered_map<std::string, std::string> uuidMap;
    std::unordered_map<int, int> oldNodeIdToNew;
    std::vector<int> pastedNodeIds;
    int importedNodeCount = 0;
    for (const EditorNodeGraph::Node& sourceNode : nodes) {
        if (sourceNode.role == Stack::GraphModel::NodeRole::OriginalImage || sourceNode.role == Stack::GraphModel::NodeRole::CurrentImage) {
            const auto found = std::find_if(targetGraph.GetNodes().begin(), targetGraph.GetNodes().end(),
                [&](const auto& node) { return node.role == sourceNode.role; });
            if (found == targetGraph.GetNodes().end()) {
                if (outSummary) *outSummary = "This graph has no matching protected layer source. Paste its creative branches into a RAW layer.";
                return false;
            }
            oldNodeIdToNew[sourceNode.id] = found->id;
            uuidMap[sourceNode.instanceUuid] = found->instanceUuid;
            continue;
        }
        EditorNodeGraph::Node nodeCopy = sourceNode;
        if (nodeCopy.role == Stack::GraphModel::NodeRole::LayerResult) nodeCopy.role = Stack::GraphModel::NodeRole::Ordinary;
        nodeCopy.id = targetGraph.GetNextNodeId();
        targetGraph.SetNextNodeId(nodeCopy.id + 1);
        nodeCopy.instanceUuid = Stack::NodeMath::GenerateCanonicalUuid();
        uuidMap[sourceNode.instanceUuid] = nodeCopy.instanceUuid;
        if (nodeCopy.kind == EditorNodeGraph::NodeKind::Compound) {
            nodeCopy.compound.instance.instanceUuid = nodeCopy.instanceUuid;
        }

        if (useCursorPos && minX != std::numeric_limits<float>::max()) {
            nodeCopy.position.x = cursorGraphPos.x + (sourceNode.position.x - minX) + cursorOffsetX;
            nodeCopy.position.y = cursorGraphPos.y + (sourceNode.position.y - minY) + cursorOffsetY;
        } else {
            nodeCopy.position.x = sourceNode.position.x + offsetX;
            nodeCopy.position.y = sourceNode.position.y + offsetY;
        }

        if (nodeCopy.kind == EditorNodeGraph::NodeKind::Layer) {
            const auto layerIt = layerIndexMap.find(sourceNode.layerIndex);
            if (layerIt == layerIndexMap.end()) {
                if (outSummary) *outSummary = "An effect node is missing its saved settings.";
                return false;
            }
            nodeCopy.layerIndex = layerIt->second;
        }

        targetGraph.EditNodes().push_back(nodeCopy);
        if (nodeCopy.kind == EditorNodeGraph::NodeKind::Compound) {
            targetGraph.ResolveCompoundNode(nodeCopy.id);
        }
        oldNodeIdToNew[sourceNode.id] = nodeCopy.id;
        pastedNodeIds.push_back(nodeCopy.id);
        importedNodeCount++;
    }

    for (int id : pastedNodeIds) {
        auto* node = targetGraph.FindNode(id);
        if (node->role == Stack::GraphModel::NodeRole::Reference && node->reference.graphId == sourceGraphId) {
            const auto mapped = uuidMap.find(node->reference.nodeUuid);
            if (mapped != uuidMap.end()) {
                node->reference.graphId = context.graphId; node->reference.nodeUuid = mapped->second;
            }
        }
    }
    if (serializerPayload.contains("graphAnimation")) {
        const auto imported = Stack::Timeline::DeserializeTimelineDocument(serializerPayload.at("graphAnimation"));
        for (auto track : imported.animation.tracks) {
            const auto mapped = uuidMap.find(track.target.nodeUuid);
            const auto local = oldNodeIdToNew.find(track.target.nodeId);
            if (mapped == uuidMap.end() || local == oldNodeIdToNew.end()) {
                if (outSummary) *outSummary = "An animation target is outside the copied graph.";
                return false;
            }
            track.target.graphId = context.graphId; track.target.nodeUuid = mapped->second; track.target.nodeId = local->second;
            animation.tracks.push_back(std::move(track));
        }
    }
    int importedLinkCount = 0;
    int skippedLinkCount = 0;
    for (const EditorNodeGraph::Link& link : tempGraph.GetLinks()) {
        const auto fromIt = oldNodeIdToNew.find(link.fromNodeId);
        const auto toIt = oldNodeIdToNew.find(link.toNodeId);
        if (fromIt == oldNodeIdToNew.end() || toIt == oldNodeIdToNew.end()) {
            if (outSummary) *outSummary = "A copied connection has no producer or destination.";
            return false;
        }
        std::string errorMessage;
        if (targetGraph.TryConnectSockets(fromIt->second, link.fromSocketId, toIt->second, link.toSocketId, &errorMessage)) {
            importedLinkCount++;
        } else {
            if (outSummary) *outSummary = errorMessage;
            return false;
        }
    }

    int importedGroupCount = 0;
    for (const EditorNodeGraph::NodeGroup& group : tempGraph.GetGroups()) {
        EditorNodeGraph::Vec2 position = group.position;
        if (useCursorPos && minX != std::numeric_limits<float>::max()) {
            position.x = cursorGraphPos.x + (group.position.x - minX) + cursorOffsetX;
            position.y = cursorGraphPos.y + (group.position.y - minY) + cursorOffsetY;
        } else {
            position.x += offsetX;
            position.y += offsetY;
        }
        if (targetGraph.AddGroup(group.title, position, group.size)) {
            importedGroupCount++;
        }
    }

    targetGraph.ClearSelection();
    for (int nodeId : pastedNodeIds) {
        targetGraph.SelectNode(nodeId, true);
    }
    auto proposal = Stack::GraphModel::ProposeEdit(*context.graph, context.revision,
        [&](auto& candidate) { candidate = std::move(targetGraph); });
    std::string error;
    if (!context.applyDocumentEdit(std::move(proposal), std::move(targetSettings), std::move(animation), error)) {
        if (outSummary) *outSummary = error;
        return false;
    }
    m_ClipboardPasteCount = pasteCount;
    editor->RefreshGraphLayerMetadata();

    std::ostringstream summary;
    summary << "Imported " << importedNodeCount << " nodes, " << importedLinkCount << " links, and " << importedGroupCount << " groups";
    if (skippedLinkCount > 0) {
        summary << "; skipped " << skippedLinkCount << " invalid links";
    }
    if (!warnings.empty()) {
        summary << ".";
    }
    if (outSummary) {
        *outSummary = summary.str();
    }
    return true;
    } catch (const std::exception& error) {
        if (outSummary) *outSummary = std::string("Cannot paste graph: ") + error.what();
        return false;
    }
}

bool EditorNodeGraphUI::ApplyPresetPayload(EditorModule* editor, const nlohmann::json& graphPayload, std::string* outSummary) {
    nlohmann::json clipboardPayload = nlohmann::json::object();
    clipboardPayload["format"] = "stack-node-graph";
    clipboardPayload["version"] = 1;
    clipboardPayload["scope"] = "preset";
    clipboardPayload["mode"] = "tree+state";
    clipboardPayload["payload"] = graphPayload;
    return PasteClipboardPayload(editor, clipboardPayload, outSummary);
}

void EditorNodeGraphUI::CopySelectedNodes(EditorModule* editor, bool writeSystemClipboard) {
    if (!editor) {
        return;
    }

    m_ClipboardPasteCount = 0;
    const auto& selectedNodeIds = editor->GetNodeGraph().GetSelectedNodeIds();
    if (selectedNodeIds.empty()) {
        m_Clipboard = nlohmann::json::object();
        return;
    }

    m_Clipboard = EditorNodeGraphSelectionExport::BuildExport(editor, selectedNodeIds, true, false).clipboardPayload;
    if (writeSystemClipboard) {
        const std::string text = BuildGraphText(m_Clipboard);
        ImGui::SetClipboardText(text.c_str());
    }
}

void EditorNodeGraphUI::PasteNodes(EditorModule* editor, bool preferSystemClipboard) {
    if (!editor) {
        return;
    }

    if (preferSystemClipboard) {
        nlohmann::json payload;
        std::string clipboardError;
        switch (TryReadSystemClipboardGraphPayload(payload, &clipboardError)) {
            case SystemClipboardGraphPayloadState::Valid: {
                std::string summary;
                if (PasteClipboardPayload(editor, payload, &summary)) {
                    m_Clipboard = payload;
                    PostNodeGraphClipboardNotification(editor, UiNotificationSeverity::Success, summary, "editor-node-graph-clipboard");
                } else if (!summary.empty()) {
                    PostNodeGraphClipboardNotification(editor, UiNotificationSeverity::Error, summary, "editor-node-graph-clipboard");
                }
                return;
            }
            case SystemClipboardGraphPayloadState::Invalid:
                if (!clipboardError.empty()) {
                    PostNodeGraphClipboardNotification(editor, UiNotificationSeverity::Error, clipboardError, "editor-node-graph-clipboard");
                }
                return;
            case SystemClipboardGraphPayloadState::Missing:
                break;
        }
    }

    if (m_Clipboard.empty()) {
        return;
    }

    std::string summary;
    if (PasteClipboardPayload(editor, m_Clipboard, &summary)) {
        PostNodeGraphClipboardNotification(editor, UiNotificationSeverity::Success, summary, "editor-node-graph-clipboard");
    } else if (!summary.empty()) {
        PostNodeGraphClipboardNotification(editor, UiNotificationSeverity::Error, summary, "editor-node-graph-clipboard");
    }
}

void EditorNodeGraphUI::CopyGraphInfo(
    EditorModule* editor,
    bool wholeGraph,
    bool includeState,
    bool includeLayout) {
    if (!editor) {
        return;
    }

    std::vector<int> nodeIds;
    if (wholeGraph) {
        for (const EditorNodeGraph::Node& node : editor->GetNodeGraph().GetNodes()) {
            nodeIds.push_back(node.id);
        }
    } else {
        nodeIds = editor->GetNodeGraph().GetSelectedNodeIds();
    }

    if (nodeIds.empty()) {
        PostNodeGraphClipboardNotification(
            editor,
            UiNotificationSeverity::Warning,
            wholeGraph ? "Graph is empty." : "Select at least one node to copy graph info.",
            "editor-node-graph-clipboard");
        return;
    }

    const EditorNodeGraphSelectionExport::LayoutMode layoutMode =
        includeLayout
            ? EditorNodeGraphSelectionExport::LayoutMode::Preserve
            : EditorNodeGraphSelectionExport::LayoutMode::Omit;
    nlohmann::json payload =
        EditorNodeGraphSelectionExport::BuildExport(
            editor, nodeIds, includeState, wholeGraph, layoutMode).clipboardPayload;
    const std::string text = BuildGraphText(payload);
    ImGui::SetClipboardText(text.c_str());
    m_Clipboard = payload;
    m_ClipboardPasteCount = 0;
    PostNodeGraphClipboardNotification(
        editor,
        UiNotificationSeverity::Success,
        !includeLayout
            ? "Whole graph copied in node/link order without layout."
            : (wholeGraph
                ? (includeState ? "Whole graph copied with state." : "Whole graph copied as tree only.")
                : (includeState ? "Selected graph copied with state." : "Selected graph copied as tree only.")),
        "editor-node-graph-clipboard");
}

void EditorNodeGraphUI::PasteGraphInfo(EditorModule* editor) {
    if (!editor) {
        return;
    }

    nlohmann::json payload;
    std::string error;
    const SystemClipboardGraphPayloadState clipboardState = TryReadSystemClipboardGraphPayload(payload, &error);
    if (clipboardState == SystemClipboardGraphPayloadState::Missing) {
        PostNodeGraphClipboardNotification(editor, UiNotificationSeverity::Warning, "Clipboard is empty.", "editor-node-graph-clipboard");
        return;
    }
    if (clipboardState == SystemClipboardGraphPayloadState::Invalid) {
        PostNodeGraphClipboardNotification(editor, UiNotificationSeverity::Error, error, "editor-node-graph-clipboard");
        return;
    }

    std::string summary;
    if (PasteClipboardPayload(editor, payload, &summary)) {
        m_Clipboard = payload;
        PostNodeGraphClipboardNotification(editor, UiNotificationSeverity::Success, summary, "editor-node-graph-clipboard");
    } else {
        PostNodeGraphClipboardNotification(
            editor,
            UiNotificationSeverity::Error,
            summary.empty() ? "Graph text could not be imported." : summary,
            "editor-node-graph-clipboard");
    }
}

void EditorNodeGraphUI::DuplicateSelectedNodes(EditorModule* editor) {
    if (!editor) return;
    CopySelectedNodes(editor);
    PasteNodes(editor);
}
