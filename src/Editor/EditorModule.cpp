#include "EditorModule.h"

#include "Async/TaskSystem.h"
#include "NodeGraph/EditorNodeGraphSerializer.h"
#include "Library/LibraryManager.h"
#include "Renderer/GLHelpers.h"
#include "Raw/RawLoader.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include "ThirdParty/stb_image.h"
#include "App/Resources/EmbeddedTabIcons.h"
#include "ThirdParty/stb_image_write.h"
#include "App/settings/AppearanceTheme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <imgui.h>
#include <imgui_internal.h>

namespace {

namespace StackFormat = StackBinaryFormat;

constexpr ImVec4 kEditorWorkspaceBaseColor = ImVec4(0.016f, 0.231f, 0.274f, 1.0f);
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

Raw::RawDevelopSettings BuildRawDevelopSettingsFromMetadata(const Raw::RawMetadata& metadata) {
    Raw::RawDevelopSettings settings;
    settings.blackLevelOverride = metadata.blackLevel;
    settings.whiteLevelOverride = metadata.whiteLevel;
    if (!metadata.isDng) {
        settings.cameraTransformSource = Raw::RawCameraTransformSource::LibRawRgbCam;
    } else if (metadata.hasDngForwardMatrix1 || metadata.hasDngForwardMatrix2 ||
        metadata.hasDngColorMatrix1 || metadata.hasDngColorMatrix2) {
        settings.cameraTransformSource = Raw::RawCameraTransformSource::DngAuto;
    } else {
        settings.cameraTransformSource = Raw::RawCameraTransformSource::LibRawRgbCam;
    }
    return settings;
}

int ResolveRawDevelopOrientation(const Raw::RawMetadata& metadata, const Raw::RawDevelopSettings& settings) {
    int exifRotationSteps = 0;
    if (metadata.orientation == 3) exifRotationSteps = 2;
    else if (metadata.orientation == 5) exifRotationSteps = 1;
    else if (metadata.orientation == 6) exifRotationSteps = 1;
    else if (metadata.orientation == 8) exifRotationSteps = 3;

    int manualRotationSteps = 0;
    if (settings.rotationDegrees == 90) manualRotationSteps = 3;
    else if (settings.rotationDegrees == 180) manualRotationSteps = 2;
    else if (settings.rotationDegrees == 270) manualRotationSteps = 1;
    const int totalRotationSteps = (exifRotationSteps + manualRotationSteps) % 4;

    int effectiveOrientation = metadata.orientation;
    if (metadata.orientation <= 1 || metadata.orientation == 3 || metadata.orientation == 6 || metadata.orientation == 8) {
        if (totalRotationSteps == 0) effectiveOrientation = 1;
        else if (totalRotationSteps == 1) effectiveOrientation = 6;
        else if (totalRotationSteps == 2) effectiveOrientation = 3;
        else if (totalRotationSteps == 3) effectiveOrientation = 8;
    } else if (metadata.orientation == 2 || metadata.orientation == 4 || metadata.orientation == 5 || metadata.orientation == 7) {
        int baseSteps = 0;
        if (metadata.orientation == 2) baseSteps = 0;
        else if (metadata.orientation == 7) baseSteps = 1;
        else if (metadata.orientation == 4) baseSteps = 2;
        else if (metadata.orientation == 5) baseSteps = 3;

        const int finalSteps = (baseSteps + manualRotationSteps) % 4;
        if (finalSteps == 0) effectiveOrientation = 2;
        else if (finalSteps == 1) effectiveOrientation = 7;
        else if (finalSteps == 2) effectiveOrientation = 4;
        else if (finalSteps == 3) effectiveOrientation = 5;
    }
    return effectiveOrientation;
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

    auto mergeInput = [&](const char* socketId) {
        if (const EditorNodeGraph::Link* input = graph.FindInputLink(nodeId, socketId)) {
            state = MergeScenePathState(state, AnalyzeScenePathFromNode(graph, layers, input->fromNodeId, visiting));
        }
    };

    switch (node->kind) {
        case EditorNodeGraph::NodeKind::RawDevelop:
            state.sceneReferred = true;
            mergeInput(EditorNodeGraph::kRawInputSocketId);
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
        case EditorNodeGraph::NodeKind::Mix:
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
            mergeInput(EditorNodeGraph::kImageInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::ImageToMask:
            mergeInput(EditorNodeGraph::kImageToMaskInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::MaskUtility:
            mergeInput(EditorNodeGraph::kMaskUtilityInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::ImageGenerator:
        case EditorNodeGraph::NodeKind::MaskGenerator:
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

template <typename T>
std::size_t HashValue(const T& value) {
    return std::hash<T>{}(value);
}

void HashCombine(std::size_t& seed, std::size_t value) {
    seed ^= value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
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
        case EditorNodeGraph::MixBlendMode::Add: return RenderMixBlendMode::Add;
        case EditorNodeGraph::MixBlendMode::Multiply: return RenderMixBlendMode::Multiply;
        case EditorNodeGraph::MixBlendMode::Screen: return RenderMixBlendMode::Screen;
        case EditorNodeGraph::MixBlendMode::AlphaOver: return RenderMixBlendMode::AlphaOver;
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

RenderImageToMaskKind ToRenderImageToMaskKind(EditorNodeGraph::ImageToMaskKind kind) {
    switch (kind) {
        case EditorNodeGraph::ImageToMaskKind::Luminance: return RenderImageToMaskKind::Luminance;
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
    result.invert = settings.invert;
    return result;
}

RenderImageToMaskSettings ToRenderImageToMaskSettings(const EditorNodeGraph::ImageToMaskSettings& settings) {
    RenderImageToMaskSettings result;
    result.low = settings.low;
    result.high = settings.high;
    result.softness = settings.softness;
    result.invert = settings.invert;
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
    return result;
}

bool IsMaskOutputNode(EditorNodeGraph::NodeKind kind) {
    return kind == EditorNodeGraph::NodeKind::MaskGenerator ||
        kind == EditorNodeGraph::NodeKind::MaskUtility ||
        kind == EditorNodeGraph::NodeKind::ImageToMask;
}

bool IsImageOutputNode(EditorNodeGraph::NodeKind kind) {
    return kind == EditorNodeGraph::NodeKind::Image ||
        kind == EditorNodeGraph::NodeKind::RawDevelop ||
        kind == EditorNodeGraph::NodeKind::ImageGenerator ||
        kind == EditorNodeGraph::NodeKind::Layer ||
        kind == EditorNodeGraph::NodeKind::Mix ||
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
    if (to->kind == EditorNodeGraph::NodeKind::Output && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::Mix &&
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
    ClearCompositeSceneTextures();
}

void EditorModule::Initialize(GLFWwindow* sharedWindow, StackAppearance::AppearanceManager* appearance) {
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
}

void EditorModule::RequestNewProject() {
    if (!HasProjectContent()) {
        ResetToBlankProject();
        return;
    }
    m_ShowNewProjectPrompt = true;
}

bool EditorModule::HasProjectContent() const {
    return !m_CurrentProjectName.empty() ||
        !m_CurrentProjectFileName.empty() ||
        !m_Layers.empty() ||
        !m_NodeGraph.GetNodes().empty() ||
        !m_NodeGraph.GetLinks().empty() ||
        m_Pipeline.HasSourceImage();
}

void EditorModule::ResetToBlankProject() {
    CancelCanvasTool();
    CancelGraphAutoFocusTracking();
    m_ShowNewProjectPrompt = false;
    m_ShowNewProjectDiscardConfirm = false;
    m_GraphDropImportTaskState = Async::TaskState::Idle;
    m_GraphDropImportStatusText.clear();
    m_PendingGraphDropImports.clear();
    m_SourceLoadTaskState = Async::TaskState::Idle;
    m_SourceLoadStatusText.clear();
    m_Layers.clear();
    m_SelectedLayerIndex = -1;
    m_NodeGraph.ResetFromLayers(0, false);
    ClearCompositeRuntimeState();
    m_NodeDirtyGenerations.clear();
    m_PreviewDisplayedRevisions.clear();
    m_PreviewPixelCache.clear();
    m_PreviewRequestedGenerations.clear();
    m_PreviewCompletedGenerations.clear();
    m_ScopeDisplayedRevisions.clear();
    m_Pipeline.Clear();
    m_CompositePreviewPipeline.Clear();
    SetCurrentProjectName("");
    SetCurrentProjectFileName("");
    MarkRenderDirty();
    ClearDirty();
    m_LastUserActionTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
}

void EditorModule::RenderProjectLifecyclePopups() {
    if (m_ShowNewProjectPrompt) {
        ImGui::OpenPopup("Start New Project##Editor");
        m_ShowNewProjectPrompt = false;
    }
    if (m_ShowNewProjectDiscardConfirm) {
        ImGui::OpenPopup("Confirm Discard Project##Editor");
        m_ShowNewProjectDiscardConfirm = false;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Start New Project##Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Start a new project? You can save the current project first, continue without saving, or cancel.");
        ImGui::Spacing();

        if (ImGui::Button("Save Project", ImVec2(140.0f, 0.0f))) {
            const std::string projectName = m_CurrentProjectName.empty() ? "Untitled Project" : m_CurrentProjectName;
            LibraryManager::Get().RequestSaveProject(projectName, this, m_CurrentProjectFileName);
            ResetToBlankProject();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(140.0f, 0.0f))) {
            m_ShowNewProjectDiscardConfirm = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Discard Project##Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Are you sure you want to discard the current project and start a new blank one?");
        ImGui::Spacing();

        if (ImGui::Button("Yes, Discard", ImVec2(140.0f, 0.0f))) {
            ResetToBlankProject();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

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
         node->kind == EditorNodeGraph::NodeKind::RawDevelop);
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
        node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
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

void EditorModule::CancelCanvasTool() {
    m_CanvasToolKind = CanvasToolKind::None;
    m_CanvasToolOwnerNodeId = -1;
    m_CanvasToolStatusText.clear();
    m_IsPickingColor = false;
    m_ColorPickerCallback = nullptr;
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
        return AddRawSourceNodeFromFile(path, graphPosition);
    }
    DecodedImageData decoded;
    if (!DecodeImageFromFile(path, decoded) || decoded.pixels.empty()) {
        return false;
    }

    return AddImageNodeFromPayload(BuildImagePayloadFromDecoded(path, std::move(decoded)), graphPosition);
}

bool EditorModule::AddRawSourceNodeFromFile(const std::string& path, EditorNodeGraph::Vec2 graphPosition) {
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
    if (const EditorNodeGraph::Node* selected = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId())) {
        if (selected->kind == EditorNodeGraph::NodeKind::RawSource) {
            payload.settings = BuildRawDevelopSettingsFromMetadata(selected->rawSource.metadata);
        }
    }
    AddRawDevelopNodeFromPayload(std::move(payload), graphPosition);
}

bool EditorModule::AddRawDevelopNodeFromPayload(EditorNodeGraph::RawDevelopPayload payload, EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::Node* node = m_NodeGraph.AddRawDevelopNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
    return node != nullptr;
}

bool EditorModule::AddFullRawTreeToSource(int rawSourceNodeId) {
    const EditorNodeGraph::Node* rawSourceNode = m_NodeGraph.FindNode(rawSourceNodeId);
    if (!rawSourceNode || rawSourceNode->kind != EditorNodeGraph::NodeKind::RawSource) {
        return false;
    }

    const EditorNodeGraph::Vec2 sourcePosition = rawSourceNode->position;
    SelectGraphNode(rawSourceNodeId);

    constexpr float kNodeSpacing = 250.0f;
    AddRawDevelopNodeAt(EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing * 1.0f, sourcePosition.y });
    const int developNodeId = m_NodeGraph.GetSelectedNodeId();
    AddLayerNodeAt(LayerType::SceneDenoise, EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing * 2.0f, sourcePosition.y });
    const int sceneDenoiseNodeId = m_NodeGraph.GetSelectedNodeId();
    AddLayerNodeAt(LayerType::ToneCurve, EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing * 3.0f, sourcePosition.y });
    const int toneCurveNodeId = m_NodeGraph.GetSelectedNodeId();
    AddLayerNodeAt(LayerType::ViewTransform, EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing * 4.0f, sourcePosition.y });
    const int viewTransformNodeId = m_NodeGraph.GetSelectedNodeId();
    AddLayerNodeAt(LayerType::Flip, EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing * 5.0f, sourcePosition.y });
    const int flipNodeId = m_NodeGraph.GetSelectedNodeId();
    AddOutputNodeAt(EditorNodeGraph::Vec2{ sourcePosition.x + kNodeSpacing * 6.0f, sourcePosition.y });
    const int outputNodeId = m_NodeGraph.GetSelectedNodeId();

    if (developNodeId <= 0 || sceneDenoiseNodeId <= 0 || toneCurveNodeId <= 0 ||
        viewTransformNodeId <= 0 || flipNodeId <= 0 || outputNodeId <= 0) {
        return false;
    }

    std::string errorMessage;
    const bool ok =
        ConnectGraphSockets(rawSourceNodeId, EditorNodeGraph::kRawOutputSocketId, developNodeId, EditorNodeGraph::kRawInputSocketId, &errorMessage) &&
        ConnectGraphSockets(developNodeId, EditorNodeGraph::kImageOutputSocketId, sceneDenoiseNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage) &&
        ConnectGraphSockets(sceneDenoiseNodeId, EditorNodeGraph::kImageOutputSocketId, toneCurveNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage) &&
        ConnectGraphSockets(toneCurveNodeId, EditorNodeGraph::kImageOutputSocketId, viewTransformNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage) &&
        ConnectGraphSockets(viewTransformNodeId, EditorNodeGraph::kImageOutputSocketId, flipNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage) &&
        ConnectGraphSockets(flipNodeId, EditorNodeGraph::kImageOutputSocketId, outputNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage);
    if (!ok) {
        return false;
    }

    MarkRenderDirty(rawSourceNodeId);
    SelectGraphNode(outputNodeId);
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

            if (decodedImages.empty() && rawPaths.empty()) {
                m_GraphDropImportTaskState = Async::TaskState::Failed;
                m_GraphDropImportStatusText = "Failed to import the dropped images.";
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
            for (const std::string& path : rawPaths) {
                EditorNodeGraph::Vec2 nodePosition = sourcePosition;
                nodePosition.y = startY + static_cast<float>(outputIndex++) * kGraphDropRowSpacing;
                if (AddGraphRawChainFromFile(path, nodePosition)) {
                    ++importedCount;
                }
            }

            if (importedCount <= 0) {
                m_GraphDropImportTaskState = Async::TaskState::Failed;
                m_GraphDropImportStatusText = "Failed to create graph nodes for the dropped images.";
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
    if (node->kind == EditorNodeGraph::NodeKind::Image) {
        if (node->image.pixels.empty()) {
            return false;
        }
        LoadSourceFromPixels(node->image.pixels.data(), node->image.width, node->image.height, node->image.channels);
    } else if (node->kind != EditorNodeGraph::NodeKind::RawDevelop) {
        return false;
    }

    m_NodeGraph.ConnectImageToOutput(nodeId);
    SelectGraphNode(nodeId);
    MarkRenderDirty();
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
        if (node->kind == EditorNodeGraph::NodeKind::Mix) {
            found = checkInput(EditorNodeGraph::kMixInputASocketId) ||
                checkInput(EditorNodeGraph::kMixInputBSocketId);
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

void EditorModule::AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind utilityKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMaskUtilityNode(utilityKind, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddImageToMaskNodeAt(EditorNodeGraph::ImageToMaskKind converterKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddImageToMaskNode(converterKind, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
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
    RenderGraphSnapshot snapshot;
    snapshot.outputNodeId = m_NodeGraph.ResolvePreviewOutputNodeId();

    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        RenderGraphNode renderNode;
        renderNode.nodeId = node.id;
        switch (node.kind) {
            case EditorNodeGraph::NodeKind::Image:
                renderNode.kind = RenderGraphNodeKind::Image;
                renderNode.image.pixels = node.image.pixels;
                renderNode.image.width = node.image.width;
                renderNode.image.height = node.image.height;
                renderNode.image.channels = node.image.channels;
                break;
            case EditorNodeGraph::NodeKind::RawSource:
                renderNode.kind = RenderGraphNodeKind::RawSource;
                renderNode.rawSource.sourcePath = node.rawSource.sourcePath;
                renderNode.rawSource.metadata = node.rawSource.metadata;
                break;
            case EditorNodeGraph::NodeKind::RawNeuralDenoise:
                renderNode.kind = RenderGraphNodeKind::RawNeuralDenoise;
                renderNode.rawNeuralDenoise.settings = node.rawNeuralDenoise.settings;
                break;
            case EditorNodeGraph::NodeKind::RawDevelop:
                renderNode.kind = RenderGraphNodeKind::RawDevelop;
                renderNode.rawDevelop.settings = node.rawDevelop.settings;
                break;
            case EditorNodeGraph::NodeKind::Layer:
                renderNode.kind = RenderGraphNodeKind::Layer;
                if (node.layerIndex >= 0 && node.layerIndex < static_cast<int>(m_Layers.size()) && m_Layers[node.layerIndex]) {
                    renderNode.layerJson = m_Layers[node.layerIndex]->Serialize();
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
            case EditorNodeGraph::NodeKind::ChannelSplit:
                renderNode.kind = RenderGraphNodeKind::ChannelSplit;
                break;
            case EditorNodeGraph::NodeKind::ChannelCombine:
                renderNode.kind = RenderGraphNodeKind::ChannelCombine;
                break;
            case EditorNodeGraph::NodeKind::Composite:
            case EditorNodeGraph::NodeKind::Scope:
            case EditorNodeGraph::NodeKind::Preview:
                continue;
        }
        snapshot.nodes.push_back(std::move(renderNode));
    }

    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (m_NodeGraph.GetLinkRole(link) == EditorNodeGraph::LinkRole::Scope) {
            continue;
        }
        snapshot.links.push_back(RenderGraphLink{
            link.fromNodeId,
            link.fromSocketId,
            link.toNodeId,
            link.toSocketId
        });
    }
    return snapshot;
}

bool EditorModule::TryCopyImageNodePixels(int sourceNodeId, std::vector<unsigned char>& outPixels, int& outW, int& outH, int& outChannels) const {
    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(sourceNodeId);
    if (!sourceNode) {
        return false;
    }

    if (sourceNode->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        const EditorNodeGraph::Link* rawInput = m_NodeGraph.FindInputLink(sourceNode->id, EditorNodeGraph::kRawInputSocketId);
        const EditorNodeGraph::Node* rawSourceNode = rawInput ? m_NodeGraph.FindNode(rawInput->fromNodeId) : nullptr;
        const Raw::RawMetadata emptyMetadata;
        const Raw::RawMetadata& metadata = rawSourceNode ? rawSourceNode->rawSource.metadata : emptyMetadata;
        
        const int visibleWidth = metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
        const int visibleHeight = metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight;
        
        const int effectiveOrientation = ResolveRawDevelopOrientation(metadata, sourceNode->rawDevelop.settings);
        const bool swaps = (effectiveOrientation == 5 || effectiveOrientation == 6 || effectiveOrientation == 7 || effectiveOrientation == 8);
        if (sourceNode->rawDevelop.settings.rotateToFitFrame) {
            outW = visibleWidth;
            outH = visibleHeight;
        } else {
            outW = swaps ? visibleHeight : visibleWidth;
            outH = swaps ? visibleWidth : visibleHeight;
        }
        outChannels = 4;
        outPixels = BuildTransparentPixels(outW, outH);
        return !outPixels.empty();
    }

    if (sourceNode->kind == EditorNodeGraph::NodeKind::RawSource) {
        outW = Raw::DisplayWidth(sourceNode->rawSource.metadata);
        outH = Raw::DisplayHeight(sourceNode->rawSource.metadata);
        outChannels = 4;
        outPixels = BuildTransparentPixels(outW, outH);
        return !outPixels.empty();
    }

    if (sourceNode->kind != EditorNodeGraph::NodeKind::Image ||
        sourceNode->image.pixels.empty() ||
        sourceNode->image.width <= 0 ||
        sourceNode->image.height <= 0) {
        return false;
    }

    outPixels = sourceNode->image.pixels;
    outW = sourceNode->image.width;
    outH = sourceNode->image.height;
    outChannels = std::max(1, sourceNode->image.channels);
    return true;
}

bool EditorModule::TryResolveReferenceSourcePixels(
    int nodeId,
    const std::string& socketId,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH,
    int& outChannels) const {
    const int referenceSourceNodeId = m_NodeGraph.ResolveReferenceSourceNodeId(nodeId, socketId);
    return TryCopyImageNodePixels(referenceSourceNodeId, outPixels, outW, outH, outChannels);
}

bool EditorModule::TryResolveReferenceSourcePixelsForOutput(
    int outputNodeId,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH,
    int& outChannels) const {
    const int referenceSourceNodeId = m_NodeGraph.ResolveReferenceSourceNodeIdForOutput(outputNodeId);
    return TryCopyImageNodePixels(referenceSourceNodeId, outPixels, outW, outH, outChannels);
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

std::size_t EditorModule::BuildCompositeChainFingerprint(const EditorNodeGraph::CompletedChainInfo& chain) const {
    std::size_t fingerprint = HashValue(chain.outputNodeId);
    HashCombine(fingerprint, HashValue(chain.terminalNodeId));
    HashCombine(fingerprint, HashValue(chain.sourceNodeId));
    for (int nodeId : chain.nodeIds) {
        HashCombine(fingerprint, HashValue(nodeId));
        const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
        if (!node) {
            continue;
        }
        HashCombine(fingerprint, HashValue(static_cast<int>(node->kind)));
        switch (node->kind) {
            case EditorNodeGraph::NodeKind::Image:
                HashCombine(fingerprint, HashValue(node->image.width));
                HashCombine(fingerprint, HashValue(node->image.height));
                HashCombine(fingerprint, HashValue(node->image.channels));
                HashCombine(fingerprint, HashValue(node->title));
                break;
            case EditorNodeGraph::NodeKind::Layer:
                if (node->layerIndex >= 0 && node->layerIndex < static_cast<int>(m_Layers.size()) && m_Layers[node->layerIndex]) {
                    HashCombine(fingerprint, HashValue(m_Layers[node->layerIndex]->Serialize().dump()));
                }
                break;
            case EditorNodeGraph::NodeKind::Mix:
                HashCombine(fingerprint, HashValue(static_cast<int>(node->mixBlendMode)));
                HashCombine(fingerprint, HashValue(node->mixFactor));
                break;
            case EditorNodeGraph::NodeKind::ImageGenerator:
                HashCombine(fingerprint, HashValue(static_cast<int>(node->imageGeneratorKind)));
                HashCombine(fingerprint, HashValue(node->imageGeneratorSettings.angle));
                HashCombine(fingerprint, HashValue(node->imageGeneratorSettings.offset));
                for (float channel : node->imageGeneratorSettings.colorA) {
                    HashCombine(fingerprint, HashValue(channel));
                }
                for (float channel : node->imageGeneratorSettings.colorB) {
                    HashCombine(fingerprint, HashValue(channel));
                }
                break;
            default:
                break;
        }
    }
    return fingerprint;
}

std::string EditorModule::BuildCompositeChainLabel(const EditorNodeGraph::CompletedChainInfo& chain) const {
    if (chain.outputNodeId <= 0) {
        return "Output";
    }

    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(chain.sourceNodeId);
    const EditorNodeGraph::Node* outputNode = m_NodeGraph.FindNode(chain.outputNodeId);
    const std::string sourceLabel = sourceNode && !sourceNode->title.empty()
        ? sourceNode->title
        : ("Source " + std::to_string(chain.sourceNodeId));
    const std::string outputLabel = outputNode && !outputNode->title.empty()
        ? outputNode->title
        : ("Output " + std::to_string(chain.outputNodeId));
    return sourceLabel + " -> " + outputLabel;
}

std::string EditorModule::BuildCompositeChainLabel(int outputNodeId) const {
    RefreshCompletedChainCacheIfNeeded();
    auto chainIt = std::find_if(
        m_CachedCompletedChains.begin(),
        m_CachedCompletedChains.end(),
        [outputNodeId](const CachedCompositeChainState& chain) { return chain.info.outputNodeId == outputNodeId; });
    if (chainIt == m_CachedCompletedChains.end()) {
        return "Output " + std::to_string(outputNodeId);
    }
    return chainIt->label.empty() ? BuildCompositeChainLabel(chainIt->info) : chainIt->label;
}

bool EditorModule::HasCompositeNode() const {
    return false;
}

void EditorModule::EnsureCompositeNode() {
    // Deprecated: settings have been fully migrated to the unified editor settings panel.
}


void EditorModule::RenderUI() {
    ConsumeRenderWorkerResults();

    // User Activity Tracking & Auto-Save Check
    {
        const ImGuiIO& io = ImGui::GetIO();
        bool userActive = false;
        // Check for any keypress
        for (int i = 0; i < ImGuiKey_NamedKey_COUNT; i++) {
            if (ImGui::IsKeyPressed((ImGuiKey)i)) {
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

        if (m_Dirty && !m_CurrentProjectFileName.empty() && !Async::IsBusy(LibraryManager::Get().GetSaveTaskState())) {
            double idleTime = ImGui::GetTime() - m_LastUserActionTime;
            if (idleTime >= 10.0 && idleTime < 30.0) {
                LibraryManager::Get().RequestSaveProject(m_CurrentProjectName, this, m_CurrentProjectFileName);
                // Reset user action time to current time to avoid repeated saves in subsequent frames
                m_LastUserActionTime = ImGui::GetTime();
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

    // Intercept keyboard hotkeys 1, 2, and 3
    if (CanConsumeEditorCommandKeys()) {
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) {
            SwitchToSubWindow(EditorSubWindow::NodeGraph);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_2, false) && GetCompletedChainCount() >= 2) {
            SwitchToSubWindow(EditorSubWindow::ExportSettings);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_3, false)) {
            SwitchToSubWindow(EditorSubWindow::Settings);
        }
    }
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, GetWorkspaceBaseColor());
    ImGui::BeginChild("StackEditorWorkspace", ImVec2(0, 0), false, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    const ImVec2 workspacePos = ImGui::GetCursorScreenPos();
    const ImVec2 workspaceSize = ImGui::GetContentRegionAvail();
    const ImVec4 workspaceColor = GetWorkspaceBaseColor();
    const ImU32 workspaceColorU32 = ImGui::ColorConvertFloat4ToU32(workspaceColor);
    const ViewportMode viewportMode = GetViewportMode();
    HandleViewportModeTransition(m_LastViewportMode, viewportMode);
    const bool compositeViewportMode = viewportMode == ViewportMode::CompositeCanvas;
    EnsureCompositeSceneState(m_LastCompositeCanvasSize);
    ImGui::GetWindowDrawList()->AddRectFilled(
        workspacePos,
        ImVec2(workspacePos.x + workspaceSize.x, workspacePos.y + workspaceSize.y),
        workspaceColorU32);
    const float splitGap = 32.0f;
    const float minLeftWidth = 260.0f;
    const float minRightWidth = 420.0f;
    const float maxLeftWidth = std::max(minLeftWidth, workspaceSize.x - minRightWidth - splitGap);

    // Transition detection for split auto-animation to optimal sizes
    if (m_TargetSubWindow != m_LastSplitTargetSubWindow || m_TargetComplexNodeId != m_LastSplitTargetComplexNodeId) {
        m_LastSplitTargetSubWindow = m_TargetSubWindow;
        m_LastSplitTargetComplexNodeId = m_TargetComplexNodeId;

        float targetWidth = m_LeftPaneWidth;
        if (m_TargetSubWindow == EditorSubWindow::NodeGraph) {
            if (m_LastUserNodeGraphWidth <= 0.0f) {
                m_LastUserNodeGraphWidth = std::clamp(workspaceSize.x * (2.0f / 3.0f), minLeftWidth, maxLeftWidth);
            }
            targetWidth = m_LastUserNodeGraphWidth;
        } else if (m_TargetSubWindow == EditorSubWindow::ExportSettings) {
            targetWidth = 360.0f;
        } else if (m_TargetSubWindow == EditorSubWindow::Settings) {
            targetWidth = 360.0f;
        } else if (m_TargetSubWindow == EditorSubWindow::ComplexNode) {
            targetWidth = 470.0f;
        }

        targetWidth = std::clamp(targetWidth, minLeftWidth, maxLeftWidth);

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
            if (m_LeftPaneWidth <= 0.0f && !compositeViewportMode) {
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

    const float effectiveSplitGap = (compositeViewportMode || m_CompositeEdgeSnapMode != CompositeEdgeSnapMode::None || m_DraggingSplitHandle || m_SplitAutoAnimating)
        ? splitGap * std::clamp(
            std::min(m_LeftPaneWidth, std::max(0.0f, workspaceSize.x - m_LeftPaneWidth)) / std::max(1.0f, splitGap),
            0.0f,
            1.0f)
        : splitGap;

    const float handleRadius = 7.0f;
    const ImVec2 handleCenter(
        std::clamp(
            workspacePos.x + m_LeftPaneWidth + (effectiveSplitGap * 0.5f),
            workspacePos.x + handleRadius + 2.0f,
            workspacePos.x + workspaceSize.x - handleRadius - 2.0f),
        workspacePos.y + 14.0f);
    bool handleHovered = false;
    if (m_SplitAutoAnimating) {
        const float t = static_cast<float>(std::clamp((ImGui::GetTime() - m_SplitAutoAnimStartTime) / 0.2, 0.0, 1.0));
        const float eased = 1.0f - std::pow(1.0f - t, 3.0f);
        m_LeftPaneWidth = m_SplitAutoAnimFrom + (m_SplitAutoAnimTo - m_SplitAutoAnimFrom) * eased;
        if (t >= 1.0f) {
            m_SplitAutoAnimating = false;
            m_CompositeEdgeSnapMode = m_SplitAutoAnimSnapMode;
        }
    }

    ImDrawList* rootDrawList = ImGui::GetForegroundDrawList();
    const ImU32 handleFill = (m_DraggingSplitHandle || m_SplitHandlePressed)
        ? IM_COL32(92, 178, 255, 230)
        : (handleHovered ? IM_COL32(72, 148, 214, 214) : IM_COL32(52, 92, 120, 188));
    rootDrawList->AddCircleFilled(handleCenter, handleRadius, handleFill, 32);

    const float paneHeight = std::max(0.0f, workspaceSize.y);
    const float rightWidth = std::max(0.0f, workspaceSize.x - m_LeftPaneWidth - effectiveSplitGap);

    // Update Left Panel Hover & Animation State
    const bool isNodeBrowserOpen = m_Sidebar.GetNodeGraphUI().IsNodeBrowserOpen();
    if (m_ActiveSubWindow == EditorSubWindow::NodeGraph && m_LeftPaneWidth > 1.0f) {
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        bool hoveringPanelOrTab = false;
        
        if (!isNodeBrowserOpen) {
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

    float nodesPanelTargetWidth = isNodeBrowserOpen ? 280.0f : 0.0f;
    m_NodesPanelWidthAnim += (nodesPanelTargetWidth - m_NodesPanelWidthAnim) * animDt * 10.0f;
    if (std::abs(m_NodesPanelWidthAnim - nodesPanelTargetWidth) < 0.1f) {
        m_NodesPanelWidthAnim = nodesPanelTargetWidth;
    }

    if (m_LeftPaneWidth > 1.0f) {
        ImGui::SetCursorScreenPos(workspacePos);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, workspaceColor);
        ImGui::BeginChild("EditorGraphPane", ImVec2(m_LeftPaneWidth, paneHeight), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_SubWindowTransitionAlpha);
        m_Sidebar.Render(this);
        ImGui::PopStyleVar();

        RenderFloatingToolbar();

        // Canvas Stack Slide-out Drawer Overlay
        if (m_ActiveSubWindow == EditorSubWindow::NodeGraph) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            
            // 1. Sliding panel background & content
            if (m_LeftPanelWidthAnim > 0.1f) {
                ImVec2 panelMin = workspacePos;
                
                // Redesigned premium feathered blend background
                float gradientWidth = std::min(60.0f, m_LeftPanelWidthAnim);
                float solidWidth = m_LeftPanelWidthAnim - gradientWidth;
                
                // Dynamic matching color with translucency based on theme workspace color
                const float luminance = 0.2126f * workspaceColor.x + 0.7152f * workspaceColor.y + 0.0722f * workspaceColor.z;
                const bool isLightBg = luminance >= 0.5f;

                ImVec4 colBgOpaqueVec = workspaceColor;
                colBgOpaqueVec.w = isLightBg ? 0.94f : 0.92f;
                const ImU32 colBgOpaque = ImGui::ColorConvertFloat4ToU32(colBgOpaqueVec);

                ImVec4 colBgTransVec = workspaceColor;
                colBgTransVec.w = 0.0f;
                const ImU32 colBgTrans = ImGui::ColorConvertFloat4ToU32(colBgTransVec);

                const ImU32 colTitleText = isLightBg ? IM_COL32(18, 24, 30, 255) : IM_COL32(255, 255, 255, 255);
                const ImU32 colPassiveText = isLightBg ? IM_COL32(80, 95, 105, 220) : IM_COL32(140, 160, 170, 200);
                const ImU32 colActiveText = isLightBg ? IM_COL32(16, 110, 190, 255) : IM_COL32(92, 178, 255, 255);
                const ImU32 colNormalText = isLightBg ? IM_COL32(40, 50, 60, 220) : IM_COL32(200, 210, 220, 200);
                const ImU32 colHoveredHeader = isLightBg ? IM_COL32(16, 110, 190, 28) : IM_COL32(92, 178, 255, 30);
                const ImU32 colActiveHeader = isLightBg ? IM_COL32(16, 110, 190, 48) : IM_COL32(92, 178, 255, 50);
                
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
                
                // Content area
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_SubWindowTransitionAlpha);
                
                float contentWidth = m_LeftPanelWidthAnim - 40.0f; // breathing room on right for the feathered edge
                if (contentWidth > 1.0f) {
                    ImGui::SetCursorScreenPos(ImVec2(workspacePos.x + 16.0f, workspacePos.y + 28.0f));
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
                }
                
                ImGui::PopStyleVar();
            }
        }

        // Nodes Panel Slide-out Drawer Overlay
        if (m_ActiveSubWindow == EditorSubWindow::NodeGraph) {
            if (m_NodesPanelWidthAnim > 0.1f) {
                m_Sidebar.GetNodeGraphUI().RenderNodesPanelDrawer(this, m_NodesPanelWidthAnim, paneHeight, workspacePos);
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    ImVec2 viewportPaneMin(workspacePos.x + m_LeftPaneWidth + effectiveSplitGap, workspacePos.y);
    ImVec2 viewportPaneMax(viewportPaneMin.x + rightWidth, workspacePos.y + paneHeight);
    bool viewportPaneRendered = false;
    if (rightWidth > 1.0f) {
        ImGui::SetCursorScreenPos(ImVec2(workspacePos.x + m_LeftPaneWidth + effectiveSplitGap, workspacePos.y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, workspaceColor);
        ImGui::BeginChild("EditorViewportPane", ImVec2(rightWidth, paneHeight), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
        m_Viewport.Render(this);
        ImGui::EndChild();
        viewportPaneMin = ImGui::GetItemRectMin();
        viewportPaneMax = ImGui::GetItemRectMax();
        viewportPaneRendered = true;
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    ImVec2 handleOverlayPos(handleCenter.x - 12.0f, handleCenter.y - 12.0f);
    ImVec2 handleOverlaySize(24.0f, 24.0f);
    if (m_CompositeEdgeSnapMode == CompositeEdgeSnapMode::ViewportOnly || m_LeftPaneWidth <= 2.0f) {
        handleOverlayPos = ImVec2(workspacePos.x, workspacePos.y + 2.0f);
        handleOverlaySize = ImVec2(28.0f, std::min(38.0f, std::max(24.0f, paneHeight)));
    } else if (m_CompositeEdgeSnapMode == CompositeEdgeSnapMode::GraphOnly || m_LeftPaneWidth >= workspaceSize.x - 2.0f) {
        handleOverlayPos = ImVec2(workspacePos.x + workspaceSize.x - 28.0f, workspacePos.y + 2.0f);
        handleOverlaySize = ImVec2(28.0f, std::min(38.0f, std::max(24.0f, paneHeight)));
    }

    ImGui::SetCursorScreenPos(handleOverlayPos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("EditorSplitHandleOverlay", handleOverlaySize, false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorScreenPos(ImVec2(handleCenter.x - 10.0f, handleCenter.y - 10.0f));
    ImGui::InvisibleButton("EditorSplitHandle", ImVec2(20.0f, 20.0f));
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
        m_SplitAutoAnimating = false;
    }
    if (m_SplitHandlePressed && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (std::abs(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x) > 2.0f) {
            m_DraggingSplitHandle = true;
            m_SplitHandleMoved = true;
            m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
            m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
        }
    }

    const bool viewportPaneHovered = ImGui::IsMouseHoveringRect(viewportPaneMin, viewportPaneMax);
    const bool viewportSplitDragAllowed = viewportPaneHovered && !IsPickingColor() && !compositeViewportMode;
    if ((viewportSplitDragAllowed || m_DraggingSplitHandle) && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (viewportSplitDragAllowed && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_DraggingSplitHandle = true;
    }
    if (m_DraggingSplitHandle) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
            m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
            if (compositeViewportMode) {
                m_LeftPaneWidth = std::clamp(m_LeftPaneWidth + ImGui::GetIO().MouseDelta.x, 0.0f, workspaceSize.x);
            } else {
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
        if (!m_SplitHandleMoved) {
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
    }

    SubmitRenderIfReady();
    RenderProjectLifecyclePopups();

    if (IsGraphDropImportBusy()) {
        ImGuiExtras::RenderBusyOverlay(
            GetGraphDropImportStatusText().empty()
                ? "Importing images into graph..."
                : GetGraphDropImportStatusText().c_str());
    } else if (IsSourceLoadBusy()) {
        ImGuiExtras::RenderBusyOverlay("Loading source image...");
    } else if (IsExportBusy()) {
        ImGuiExtras::RenderBusyOverlay(GetExportStatusText().empty() ? "Exporting..." : GetExportStatusText().c_str());
    }

    ImGui::EndChild();
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

std::uint64_t EditorModule::GetPreviewNodeRevision(int previewNodeId) const {
    const EditorNodeGraph::Link* input =
        m_NodeGraph.FindAnyInputLink(previewNodeId, EditorNodeGraph::kPreviewInputSocketId);
    if (!input) {
        return 0;
    }
    return std::max<std::uint64_t>(1, GetNodeDirtyGeneration(input->fromNodeId));
}

const EditorModule::GraphPreviewPixels* EditorModule::GetCachedPreviewPixelsForNode(int previewNodeId) const {
    const auto it = m_PreviewPixelCache.find(previewNodeId);
    return it != m_PreviewPixelCache.end() ? &it->second : nullptr;
}

std::uint64_t EditorModule::GetScopeNodeRevision(int sourceNodeId) const {
    if (sourceNodeId <= 0) {
        m_ScopeDisplayedRevisions[sourceNodeId] = 0;
        return 0;
    }

    const std::uint64_t desiredRevision = GetNodeDirtyGeneration(sourceNodeId);
    std::uint64_t& displayedRevision = m_ScopeDisplayedRevisions[sourceNodeId];
    if (CanRefreshPreviewLikeNodes() && !HasPendingPreviewRefreshes()) {
        displayedRevision = desiredRevision;
    }
    return displayedRevision;
}

ImVec4 EditorModule::GetWorkspaceBaseColor() const {
    if (m_Appearance) {
        return m_Appearance->GetWorkingTheme().colors[ImGuiCol_WindowBg];
    }
    if (ImGui::GetCurrentContext() != nullptr) {
        return ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    }
    return kEditorWorkspaceBaseColor;
}

bool EditorModule::CanConsumeEditorCommandKeys() const {
    return !ImGui::GetIO().WantTextInput;
}

void EditorModule::SwitchToSubWindow(EditorSubWindow target) {
    if (m_ActiveSubWindow == target && m_TargetSubWindow == target) {
        return;
    }
    m_TargetSubWindow = target;
    m_SubWindowTransitionFadingOut = true;
}

void EditorModule::SwitchToComplexNodeSubWindow(int nodeId) {
    if (m_ActiveSubWindow == EditorSubWindow::ComplexNode && m_ActiveComplexNodeId == nodeId &&
        m_TargetSubWindow == EditorSubWindow::ComplexNode && m_TargetComplexNodeId == nodeId) {
        return;
    }
    m_TargetSubWindow = EditorSubWindow::ComplexNode;
    m_TargetComplexNodeId = nodeId;
    m_SubWindowTransitionFadingOut = true;
}

static unsigned int LoadEditorResourceTexture(const unsigned char* data, unsigned int size, const char* debugName) {
    if (!data || size == 0) {
        return 0;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(0);
    unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
    if (!pixels) {
        fprintf(stderr, "[EditorModule] Failed to decode embedded %s icon.\n", debugName);
        return 0;
    }
    const unsigned int texture = GLHelpers::CreateTextureFromPixels(pixels, width, height, 4);
    stbi_image_free(pixels);
    return texture;
}

static unsigned int LoadTextureFromFile(const char* path, const char* debugName) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(0);
    unsigned char* pixels = stbi_load(path, &width, &height, &channels, 4);
    if (!pixels) {
        fprintf(stderr, "[EditorModule] Failed to load %s icon from path: %s\n", debugName, path);
        return 0;
    }
    const unsigned int texture = GLHelpers::CreateTextureFromPixels(pixels, width, height, 4);
    stbi_image_free(pixels);
    return texture;
}

void EditorModule::LoadResourceTextures() {
    if (m_TexturesLoaded) {
        return;
    }
    m_NodeGraphIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::NodeGraph_png_data,
        EmbeddedTabIcons::NodeGraph_png_size,
        "NodeGraph"
    );
    m_ExportIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::Export_png_data,
        EmbeddedTabIcons::Export_png_size,
        "Export"
    );
    m_SettingsIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::Settings_png_data,
        EmbeddedTabIcons::Settings_png_size,
        "Settings"
    );
    m_BackgroundRemoverIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::BackgroundRemover_png_data,
        EmbeddedTabIcons::BackgroundRemover_png_size,
        "BackgroundRemover"
    );
    m_ColorGradeIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::ColorGrade_png_data,
        EmbeddedTabIcons::ColorGrade_png_size,
        "ColorGrade"
    );
    m_TexturesLoaded = true;
}

void EditorModule::RenderFloatingToolbar() {
    LoadResourceTextures();

    const float radius = 20.0f;
    const float margin = 24.0f;
    const float spacing = 12.0f;
    const float buttonDiameter = radius * 2.0f;

    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImVec2 basePos(
        windowPos.x + margin + radius + m_LeftPanelWidthAnim,
        windowPos.y + windowSize.y - margin - radius
    );

    struct ToolbarButton {
        bool isStandard = true;
        EditorSubWindow subWin = EditorSubWindow::NodeGraph;
        int nodeId = -1;
        std::string typeId;
        std::string label;
    };

    std::vector<ToolbarButton> buttons;
    buttons.push_back({ true, EditorSubWindow::Settings, -1, "", "Editor Settings [3]" });
    buttons.push_back({ true, EditorSubWindow::NodeGraph, -1, "", "Node Graph [1]" });

    // Append dynamic complex node buttons
    for (const auto& node : m_NodeGraph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::Layer) {
            const NodeSurfaceSpec surfaceSpec = GetNodeSurfaceSpec(node.id);
            if (surfaceSpec.presentation == NodeSurfacePresentation::RichExpandedSurface) {
                const LayerDescriptor* descriptor = LayerRegistry::FindDescriptorByTypeId(node.typeId);
                const std::string label = !node.title.empty()
                    ? node.title
                    : (descriptor && descriptor->displayName ? descriptor->displayName : std::string("Advanced Node"));
                buttons.push_back({ false, EditorSubWindow::ComplexNode, node.id, node.typeId, label });
            }
        }
    }

    if (GetCompletedChainCount() >= 2) {
        buttons.push_back({ true, EditorSubWindow::ExportSettings, -1, "", "Export Settings [2]" });
    }

    auto scaleAlpha = [](ImU32 col, float alpha) -> ImU32 {
        unsigned int r = col & 0xFF;
        unsigned int g = (col >> 8) & 0xFF;
        unsigned int b = (col >> 16) & 0xFF;
        unsigned int a = (col >> 24) & 0xFF;
        unsigned int newA = static_cast<unsigned int>(a * alpha);
        return r | (g << 8) | (b << 16) | (newA << 24);
    };

    for (size_t i = 0; i < buttons.size(); ++i) {
        const auto& btn = buttons[i];
        const bool isActive = btn.isStandard 
            ? (m_TargetSubWindow == btn.subWin) 
            : (m_TargetSubWindow == EditorSubWindow::ComplexNode && m_TargetComplexNodeId == btn.nodeId);
        
        ImVec2 center(basePos.x + i * (buttonDiameter + spacing), basePos.y);

        float animAlpha = 1.0f;
        if (!btn.isStandard) {
            double curTime = ImGui::GetTime();
            double spawnTime = curTime;
            auto it = m_ToolbarButtonSpawnTimes.find(btn.nodeId);
            if (it == m_ToolbarButtonSpawnTimes.end()) {
                m_ToolbarButtonSpawnTimes[btn.nodeId] = curTime;
            } else {
                spawnTime = it->second;
            }
            double age = curTime - spawnTime;
            float tAnim = std::clamp(static_cast<float>(age / 0.4), 0.0f, 1.0f);
            float easeOut = 1.0f - std::pow(1.0f - tAnim, 3.0f);

            center.x += (easeOut - 1.0f) * 40.0f;
            animAlpha = easeOut;
        }

        ImGui::SetCursorScreenPos(ImVec2(center.x - radius, center.y - radius));
        char btnId[64];
        if (btn.isStandard) {
            snprintf(btnId, sizeof(btnId), "##FloatingSubWindowBtn_%d", static_cast<int>(btn.subWin));
        } else {
            snprintf(btnId, sizeof(btnId), "##FloatingComplexNodeBtn_%d", btn.nodeId);
        }
        
        ImGui::InvisibleButton(btnId, ImVec2(buttonDiameter, buttonDiameter));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();

        if (clicked) {
            if (btn.isStandard) {
                SwitchToSubWindow(btn.subWin);
            } else {
                SwitchToComplexNodeSubWindow(btn.nodeId);
            }
        }

        ImU32 bgCol = IM_COL32(24, 24, 24, 200);
        ImU32 borderCol = IM_COL32(60, 60, 60, 180);
        float borderThickness = 1.0f;

        if (isActive) {
            borderCol = IM_COL32(92, 178, 255, 255);
            borderThickness = 2.0f;
            bgCol = IM_COL32(30, 30, 30, 230);
        } else if (hovered) {
            borderCol = IM_COL32(100, 100, 100, 255);
            bgCol = IM_COL32(40, 40, 40, 230);
        }

        if (animAlpha < 1.0f) {
            bgCol = scaleAlpha(bgCol, animAlpha);
            borderCol = scaleAlpha(borderCol, animAlpha);
        }

        drawList->AddCircleFilled(ImVec2(center.x, center.y + 2.0f), radius, scaleAlpha(IM_COL32(0, 0, 0, 40), animAlpha), 32);
        drawList->AddCircleFilled(center, radius, bgCol, 32);
        drawList->AddCircle(center, radius, borderCol, 32, borderThickness);

        unsigned int tex = 0;
        if (btn.isStandard) {
            if (btn.subWin == EditorSubWindow::NodeGraph) {
                tex = m_NodeGraphIconTexture;
            } else if (btn.subWin == EditorSubWindow::ExportSettings) {
                tex = m_ExportIconTexture;
            } else if (btn.subWin == EditorSubWindow::Settings) {
                tex = m_SettingsIconTexture;
            }
        } else {
            if (btn.typeId == "BackgroundPatcher") {
                tex = m_BackgroundRemoverIconTexture;
            } else if (btn.typeId == "ColorGrade") {
                tex = m_ColorGradeIconTexture;
            } else {
                tex = m_SettingsIconTexture;
            }
        }

        if (tex) {
            const float iconHalfSize = 12.0f;
            ImVec2 pMin(center.x - iconHalfSize, center.y - iconHalfSize);
            ImVec2 pMax(center.x + iconHalfSize, center.y + iconHalfSize);
            
            ImU32 iconColor = IM_COL32(255, 255, 255, 255);
            if (!isActive && !hovered) {
                iconColor = IM_COL32(180, 180, 180, 180);
            }
            if (animAlpha < 1.0f) {
                iconColor = scaleAlpha(iconColor, animAlpha);
            }
            drawList->AddImage((ImTextureID)(intptr_t)tex, pMin, pMax, ImVec2(0, 0), ImVec2(1, 1), iconColor);
        }

        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(btn.label.c_str());
            ImGui::EndTooltip();
        }
    }

    // Clean up spawn times map
    for (auto it = m_ToolbarButtonSpawnTimes.begin(); it != m_ToolbarButtonSpawnTimes.end();) {
        bool found = false;
        for (const auto& btn : buttons) {
            if (!btn.isStandard && btn.nodeId == it->first) {
                found = true;
                break;
            }
        }
        if (!found) {
            it = m_ToolbarButtonSpawnTimes.erase(it);
        } else {
            ++it;
        }
    }
}
