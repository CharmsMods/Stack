#include "EditorModule.h"

#include "Async/TaskSystem.h"
#include "Layers/ToneLayers.h"
#include "NodeGraph/EditorNodeGraphDefinitions.h"
#include "NodeGraph/EditorNodeGraphSerializer.h"
#include "Library/LibraryManager.h"
#include "Raw/LibRawRuntime.h"
#include "Raw/RawLoader.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include "App/settings/AppearanceTheme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <imgui.h>
#include <imgui_internal.h>

namespace {

namespace StackFormat = StackBinaryFormat;

constexpr double kSplitAutoAnimationDurationSeconds = 0.32;

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

std::string SanitizeProjectFileStem(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '_' || ch == '-') {
            result.push_back(static_cast<char>(ch));
        } else if (std::isspace(ch)) {
            result.push_back('_');
        }
    }

    while (!result.empty() && result.front() == '_') {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    return result.empty() ? std::string("project") : result;
}

void ResizeNearestRgba(
    const std::vector<unsigned char>& srcPixels,
    int srcW,
    int srcH,
    int dstW,
    int dstH,
    std::vector<unsigned char>& dstPixels) {
    dstPixels.assign(static_cast<size_t>(dstW * dstH * 4), 0);
    if (srcPixels.empty() || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) {
        return;
    }

    for (int y = 0; y < dstH; ++y) {
        const int srcY = std::clamp(static_cast<int>((static_cast<float>(y) / static_cast<float>(dstH)) * srcH), 0, srcH - 1);
        for (int x = 0; x < dstW; ++x) {
            const int srcX = std::clamp(static_cast<int>((static_cast<float>(x) / static_cast<float>(dstW)) * srcW), 0, srcW - 1);
            const size_t dstIndex = static_cast<size_t>((y * dstW + x) * 4);
            const size_t srcIndex = static_cast<size_t>((srcY * srcW + srcX) * 4);
            dstPixels[dstIndex + 0] = srcPixels[srcIndex + 0];
            dstPixels[dstIndex + 1] = srcPixels[srcIndex + 1];
            dstPixels[dstIndex + 2] = srcPixels[srcIndex + 2];
            dstPixels[dstIndex + 3] = srcPixels[srcIndex + 3];
        }
    }
}

std::vector<unsigned char> BuildThumbnailBytes(
    const std::vector<unsigned char>& rgbaPixels,
    int width,
    int height) {
    if (rgbaPixels.empty() || width <= 0 || height <= 0) {
        return {};
    }

    constexpr int kMaxEdge = 320;
    if (width <= kMaxEdge && height <= kMaxEdge) {
        return EncodePngBytes(rgbaPixels, width, height, 4);
    }

    const float scale = static_cast<float>(kMaxEdge) / static_cast<float>(std::max(width, height));
    const int thumbW = std::max(1, static_cast<int>(std::floor(static_cast<float>(width) * scale)));
    const int thumbH = std::max(1, static_cast<int>(std::floor(static_cast<float>(height) * scale)));
    std::vector<unsigned char> thumbPixels;
    ResizeNearestRgba(rgbaPixels, width, height, thumbW, thumbH, thumbPixels);
    return EncodePngBytes(thumbPixels, thumbW, thumbH, 4);
}

std::string BuildTimestampString() {
    std::time_t now = std::time(nullptr);
    std::tm timeInfo{};
#ifdef _WIN32
    localtime_s(&timeInfo, &now);
#else
    timeInfo = *std::localtime(&now);
#endif
    char buffer[64];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo) == 0) {
        return {};
    }
    return std::string(buffer);
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

std::string FileNameFromPath(const std::string& path);

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

} // namespace

namespace {

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

    auto mergeInput = [&](const std::string& socketId) {
        if (const EditorNodeGraph::Link* input = graph.FindInputLink(nodeId, socketId)) {
            state = MergeScenePathState(state, AnalyzeScenePathFromNode(graph, layers, input->fromNodeId, visiting));
        }
    };

    switch (node->kind) {
        case EditorNodeGraph::NodeKind::RawDevelopment:
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
        case EditorNodeGraph::NodeKind::Mfsr:
            state.sceneReferred = true;
            for (int inputIndex = 0; inputIndex < EditorNodeGraph::kMaxMfsrInputCount; ++inputIndex) {
                mergeInput(EditorNodeGraph::MfsrInputSocketId(inputIndex));
            }
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
            mergeInput(EditorNodeGraph::kMixInputASocketId);
            mergeInput(EditorNodeGraph::kMixInputBSocketId);
            break;
        case EditorNodeGraph::NodeKind::DataMath:
            for (int inputIndex = 0; inputIndex < EditorNodeGraph::kMaxDataMathInputCount; ++inputIndex) {
                mergeInput(EditorNodeGraph::DataMathInputSocketId(inputIndex));
            }
            mergeInput(EditorNodeGraph::kDataMathBaseInputSocketId);
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
        case EditorNodeGraph::NodeKind::Mix: {
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
        case EditorNodeGraph::NodeKind::DataMath: {
            const EditorNodeGraph::Link* selectedInput = nullptr;
            int connectedInputCount = 0;
            for (int inputIndex = 0; inputIndex < EditorNodeGraph::kMaxDataMathInputCount; ++inputIndex) {
                if (const EditorNodeGraph::Link* input = graph.FindInputLink(node.id, EditorNodeGraph::DataMathInputSocketId(inputIndex))) {
                    ++connectedInputCount;
                    selectedInput = input;
                    if (connectedInputCount > 1) {
                        selectedInput = nullptr;
                        break;
                    }
                }
            }
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

std::string FileNameFromPath(const std::string& path) {
    try {
        return std::filesystem::path(path).filename().string();
    } catch (...) {
        return path.empty() ? std::string("Image") : path;
    }
}

std::vector<unsigned char> BuildTransparentPixels(int width, int height) {
    if (width <= 0 || height <= 0) {
        return {};
    }
    return std::vector<unsigned char>(static_cast<size_t>(width * height * 4), 0);
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

bool IsMaskOutputNode(EditorNodeGraph::NodeKind kind) {
    return kind == EditorNodeGraph::NodeKind::MaskGenerator ||
        kind == EditorNodeGraph::NodeKind::MaskCombine ||
        kind == EditorNodeGraph::NodeKind::MaskUtility ||
        kind == EditorNodeGraph::NodeKind::CustomMask ||
        kind == EditorNodeGraph::NodeKind::ImageToMask ||
        kind == EditorNodeGraph::NodeKind::RawDetailAutoMask ||
        kind == EditorNodeGraph::NodeKind::RawDetailFusion;
}

bool IsImageOutputNode(EditorNodeGraph::NodeKind kind) {
    return kind == EditorNodeGraph::NodeKind::Image ||
        kind == EditorNodeGraph::NodeKind::RawDecode ||
        kind == EditorNodeGraph::NodeKind::RawDevelop ||
        kind == EditorNodeGraph::NodeKind::RawDetailFusion ||
        kind == EditorNodeGraph::NodeKind::HdrMerge ||
        kind == EditorNodeGraph::NodeKind::Mfsr ||
        kind == EditorNodeGraph::NodeKind::ImageGenerator ||
        kind == EditorNodeGraph::NodeKind::Layer ||
        kind == EditorNodeGraph::NodeKind::Mix ||
        kind == EditorNodeGraph::NodeKind::DataMath ||
        kind == EditorNodeGraph::NodeKind::Output;
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
    if (to->kind == EditorNodeGraph::NodeKind::Mfsr &&
        toSocketId == EditorNodeGraph::kMfsrReferenceInputSocketId) {
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
        (EditorNodeGraph::IsDataMathInputSocketId(toSocketId) ||
         toSocketId == EditorNodeGraph::kDataMathBaseInputSocketId)) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::ChannelSplit &&
        toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    return false;
}

float SmoothStep(float edge0, float edge1, float x) {
    if (std::abs(edge1 - edge0) < 0.00001f) {
        return x < edge0 ? 0.0f : 1.0f;
    }
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float MaskPreviewValue(const EditorNodeGraph::Node& node, float u, float v) {
    const EditorNodeGraph::MaskGeneratorSettings& settings = node.maskSettings;
    float value = settings.value;
    if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::LinearGradient) {
        constexpr float kPi = 3.14159265358979323846f;
        const float radians = settings.angle * kPi / 180.0f;
        const float dx = std::cos(radians);
        const float dy = std::sin(radians);
        value = ((u - 0.5f) * dx + (v - 0.5f) * dy) * settings.scale + 0.5f + settings.offset;
        value = std::clamp(value, 0.0f, 1.0f);
    } else if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::RadialGradient) {
        const float dx = u - settings.centerX;
        const float dy = v - settings.centerY;
        const float dist = std::sqrt(dx * dx + dy * dy);
        value = 1.0f - SmoothStep(std::max(0.0f, settings.radius - settings.feather), settings.radius + settings.feather, dist);
    }
    if (settings.invert) {
        value = 1.0f - value;
    }
    return std::clamp(value, 0.0f, 1.0f);
}

std::vector<unsigned char> GenerateMaskPreviewPixels(const EditorNodeGraph::Node& node, int& outW, int& outH) {
    outW = 192;
    outH = 128;
    std::vector<unsigned char> pixels(static_cast<size_t>(outW * outH * 4), 255);
    for (int y = 0; y < outH; ++y) {
        const float v = outH > 1 ? static_cast<float>(y) / static_cast<float>(outH - 1) : 0.0f;
        for (int x = 0; x < outW; ++x) {
            const float u = outW > 1 ? static_cast<float>(x) / static_cast<float>(outW - 1) : 0.0f;
            const unsigned char gray = static_cast<unsigned char>(std::round(MaskPreviewValue(node, u, v) * 255.0f));
            const size_t dst = static_cast<size_t>(y * outW + x) * 4;
            pixels[dst + 0] = gray;
            pixels[dst + 1] = gray;
            pixels[dst + 2] = gray;
            pixels[dst + 3] = 255;
        }
    }
    return pixels;
}

} // namespace

EditorModule::EditorModule() {}

EditorModule::~EditorModule() {
    Shutdown();
}

void EditorModule::MarkDirty() {
    m_Dirty = true;
    if (IsRawWorkspaceProjectActive()) {
        BumpRawWorkspaceProjectSaveRevision(
            m_RawWorkspace.workspaceRoot,
            m_ActiveRawWorkspaceSourceKey);
    }
}

void EditorModule::RequestWorkerShutdownForAppClose() {
    RequestRawWorkspaceProjectSaveWorkerDrain();
    m_RenderWorker.RequestStopForShutdown();
    m_NodeBrowserRenderWorker.RequestStopForShutdown();
    m_RenderPending = false;
}

bool EditorModule::IsWorkerShutdownReadyForAppClose() const {
    return !m_RenderPending &&
        IsRawWorkspaceProjectSaveWorkerIdle() &&
        !m_RenderWorker.HasPendingOrBusyForShutdown() &&
        !m_NodeBrowserRenderWorker.HasPendingOrBusyForShutdown();
}

void EditorModule::Shutdown() {
    if (m_ShutdownComplete) {
        return;
    }
    m_ShutdownComplete = true;
    CloseDetachedPreviewFullscreen();
    ShutdownRawWorkspaceProjectSaveWorker();
    FlushRawWorkspacePersistenceForShutdown();
    m_RenderWorker.Shutdown();
    m_NodeBrowserRenderWorker.Shutdown();
    m_RenderWorkerAvailable = false;
    m_NodeBrowserRenderWorkerAvailable = false;
    for (EditorRenderWorker::Result& result : m_DeferredRenderResults) {
        QueueViewportOutputTextureRelease(result.outputTexture);
        QueueViewportOutputTileSetRelease(result.outputTiles);
    }
    m_DeferredRenderResults.clear();
    ClearViewportOutputTiles();
    PumpViewportOutputTextureDeletes(true);
    PumpViewportOutputTileTextureDeletes(true);
    ClearCompositeSceneTextures();
    ClearRawWorkspaceThumbnailTextures(true);
}

bool EditorModule::AddGeneratedLutNodeFromPayload(EditorNodeGraph::LutPayload payload) {
    const auto nodeHasImageOutput = [](EditorNodeGraph::NodeKind kind) {
        switch (kind) {
            case EditorNodeGraph::NodeKind::Image:
            case EditorNodeGraph::NodeKind::RawDevelopment:
            case EditorNodeGraph::NodeKind::RawDecode:
            case EditorNodeGraph::NodeKind::RawDevelop:
            case EditorNodeGraph::NodeKind::RawDetailFusion:
            case EditorNodeGraph::NodeKind::HdrMerge:
            case EditorNodeGraph::NodeKind::Mfsr:
            case EditorNodeGraph::NodeKind::Lut:
            case EditorNodeGraph::NodeKind::Layer:
            case EditorNodeGraph::NodeKind::Mix:
            case EditorNodeGraph::NodeKind::DataMath:
            case EditorNodeGraph::NodeKind::ImageGenerator:
            case EditorNodeGraph::NodeKind::ChannelCombine:
                return true;
            default:
                return false;
        }
    };

    const EditorNodeGraph::Node* selected = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId());
    const EditorNodeGraph::Vec2 graphPosition = selected
        ? EditorNodeGraph::Vec2{ selected->position.x + 300.0f, selected->position.y }
        : EditorNodeGraph::Vec2{ 260.0f, 180.0f };
    const bool selectedHasImageOutput = selected && nodeHasImageOutput(selected->kind);
    const int upstreamNodeId = selectedHasImageOutput ? selected->id : -1;

    EditorNodeGraph::Node* lutNode = m_NodeGraph.AddLutNode(std::move(payload), graphPosition);
    if (!lutNode) {
        return false;
    }

    const int lutNodeId = lutNode->id;
    std::string errorMessage;
    const EditorNodeGraph::Vec2 maskPosition{
        graphPosition.x - 172.0f,
        graphPosition.y + 78.0f
    };
    EditorNodeGraph::Node* solidMaskNode = m_NodeGraph.AddMaskGeneratorNode(
        EditorNodeGraph::MaskGeneratorKind::Solid,
        maskPosition);
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
                "Generated LUT mask auto-connect failed: " + errorMessage,
                "generated-lut-mask-autoconnect");
            errorMessage.clear();
        }
    }

    if (upstreamNodeId > 0) {
        if (!ConnectGraphSockets(
                upstreamNodeId,
                EditorNodeGraph::kImageOutputSocketId,
                lutNodeId,
                EditorNodeGraph::kImageInputSocketId,
                &errorMessage) &&
            !errorMessage.empty()) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "Generated LUT auto-connect failed: " + errorMessage,
                "generated-lut-autoconnect");
        }
    }

    SelectGraphNode(lutNodeId);
    MarkRenderDirty(lutNodeId);
    SwitchToComplexNodeSubWindow(lutNodeId);
    return true;
}

EditorNodeGraph::Graph& EditorModule::GetNodeGraph() {
    return m_NodeGraph;
}

const EditorNodeGraph::Graph& EditorModule::GetNodeGraph() const {
    return m_NodeGraph;
}

bool EditorModule::IsGraphOutputConnected() const {
    return m_NodeGraph.IsOutputConnected();
}

void EditorModule::ClearCompositeSelection() {
    m_CompositeSelectedOutputNodeId = -1;
    m_NodeGraph.ClearSelection();
}

void EditorModule::Initialize(GLFWwindow* sharedWindow, StackAppearance::AppearanceManager* appearance) {
    m_ShutdownComplete = false;
    m_Appearance = appearance;
    std::vector<std::string> registryErrors;
    if (!LayerRegistry::ValidateRegistry(&registryErrors)) {
        for (const std::string& error : registryErrors) {
            fprintf(stderr, "LayerRegistry validation failed: %s\n", error.c_str());
        }
    }

    m_Pipeline.Initialize();
    m_CompositePreviewPipeline.Initialize();
    m_Sidebar.Initialize();
    m_Viewport.Initialize();
    m_Scopes.Initialize();
    m_RenderWorkerAvailable = sharedWindow && m_RenderWorker.Initialize(sharedWindow);
    m_NodeBrowserRenderWorkerAvailable = sharedWindow && m_NodeBrowserRenderWorker.Initialize(sharedWindow);
    (void)Raw::GetLibRawRuntimeStatus();

    m_Layers.clear();
    m_SelectedLayerIndex = -1;
    m_CanvasToolKind = CanvasToolKind::None;
    m_CanvasToolOwnerNodeId = -1;
    m_CanvasToolStatusText.clear();
    m_IsPickingColor = false;
    m_ColorPickerCallback = nullptr;
    m_NodeGraph.ResetFromLayers(0, false);
    ClearCompositeRuntimeState();
    MarkRenderDirty();

    m_Dirty = false;
    m_LastUserActionTime = 0.0;
    m_LastAutoSaveTime = -1.0;
    LoadRawWorkspaceAppState();
}
void EditorModule::EnterSingleOutputPreviewMode() {
    ClearCompositeTransientInteractionState();
    m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
    m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
    m_CompositeExportBoundsEditMode = false;
    m_Viewport.ResetSinglePreviewState();
    m_HoverFade = 0.0f;

    const int previewOutputNodeId = m_NodeGraph.ResolvePreviewOutputNodeId();
    if (previewOutputNodeId <= 0) {
        ClearViewportOutputTiles();
        m_Pipeline.ClearOutput();
        MarkRenderDirty();
        return;
    }

    RefreshCompletedChainCacheIfNeeded();
    const auto chainIt = std::find_if(
        m_CachedCompletedChains.begin(),
        m_CachedCompletedChains.end(),
        [previewOutputNodeId](const CachedCompositeChainState& chain) {
            return chain.info.outputNodeId == previewOutputNodeId;
        });
    if (chainIt == m_CachedCompletedChains.end()) {
        MarkRenderDirty();
        return;
    }

    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(chainIt->info.sourceNodeId);
    if (sourceNode &&
        sourceNode->kind == EditorNodeGraph::NodeKind::Image &&
        !sourceNode->image.pixels.empty() &&
        sourceNode->image.width > 0 &&
        sourceNode->image.height > 0) {
        LoadSourceFromPixels(
            sourceNode->image.pixels.data(),
            sourceNode->image.width,
            sourceNode->image.height,
            sourceNode->image.channels);
        return;
    }

    int outW = 0;
    int outH = 0;
    (void)GetCompositePixelsForOutputNode(previewOutputNodeId, outW, outH);
    if (outW > 0 && outH > 0) {
        const std::vector<unsigned char> transparentPixels = BuildTransparentPixels(outW, outH);
        LoadSourceFromPixels(transparentPixels.data(), outW, outH, 4);
    } else {
        MarkRenderDirty();
    }
}

void EditorModule::HandleViewportModeTransition(ViewportMode previousMode, ViewportMode currentMode) {
    if (previousMode == currentMode) {
        return;
    }
    if (previousMode == ViewportMode::CompositeCanvas && currentMode == ViewportMode::SingleOutputPreview) {
        EnterSingleOutputPreviewMode();
    } else if (previousMode == ViewportMode::SingleOutputPreview && currentMode == ViewportMode::CompositeCanvas) {
        m_HoverFade = 0.0f;
        ClearCompositeTransientInteractionState();
    }
    m_LastViewportMode = currentMode;
}

void EditorModule::BeginLibraryLoadReveal() {
    const bool wallpaperSurfaces = m_Appearance && m_Appearance->GetSeamlessSurfaceStylingEnabled();
    m_LibraryLoadRevealStartTime = -1.0;
    m_LibraryLoadRevealPendingFirstFrame = !wallpaperSurfaces;
    m_LibraryLoadRevealLayoutPending = true;
    m_LibraryLoadCanvasRevealAlpha = wallpaperSurfaces ? 1.0f : 0.0f;
    m_LibraryLoadGraphRevealAlpha = wallpaperSurfaces ? 1.0f : 0.0f;
    m_LibraryLoadToolbarRevealAlpha = wallpaperSurfaces ? 1.0f : 0.0f;
    m_NodeGraphFullscreen = false;
    m_TargetSubWindow = EditorSubWindow::NodeGraph;
    m_ActiveSubWindow = EditorSubWindow::NodeGraph;
    m_TargetComplexNodeId = -1;
    m_ActiveComplexNodeId = -1;
    m_LastSplitTargetSubWindow = EditorSubWindow::NodeGraph;
    m_LastSplitTargetComplexNodeId = -1;
    m_LeftPaneWidth = 520.0f;
    m_LastUserNodeGraphWidth = 520.0f;
    m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
    m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
    m_SplitAutoAnimating = false;
    m_DraggingSplitHandle = false;
    m_SplitHandlePressed = false;
    m_SplitHandleMoved = false;
    m_SubWindowTransitionAlpha = 1.0f;
    m_SubWindowTransitionFadingOut = false;
}


void EditorModule::RenderUI() {
    PumpNonRenderingWork(2.5);
    if (m_DetachedPreviewTogglePending) {
        m_DetachedPreviewTogglePending = false;
        ToggleDetachedPreviewFullscreen();
    }
    if (m_PendingAddImageNodePrompt) {
        const EditorNodeGraph::Vec2 graphPosition = m_PendingAddImageNodeGraphPosition;
        m_PendingAddImageNodePrompt = false;
        m_PendingAddImageNodeGraphPosition = {};
        PromptAddImageNodeAt(graphPosition);
    }

    // User Activity Tracking & Auto-Save Check
    {
        const ImGuiIO& io = ImGui::GetIO();
        bool userActive = false;
        // Check for any keypress
        for (ImGuiKey key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key = (ImGuiKey)(key + 1)) {
            if (ImGui::IsKeyPressed(key)) {
                userActive = true;
                break;
            }
        }
        // Check for mouse down
        if (!userActive) {
            for (int i = 0; i < 5; i++) {
                if (ImGui::IsMouseDown(i)) {
                    userActive = true;
                    break;
                }
            }
        }
        // Check for mouse movement or characters typed
        if (!userActive && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.InputQueueCharacters.Size > 0)) {
            userActive = true;
        }

        if (userActive) {
            m_LastUserActionTime = ImGui::GetTime();
        }

        if (m_Dirty &&
            !IsRawWorkspaceProjectActive() &&
            !m_CurrentProjectFileName.empty() &&
            !Async::IsBusy(LibraryManager::Get().GetSaveTaskState())) {
            double idleTime = ImGui::GetTime() - m_LastUserActionTime;
            if (idleTime >= 10.0 && idleTime < 30.0 && m_LastAutoSaveTime < m_LastUserActionTime) {
                RequestSaveCurrentProject(m_CurrentProjectName);
                m_LastAutoSaveTime = ImGui::GetTime();
            }
        }
    }

    // Safety fallback: if ExportSettings is active or targeted but we have less than 2 completed chains, fallback to NodeGraph
    if (GetCompletedChainCount() < 2) {
        if (m_ActiveSubWindow == EditorSubWindow::ExportSettings) {
            m_ActiveSubWindow = EditorSubWindow::NodeGraph;
        }
        if (m_TargetSubWindow == EditorSubWindow::ExportSettings) {
            m_TargetSubWindow = EditorSubWindow::NodeGraph;
        }
    }

    // Safety fallback: if ComplexNode is active or targeted but the node no longer exists in the graph, fallback to NodeGraph
    if (m_ActiveSubWindow == EditorSubWindow::ComplexNode) {
        if (!m_NodeGraph.FindNode(m_ActiveComplexNodeId)) {
            m_ActiveSubWindow = EditorSubWindow::NodeGraph;
        }
    }
    if (m_TargetSubWindow == EditorSubWindow::ComplexNode) {
        if (!m_NodeGraph.FindNode(m_TargetComplexNodeId)) {
            m_TargetSubWindow = EditorSubWindow::NodeGraph;
        }
    }

    // Update Sub-Window Transition
    if (m_ActiveSubWindow != m_TargetSubWindow || m_SubWindowTransitionFadingOut || m_SubWindowTransitionAlpha < 1.0f) {
        float dt = ImGui::GetIO().DeltaTime;
        if (m_SubWindowTransitionFadingOut) {
            m_SubWindowTransitionAlpha -= dt * 6.0f;
            if (m_SubWindowTransitionAlpha <= 0.0f) {
                m_SubWindowTransitionAlpha = 0.0f;
                m_ActiveSubWindow = m_TargetSubWindow;
                m_ActiveComplexNodeId = m_TargetComplexNodeId;
                m_SubWindowTransitionFadingOut = false; // Start fading in
            }
        } else {
            m_SubWindowTransitionAlpha += dt * 6.0f;
            if (m_SubWindowTransitionAlpha >= 1.0f) {
                m_SubWindowTransitionAlpha = 1.0f;
            }
        }
    }

    const bool wallpaperSurfaces = m_Appearance && m_Appearance->GetSeamlessSurfaceStylingEnabled();

    if (m_LibraryLoadRevealPendingFirstFrame) {
        m_LibraryLoadRevealPendingFirstFrame = false;
        m_LibraryLoadRevealStartTime = ImGui::GetTime();
    }

    if (m_LibraryLoadRevealStartTime >= 0.0) {
        if (wallpaperSurfaces) {
            m_LibraryLoadRevealStartTime = -1.0;
            m_LibraryLoadRevealPendingFirstFrame = false;
            m_LibraryLoadCanvasRevealAlpha = 1.0f;
            m_LibraryLoadGraphRevealAlpha = 1.0f;
            m_LibraryLoadToolbarRevealAlpha = 1.0f;
        } else {
            const double elapsed = ImGui::GetTime() - m_LibraryLoadRevealStartTime;
            auto easeOutCubic = [](float value) {
                value = std::clamp(value, 0.0f, 1.0f);
                return 1.0f - std::pow(1.0f - value, 3.0f);
            };
            m_LibraryLoadCanvasRevealAlpha = easeOutCubic(static_cast<float>(elapsed / 0.78));
            m_LibraryLoadGraphRevealAlpha = easeOutCubic(static_cast<float>((elapsed - 0.58) / 0.62));
            m_LibraryLoadToolbarRevealAlpha = easeOutCubic(static_cast<float>((elapsed - 1.22) / 0.36));
            if (elapsed >= 1.90) {
                m_LibraryLoadRevealStartTime = -1.0;
                m_LibraryLoadRevealPendingFirstFrame = false;
                m_LibraryLoadRevealLayoutPending = false;
                m_LibraryLoadCanvasRevealAlpha = 1.0f;
                m_LibraryLoadGraphRevealAlpha = 1.0f;
                m_LibraryLoadToolbarRevealAlpha = 1.0f;
            }
        }
    } else {
        m_LibraryLoadCanvasRevealAlpha = 1.0f;
        m_LibraryLoadGraphRevealAlpha = 1.0f;
        m_LibraryLoadToolbarRevealAlpha = 1.0f;
    }
    const float graphPaneRevealAlpha = wallpaperSurfaces ? 1.0f : m_LibraryLoadGraphRevealAlpha;
    const float canvasPaneRevealAlpha = wallpaperSurfaces ? 1.0f : m_LibraryLoadCanvasRevealAlpha;
    const float toolbarRevealAlpha = wallpaperSurfaces ? 1.0f : m_LibraryLoadToolbarRevealAlpha;

    const bool hotkeyOnePressed = CanConsumeEditorCommandKeys() && ImGui::IsKeyPressed(ImGuiKey_1, false);
    const bool hotkeyTwoPressed = CanConsumeEditorCommandKeys() && ImGui::IsKeyPressed(ImGuiKey_2, false);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, wallpaperSurfaces ? ImVec4(0.0f, 0.0f, 0.0f, 0.0f) : GetWorkspaceBaseColor());
    ImGui::BeginChild("StackEditorWorkspace", ImVec2(0, 0), false, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    const ImVec2 workspacePos = ImGui::GetCursorScreenPos();
    const ImVec2 workspaceSize = ImGui::GetContentRegionAvail();
    const ImVec4 workspaceColor = GetWorkspaceBaseColor();
    const StackAppearance::RuntimeSurfacePalette surfacePalette =
        m_Appearance ? m_Appearance->GetRuntimeSurfacePalette() : StackAppearance::RuntimeSurfacePalette{};
    const ImU32 workspaceColorU32 = ImGui::ColorConvertFloat4ToU32(workspaceColor);
    const ViewportMode viewportMode = GetViewportMode();
    HandleViewportModeTransition(m_LastViewportMode, viewportMode);
    const bool compositeViewportMode = viewportMode == ViewportMode::CompositeCanvas;
    const bool detachedPreviewActive = m_DetachedPreviewLayoutDetached;
    EnsureCompositeSceneState(m_LastCompositeCanvasSize);
    if (!wallpaperSurfaces) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            workspacePos,
            ImVec2(workspacePos.x + workspaceSize.x, workspacePos.y + workspaceSize.y),
            workspaceColorU32);
    }
    const float splitGap = 32.0f;
    const float minLeftWidth = 260.0f;
    const float minRightWidth = 420.0f;
    const float maxLeftWidth = std::max(minLeftWidth, workspaceSize.x - minRightWidth - splitGap);

    if (m_LibraryLoadRevealLayoutPending) {
        int imgW = m_Pipeline.GetCanvasWidth();
        int imgH = m_Pipeline.GetCanvasHeight();
        float targetLeftPaneWidth = std::clamp(520.0f, minLeftWidth, maxLeftWidth);
        if (imgW > 0 && imgH > 0) {
            const float paddingY = 32.0f;
            const float paddingX = 36.0f;
            const float availY = std::max(100.0f, workspaceSize.y - paddingY);
            const float imageAspect = static_cast<float>(imgW) / std::max(1.0f, static_cast<float>(imgH));
            const float displayWidth = availY * imageAspect;
            const float optimalRightWidth = displayWidth + paddingX;
            const float maxRightWidth = std::max(minRightWidth, workspaceSize.x - minLeftWidth - splitGap);
            const float constrainedRightWidth = std::clamp(optimalRightWidth, minRightWidth, maxRightWidth);
            targetLeftPaneWidth = workspaceSize.x - splitGap - constrainedRightWidth;
        }

        targetLeftPaneWidth = std::clamp(targetLeftPaneWidth, minLeftWidth, maxLeftWidth);
        m_LeftPaneWidth = targetLeftPaneWidth;
        m_LastUserNodeGraphWidth = targetLeftPaneWidth;
        m_SplitAutoAnimFrom = targetLeftPaneWidth;
        m_SplitAutoAnimTo = targetLeftPaneWidth;
        m_SplitAutoAnimating = false;
        m_LibraryLoadRevealLayoutPending = false;
    }

    auto startSplitAutoAnimation = [&](const float targetWidth, const CompositeEdgeSnapMode snapMode = CompositeEdgeSnapMode::None) {
        m_SplitAutoAnimFrom = m_LeftPaneWidth;
        m_SplitAutoAnimTo = std::clamp(targetWidth, 0.0f, workspaceSize.x);
        m_SplitAutoAnimSnapMode = snapMode;
        m_SplitAutoAnimStartTime = ImGui::GetTime();
        m_SplitAutoAnimating = true;
    };

    if (hotkeyOnePressed) {
        const bool graphSelected = m_TargetSubWindow == EditorSubWindow::NodeGraph &&
            (m_ActiveSubWindow == EditorSubWindow::NodeGraph || !m_SubWindowTransitionFadingOut);
        if (!graphSelected || m_ActiveSubWindow != EditorSubWindow::NodeGraph) {
            m_NodeGraphFullscreen = false;
            SwitchToSubWindow(EditorSubWindow::NodeGraph);
        } else if (m_NodeGraphFullscreen || m_LeftPaneWidth >= workspaceSize.x - 2.0f) {
            m_NodeGraphFullscreen = false;
            const float restoreWidth = (m_LastUserNodeGraphWidth > 0.0f)
                ? std::clamp(m_LastUserNodeGraphWidth, minLeftWidth, maxLeftWidth)
                : std::clamp(workspaceSize.x * (2.0f / 3.0f), minLeftWidth, maxLeftWidth);
            startSplitAutoAnimation(restoreWidth);
        } else {
            if (m_LeftPaneWidth > 0.0f && m_LeftPaneWidth < workspaceSize.x - 2.0f) {
                m_LastUserNodeGraphWidth = std::clamp(m_LeftPaneWidth, minLeftWidth, maxLeftWidth);
            }
            m_NodeGraphFullscreen = true;
            startSplitAutoAnimation(workspaceSize.x);
        }
    }
    if (hotkeyTwoPressed && GetCompletedChainCount() >= 2) {
        m_NodeGraphFullscreen = false;
        m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
        m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
        SwitchToSubWindow(EditorSubWindow::NodeGraph);
        const float restoreWidth = (m_LastUserNodeGraphWidth > 0.0f)
            ? std::clamp(m_LastUserNodeGraphWidth, minLeftWidth, maxLeftWidth)
            : std::clamp(workspaceSize.x * (2.0f / 3.0f), minLeftWidth, maxLeftWidth);
        startSplitAutoAnimation(restoreWidth);
    }
    // Transition detection for split auto-animation to optimal sizes
    if (m_TargetSubWindow != m_LastSplitTargetSubWindow || m_TargetComplexNodeId != m_LastSplitTargetComplexNodeId) {
        m_LastSplitTargetSubWindow = m_TargetSubWindow;
        m_LastSplitTargetComplexNodeId = m_TargetComplexNodeId;

        float targetWidth = m_LeftPaneWidth;
        if (m_TargetSubWindow == EditorSubWindow::NodeGraph) {
            if (m_NodeGraphFullscreen) {
                targetWidth = workspaceSize.x;
            } else {
                if (m_LastUserNodeGraphWidth <= 0.0f) {
                    m_LastUserNodeGraphWidth = std::clamp(workspaceSize.x * (2.0f / 3.0f), minLeftWidth, maxLeftWidth);
                }
                targetWidth = m_LastUserNodeGraphWidth;
            }
        } else if (m_TargetSubWindow == EditorSubWindow::ExportSettings) {
            targetWidth = 360.0f;
        } else if (m_TargetSubWindow == EditorSubWindow::Presets) {
            targetWidth = 560.0f;
        } else if (m_TargetSubWindow == EditorSubWindow::ComplexNode) {
            const EditorNodeGraph::Node* targetComplexNode =
                m_TargetComplexNodeId > 0 ? m_NodeGraph.FindNode(m_TargetComplexNodeId) : nullptr;
            targetWidth = targetComplexNode && targetComplexNode->kind == EditorNodeGraph::NodeKind::CustomMask
                ? (workspaceSize.x - splitGap) * 0.5f
                : 470.0f;
        }

        if (!(m_TargetSubWindow == EditorSubWindow::NodeGraph && m_NodeGraphFullscreen)) {
            targetWidth = std::clamp(targetWidth, minLeftWidth, maxLeftWidth);
        }

        m_SplitAutoAnimFrom = m_LeftPaneWidth;
        m_SplitAutoAnimTo = targetWidth;
        m_SplitAutoAnimStartTime = ImGui::GetTime();
        m_SplitAutoAnimating = true;
        if (compositeViewportMode) {
            m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
            m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
        }
    }

    if (m_CompositeEdgeSnapMode == CompositeEdgeSnapMode::GraphOnly && !m_DraggingSplitHandle && !m_SplitAutoAnimating) {
        m_LeftPaneWidth = workspaceSize.x;
    } else if (m_CompositeEdgeSnapMode == CompositeEdgeSnapMode::ViewportOnly && !m_DraggingSplitHandle && !m_SplitAutoAnimating) {
        m_LeftPaneWidth = 0.0f;
    } else {
        if (m_DraggingSplitHandle || m_SplitAutoAnimating) {
            m_LeftPaneWidth = std::clamp(m_LeftPaneWidth, 0.0f, workspaceSize.x);
        } else {
            if (m_NodeGraphFullscreen && !compositeViewportMode) {
                m_LeftPaneWidth = workspaceSize.x;
            } else if (m_LeftPaneWidth <= 0.0f && !compositeViewportMode) {
                m_LeftPaneWidth = std::clamp(workspaceSize.x * (2.0f / 3.0f), minLeftWidth, maxLeftWidth);
            } else {
                m_LeftPaneWidth = std::clamp(m_LeftPaneWidth, minLeftWidth, maxLeftWidth);
            }
        }
    }

    if (CanConsumeEditorCommandKeys() && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        m_SpacebarPressTime = ImGui::GetTime();
        m_SpacebarHeld = true;
    }

    if (CanConsumeEditorCommandKeys() &&
        ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
        TogglePresetsSubWindow();
    }

    if (CanConsumeEditorCommandKeys() &&
        ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        RequestSaveCurrentProject(m_CurrentProjectName.empty() ? "Untitled Project" : m_CurrentProjectName);
    }

    if (m_SpacebarHeld) {
        if (!ImGui::IsKeyDown(ImGuiKey_Space)) {
            const float holdTime = static_cast<float>(ImGui::GetTime() - m_SpacebarPressTime);
            const float paneHeight = std::max(0.0f, workspaceSize.y);
            if (holdTime >= 0.4f) {
                HandleSpacebarLongPress(workspaceSize.x, paneHeight, minLeftWidth, maxLeftWidth, splitGap);
            } else {
                HandleSpacebarPress(workspaceSize.x, paneHeight, minLeftWidth, maxLeftWidth, splitGap);
            }
            m_SpacebarHeld = false;
        }
    }

    const float effectiveSplitGap = detachedPreviewActive
        ? 0.0f
        : ((compositeViewportMode || m_CompositeEdgeSnapMode != CompositeEdgeSnapMode::None || m_DraggingSplitHandle || m_SplitAutoAnimating)
            ? splitGap * std::clamp(
                std::min(m_LeftPaneWidth, std::max(0.0f, workspaceSize.x - m_LeftPaneWidth)) / std::max(1.0f, splitGap),
                0.0f,
                1.0f)
            : splitGap);

    const float handleRadius = 7.0f;
    const ImVec2 handleCenter(
        std::clamp(
            workspacePos.x + m_LeftPaneWidth + (effectiveSplitGap * 0.5f),
            workspacePos.x + handleRadius + 2.0f,
            workspacePos.x + workspaceSize.x - handleRadius - 2.0f),
        workspacePos.y + 14.0f);
    bool handleHovered = false;
    if (m_SplitAutoAnimating) {
        const float t = static_cast<float>(std::clamp(
            (ImGui::GetTime() - m_SplitAutoAnimStartTime) / kSplitAutoAnimationDurationSeconds,
            0.0,
            1.0));
        const float eased = 1.0f - std::pow(1.0f - t, 3.0f);
        m_LeftPaneWidth = m_SplitAutoAnimFrom + (m_SplitAutoAnimTo - m_SplitAutoAnimFrom) * eased;
        if (t >= 1.0f) {
            m_SplitAutoAnimating = false;
            m_CompositeEdgeSnapMode = m_SplitAutoAnimSnapMode;
        }
    }

    ImDrawList* rootDrawList = ImGui::GetForegroundDrawList();
    if (!detachedPreviewActive) {
        const ImU32 handleFill = wallpaperSurfaces
            ? ImGui::ColorConvertFloat4ToU32(
                (m_DraggingSplitHandle || m_SplitHandlePressed)
                    ? surfacePalette.controlSurfaceActive
                    : surfacePalette.controlSurface)
            : ((m_DraggingSplitHandle || m_SplitHandlePressed)
                ? IM_COL32(92, 178, 255, 230)
                : (handleHovered ? IM_COL32(72, 148, 214, 214) : IM_COL32(52, 92, 120, 188)));
        if (graphPaneRevealAlpha > 0.01f) {
            ImVec4 handleColor = ImGui::ColorConvertU32ToFloat4(handleFill);
            handleColor.w *= graphPaneRevealAlpha;
            rootDrawList->AddCircleFilled(handleCenter, handleRadius, ImGui::ColorConvertFloat4ToU32(handleColor), 32);
        }
    }

    const float paneHeight = std::max(0.0f, workspaceSize.y);
    const float visibleLeftPaneWidth = detachedPreviewActive ? workspaceSize.x : m_LeftPaneWidth;
    const float rightWidth = detachedPreviewActive
        ? 0.0f
        : std::max(0.0f, workspaceSize.x - m_LeftPaneWidth - effectiveSplitGap);

    // Update Left Panel Hover & Animation State
    const bool isGraphDrawerOpen = m_Sidebar.GetNodeGraphUI().HasDrawerOpen();
    if (m_ActiveSubWindow == EditorSubWindow::NodeGraph && visibleLeftPaneWidth > 1.0f) {
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        bool hoveringPanelOrTab = false;

        if (!isGraphDrawerOpen) {
            if (!m_LeftPanelExpanded) {
                // Hover trigger along the entire left wall/edge of the window
                if (mousePos.x >= workspacePos.x && mousePos.x <= workspacePos.x + 15.0f &&
                    mousePos.y >= workspacePos.y && mousePos.y <= workspacePos.y + paneHeight) {
                    hoveringPanelOrTab = true;
                }
            } else {
                if (mousePos.x >= workspacePos.x && mousePos.x <= workspacePos.x + m_LeftPanelWidthAnim + 15.0f &&
                    mousePos.y >= workspacePos.y && mousePos.y <= workspacePos.y + paneHeight) {
                    hoveringPanelOrTab = true;
                }
            }

            if (ImGui::IsDragDropActive()) {
                hoveringPanelOrTab = true;
            }
        }

        m_LeftPanelExpanded = hoveringPanelOrTab;
    } else {
        m_LeftPanelExpanded = false;
    }

    float leftPanelTargetWidth = m_LeftPanelExpanded ? 220.0f : 0.0f;
    float animDt = ImGui::GetIO().DeltaTime;
    m_LeftPanelWidthAnim += (leftPanelTargetWidth - m_LeftPanelWidthAnim) * animDt * 10.0f;
    if (std::abs(m_LeftPanelWidthAnim - leftPanelTargetWidth) < 0.1f) {
        m_LeftPanelWidthAnim = leftPanelTargetWidth;
    }

    const float nodeBrowserMaxWidth = std::clamp(workspaceSize.x * 0.32f, 480.0f, 720.0f);
    float nodesPanelTargetWidth = isGraphDrawerOpen ? std::min(nodeBrowserMaxWidth, workspaceSize.x) : 0.0f;
    m_NodesPanelWidthAnim += (nodesPanelTargetWidth - m_NodesPanelWidthAnim) * animDt * 10.0f;
    if (std::abs(m_NodesPanelWidthAnim - nodesPanelTargetWidth) < 0.1f) {
        m_NodesPanelWidthAnim = nodesPanelTargetWidth;
    }

    if (visibleLeftPaneWidth > 1.0f) {
        ImGui::SetCursorScreenPos(workspacePos);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, graphPaneRevealAlpha);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, wallpaperSurfaces ? ImVec4(0.0f, 0.0f, 0.0f, 0.0f) : workspaceColor);
        ImGui::BeginChild("EditorGraphPane", ImVec2(visibleLeftPaneWidth, paneHeight), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_SubWindowTransitionAlpha);
        m_Sidebar.Render(this);
        ImGui::PopStyleVar();

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, toolbarRevealAlpha);
        RenderFloatingToolbar();
        ImGui::PopStyleVar();

        // Canvas Stack Slide-out Drawer Overlay
        if (m_ActiveSubWindow == EditorSubWindow::NodeGraph) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            // 1. Sliding panel background & content
            if (m_LeftPanelWidthAnim > 0.1f) {
                ImVec2 panelMin = workspacePos;

                // Redesigned premium feathered blend background
                float gradientWidth = std::min(60.0f, m_LeftPanelWidthAnim);
                float solidWidth = m_LeftPanelWidthAnim - gradientWidth;
                auto colorWithAlpha = [](ImVec4 color, float alphaScale) {
                    color.w *= std::clamp(alphaScale, 0.0f, 1.0f);
                    return ImGui::ColorConvertFloat4ToU32(color);
                };

                const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                const ImVec4 passiveTextColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                const ImVec4 accentTextColor = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
                const ImVec4 hoveredHeaderColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
                const ImVec4 activeHeaderColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);

                ImVec4 colBgOpaqueVec = workspaceColor;
                ImU32 colBgOpaque = 0;
                ImU32 colBgTrans = 0;
                if (wallpaperSurfaces) {
                    colBgOpaqueVec = surfacePalette.drawerSurface;
                    colBgOpaque = ImGui::ColorConvertFloat4ToU32(colBgOpaqueVec);
                    colBgTrans = ImGui::ColorConvertFloat4ToU32(surfacePalette.drawerSurfaceTransparent);
                } else {
                    const float luminance = 0.2126f * workspaceColor.x + 0.7152f * workspaceColor.y + 0.0722f * workspaceColor.z;
                    const bool isLightBg = luminance >= 0.5f;
                    colBgOpaqueVec.w = isLightBg ? 0.94f : 0.92f;
                    colBgOpaque = ImGui::ColorConvertFloat4ToU32(colBgOpaqueVec);
                    ImVec4 colBgTransVec = workspaceColor;
                    colBgTransVec.w = 0.0f;
                    colBgTrans = ImGui::ColorConvertFloat4ToU32(colBgTransVec);
                }

                const ImU32 colTitleText = colorWithAlpha(textColor, 1.0f);
                const ImU32 colPassiveText = colorWithAlpha(passiveTextColor, 0.96f);
                const ImU32 colActiveText = colorWithAlpha(accentTextColor, 1.0f);
                const ImU32 colNormalText = colorWithAlpha(textColor, 0.92f);
                const ImU32 colHoveredHeader = colorWithAlpha(hoveredHeaderColor, 1.0f);
                const ImU32 colActiveHeader = colorWithAlpha(activeHeaderColor, 1.0f);

                if (wallpaperSurfaces) {
                    drawList->AddRectFilled(
                        panelMin,
                        ImVec2(workspacePos.x + m_LeftPanelWidthAnim, workspacePos.y + paneHeight),
                        colBgOpaque);
                } else {
                    // Solid part
                    if (solidWidth > 0.0f) {
                        drawList->AddRectFilled(panelMin, ImVec2(workspacePos.x + solidWidth, workspacePos.y + paneHeight), colBgOpaque);
                    }
                    // Gradient feathered blend part
                    drawList->AddRectFilledMultiColor(
                        ImVec2(workspacePos.x + solidWidth, workspacePos.y),
                        ImVec2(workspacePos.x + m_LeftPanelWidthAnim, workspacePos.y + paneHeight),
                        colBgOpaque, colBgTrans, colBgTrans, colBgOpaque
                    );
                }

                // Content area
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_SubWindowTransitionAlpha);

                float contentWidth = m_LeftPanelWidthAnim - 40.0f; // breathing room on right for the feathered edge
                if (contentWidth > 1.0f) {
                    ImGui::SetCursorScreenPos(ImVec2(workspacePos.x + 16.0f, workspacePos.y + 28.0f));
                    if (wallpaperSurfaces) {
                        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
                    }
                    ImGui::BeginChild("CanvasStackDrawer", ImVec2(contentWidth, paneHeight - 56.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav);

                    ImGui::PushStyleColor(ImGuiCol_Text, colTitleText);
                    ImGui::TextUnformatted("CANVAS STACK");
                    ImGui::PopStyleColor();

                    ImGui::Dummy(ImVec2(0.0f, 10.0f)); // Spacing instead of solid separator line

                    const std::vector<int>& zOrder = GetCompositeZOrder();
                    if (zOrder.empty()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, colPassiveText);
                        ImGui::TextWrapped("Add at least two completed chains to enable the canvas stack.");
                        ImGui::PopStyleColor();
                    } else {
                        // Keyboard Z-order Reordering via Up/Down Arrow keys
                        const bool drawerFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                        const bool drawerHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
                        int selectedId = GetCompositeSelectedOutputNodeId();
                        if ((drawerFocused || drawerHovered) && selectedId > 0 && !ImGui::GetIO().WantTextInput) {
                            int selectedIdx = -1;
                            for (int idx = 0; idx < static_cast<int>(zOrder.size()); ++idx) {
                                if (zOrder[idx] == selectedId) {
                                    selectedIdx = idx;
                                    break;
                                }
                            }
                            if (selectedIdx != -1) {
                                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                                    if (selectedIdx < static_cast<int>(zOrder.size()) - 1) {
                                        MoveCompositeOutputToIndex(selectedId, selectedIdx + 1);
                                        MarkDirty();
                                    }
                                } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                                    if (selectedIdx > 0) {
                                        MoveCompositeOutputToIndex(selectedId, selectedIdx - 1);
                                        MarkDirty();
                                    }
                                }
                            }
                        }

                        ImGui::PushStyleColor(ImGuiCol_Text, colPassiveText);
                        ImGui::TextUnformatted("Drag to reorder:");
                        ImGui::PopStyleColor();
                        ImGui::Dummy(ImVec2(0.0f, 8.0f));

                        // Iterate in REVERSE order (top is front, bottom is back)
                        for (int i = (int)zOrder.size() - 1; i >= 0; --i) {
                            int outputNodeId = zOrder[i];
                            const CompositeSceneItem* item = FindCompositeSceneItem(outputNodeId);
                            std::string label = item && !item->label.empty() ? item->label : ("Output " + std::to_string(outputNodeId));

                            // Truncate if too long
                            std::string displayLabel = label;
                            if (displayLabel.size() > 18) {
                                displayLabel = displayLabel.substr(0, 15) + "...";
                            }

                            char itemID[128];
                            snprintf(itemID, sizeof(itemID), "%s##DrawerZItem_%d", displayLabel.c_str(), outputNodeId);

                            bool selected = GetCompositeSelectedOutputNodeId() == outputNodeId;

                            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                            // Completely borderless and backgroundless selectables
                            ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, colHoveredHeader);
                            ImGui::PushStyleColor(ImGuiCol_HeaderActive, colActiveHeader);

                            if (selected) {
                                ImGui::PushStyleColor(ImGuiCol_Text, colActiveText); // Active text glow
                            } else {
                                ImGui::PushStyleColor(ImGuiCol_Text, colNormalText); // Floating passive text
                            }

                            if (ImGui::Selectable(itemID, selected, 0, ImVec2(contentWidth, 26.0f))) {
                                SetCompositeSelectedOutputNodeId(outputNodeId);
                            }

                            ImGui::PopStyleColor(4);
                            ImGui::PopStyleVar();

                            // Drag and Drop Source
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                                ImGui::SetDragDropPayload("CompositeZOrderItem", &outputNodeId, sizeof(outputNodeId));
                                ImGui::Text("Moving %s", label.c_str());
                                ImGui::EndDragDropSource();
                            }

                            // Drag and Drop Target
                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CompositeZOrderItem")) {
                                    int draggedId = *static_cast<const int*>(payload->Data);
                                    MoveCompositeOutputZOrder(draggedId, outputNodeId);
                                }
                                ImGui::EndDragDropTarget();
                            }

                            ImGui::Dummy(ImVec2(0.0f, 4.0f));
                        }
                    }

                    ImGui::EndChild();
                    if (wallpaperSurfaces) {
                        ImGui::PopStyleColor();
                    }
                }

                ImGui::PopStyleVar();
            }
        }

        ImGui::EndChild();
        const ImVec2 graphPaneMin = ImGui::GetItemRectMin();
        const ImVec2 graphPaneMax = ImGui::GetItemRectMax();
        if (!wallpaperSurfaces && graphPaneRevealAlpha < 0.999f) {
            ImVec4 maskColor = workspaceColor;
            maskColor.w = std::clamp(1.0f - graphPaneRevealAlpha, 0.0f, 1.0f);
            ImGui::GetForegroundDrawList()->AddRectFilled(
                graphPaneMin,
                graphPaneMax,
                ImGui::ColorConvertFloat4ToU32(maskColor));
        }
        if (m_ActiveSubWindow == EditorSubWindow::NodeGraph && m_ShowGraphPerformancePopup) {
            RenderGraphPerformancePopup(graphPaneMin, graphPaneMax);
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    const float revealedRightWidth = rightWidth * canvasPaneRevealAlpha;
    ImVec2 viewportPaneMin = workspacePos;
    ImVec2 viewportPaneMax = workspacePos;
    bool viewportPaneVisible = false;
    if (revealedRightWidth > 1.0f) {
        const float viewportX = workspacePos.x + workspaceSize.x - revealedRightWidth;
        ImGui::SetCursorScreenPos(ImVec2(viewportX, workspacePos.y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, canvasPaneRevealAlpha);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, wallpaperSurfaces ? ImVec4(0.0f, 0.0f, 0.0f, 0.0f) : workspaceColor);
        ImGui::BeginChild("EditorViewportPane", ImVec2(revealedRightWidth, paneHeight), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
        if (m_ActiveSubWindow == EditorSubWindow::Presets) {
            m_Sidebar.GetNodeGraphUI().RenderPresetPreviewPane(this, ImGui::GetContentRegionAvail());
        } else {
            m_Viewport.Render(this, canvasPaneRevealAlpha, EditorViewport::HostMode::DockedPane);
        }
        ImGui::EndChild();
        viewportPaneMin = ImGui::GetItemRectMin();
        viewportPaneMax = ImGui::GetItemRectMax();
        viewportPaneVisible = true;
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    ImVec2 handleOverlayPos(handleCenter.x - 14.0f, workspacePos.y);
    ImVec2 handleOverlaySize(32.0f, paneHeight);
    if (m_CompositeEdgeSnapMode == CompositeEdgeSnapMode::ViewportOnly || m_LeftPaneWidth <= 2.0f) {
        handleOverlayPos = ImVec2(workspacePos.x, workspacePos.y);
        handleOverlaySize = ImVec2(32.0f, paneHeight);
    } else if (m_CompositeEdgeSnapMode == CompositeEdgeSnapMode::GraphOnly || m_LeftPaneWidth >= workspaceSize.x - 2.0f) {
        handleOverlayPos = ImVec2(workspacePos.x + workspaceSize.x - 32.0f, workspacePos.y);
        handleOverlaySize = ImVec2(32.0f, paneHeight);
    }

    if (!detachedPreviewActive) {
        ImGui::SetCursorScreenPos(handleOverlayPos);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
        ImGui::BeginChild("EditorSplitHandleOverlay", handleOverlaySize, false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
        ImGui::SetCursorScreenPos(handleOverlayPos);
        ImGui::InvisibleButton("EditorSplitHandle", handleOverlaySize);
        handleHovered = ImGui::IsItemHovered();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        if (handleHovered || m_DraggingSplitHandle || m_SplitHandlePressed) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (handleHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_SplitHandlePressed = true;
            m_SplitHandleMoved = false;
            m_SplitHandlePressedFromViewportPane = false;
            m_SplitAutoAnimating = false;
        }
        EditorModule::DevelopSubjectViewportState splitDragSubjectState;
        const bool hasDevelopSubjectViewportInteraction = GetDevelopSubjectImportanceViewportState(splitDragSubjectState);
        const bool canDragSplitFromViewportPane =
            viewportPaneVisible &&
            !compositeViewportMode &&
            !IsPickingColor() &&
            !IsToneCurveTargeting() &&
            !HasFocusedToneCurveViewportInteraction() &&
            !hasDevelopSubjectViewportInteraction &&
            !CanToggleActiveAutoGainMaskPreview() &&
            !HasActiveCustomMaskOverlay();
        const bool viewportPaneHoveredForSplit =
            canDragSplitFromViewportPane &&
            ImGui::IsMouseHoveringRect(viewportPaneMin, viewportPaneMax, false);
        if (!handleHovered &&
            viewportPaneHoveredForSplit &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_SplitHandlePressed = true;
            m_SplitHandleMoved = false;
            m_SplitHandlePressedFromViewportPane = true;
            m_SplitAutoAnimating = false;
        }
        if (m_SplitHandlePressed && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            const float dragThreshold = m_SplitHandlePressedFromViewportPane ? 4.0f : 2.0f;
            if (std::abs(dragDelta.x) > dragThreshold &&
                (!m_SplitHandlePressedFromViewportPane || std::abs(dragDelta.x) >= std::abs(dragDelta.y))) {
                m_DraggingSplitHandle = true;
                m_SplitHandleMoved = true;
                m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
                m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
            }
        }

        if (m_DraggingSplitHandle) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
                m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
                if (compositeViewportMode) {
                    m_LeftPaneWidth = std::clamp(m_LeftPaneWidth + ImGui::GetIO().MouseDelta.x, 0.0f, workspaceSize.x);
                } else {
                    m_NodeGraphFullscreen = false;
                    m_LeftPaneWidth = std::clamp(m_LeftPaneWidth + ImGui::GetIO().MouseDelta.x, minLeftWidth, maxLeftWidth);
                }
                if (m_ActiveSubWindow == EditorSubWindow::NodeGraph) {
                    m_LastUserNodeGraphWidth = m_LeftPaneWidth;
                }
            } else {
                m_DraggingSplitHandle = false;
            }
        }
        if (m_SplitHandlePressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (!m_SplitHandleMoved && !m_SplitHandlePressedFromViewportPane) {
                const float leftTarget = compositeViewportMode ? 0.0f : minLeftWidth;
                const float rightTarget = compositeViewportMode ? workspaceSize.x : maxLeftWidth;
                const float centerThreshold = workspaceSize.x * 0.5f;
                const float currentCenter = m_LeftPaneWidth + effectiveSplitGap * 0.5f;
                m_SplitAutoAnimFrom = m_LeftPaneWidth;
                if (compositeViewportMode && m_LeftPaneWidth <= 2.0f) {
                    m_SplitAutoAnimTo = rightTarget;
                    m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::GraphOnly;
                } else if (compositeViewportMode && m_LeftPaneWidth >= workspaceSize.x - 2.0f) {
                    m_SplitAutoAnimTo = leftTarget;
                    m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::ViewportOnly;
                } else {
                    m_SplitAutoAnimTo = currentCenter < centerThreshold ? leftTarget : rightTarget;
                    if (compositeViewportMode) {
                        m_SplitAutoAnimSnapMode = m_SplitAutoAnimTo <= 0.0f
                            ? CompositeEdgeSnapMode::ViewportOnly
                            : CompositeEdgeSnapMode::GraphOnly;
                    } else {
                        m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
                    }
                }
                m_SplitAutoAnimStartTime = ImGui::GetTime();
                m_SplitAutoAnimating = true;
            }
            m_SplitHandlePressed = false;
            m_SplitHandleMoved = false;
            m_SplitHandlePressedFromViewportPane = false;
        }
    }

    SubmitRenderIfReady();

    ImGui::EndChild();

    if (m_ActiveSubWindow == EditorSubWindow::NodeGraph && m_NodesPanelWidthAnim > 0.1f) {
        m_Sidebar.GetNodeGraphUI().RenderNodesPanelDrawer(
            this,
            m_NodesPanelWidthAnim,
            nodesPanelTargetWidth,
            paneHeight,
            workspacePos);
    }

    RenderProjectLifecyclePopups();
    RenderManagedRawGraphMutationConfirmPopup();

    if (IsGraphDropImportBusy()) {
        ImGuiExtras::RenderBusyOverlay(
            GetGraphDropImportStatusText().empty()
                ? "Importing images into graph..."
                : GetGraphDropImportStatusText().c_str());
    } else if (IsSourceLoadBusy()) {
        ImGuiExtras::RenderBusyOverlay("Loading source image...");
    } else if (IsExportBusy()) {
        ImGuiExtras::RenderBusyOverlay(GetExportStatusText().empty() ? "Exporting..." : GetExportStatusText().c_str());
    } else if (IsEditorRenderBusy()) {
        EditorRenderWorker::RenderProgress progress = m_RenderWorker.GetProgress();
        std::string label = progress.label.empty() ? std::string("Rendering...") : progress.label;
        if (!progress.busy && m_RenderPending) {
            label = "Finalizing render...";
        }
        const int totalSteps = std::max(1, progress.totalSteps);
        float fraction = static_cast<float>(std::clamp(progress.completedSteps, 0, totalSteps)) /
            static_cast<float>(totalSteps);
        if (!progress.busy && m_RenderPending) {
            fraction = std::max(fraction, 0.98f);
        }
        ImGuiExtras::RenderProgressOverlay(label.c_str(), fraction);
    }
}

EditorModule::ViewportMode EditorModule::GetViewportMode() const {
    RefreshCompletedChainCacheIfNeeded();
    return GetCompletedChainCount() >= 2
        ? ViewportMode::CompositeCanvas
        : ViewportMode::SingleOutputPreview;
}

int EditorModule::GetCompletedChainCount() const {
    RefreshCompletedChainCacheIfNeeded();
    return static_cast<int>(m_CachedCompletedChains.size());
}

int EditorModule::GetConnectedOutputCount() const {
    RefreshCompletedChainCacheIfNeeded();
    return m_CachedConnectedOutputCount;
}

bool EditorModule::CanConsumeEditorCommandKeys() const {
    return !ImGui::GetIO().WantTextInput;
}
