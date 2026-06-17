#include "Editor/EditorModule.h"

#include "Async/TaskSystem.h"
#include "Editor/Layers/ToneLayers.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include "Library/LibraryManager.h"
#include "Raw/LibRawRuntime.h"
#include "Raw/RawLoader.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include "Utils/FileDialogs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <unordered_set>

namespace {

struct DecodedImageData {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int channels = 4;
    int originalChannels = 4;
};

bool DecodeImageFromFile(const std::string& path, DecodedImageData& outImage) {
    outImage = {};

    stbi_set_flip_vertically_on_load_thread(1);
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        return false;
    }

    outImage.width = width;
    outImage.height = height;
    outImage.channels = 4;
    outImage.originalChannels = channels;
    outImage.pixels.assign(pixels, pixels + (width * height * 4));
    stbi_image_free(pixels);
    return true;
}

void PngWriteCallback(void* context, void* data, int size) {
    auto* bytes = static_cast<std::vector<unsigned char>*>(context);
    const auto* begin = static_cast<unsigned char*>(data);
    bytes->insert(bytes->end(), begin, begin + size);
}

std::vector<unsigned char> EncodePngBytes(const std::vector<unsigned char>& pixels, int width, int height, int channels) {
    std::vector<unsigned char> pngBytes;
    if (pixels.empty() || width <= 0 || height <= 0) {
        return pngBytes;
    }

    const int safeChannels = std::max(1, channels);
    stbi_write_png_to_func(PngWriteCallback, &pngBytes, width, height, safeChannels, pixels.data(), width * safeChannels);
    return pngBytes;
}

std::string FileNameFromPath(const std::string& path) {
    try {
        return std::filesystem::path(path).filename().string();
    } catch (...) {
        return path.empty() ? std::string("Image") : path;
    }
}

nlohmann::json BuildDefaultIntegratedToneLayerJson() {
    ToneCurveLayer toneCurve;
    return toneCurve.Serialize();
}

std::vector<unsigned char> EncodePngBytesForImageStorage(
    const std::vector<unsigned char>& bottomLeftPixels,
    int width,
    int height,
    int channels) {
    if (bottomLeftPixels.empty() || width <= 0 || height <= 0 || channels <= 0) {
        return {};
    }

    std::vector<unsigned char> topLeftPixels = bottomLeftPixels;
    LibraryManager::FlipImageRowsInPlace(topLeftPixels, width, height, std::max(1, channels));
    return EncodePngBytes(topLeftPixels, width, height, channels);
}

int NormalizeQuarterTurnsClockwise(int quarterTurnsClockwise) {
    int normalized = quarterTurnsClockwise % 4;
    if (normalized < 0) {
        normalized += 4;
    }
    return normalized;
}

std::vector<unsigned char> RotateBottomLeftImagePixels(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    int channels,
    int quarterTurnsClockwise,
    int& outWidth,
    int& outHeight) {
    outWidth = width;
    outHeight = height;
    const int safeChannels = std::max(1, channels);
    const int normalizedTurns = NormalizeQuarterTurnsClockwise(quarterTurnsClockwise);
    if (pixels.empty() || width <= 0 || height <= 0 || normalizedTurns == 0) {
        return pixels;
    }

    if (normalizedTurns == 2) {
        std::vector<unsigned char> rotated(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(safeChannels));
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int srcX = width - 1 - x;
                const int srcY = height - 1 - y;
                const std::size_t dstIndex = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * static_cast<std::size_t>(safeChannels);
                const std::size_t srcIndex = (static_cast<std::size_t>(srcY) * static_cast<std::size_t>(width) + static_cast<std::size_t>(srcX)) * static_cast<std::size_t>(safeChannels);
                std::copy_n(pixels.data() + srcIndex, safeChannels, rotated.data() + dstIndex);
            }
        }
        return rotated;
    }

    outWidth = height;
    outHeight = width;
    std::vector<unsigned char> rotated(static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight) * static_cast<std::size_t>(safeChannels));
    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            int srcX = 0;
            int srcY = 0;
            if (normalizedTurns == 1) {
                srcX = width - 1 - y;
                srcY = x;
            } else {
                srcX = y;
                srcY = height - 1 - x;
            }
            const std::size_t dstIndex = (static_cast<std::size_t>(y) * static_cast<std::size_t>(outWidth) + static_cast<std::size_t>(x)) * static_cast<std::size_t>(safeChannels);
            const std::size_t srcIndex = (static_cast<std::size_t>(srcY) * static_cast<std::size_t>(width) + static_cast<std::size_t>(srcX)) * static_cast<std::size_t>(safeChannels);
            std::copy_n(pixels.data() + srcIndex, safeChannels, rotated.data() + dstIndex);
        }
    }

    return rotated;
}

EditorNodeGraph::ImagePayload BuildImagePayloadFromDecoded(
    const std::string& path,
    DecodedImageData decoded) {
    EditorNodeGraph::ImagePayload payload;
    payload.label = FileNameFromPath(path);
    payload.sourcePath = path;
    payload.width = decoded.width;
    payload.height = decoded.height;
    payload.channels = decoded.channels;
    payload.originalChannels = decoded.originalChannels;
    payload.pngBytes = EncodePngBytesForImageStorage(decoded.pixels, decoded.width, decoded.height, decoded.channels);
    payload.pixels = std::move(decoded.pixels);
    return payload;
}

EditorNodeGraph::RawSourcePayload BuildRawPayloadFromMetadata(
    const std::string& path,
    Raw::RawMetadata metadata) {
    EditorNodeGraph::RawSourcePayload payload;
    payload.label = FileNameFromPath(path).empty() ? "RAW" : FileNameFromPath(path);
    payload.sourcePath = path;
    payload.metadata = std::move(metadata);
    payload.metadata.sourcePath = path;
    return payload;
}

Raw::RawDevelopSettings BuildRawDevelopSettingsFromMetadata(const Raw::RawMetadata& metadata) {
    Raw::RawDevelopSettings settings;
    if (metadata.hasDngBaselineExposure) {
        settings.exposureStops = metadata.dngBaselineExposure;
    }
    settings.blackLevelOverride = metadata.blackLevel;
    settings.whiteLevelOverride = metadata.whiteLevel;
    const bool dngHasColorTags = metadata.hasDngForwardMatrix1 || metadata.hasDngForwardMatrix2 ||
        metadata.hasDngColorMatrix1 || metadata.hasDngColorMatrix2;
    const bool dngHasCalibrationTags = metadata.hasDngCameraCalibration1 || metadata.hasDngCameraCalibration2;
    const bool dngHasDualForwardMatrices = metadata.hasDngForwardMatrix1 && metadata.hasDngForwardMatrix2;
    const bool preferLibRawForUnderSpecifiedDng =
        metadata.isDng &&
        metadata.hasCameraMatrix &&
        dngHasDualForwardMatrices &&
        !dngHasCalibrationTags;
    if (!metadata.isDng || preferLibRawForUnderSpecifiedDng) {
        settings.cameraTransformSource = Raw::RawCameraTransformSource::LibRawRgbCam;
    } else if (dngHasColorTags) {
        settings.cameraTransformSource = Raw::RawCameraTransformSource::DngAuto;
    } else {
        settings.cameraTransformSource = Raw::RawCameraTransformSource::LibRawRgbCam;
    }
    return settings;
}

std::shared_ptr<LayerBase> CloneLayerInstance(const std::shared_ptr<LayerBase>& source) {
    if (!source) {
        return nullptr;
    }
    const nlohmann::json layerJson = source->Serialize();
    const std::string typeId = layerJson.value("type", std::string());
    std::shared_ptr<LayerBase> clone = LayerRegistry::CreateLayerFromTypeId(typeId);
    if (!clone) {
        return nullptr;
    }
    clone->InitializeGL();
    clone->Deserialize(layerJson);
    clone->SetVisible(source->IsVisible());
    return clone;
}

struct GraphReconnectPlan {
    int fromNodeId = 0;
    std::string fromSocketId;
    int toNodeId = 0;
    std::string toSocketId;
};

struct ScenePathState {
    bool sceneReferred = false;
    bool hasViewTransform = false;
};

const EditorNodeGraph::Node* FindUpstreamRawSourceNode(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& rawDomainNode) {
    const EditorNodeGraph::Link* rawInput = graph.FindInputLink(rawDomainNode.id, EditorNodeGraph::kRawInputSocketId);
    std::unordered_set<int> visited;
    while (rawInput) {
        if (!visited.insert(rawInput->fromNodeId).second) {
            return nullptr;
        }

        const EditorNodeGraph::Node* upstream = graph.FindNode(rawInput->fromNodeId);
        if (!upstream) {
            return nullptr;
        }
        if (upstream->kind == EditorNodeGraph::NodeKind::RawSource) {
            return upstream;
        }
        if (upstream->kind != EditorNodeGraph::NodeKind::RawNeuralDenoise) {
            return nullptr;
        }
        rawInput = graph.FindInputLink(upstream->id, EditorNodeGraph::kRawInputSocketId);
    }
    return nullptr;
}

ScenePathState MergeScenePathState(ScenePathState a, const ScenePathState& b) {
    a.sceneReferred = a.sceneReferred || b.sceneReferred;
    a.hasViewTransform = a.hasViewTransform || b.hasViewTransform;
    return a;
}

ScenePathState AnalyzeScenePathFromNode(
    const EditorNodeGraph::Graph& graph,
    const std::vector<std::shared_ptr<LayerBase>>& layers,
    int nodeId,
    std::unordered_set<int>& visiting) {
    if (!visiting.insert(nodeId).second) {
        return {};
    }

    ScenePathState state;
    const EditorNodeGraph::Node* node = graph.FindNode(nodeId);
    if (!node) {
        visiting.erase(nodeId);
        return state;
    }

    auto mergeInput = [&](const char* socketId) {
        if (const EditorNodeGraph::Link* input = graph.FindInputLink(nodeId, socketId)) {
            state = MergeScenePathState(state, AnalyzeScenePathFromNode(graph, layers, input->fromNodeId, visiting));
        }
    };

    switch (node->kind) {
        case EditorNodeGraph::NodeKind::RawDecode:
        case EditorNodeGraph::NodeKind::RawDevelop:
            state.sceneReferred = true;
            mergeInput(EditorNodeGraph::kRawInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::RawDetailFusion:
            state.sceneReferred = true;
            mergeInput(EditorNodeGraph::kImageInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::HdrMerge:
            state.sceneReferred = true;
            mergeInput(EditorNodeGraph::kHdrMergeInput1SocketId);
            mergeInput(EditorNodeGraph::kHdrMergeInput2SocketId);
            mergeInput(EditorNodeGraph::kHdrMergeInput3SocketId);
            break;
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
            state.sceneReferred = true;
            mergeInput(EditorNodeGraph::kImageInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
            mergeInput(EditorNodeGraph::kRawInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::Layer:
            if (node->layerType == LayerType::ToneCurve) {
                state.sceneReferred = true;
            } else if (node->layerType == LayerType::ViewTransform) {
                state.hasViewTransform = true;
            }
            mergeInput(EditorNodeGraph::kImageInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::Lut:
            if (graph.FindInputLink(node->id, EditorNodeGraph::kImageInputSocketId)) {
                mergeInput(EditorNodeGraph::kImageInputSocketId);
            } else {
                mergeInput("r");
                mergeInput("g");
                mergeInput("b");
                mergeInput("a");
            }
            break;
        case EditorNodeGraph::NodeKind::Mix:
        case EditorNodeGraph::NodeKind::DataMath:
            mergeInput(EditorNodeGraph::kMixInputASocketId);
            mergeInput(EditorNodeGraph::kMixInputBSocketId);
            break;
        case EditorNodeGraph::NodeKind::ChannelSplit:
            mergeInput(EditorNodeGraph::kImageInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::ChannelCombine:
            mergeInput("r");
            mergeInput("g");
            mergeInput("b");
            mergeInput("a");
            break;
        case EditorNodeGraph::NodeKind::Output:
            if (graph.FindInputLink(node->id, EditorNodeGraph::kImageInputSocketId)) {
                mergeInput(EditorNodeGraph::kImageInputSocketId);
            } else {
                mergeInput("r");
                mergeInput("g");
                mergeInput("b");
                mergeInput("a");
            }
            break;
        case EditorNodeGraph::NodeKind::ImageToMask:
            mergeInput(EditorNodeGraph::kImageToMaskInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::MaskCombine:
            mergeInput(EditorNodeGraph::kMaskCombineInputASocketId);
            mergeInput(EditorNodeGraph::kMaskCombineInputBSocketId);
            break;
        case EditorNodeGraph::NodeKind::MaskUtility:
            mergeInput(EditorNodeGraph::kMaskUtilityInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::ImageGenerator:
        case EditorNodeGraph::NodeKind::MaskGenerator:
        case EditorNodeGraph::NodeKind::CustomMask:
        case EditorNodeGraph::NodeKind::Composite:
        case EditorNodeGraph::NodeKind::Scope:
        case EditorNodeGraph::NodeKind::Preview:
            break;
    }

    visiting.erase(nodeId);
    (void)layers;
    return state;
}

std::optional<GraphReconnectPlan> BuildReconnectSourcePlan(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Layer: {
            const EditorNodeGraph::Link* input = graph.FindInputLink(node.id, EditorNodeGraph::kImageInputSocketId);
            if (!input) {
                return std::nullopt;
            }
            return GraphReconnectPlan{
                input->fromNodeId,
                input->fromSocketId,
                0,
                EditorNodeGraph::kImageOutputSocketId
            };
        }
        case EditorNodeGraph::NodeKind::MaskUtility: {
            const EditorNodeGraph::Link* input = graph.FindAnyInputLink(node.id, EditorNodeGraph::kMaskInputSocketId);
            if (!input) {
                return std::nullopt;
            }
            return GraphReconnectPlan{
                input->fromNodeId,
                input->fromSocketId,
                0,
                EditorNodeGraph::kMaskOutputSocketId
            };
        }
        case EditorNodeGraph::NodeKind::MaskCombine: {
            const EditorNodeGraph::Link* inputA = graph.FindAnyInputLink(node.id, EditorNodeGraph::kMaskCombineInputASocketId);
            const EditorNodeGraph::Link* inputB = graph.FindAnyInputLink(node.id, EditorNodeGraph::kMaskCombineInputBSocketId);
            const EditorNodeGraph::Link* selectedInput =
                inputA && !inputB ? inputA :
                inputB && !inputA ? inputB :
                nullptr;
            if (!selectedInput) {
                return std::nullopt;
            }
            return GraphReconnectPlan{
                selectedInput->fromNodeId,
                selectedInput->fromSocketId,
                0,
                EditorNodeGraph::kMaskOutputSocketId
            };
        }
        case EditorNodeGraph::NodeKind::RawNeuralDenoise: {
            const EditorNodeGraph::Link* input = graph.FindInputLink(node.id, EditorNodeGraph::kRawInputSocketId);
            if (!input) {
                return std::nullopt;
            }
            return GraphReconnectPlan{
                input->fromNodeId,
                input->fromSocketId,
                0,
                EditorNodeGraph::kRawOutputSocketId
            };
        }
        case EditorNodeGraph::NodeKind::Mix:
        case EditorNodeGraph::NodeKind::DataMath: {
            const EditorNodeGraph::Link* inputA = graph.FindInputLink(node.id, EditorNodeGraph::kMixInputASocketId);
            const EditorNodeGraph::Link* inputB = graph.FindInputLink(node.id, EditorNodeGraph::kMixInputBSocketId);
            const EditorNodeGraph::Link* selectedInput =
                inputA && !inputB ? inputA :
                inputB && !inputA ? inputB :
                nullptr;
            if (!selectedInput) {
                return std::nullopt;
            }
            return GraphReconnectPlan{
                selectedInput->fromNodeId,
                selectedInput->fromSocketId,
                0,
                EditorNodeGraph::kImageOutputSocketId
            };
        }
        default:
            break;
    }
    return std::nullopt;
}

std::vector<GraphReconnectPlan> BuildReconnectPlansForNodeRemoval(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) {
    const std::optional<GraphReconnectPlan> sourcePlan = BuildReconnectSourcePlan(graph, node);
    if (!sourcePlan.has_value()) {
        return {};
    }

    std::vector<GraphReconnectPlan> plans;
    for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
        if (link.fromNodeId != node.id || link.fromSocketId != sourcePlan->toSocketId) {
            continue;
        }
        if (sourcePlan->fromNodeId == link.toNodeId) {
            continue;
        }
        plans.push_back(GraphReconnectPlan{
            sourcePlan->fromNodeId,
            sourcePlan->fromSocketId,
            link.toNodeId,
            link.toSocketId
        });
    }
    return plans;
}

template <typename T>
std::size_t HashValue(const T& value) {
    return std::hash<T>{}(value);
}

void HashCombine(std::size_t& seed, std::size_t value) {
    seed ^= value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
}

void HashDevelopSubjectImportance(std::size_t& hash, const EditorNodeGraph::DevelopSubjectImportanceMap& importance) {
    HashCombine(hash, HashValue(importance.enabled));
    HashCombine(hash, HashValue(importance.regions.size()));
    for (const EditorNodeGraph::DevelopSubjectImportanceRegion& region : importance.regions) {
        HashCombine(hash, HashValue(region.id));
        HashCombine(hash, HashValue(static_cast<int>(region.mode)));
        HashCombine(hash, HashValue(region.enabled));
        HashCombine(hash, HashValue(region.centerX));
        HashCombine(hash, HashValue(region.centerY));
        HashCombine(hash, HashValue(region.radiusX));
        HashCombine(hash, HashValue(region.radiusY));
        HashCombine(hash, HashValue(region.feather));
        HashCombine(hash, HashValue(region.strength));
    }
    HashCombine(hash, HashValue(importance.strokes.size()));
    for (const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : importance.strokes) {
        HashCombine(hash, HashValue(stroke.id));
        HashCombine(hash, HashValue(static_cast<int>(stroke.mode)));
        HashCombine(hash, HashValue(stroke.enabled));
        HashCombine(hash, HashValue(stroke.subtract));
        HashCombine(hash, HashValue(stroke.radius));
        HashCombine(hash, HashValue(stroke.feather));
        HashCombine(hash, HashValue(stroke.strength));
        HashCombine(hash, HashValue(stroke.points.size()));
        for (const EditorNodeGraph::DevelopSubjectImportanceStrokePoint& point : stroke.points) {
            HashCombine(hash, HashValue(point.x));
            HashCombine(hash, HashValue(point.y));
        }
    }
}

std::size_t BuildDevelopAutoSolveTriggerHash(
    const EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawMetadata& metadata) {
    std::size_t hash = HashValue(metadata.sourcePath);
    HashCombine(hash, HashValue(metadata.hasDngBaselineExposure));
    HashCombine(hash, HashValue(metadata.dngBaselineExposure));
    HashCombine(hash, HashValue(metadata.blackLevel));
    HashCombine(hash, HashValue(metadata.whiteLevel));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[0]));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[1]));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[2]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[0]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[1]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[2]));
    HashCombine(hash, HashValue(static_cast<int>(payload.uiMode)));
    HashCombine(hash, HashValue(static_cast<int>(payload.autoGuidance.intent)));
    HashCombine(hash, HashValue(payload.autoGuidance.autoStrength));
    HashCombine(hash, HashValue(payload.autoGuidance.exposureBias));
    HashCombine(hash, HashValue(payload.autoGuidance.dynamicRange));
    HashCombine(hash, HashValue(payload.autoGuidance.shadowLift));
    HashCombine(hash, HashValue(payload.autoGuidance.highlightGuard));
    HashCombine(hash, HashValue(payload.autoGuidance.highlightCharacter));
    HashCombine(hash, HashValue(payload.autoGuidance.contrastBias));
    HashCombine(hash, HashValue(payload.autoGuidance.subjectSceneBias));
    HashCombine(hash, HashValue(payload.autoGuidance.moodReadabilityBias));
    HashDevelopSubjectImportance(hash, payload.subjectImportance);
    return hash;
}

std::size_t BuildDevelopAutoRawSolveTriggerHash(
    const EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawMetadata& metadata) {
    std::size_t hash = HashValue(metadata.sourcePath);
    HashCombine(hash, HashValue(metadata.hasDngBaselineExposure));
    HashCombine(hash, HashValue(metadata.dngBaselineExposure));
    HashCombine(hash, HashValue(metadata.blackLevel));
    HashCombine(hash, HashValue(metadata.whiteLevel));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[0]));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[1]));
    HashCombine(hash, HashValue(metadata.cameraWhiteBalance[2]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[0]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[1]));
    HashCombine(hash, HashValue(metadata.daylightWhiteBalance[2]));
    HashCombine(hash, HashValue(static_cast<int>(payload.uiMode)));
    HashCombine(hash, HashValue(static_cast<int>(payload.autoGuidance.intent)));
    HashCombine(hash, HashValue(payload.autoGuidance.autoStrength));
    HashCombine(hash, HashValue(payload.autoGuidance.exposureBias));
    HashCombine(hash, HashValue(payload.autoGuidance.dynamicRange));
    HashCombine(hash, HashValue(payload.autoGuidance.shadowLift));
    HashCombine(hash, HashValue(payload.autoGuidance.highlightGuard));
    HashCombine(hash, HashValue(payload.autoGuidance.highlightCharacter));
    HashCombine(hash, HashValue(payload.autoGuidance.contrastBias));
    HashCombine(hash, HashValue(payload.autoGuidance.subjectSceneBias));
    HashCombine(hash, HashValue(payload.autoGuidance.moodReadabilityBias));
    HashDevelopSubjectImportance(hash, payload.subjectImportance);
    return hash;
}

EditorNodeGraph::MaskCombineMode ToGraphMaskCombineMode(ToneCurveScopeMaskAction action) {
    switch (action) {
        case ToneCurveScopeMaskAction::Add: return EditorNodeGraph::MaskCombineMode::Add;
        case ToneCurveScopeMaskAction::Subtract: return EditorNodeGraph::MaskCombineMode::Subtract;
        case ToneCurveScopeMaskAction::Intersect:
        case ToneCurveScopeMaskAction::NewMask:
        default: return EditorNodeGraph::MaskCombineMode::Intersect;
    }
}

bool ConnectionUsesImageAsRenderSource(
    const EditorNodeGraph::Graph& graph,
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId) {
    const EditorNodeGraph::Node* from = graph.FindNode(fromNodeId);
    const EditorNodeGraph::Node* to = graph.FindNode(toNodeId);
    if (!from || !to || from->kind != EditorNodeGraph::NodeKind::Image ||
        fromSocketId != EditorNodeGraph::kImageOutputSocketId) {
        return false;
    }

    if (to->kind == EditorNodeGraph::NodeKind::Layer && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::RawDetailAutoMask && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::RawDetailFusion && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::HdrMerge &&
        toSocketId == EditorNodeGraph::kHdrMergeInput1SocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::Output && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::Mix &&
        (toSocketId == EditorNodeGraph::kMixInputASocketId ||
         toSocketId == EditorNodeGraph::kMixInputBSocketId)) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::DataMath &&
        (toSocketId == EditorNodeGraph::kMixInputASocketId ||
         toSocketId == EditorNodeGraph::kMixInputBSocketId)) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::ChannelSplit &&
        toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    return false;
}

} // namespace

void EditorModule::AddLayer(LayerType type) {
    std::shared_ptr<LayerBase> newLayer = LayerRegistry::CreateLayer(type);

    if (newLayer) {
        newLayer->InitializeGL();

        const char* defaultName = newLayer->GetDefaultName();
        int count = 0;
        for (const auto& existing : m_Layers) {
            if (strcmp(existing->GetDefaultName(), defaultName) == 0) {
                count++;
            }
        }

        if (count > 0) {
            char suffix[64];
            snprintf(suffix, sizeof(suffix), "%s (%d)", defaultName, count + 1);
            newLayer->SetInstanceName(suffix);
        }

        m_Layers.push_back(newLayer);
        SelectLayer(static_cast<int>(m_Layers.size()) - 1);
        m_FocusSelectedTabNextRender = true;
        MarkRenderDirty();
    }
}

void EditorModule::AddLayerNodeAt(LayerType type, EditorNodeGraph::Vec2 graphPosition) {
    const int layerIndex = static_cast<int>(m_Layers.size());
    AddLayer(type);
    if (static_cast<int>(m_Layers.size()) == layerIndex + 1) {
        m_NodeGraph.AddLayerNode(type, layerIndex, graphPosition);
        RefreshGraphLayerMetadata();
        if (EditorNodeGraph::Node* node = m_NodeGraph.FindNodeByLayerIndex(layerIndex)) {
            SelectGraphNode(node->id);
        }
        MarkRenderDirty();
    }
}

void EditorModule::RemoveLayer(int index) {
    if (index >= 0 && index < static_cast<int>(m_Layers.size())) {
        if (m_CanvasToolOwnerNodeId > 0) {
            const EditorNodeGraph::Node* ownerNode = m_NodeGraph.FindNode(m_CanvasToolOwnerNodeId);
            if (ownerNode && ownerNode->kind == EditorNodeGraph::NodeKind::Layer && ownerNode->layerIndex == index) {
                CancelCanvasTool();
            }
        }
        // TODO: Promote this to an undoable editor command when command history lands.
        m_Layers.erase(m_Layers.begin() + index);
        m_NodeGraph.RemoveLayerNode(index);
        RefreshGraphLayerMetadata();
        if (m_SelectedLayerIndex >= static_cast<int>(m_Layers.size())) {
            m_SelectedLayerIndex = static_cast<int>(m_Layers.size()) - 1;
        }
        MarkRenderDirty();
    }
}

void EditorModule::MoveLayer(int from, int to) {
    if (from == to) return;
    if (from < 0 || from >= static_cast<int>(m_Layers.size())) return;
    if (to < 0 || to >= static_cast<int>(m_Layers.size())) return;

    // TODO: Promote this to an undoable editor command when command history lands.
    if (from < to) {
        std::rotate(m_Layers.begin() + from, m_Layers.begin() + from + 1, m_Layers.begin() + to + 1);
    } else {
        std::rotate(m_Layers.begin() + to, m_Layers.begin() + from, m_Layers.begin() + from + 1);
    }

    if (m_SelectedLayerIndex == from) {
        m_SelectedLayerIndex = to;
    } else if (from < m_SelectedLayerIndex && to >= m_SelectedLayerIndex) {
        m_SelectedLayerIndex--;
    } else if (from > m_SelectedLayerIndex && to <= m_SelectedLayerIndex) {
        m_SelectedLayerIndex++;
    }

    RefreshGraphLayerMetadata();
    MarkRenderDirty();
}

void EditorModule::SetLayerVisible(int index, bool visible) {
    if (index < 0 || index >= static_cast<int>(m_Layers.size())) {
        return;
    }

    // TODO: Promote this to an undoable editor command when command history lands.
    m_Layers[index]->SetVisible(visible);
    MarkRenderDirty();
}

void EditorModule::SelectLayer(int index) {
    if (index < -1 || index >= static_cast<int>(m_Layers.size())) {
        return;
    }

    m_SelectedLayerIndex = index;
}

void EditorModule::SelectGraphNode(int nodeId) {
    m_NodeGraph.SelectNode(nodeId);
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (node && node->kind == EditorNodeGraph::NodeKind::Layer) {
        SelectLayer(node->layerIndex);
    } else {
        SelectLayer(-1);
    }
    if (node && node->kind == EditorNodeGraph::NodeKind::Output) {
        m_CompositeSelectedOutputNodeId = nodeId;
    }
}

bool EditorModule::LayerUsesRichNodeSurface(int layerIndex) const {
    // Rich expanded surface layers now render as clean, compact nodes in the graph
    // and open their settings in dedicated settings pages instead of expanding on-graph.
    return false;
}

bool EditorModule::NodeUsesRichNodeSurface(int nodeId) const {
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    return node &&
        ((node->kind == EditorNodeGraph::NodeKind::Layer && LayerUsesRichNodeSurface(node->layerIndex)) ||
         node->kind == EditorNodeGraph::NodeKind::RawSource ||
         node->kind == EditorNodeGraph::NodeKind::RawNeuralDenoise ||
         node->kind == EditorNodeGraph::NodeKind::RawDecode ||
         node->kind == EditorNodeGraph::NodeKind::RawDevelop ||
         node->kind == EditorNodeGraph::NodeKind::RawDetailAutoMask ||
         node->kind == EditorNodeGraph::NodeKind::RawDetailFusion ||
         node->kind == EditorNodeGraph::NodeKind::HdrMerge ||
         node->kind == EditorNodeGraph::NodeKind::Lut ||
         node->kind == EditorNodeGraph::NodeKind::CustomMask);
}

NodeSurfaceSpec EditorModule::GetLayerNodeSurfaceSpec(int layerIndex) const {
    if (layerIndex < 0 || layerIndex >= static_cast<int>(m_Layers.size()) || !m_Layers[layerIndex]) {
        return {};
    }
    return m_Layers[layerIndex]->GetNodeSurfaceSpec();
}

NodeSurfaceSpec EditorModule::GetNodeSurfaceSpec(int nodeId) const {
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node) {
        return {};
    }
    if (node->kind == EditorNodeGraph::NodeKind::RawSource) {
        NodeSurfaceSpec spec;
        spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
        spec.density = NodeSurfaceDensity::Dense;
        spec.preferredWidth = 420.0f;
        spec.maxWidth = 520.0f;
        return spec;
    }
    if (node->kind == EditorNodeGraph::NodeKind::RawNeuralDenoise ||
        node->kind == EditorNodeGraph::NodeKind::RawDecode ||
        node->kind == EditorNodeGraph::NodeKind::RawDevelop ||
        node->kind == EditorNodeGraph::NodeKind::RawDetailAutoMask ||
        node->kind == EditorNodeGraph::NodeKind::RawDetailFusion ||
        node->kind == EditorNodeGraph::NodeKind::HdrMerge ||
        node->kind == EditorNodeGraph::NodeKind::Lut) {
        NodeSurfaceSpec spec;
        spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
        spec.density = NodeSurfaceDensity::Dense;
        spec.preferredWidth = 420.0f;
        spec.maxWidth = 520.0f;
        return spec;
    }
    if (node->kind != EditorNodeGraph::NodeKind::Layer) {
        return {};
    }
    return GetLayerNodeSurfaceSpec(node->layerIndex);
}

void EditorModule::BeginCanvasColorPick(
    int ownerNodeId,
    const std::string& statusText,
    std::function<void(float, float, float)> callback) {
    m_CanvasToolKind = CanvasToolKind::PickColor;
    m_CanvasToolOwnerNodeId = ownerNodeId;
    m_CanvasToolStatusText = statusText.empty() ? "Click canvas to sample color" : statusText;
    m_IsPickingColor = true;
    m_ColorPickerCallback = std::move(callback);
}

void EditorModule::BeginToneCurveTargeting(int ownerNodeId, const std::string& statusText) {
    m_CanvasToolKind = CanvasToolKind::ToneCurveTarget;
    m_CanvasToolOwnerNodeId = ownerNodeId;
    m_CanvasToolStatusText = statusText.empty()
        ? "Click and drag in the main viewport to adjust the sampled tone"
        : statusText;
    m_IsPickingColor = false;
    m_ColorPickerCallback = nullptr;
}

void EditorModule::CancelCanvasTool() {
    if (m_CanvasToolKind == CanvasToolKind::ToneCurveTarget) {
        EndToneCurveViewportTargetDrag();
        ClearTrackedToneCurveProbe();
    }
    m_CanvasToolKind = CanvasToolKind::None;
    m_CanvasToolOwnerNodeId = -1;
    m_CanvasToolStatusText.clear();
    m_IsPickingColor = false;
    m_ColorPickerCallback = nullptr;
}

void EditorModule::RestoreIntegratedToneTransientState(int ownerNodeId, ToneCurveLayer& toneCurve) const {
    const auto it = m_IntegratedToneViewportInteractionCache.find(ownerNodeId);
    if (it == m_IntegratedToneViewportInteractionCache.end()) {
        toneCurve.RestoreViewportInteractionState(ToneCurveLayer::ViewportInteractionState{});
        return;
    }

    ToneCurveLayer::ViewportInteractionState state;
    state.probeValid = it->second.probeValid;
    state.probeSamplingBasis = static_cast<ToneCurveSamplingBasis>(std::clamp(it->second.probeSamplingBasis, 0, 1));
    state.probeU = it->second.probeU;
    state.probeV = it->second.probeV;
    state.probeRgba = it->second.probeRgba;
    state.selectionSeedValid = it->second.selectionSeedValid;
    state.selectionSeedU = it->second.selectionSeedU;
    state.selectionSeedV = it->second.selectionSeedV;
    state.selectionSeedInputX = it->second.selectionSeedInputX;
    state.selectionSeedSceneValue = it->second.selectionSeedSceneValue;
    state.selectionSeedRgba = it->second.selectionSeedRgba;
    state.onImageDragPointIndex = it->second.onImageDragPointIndex;
    state.onImageDragAnchorInputX = it->second.onImageDragAnchorInputX;
    state.onImageDragAnchorOutputY = it->second.onImageDragAnchorOutputY;
    toneCurve.RestoreViewportInteractionState(state);
}

void EditorModule::StoreIntegratedToneTransientState(int ownerNodeId, const ToneCurveLayer& toneCurve) const {
    ToneCurveViewportInteractionCache cache;
    const ToneCurveLayer::ViewportInteractionState state = toneCurve.CaptureViewportInteractionState();
    cache.probeValid = state.probeValid;
    cache.probeSamplingBasis = static_cast<int>(state.probeSamplingBasis);
    cache.probeU = state.probeU;
    cache.probeV = state.probeV;
    cache.probeRgba = state.probeRgba;
    cache.selectionSeedValid = state.selectionSeedValid;
    cache.selectionSeedU = state.selectionSeedU;
    cache.selectionSeedV = state.selectionSeedV;
    cache.selectionSeedInputX = state.selectionSeedInputX;
    cache.selectionSeedSceneValue = state.selectionSeedSceneValue;
    cache.selectionSeedRgba = state.selectionSeedRgba;
    cache.onImageDragPointIndex = state.onImageDragPointIndex;
    cache.onImageDragAnchorInputX = state.onImageDragAnchorInputX;
    cache.onImageDragAnchorOutputY = state.onImageDragAnchorOutputY;
    m_IntegratedToneViewportInteractionCache[ownerNodeId] = cache;
}

void EditorModule::ClearIntegratedToneTransientState(int ownerNodeId) const {
    m_IntegratedToneViewportInteractionCache.erase(ownerNodeId);
}

void EditorModule::OnCanvasColorPicked(float r, float g, float b) {
    if (m_ColorPickerCallback) {
        m_ColorPickerCallback(r, g, b);
    }
    CancelCanvasTool();
}

bool EditorModule::SelectAdjacentMainChainNode(int direction) {
    if (direction == 0) {
        return false;
    }

    const int selectedNodeId = m_NodeGraph.GetSelectedNodeId();
    if (selectedNodeId <= 0) {
        return false;
    }

    const int adjacentNodeId = m_NodeGraph.FindAdjacentMainChainNodeId(selectedNodeId, direction);
    if (adjacentNodeId <= 0 || adjacentNodeId == selectedNodeId) {
        return false;
    }

    SelectGraphNode(adjacentNodeId);
    return true;
}

void EditorModule::ApplyGraphLayerOrder() {
    const std::vector<int> order = m_NodeGraph.GetRenderLayerIndexPath();
    if (order.empty()) {
        return;
    }

    std::vector<int> uniqueOrder;
    for (int index : order) {
        if (index >= 0 && index < static_cast<int>(m_Layers.size()) &&
            std::find(uniqueOrder.begin(), uniqueOrder.end(), index) == uniqueOrder.end()) {
            uniqueOrder.push_back(index);
        }
    }
    if (uniqueOrder.size() != m_Layers.size()) {
        return;
    }

    std::vector<std::shared_ptr<LayerBase>> reordered;
    reordered.reserve(m_Layers.size());
    for (int index : uniqueOrder) {
        reordered.push_back(m_Layers[index]);
    }
    m_Layers = std::move(reordered);

    for (int i = 0; i < static_cast<int>(uniqueOrder.size()); ++i) {
        if (EditorNodeGraph::Node* node = m_NodeGraph.FindNodeByLayerIndex(uniqueOrder[i])) {
            node->layerIndex = i;
        }
    }
    RefreshGraphLayerMetadata();
}

void EditorModule::PromptAddImageNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    const std::string path = FileDialogs::OpenImageFileDialog("Add Image Node");
    if (!path.empty()) {
        AddImageNodeFromFile(path, graphPosition);
    }
}

bool EditorModule::AddImageNodeFromFile(const std::string& path, EditorNodeGraph::Vec2 graphPosition) {
    if (Raw::RawLoader::IsRawPath(path)) {
        const Raw::LibRawRuntimeStatus& runtimeStatus = Raw::GetLibRawRuntimeStatus();
        if (!runtimeStatus.runtimeAvailable) {
            QueueUiNotification(UiNotificationSeverity::Error, runtimeStatus.message, "editor-raw-runtime");
            return false;
        }
        return AddRawSourceNodeFromFile(path, graphPosition);
    }
    DecodedImageData decoded;
    if (!DecodeImageFromFile(path, decoded) || decoded.pixels.empty()) {
        return false;
    }

    return AddImageNodeFromPayload(BuildImagePayloadFromDecoded(path, std::move(decoded)), graphPosition);
}

bool EditorModule::AddRawSourceNodeFromFile(const std::string& path, EditorNodeGraph::Vec2 graphPosition) {
    if (!Raw::IsLibRawRuntimeAvailable()) {
        return false;
    }

    Raw::RawImageData rawData;
    const bool loaded = Raw::RawLoader::LoadFile(path, rawData);
    if (!loaded && rawData.metadata.sourcePath.empty()) {
        rawData.metadata.sourcePath = path;
    }
    if (!loaded && rawData.metadata.error.empty()) {
        rawData.metadata.error = "Failed to load RAW file.";
    }

    EditorNodeGraph::RawSourcePayload payload = BuildRawPayloadFromMetadata(path, std::move(rawData.metadata));
    return AddRawSourceNodeFromPayload(std::move(payload), graphPosition);
}

bool EditorModule::AddImageNodeFromPayload(EditorNodeGraph::ImagePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddImageNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
    }
    return node != nullptr;
}

bool EditorModule::AddRawSourceNodeFromPayload(EditorNodeGraph::RawSourcePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    if (payload.metadata.sourcePath.empty()) {
        payload.metadata.sourcePath = payload.sourcePath;
    }
    if (!Raw::IsLibRawRuntimeAvailable() &&
        !payload.metadata.sourcePath.empty() &&
        payload.metadata.error.empty()) {
        payload.metadata.error = Raw::GetLibRawRuntimeStatus().message;
    }

    EditorNodeGraph::Node* node = m_NodeGraph.AddRawSourceNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

void EditorModule::AddRawNeuralDenoiseNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::RawNeuralDenoisePayload payload;
    AddRawNeuralDenoiseNodeFromPayload(std::move(payload), graphPosition);
}

bool EditorModule::AddRawNeuralDenoiseNodeFromPayload(EditorNodeGraph::RawNeuralDenoisePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawNeuralDenoiseNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

void EditorModule::AddRawDevelopNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::RawDevelopPayload payload;
    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    payload.integratedToneLayerJson = BuildDefaultIntegratedToneLayerJson();
    payload.uiMode = EditorNodeGraph::RawDevelopUiMode::Auto;
    if (const EditorNodeGraph::Node* selected = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId())) {
        if (selected->kind == EditorNodeGraph::NodeKind::RawSource) {
            payload.settings = BuildRawDevelopSettingsFromMetadata(selected->rawSource.metadata);
            ApplyDevelopAutoSolve(payload, selected->rawSource.metadata, true);
        }
    }
    AddRawDevelopNodeFromPayload(std::move(payload), graphPosition);
}

void EditorModule::AddRawDecodeNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::RawDecodePayload payload;
    if (const EditorNodeGraph::Node* selected = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId())) {
        if (selected->kind == EditorNodeGraph::NodeKind::RawSource) {
            payload.settings = BuildRawDevelopSettingsFromMetadata(selected->rawSource.metadata);
        } else if (selected->kind == EditorNodeGraph::NodeKind::RawNeuralDenoise) {
            if (const EditorNodeGraph::Node* rawSourceNode = FindUpstreamRawSourceNode(m_NodeGraph, *selected)) {
                payload.settings = BuildRawDevelopSettingsFromMetadata(rawSourceNode->rawSource.metadata);
            }
        }
    }
    AddRawDecodeNodeFromPayload(std::move(payload), graphPosition);
}

bool EditorModule::AddRawDecodeNodeFromPayload(EditorNodeGraph::RawDecodePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawDecodeNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

bool EditorModule::AddRawDevelopNodeFromPayload(EditorNodeGraph::RawDevelopPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    NormalizeDevelopAutoGuidance(payload.autoGuidance);
    NormalizeDevelopSubjectImportance(payload.subjectImportance);
    if (!payload.integratedToneLayerJson.is_object()) {
        payload.integratedToneLayerJson = BuildDefaultIntegratedToneLayerJson();
    }
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawDevelopNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

bool EditorModule::UpdateDevelopAutoState(
    int nodeId,
    EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawMetadata& metadata,
    bool forceReanalysis,
    bool forceFullReanalysis) {
    if (payload.uiMode != EditorNodeGraph::RawDevelopUiMode::Auto) {
        m_DevelopAutoSolveTriggerHashes.erase(nodeId);
        m_DevelopAutoRawSolveTriggerHashes.erase(nodeId);
        m_DevelopAutoRawCalibrationHashes.erase(nodeId);
        return false;
    }

    const std::size_t triggerHash = BuildDevelopAutoSolveTriggerHash(payload, metadata);
    const std::size_t rawTriggerHash = BuildDevelopAutoRawSolveTriggerHash(payload, metadata);
    const auto it = m_DevelopAutoSolveTriggerHashes.find(nodeId);
    const auto rawIt = m_DevelopAutoRawSolveTriggerHashes.find(nodeId);
    const auto rawCalibrationIt = m_DevelopAutoRawCalibrationHashes.find(nodeId);
    const bool rawInputsChanged =
        rawIt == m_DevelopAutoRawSolveTriggerHashes.end() ||
        rawIt->second != rawTriggerHash;
    const bool explicitRawCalibrationNeeded =
        forceFullReanalysis &&
        (rawCalibrationIt == m_DevelopAutoRawCalibrationHashes.end() ||
         rawCalibrationIt->second != rawTriggerHash);
    const bool anySolveNeeded =
        forceReanalysis ||
        forceFullReanalysis ||
        it == m_DevelopAutoSolveTriggerHashes.end() ||
        it->second != triggerHash ||
        rawInputsChanged;
    if (!anySolveNeeded) {
        return false;
    }

    const bool fullSolveNeeded =
        forceFullReanalysis ||
        rawInputsChanged ||
        explicitRawCalibrationNeeded;
    ApplyDevelopAutoSolve(payload, metadata, true, fullSolveNeeded);
    m_DevelopAutoSolveTriggerHashes[nodeId] = BuildDevelopAutoSolveTriggerHash(payload, metadata);
    m_DevelopAutoRawSolveTriggerHashes[nodeId] = BuildDevelopAutoRawSolveTriggerHash(payload, metadata);
    if (fullSolveNeeded) {
        m_DevelopAutoRawCalibrationHashes[nodeId] = rawTriggerHash;
    }
    return true;
}

void EditorModule::AddRawDetailAutoMaskNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::RawDetailAutoMaskPayload payload;
    AddRawDetailAutoMaskNodeFromPayload(std::move(payload), graphPosition);
}

bool EditorModule::AddRawDetailAutoMaskNodeFromPayload(EditorNodeGraph::RawDetailAutoMaskPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawDetailAutoMaskNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

void EditorModule::AddRawDetailFusionNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    const EditorNodeGraph::Node* selected = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId());
    const bool selectedHasImageOutput = selected &&
        (selected->kind == EditorNodeGraph::NodeKind::Image ||
         selected->kind == EditorNodeGraph::NodeKind::RawDecode ||
         selected->kind == EditorNodeGraph::NodeKind::RawDevelop ||
         selected->kind == EditorNodeGraph::NodeKind::RawDetailFusion ||
         selected->kind == EditorNodeGraph::NodeKind::HdrMerge ||
         selected->kind == EditorNodeGraph::NodeKind::Lut ||
         selected->kind == EditorNodeGraph::NodeKind::Layer ||
         selected->kind == EditorNodeGraph::NodeKind::Mix ||
         selected->kind == EditorNodeGraph::NodeKind::DataMath ||
         selected->kind == EditorNodeGraph::NodeKind::ImageGenerator ||
         selected->kind == EditorNodeGraph::NodeKind::ChannelCombine);
    const int upstreamNodeId = selectedHasImageOutput ? selected->id : -1;

    EditorNodeGraph::RawDetailFusionPayload fusionPayload;
    EditorNodeGraph::Node* fusionNode = m_NodeGraph.AddRawDetailFusionNode(std::move(fusionPayload), graphPosition);
    if (!fusionNode) {
        return;
    }
    const int fusionNodeId = fusionNode->id;

    std::string errorMessage;
    if (upstreamNodeId > 0) {
        ConnectGraphSockets(upstreamNodeId, EditorNodeGraph::kImageOutputSocketId, fusionNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage);
    }
    SelectGraphNode(fusionNodeId);
    MarkRenderDirty(fusionNodeId);
}

bool EditorModule::AddRawDetailFusionNodeFromPayload(EditorNodeGraph::RawDetailFusionPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawDetailFusionNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

void EditorModule::AddHdrMergeNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    const EditorNodeGraph::Node* selected = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId());
    const bool selectedHasImageOutput = selected &&
        (selected->kind == EditorNodeGraph::NodeKind::Image ||
         selected->kind == EditorNodeGraph::NodeKind::RawDecode ||
         selected->kind == EditorNodeGraph::NodeKind::RawDevelop ||
         selected->kind == EditorNodeGraph::NodeKind::RawDetailFusion ||
         selected->kind == EditorNodeGraph::NodeKind::HdrMerge ||
         selected->kind == EditorNodeGraph::NodeKind::Lut ||
         selected->kind == EditorNodeGraph::NodeKind::Layer ||
         selected->kind == EditorNodeGraph::NodeKind::Mix ||
         selected->kind == EditorNodeGraph::NodeKind::DataMath ||
         selected->kind == EditorNodeGraph::NodeKind::ImageGenerator ||
         selected->kind == EditorNodeGraph::NodeKind::ChannelCombine);
    const int upstreamNodeId = selectedHasImageOutput ? selected->id : -1;

    EditorNodeGraph::HdrMergePayload payload;
    EditorNodeGraph::Node* hdrNode = m_NodeGraph.AddHdrMergeNode(std::move(payload), graphPosition);
    if (!hdrNode) {
        return;
    }
    const int hdrNodeId = hdrNode->id;

    std::string errorMessage;
    if (upstreamNodeId > 0) {
        if (!ConnectGraphSockets(upstreamNodeId, EditorNodeGraph::kImageOutputSocketId, hdrNodeId, EditorNodeGraph::kHdrMergeInput1SocketId, &errorMessage) &&
            !errorMessage.empty()) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "HDR Merge auto-connect failed: " + errorMessage,
                "hdr-merge-autoconnect");
        }
    }
    SelectGraphNode(hdrNodeId);
    MarkRenderDirty(hdrNodeId);
}

bool EditorModule::AddHdrMergeNodeFromPayload(EditorNodeGraph::HdrMergePayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddHdrMergeNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

void EditorModule::AddLutNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    const EditorNodeGraph::Node* selected = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId());
    const bool selectedHasImageOutput = selected &&
        (selected->kind == EditorNodeGraph::NodeKind::Image ||
         selected->kind == EditorNodeGraph::NodeKind::RawDecode ||
         selected->kind == EditorNodeGraph::NodeKind::RawDevelop ||
         selected->kind == EditorNodeGraph::NodeKind::RawDetailFusion ||
         selected->kind == EditorNodeGraph::NodeKind::HdrMerge ||
         selected->kind == EditorNodeGraph::NodeKind::Lut ||
         selected->kind == EditorNodeGraph::NodeKind::Layer ||
         selected->kind == EditorNodeGraph::NodeKind::Mix ||
         selected->kind == EditorNodeGraph::NodeKind::DataMath ||
         selected->kind == EditorNodeGraph::NodeKind::ImageGenerator ||
         selected->kind == EditorNodeGraph::NodeKind::ChannelCombine);
    const int upstreamNodeId = selectedHasImageOutput ? selected->id : -1;

    EditorNodeGraph::LutPayload payload;
    EditorNodeGraph::Node* lutNode = m_NodeGraph.AddLutNode(std::move(payload), graphPosition);
    if (!lutNode) {
        return;
    }
    const int lutNodeId = lutNode->id;
    std::string errorMessage;

    const EditorNodeGraph::Vec2 maskPosition{
        graphPosition.x - 172.0f,
        graphPosition.y + 78.0f
    };
    EditorNodeGraph::Node* solidMaskNode = m_NodeGraph.AddMaskGeneratorNode(EditorNodeGraph::MaskGeneratorKind::Solid, maskPosition);
    if (solidMaskNode) {
        if (!ConnectGraphSockets(
                solidMaskNode->id,
                EditorNodeGraph::kMaskOutputSocketId,
                lutNodeId,
                EditorNodeGraph::kMaskInputSocketId,
                &errorMessage) &&
            !errorMessage.empty()) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "LUT mask auto-connect failed: " + errorMessage,
                "lut-mask-autoconnect");
            errorMessage.clear();
        }
    }

    if (upstreamNodeId > 0) {
        if (!ConnectGraphSockets(upstreamNodeId, EditorNodeGraph::kImageOutputSocketId, lutNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage) &&
            !errorMessage.empty()) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "LUT auto-connect failed: " + errorMessage,
                "lut-autoconnect");
        }
    }

    SelectGraphNode(lutNodeId);
    MarkRenderDirty(lutNodeId);
    SwitchToComplexNodeSubWindow(lutNodeId);
}

bool EditorModule::AddLutNodeFromPayload(EditorNodeGraph::LutPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddLutNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

bool EditorModule::ConvertRawDetailFusionToHybrid(int fusionNodeId) {
    EditorNodeGraph::Node* fusionNode = m_NodeGraph.FindNode(fusionNodeId);
    if (!fusionNode || fusionNode->kind != EditorNodeGraph::NodeKind::RawDetailFusion) {
        return false;
    }
    const EditorNodeGraph::Link* maskInput = m_NodeGraph.FindInputLink(fusionNodeId, EditorNodeGraph::kMaskInputSocketId);
    if (!maskInput) {
        return false;
    }
    const EditorNodeGraph::Node* autoMaskNode = m_NodeGraph.FindNode(maskInput->fromNodeId);
    if (!autoMaskNode || autoMaskNode->kind != EditorNodeGraph::NodeKind::RawDetailAutoMask ||
        maskInput->fromSocketId != EditorNodeGraph::kMaskOutputSocketId) {
        return false;
    }
    const int autoMaskNodeId = autoMaskNode->id;
    const EditorNodeGraph::Vec2 autoMaskPosition = autoMaskNode->position;
    const EditorNodeGraph::Vec2 fusionPosition = fusionNode->position;

    const EditorNodeGraph::Vec2 pos{
        (autoMaskPosition.x + fusionPosition.x) * 0.5f,
        autoMaskPosition.y
    };
    EditorNodeGraph::Node* levelsNode = m_NodeGraph.AddMaskUtilityNode(EditorNodeGraph::MaskUtilityKind::Levels, pos);
    if (!levelsNode) {
        return false;
    }

    std::string errorMessage;
    if (!ConnectGraphSockets(autoMaskNodeId, EditorNodeGraph::kMaskOutputSocketId, levelsNode->id, EditorNodeGraph::kMaskUtilityInputSocketId, &errorMessage)) {
        return false;
    }
    if (!ConnectGraphSockets(levelsNode->id, EditorNodeGraph::kMaskOutputSocketId, fusionNodeId, EditorNodeGraph::kMaskInputSocketId, &errorMessage)) {
        return false;
    }
    SelectGraphNode(levelsNode->id);
    MarkRenderDirty(fusionNodeId);
    return true;
}

bool EditorModule::AddFullRawTreeToSource(int rawSourceNodeId) {
    const EditorNodeGraph::Node* rawSourceNode = m_NodeGraph.FindNode(rawSourceNodeId);
    if (!rawSourceNode || rawSourceNode->kind != EditorNodeGraph::NodeKind::RawSource) {
        return false;
    }

    const int completedBefore = GetCompletedChainCount();
    const EditorNodeGraph::Vec2 sourcePosition = rawSourceNode->position;
    SelectGraphNode(rawSourceNodeId);

    constexpr float kNodeSpacing = 280.0f;
    AddRawDecodeNodeAt(EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing * 1.0f, sourcePosition.y });
    const int rawDecodeNodeId = m_NodeGraph.GetSelectedNodeId();
    AddLayerNodeAt(LayerType::ToneCurve, EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing * 2.0f, sourcePosition.y });
    const int toneCurveNodeId = m_NodeGraph.GetSelectedNodeId();
    AddLayerNodeAt(LayerType::ViewTransform, EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing * 3.0f, sourcePosition.y });
    const int viewTransformNodeId = m_NodeGraph.GetSelectedNodeId();
    AddOutputNodeAt(EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing * 4.0f, sourcePosition.y });
    const int outputNodeId = m_NodeGraph.GetSelectedNodeId();

    if (rawDecodeNodeId <= 0 || toneCurveNodeId <= 0 || viewTransformNodeId <= 0 || outputNodeId <= 0) {
        return false;
    }

    std::string errorMessage;
    const bool ok =
        ConnectGraphSockets(rawSourceNodeId, EditorNodeGraph::kRawOutputSocketId, rawDecodeNodeId, EditorNodeGraph::kRawInputSocketId, &errorMessage) &&
        ConnectGraphSockets(rawDecodeNodeId, EditorNodeGraph::kImageOutputSocketId, toneCurveNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage) &&
        ConnectGraphSockets(toneCurveNodeId, EditorNodeGraph::kImageOutputSocketId, viewTransformNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage) &&
        ConnectGraphSockets(viewTransformNodeId, EditorNodeGraph::kImageOutputSocketId, outputNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage);
    if (!ok) {
        return false;
    }

    if (completedBefore < 2 && GetCompletedChainCount() >= 2) {
        EnsureCompositeNode();
    }
    EnsureCompositeSceneState(m_LastCompositeCanvasSize);
    MoveCompositeOutputToFront(outputNodeId);
    m_CompositeSelectedOutputNodeId = outputNodeId;
    m_NodeGraph.SetOutputNodeId(outputNodeId);
    MarkRenderDirty(outputNodeId);
    SelectGraphNode(outputNodeId);
    return true;
}

bool EditorModule::SplitLayerNodeIntoChannels(int layerNodeId) {
    EditorNodeGraph::Node* layerNode = m_NodeGraph.FindNode(layerNodeId);
    if (!layerNode || layerNode->kind != EditorNodeGraph::NodeKind::Layer) {
        return false;
    }
    if (layerNode->layerIndex < 0 || layerNode->layerIndex >= static_cast<int>(m_Layers.size())) {
        return false;
    }

    const EditorNodeGraph::Link* imageInput = m_NodeGraph.FindInputLink(layerNodeId, EditorNodeGraph::kImageInputSocketId);
    if (!imageInput) {
        return false;
    }

    std::vector<EditorNodeGraph::Link> outputLinks;
    std::vector<EditorNodeGraph::Link> maskLinks;
    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (link.fromNodeId == layerNodeId && link.fromSocketId == EditorNodeGraph::kImageOutputSocketId) {
            outputLinks.push_back(link);
        } else if (link.toNodeId == layerNodeId && link.toSocketId == EditorNodeGraph::kMaskInputSocketId) {
            maskLinks.push_back(link);
        }
    }
    if (outputLinks.empty()) {
        return false;
    }

    const EditorNodeGraph::Vec2 originalPos = layerNode->position;
    const int originalLayerIndex = layerNode->layerIndex;
    const LayerType originalLayerType = layerNode->layerType;
    const std::string originalTypeId = layerNode->typeId;
    const std::shared_ptr<LayerBase> originalLayer = m_Layers[originalLayerIndex];
    if (!originalLayer) {
        return false;
    }
    const EditorNodeGraph::Graph graphSnapshot = m_NodeGraph;
    const std::vector<std::shared_ptr<LayerBase>> layersSnapshot = m_Layers;

    std::vector<int> downstreamNodeIds = m_NodeGraph.GetDownstreamRenderNodeIds(layerNodeId);
    downstreamNodeIds.erase(
        std::remove(downstreamNodeIds.begin(), downstreamNodeIds.end(), layerNodeId),
        downstreamNodeIds.end());

    std::vector<std::pair<int, EditorNodeGraph::Vec2>> originalDownstreamPositions;
    originalDownstreamPositions.reserve(downstreamNodeIds.size());
    for (const int downstreamNodeId : downstreamNodeIds) {
        if (const EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(downstreamNodeId)) {
            originalDownstreamPositions.emplace_back(downstreamNodeId, downstream->position);
        }
    }

    std::array<std::shared_ptr<LayerBase>, 4> cloneLayers;
    for (std::shared_ptr<LayerBase>& cloneLayer : cloneLayers) {
        cloneLayer = CloneLayerInstance(originalLayer);
        if (!cloneLayer) {
            return false;
        }
    }

    for (const auto& entry : originalDownstreamPositions) {
        if (EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(entry.first)) {
            downstream->position.x = entry.second.x + 620.0f;
        }
    }

    AddChannelSplitNodeAt(EditorNodeGraph::Vec2{ originalPos.x - 250.0f, originalPos.y });
    const int splitNodeId = m_NodeGraph.GetSelectedNodeId();
    AddChannelCombineNodeAt(EditorNodeGraph::Vec2{ originalPos.x + 370.0f, originalPos.y });
    const int combineNodeId = m_NodeGraph.GetSelectedNodeId();
    if (splitNodeId <= 0 || combineNodeId <= 0) {
        for (const auto& entry : originalDownstreamPositions) {
            if (EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(entry.first)) {
                downstream->position = entry.second;
            }
        }
        if (splitNodeId > 0) {
            RemoveGraphNode(splitNodeId);
        }
        if (combineNodeId > 0) {
            RemoveGraphNode(combineNodeId);
        }
        return false;
    }

    std::array<int, 4> cloneNodeIds{ -1, -1, -1, -1 };
    constexpr const char* kChannels[4] = { "r", "g", "b", "a" };
    constexpr float kRowOffsets[4] = { -240.0f, -80.0f, 80.0f, 240.0f };
    bool createdAllClones = true;
    for (int i = 0; i < 4; ++i) {
        const int newLayerIndex = static_cast<int>(m_Layers.size());
        m_Layers.push_back(cloneLayers[i]);
        EditorNodeGraph::Node* cloneNode = m_NodeGraph.AddLayerNode(
            originalLayerType,
            newLayerIndex,
            EditorNodeGraph::Vec2{ originalPos.x + 60.0f, originalPos.y + kRowOffsets[i] });
        if (!cloneNode) {
            createdAllClones = false;
            break;
        }
        cloneNode->typeId = originalTypeId;
        cloneNodeIds[i] = cloneNode->id;
    }
    if (!createdAllClones) {
        while (static_cast<int>(m_Layers.size()) > originalLayerIndex + 1) {
            m_Layers.pop_back();
        }
        for (int cloneNodeId : cloneNodeIds) {
            if (cloneNodeId > 0) {
                RemoveGraphNode(cloneNodeId);
            }
        }
        RemoveGraphNode(splitNodeId);
        RemoveGraphNode(combineNodeId);
        for (const auto& entry : originalDownstreamPositions) {
            if (EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(entry.first)) {
                downstream->position = entry.second;
            }
        }
        RefreshGraphLayerMetadata();
        MarkRenderDirty();
        return false;
    }

    std::string errorMessage;
    bool ok = ConnectGraphSockets(imageInput->fromNodeId, imageInput->fromSocketId, splitNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage);
    for (int i = 0; ok && i < 4; ++i) {
        ok = ConnectGraphSockets(splitNodeId, kChannels[i], cloneNodeIds[i], EditorNodeGraph::kImageInputSocketId, &errorMessage);
        if (ok) {
            ok = ConnectGraphSockets(cloneNodeIds[i], EditorNodeGraph::kImageOutputSocketId, combineNodeId, kChannels[i], &errorMessage);
        }
    }

    for (const EditorNodeGraph::Link& maskLink : maskLinks) {
        for (int cloneNodeId : cloneNodeIds) {
            if (!ok) {
                break;
            }
            ok = ConnectGraphSockets(maskLink.fromNodeId, maskLink.fromSocketId, cloneNodeId, maskLink.toSocketId, &errorMessage);
        }
        if (!ok) {
            break;
        }
    }

    for (const EditorNodeGraph::Link& outputLink : outputLinks) {
        if (!ok) {
            break;
        }
        if (outputLink.toNodeId == combineNodeId) {
            continue;
        }
        ok = ConnectGraphSockets(combineNodeId, EditorNodeGraph::kImageOutputSocketId, outputLink.toNodeId, outputLink.toSocketId, &errorMessage);
    }

    if (!ok) {
        m_NodeGraph = graphSnapshot;
        m_Layers = layersSnapshot;
        RefreshGraphLayerMetadata();
        MarkRenderDirty();
        return false;
    }

    ClearGraphAutoFocusIfTrackedNode(layerNodeId);
    if (m_CanvasToolOwnerNodeId == layerNodeId) {
        CancelCanvasTool();
    }
    RemoveLayer(originalLayerIndex);
    RefreshGraphLayerMetadata();
    SelectGraphNode(combineNodeId);
    MarkRenderDirty(combineNodeId);
    return true;
}

bool EditorModule::ToggleOutputNodeEnabled(int outputNodeId) {
    EditorNodeGraph::Node* outputNode = m_NodeGraph.FindNode(outputNodeId);
    if (!outputNode || outputNode->kind != EditorNodeGraph::NodeKind::Output) {
        return false;
    }

    const bool enabled = !outputNode->outputEnabled;
    if (!m_NodeGraph.SetOutputNodeEnabled(outputNodeId, enabled)) {
        return false;
    }

    outputNode = m_NodeGraph.FindNode(outputNodeId);
    if (!outputNode) {
        return false;
    }
    EditorNodeGraphDefinitions::ApplyNodeMetadata(*outputNode);

    if (!outputNode->outputEnabled && m_CompositeSelectedOutputNodeId == outputNodeId) {
        int replacementOutputNodeId = m_NodeGraph.ResolvePreviewOutputNodeId();
        if (replacementOutputNodeId == outputNodeId) {
            replacementOutputNodeId = -1;
        }
        m_CompositeSelectedOutputNodeId = replacementOutputNodeId;
    }
    if (!outputNode->outputEnabled) {
        if (CompositeSceneItem* item = FindCompositeSceneItem(outputNodeId)) {
            if (item->texture != 0) {
                glDeleteTextures(1, &item->texture);
                item->texture = 0;
            }
        }
        m_CompositeSceneItems.erase(
            std::remove_if(
                m_CompositeSceneItems.begin(),
                m_CompositeSceneItems.end(),
                [outputNodeId](const CompositeSceneItem& item) { return item.outputNodeId == outputNodeId; }),
            m_CompositeSceneItems.end());
        m_CompositeZOrder.erase(
            std::remove(m_CompositeZOrder.begin(), m_CompositeZOrder.end(), outputNodeId),
            m_CompositeZOrder.end());
        m_CompositeOutputDirtyGenerations.erase(outputNodeId);
        m_CompositeOutputRequestedGenerations.erase(outputNodeId);
        m_CompositeOutputCompletedGenerations.erase(outputNodeId);
    }
    if (!m_NodeGraph.IsOutputConnected()) {
        m_Pipeline.ClearOutput();
    }
    EnsureCompositeSceneState(m_LastCompositeCanvasSize);
    MarkRenderDirty(outputNodeId);
    return true;
}

bool EditorModule::RequestGraphImageChainImports(
    const std::vector<std::string>& paths,
    EditorNodeGraph::Vec2 sourcePosition) {
    std::vector<std::string> validPaths;
    validPaths.reserve(paths.size());
    for (const std::string& path : paths) {
        if (!path.empty()) {
            validPaths.push_back(path);
        }
    }
    if (validPaths.empty()) {
        return false;
    }
    if (Async::IsBusy(m_GraphDropImportTaskState)) {
        m_PendingGraphDropImports.push_back(PendingGraphDropImportRequest{ std::move(validPaths), sourcePosition });
        return true;
    }

    return StartGraphImageChainImport(std::move(validPaths), sourcePosition);
}

bool EditorModule::StartGraphImageChainImport(
    std::vector<std::string> validPaths,
    EditorNodeGraph::Vec2 sourcePosition) {
    if (validPaths.empty()) {
        return false;
    }

    ++m_GraphDropImportGeneration;
    const std::uint64_t generation = m_GraphDropImportGeneration;
    m_GraphDropImportTaskState = Async::TaskState::Queued;
    m_GraphDropImportStatusText = validPaths.size() > 1
        ? "Loading dropped images into the graph..."
        : "Loading dropped image into the graph...";

    Async::TaskSystem::Get().Submit([this, generation, validPaths = std::move(validPaths), sourcePosition]() mutable {
        struct DecodedDropImage {
            std::string path;
            DecodedImageData decoded;
        };

        std::vector<DecodedDropImage> decodedImages;
        decodedImages.reserve(validPaths.size());
        std::vector<std::string> rawPaths;
        for (const std::string& path : validPaths) {
            if (Raw::RawLoader::IsRawPath(path)) {
                rawPaths.push_back(path);
                continue;
            }
            DecodedImageData decoded;
            if (!DecodeImageFromFile(path, decoded) || decoded.pixels.empty()) {
                continue;
            }
            decodedImages.push_back(DecodedDropImage{ path, std::move(decoded) });
        }

        Async::TaskSystem::Get().PostToMain([
            this,
            generation,
            sourcePosition,
            requestedCount = validPaths.size(),
            decodedImages = std::move(decodedImages),
            rawPaths = std::move(rawPaths)
        ]() mutable {
            if (generation != m_GraphDropImportGeneration) {
                return;
            }

            const Raw::LibRawRuntimeStatus& rawRuntimeStatus = Raw::GetLibRawRuntimeStatus();
            const bool rawRuntimeUnavailable = !rawPaths.empty() && !rawRuntimeStatus.runtimeAvailable;

            if (decodedImages.empty() && rawPaths.empty()) {
                m_GraphDropImportTaskState = Async::TaskState::Failed;
                m_GraphDropImportStatusText = "Failed to import the dropped images.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to import the dropped images.", "editor-graph-drop-import");
                if (!m_PendingGraphDropImports.empty()) {
                    PendingGraphDropImportRequest nextRequest = std::move(m_PendingGraphDropImports.front());
                    m_PendingGraphDropImports.erase(m_PendingGraphDropImports.begin());
                    StartGraphImageChainImport(std::move(nextRequest.paths), nextRequest.sourcePosition);
                }
                return;
            }

            m_GraphDropImportTaskState = Async::TaskState::Applying;
            m_GraphDropImportStatusText = "Creating image nodes...";

            constexpr float kGraphDropRowSpacing = 190.0f;
            const std::size_t totalNodes = decodedImages.size() + rawPaths.size();
            const float startY = sourcePosition.y - (static_cast<float>(totalNodes - 1) * kGraphDropRowSpacing * 0.5f);
            int importedCount = 0;
            size_t outputIndex = 0;
            for (size_t index = 0; index < decodedImages.size(); ++index, ++outputIndex) {
                EditorNodeGraph::Vec2 nodePosition = sourcePosition;
                nodePosition.y = startY + static_cast<float>(outputIndex) * kGraphDropRowSpacing;
                if (AddGraphImageChainFromPayload(
                    BuildImagePayloadFromDecoded(decodedImages[index].path, std::move(decodedImages[index].decoded)),
                    nodePosition)) {
                    ++importedCount;
                }
            }
            if (rawRuntimeUnavailable) {
                QueueUiNotification(UiNotificationSeverity::Error, rawRuntimeStatus.message, "editor-raw-runtime");
                outputIndex += rawPaths.size();
            } else {
                for (const std::string& path : rawPaths) {
                    EditorNodeGraph::Vec2 nodePosition = sourcePosition;
                    nodePosition.y = startY + static_cast<float>(outputIndex++) * kGraphDropRowSpacing;
                    if (AddGraphRawChainFromFile(path, nodePosition)) {
                        ++importedCount;
                    }
                }
            }

            if (importedCount <= 0) {
                m_GraphDropImportTaskState = Async::TaskState::Failed;
                if (rawRuntimeUnavailable) {
                    m_GraphDropImportStatusText = rawRuntimeStatus.message;
                } else {
                    m_GraphDropImportStatusText = "Failed to create graph nodes for the dropped images.";
                    QueueUiNotification(UiNotificationSeverity::Error, "Failed to create graph nodes for the dropped images.", "editor-graph-drop-import");
                }
                if (!m_PendingGraphDropImports.empty()) {
                    PendingGraphDropImportRequest nextRequest = std::move(m_PendingGraphDropImports.front());
                    m_PendingGraphDropImports.erase(m_PendingGraphDropImports.begin());
                    StartGraphImageChainImport(std::move(nextRequest.paths), nextRequest.sourcePosition);
                }
                return;
            }

            m_GraphDropImportTaskState = Async::TaskState::Idle;
            if (importedCount == static_cast<int>(requestedCount)) {
                m_GraphDropImportStatusText = importedCount == 1
                    ? "Imported 1 image into the graph."
                    : "Imported " + std::to_string(importedCount) + " images into the graph.";
            } else {
                m_GraphDropImportStatusText =
                    "Imported " + std::to_string(importedCount) + " of " + std::to_string(requestedCount) + " dropped images.";
            }
            QueueUiNotification(UiNotificationSeverity::Success, m_GraphDropImportStatusText, "editor-graph-drop-import");

            if (!m_PendingGraphDropImports.empty()) {
                PendingGraphDropImportRequest nextRequest = std::move(m_PendingGraphDropImports.front());
                m_PendingGraphDropImports.erase(m_PendingGraphDropImports.begin());
                StartGraphImageChainImport(std::move(nextRequest.paths), nextRequest.sourcePosition);
            }
        });
    });

    return true;
}

bool EditorModule::ConnectGraphImageNode(int nodeId) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node) {
        return false;
    }
    bool sourceChanged = false;
    if (node->kind == EditorNodeGraph::NodeKind::Image) {
        if (node->image.pixels.empty()) {
            return false;
        }
        LoadSourceFromPixels(node->image.pixels.data(), node->image.width, node->image.height, node->image.channels);
        m_NodeGraph.SetActiveImageNodeId(nodeId);
        sourceChanged = true;
    } else if (node->kind != EditorNodeGraph::NodeKind::RawDevelop) {
        return false;
    }

    m_NodeGraph.ConnectImageToOutput(nodeId);
    if (sourceChanged) {
        MarkNodeBrowserThumbnailSourceChanged();
    }
    SelectGraphNode(nodeId);
    MarkRenderDirty();
    return true;
}

bool EditorModule::RotateImageNode(int nodeId, int quarterTurnsClockwise) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::Image) {
        return false;
    }

    const int normalizedTurns = NormalizeQuarterTurnsClockwise(quarterTurnsClockwise);
    if (normalizedTurns == 0 || node->image.pixels.empty() || node->image.width <= 0 || node->image.height <= 0) {
        return normalizedTurns == 0;
    }

    int rotatedWidth = node->image.width;
    int rotatedHeight = node->image.height;
    std::vector<unsigned char> rotatedPixels = RotateBottomLeftImagePixels(
        node->image.pixels,
        node->image.width,
        node->image.height,
        node->image.channels,
        normalizedTurns,
        rotatedWidth,
        rotatedHeight);
    if (rotatedPixels.empty()) {
        return false;
    }

    node->image.width = rotatedWidth;
    node->image.height = rotatedHeight;
    node->image.pixels = std::move(rotatedPixels);
    node->image.pngBytes = EncodePngBytesForImageStorage(
        node->image.pixels,
        node->image.width,
        node->image.height,
        node->image.channels);
    EditorNodeGraph::InvalidateImagePayloadRuntime(node->image);

    if (m_NodeGraph.GetActiveImageNodeId() == nodeId) {
        LoadSourceFromPixels(
            node->image.pixels.data(),
            node->image.width,
            node->image.height,
            node->image.channels);
        MarkNodeBrowserThumbnailSourceChanged();
    }

    MarkRenderDirty(nodeId);
    return true;
}

bool EditorModule::ConnectGraphNodes(int fromNodeId, int toNodeId, std::string* errorMessage) {
    EditorNodeGraph::Node* from = m_NodeGraph.FindNode(fromNodeId);
    if (from && from->kind == EditorNodeGraph::NodeKind::Image) {
        if (from->image.pixels.empty()) {
            if (errorMessage) *errorMessage = "Image node has no embedded pixels.";
            return false;
        }
    }

    const std::string fromSocket = from ? m_NodeGraph.DefaultOutputSocket(*from) : std::string();
    const EditorNodeGraph::Node* pendingTo = m_NodeGraph.FindNode(toNodeId);
    const std::string toSocket = pendingTo ? m_NodeGraph.DefaultInputSocket(*pendingTo) : std::string();
    return ConnectGraphSockets(fromNodeId, fromSocket, toNodeId, toSocket, errorMessage);
}

int EditorModule::FindDirectDownstreamToneCurveNode(int sourceNodeId) const {
    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (link.fromNodeId != sourceNodeId ||
            link.fromSocketId != EditorNodeGraph::kImageOutputSocketId ||
            m_NodeGraph.GetLinkRole(link) != EditorNodeGraph::LinkRole::Render) {
            continue;
        }
        const EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(link.toNodeId);
        if (downstream &&
            downstream->kind == EditorNodeGraph::NodeKind::Layer &&
            downstream->layerType == LayerType::ToneCurve) {
            return downstream->id;
        }
    }
    return -1;
}

int EditorModule::FindNearestDownstreamToneCurveNode(int sourceNodeId) const {
    int currentNodeId = sourceNodeId;
    const std::size_t maxHops = m_NodeGraph.GetNodes().size();
    for (std::size_t hop = 0; hop < maxHops && currentNodeId > 0; ++hop) {
        currentNodeId = m_NodeGraph.FindAdjacentMainChainNodeId(currentNodeId, 1);
        if (currentNodeId <= 0) {
            return -1;
        }
        const EditorNodeGraph::Node* currentNode = m_NodeGraph.FindNode(currentNodeId);
        if (!currentNode) {
            return -1;
        }
        if (currentNode->kind == EditorNodeGraph::NodeKind::Layer &&
            currentNode->layerType == LayerType::ToneCurve) {
            return currentNode->id;
        }
    }
    return -1;
}

int EditorModule::FindNearestUpstreamRawDevelopNode(int sourceNodeId) const {
    int currentNodeId = sourceNodeId;
    const std::size_t maxHops = m_NodeGraph.GetNodes().size();
    for (std::size_t hop = 0; hop < maxHops && currentNodeId > 0; ++hop) {
        const EditorNodeGraph::Node* currentNode = m_NodeGraph.FindNode(currentNodeId);
        if (!currentNode) {
            return -1;
        }
        if (currentNode->kind == EditorNodeGraph::NodeKind::RawDevelop) {
            return currentNode->id;
        }
        currentNodeId = m_NodeGraph.FindAdjacentMainChainNodeId(currentNodeId, -1);
    }
    return -1;
}

bool EditorModule::RawDevelopNodeUsesIntegratedTone(int nodeId) const {
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    return node &&
        node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled;
}

bool EditorModule::CanAbsorbDirectDownstreamToneFinishIntoDevelop(int sourceNodeId, std::string* reason) const {
    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(sourceNodeId);
    if (!sourceNode || sourceNode->kind != EditorNodeGraph::NodeKind::RawDevelop) {
        if (reason) {
            *reason = "No Develop node was found for this merge action.";
        }
        return false;
    }

    const int directToneNodeId = FindDirectDownstreamToneCurveNode(sourceNodeId);
    if (directToneNodeId <= 0) {
        if (reason) {
            *reason = "No direct downstream Tone Curve is connected to this Develop node.";
        }
        return false;
    }

    const EditorNodeGraph::Node* toneNode = m_NodeGraph.FindNode(directToneNodeId);
    if (!toneNode ||
        toneNode->kind != EditorNodeGraph::NodeKind::Layer ||
        toneNode->layerType != LayerType::ToneCurve) {
        if (reason) {
            *reason = "The direct downstream node is not a Tone Curve layer.";
        }
        return false;
    }

    const EditorNodeGraph::Link* toneMaskLink =
        m_NodeGraph.FindAnyInputLink(directToneNodeId, EditorNodeGraph::kMaskInputSocketId);
    const EditorNodeGraph::Link* developMaskLink =
        m_NodeGraph.FindAnyInputLink(sourceNodeId, EditorNodeGraph::kMaskInputSocketId);
    if (toneMaskLink && developMaskLink &&
        (toneMaskLink->fromNodeId != developMaskLink->fromNodeId ||
         toneMaskLink->fromSocketId != developMaskLink->fromSocketId)) {
        if (reason) {
            *reason = "Develop already has a different finish mask connected, so this legacy Tone Curve cannot be absorbed automatically.";
        }
        return false;
    }

    if (reason) {
        reason->clear();
    }
    return true;
}

bool EditorModule::SelectOrCreateToneFinishAfterNode(int sourceNodeId) {
    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(sourceNodeId);
    if (!sourceNode) {
        return false;
    }

    if (const int existingToneNodeId = FindNearestDownstreamToneCurveNode(sourceNodeId);
        existingToneNodeId > 0) {
        SelectGraphNode(existingToneNodeId);
        return true;
    }

    std::vector<EditorNodeGraph::Link> downstreamLinks;
    EditorNodeGraph::Vec2 tonePosition{ sourceNode->position.x + 280.0f, sourceNode->position.y };
    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (link.fromNodeId != sourceNodeId ||
            link.fromSocketId != EditorNodeGraph::kImageOutputSocketId ||
            m_NodeGraph.GetLinkRole(link) != EditorNodeGraph::LinkRole::Render) {
            continue;
        }
        downstreamLinks.push_back(link);
    }

    if (!downstreamLinks.empty()) {
        if (const EditorNodeGraph::Node* firstDownstream = m_NodeGraph.FindNode(downstreamLinks.front().toNodeId)) {
            tonePosition.x = (sourceNode->position.x + firstDownstream->position.x) * 0.5f;
            tonePosition.y = (sourceNode->position.y + firstDownstream->position.y) * 0.5f;
        }
    }

    AddLayerNodeAt(LayerType::ToneCurve, tonePosition);
    const int toneNodeId = m_NodeGraph.GetSelectedNodeId();
    if (toneNodeId <= 0) {
        QueueUiNotification(
            UiNotificationSeverity::Error,
            "Could not create a downstream Tone Curve node.",
            "raw-develop-tone-finish-create");
        return false;
    }

    std::string errorMessage;
    if (!ConnectGraphSockets(
            sourceNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            toneNodeId,
            EditorNodeGraph::kImageInputSocketId,
            &errorMessage)) {
        QueueUiNotification(
            UiNotificationSeverity::Error,
            errorMessage.empty() ? "Could not connect Develop to the new Tone Curve node." : errorMessage,
            "raw-develop-tone-finish-connect");
        return false;
    }

    for (const EditorNodeGraph::Link& downstreamLink : downstreamLinks) {
        if (!ConnectGraphSockets(
                toneNodeId,
                EditorNodeGraph::kImageOutputSocketId,
                downstreamLink.toNodeId,
                downstreamLink.toSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty() ? "Could not reconnect one of the downstream finish-tone links." : errorMessage,
                "raw-develop-tone-finish-rewire");
            SelectGraphNode(toneNodeId);
            return false;
        }
    }

    SelectGraphNode(toneNodeId);
    return true;
}

bool EditorModule::AbsorbDirectDownstreamToneFinishIntoDevelop(int sourceNodeId) {
    EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(sourceNodeId);
    if (!sourceNode || sourceNode->kind != EditorNodeGraph::NodeKind::RawDevelop) {
        return false;
    }

    std::string absorbReason;
    if (!CanAbsorbDirectDownstreamToneFinishIntoDevelop(sourceNodeId, &absorbReason)) {
        if (!absorbReason.empty()) {
            QueueUiNotification(
                UiNotificationSeverity::Info,
                absorbReason,
                "raw-develop-tone-finish-absorb-unsafe");
        }
        return false;
    }
    const int directToneNodeId = FindDirectDownstreamToneCurveNode(sourceNodeId);

    const EditorNodeGraph::Node* toneNode = m_NodeGraph.FindNode(directToneNodeId);
    if (!toneNode ||
        toneNode->kind != EditorNodeGraph::NodeKind::Layer ||
        toneNode->layerIndex < 0 ||
        toneNode->layerIndex >= static_cast<int>(m_Layers.size()) ||
        !m_Layers[toneNode->layerIndex]) {
        return false;
    }

    ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[toneNode->layerIndex].get());
    if (!toneCurve) {
        return false;
    }

    const EditorNodeGraph::Link* toneMaskLink =
        m_NodeGraph.FindAnyInputLink(directToneNodeId, EditorNodeGraph::kMaskInputSocketId);
    const EditorNodeGraph::Link* developMaskLink =
        m_NodeGraph.FindAnyInputLink(sourceNodeId, EditorNodeGraph::kMaskInputSocketId);

    sourceNode->rawDevelop.integratedToneEnabled = true;
    sourceNode->rawDevelop.integratedToneLayerJson = toneCurve->Serialize();

    if (toneMaskLink && !developMaskLink) {
        std::string errorMessage;
        if (!ConnectGraphSockets(
                toneMaskLink->fromNodeId,
                toneMaskLink->fromSocketId,
                sourceNodeId,
                EditorNodeGraph::kMaskInputSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty()
                    ? "Could not transfer the legacy Tone Curve finish mask into Develop."
                    : errorMessage,
                "raw-develop-tone-finish-mask-transfer");
            return false;
        }
    }

    if (!RemoveGraphNode(directToneNodeId)) {
        return false;
    }

    SelectGraphNode(sourceNodeId);
    MarkRenderDirty(sourceNodeId);
    return true;
}

bool EditorModule::SelectUpstreamDevelopForToneNode(int toneNodeId) {
    const EditorNodeGraph::Node* toneNode = m_NodeGraph.FindNode(toneNodeId);
    if (!toneNode ||
        toneNode->kind != EditorNodeGraph::NodeKind::Layer ||
        toneNode->layerType != LayerType::ToneCurve) {
        return false;
    }

    const int rawDevelopNodeId = FindNearestUpstreamRawDevelopNode(toneNodeId);
    if (rawDevelopNodeId <= 0) {
        return false;
    }

    SelectGraphNode(rawDevelopNodeId);
    return true;
}

bool EditorModule::ConnectGraphSockets(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId, std::string* errorMessage) {
    EditorNodeGraph::Node* from = m_NodeGraph.FindNode(fromNodeId);
    if (from && from->kind == EditorNodeGraph::NodeKind::Image) {
        if (from->image.pixels.empty()) {
            if (errorMessage) *errorMessage = "Image node has no embedded pixels.";
            return false;
        }
    }

    const EditorNodeGraph::Node* targetNode = m_NodeGraph.FindNode(toNodeId);
    if (targetNode &&
        targetNode->kind == EditorNodeGraph::NodeKind::Output &&
        toSocketId == EditorNodeGraph::kImageInputSocketId &&
        fromSocketId == EditorNodeGraph::kImageOutputSocketId) {
        std::unordered_set<int> visiting;
        const ScenePathState scenePath = AnalyzeScenePathFromNode(m_NodeGraph, m_Layers, fromNodeId, visiting);
        if (scenePath.sceneReferred && !scenePath.hasViewTransform) {
            const EditorNodeGraph::Vec2 fromPosition = from ? from->position : EditorNodeGraph::Vec2{};
            const EditorNodeGraph::Vec2 toPosition = targetNode->position;
            const EditorNodeGraph::Vec2 viewPosition{
                (fromPosition.x + toPosition.x) * 0.5f,
                (fromPosition.y + toPosition.y) * 0.5f
            };
            AddLayerNodeAt(LayerType::ViewTransform, viewPosition);
            const int viewNodeId = m_NodeGraph.GetSelectedNodeId();
            if (viewNodeId <= 0) {
                if (errorMessage) *errorMessage = "Could not create View Transform node.";
                return false;
            }
            if (!m_NodeGraph.TryConnectSockets(fromNodeId, fromSocketId, viewNodeId, EditorNodeGraph::kImageInputSocketId, errorMessage)) {
                return false;
            }
            if (!m_NodeGraph.TryConnectSockets(viewNodeId, EditorNodeGraph::kImageOutputSocketId, toNodeId, toSocketId, errorMessage)) {
                return false;
            }
            ApplyGraphLayerOrder();
            MarkRenderDirty();
            SelectGraphNode(viewNodeId);
            return true;
        }
    }

    if (!m_NodeGraph.TryConnectSockets(fromNodeId, fromSocketId, toNodeId, toSocketId, errorMessage)) {
        return false;
    }

    if (from && from->kind == EditorNodeGraph::NodeKind::Image &&
        ConnectionUsesImageAsRenderSource(m_NodeGraph, fromNodeId, fromSocketId, toNodeId, toSocketId)) {
        LoadSourceFromPixels(from->image.pixels.data(), from->image.width, from->image.height, from->image.channels);
        m_NodeGraph.SetActiveImageNodeId(fromNodeId);
        MarkNodeBrowserThumbnailSourceChanged();
    }

    ApplyGraphLayerOrder();
    MarkRenderDirty();
    const EditorNodeGraph::Node* to = m_NodeGraph.FindNode(toNodeId);
    if (to && to->kind == EditorNodeGraph::NodeKind::Layer) {
        SelectGraphNode(toNodeId);
    } else if (from) {
        SelectGraphNode(fromNodeId);
    }
    return true;
}

bool EditorModule::OutputPathNeedsViewTransform(int outputNodeId) const {
    const EditorNodeGraph::Node* output = m_NodeGraph.FindNode(outputNodeId);
    if (!output || output->kind != EditorNodeGraph::NodeKind::Output) {
        return false;
    }
    const EditorNodeGraph::Link* input = m_NodeGraph.FindInputLink(outputNodeId, EditorNodeGraph::kImageInputSocketId);
    if (!input) {
        return false;
    }
    std::unordered_set<int> visiting;
    const ScenePathState scenePath = AnalyzeScenePathFromNode(m_NodeGraph, m_Layers, input->fromNodeId, visiting);
    return scenePath.sceneReferred && !scenePath.hasViewTransform;
}

bool EditorModule::SelectedLayerInputContainsViewTransform() const {
    if (m_SelectedLayerIndex < 0) {
        return false;
    }
    const EditorNodeGraph::Node* selectedNode = m_NodeGraph.FindNodeByLayerIndex(m_SelectedLayerIndex);
    if (!selectedNode || selectedNode->kind != EditorNodeGraph::NodeKind::Layer) {
        return false;
    }

    std::unordered_set<int> visiting;
    std::function<bool(int)> inputContainsViewTransform = [&](int nodeId) -> bool {
        if (!visiting.insert(nodeId).second) {
            return false;
        }
        const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
        if (!node) {
            visiting.erase(nodeId);
            return false;
        }
        if (node->kind == EditorNodeGraph::NodeKind::Layer && node->layerType == LayerType::ViewTransform) {
            visiting.erase(nodeId);
            return true;
        }
        auto checkInput = [&](const std::string& socketId) {
            const EditorNodeGraph::Link* link = m_NodeGraph.FindInputLink(nodeId, socketId);
            return link ? inputContainsViewTransform(link->fromNodeId) : false;
        };
        bool found = false;
        if (node->kind == EditorNodeGraph::NodeKind::Mix ||
            node->kind == EditorNodeGraph::NodeKind::DataMath) {
            found = checkInput(EditorNodeGraph::kMixInputASocketId) ||
                checkInput(EditorNodeGraph::kMixInputBSocketId);
        } else if (node->kind == EditorNodeGraph::NodeKind::HdrMerge) {
            found = checkInput(EditorNodeGraph::kHdrMergeInput1SocketId) ||
                checkInput(EditorNodeGraph::kHdrMergeInput2SocketId) ||
                checkInput(EditorNodeGraph::kHdrMergeInput3SocketId);
        } else if (node->kind == EditorNodeGraph::NodeKind::ChannelCombine) {
            found = checkInput("r") || checkInput("g") || checkInput("b") || checkInput("a");
        } else if (node->kind != EditorNodeGraph::NodeKind::RawSource &&
            node->kind != EditorNodeGraph::NodeKind::Image) {
            found = checkInput(EditorNodeGraph::kImageInputSocketId);
        }
        visiting.erase(nodeId);
        return found;
    };

    const EditorNodeGraph::Link* input = m_NodeGraph.FindInputLink(selectedNode->id, EditorNodeGraph::kImageInputSocketId);
    return input ? inputContainsViewTransform(input->fromNodeId) : false;
}

bool EditorModule::RenderLayerControlsWithDirtyTracking(
    EditorNodeGraph::Node& node,
    const std::function<void(LayerBase&)>& renderControls) {
    if (node.kind != EditorNodeGraph::NodeKind::Layer ||
        node.layerIndex < 0 ||
        node.layerIndex >= static_cast<int>(m_Layers.size()) ||
        !m_Layers[node.layerIndex]) {
        return false;
    }

    LayerBase& layer = *m_Layers[node.layerIndex];
    const nlohmann::json before = layer.Serialize();
    const bool beforeEnabled = layer.IsEnabled();
    const bool beforeVisible = layer.IsVisible();
    renderControls(layer);
    const nlohmann::json after = layer.Serialize();
    if (before != after ||
        beforeEnabled != layer.IsEnabled() ||
        beforeVisible != layer.IsVisible()) {
        MarkRenderDirty(node.id);
        return true;
    }
    return false;
}

void EditorModule::MarkSelectedLayerRenderDirty() {
    if (m_SelectedLayerIndex >= 0) {
        if (const EditorNodeGraph::Node* node = m_NodeGraph.FindNodeByLayerIndex(m_SelectedLayerIndex)) {
            MarkRenderDirty(node->id);
            return;
        }
    }
    MarkRenderDirty();
}

bool EditorModule::RemoveGraphLink(int fromNodeId, int toNodeId) {
    const bool removed = m_NodeGraph.RemoveLink(fromNodeId, toNodeId);
    if (removed) {
        ApplyGraphLayerOrder();
        if (!m_NodeGraph.IsOutputConnected()) {
            m_Pipeline.ClearOutput();
        }
        MarkRenderDirty();
    }
    return removed;
}

bool EditorModule::RemoveGraphLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) {
    const bool removed = m_NodeGraph.RemoveLink(fromNodeId, fromSocketId, toNodeId, toSocketId);
    if (removed) {
        ApplyGraphLayerOrder();
        if (!m_NodeGraph.IsOutputConnected()) {
            m_Pipeline.ClearOutput();
        }
        MarkRenderDirty();
    }
    return removed;
}

bool EditorModule::DeleteSelectedGraphLink() {
    const bool removed = m_NodeGraph.RemoveSelectedLink();
    if (removed) {
        ApplyGraphLayerOrder();
        if (!m_NodeGraph.IsOutputConnected()) {
            m_Pipeline.ClearOutput();
        }
        MarkRenderDirty();
    }
    return removed;
}

bool EditorModule::RemoveGraphNode(int nodeId) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node) {
        return false;
    }

    const std::vector<GraphReconnectPlan> reconnectPlans =
        BuildReconnectPlansForNodeRemoval(m_NodeGraph, *node);

    ClearGraphAutoFocusIfTrackedNode(nodeId);
    if (m_CanvasToolOwnerNodeId == nodeId) {
        CancelCanvasTool();
    }

    if (node->kind == EditorNodeGraph::NodeKind::Layer) {
        const int layerIndex = node->layerIndex;
        RemoveLayer(node->layerIndex);
        for (const GraphReconnectPlan& plan : reconnectPlans) {
            std::string errorMessage;
            ConnectGraphSockets(plan.fromNodeId, plan.fromSocketId, plan.toNodeId, plan.toSocketId, &errorMessage);
        }
        if (layerIndex >= 0) {
            RefreshGraphLayerMetadata();
        }
        return true;
    }

    const bool removed = m_NodeGraph.RemoveNode(nodeId);
    if (removed && !m_NodeGraph.IsOutputConnected()) {
        m_Pipeline.ClearOutput();
    }
    if (removed) {
        for (const GraphReconnectPlan& plan : reconnectPlans) {
            std::string errorMessage;
            ConnectGraphSockets(plan.fromNodeId, plan.fromSocketId, plan.toNodeId, plan.toSocketId, &errorMessage);
        }
        MarkRenderDirty();
    }
    return removed;
}

bool EditorModule::DeleteSelectedGraphNodes() {
    std::vector<int> nodeIds = m_NodeGraph.GetSelectedNodeIds();
    if (nodeIds.empty()) {
        return false;
    }

    std::sort(nodeIds.begin(), nodeIds.end(), [this](int a, int b) {
        const EditorNodeGraph::Node* nodeA = m_NodeGraph.FindNode(a);
        const EditorNodeGraph::Node* nodeB = m_NodeGraph.FindNode(b);
        const int layerA = nodeA && nodeA->kind == EditorNodeGraph::NodeKind::Layer ? nodeA->layerIndex : -1;
        const int layerB = nodeB && nodeB->kind == EditorNodeGraph::NodeKind::Layer ? nodeB->layerIndex : -1;
        return layerA > layerB;
    });

    bool removedAny = false;
    for (int nodeId : nodeIds) {
        removedAny = RemoveGraphNode(nodeId) || removedAny;
    }
    m_NodeGraph.ClearSelection();
    RefreshGraphLayerMetadata();
    if (removedAny) {
        MarkRenderDirty();
    }
    return removedAny;
}

void EditorModule::AddScopeNodeAt(EditorNodeGraph::ScopeKind scopeKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddScopeNode(scopeKind, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind maskKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMaskGeneratorNode(maskKind, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddMaskCombineNodeAt(EditorNodeGraph::MaskCombineMode combineMode, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMaskCombineNode(combineMode, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind utilityKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMaskUtilityNode(utilityKind, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddCustomMaskNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::CustomMaskPayload payload;
    if (m_Pipeline.GetCanvasWidth() > 0 && m_Pipeline.GetCanvasHeight() > 0) {
        payload.width = std::clamp(m_Pipeline.GetCanvasWidth(), 1, 4096);
        payload.height = std::clamp(m_Pipeline.GetCanvasHeight(), 1, 4096);
    }
    payload.rasterLayer.assign(
        static_cast<std::size_t>(payload.width) * static_cast<std::size_t>(payload.height),
        0.0f);

    if (EditorNodeGraph::Node* node = m_NodeGraph.AddCustomMaskNode(std::move(payload), graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
        SwitchToComplexNodeSubWindow(node->id);
    }
}

void EditorModule::AddImageToMaskNodeAt(EditorNodeGraph::ImageToMaskKind converterKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddImageToMaskNode(converterKind, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

bool EditorModule::CreateToneCurveSelectionMask(
    int toneCurveNodeId,
    float low,
    float high,
    float softness,
    const std::array<float, 4>& sampleRgba,
    float sampleLuma,
    float sampleU,
    float sampleV,
    float toneSimilarity,
    float colorSimilarity,
    float regionRadius,
    float regionFeather,
    float edgeSensitivity,
    float localCoherence,
    ToneCurveScopeMaskAction action) {
    EditorNodeGraph::Node* toneCurveNode = m_NodeGraph.FindNode(toneCurveNodeId);
    if (!toneCurveNode) {
        return false;
    }

    int maskOwnerNodeId = toneCurveNodeId;
    int sourceImageNodeId = -1;
    std::string sourceImageSocketId;
    const EditorNodeGraph::Link* maskInput = nullptr;
    if (toneCurveNode->kind == EditorNodeGraph::NodeKind::Layer &&
        toneCurveNode->layerType == LayerType::ToneCurve) {
        const EditorNodeGraph::Link* imageInput = m_NodeGraph.FindInputLink(toneCurveNodeId, EditorNodeGraph::kImageInputSocketId);
        if (!imageInput) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "Tone Curve needs an image input before a tone scope mask can be created.",
                "tone-curve-mask-create");
            return false;
        }
        sourceImageNodeId = imageInput->fromNodeId;
        sourceImageSocketId = imageInput->fromSocketId;
        maskInput = m_NodeGraph.FindInputLink(toneCurveNodeId, EditorNodeGraph::kMaskInputSocketId);
    } else if (toneCurveNode->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        if (!toneCurveNode->rawDevelop.integratedToneEnabled) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "Develop needs its finish stage enabled before creating a tone scope mask.",
                "tone-curve-mask-create");
            return false;
        }
        if (!m_NodeGraph.FindInputLink(toneCurveNodeId, EditorNodeGraph::kRawInputSocketId)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "Develop needs a RAW input before a finish scope mask can be created.",
                "tone-curve-mask-create");
            return false;
        }
        sourceImageNodeId = toneCurveNodeId;
        sourceImageSocketId = EditorNodeGraph::kPreFinishImageOutputSocketId;
        maskInput = m_NodeGraph.FindInputLink(toneCurveNodeId, EditorNodeGraph::kMaskInputSocketId);
    } else {
        return false;
    }

    EditorNodeGraph::Node* maskNode = nullptr;
    EditorNodeGraph::Node* combineNode = nullptr;
    const bool hadExistingMaskInput = maskInput != nullptr;
    const bool startNewScopedMask = action == ToneCurveScopeMaskAction::NewMask;
    const EditorNodeGraph::MaskCombineMode requestedCombineMode = ToGraphMaskCombineMode(action);
    bool reusedExistingToneScopeMask = false;
    if (maskInput) {
        combineNode = m_NodeGraph.FindNode(maskInput->fromNodeId);
        if (combineNode && combineNode->kind == EditorNodeGraph::NodeKind::MaskCombine) {
            const EditorNodeGraph::Link* inputA = m_NodeGraph.FindInputLink(combineNode->id, EditorNodeGraph::kMaskCombineInputASocketId);
            const EditorNodeGraph::Link* inputB = m_NodeGraph.FindInputLink(combineNode->id, EditorNodeGraph::kMaskCombineInputBSocketId);
            for (const EditorNodeGraph::Link* input : { inputA, inputB }) {
                if (!input) {
                    continue;
                }
                EditorNodeGraph::Node* candidate = m_NodeGraph.FindNode(input->fromNodeId);
                if (candidate &&
                    candidate->kind == EditorNodeGraph::NodeKind::ImageToMask &&
                    candidate->title == "Tone Scope Mask") {
                    maskNode = candidate;
                    reusedExistingToneScopeMask = true;
                    break;
                }
            }
        } else {
            maskNode = m_NodeGraph.FindNode(maskInput->fromNodeId);
            if (!maskNode || maskNode->kind != EditorNodeGraph::NodeKind::ImageToMask || maskNode->title != "Tone Scope Mask") {
                maskNode = nullptr;
            } else {
                reusedExistingToneScopeMask = true;
            }
        }
    } else {
        const EditorNodeGraph::Vec2 position{
            toneCurveNode->position.x - 250.0f,
            toneCurveNode->position.y + 135.0f
        };
        maskNode = m_NodeGraph.AddImageToMaskNode(EditorNodeGraph::ImageToMaskKind::Luminance, position);
        if (!maskNode) {
            return false;
        }
    }

    if (!maskNode) {
        const EditorNodeGraph::Vec2 position{
            toneCurveNode->position.x - 250.0f,
            toneCurveNode->position.y + 135.0f
        };
        maskNode = m_NodeGraph.AddImageToMaskNode(EditorNodeGraph::ImageToMaskKind::Luminance, position);
        if (!maskNode) {
            return false;
        }
        maskNode->title = "Tone Scope Mask";
    } else if (maskNode->title.empty()) {
        maskNode->title = "Tone Scope Mask";
    }

    const EditorNodeGraph::Link* maskImageInput = m_NodeGraph.FindInputLink(maskNode->id, EditorNodeGraph::kImageInputSocketId);
    if (!maskImageInput ||
        maskImageInput->fromNodeId != sourceImageNodeId ||
        maskImageInput->fromSocketId != sourceImageSocketId) {
        if (maskImageInput) {
            RemoveGraphLink(maskImageInput->fromNodeId, maskImageInput->fromSocketId, maskNode->id, EditorNodeGraph::kImageInputSocketId);
        }
        std::string errorMessage;
        if (!ConnectGraphSockets(
                sourceImageNodeId,
                sourceImageSocketId,
                maskNode->id,
                EditorNodeGraph::kImageInputSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty() ? "Failed to connect the tone scope mask to the finish-stage input." : errorMessage,
                "tone-curve-mask-create");
            return false;
        }
    }

    if (startNewScopedMask && maskInput) {
        RemoveGraphLink(maskInput->fromNodeId, maskInput->fromSocketId, maskOwnerNodeId, EditorNodeGraph::kMaskInputSocketId);
        combineNode = nullptr;
        maskInput = nullptr;
    }

    if (!maskInput) {
        std::string errorMessage;
        if (!ConnectGraphSockets(
                maskNode->id,
                EditorNodeGraph::kMaskOutputSocketId,
                maskOwnerNodeId,
                EditorNodeGraph::kMaskInputSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty() ? "Failed to connect the tone scope mask to the finish-stage target." : errorMessage,
                "tone-curve-mask-create");
            return false;
        }
    } else if (!combineNode && maskInput->fromNodeId != maskNode->id) {
        const EditorNodeGraph::Node* existingMaskNode = m_NodeGraph.FindNode(maskInput->fromNodeId);
        const EditorNodeGraph::Vec2 combinePosition{
            toneCurveNode->position.x - 125.0f,
            toneCurveNode->position.y + 140.0f
        };
        combineNode = m_NodeGraph.AddMaskCombineNode(requestedCombineMode, combinePosition);
        if (!combineNode) {
            return false;
        }
        combineNode->title = "Tone Scope Combine";

        RemoveGraphLink(maskInput->fromNodeId, maskInput->fromSocketId, maskOwnerNodeId, EditorNodeGraph::kMaskInputSocketId);

        std::string errorMessage;
        if (!ConnectGraphSockets(
                existingMaskNode ? existingMaskNode->id : maskInput->fromNodeId,
                maskInput->fromSocketId,
                combineNode->id,
                EditorNodeGraph::kMaskCombineInputASocketId,
                &errorMessage) ||
            !ConnectGraphSockets(
                maskNode->id,
                EditorNodeGraph::kMaskOutputSocketId,
                combineNode->id,
                EditorNodeGraph::kMaskCombineInputBSocketId,
                &errorMessage) ||
            !ConnectGraphSockets(
                combineNode->id,
                EditorNodeGraph::kMaskOutputSocketId,
                maskOwnerNodeId,
                EditorNodeGraph::kMaskInputSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty() ? "Failed to combine the existing mask with the new tone scope mask." : errorMessage,
                "tone-curve-mask-create");
            return false;
        }
    } else if (combineNode &&
               (m_NodeGraph.HasLink(maskNode->id, EditorNodeGraph::kMaskOutputSocketId, combineNode->id, EditorNodeGraph::kMaskCombineInputASocketId) ||
                m_NodeGraph.HasLink(maskNode->id, EditorNodeGraph::kMaskOutputSocketId, combineNode->id, EditorNodeGraph::kMaskCombineInputBSocketId))) {
        combineNode->maskCombineMode = requestedCombineMode;
    } else if (maskInput && maskInput->fromNodeId != maskNode->id) {
        const EditorNodeGraph::Vec2 combinePosition{
            toneCurveNode->position.x - 125.0f,
            toneCurveNode->position.y + 140.0f
        };
        EditorNodeGraph::Node* nestedCombine = m_NodeGraph.AddMaskCombineNode(requestedCombineMode, combinePosition);
        if (!nestedCombine) {
            return false;
        }
        nestedCombine->title = "Tone Scope Combine";

        RemoveGraphLink(maskInput->fromNodeId, maskInput->fromSocketId, maskOwnerNodeId, EditorNodeGraph::kMaskInputSocketId);

        std::string errorMessage;
        if (!ConnectGraphSockets(
                maskInput->fromNodeId,
                maskInput->fromSocketId,
                nestedCombine->id,
                EditorNodeGraph::kMaskCombineInputASocketId,
                &errorMessage) ||
            !ConnectGraphSockets(
                maskNode->id,
                EditorNodeGraph::kMaskOutputSocketId,
                nestedCombine->id,
                EditorNodeGraph::kMaskCombineInputBSocketId,
                &errorMessage) ||
            !ConnectGraphSockets(
                nestedCombine->id,
                EditorNodeGraph::kMaskOutputSocketId,
                maskOwnerNodeId,
                EditorNodeGraph::kMaskInputSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty() ? "Failed to refine the existing mask with a new tone scope component." : errorMessage,
                "tone-curve-mask-create");
            return false;
        }
        combineNode = nestedCombine;
    }

    const float clampedLow = std::clamp(std::min(low, high), 0.0f, 1.0f);
    const float clampedHigh = std::clamp(std::max(low, high), 0.0f, 1.0f);
    const float clampedSampleRgb[3] = {
        std::clamp(sampleRgba[0], 0.0f, 16.0f),
        std::clamp(sampleRgba[1], 0.0f, 16.0f),
        std::clamp(sampleRgba[2], 0.0f, 16.0f)
    };
    const float clampedSampleLuma = std::clamp(sampleLuma, 0.0f, 16.0f);

    maskNode->imageToMaskKind = EditorNodeGraph::ImageToMaskKind::SampledRange;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(*maskNode);
    maskNode->title = "Tone Scope Mask";
    maskNode->imageToMaskSettings.low = clampedLow;
    maskNode->imageToMaskSettings.high = std::max(clampedLow + 0.0001f, clampedHigh);
    maskNode->imageToMaskSettings.softness = std::clamp(softness, 0.0f, 0.5f);
    maskNode->imageToMaskSettings.invert = false;
    maskNode->imageToMaskSettings.sampleU = std::clamp(sampleU, 0.0f, 1.0f);
    maskNode->imageToMaskSettings.sampleV = std::clamp(sampleV, 0.0f, 1.0f);
    maskNode->imageToMaskSettings.toneSimilarity = std::clamp(toneSimilarity, 0.02f, 0.35f);
    maskNode->imageToMaskSettings.colorSimilarity = std::clamp(colorSimilarity, 0.02f, 0.50f);
    maskNode->imageToMaskSettings.regionRadius = std::clamp(regionRadius, 0.05f, 1.0f);
    maskNode->imageToMaskSettings.regionFeather = std::clamp(regionFeather, 0.0f, 1.0f);
    maskNode->imageToMaskSettings.edgeSensitivity = std::clamp(edgeSensitivity, 0.0f, 1.0f);
    maskNode->imageToMaskSettings.localCoherence = std::clamp(localCoherence, 0.0f, 1.0f);

    auto clearExtraSamples = [&](EditorNodeGraph::ImageToMaskSettings& settings) {
        for (int i = 0; i < 4; ++i) {
            settings.extraSampleRgb[i][0] = 0.5f;
            settings.extraSampleRgb[i][1] = 0.5f;
            settings.extraSampleRgb[i][2] = 0.5f;
            settings.extraSampleLuma[i] = 0.5f;
        }
    };
    auto resetPrimarySample = [&](EditorNodeGraph::ImageToMaskSettings& settings) {
        settings.sampleCount = 1;
        settings.sampleRgb[0] = clampedSampleRgb[0];
        settings.sampleRgb[1] = clampedSampleRgb[1];
        settings.sampleRgb[2] = clampedSampleRgb[2];
        settings.sampleLuma = clampedSampleLuma;
        clearExtraSamples(settings);
    };
    auto sampleMatches = [&](const EditorNodeGraph::ImageToMaskSettings& settings, int sampleIndex) {
        if (sampleIndex <= 0) {
            return std::abs(settings.sampleRgb[0] - clampedSampleRgb[0]) < 0.0005f &&
                std::abs(settings.sampleRgb[1] - clampedSampleRgb[1]) < 0.0005f &&
                std::abs(settings.sampleRgb[2] - clampedSampleRgb[2]) < 0.0005f &&
                std::abs(settings.sampleLuma - clampedSampleLuma) < 0.0005f;
        }
        const int extraIndex = sampleIndex - 1;
        return std::abs(settings.extraSampleRgb[extraIndex][0] - clampedSampleRgb[0]) < 0.0005f &&
            std::abs(settings.extraSampleRgb[extraIndex][1] - clampedSampleRgb[1]) < 0.0005f &&
            std::abs(settings.extraSampleRgb[extraIndex][2] - clampedSampleRgb[2]) < 0.0005f &&
            std::abs(settings.extraSampleLuma[extraIndex] - clampedSampleLuma) < 0.0005f;
    };

    bool appendedSample = false;
    bool duplicateSample = false;
    bool sampleCapacityReached = false;
    const bool allowSampleAppend = !startNewScopedMask && reusedExistingToneScopeMask;
    EditorNodeGraph::ImageToMaskSettings& imageToMaskSettings = maskNode->imageToMaskSettings;
    if (imageToMaskSettings.sampleCount < 1 || imageToMaskSettings.sampleCount > 5) {
        imageToMaskSettings.sampleCount = 1;
    }
    if (allowSampleAppend && imageToMaskSettings.sampleCount >= 1) {
        for (int i = 0; i < imageToMaskSettings.sampleCount; ++i) {
            if (sampleMatches(imageToMaskSettings, i)) {
                duplicateSample = true;
                break;
            }
        }
        if (!duplicateSample) {
            if (imageToMaskSettings.sampleCount < 5) {
                const int extraIndex = imageToMaskSettings.sampleCount - 1;
                imageToMaskSettings.extraSampleRgb[extraIndex][0] = clampedSampleRgb[0];
                imageToMaskSettings.extraSampleRgb[extraIndex][1] = clampedSampleRgb[1];
                imageToMaskSettings.extraSampleRgb[extraIndex][2] = clampedSampleRgb[2];
                imageToMaskSettings.extraSampleLuma[extraIndex] = clampedSampleLuma;
                imageToMaskSettings.sampleCount += 1;
                appendedSample = true;
            } else {
                sampleCapacityReached = true;
            }
        }
    } else {
        resetPrimarySample(imageToMaskSettings);
    }

    if (!allowSampleAppend && !appendedSample) {
        resetPrimarySample(imageToMaskSettings);
    } else if (imageToMaskSettings.sampleCount <= 0) {
        resetPrimarySample(imageToMaskSettings);
    }
    SelectGraphNode(
        toneCurveNode->kind == EditorNodeGraph::NodeKind::RawDevelop
            ? maskOwnerNodeId
            : maskNode->id);
    MarkRenderDirty(maskOwnerNodeId);
    if (sampleCapacityReached) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Tone scope mask already has five samples. Refine settings were updated, but no additional sample was added.",
            "tone-curve-mask-create");
    } else if (duplicateSample) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "That sampled tone is already present in the tone scope mask. Refine settings were updated.",
            "tone-curve-mask-create");
    } else {
        QueueUiNotification(
            UiNotificationSeverity::Success,
            appendedSample
                ? ("Tone scope mask refined with sample " + std::to_string(imageToMaskSettings.sampleCount) + " of 5.")
                : (startNewScopedMask
                    ? "Created a new scoped tone mask from the sampled tone and color range."
                    : (!hadExistingMaskInput
                        ? "Created a scoped tone mask from the sampled tone and color range."
                        : (action == ToneCurveScopeMaskAction::Add
                        ? "Added a sampled tone scope component to the existing mask."
                        : (action == ToneCurveScopeMaskAction::Subtract
                            ? "Subtracted a sampled tone scope component from the existing mask."
                            : "Intersected the existing mask with a sampled tone scope component.")))),
            "tone-curve-mask-create");
    }
    return true;
}

void EditorModule::AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind generatorKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddImageGeneratorNode(generatorKind, graphPosition)) {
        const int nodeId = node->id;
        SelectGraphNode(nodeId);
        if (GetConnectedOutputCount() == 0) {
            if (EditorNodeGraph::Node* outputNode = m_NodeGraph.AddOutputNode(
                EditorNodeGraph::Vec2{ graphPosition.x + 330.0f, graphPosition.y })) {
                const int outputNodeId = outputNode->id;
                std::string errorMessage;
                ConnectGraphNodes(nodeId, outputNodeId, &errorMessage);
                if (GetCompletedChainCount() == 1 && m_Pipeline.GetSourcePixelsRaw().empty()) {
                    EnterSingleOutputPreviewMode();
                }
            }
        }
        MarkRenderDirty();
    }
}

void EditorModule::AddMixNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMixNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddDataMathNodeAt(EditorNodeGraph::DataMathMode mode, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddDataMathNode(mode, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddPreviewNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddPreviewNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddChannelSplitNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddChannelSplitNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddChannelCombineNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddChannelCombineNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddOutputNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddOutputNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty();
    }
}

void EditorModule::AutoLayoutGraph() {
    m_NodeGraph.AutoLayout();
}

void EditorModule::DisconnectGraphOutput() {
    m_NodeGraph.DisconnectOutput();
    m_Pipeline.ClearOutput();
    MarkRenderDirty();
}

void EditorModule::RefreshGraphLayerMetadata() {
    m_NodeGraph.SyncLayerNodes(static_cast<int>(m_Layers.size()));

    for (int i = 0; i < static_cast<int>(m_Layers.size()); ++i) {
        EditorNodeGraph::Node* node = m_NodeGraph.FindNodeByLayerIndex(i);
        if (!node) {
            continue;
        }

        const nlohmann::json layerJson = m_Layers[i]->Serialize();
        const std::string typeId = layerJson.value("type", std::string());
        const LayerDescriptor* descriptor = LayerRegistry::FindDescriptorByTypeId(typeId);
        node->typeId = typeId;
        if (descriptor) {
            node->layerType = descriptor->type;
            node->title = descriptor->displayName;
        } else {
            node->title = m_Layers[i]->GetDefaultName();
        }
    }
}

