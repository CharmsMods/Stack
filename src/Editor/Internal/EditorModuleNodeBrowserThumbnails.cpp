#include "Editor/EditorModule.h"

#include "Async/TaskSystem.h"
#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include "Library/LibraryManager.h"
#include "ThirdParty/stb_image_write.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace {

constexpr int kNodeBrowserThumbnailMaxDimension = 256;
constexpr int kFallbackCardWidth = 256;
constexpr int kFallbackCardHeight = 160;

const std::vector<EditorNodeGraphDefinitions::NodeCatalogEntry>& CachedNodeBrowserEntries() {
    static const std::vector<EditorNodeGraphDefinitions::NodeCatalogEntry> entries =
        EditorNodeGraphDefinitions::BuildNodeCatalogEntries();
    return entries;
}

std::uint64_t HashBytes64(const unsigned char* data, std::size_t size, std::uint64_t seed = 1469598103934665603ull) {
    std::uint64_t hash = seed;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= static_cast<std::uint64_t>(data[index]);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t HashString64(const std::string& value, std::uint64_t seed = 1469598103934665603ull) {
    return HashBytes64(reinterpret_cast<const unsigned char*>(value.data()), value.size(), seed);
}

std::string HexString64(std::uint64_t value) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text(16, '0');
    for (int index = 15; index >= 0; --index) {
        text[static_cast<std::size_t>(index)] = kDigits[value & 0x0full];
        value >>= 4u;
    }
    return text;
}

bool TryComputePixelByteCount(int width, int height, int channels, std::size_t& outByteCount) {
    outByteCount = 0;
    if (width <= 0 || height <= 0 || channels <= 0) {
        return false;
    }

    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    const std::size_t c = static_cast<std::size_t>(channels);
    if (w > std::numeric_limits<std::size_t>::max() / h) {
        return false;
    }
    const std::size_t pixelCount = w * h;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / c) {
        return false;
    }

    outByteCount = pixelCount * c;
    return true;
}

bool HasCompletePixelBuffer(const std::vector<unsigned char>& pixels, int width, int height, int channels) {
    std::size_t requiredBytes = 0;
    return TryComputePixelByteCount(width, height, channels, requiredBytes) &&
        pixels.size() >= requiredBytes;
}

void FlipRowsInPlace(std::vector<unsigned char>& pixels, int width, int height, int channels = 4) {
    if (pixels.empty() || width <= 0 || height <= 0 || channels <= 0 ||
        !HasCompletePixelBuffer(pixels, width, height, channels)) {
        return;
    }

    const std::size_t rowStride = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    std::vector<unsigned char> temp(rowStride, 0u);
    for (int y = 0; y < height / 2; ++y) {
        unsigned char* top = pixels.data() + static_cast<std::size_t>(y) * rowStride;
        unsigned char* bottom = pixels.data() + static_cast<std::size_t>(height - 1 - y) * rowStride;
        std::memcpy(temp.data(), top, rowStride);
        std::memcpy(top, bottom, rowStride);
        std::memcpy(bottom, temp.data(), rowStride);
    }
}

std::vector<unsigned char> EncodePngBytesTopLeft(const std::vector<unsigned char>& pixels, int width, int height, int channels) {
    std::vector<unsigned char> encoded;
    if (pixels.empty() || width <= 0 || height <= 0 || channels <= 0 ||
        !HasCompletePixelBuffer(pixels, width, height, channels)) {
        return encoded;
    }
    const std::size_t rowStride = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    if (rowStride > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return encoded;
    }

    auto writeCallback = [](void* context, void* data, int size) {
        auto* bytes = static_cast<std::vector<unsigned char>*>(context);
        const auto* src = static_cast<unsigned char*>(data);
        bytes->insert(bytes->end(), src, src + size);
    };

    stbi_write_png_to_func(writeCallback, &encoded, width, height, channels, pixels.data(), static_cast<int>(rowStride));
    return encoded;
}

std::vector<unsigned char> EncodePngBytesFromBottomLeft(const std::vector<unsigned char>& pixels, int width, int height, int channels) {
    if (pixels.empty() || width <= 0 || height <= 0 || channels <= 0) {
        return {};
    }
    std::vector<unsigned char> topLeftPixels = pixels;
    FlipRowsInPlace(topLeftPixels, width, height, channels);
    return EncodePngBytesTopLeft(topLeftPixels, width, height, channels);
}

std::vector<unsigned char> ResizePixelsNearest(
    const std::vector<unsigned char>& sourcePixels,
    int sourceWidth,
    int sourceHeight,
    int sourceChannels,
    int maxDimension,
    int& outWidth,
    int& outHeight) {
    outWidth = sourceWidth;
    outHeight = sourceHeight;
    if (sourcePixels.empty() || sourceWidth <= 0 || sourceHeight <= 0 || sourceChannels <= 0 ||
        !HasCompletePixelBuffer(sourcePixels, sourceWidth, sourceHeight, sourceChannels)) {
        outWidth = 0;
        outHeight = 0;
        return {};
    }

    if (maxDimension > 0 && std::max(sourceWidth, sourceHeight) > maxDimension) {
        const float scale = static_cast<float>(maxDimension) /
            static_cast<float>(std::max(sourceWidth, sourceHeight));
        outWidth = std::max(1, static_cast<int>(std::round(sourceWidth * scale)));
        outHeight = std::max(1, static_cast<int>(std::round(sourceHeight * scale)));
    }

    std::size_t resizedByteCount = 0;
    if (!TryComputePixelByteCount(outWidth, outHeight, 4, resizedByteCount)) {
        outWidth = 0;
        outHeight = 0;
        return {};
    }

    std::size_t sourceByteCount = 0;
    if (sourceChannels == 4 &&
        outWidth == sourceWidth &&
        outHeight == sourceHeight &&
        TryComputePixelByteCount(sourceWidth, sourceHeight, sourceChannels, sourceByteCount)) {
        return std::vector<unsigned char>(sourcePixels.begin(), sourcePixels.begin() + static_cast<std::ptrdiff_t>(sourceByteCount));
    }

    std::vector<unsigned char> resized(resizedByteCount, 0u);
    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            const int srcX = std::clamp((x * sourceWidth) / std::max(1, outWidth), 0, sourceWidth - 1);
            const int srcY = std::clamp((y * sourceHeight) / std::max(1, outHeight), 0, sourceHeight - 1);
            const std::size_t srcIndex =
                (static_cast<std::size_t>(srcY) * static_cast<std::size_t>(sourceWidth) + static_cast<std::size_t>(srcX)) *
                static_cast<std::size_t>(sourceChannels);
            const std::size_t dstIndex =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(outWidth) + static_cast<std::size_t>(x)) * 4ull;
            const unsigned char* src = sourcePixels.data() + srcIndex;
            unsigned char* dst = resized.data() + dstIndex;
            if (sourceChannels == 1) {
                dst[0] = src[0];
                dst[1] = src[0];
                dst[2] = src[0];
                dst[3] = 255u;
            } else if (sourceChannels == 2) {
                dst[0] = src[0];
                dst[1] = src[0];
                dst[2] = src[0];
                dst[3] = src[1];
            } else if (sourceChannels == 3) {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = 255u;
            } else {
                std::memcpy(dst, src, 4u);
            }
        }
    }
    return resized;
}

std::vector<unsigned char> BuildTransparentPixels(int width, int height) {
    std::size_t byteCount = 0;
    if (!TryComputePixelByteCount(width, height, 4, byteCount)) {
        return {};
    }
    return std::vector<unsigned char>(byteCount, 0u);
}

Raw::RawDevelopSettings BuildRawPreviewDevelopSettings(const Raw::RawMetadata& metadata) {
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

std::array<unsigned char, 3> KeyToColor(const std::string& key) {
    const std::uint64_t hash = HashString64(key);
    const float hue = static_cast<float>(hash % 360u) / 360.0f;
    const float sat = 0.24f + static_cast<float>((hash >> 9u) % 30u) / 100.0f;
    const float val = 0.32f + static_cast<float>((hash >> 17u) % 36u) / 100.0f;
    const float sector = hue * 6.0f;
    const int sectorIndex = static_cast<int>(std::floor(sector)) % 6;
    const float fraction = sector - std::floor(sector);
    const float p = val * (1.0f - sat);
    const float q = val * (1.0f - sat * fraction);
    const float t = val * (1.0f - sat * (1.0f - fraction));
    float r = val;
    float g = t;
    float b = p;
    switch (sectorIndex) {
        case 0: r = val; g = t; b = p; break;
        case 1: r = q; g = val; b = p; break;
        case 2: r = p; g = val; b = t; break;
        case 3: r = p; g = q; b = val; break;
        case 4: r = t; g = p; b = val; break;
        case 5: default: r = val; g = p; b = q; break;
    }
    return {
        static_cast<unsigned char>(std::clamp(r, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(g, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(b, 0.0f, 1.0f) * 255.0f)
    };
}

std::vector<unsigned char> BuildFallbackCardPixels(const std::string& previewKey) {
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(kFallbackCardWidth) * static_cast<std::size_t>(kFallbackCardHeight) * 4ull,
        0u);
    const auto base = KeyToColor(previewKey);
    const float baseR = base[0] / 255.0f;
    const float baseG = base[1] / 255.0f;
    const float baseB = base[2] / 255.0f;
    for (int y = 0; y < kFallbackCardHeight; ++y) {
        for (int x = 0; x < kFallbackCardWidth; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(std::max(1, kFallbackCardWidth - 1));
            const float v = static_cast<float>(y) / static_cast<float>(std::max(1, kFallbackCardHeight - 1));
            const float radialA = std::sqrt((u - 0.22f) * (u - 0.22f) + (v - 0.28f) * (v - 0.28f));
            const float radialB = std::sqrt((u - 0.78f) * (u - 0.70f) + (v - 0.72f) * (v - 0.72f));
            const float stripe = 0.5f + 0.5f * std::sin((u * 9.0f + v * 5.0f) * 3.1415926f);
            const float glowA = std::clamp(1.0f - radialA * 2.4f, 0.0f, 1.0f);
            const float glowB = std::clamp(1.0f - radialB * 2.2f, 0.0f, 1.0f);
            const float accent = std::clamp(glowA * 0.52f + glowB * 0.38f + stripe * 0.10f, 0.0f, 1.0f);
            const float bg = 0.10f + 0.04f * (1.0f - v);
            const float r = std::clamp(bg + baseR * 0.22f + accent * 0.30f, 0.0f, 1.0f);
            const float g = std::clamp(bg + baseG * 0.18f + accent * 0.22f, 0.0f, 1.0f);
            const float b = std::clamp(bg + baseB * 0.28f + accent * 0.32f, 0.0f, 1.0f);
            const std::size_t index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(kFallbackCardWidth) + static_cast<std::size_t>(x)) * 4ull;
            pixels[index + 0] = static_cast<unsigned char>(r * 255.0f);
            pixels[index + 1] = static_cast<unsigned char>(g * 255.0f);
            pixels[index + 2] = static_cast<unsigned char>(b * 255.0f);
            pixels[index + 3] = 255u;
        }
    }

    for (int y = 18; y < kFallbackCardHeight - 18; ++y) {
        for (int x = 18; x < kFallbackCardWidth - 18; ++x) {
            const bool onBand = std::abs((x - y) - 24) < 6 || std::abs((x + y) - (kFallbackCardWidth + 12)) < 5;
            if (!onBand) {
                continue;
            }
            const std::size_t index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(kFallbackCardWidth) + static_cast<std::size_t>(x)) * 4ull;
            pixels[index + 0] = static_cast<unsigned char>(std::min(255, static_cast<int>(pixels[index + 0]) + 20));
            pixels[index + 1] = static_cast<unsigned char>(std::min(255, static_cast<int>(pixels[index + 1]) + 20));
            pixels[index + 2] = static_cast<unsigned char>(std::min(255, static_cast<int>(pixels[index + 2]) + 20));
        }
    }

    return pixels;
}

std::vector<unsigned char> BuildFallbackCardPng(const std::string& previewKey) {
    return EncodePngBytesTopLeft(BuildFallbackCardPixels(previewKey), kFallbackCardWidth, kFallbackCardHeight, 4);
}

void ApplyCuratedLayerPreviewRecipe(LayerType layerType, nlohmann::json& layerJson) {
    switch (layerType) {
        case LayerType::Brightness:
            layerJson["brightness"] = 0.28f;
            break;
        case LayerType::Contrast:
            layerJson["contrast"] = 1.38f;
            break;
        case LayerType::Saturation:
            layerJson["saturation"] = 1.55f;
            break;
        case LayerType::Warmth:
            layerJson["warmth"] = 0.24f;
            break;
        case LayerType::Sharpen:
            layerJson["sharpening"] = 0.74f;
            layerJson["sharpenThreshold"] = 0.12f;
            break;
        case LayerType::ToneCurve: {
            nlohmann::json points = nlohmann::json::array();
            points.push_back({ { "x", 0.0f }, { "y", 0.0f }, { "shape", 0 } });
            points.push_back({ { "x", 0.22f }, { "y", 0.15f }, { "shape", 0 } });
            points.push_back({ { "x", 0.50f }, { "y", 0.54f }, { "shape", 0 } });
            points.push_back({ { "x", 0.78f }, { "y", 0.86f }, { "shape", 0 } });
            points.push_back({ { "x", 1.0f }, { "y", 1.0f }, { "shape", 0 } });
            layerJson["points"] = points;
            layerJson["preparedPoints"] = points;
            layerJson["autoCalibratePending"] = false;
            break;
        }
        default:
            if (layerJson.contains("amount")) {
                layerJson["amount"] = 0.72f;
            }
            break;
    }
}

ColorLut::LutPayload BuildSampleLutPayload() {
    ColorLut::LutPayload payload;
    payload.label = "Catalog Preview LUT";
    payload.importedTitle = "Catalog Preview LUT";
    payload.importFormat = ColorLut::LutImportFormat::Cube;
    payload.useMode = ColorLut::LutUseMode::PostViewTransform;
    payload.inputTransform = ColorLut::DefaultInputTransformForMode(payload.useMode);
    payload.outputTransform = ColorLut::DefaultOutputTransformForMode(payload.useMode);
    payload.lut1D.size = 17;
    payload.lut1D.values.reserve(static_cast<std::size_t>(payload.lut1D.size) * 3u);
    for (int index = 0; index < payload.lut1D.size; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(payload.lut1D.size - 1);
        const float curve = std::clamp((t * t * (3.0f - 2.0f * t)) * 1.04f, 0.0f, 1.0f);
        payload.lut1D.values.push_back(std::clamp(curve * 1.03f, 0.0f, 1.0f));
        payload.lut1D.values.push_back(std::clamp(curve, 0.0f, 1.0f));
        payload.lut1D.values.push_back(std::clamp(curve * 0.93f + 0.02f, 0.0f, 1.0f));
    }
    return payload;
}

RenderGraphNode BuildRenderNodeFromPrototype(const EditorNodeGraph::Node& node) {
    RenderGraphNode renderNode;
    renderNode.kind = RenderGraphNodeKind::Image;
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Image:
            renderNode.kind = RenderGraphNodeKind::Image;
            renderNode.image.pixels = MakeSharedPixelBufferCopy(node.image.pixels);
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
        case EditorNodeGraph::NodeKind::RawDecode:
            renderNode.kind = RenderGraphNodeKind::RawDecode;
            renderNode.rawDecode.settings = node.rawDecode.settings;
            break;
        case EditorNodeGraph::NodeKind::RawDevelop:
            renderNode.kind = RenderGraphNodeKind::RawDevelop;
            renderNode.rawDevelop.settings = node.rawDevelop.settings;
            renderNode.rawDevelop.scenePrepEnabled = node.rawDevelop.scenePrepEnabled;
            renderNode.rawDevelop.scenePrepSettings = node.rawDevelop.scenePrepSettings;
            renderNode.rawDevelop.integratedToneEnabled = node.rawDevelop.integratedToneEnabled;
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
            if (std::shared_ptr<LayerBase> layer = LayerRegistry::CreateLayer(node.layerType)) {
                renderNode.layerJson = layer->Serialize();
            }
            break;
        case EditorNodeGraph::NodeKind::Output:
            renderNode.kind = RenderGraphNodeKind::Output;
            break;
        case EditorNodeGraph::NodeKind::MaskGenerator:
            renderNode.kind = RenderGraphNodeKind::MaskGenerator;
            switch (node.maskKind) {
                case EditorNodeGraph::MaskGeneratorKind::LinearGradient: renderNode.maskKind = RenderMaskGeneratorKind::LinearGradient; break;
                case EditorNodeGraph::MaskGeneratorKind::RadialGradient: renderNode.maskKind = RenderMaskGeneratorKind::RadialGradient; break;
                case EditorNodeGraph::MaskGeneratorKind::Noise: renderNode.maskKind = RenderMaskGeneratorKind::Noise; break;
                case EditorNodeGraph::MaskGeneratorKind::Solid:
                default: renderNode.maskKind = RenderMaskGeneratorKind::Solid; break;
            }
            renderNode.maskSettings.value = node.maskSettings.value;
            renderNode.maskSettings.angle = node.maskSettings.angle;
            renderNode.maskSettings.offset = node.maskSettings.offset;
            renderNode.maskSettings.scale = node.maskSettings.scale;
            renderNode.maskSettings.centerX = node.maskSettings.centerX;
            renderNode.maskSettings.centerY = node.maskSettings.centerY;
            renderNode.maskSettings.radius = node.maskSettings.radius;
            renderNode.maskSettings.feather = node.maskSettings.feather;
            renderNode.maskSettings.invert = node.maskSettings.invert;
            break;
        case EditorNodeGraph::NodeKind::MaskCombine:
            renderNode.kind = RenderGraphNodeKind::MaskCombine;
            switch (node.maskCombineMode) {
                case EditorNodeGraph::MaskCombineMode::Add: renderNode.maskCombineMode = RenderMaskCombineMode::Add; break;
                case EditorNodeGraph::MaskCombineMode::Subtract: renderNode.maskCombineMode = RenderMaskCombineMode::Subtract; break;
                case EditorNodeGraph::MaskCombineMode::Exclude: renderNode.maskCombineMode = RenderMaskCombineMode::Exclude; break;
                case EditorNodeGraph::MaskCombineMode::Intersect:
                default: renderNode.maskCombineMode = RenderMaskCombineMode::Intersect; break;
            }
            break;
        case EditorNodeGraph::NodeKind::Mix:
            renderNode.kind = RenderGraphNodeKind::Mix;
            renderNode.mixBlendMode = static_cast<RenderMixBlendMode>(node.mixBlendMode);
            renderNode.mixFactor = node.mixFactor;
            break;
        case EditorNodeGraph::NodeKind::MaskUtility:
            renderNode.kind = RenderGraphNodeKind::MaskUtility;
            renderNode.maskUtilityKind = static_cast<RenderMaskUtilityKind>(node.maskUtilityKind);
            renderNode.maskUtilitySettings.blackPoint = node.maskUtilitySettings.blackPoint;
            renderNode.maskUtilitySettings.whitePoint = node.maskUtilitySettings.whitePoint;
            renderNode.maskUtilitySettings.gamma = node.maskUtilitySettings.gamma;
            renderNode.maskUtilitySettings.threshold = node.maskUtilitySettings.threshold;
            renderNode.maskUtilitySettings.softness = node.maskUtilitySettings.softness;
            renderNode.maskUtilitySettings.invert = node.maskUtilitySettings.invert;
            break;
        case EditorNodeGraph::NodeKind::ImageToMask:
            renderNode.kind = RenderGraphNodeKind::ImageToMask;
            renderNode.imageToMaskKind = static_cast<RenderImageToMaskKind>(node.imageToMaskKind);
            renderNode.imageToMaskSettings.low = node.imageToMaskSettings.low;
            renderNode.imageToMaskSettings.high = node.imageToMaskSettings.high;
            renderNode.imageToMaskSettings.softness = node.imageToMaskSettings.softness;
            renderNode.imageToMaskSettings.invert = node.imageToMaskSettings.invert;
            renderNode.imageToMaskSettings.sampleCount = node.imageToMaskSettings.sampleCount;
            renderNode.imageToMaskSettings.sampleLuma = node.imageToMaskSettings.sampleLuma;
            renderNode.imageToMaskSettings.sampleU = node.imageToMaskSettings.sampleU;
            renderNode.imageToMaskSettings.sampleV = node.imageToMaskSettings.sampleV;
            renderNode.imageToMaskSettings.toneSimilarity = node.imageToMaskSettings.toneSimilarity;
            renderNode.imageToMaskSettings.colorSimilarity = node.imageToMaskSettings.colorSimilarity;
            renderNode.imageToMaskSettings.regionRadius = node.imageToMaskSettings.regionRadius;
            renderNode.imageToMaskSettings.regionFeather = node.imageToMaskSettings.regionFeather;
            renderNode.imageToMaskSettings.edgeSensitivity = node.imageToMaskSettings.edgeSensitivity;
            renderNode.imageToMaskSettings.localCoherence = node.imageToMaskSettings.localCoherence;
            std::memcpy(renderNode.imageToMaskSettings.sampleRgb, node.imageToMaskSettings.sampleRgb, sizeof(node.imageToMaskSettings.sampleRgb));
            std::memcpy(renderNode.imageToMaskSettings.extraSampleRgb, node.imageToMaskSettings.extraSampleRgb, sizeof(node.imageToMaskSettings.extraSampleRgb));
            std::memcpy(renderNode.imageToMaskSettings.extraSampleLuma, node.imageToMaskSettings.extraSampleLuma, sizeof(node.imageToMaskSettings.extraSampleLuma));
            break;
        case EditorNodeGraph::NodeKind::ImageGenerator:
            renderNode.kind = RenderGraphNodeKind::ImageGenerator;
            renderNode.imageGeneratorKind = static_cast<RenderImageGeneratorKind>(node.imageGeneratorKind);
            renderNode.imageGeneratorSettings.angle = node.imageGeneratorSettings.angle;
            renderNode.imageGeneratorSettings.offset = node.imageGeneratorSettings.offset;
            renderNode.imageGeneratorSettings.text = node.imageGeneratorSettings.text;
            renderNode.imageGeneratorSettings.fontSize = node.imageGeneratorSettings.fontSize;
            renderNode.imageGeneratorSettings.textBackdropBlur = node.imageGeneratorSettings.textBackdropBlur;
            renderNode.imageGeneratorSettings.textBackdropOpacity = node.imageGeneratorSettings.textBackdropOpacity;
            renderNode.imageGeneratorSettings.textBackdropPadding = node.imageGeneratorSettings.textBackdropPadding;
            std::memcpy(renderNode.imageGeneratorSettings.colorA, node.imageGeneratorSettings.colorA, sizeof(node.imageGeneratorSettings.colorA));
            std::memcpy(renderNode.imageGeneratorSettings.colorB, node.imageGeneratorSettings.colorB, sizeof(node.imageGeneratorSettings.colorB));
            break;
        case EditorNodeGraph::NodeKind::ChannelSplit:
            renderNode.kind = RenderGraphNodeKind::ChannelSplit;
            break;
        case EditorNodeGraph::NodeKind::ChannelCombine:
            renderNode.kind = RenderGraphNodeKind::ChannelCombine;
            break;
        case EditorNodeGraph::NodeKind::CustomMask:
            renderNode.kind = RenderGraphNodeKind::CustomMask;
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
        case EditorNodeGraph::NodeKind::Preview:
        case EditorNodeGraph::NodeKind::Scope:
        case EditorNodeGraph::NodeKind::Composite:
            break;
    }
    return renderNode;
}

} // namespace

bool EditorModule::GetNodeBrowserThumbnailView(const std::string& previewKey, NodeBrowserThumbnailView& outView) const {
    outView = {};
    const auto it = m_NodeBrowserThumbnailEntries.find(previewKey);
    if (it == m_NodeBrowserThumbnailEntries.end()) {
        return false;
    }
    outView.pngBytes = it->second.pngBytes.empty() ? nullptr : &it->second.pngBytes;
    outView.decodedPixels = it->second.decodedPixels.empty() ? nullptr : &it->second.decodedPixels;
    outView.width = it->second.width;
    outView.height = it->second.height;
    outView.channels = it->second.channels;
    outView.revision = it->second.revision;
    outView.pending = it->second.pending;
    outView.fallback = it->second.fallback;
    return true;
}

std::vector<StackBinaryFormat::NodeBrowserThumbnailEntry> EditorModule::GetPersistedNodeBrowserThumbnails() const {
    std::vector<StackBinaryFormat::NodeBrowserThumbnailEntry> entries;
    entries.reserve(m_NodeBrowserThumbnailEntries.size());
    for (const auto& entry : m_NodeBrowserThumbnailEntries) {
        if (entry.second.pngBytes.empty()) {
            continue;
        }
        StackBinaryFormat::NodeBrowserThumbnailEntry persisted;
        persisted.previewKey = entry.first;
        persisted.previewSeedHash = entry.second.previewSeedHash;
        persisted.previewRecipeVersion = entry.second.previewRecipeVersion;
        persisted.pngBytes = entry.second.pngBytes;
        entries.push_back(std::move(persisted));
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.previewKey < b.previewKey;
    });
    return entries;
}

void EditorModule::ResetNodeBrowserThumbnailState() {
    ++m_NodeBrowserThumbnailGeneration;
    m_NodeBrowserThumbnailEntries.clear();
    m_NodeBrowserPreviewRequestMeta.clear();
    m_NodeBrowserThumbnailWarmPendingEntries = 0;
    m_NodeBrowserThumbnailPendingEntries = 0;
    m_NodeBrowserThumbnailBatchHasChanges = false;
    m_NodeBrowserThumbnailGenerationQueued = false;
    m_NodeBrowserThumbnailSeedHash.clear();
}

void EditorModule::WarmNodeBrowserThumbnailPixelsAsync() {
    struct PendingDecode {
        std::string previewKey;
        std::uint64_t revision = 0;
        std::vector<unsigned char> pngBytes;
    };

    std::vector<PendingDecode> pending;
    pending.reserve(m_NodeBrowserThumbnailEntries.size());
    for (const auto& [previewKey, entry] : m_NodeBrowserThumbnailEntries) {
        if (entry.pngBytes.empty() || !entry.decodedPixels.empty()) {
            continue;
        }
        PendingDecode decode;
        decode.previewKey = previewKey;
        decode.revision = entry.revision;
        decode.pngBytes = entry.pngBytes;
        pending.push_back(std::move(decode));
    }
    if (pending.empty()) {
        m_NodeBrowserThumbnailWarmPendingEntries = 0;
        return;
    }

    const std::uint64_t generation = m_NodeBrowserThumbnailGeneration;
    m_NodeBrowserThumbnailWarmPendingEntries = pending.size();
    Async::TaskSystem::Get().Submit([this, generation, pending = std::move(pending)]() mutable {
        struct DecodedThumbnail {
            std::string previewKey;
            std::uint64_t revision = 0;
            std::vector<unsigned char> pixels;
            int width = 0;
            int height = 0;
            int channels = 4;
        };

        std::vector<DecodedThumbnail> decoded;
        decoded.reserve(pending.size());
        for (auto& item : pending) {
            DecodedThumbnail thumb;
            thumb.previewKey = std::move(item.previewKey);
            thumb.revision = item.revision;
            if (!LibraryManager::DecodeImageBytes(item.pngBytes, thumb.pixels, thumb.width, thumb.height, thumb.channels)) {
                thumb.pixels = BuildFallbackCardPixels(thumb.previewKey);
                thumb.width = kFallbackCardWidth;
                thumb.height = kFallbackCardHeight;
                thumb.channels = 4;
            }
            decoded.push_back(std::move(thumb));
        }

        Async::TaskSystem::Get().PostToMain([this, generation, decoded = std::move(decoded)]() mutable {
            if (generation != m_NodeBrowserThumbnailGeneration) {
                return;
            }
            std::size_t processedCount = 0;
            for (auto& thumb : decoded) {
                auto it = m_NodeBrowserThumbnailEntries.find(thumb.previewKey);
                if (it != m_NodeBrowserThumbnailEntries.end() && it->second.revision == thumb.revision) {
                    it->second.decodedPixels = std::move(thumb.pixels);
                    it->second.width = thumb.width;
                    it->second.height = thumb.height;
                    it->second.channels = thumb.channels;
                }
                ++processedCount;
            }
            if (m_NodeBrowserThumbnailWarmPendingEntries >= processedCount) {
                m_NodeBrowserThumbnailWarmPendingEntries -= processedCount;
            } else {
                m_NodeBrowserThumbnailWarmPendingEntries = 0;
            }
        });
    });
}

EditorModule::NodeBrowserPreviewSeed EditorModule::ResolveNodeBrowserPreviewSeed() const {
    auto makeBlankSeed = []() {
        NodeBrowserPreviewSeed blank;
        blank.kind = NodeBrowserPreviewSeed::Kind::None;
        blank.width = 768;
        blank.height = 512;
        blank.channels = 4;
        blank.pixels = BuildTransparentPixels(blank.width, blank.height);
        blank.seedHash = "blank";
        return blank;
    };

    NodeBrowserPreviewSeed seed;
    const EditorNodeGraph::Node* activeNode = m_NodeGraph.FindNode(m_NodeGraph.GetActiveImageNodeId());

    if (activeNode && activeNode->kind == EditorNodeGraph::NodeKind::Image) {
        std::vector<unsigned char> pixels = activeNode->image.pixels;
        int width = activeNode->image.width;
        int height = activeNode->image.height;
        int channels = std::max(1, activeNode->image.channels);
        if (pixels.empty() && !activeNode->image.pngBytes.empty()) {
            LibraryManager::DecodeImageBytes(activeNode->image.pngBytes, pixels, width, height, channels);
        }
        if (!pixels.empty() && width > 0 && height > 0) {
            int resizedW = width;
            int resizedH = height;
            seed.pixels = ResizePixelsNearest(pixels, width, height, channels, 1024, resizedW, resizedH);
            if (!seed.pixels.empty() && resizedW > 0 && resizedH > 0) {
                seed.kind = NodeBrowserPreviewSeed::Kind::Image;
                seed.width = resizedW;
                seed.height = resizedH;
                seed.channels = 4;
                const std::uint64_t hash = !activeNode->image.pngBytes.empty()
                    ? HashBytes64(activeNode->image.pngBytes.data(), activeNode->image.pngBytes.size())
                    : HashBytes64(seed.pixels.data(), seed.pixels.size());
                seed.seedHash = "image:" + HexString64(hash);
                return seed;
            }
        }
    }

    if (activeNode && activeNode->kind == EditorNodeGraph::NodeKind::RawSource) {
        seed.kind = NodeBrowserPreviewSeed::Kind::Raw;
        seed.rawSource = activeNode->rawSource;
        const int width = std::max(1, Raw::DisplayWidth(activeNode->rawSource.metadata));
        const int height = std::max(1, Raw::DisplayHeight(activeNode->rawSource.metadata));
        const int maxDimension = 1024;
        if (width >= height) {
            seed.width = maxDimension;
            seed.height = std::max(1, static_cast<int>(std::round(
                static_cast<float>(height) * (static_cast<float>(maxDimension) / static_cast<float>(width)))));
        } else {
            seed.height = maxDimension;
            seed.width = std::max(1, static_cast<int>(std::round(
                static_cast<float>(width) * (static_cast<float>(maxDimension) / static_cast<float>(height)))));
        }
        seed.channels = 4;
        seed.pixels = BuildTransparentPixels(seed.width, seed.height);
        std::string rawIdentity = activeNode->rawSource.sourcePath;
        rawIdentity += "|" + std::to_string(activeNode->rawSource.metadata.rawWidth);
        rawIdentity += "|" + std::to_string(activeNode->rawSource.metadata.rawHeight);
        rawIdentity += "|" + std::to_string(activeNode->rawSource.metadata.visibleWidth);
        rawIdentity += "|" + std::to_string(activeNode->rawSource.metadata.visibleHeight);
        rawIdentity += "|" + activeNode->rawSource.metadata.cameraMake;
        rawIdentity += "|" + activeNode->rawSource.metadata.cameraModel;
        seed.seedHash = "raw:" + HexString64(HashString64(rawIdentity));
        return seed;
    }

    const int sourceW = m_Pipeline.GetCanvasWidth();
    const int sourceH = m_Pipeline.GetCanvasHeight();
    const std::vector<unsigned char>& pipelineSource = m_Pipeline.GetSourcePixelsRaw();
    if (!pipelineSource.empty() && sourceW > 0 && sourceH > 0) {
        int resizedW = sourceW;
        int resizedH = sourceH;
        seed.pixels = ResizePixelsNearest(pipelineSource, sourceW, sourceH, 4, 1024, resizedW, resizedH);
        if (!seed.pixels.empty() && resizedW > 0 && resizedH > 0) {
            seed.kind = NodeBrowserPreviewSeed::Kind::Image;
            seed.width = resizedW;
            seed.height = resizedH;
            seed.channels = 4;
            seed.seedHash = "pipeline:" + HexString64(HashBytes64(seed.pixels.data(), seed.pixels.size()));
            return seed;
        }
    }

    return makeBlankSeed();
}

void EditorModule::EnsureNodeBrowserThumbnailCatalog() {
    const auto& entries = CachedNodeBrowserEntries();
    const NodeBrowserPreviewSeed seed = ResolveNodeBrowserPreviewSeed();
    bool needsRefresh = seed.seedHash != m_NodeBrowserThumbnailSeedHash;
    for (const auto& entry : entries) {
        const auto it = m_NodeBrowserThumbnailEntries.find(entry.previewKey);
        const bool suppressedPreviewStillStored =
            entry.previewStrategy == EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::NoPreview &&
            it != m_NodeBrowserThumbnailEntries.end() &&
            !it->second.pngBytes.empty();
        if (suppressedPreviewStillStored ||
            it == m_NodeBrowserThumbnailEntries.end() ||
            (entry.previewStrategy != EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::NoPreview &&
             it->second.pngBytes.empty()) ||
            it->second.previewSeedHash != seed.seedHash ||
            it->second.previewRecipeVersion != entry.previewRecipeVersion) {
            needsRefresh = true;
            break;
        }
    }

    if (!needsRefresh) {
        return;
    }

    StartNodeBrowserThumbnailGeneration(false);
}

void EditorModule::MarkNodeBrowserThumbnailSourceChanged() {
    StartNodeBrowserThumbnailGeneration(true);
}

void EditorModule::FinalizeNodeBrowserThumbnailBatch(std::uint64_t generation) {
    if (generation != m_NodeBrowserThumbnailGeneration || m_NodeBrowserThumbnailPendingEntries != 0) {
        return;
    }
    m_NodeBrowserThumbnailGenerationQueued = false;
    if (m_NodeBrowserThumbnailBatchHasChanges && !m_CurrentProjectFileName.empty()) {
        LibraryManager::Get().RequestPersistNodeBrowserThumbnails(
            m_CurrentProjectFileName,
            GetPersistedNodeBrowserThumbnails());
    }
}

void EditorModule::StartNodeBrowserThumbnailGeneration(bool forceRefresh) {
    const auto& entries = CachedNodeBrowserEntries();
    const NodeBrowserPreviewSeed seed = ResolveNodeBrowserPreviewSeed();
    const bool seedChanged = seed.seedHash != m_NodeBrowserThumbnailSeedHash;
    if (!forceRefresh && !seedChanged && m_NodeBrowserThumbnailGenerationQueued && m_NodeBrowserThumbnailPendingEntries > 0) {
        return;
    }

    std::vector<EditorNodeGraphDefinitions::NodeCatalogEntry> pendingEntries;
    pendingEntries.reserve(entries.size());
    bool clearedSuppressedEntries = false;
    for (const auto& entry : entries) {
        const auto it = m_NodeBrowserThumbnailEntries.find(entry.previewKey);
        if (entry.previewStrategy == EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::NoPreview) {
            NodeBrowserThumbnailRuntimeEntry& runtimeEntry = m_NodeBrowserThumbnailEntries[entry.previewKey];
            const bool hadPixels = !runtimeEntry.pngBytes.empty();
            runtimeEntry.previewSeedHash = seed.seedHash;
            runtimeEntry.previewRecipeVersion = entry.previewRecipeVersion;
            runtimeEntry.pngBytes.clear();
            runtimeEntry.decodedPixels.clear();
            runtimeEntry.width = 0;
            runtimeEntry.height = 0;
            runtimeEntry.channels = 4;
            runtimeEntry.pending = false;
            runtimeEntry.fallback = false;
            if (hadPixels) {
                runtimeEntry.revision = m_NodeBrowserThumbnailRevisionCounter++;
                clearedSuppressedEntries = true;
            }
            continue;
        }
        const bool stale = forceRefresh ||
            it == m_NodeBrowserThumbnailEntries.end() ||
            it->second.pngBytes.empty() ||
            it->second.previewSeedHash != seed.seedHash ||
            it->second.previewRecipeVersion != entry.previewRecipeVersion;
        if (stale) {
            pendingEntries.push_back(entry);
            NodeBrowserThumbnailRuntimeEntry& runtimeEntry = m_NodeBrowserThumbnailEntries[entry.previewKey];
            runtimeEntry.pending = true;
        }
    }

    m_NodeBrowserThumbnailSeedHash = seed.seedHash;
    if (pendingEntries.empty()) {
        m_NodeBrowserThumbnailGenerationQueued = false;
        m_NodeBrowserThumbnailPendingEntries = 0;
        if (clearedSuppressedEntries && !m_CurrentProjectFileName.empty()) {
            LibraryManager::Get().RequestPersistNodeBrowserThumbnails(
                m_CurrentProjectFileName,
                GetPersistedNodeBrowserThumbnails());
        }
        return;
    }

    ++m_NodeBrowserThumbnailGeneration;
    const std::uint64_t generation = m_NodeBrowserThumbnailGeneration;
    m_NodeBrowserThumbnailGenerationQueued = true;
    m_NodeBrowserThumbnailPendingEntries = pendingEntries.size();
    m_NodeBrowserThumbnailBatchHasChanges = clearedSuppressedEntries;
    m_NodeBrowserPreviewRequestMeta.clear();

    EditorRenderWorker::Snapshot snapshot;
    snapshot.generation = generation;
    snapshot.outputConnected = false;
    snapshot.sourcePixels = MakeSharedPixelBufferCopy(seed.pixels);
    snapshot.width = seed.width;
    snapshot.height = seed.height;
    snapshot.channels = std::max(1, seed.channels);

    std::vector<EditorNodeGraphDefinitions::NodeCatalogEntry> fallbackEntries;
    fallbackEntries.reserve(pendingEntries.size());

    int nextNodeId = 1;
    int nextPreviewId = 100000;
    int sharedImageSourceNodeId = -1;
    int sharedRawSourceNodeId = -1;
    int sharedRawDecodeNodeId = -1;

    auto addNode = [&](RenderGraphNode node) {
        node.nodeId = nextNodeId++;
        snapshot.graph.nodes.push_back(std::move(node));
        return snapshot.graph.nodes.back().nodeId;
    };
    auto addLink = [&](int fromNodeId, const char* fromSocketId, int toNodeId, const char* toSocketId) {
        snapshot.graph.links.push_back(RenderGraphLink { fromNodeId, fromSocketId, toNodeId, toSocketId });
    };
    auto addRequest = [&](const EditorNodeGraphDefinitions::NodeCatalogEntry& entry, int sourceNodeId, const char* socketId, bool maskOutput) {
        EditorRenderWorker::PreviewRequest request;
        request.previewNodeId = nextPreviewId++;
        request.sourceNodeId = sourceNodeId;
        request.sourceSocketId = socketId;
        request.maskInput = maskOutput;
        request.directSourceOutput = true;
        request.dirtyGeneration = generation;
        snapshot.previews.push_back(std::move(request));
        m_NodeBrowserPreviewRequestMeta[snapshot.previews.back().previewNodeId] =
            NodeBrowserPreviewRequestMeta { entry.previewKey, entry.previewRecipeVersion };
    };

    auto ensureSharedImageSource = [&]() -> int {
        if (sharedImageSourceNodeId > 0) {
            return sharedImageSourceNodeId;
        }
        if (seed.kind != NodeBrowserPreviewSeed::Kind::Image || seed.pixels.empty()) {
            return -1;
        }
        RenderGraphNode sourceNode;
        sourceNode.kind = RenderGraphNodeKind::Image;
        sourceNode.image.pixels = MakeSharedPixelBufferCopy(seed.pixels);
        sourceNode.image.width = seed.width;
        sourceNode.image.height = seed.height;
        sourceNode.image.channels = seed.channels;
        sharedImageSourceNodeId = addNode(std::move(sourceNode));
        return sharedImageSourceNodeId;
    };

    auto ensureSharedRawSource = [&]() -> int {
        if (sharedRawSourceNodeId > 0) {
            return sharedRawSourceNodeId;
        }
        if (seed.kind != NodeBrowserPreviewSeed::Kind::Raw) {
            return -1;
        }
        RenderGraphNode rawSourceNode;
        rawSourceNode.kind = RenderGraphNodeKind::RawSource;
        rawSourceNode.rawSource.sourcePath = seed.rawSource.sourcePath;
        rawSourceNode.rawSource.metadata = seed.rawSource.metadata;
        sharedRawSourceNodeId = addNode(std::move(rawSourceNode));
        return sharedRawSourceNodeId;
    };

    auto ensureSharedRawDecode = [&]() -> int {
        if (sharedRawDecodeNodeId > 0) {
            return sharedRawDecodeNodeId;
        }
        const int rawSourceNodeId = ensureSharedRawSource();
        if (rawSourceNodeId <= 0) {
            return -1;
        }
        EditorNodeGraph::Node prototype = EditorNodeGraphDefinitions::BuildPrototypeNode(
            EditorNodeGraphDefinitions::NodeCatalogEntry {
                EditorNodeGraph::NodeKind::RawDecode,
                0,
                "RAW Decode",
                "Input / Output",
                "raw-decode",
                1,
                EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::Auto
            });
        prototype.rawDecode.settings = BuildRawPreviewDevelopSettings(seed.rawSource.metadata);
        RenderGraphNode rawDecodeNode = BuildRenderNodeFromPrototype(prototype);
        sharedRawDecodeNodeId = addNode(std::move(rawDecodeNode));
        addLink(rawSourceNodeId, EditorNodeGraph::kRawOutputSocketId, sharedRawDecodeNodeId, EditorNodeGraph::kRawInputSocketId);
        return sharedRawDecodeNodeId;
    };

    auto resolveSharedImageInputNode = [&]() -> int {
        if (seed.kind == NodeBrowserPreviewSeed::Kind::Image) {
            return ensureSharedImageSource();
        }
        if (seed.kind == NodeBrowserPreviewSeed::Kind::Raw) {
            return ensureSharedRawDecode();
        }
        return -1;
    };

    for (const auto& entry : pendingEntries) {
        if (entry.previewStrategy == EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::FallbackOnly) {
            fallbackEntries.push_back(entry);
            continue;
        }

        EditorNodeGraph::Node prototype = EditorNodeGraphDefinitions::BuildPrototypeNode(entry);
        bool renderable = false;

        switch (entry.kind) {
            case EditorNodeGraph::NodeKind::Layer: {
                const int inputNodeId = resolveSharedImageInputNode();
                if (inputNodeId <= 0) {
                    break;
                }
                RenderGraphNode node = BuildRenderNodeFromPrototype(prototype);
                ApplyCuratedLayerPreviewRecipe(prototype.layerType, node.layerJson);
                const int nodeId = addNode(std::move(node));
                addLink(inputNodeId, EditorNodeGraph::kImageOutputSocketId, nodeId, EditorNodeGraph::kImageInputSocketId);
                addRequest(entry, nodeId, EditorNodeGraph::kImageOutputSocketId, false);
                renderable = true;
                break;
            }
            case EditorNodeGraph::NodeKind::Lut: {
                const int inputNodeId = resolveSharedImageInputNode();
                if (inputNodeId <= 0) {
                    break;
                }
                prototype.lut = BuildSampleLutPayload();
                RenderGraphNode node = BuildRenderNodeFromPrototype(prototype);
                const int nodeId = addNode(std::move(node));
                addLink(inputNodeId, EditorNodeGraph::kImageOutputSocketId, nodeId, EditorNodeGraph::kImageInputSocketId);
                addRequest(entry, nodeId, EditorNodeGraph::kImageOutputSocketId, false);
                renderable = true;
                break;
            }
            case EditorNodeGraph::NodeKind::MaskGenerator: {
                switch (prototype.maskKind) {
                    case EditorNodeGraph::MaskGeneratorKind::Solid:
                        prototype.maskSettings.value = 0.72f;
                        break;
                    case EditorNodeGraph::MaskGeneratorKind::LinearGradient:
                        prototype.maskSettings.angle = 0.22f;
                        prototype.maskSettings.scale = 1.85f;
                        prototype.maskSettings.offset = -0.14f;
                        break;
                    case EditorNodeGraph::MaskGeneratorKind::RadialGradient:
                        prototype.maskSettings.centerX = 0.42f;
                        prototype.maskSettings.centerY = 0.53f;
                        prototype.maskSettings.radius = 0.34f;
                        prototype.maskSettings.feather = 0.24f;
                        break;
                    case EditorNodeGraph::MaskGeneratorKind::Noise:
                        prototype.maskSettings.scale = 2.25f;
                        prototype.maskSettings.value = 0.92f;
                        break;
                }
                const int nodeId = addNode(BuildRenderNodeFromPrototype(prototype));
                addRequest(entry, nodeId, EditorNodeGraph::kMaskOutputSocketId, true);
                renderable = true;
                break;
            }
            case EditorNodeGraph::NodeKind::MaskUtility: {
                EditorNodeGraph::Node sourceMask = EditorNodeGraphDefinitions::BuildPrototypeNode(
                    EditorNodeGraphDefinitions::NodeCatalogEntry {
                        EditorNodeGraph::NodeKind::MaskGenerator,
                        static_cast<int>(EditorNodeGraph::MaskGeneratorKind::RadialGradient),
                        "Radial Gradient Mask",
                        "Masks",
                        "mask:radial-gradient",
                        1,
                        EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::Auto
                    });
                sourceMask.maskSettings.radius = 0.34f;
                sourceMask.maskSettings.feather = 0.20f;
                sourceMask.maskSettings.centerX = 0.46f;
                sourceMask.maskSettings.centerY = 0.52f;
                const int sourceMaskId = addNode(BuildRenderNodeFromPrototype(sourceMask));
                if (prototype.maskUtilityKind == EditorNodeGraph::MaskUtilityKind::Levels) {
                    prototype.maskUtilitySettings.blackPoint = 0.18f;
                    prototype.maskUtilitySettings.whitePoint = 0.82f;
                    prototype.maskUtilitySettings.gamma = 0.78f;
                } else if (prototype.maskUtilityKind == EditorNodeGraph::MaskUtilityKind::Threshold) {
                    prototype.maskUtilitySettings.threshold = 0.54f;
                    prototype.maskUtilitySettings.softness = 0.16f;
                } else if (prototype.maskUtilityKind == EditorNodeGraph::MaskUtilityKind::Invert) {
                    prototype.maskUtilitySettings.invert = true;
                }
                const int nodeId = addNode(BuildRenderNodeFromPrototype(prototype));
                addLink(sourceMaskId, EditorNodeGraph::kMaskOutputSocketId, nodeId, EditorNodeGraph::kMaskInputSocketId);
                addRequest(entry, nodeId, EditorNodeGraph::kMaskOutputSocketId, true);
                renderable = true;
                break;
            }
            case EditorNodeGraph::NodeKind::MaskCombine: {
                EditorNodeGraph::Node maskA = EditorNodeGraphDefinitions::BuildPrototypeNode(
                    EditorNodeGraphDefinitions::NodeCatalogEntry {
                        EditorNodeGraph::NodeKind::MaskGenerator,
                        static_cast<int>(EditorNodeGraph::MaskGeneratorKind::LinearGradient),
                        "Linear Gradient Mask",
                        "Masks",
                        "mask:linear-gradient",
                        1,
                        EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::Auto
                    });
                maskA.maskSettings.angle = 0.12f;
                maskA.maskSettings.scale = 1.66f;
                EditorNodeGraph::Node maskB = EditorNodeGraphDefinitions::BuildPrototypeNode(
                    EditorNodeGraphDefinitions::NodeCatalogEntry {
                        EditorNodeGraph::NodeKind::MaskGenerator,
                        static_cast<int>(EditorNodeGraph::MaskGeneratorKind::RadialGradient),
                        "Radial Gradient Mask",
                        "Masks",
                        "mask:radial-gradient",
                        1,
                        EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::Auto
                    });
                maskB.maskSettings.radius = 0.30f;
                maskB.maskSettings.feather = 0.16f;
                maskB.maskSettings.centerX = 0.58f;
                maskB.maskSettings.centerY = 0.48f;
                const int maskAId = addNode(BuildRenderNodeFromPrototype(maskA));
                const int maskBId = addNode(BuildRenderNodeFromPrototype(maskB));
                const int nodeId = addNode(BuildRenderNodeFromPrototype(prototype));
                addLink(maskAId, EditorNodeGraph::kMaskOutputSocketId, nodeId, EditorNodeGraph::kMaskCombineInputASocketId);
                addLink(maskBId, EditorNodeGraph::kMaskOutputSocketId, nodeId, EditorNodeGraph::kMaskCombineInputBSocketId);
                addRequest(entry, nodeId, EditorNodeGraph::kMaskOutputSocketId, true);
                renderable = true;
                break;
            }
            case EditorNodeGraph::NodeKind::ImageToMask: {
                const int inputNodeId = resolveSharedImageInputNode();
                if (inputNodeId <= 0) {
                    break;
                }
                if (prototype.imageToMaskKind == EditorNodeGraph::ImageToMaskKind::Luminance) {
                    prototype.imageToMaskSettings.low = 0.22f;
                    prototype.imageToMaskSettings.high = 0.78f;
                    prototype.imageToMaskSettings.softness = 0.14f;
                } else {
                    prototype.imageToMaskSettings.sampleCount = 1;
                    prototype.imageToMaskSettings.sampleLuma = 0.58f;
                    prototype.imageToMaskSettings.toneSimilarity = 0.20f;
                    prototype.imageToMaskSettings.colorSimilarity = 0.26f;
                    prototype.imageToMaskSettings.regionRadius = 0.42f;
                    prototype.imageToMaskSettings.regionFeather = 0.22f;
                    prototype.imageToMaskSettings.edgeSensitivity = 0.30f;
                }
                const int nodeId = addNode(BuildRenderNodeFromPrototype(prototype));
                addLink(inputNodeId, EditorNodeGraph::kImageOutputSocketId, nodeId, EditorNodeGraph::kImageInputSocketId);
                addRequest(entry, nodeId, EditorNodeGraph::kMaskOutputSocketId, true);
                renderable = true;
                break;
            }
            case EditorNodeGraph::NodeKind::ImageGenerator: {
                switch (prototype.imageGeneratorKind) {
                    case EditorNodeGraph::ImageGeneratorKind::SolidColor:
                        prototype.imageGeneratorSettings.colorA[0] = 0.92f;
                        prototype.imageGeneratorSettings.colorA[1] = 0.38f;
                        prototype.imageGeneratorSettings.colorA[2] = 0.32f;
                        prototype.imageGeneratorSettings.colorA[3] = 1.0f;
                        break;
                    case EditorNodeGraph::ImageGeneratorKind::ColorGradient:
                        prototype.imageGeneratorSettings.colorA[0] = 0.08f;
                        prototype.imageGeneratorSettings.colorA[1] = 0.74f;
                        prototype.imageGeneratorSettings.colorA[2] = 0.78f;
                        prototype.imageGeneratorSettings.colorB[0] = 0.95f;
                        prototype.imageGeneratorSettings.colorB[1] = 0.40f;
                        prototype.imageGeneratorSettings.colorB[2] = 0.24f;
                        prototype.imageGeneratorSettings.angle = 0.12f;
                        break;
                    case EditorNodeGraph::ImageGeneratorKind::Square:
                        prototype.imageGeneratorSettings.colorA[0] = 0.96f;
                        prototype.imageGeneratorSettings.colorA[1] = 0.84f;
                        prototype.imageGeneratorSettings.colorA[2] = 0.18f;
                        break;
                    case EditorNodeGraph::ImageGeneratorKind::Circle:
                        prototype.imageGeneratorSettings.colorA[0] = 0.26f;
                        prototype.imageGeneratorSettings.colorA[1] = 0.62f;
                        prototype.imageGeneratorSettings.colorA[2] = 0.98f;
                        break;
                    case EditorNodeGraph::ImageGeneratorKind::Text:
                        prototype.imageGeneratorSettings.text = "A";
                        prototype.imageGeneratorSettings.fontSize = 180.0f;
                        prototype.imageGeneratorSettings.colorA[0] = 0.98f;
                        prototype.imageGeneratorSettings.colorA[1] = 0.89f;
                        prototype.imageGeneratorSettings.colorA[2] = 0.15f;
                        break;
                }
                const int nodeId = addNode(BuildRenderNodeFromPrototype(prototype));
                addRequest(entry, nodeId, EditorNodeGraph::kImageOutputSocketId, false);
                renderable = true;
                break;
            }
            case EditorNodeGraph::NodeKind::DataMath: {
                const int inputNodeId = resolveSharedImageInputNode();
                if (inputNodeId <= 0) {
                    break;
                }
                prototype.dataMathSettings.constantA = 0.0f;
                prototype.dataMathSettings.constantB = 0.18f;
                prototype.dataMathSettings.minValue = 0.15f;
                prototype.dataMathSettings.maxValue = 0.82f;
                prototype.dataMathSettings.outMin = 0.06f;
                prototype.dataMathSettings.outMax = 1.0f;
                switch (prototype.dataMathMode) {
                    case EditorNodeGraph::DataMathMode::Subtract: prototype.dataMathSettings.constantB = 0.16f; break;
                    case EditorNodeGraph::DataMathMode::Multiply: prototype.dataMathSettings.constantB = 1.35f; break;
                    case EditorNodeGraph::DataMathMode::Divide: prototype.dataMathSettings.constantB = 0.80f; break;
                    case EditorNodeGraph::DataMathMode::Average: prototype.dataMathSettings.constantB = 0.64f; break;
                    case EditorNodeGraph::DataMathMode::ImageAverage: prototype.dataMathSettings.constantB = 0.64f; break;
                    case EditorNodeGraph::DataMathMode::Min: prototype.dataMathSettings.constantB = 0.72f; break;
                    case EditorNodeGraph::DataMathMode::Max: prototype.dataMathSettings.constantB = 0.36f; break;
                    case EditorNodeGraph::DataMathMode::Difference: prototype.dataMathSettings.constantB = 0.32f; break;
                    case EditorNodeGraph::DataMathMode::Clamp:
                    case EditorNodeGraph::DataMathMode::Add:
                    case EditorNodeGraph::DataMathMode::Remap:
                        break;
                }
                const int nodeId = addNode(BuildRenderNodeFromPrototype(prototype));
                addLink(inputNodeId, EditorNodeGraph::kImageOutputSocketId, nodeId, EditorNodeGraph::kMixInputASocketId);
                addRequest(entry, nodeId, EditorNodeGraph::kImageOutputSocketId, false);
                renderable = true;
                break;
            }
            case EditorNodeGraph::NodeKind::Mix: {
                const int inputNodeId = resolveSharedImageInputNode();
                if (inputNodeId <= 0) {
                    break;
                }
                prototype.mixBlendMode = EditorNodeGraph::MixBlendMode::Screen;
                prototype.mixFactor = 0.58f;
                EditorNodeGraph::Node colorSource = EditorNodeGraphDefinitions::BuildPrototypeNode(
                    EditorNodeGraphDefinitions::NodeCatalogEntry {
                        EditorNodeGraph::NodeKind::ImageGenerator,
                        static_cast<int>(EditorNodeGraph::ImageGeneratorKind::ColorGradient),
                        "Color Gradient Image",
                        "Texture / Generate",
                        "image-generator:color-gradient",
                        1,
                        EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::Auto
                    });
                colorSource.imageGeneratorSettings.colorA[0] = 0.95f;
                colorSource.imageGeneratorSettings.colorA[1] = 0.40f;
                colorSource.imageGeneratorSettings.colorA[2] = 0.22f;
                colorSource.imageGeneratorSettings.colorB[0] = 0.10f;
                colorSource.imageGeneratorSettings.colorB[1] = 0.66f;
                colorSource.imageGeneratorSettings.colorB[2] = 0.82f;
                colorSource.imageGeneratorSettings.angle = 0.34f;
                EditorNodeGraph::Node factorMask = EditorNodeGraphDefinitions::BuildPrototypeNode(
                    EditorNodeGraphDefinitions::NodeCatalogEntry {
                        EditorNodeGraph::NodeKind::MaskGenerator,
                        static_cast<int>(EditorNodeGraph::MaskGeneratorKind::LinearGradient),
                        "Linear Gradient Mask",
                        "Masks",
                        "mask:linear-gradient",
                        1,
                        EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::Auto
                    });
                factorMask.maskSettings.angle = 0.08f;
                factorMask.maskSettings.scale = 1.30f;
                const int colorSourceId = addNode(BuildRenderNodeFromPrototype(colorSource));
                const int factorMaskId = addNode(BuildRenderNodeFromPrototype(factorMask));
                const int nodeId = addNode(BuildRenderNodeFromPrototype(prototype));
                addLink(inputNodeId, EditorNodeGraph::kImageOutputSocketId, nodeId, EditorNodeGraph::kMixInputASocketId);
                addLink(colorSourceId, EditorNodeGraph::kImageOutputSocketId, nodeId, EditorNodeGraph::kMixInputBSocketId);
                addLink(factorMaskId, EditorNodeGraph::kMaskOutputSocketId, nodeId, EditorNodeGraph::kMixFactorSocketId);
                addRequest(entry, nodeId, EditorNodeGraph::kImageOutputSocketId, false);
                renderable = true;
                break;
            }
            case EditorNodeGraph::NodeKind::ChannelSplit: {
                const int inputNodeId = resolveSharedImageInputNode();
                if (inputNodeId <= 0) {
                    break;
                }
                const int nodeId = addNode(BuildRenderNodeFromPrototype(prototype));
                addLink(inputNodeId, EditorNodeGraph::kImageOutputSocketId, nodeId, EditorNodeGraph::kImageInputSocketId);
                addRequest(entry, nodeId, "r", true);
                renderable = true;
                break;
            }
            case EditorNodeGraph::NodeKind::ChannelCombine: {
                const int inputNodeId = resolveSharedImageInputNode();
                if (inputNodeId <= 0) {
                    break;
                }
                EditorNodeGraph::Node split = EditorNodeGraphDefinitions::BuildPrototypeNode(
                    EditorNodeGraphDefinitions::NodeCatalogEntry {
                        EditorNodeGraph::NodeKind::ChannelSplit,
                        0,
                        "Channel Split",
                        "Channels",
                        "channel-split",
                        1,
                        EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::Auto
                    });
                const int splitId = addNode(BuildRenderNodeFromPrototype(split));
                const int combineId = addNode(BuildRenderNodeFromPrototype(prototype));
                addLink(inputNodeId, EditorNodeGraph::kImageOutputSocketId, splitId, EditorNodeGraph::kImageInputSocketId);
                addLink(splitId, "b", combineId, "r");
                addLink(splitId, "g", combineId, "g");
                addLink(splitId, "r", combineId, "b");
                addLink(splitId, "a", combineId, "a");
                addRequest(entry, combineId, EditorNodeGraph::kImageOutputSocketId, false);
                renderable = true;
                break;
            }
            case EditorNodeGraph::NodeKind::RawDecode: {
                if (seed.kind != NodeBrowserPreviewSeed::Kind::Raw) {
                    break;
                }
                const int rawSourceNodeId = ensureSharedRawSource();
                prototype.rawDecode.settings = BuildRawPreviewDevelopSettings(seed.rawSource.metadata);
                const int nodeId = addNode(BuildRenderNodeFromPrototype(prototype));
                addLink(rawSourceNodeId, EditorNodeGraph::kRawOutputSocketId, nodeId, EditorNodeGraph::kRawInputSocketId);
                addRequest(entry, nodeId, EditorNodeGraph::kImageOutputSocketId, false);
                renderable = true;
                break;
            }
            case EditorNodeGraph::NodeKind::RawDevelop: {
                if (seed.kind != NodeBrowserPreviewSeed::Kind::Raw) {
                    break;
                }
                const int rawSourceNodeId = ensureSharedRawSource();
                prototype.rawDevelop.settings = BuildRawPreviewDevelopSettings(seed.rawSource.metadata);
                prototype.rawDevelop.scenePrepEnabled = false;
                prototype.rawDevelop.integratedToneEnabled = false;
                prototype.rawDevelop.integratedToneLayerJson = nlohmann::json::object();
                const int nodeId = addNode(BuildRenderNodeFromPrototype(prototype));
                addLink(rawSourceNodeId, EditorNodeGraph::kRawOutputSocketId, nodeId, EditorNodeGraph::kRawInputSocketId);
                addRequest(entry, nodeId, EditorNodeGraph::kImageOutputSocketId, false);
                renderable = true;
                break;
            }
            case EditorNodeGraph::NodeKind::RawNeuralDenoise: {
                if (seed.kind != NodeBrowserPreviewSeed::Kind::Raw) {
                    break;
                }
                const int rawSourceNodeId = ensureSharedRawSource();
                const int denoiseNodeId = addNode(BuildRenderNodeFromPrototype(prototype));
                EditorNodeGraph::Node decodePrototype = EditorNodeGraphDefinitions::BuildPrototypeNode(
                    EditorNodeGraphDefinitions::NodeCatalogEntry {
                        EditorNodeGraph::NodeKind::RawDecode,
                        0,
                        "RAW Decode",
                        "Input / Output",
                        "raw-decode",
                        1,
                        EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::Auto
                    });
                decodePrototype.rawDecode.settings = BuildRawPreviewDevelopSettings(seed.rawSource.metadata);
                const int decodeNodeId = addNode(BuildRenderNodeFromPrototype(decodePrototype));
                addLink(rawSourceNodeId, EditorNodeGraph::kRawOutputSocketId, denoiseNodeId, EditorNodeGraph::kRawInputSocketId);
                addLink(denoiseNodeId, EditorNodeGraph::kRawOutputSocketId, decodeNodeId, EditorNodeGraph::kRawInputSocketId);
                addRequest(entry, decodeNodeId, EditorNodeGraph::kImageOutputSocketId, false);
                renderable = true;
                break;
            }
            case EditorNodeGraph::NodeKind::RawDetailAutoMask: {
                const int inputNodeId = resolveSharedImageInputNode();
                if (inputNodeId <= 0) {
                    break;
                }
                prototype.rawDetailAutoMask.settings.strength = 0.92f;
                prototype.rawDetailAutoMask.settings.baseRadiusPercent = 0.014f;
                prototype.rawDetailAutoMask.settings.edgeAwareness = 0.72f;
                const int nodeId = addNode(BuildRenderNodeFromPrototype(prototype));
                addLink(inputNodeId, EditorNodeGraph::kImageOutputSocketId, nodeId, EditorNodeGraph::kImageInputSocketId);
                addRequest(entry, nodeId, EditorNodeGraph::kMaskOutputSocketId, true);
                renderable = true;
                break;
            }
            case EditorNodeGraph::NodeKind::RawDetailFusion: {
                const int inputNodeId = resolveSharedImageInputNode();
                if (inputNodeId <= 0) {
                    break;
                }
                EditorNodeGraph::Node blendMask = EditorNodeGraphDefinitions::BuildPrototypeNode(
                    EditorNodeGraphDefinitions::NodeCatalogEntry {
                        EditorNodeGraph::NodeKind::MaskGenerator,
                        static_cast<int>(EditorNodeGraph::MaskGeneratorKind::RadialGradient),
                        "Radial Gradient Mask",
                        "Masks",
                        "mask:radial-gradient",
                        1,
                        EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::Auto
                    });
                blendMask.maskSettings.radius = 0.38f;
                blendMask.maskSettings.feather = 0.18f;
                const int maskId = addNode(BuildRenderNodeFromPrototype(blendMask));
                const int nodeId = addNode(BuildRenderNodeFromPrototype(prototype));
                addLink(inputNodeId, EditorNodeGraph::kImageOutputSocketId, nodeId, EditorNodeGraph::kImageInputSocketId);
                addLink(maskId, EditorNodeGraph::kMaskOutputSocketId, nodeId, EditorNodeGraph::kMaskInputSocketId);
                addRequest(entry, nodeId, EditorNodeGraph::kImageOutputSocketId, false);
                renderable = true;
                break;
            }
            default:
                break;
        }

        if (!renderable) {
            fallbackEntries.push_back(entry);
        }
    }

    if (!fallbackEntries.empty()) {
        Async::TaskSystem::Get().Submit([this, generation, seedHash = seed.seedHash, fallbackEntries]() {
            struct EncodedFallback {
                std::string previewKey;
                std::string previewSeedHash;
                std::uint32_t previewRecipeVersion = 0;
                std::vector<unsigned char> pngBytes;
                std::vector<unsigned char> decodedPixels;
                int width = 0;
                int height = 0;
                int channels = 4;
            };

            std::vector<EncodedFallback> encoded;
            encoded.reserve(fallbackEntries.size());
            for (const auto& entry : fallbackEntries) {
                EncodedFallback thumb;
                thumb.previewKey = entry.previewKey;
                thumb.previewSeedHash = seedHash;
                thumb.previewRecipeVersion = entry.previewRecipeVersion;
                thumb.decodedPixels = BuildFallbackCardPixels(entry.previewKey);
                thumb.width = kFallbackCardWidth;
                thumb.height = kFallbackCardHeight;
                thumb.channels = 4;
                thumb.pngBytes = EncodePngBytesTopLeft(thumb.decodedPixels, thumb.width, thumb.height, thumb.channels);
                encoded.push_back(std::move(thumb));
            }
            Async::TaskSystem::Get().PostToMain([this, generation, encoded = std::move(encoded)]() mutable {
                if (generation != m_NodeBrowserThumbnailGeneration) {
                    return;
                }
                for (auto& entry : encoded) {
                    NodeBrowserThumbnailRuntimeEntry& runtime = m_NodeBrowserThumbnailEntries[entry.previewKey];
                    runtime.previewSeedHash = entry.previewSeedHash;
                    runtime.previewRecipeVersion = entry.previewRecipeVersion;
                    runtime.pngBytes = std::move(entry.pngBytes);
                    runtime.decodedPixels = std::move(entry.decodedPixels);
                    runtime.width = entry.width;
                    runtime.height = entry.height;
                    runtime.channels = entry.channels;
                    runtime.revision = m_NodeBrowserThumbnailRevisionCounter++;
                    runtime.pending = false;
                    runtime.fallback = true;
                    m_NodeBrowserThumbnailBatchHasChanges = true;
                    if (m_NodeBrowserThumbnailPendingEntries > 0) {
                        --m_NodeBrowserThumbnailPendingEntries;
                    }
                }
                FinalizeNodeBrowserThumbnailBatch(generation);
            });
        });
    }

    if (!snapshot.previews.empty() &&
        m_NodeBrowserRenderWorkerAvailable &&
        !IsRawWorkspaceProjectActive()) {
        m_NodeBrowserRenderWorker.Submit(std::move(snapshot));
    } else if (!snapshot.previews.empty()) {
        Async::TaskSystem::Get().Submit([this, generation, seedHash = seed.seedHash, pendingEntries]() {
            struct EncodedFallback {
                std::string previewKey;
                std::string previewSeedHash;
                std::uint32_t previewRecipeVersion = 0;
                std::vector<unsigned char> pngBytes;
                std::vector<unsigned char> decodedPixels;
                int width = 0;
                int height = 0;
                int channels = 4;
            };

            std::vector<EncodedFallback> encoded;
            encoded.reserve(pendingEntries.size());
            for (const auto& entry : pendingEntries) {
                EncodedFallback thumb;
                thumb.previewKey = entry.previewKey;
                thumb.previewSeedHash = seedHash;
                thumb.previewRecipeVersion = entry.previewRecipeVersion;
                thumb.decodedPixels = BuildFallbackCardPixels(entry.previewKey);
                thumb.width = kFallbackCardWidth;
                thumb.height = kFallbackCardHeight;
                thumb.channels = 4;
                thumb.pngBytes = EncodePngBytesTopLeft(thumb.decodedPixels, thumb.width, thumb.height, thumb.channels);
                encoded.push_back(std::move(thumb));
            }
            Async::TaskSystem::Get().PostToMain([this, generation, encoded = std::move(encoded)]() mutable {
                if (generation != m_NodeBrowserThumbnailGeneration) {
                    return;
                }
                for (auto& entry : encoded) {
                    NodeBrowserThumbnailRuntimeEntry& runtime = m_NodeBrowserThumbnailEntries[entry.previewKey];
                    runtime.previewSeedHash = entry.previewSeedHash;
                    runtime.previewRecipeVersion = entry.previewRecipeVersion;
                    runtime.pngBytes = std::move(entry.pngBytes);
                    runtime.decodedPixels = std::move(entry.decodedPixels);
                    runtime.width = entry.width;
                    runtime.height = entry.height;
                    runtime.channels = entry.channels;
                    runtime.revision = m_NodeBrowserThumbnailRevisionCounter++;
                    runtime.pending = false;
                    runtime.fallback = true;
                    m_NodeBrowserThumbnailBatchHasChanges = true;
                    if (m_NodeBrowserThumbnailPendingEntries > 0) {
                        --m_NodeBrowserThumbnailPendingEntries;
                    }
                }
                FinalizeNodeBrowserThumbnailBatch(generation);
            });
        });
    }
}

void EditorModule::ConsumeNodeBrowserThumbnailWorkerResults() {
    EditorRenderWorker::Result result;
    while (m_NodeBrowserRenderWorker.TryConsumeCompleted(result)) {
        if (result.generation < m_NodeBrowserThumbnailGeneration) {
            continue;
        }

        struct EncodedPreview {
            std::string previewKey;
            std::string previewSeedHash;
            std::uint32_t previewRecipeVersion = 0;
            std::vector<unsigned char> pngBytes;
            std::vector<unsigned char> decodedPixels;
            int width = 0;
            int height = 0;
            int channels = 4;
            bool fallback = false;
        };

        std::vector<EncodedPreview> jobs;
        jobs.reserve(result.previews.size());
        for (const auto& previewResult : result.previews) {
            const auto metaIt = m_NodeBrowserPreviewRequestMeta.find(previewResult.previewNodeId);
            if (metaIt == m_NodeBrowserPreviewRequestMeta.end()) {
                continue;
            }
            EncodedPreview job;
            job.previewKey = metaIt->second.previewKey;
            job.previewSeedHash = m_NodeBrowserThumbnailSeedHash;
            job.previewRecipeVersion = metaIt->second.previewRecipeVersion;
            if (previewResult.success && !previewResult.pixels.empty() && previewResult.width > 0 && previewResult.height > 0) {
                job.width = previewResult.width;
                job.height = previewResult.height;
                job.decodedPixels = ResizePixelsNearest(
                    previewResult.pixels,
                    previewResult.width,
                    previewResult.height,
                    4,
                    kNodeBrowserThumbnailMaxDimension,
                    job.width,
                    job.height);
                if (!job.decodedPixels.empty() && job.width > 0 && job.height > 0) {
                    job.channels = 4;
                    job.pngBytes = EncodePngBytesFromBottomLeft(job.decodedPixels, job.width, job.height, 4);
                    job.fallback = !HasCompletePixelBuffer(job.decodedPixels, job.width, job.height, 4) ||
                        job.pngBytes.empty();
                } else {
                    job.fallback = true;
                }
            } else {
                job.fallback = true;
            }
            if (job.fallback) {
                job.decodedPixels = BuildFallbackCardPixels(job.previewKey);
                job.width = kFallbackCardWidth;
                job.height = kFallbackCardHeight;
                job.channels = 4;
                job.pngBytes = EncodePngBytesTopLeft(job.decodedPixels, job.width, job.height, 4);
            }
            jobs.push_back(std::move(job));
        }

        if (jobs.empty()) {
            continue;
        }

        Async::TaskSystem::Get().PostToMain([this, generation = result.generation, jobs = std::move(jobs)]() mutable {
            if (generation != m_NodeBrowserThumbnailGeneration) {
                return;
            }
            for (auto& job : jobs) {
                NodeBrowserThumbnailRuntimeEntry& runtime = m_NodeBrowserThumbnailEntries[job.previewKey];
                runtime.previewSeedHash = job.previewSeedHash;
                runtime.previewRecipeVersion = job.previewRecipeVersion;
                runtime.pngBytes = std::move(job.pngBytes);
                runtime.decodedPixels = std::move(job.decodedPixels);
                runtime.width = job.width;
                runtime.height = job.height;
                runtime.channels = job.channels;
                runtime.revision = m_NodeBrowserThumbnailRevisionCounter++;
                runtime.pending = false;
                runtime.fallback = job.fallback;
                m_NodeBrowserThumbnailBatchHasChanges = true;
                if (m_NodeBrowserThumbnailPendingEntries > 0) {
                    --m_NodeBrowserThumbnailPendingEntries;
                }
            }
            FinalizeNodeBrowserThumbnailBatch(generation);
        });
    }
}
