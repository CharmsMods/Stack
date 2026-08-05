#include "Renderer/RenderPipeline.h"

#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawLoader.h"
#include "Editor/LayerRegistry.h"
#include "Editor/Layers/ToneLayers.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/RawPreviewProxy.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include "Renderer/ScopedGLObjects.h"
#include "Restormer/RestormerClient.h"
#include "Utils/PixelBufferUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Stack::Renderer::GraphExecution;

namespace {

RawDevelopmentStageStatsReadback MakeStageStatsReadback(
    Stack::RawAutoStartPoint::RawAutoStartPointStage stage,
    const RenderTextureStats& stats,
    std::string sourceDescription,
    std::string measurementDomain,
    bool sceneLinearBeforeViewTransform,
    bool displayMappedLinearRgb,
    Stack::RawAutoStartPoint::RawAutoStartPointStageStatus statusIfValid =
        Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Complete) {
    RawDevelopmentStageStatsReadback readback;
    readback.valid = stats.valid;
    readback.stage = stage;
    readback.status = stats.valid
        ? statusIfValid
        : Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Unavailable;
    readback.stageId = Stack::RawAutoStartPoint::StageStableString(stage);
    readback.label = Stack::RawAutoStartPoint::StageLabel(stage);
    readback.sourceDescription = std::move(sourceDescription);
    readback.measurementDomain = std::move(measurementDomain);
    readback.sceneLinearBeforeViewTransform = sceneLinearBeforeViewTransform;
    readback.displayMappedLinearRgb = displayMappedLinearRgb;
    readback.rawSafetyStats = false;
    readback.textureStats = stats;
    return readback;
}

RawDevelopmentStageStatsReadback MakeRawSafetyStageReadback(
    const Stack::RawAutoStartPoint::RawAutoStartPointRawSafetyStats& stats) {
    RawDevelopmentStageStatsReadback readback;
    readback.valid = stats.valid;
    readback.stage = Stack::RawAutoStartPoint::RawAutoStartPointStage::RawTechnical;
    readback.status = stats.valid
        ? Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Complete
        : Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Unavailable;
    readback.stageId = Stack::RawAutoStartPoint::StageStableString(readback.stage);
    readback.label = Stack::RawAutoStartPoint::StageLabel(readback.stage);
    readback.sourceDescription =
        "Raw technical safety ledger from RAW source samples and metadata before demosaic or display transforms.";
    readback.measurementDomain = "raw-active-area-metadata";
    readback.rawSafetyStats = true;
    readback.rawSafety = stats;
    return readback;
}

int RawPlaneAt(Raw::CfaPattern pattern, int x, int y) {
    const bool evenX = (x & 1) == 0;
    const bool evenY = (y & 1) == 0;
    switch (pattern) {
        case Raw::CfaPattern::RGGB:
            return evenY ? (evenX ? 0 : 1) : (evenX ? 3 : 2);
        case Raw::CfaPattern::BGGR:
            return evenY ? (evenX ? 2 : 1) : (evenX ? 3 : 0);
        case Raw::CfaPattern::GBRG:
            return evenY ? (evenX ? 1 : 2) : (evenX ? 0 : 3);
        case Raw::CfaPattern::GRBG:
            return evenY ? (evenX ? 1 : 0) : (evenX ? 2 : 3);
        case Raw::CfaPattern::Unknown:
        default:
            return -1;
    }
}

int RawSafetyChannelFromPlane(int plane) {
    if (plane == 0) {
        return 0;
    }
    if (plane == 2) {
        return 2;
    }
    return 1;
}

float RawSafetyBlackForPlane(const Raw::RawMetadata& metadata, int plane) {
    if (plane >= 0 && plane < 4) {
        const float channelBlack = metadata.perChannelBlack[static_cast<std::size_t>(plane)];
        if (std::isfinite(channelBlack) && channelBlack > 0.0f) {
            return channelBlack;
        }
    }
    return std::max(0.0f, metadata.blackLevel);
}

float NormalizeRawSafetyValue(float value, float black, float white) {
    const float range = std::max(1.0f, white - black);
    return (value - black) / range;
}

float PercentileSorted(const std::vector<float>& values, float q) {
    if (values.empty()) {
        return 0.0f;
    }
    const float clampedQ = std::clamp(q, 0.0f, 1.0f);
    const float scaled = clampedQ * static_cast<float>(values.size() - 1);
    const std::size_t index = static_cast<std::size_t>(std::round(scaled));
    return values[std::min(index, values.size() - 1)];
}

float SafeHeadroomEv(float value, float limit = 1.0f) {
    const float safeValue = std::max(0.000001f, value);
    const float safeLimit = std::max(0.000001f, limit);
    return std::clamp(std::log2(safeLimit / safeValue), -8.0f, 12.0f);
}

int RawSafetySampleStep(int extent) {
    constexpr int kMaximumSamplesPerAxis = 512;
    if (extent <= 1) {
        return 1;
    }
    return (extent - 1) / kMaximumSamplesPerAxis + 1;
}

float RawSafetyEstimatedCoverage(
    int sampledPixels,
    int stepX,
    int stepY,
    int width,
    int height) {
    const double expectedPixels = std::max(
        1.0,
        static_cast<double>(width) * static_cast<double>(height));
    const double representedPixels =
        static_cast<double>(sampledPixels) *
        static_cast<double>(stepX) *
        static_cast<double>(stepY);
    return static_cast<float>(std::clamp(
        representedPixels / expectedPixels,
        0.0,
        1.0));
}

std::array<float, 3> ResolveRawSafetyWhiteBalance(
    const Raw::RawMetadata& metadata,
    const Raw::RawDevelopSettings& settings) {
    if (settings.whiteBalanceMode == Raw::WhiteBalanceMode::Manual) {
        return {
            std::max(0.001f, settings.manualWhiteBalance[0]),
            std::max(0.001f, settings.manualWhiteBalance[1]),
            std::max(0.001f, settings.manualWhiteBalance[2])
        };
    }
    if (settings.whiteBalanceMode == Raw::WhiteBalanceMode::Neutral) {
        return { 1.0f, 1.0f, 1.0f };
    }
    const std::array<float, 4>& source =
        settings.whiteBalanceMode == Raw::WhiteBalanceMode::Auto
            ? metadata.daylightWhiteBalance
            : metadata.cameraWhiteBalance;
    const float green = std::max(0.001f, source[1]);
    return {
        std::max(0.001f, source[0]) / green,
        1.0f,
        std::max(0.001f, source[2]) / green
    };
}

Stack::RawAutoStartPoint::RawAutoStartPointRawSafetyStats BuildRawSafetyStats(
    const Raw::RawImageData& rawData,
    const Raw::RawDevelopSettings& settings) {
    Stack::RawAutoStartPoint::RawAutoStartPointRawSafetyStats stats;
    const Raw::RawMetadata& metadata = rawData.metadata;
    stats.blackLevelSource = metadata.blackLevelSource.empty()
        ? "metadata/default"
        : metadata.blackLevelSource;
    stats.whiteLevelSource = metadata.whiteLevelSource.empty()
        ? "metadata/default"
        : metadata.whiteLevelSource;
    stats.maskedBlackMean = metadata.blackLevel;
    stats.maskedBlackStd = 0.0f;
    stats.lensShadingConfidence = metadata.dngGainMapCount > 0 ? 0.50f : 0.0f;
    stats.linearResponseLimit = 1.0f;
    stats.noiseProfileAvailable = false;
    stats.baselineExposureEv = metadata.hasDngBaselineExposure ? metadata.dngBaselineExposure : 0.0f;
    stats.colorMatrixConfidence =
        (metadata.hasDngForwardMatrix1 || metadata.hasDngForwardMatrix2)
            ? 1.0f
            : (metadata.hasCameraMatrix ? 0.80f : 0.35f);
    stats.asShotWbAvailable =
        metadata.hasDngAsShotNeutral ||
        (metadata.cameraWhiteBalance[0] > 0.0001f &&
            metadata.cameraWhiteBalance[1] > 0.0001f &&
            metadata.cameraWhiteBalance[2] > 0.0001f);

    std::array<std::vector<float>, 3> channelValues;
    std::array<int, 3> channelCounts { 0, 0, 0 };
    std::array<int, 3> channelNearClipped { 0, 0, 0 };
    std::array<int, 3> channelClipped { 0, 0, 0 };
    int sampledPixels = 0;
    int singleClipPixels = 0;
    int multiClipPixels = 0;
    int fullClipPixels = 0;
    int hotPixels = 0;

    const float metadataWhite = std::max(metadata.blackLevel + 1.0f, metadata.whiteLevel);
    const auto recordChannel = [&](int channel, float normalized) {
        if (channel < 0 || channel >= 3 || !std::isfinite(normalized)) {
            return false;
        }
        const float safeNormalized = std::max(0.0f, normalized);
        channelValues[static_cast<std::size_t>(channel)].push_back(safeNormalized);
        ++channelCounts[static_cast<std::size_t>(channel)];
        if (safeNormalized >= 0.98f) {
            ++channelNearClipped[static_cast<std::size_t>(channel)];
        }
        if (safeNormalized >= 0.999f) {
            ++channelClipped[static_cast<std::size_t>(channel)];
        }
        if (safeNormalized > 1.05f) {
            ++hotPixels;
        }
        return safeNormalized >= 0.999f;
    };

    std::size_t requiredMosaicElements = 0;
    const bool hasNormalizedMosaic =
        rawData.normalizedMosaicBuffer &&
        !rawData.normalizedMosaicBuffer->empty();
    const bool hasCompleteMosaic =
        Stack::PixelBuffer::TryComputePixelElementCount(
            metadata.rawWidth,
            metadata.rawHeight,
            1,
            requiredMosaicElements) &&
        (rawData.rawBuffer.size() >= requiredMosaicElements ||
         (hasNormalizedMosaic &&
          rawData.normalizedMosaicBuffer->size() >=
              requiredMosaicElements));
    if (metadata.pixelLayout == Raw::RawPixelLayout::MosaicBayer &&
        metadata.rawWidth > 0 &&
        metadata.rawHeight > 0 &&
        hasCompleteMosaic) {
        int left = std::max(0, metadata.leftMargin);
        int top = std::max(0, metadata.topMargin);
        const std::int64_t requestedRight =
            static_cast<std::int64_t>(left) +
            static_cast<std::int64_t>(std::max(1, metadata.visibleWidth));
        const std::int64_t requestedBottom =
            static_cast<std::int64_t>(top) +
            static_cast<std::int64_t>(std::max(1, metadata.visibleHeight));
        int right = static_cast<int>(std::min<std::int64_t>(
            metadata.rawWidth, requestedRight));
        int bottom = static_cast<int>(std::min<std::int64_t>(
            metadata.rawHeight, requestedBottom));
        if (right <= left || bottom <= top) {
            left = 0;
            top = 0;
            right = metadata.rawWidth;
            bottom = metadata.rawHeight;
        }
        const int width = std::max(1, right - left);
        const int height = std::max(1, bottom - top);
        const int stepX = RawSafetySampleStep(width);
        const int stepY = RawSafetySampleStep(height);
        for (std::int64_t y = top; y < bottom; y += stepY) {
            for (std::int64_t x = left; x < right; x += stepX) {
                const int sampleX = static_cast<int>(x);
                const int sampleY = static_cast<int>(y);
                const int plane = RawPlaneAt(
                    metadata.cfaPattern, sampleX - left, sampleY - top);
                if (plane < 0) {
                    continue;
                }
                const int channel = RawSafetyChannelFromPlane(plane);
                const float black = RawSafetyBlackForPlane(metadata, plane);
                const std::size_t sampleIndex =
                    static_cast<std::size_t>(sampleY) *
                        static_cast<std::size_t>(metadata.rawWidth) +
                    static_cast<std::size_t>(sampleX);
                const float value = hasNormalizedMosaic
                    ? (*rawData.normalizedMosaicBuffer)[sampleIndex]
                    : static_cast<float>(rawData.rawBuffer[sampleIndex]);
                const bool clipped = recordChannel(
                    channel,
                    hasNormalizedMosaic
                        ? value
                        : NormalizeRawSafetyValue(
                              value, black, metadataWhite));
                ++sampledPixels;
                if (clipped) {
                    ++singleClipPixels;
                }
            }
        }
        stats.activeValidFraction = RawSafetyEstimatedCoverage(
            sampledPixels, stepX, stepY, width, height);
        stats.statusMessage = hasNormalizedMosaic
            ? "Partial raw safety ledger from the normalized multi-frame Bayer result and reference metadata. Multi-channel clip fractions remain unavailable for single-photosite mosaic samples."
            : "Partial raw safety ledger from active Bayer samples and metadata levels. Masked optical-black drift, true linear response limits, and noise profiles are not captured yet; multi-channel clip fractions are unavailable for single-photosite mosaic samples.";
    } else {
        const int width = std::max(1, metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth);
        const int height = std::max(1, metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight);
        const int channels = std::clamp(metadata.linearChannels > 0 ? metadata.linearChannels : 3, 3, 4);
        std::size_t requiredLinearElements = 0;
        const bool validLinearExtent =
            Stack::PixelBuffer::TryComputePixelElementCount(
                width, height, channels, requiredLinearElements);
        const bool useUInt16 =
            validLinearExtent &&
            rawData.linearUInt16Buffer.size() >= requiredLinearElements;
        const bool useFloat =
            validLinearExtent &&
            rawData.linearFloatBuffer.size() >= requiredLinearElements;
        const int stepX = RawSafetySampleStep(width);
        const int stepY = RawSafetySampleStep(height);
        if (useUInt16 || useFloat) {
            const bool floatLooksUnitScale =
                useFloat && metadata.rawMaximum > 0.0f && metadata.rawMaximum <= 4.0f;
            for (std::int64_t y = 0; y < height; y += stepY) {
                for (std::int64_t x = 0; x < width; x += stepX) {
                    const std::size_t base =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(x)) *
                        static_cast<std::size_t>(channels);
                    int clippedChannels = 0;
                    for (int c = 0; c < 3; ++c) {
                        const float value = useUInt16
                            ? static_cast<float>(rawData.linearUInt16Buffer[base + static_cast<std::size_t>(c)])
                            : rawData.linearFloatBuffer[base + static_cast<std::size_t>(c)];
                        const float normalized = useFloat && floatLooksUnitScale
                            ? value
                            : NormalizeRawSafetyValue(value, metadata.blackLevel, metadataWhite);
                        if (recordChannel(c, normalized)) {
                            ++clippedChannels;
                        }
                    }
                    ++sampledPixels;
                    if (clippedChannels >= 3) {
                        ++fullClipPixels;
                    } else if (clippedChannels >= 2) {
                        ++multiClipPixels;
                    } else if (clippedChannels == 1) {
                        ++singleClipPixels;
                    }
                }
            }
            stats.activeValidFraction = RawSafetyEstimatedCoverage(
                sampledPixels, stepX, stepY, width, height);
            stats.statusMessage =
                "Partial raw safety ledger from active linear RGB samples and metadata levels. "
                "Masked optical-black drift, true linear response limits, and noise profiles are not captured yet.";
        }
    }

    if (sampledPixels <= 0) {
        stats.valid = false;
        stats.statusMessage =
            "Raw safety stats unavailable: no supported RAW or linear active-area samples were available.";
        return stats;
    }

    for (int c = 0; c < 3; ++c) {
        std::vector<float>& values = channelValues[static_cast<std::size_t>(c)];
        std::sort(values.begin(), values.end());
        const float count = static_cast<float>(std::max(1, channelCounts[static_cast<std::size_t>(c)]));
        stats.perChannelClippedFraction[static_cast<std::size_t>(c)] =
            static_cast<float>(channelClipped[static_cast<std::size_t>(c)]) / count;
        stats.perChannelNearClippedFraction[static_cast<std::size_t>(c)] =
            static_cast<float>(channelNearClipped[static_cast<std::size_t>(c)]) / count;
        stats.perChannelP999[static_cast<std::size_t>(c)] = PercentileSorted(values, 0.999f);
        stats.rawWhiteP999 = std::max(
            stats.rawWhiteP999,
            stats.perChannelP999[static_cast<std::size_t>(c)]);
    }

    const float pixelDenominator = static_cast<float>(std::max(1, sampledPixels));
    stats.singleChannelClipFraction = static_cast<float>(singleClipPixels) / pixelDenominator;
    stats.multiChannelClipFraction = static_cast<float>(multiClipPixels) / pixelDenominator;
    stats.fullClipFraction = static_cast<float>(fullClipPixels) / pixelDenominator;
    stats.hotPixelFraction = static_cast<float>(hotPixels) / pixelDenominator;
    stats.headroomEv = SafeHeadroomEv(stats.rawWhiteP999, stats.linearResponseLimit);

    const std::array<float, 3> wb = ResolveRawSafetyWhiteBalance(metadata, settings);
    float wbScaledP999 = 0.0f;
    for (int c = 0; c < 3; ++c) {
        wbScaledP999 = std::max(
            wbScaledP999,
            stats.perChannelP999[static_cast<std::size_t>(c)] *
                std::max(0.001f, wb[static_cast<std::size_t>(c)]));
    }
    stats.wbScaledHeadroomEv = SafeHeadroomEv(wbScaledP999, stats.linearResponseLimit);
    stats.highlightRecoverabilityScore = std::clamp(
        1.0f -
            stats.fullClipFraction * 10.0f -
            stats.multiChannelClipFraction * 4.0f -
            stats.singleChannelClipFraction * 0.50f,
        0.0f,
        1.0f);
    stats.valid = true;
    return stats;
}

} // namespace

const Raw::RawImageData& RenderPipeline::ResolveRawPreviewRenderData(
    int cacheNodeId,
    const Raw::RawImageData& rawData,
    const std::string& sourceCacheKey) {
    if (m_PreviewMaxDimension <= 0 ||
        !Stack::Renderer::RawPreviewProxy::HasPixels(rawData) ||
        !rawData.metadata.error.empty()) {
        return rawData;
    }

    const std::string previewCacheKey =
        Stack::Renderer::RawPreviewProxy::BuildCacheKey(sourceCacheKey, rawData, m_PreviewMaxDimension);
    std::string& cachedKey = m_RawPreviewDataCacheKeys[cacheNodeId];
    Raw::RawImageData& cachedPreview = m_RawPreviewDataCache[cacheNodeId];
    if (cachedKey == previewCacheKey &&
        Stack::Renderer::RawPreviewProxy::HasPixels(cachedPreview) &&
        cachedPreview.metadata.error.empty()) {
        return cachedPreview;
    }

    Raw::RawImageData preview;
    if (!Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(rawData, m_PreviewMaxDimension, preview)) {
        cachedKey.clear();
        cachedPreview = Raw::RawImageData {};
        return rawData;
    }

    cachedPreview = std::move(preview);
    cachedKey = previewCacheKey;
    return cachedPreview;
}

RenderPipeline::GraphNodeRenderResult RenderPipeline::RenderRawDevelopmentGraphNode(
    const RenderGraphNode& node,
    std::size_t fingerprint) {
    GraphNodeRenderResult result;
    Stack::Renderer::ScopedOwnedGLTextureExceptionCleanup
        resultExceptionCleanup(result.texture, result.owned);

    const Stack::RawRecipe::RawDevelopmentRecipe& recipe = node.rawDevelopment.recipe;
    const std::string& sourcePath = recipe.source.sourcePath;
    const std::shared_ptr<const Raw::RawImageData>& embeddedRawData =
        node.rawDevelopment.embeddedRawData;
    if (sourcePath.empty() && !embeddedRawData) {
        return result;
    }

    const std::string cacheKeyPath =
        Stack::Renderer::RawDevelopmentCache::BuildSourceDataIdentity(
            recipe.source);
    const Raw::RawImageData* rawData = embeddedRawData.get();
    if (!rawData) {
        Raw::RawImageData& cachedRawData = m_RawDataCache[node.nodeId];
        std::string& cachedPath = m_RawDataCachePaths[node.nodeId];
        if (cachedPath != cacheKeyPath ||
            (cachedRawData.rawBuffer.empty() &&
             cachedRawData.linearUInt16Buffer.empty() &&
             cachedRawData.linearFloatBuffer.empty() &&
             !cachedRawData.normalizedMosaicBuffer)) {
            Raw::RawImageData loadedRaw;
            Raw::RawLoader::LoadFile(sourcePath, loadedRaw);
            cachedRawData = std::move(loadedRaw);
            cachedPath = cacheKeyPath;
        }
        rawData = &cachedRawData;
    }

    const bool rawDataHasPixels =
        Stack::Renderer::RawPreviewProxy::HasPixels(*rawData);
    if (!rawDataHasPixels || !rawData->metadata.error.empty()) {
        const std::string error = !rawData->metadata.error.empty()
            ? rawData->metadata.error
            : "LibRaw did not produce a usable raw buffer.";
        std::cerr << "[RAW] Load failed for RAW Development node " << node.nodeId
                  << " (" << sourcePath << "): " << error << "\n";
        return result;
    }

    const Raw::RawDevelopSettings settings = Stack::RawRecipe::ToRawDevelopSettings(recipe);
    const bool captureAnalysis = m_RawDevelopmentAnalysisEnabled;
    Stack::RawAutoStartPoint::RawAutoStartPointRawSafetyStats rawSafetyStats;
    if (captureAnalysis) {
        rawSafetyStats = BuildRawSafetyStats(*rawData, settings);
    }
    const bool localExposureEnabled = Stack::RawRecipe::IsLocalExposureEnabled(recipe);
    if (!localExposureEnabled) {
        m_PreLocalExposureSummaries.erase(node.nodeId);
    }
    Raw::RawDevelopSettings rawRenderSettings = settings;
    rawRenderSettings.toneCurvePoints.clear();
    const Raw::RawImageData& renderRawData =
        rawData->normalizedMosaicBuffer
            ? *rawData
            : ResolveRawPreviewRenderData(
                  node.nodeId, *rawData, cacheKeyPath);
    const std::string rawPlacementCacheKey =
        std::to_string(node.nodeId) + ":__rawDevelopmentPlacement";
    const std::string neutralPlacementCacheKey =
        std::to_string(node.nodeId) + ":__rawDevelopmentNeutral";
    const std::string rawBaseCacheKey =
        std::to_string(node.nodeId) + ":__rawDevelopmentRgbBase";
    const std::string rgbDenoiseCacheKey =
        std::to_string(node.nodeId) + ":__rawDevelopmentRgbDenoise";
    const std::string postLocalExposureCacheKey =
        std::to_string(node.nodeId) + ":__rawDevelopmentPostLocalExposure";
    const std::string postLocalRangeCacheKey =
        std::to_string(node.nodeId) + ":__rawDevelopmentPostLocalRange";
    const std::string postFinishToneCacheKey =
        std::to_string(node.nodeId) + ":__rawDevelopmentPostFinishTone";
    const std::size_t rawPlacementFingerprint =
        Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint(
            recipe,
            m_PreviewMaxDimension,
            Stack::Renderer::RawDevelopmentCache::Stage::RawPlacement);
    const std::size_t neutralPlacementFingerprint =
        Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint(
            recipe,
            m_PreviewMaxDimension,
            Stack::Renderer::RawDevelopmentCache::Stage::NeutralPlacement);
    const std::size_t rawBaseFingerprint =
        Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint(
            recipe,
            m_PreviewMaxDimension,
            Stack::Renderer::RawDevelopmentCache::Stage::RawBase);
    const std::size_t postLocalExposureFingerprint =
        Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint(
            recipe,
            m_PreviewMaxDimension,
            Stack::Renderer::RawDevelopmentCache::Stage::PostLocalExposure);
    const std::size_t postLocalRangeFingerprint =
        Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint(
            recipe,
            m_PreviewMaxDimension,
            Stack::Renderer::RawDevelopmentCache::Stage::PostLocalRange);
    const std::size_t postFinishToneFingerprint =
        Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint(
            recipe,
            m_PreviewMaxDimension,
            Stack::Renderer::RawDevelopmentCache::Stage::PostFinishTone);
    const Stack::RawRecipe::RawRgbDenoiseRecipe rgbDenoiseSettings =
        Stack::RawRecipe::SanitizeRgbDenoiseRecipe(recipe.rgbDenoise);
    const bool rgbDenoiseEnabled = rgbDenoiseSettings.enabled;
    Raw::RawDevelopSettings neutralSceneSettings = rawRenderSettings;
    neutralSceneSettings.exposureStops = 0.0f;
    neutralSceneSettings.toneCurvePoints.clear();
    const bool currentRawRenderIsNeutralScene =
        std::fabs(rawRenderSettings.exposureStops) <= 0.0001f &&
        rawRenderSettings.toneCurvePoints.empty();
    RenderTextureStats neutralSceneStats;
    if (!rgbDenoiseEnabled) {
        if (captureAnalysis && !currentRawRenderIsNeutralScene) {
            const CachedGraphTexture cachedNeutral =
                FindRawDevelopStageCacheEntry(neutralPlacementCacheKey, neutralPlacementFingerprint);
            unsigned int neutralTexture = cachedNeutral.texture;
            int neutralWidth = cachedNeutral.width;
            int neutralHeight = cachedNeutral.height;
            if (neutralTexture != 0 && neutralWidth > 0 && neutralHeight > 0) {
                ++m_LastGraphExecutionStats.rawStageCacheHits;
            } else {
                ++m_LastGraphExecutionStats.rawStageCacheMisses;
                neutralTexture =
                    m_RawPipelines[node.nodeId].Render(
                        renderRawData,
                        neutralSceneSettings,
                        m_PreviewMaxDimension);
                neutralWidth = m_RawPipelines[node.nodeId].GetOutputWidth();
                neutralHeight = m_RawPipelines[node.nodeId].GetOutputHeight();
                if (neutralTexture != 0 && neutralWidth > 0 && neutralHeight > 0) {
                    m_Width = neutralWidth;
                    m_Height = neutralHeight;
                    StoreRawDevelopStageCacheEntry(
                        neutralPlacementCacheKey,
                        neutralTexture,
                        neutralPlacementFingerprint);
                }
            }
            if (neutralTexture != 0 && neutralWidth > 0 && neutralHeight > 0) {
                neutralSceneStats =
                    ReadTextureStats(neutralTexture, neutralWidth, neutralHeight, "RawDevelopmentNeutralSceneStats");
                CaptureRawDevelopmentStageImageReadback(
                    Stack::RawAutoStartPoint::RawAutoStartPointStage::NeutralScene,
                    Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Complete,
                    neutralTexture,
                    neutralWidth,
                    neutralHeight,
                    "scene-linear-neutral-srgb",
                    true,
                    false);
            }
        }

        const CachedGraphTexture cachedRawPlacement =
            FindRawDevelopStageCacheEntry(rawPlacementCacheKey, rawPlacementFingerprint);
        if (cachedRawPlacement.texture != 0 &&
            cachedRawPlacement.width > 0 &&
            cachedRawPlacement.height > 0) {
            ++m_LastGraphExecutionStats.rawStageCacheHits;
            result.texture = cachedRawPlacement.texture;
            m_Width = cachedRawPlacement.width;
            m_Height = cachedRawPlacement.height;
        } else {
            ++m_LastGraphExecutionStats.rawStageCacheMisses;
            result.texture =
                m_RawPipelines[node.nodeId].Render(
                    renderRawData,
                    rawRenderSettings,
                    m_PreviewMaxDimension);
            m_Width = m_RawPipelines[node.nodeId].GetOutputWidth();
            m_Height = m_RawPipelines[node.nodeId].GetOutputHeight();
            if (result.texture != 0 && m_Width > 0 && m_Height > 0) {
                StoreRawDevelopStageCacheEntry(
                    rawPlacementCacheKey,
                    result.texture,
                    rawPlacementFingerprint);
            }
        }
        result.owned = false;
    } else {
        const bool aiDenoiseMethod =
            rgbDenoiseSettings.method !=
            Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1;
        if (aiDenoiseMethod) {
            const Stack::Restormer::ValidationResult package =
                Stack::Restormer::Client::Instance().Validate(
                    rgbDenoiseSettings);
            if (!package.ok) {
                m_LastRawRgbDenoiseError = package.error;
                std::cerr
                    << "[RAW] Restormer RGB denoise blocked RAW Development "
                    << "node " << node.nodeId << ": " << package.error << "\n";
                return result;
            }
        }
        const CachedGraphTexture cachedDenoise =
            FindRawDevelopStageCacheEntry(
                rgbDenoiseCacheKey,
                neutralPlacementFingerprint);
        unsigned int neutralTexture = cachedDenoise.texture;
        Stack::Renderer::ScopedGLTexture neutralTextureOwner;
        int neutralWidth = cachedDenoise.width;
        int neutralHeight = cachedDenoise.height;
        if (neutralTexture != 0 && neutralWidth > 0 && neutralHeight > 0) {
            ++m_LastGraphExecutionStats.rawStageCacheHits;
            m_Width = neutralWidth;
            m_Height = neutralHeight;
        } else {
            ++m_LastGraphExecutionStats.rawStageCacheMisses;
            const CachedGraphTexture cachedBase =
                FindRawDevelopStageCacheEntry(rawBaseCacheKey, rawBaseFingerprint);
            unsigned int baseTexture = cachedBase.texture;
            int baseWidth = cachedBase.width;
            int baseHeight = cachedBase.height;
            if (baseTexture != 0 && baseWidth > 0 && baseHeight > 0) {
                ++m_LastGraphExecutionStats.rawStageCacheHits;
                m_Width = baseWidth;
                m_Height = baseHeight;
            } else {
                ++m_LastGraphExecutionStats.rawStageCacheMisses;
                baseTexture =
                    m_RawPipelines[node.nodeId].Render(
                        renderRawData,
                        neutralSceneSettings,
                        m_PreviewMaxDimension);
                baseWidth = m_RawPipelines[node.nodeId].GetOutputWidth();
                baseHeight = m_RawPipelines[node.nodeId].GetOutputHeight();
                m_Width = baseWidth;
                m_Height = baseHeight;
                if (baseTexture != 0 && baseWidth > 0 && baseHeight > 0) {
                    StoreRawDevelopStageCacheEntry(
                        rawBaseCacheKey,
                        baseTexture,
                        rawBaseFingerprint);
                }
            }

            if (baseTexture != 0 && m_Width > 0 && m_Height > 0) {
                neutralTexture = RenderRawDevelopmentRgbDenoise(
                    baseTexture,
                    rgbDenoiseSettings,
                    recipe.technical.workingSpace,
                    rawBaseFingerprint);
                if (neutralTexture != 0) {
                    neutralTextureOwner.Reset(neutralTexture);
                    neutralWidth = m_Width;
                    neutralHeight = m_Height;
                    StoreRawDevelopStageCacheEntry(
                        rgbDenoiseCacheKey,
                        neutralTexture,
                        neutralPlacementFingerprint);
                } else {
                    const bool aiMethod =
                        rgbDenoiseSettings.method !=
                        Stack::RawRecipe::RawRgbDenoiseMethod::
                            ClassicalMultiscaleV1;
                    if (aiMethod) {
                        neutralTexture = 0;
                        neutralWidth = 0;
                        neutralHeight = 0;
                        std::cerr
                            << "[RAW] Restormer RGB denoise blocked RAW "
                            << "Development node " << node.nodeId << ": "
                            << (m_LastRawRgbDenoiseError.empty()
                                ? "external model inference failed"
                                : m_LastRawRgbDenoiseError)
                            << "\n";
                    } else {
                        // The built-in method may fail open because it does
                        // not represent an authored external model identity.
                        neutralTexture = baseTexture;
                        neutralWidth = m_Width;
                        neutralHeight = m_Height;
                        std::cerr
                            << "[RAW] Classical RGB denoise failed for RAW "
                            << "Development node " << node.nodeId
                            << "; passing neutral demosaic through.\n";
                    }
                }
            }
        }

        if (captureAnalysis &&
            neutralTexture != 0 &&
            neutralWidth > 0 &&
            neutralHeight > 0 &&
            !currentRawRenderIsNeutralScene) {
            neutralSceneStats =
                ReadTextureStats(
                    neutralTexture,
                    neutralWidth,
                    neutralHeight,
                    "RawDevelopmentNeutralSceneStats");
            CaptureRawDevelopmentStageImageReadback(
                Stack::RawAutoStartPoint::RawAutoStartPointStage::NeutralScene,
                Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Complete,
                neutralTexture,
                neutralWidth,
                neutralHeight,
                "scene-linear-neutral-srgb",
                true,
                false);
        }

        const CachedGraphTexture cachedRawPlacement =
            FindRawDevelopStageCacheEntry(
                rawPlacementCacheKey,
                rawPlacementFingerprint);
        if (cachedRawPlacement.texture != 0 &&
            cachedRawPlacement.width > 0 &&
            cachedRawPlacement.height > 0) {
            ++m_LastGraphExecutionStats.rawStageCacheHits;
            result.texture = cachedRawPlacement.texture;
            result.owned = false;
            m_Width = cachedRawPlacement.width;
            m_Height = cachedRawPlacement.height;
        } else if (neutralTexture != 0) {
            ++m_LastGraphExecutionStats.rawStageCacheMisses;
            m_Width = neutralWidth;
            m_Height = neutralHeight;
            result.texture = RenderRawDevelopmentExposure(
                neutralTexture,
                recipe.preToneExposureEv);
            result.owned = result.texture != 0;
            if (result.texture != 0) {
                StoreRawDevelopStageCacheEntry(
                    rawPlacementCacheKey,
                    result.texture,
                    rawPlacementFingerprint);
            } else {
                // The exposure pass is deliberately simple, but preserve a
                // last-resort path through the established RAW renderer.
                result.texture =
                    m_RawPipelines[node.nodeId].Render(
                        renderRawData,
                        rawRenderSettings,
                        m_PreviewMaxDimension);
                m_Width = m_RawPipelines[node.nodeId].GetOutputWidth();
                m_Height = m_RawPipelines[node.nodeId].GetOutputHeight();
                result.owned = false;
            }
        }

    }
    if (result.texture == 0) {
        const std::string& error = m_RawPipelines[node.nodeId].GetLastError();
        std::cerr << "[RAW] Render failed for RAW Development node " << node.nodeId
                  << " (" << sourcePath << "): "
                  << (error.empty() ? "unknown RAW GPU failure" : error)
                  << "\n";
        return result;
    }

    const auto adoptCachedStage = [&](const CachedGraphTexture& cached) {
        if (cached.texture == 0 || cached.width <= 0 || cached.height <= 0) {
            return false;
        }
        if (result.owned &&
            result.texture != 0 &&
            result.texture != cached.texture) {
            glDeleteTextures(1, &result.texture);
        }
        result.texture = cached.texture;
        result.owned = false;
        m_Width = cached.width;
        m_Height = cached.height;
        ++m_LastGraphExecutionStats.rawStageCacheHits;
        return true;
    };

    RenderTextureStats rawPlacementStats;
    if (captureAnalysis) {
        rawPlacementStats =
            ReadTextureStats(result.texture, m_Width, m_Height, "RawDevelopmentRawPlacementStats");
        CaptureRawDevelopmentStageImageReadback(
            Stack::RawAutoStartPoint::RawAutoStartPointStage::RawPlacement,
            Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Complete,
            result.texture,
            m_Width,
            m_Height,
            "scene-linear-raw-placement-srgb",
            true,
            false);
        if (currentRawRenderIsNeutralScene) {
            neutralSceneStats = rawPlacementStats;
            CaptureRawDevelopmentStageImageReadback(
                Stack::RawAutoStartPoint::RawAutoStartPointStage::NeutralScene,
                Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Complete,
                result.texture,
                m_Width,
                m_Height,
                "scene-linear-neutral-srgb",
                true,
                false);
        }
    }
    if (localExposureEnabled && result.texture != 0) {
        Raw::RawDetailFusionSettings localSettings = Stack::RawRecipe::ToRawDetailFusionSettings(recipe);
        localSettings.mode = Raw::RawDetailFusionMode::AutoAnalyze;
        localSettings.debugView = Raw::RawDetailFusionDebugView::FinalImage;
        localSettings.invertMask = false;
        localSettings.maskBlackPoint = 0.0f;
        localSettings.maskWhitePoint = 1.0f;
        localSettings.maskGamma = 1.0f;
        localSettings.manualBlend = 0.0f;
        if (localSettings.autoSafetyEnabled && !localSettings.overrideBaseEv) {
            localSettings.baseEv = std::clamp(localSettings.baseEvBias, -1.0f, 1.0f);
            localSettings.overrideBaseEv = true;
        }

        if (captureAnalysis) {
            m_PreLocalExposureSummaries[node.nodeId] = BuildPreLocalExposureSummary(
                result.texture,
                localSettings,
                false,
                !localSettings.autoSafetyEnabled);
        }

        const CachedGraphTexture cachedPostLocalExposure =
            FindRawDevelopStageCacheEntry(
                postLocalExposureCacheKey,
                postLocalExposureFingerprint);
        if (!adoptCachedStage(cachedPostLocalExposure)) {
            ++m_LastGraphExecutionStats.rawStageCacheMisses;
            RenderGraphNode localMapNode;
            localMapNode.nodeId = node.nodeId;
            localMapNode.kind = RenderGraphNodeKind::RawDetailFusion;
            localMapNode.rawDetailFusion.settings = localSettings;

            const unsigned int preLocalTexture = result.texture;
            const bool preLocalTextureOwned = result.owned;
            Stack::Renderer::ScopedGLTexture exposureMap(
                RenderRawDetailAutoMask(
                    preLocalTexture,
                    localMapNode,
                    0,
                    false));
            Stack::Renderer::ScopedGLTexture localResult(
                exposureMap
                    ? RenderRawDetailFusion(
                        preLocalTexture,
                        exposureMap.Get(),
                        localSettings)
                    : 0);
            if (localResult) {
                QuickTextureStats inputStats;
                QuickTextureStats outputStats;
                if (captureAnalysis) {
                    inputStats = ProbeTextureStats(preLocalTexture, m_Width, m_Height);
                    outputStats = ProbeTextureStats(localResult.Get(), m_Width, m_Height);
                }
                const bool inputHasSignal =
                    captureAnalysis && inputStats.valid && inputStats.p99Luma > 0.00001f;
                const bool outputIsBlank =
                    captureAnalysis &&
                    outputStats.valid &&
                    outputStats.p99Luma <= 0.000001f &&
                    outputStats.maxRgb <= 0.00001f;
                if (inputHasSignal && outputIsBlank) {
                    std::cerr << "[RenderPipeline] RAW Workspace local exposure produced a blank output for RAW Development node "
                              << node.nodeId << " (input p99 luma " << inputStats.p99Luma
                              << ", output p99 luma " << outputStats.p99Luma
                              << "); passing pre-local texture through.\n";
                } else {
                    if (preLocalTextureOwned && preLocalTexture != 0) {
                        glDeleteTextures(1, &preLocalTexture);
                    }
                    result.texture = localResult.Release();
                    result.owned = true;
                    StoreRawDevelopStageCacheEntry(
                        postLocalExposureCacheKey,
                        result.texture,
                        postLocalExposureFingerprint);
                }
            }
        }
    }

    const bool localRangeActive = Stack::RawRecipe::IsLocalRangeEnabled(recipe);
    RenderTextureStats preLocalRangeStats;
    bool hasPreLocalRangeStats = false;
    bool localRangeApplied = false;

    if (captureAnalysis && result.texture != 0) {
        m_RawDevelopmentLocalSuggestionImage =
            ReadLocalSuggestionAnalysisImage(
                result.texture,
                m_Width,
                m_Height,
                512,
                "RawDevelopmentLocalSuggestionImage");
        preLocalRangeStats =
            ReadTextureStats(result.texture, m_Width, m_Height, "RawDevelopmentPreLocalRangeStats");
        hasPreLocalRangeStats = true;
        m_RawDevelopmentStageStatsReadbacks.push_back(MakeRawSafetyStageReadback(rawSafetyStats));
        m_RawDevelopmentStageStatsReadbacks.push_back(
            MakeStageStatsReadback(
                Stack::RawAutoStartPoint::RawAutoStartPointStage::NeutralScene,
                neutralSceneStats,
                "Neutral scene analysis render using the current WB policy and optional RGB Denoise at 0 EV authored RAW Exposure before Local Exposure, Local Range, Finish Tone, and View Transform.",
                "scene-linear-neutral-rgb",
                true,
                false));
        m_RawDevelopmentStageStatsReadbacks.push_back(
            MakeStageStatsReadback(
                Stack::RawAutoStartPoint::RawAutoStartPointStage::RawPlacement,
                rawPlacementStats,
                "Current converted RAW texture after optional RGB Denoise and authored RAW Exposure, before legacy Local Exposure, Local Range, Finish Tone, and View Transform.",
                "scene-linear-raw-placement-rgb",
                true,
                false));
        if (!localRangeActive) {
            CaptureRawDevelopmentStageImageReadback(
                Stack::RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate,
                Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Fallback,
                result.texture,
                m_Width,
                m_Height,
                "scene-linear-pre-local-range-srgb",
                true,
                false);
            m_RawDevelopmentStageStatsReadbacks.push_back(
                MakeStageStatsReadback(
                    Stack::RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate,
                    preLocalRangeStats,
                    "Fallback local evidence from the current pre-Local-Range texture used by Local Range suggestions; Local Range is not active in the current recipe.",
                    "scene-linear-pre-local-range-rgb",
                    true,
                    false,
                    Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Fallback));
        }
    }
    if (result.texture != 0 && m_RawDevelopmentLocalRangeTargetSampleRequested) {
        CaptureRawDevelopmentLocalRangeTargetSample(
            result.texture,
            recipe.localRange,
            recipe.technical.workingSpace,
            postLocalExposureFingerprint);
    }
    if (captureAnalysis && result.texture != 0) {
        CaptureRawDevelopmentLocalRangeGraphScopeReadback(
            result.texture,
            recipe.localRange,
            m_Width,
            m_Height);
    }

    const bool regionMaskOverlayActive =
        m_RawDevelopmentLocalRangeOverlayRequestMode == "region-mask" &&
        (Stack::RawRecipe::SanitizeLocalRangeRecipe(recipe.localRange).regionMaskEnabled ||
            Stack::RawRecipe::SanitizeLocalRangeRecipe(recipe.localRange).colorMaskEnabled ||
            !Stack::RawRecipe::SanitizeLocalRangeRecipe(recipe.localRange).targetZones.empty());
    const bool targetOutlineOverlayActive =
        m_RawDevelopmentLocalRangeOverlayRequestMode == "target-outline" &&
        m_RawDevelopmentLocalRangeTargetPreviewRequest.enabled;
    if ((localRangeActive ||
            regionMaskOverlayActive ||
            targetOutlineOverlayActive) &&
        result.texture != 0) {
        const unsigned int preLocalRangeTexture = result.texture;
        if (!m_RawDevelopmentLocalRangeOverlayRequestMode.empty() &&
            m_RawDevelopmentLocalRangeOverlayRequestMode != "none") {
            std::string replacementOverlayMode =
                m_RawDevelopmentLocalRangeOverlayRequestMode;
            Stack::Renderer::ScopedGLTexture overlayTexture(
                RenderRawDevelopmentLocalRangeOverlay(
                    preLocalRangeTexture,
                    recipe.localRange,
                    recipe.technical.workingSpace,
                    replacementOverlayMode,
                    postLocalExposureFingerprint));
            if (overlayTexture) {
                ClearRawDevelopmentLocalRangeOverlay();
                m_RawDevelopmentLocalRangeOverlayWidth = m_Width;
                m_RawDevelopmentLocalRangeOverlayHeight = m_Height;
                m_RawDevelopmentLocalRangeOverlayMode =
                    std::move(replacementOverlayMode);
                m_RawDevelopmentLocalRangeOverlayTexture =
                    overlayTexture.Release();
            }
        }
        if (localRangeActive) {
            const CachedGraphTexture cachedPostLocalRange =
                FindRawDevelopStageCacheEntry(
                    postLocalRangeCacheKey,
                    postLocalRangeFingerprint);
            if (adoptCachedStage(cachedPostLocalRange)) {
                localRangeApplied = true;
            } else {
                ++m_LastGraphExecutionStats.rawStageCacheMisses;
                Stack::Renderer::ScopedGLTexture localRangeResult(
                    RenderRawDevelopmentLocalRange(
                        preLocalRangeTexture,
                        recipe.localRange,
                        recipe.technical.workingSpace,
                        postLocalExposureFingerprint));
                if (localRangeResult) {
                    QuickTextureStats inputStats;
                    QuickTextureStats outputStats;
                    if (captureAnalysis) {
                        inputStats = ProbeTextureStats(preLocalRangeTexture, m_Width, m_Height);
                        outputStats = ProbeTextureStats(localRangeResult.Get(), m_Width, m_Height);
                    }
                    const bool inputHasSignal =
                        captureAnalysis && inputStats.valid && inputStats.p99Luma > 0.00001f;
                    const bool outputIsBlank =
                        captureAnalysis &&
                        outputStats.valid &&
                        outputStats.p99Luma <= 0.000001f &&
                        outputStats.maxRgb <= 0.00001f;
                    if (inputHasSignal && outputIsBlank) {
                        std::cerr << "[RenderPipeline] RAW Development local range produced a blank output for node "
                                  << node.nodeId << " (input p99 luma " << inputStats.p99Luma
                                  << ", output p99 luma " << outputStats.p99Luma
                                  << "); passing pre-local-range texture through.\n";
                    } else {
                        if (result.owned && preLocalRangeTexture != 0) {
                            glDeleteTextures(1, &preLocalRangeTexture);
                        }
                        result.texture = localRangeResult.Release();
                        result.owned = true;
                        localRangeApplied = true;
                        StoreRawDevelopStageCacheEntry(
                            postLocalRangeCacheKey,
                            result.texture,
                            postLocalRangeFingerprint);
                    }
                }
            }
        }
    }
    if (captureAnalysis && localRangeActive && result.texture != 0) {
        if (localRangeApplied) {
            CaptureRawDevelopmentStageImageReadback(
                Stack::RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate,
                Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Complete,
                result.texture,
                m_Width,
                m_Height,
                "scene-linear-post-local-range-srgb",
                true,
                false);
            const RenderTextureStats postLocalRangeStats =
                ReadTextureStats(result.texture, m_Width, m_Height, "RawDevelopmentPostLocalRangeStats");
            m_RawDevelopmentStageStatsReadbacks.push_back(
                MakeStageStatsReadback(
                    Stack::RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate,
                    postLocalRangeStats,
                    "Post-Local-Range texture immediately before Finish Tone and View Transform.",
                    "scene-linear-post-local-range-rgb",
                    true,
                    false));
        } else if (hasPreLocalRangeStats) {
            CaptureRawDevelopmentStageImageReadback(
                Stack::RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate,
                Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Fallback,
                result.texture,
                m_Width,
                m_Height,
                "scene-linear-pre-local-range-srgb",
                true,
                false);
            m_RawDevelopmentStageStatsReadbacks.push_back(
                MakeStageStatsReadback(
                    Stack::RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate,
                    preLocalRangeStats,
                    "Fallback local evidence from the current pre-Local-Range texture; Local Range was active but no accepted post-Local-Range render was produced.",
                    "scene-linear-pre-local-range-rgb",
                    true,
                    false,
                    Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Fallback));
        }
    }

    auto renderRecipeLayer = [&](const nlohmann::json& layerJson, const char* fallbackType) {
        if (result.texture == 0) {
            return false;
        }
        const nlohmann::json authoredPayload =
            layerJson.is_object() ? layerJson : nlohmann::json::object();
        const std::string type =
            authoredPayload.contains("type") && authoredPayload["type"].is_string()
                ? authoredPayload["type"].get<std::string>()
                : std::string(fallbackType);
        const std::string layerCacheKey =
            std::to_string(node.nodeId) + ":__rawDevelopmentRecipeLayer:" + fallbackType;
        std::shared_ptr<LayerBase> layer;
        nlohmann::json layerPayload;
        try {
            auto cacheIt = m_RawDevelopmentRecipeLayerCache.find(layerCacheKey);
            if (cacheIt == m_RawDevelopmentRecipeLayerCache.end() ||
                !cacheIt->second.layer ||
                cacheIt->second.type != type) {
                std::shared_ptr<LayerBase> replacement =
                    LayerRegistry::CreateLayerFromTypeId(type);
                if (!replacement) {
                    return false;
                }

                CachedRawDevelopmentRecipeLayer replacementEntry;
                replacementEntry.type = type;
                replacement->InitializeGL();
                // Canonicalize the state produced by a fresh instance receiving
                // only its type. This preserves Deserialize's legacy missing-field
                // defaults (which are not always identical to member initializers)
                // while giving later retained-instance renders a complete reset.
                replacement->Deserialize(nlohmann::json{{ "type", type }});
                replacementEntry.defaultPayload = replacement->Serialize();
                replacementEntry.layer = std::move(replacement);
                cacheIt = m_RawDevelopmentRecipeLayerCache.insert_or_assign(
                    layerCacheKey,
                    std::move(replacementEntry)).first;
            }

            layer = cacheIt->second.layer;
            layerPayload = cacheIt->second.defaultPayload;
            for (auto authoredIt = authoredPayload.begin();
                 authoredIt != authoredPayload.end();
                 ++authoredIt) {
                layerPayload[authoredIt.key()] = authoredIt.value();
            }
            layerPayload["type"] = type;
            layer->Deserialize(layerPayload);
        } catch (const std::exception& error) {
            std::cerr << "[RenderPipeline] RAW Development " << type
                      << " recipe layer setup failed for node " << node.nodeId
                      << ": " << error.what() << "; passing input texture through.\n";
            return false;
        }
        if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(layer.get())) {
            toneCurve->SetAutoRewriteRenderContext(node.nodeId, node.requestRevision);
        }

        const unsigned int inputTexture = result.texture;
        Stack::Renderer::ScopedGLTexture processed(
            CreateGraphRenderTargetTexture());
        const unsigned int sourceTexture = m_SourceTexture != 0 ? m_SourceTexture : inputTexture;
        const bool renderedLayer = RenderIntoGraphTargetTexture(processed.Get(), [&](unsigned int) {
            layer->ExecuteWithSource(inputTexture, sourceTexture, m_Width, m_Height, m_Quad);
        });
        if (!renderedLayer || !processed) {
            std::cerr << "[RenderPipeline] RAW Development " << type
                      << " finish pass failed for node " << node.nodeId
                      << "; passing input texture through.\n";
            return false;
        }

        if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(layer.get());
            toneCurve && toneCurve->HasPendingAutoRewriteFeedback()) {
            m_ToneCurveAutoRewriteFeedback.push_back(toneCurve->TakePendingAutoRewriteFeedback());
        }

        if (captureAnalysis && type == "ToneCurve" && IsDefaultToneCurvePayload(layerPayload)) {
            const QuickTextureStats inputStats = ProbeTextureStats(inputTexture, m_Width, m_Height);
            const QuickTextureStats outputStats = ProbeTextureStats(processed.Get(), m_Width, m_Height);
            const bool inputHasSignal = inputStats.valid && inputStats.p99Luma > 0.00001f;
            const bool outputIsBlank =
                outputStats.valid &&
                outputStats.p99Luma <= 0.000001f &&
                outputStats.maxRgb <= 0.00001f;
            if (inputHasSignal && outputIsBlank) {
                std::cerr << "[RenderPipeline] RAW Development default Tone Curve produced a blank output for node "
                          << node.nodeId << " (input p99 luma " << inputStats.p99Luma
                          << ", output p99 luma " << outputStats.p99Luma
                          << "); passing input texture through.\n";
                return false;
            }
        }

        if (result.owned && inputTexture != 0) {
            glDeleteTextures(1, &inputTexture);
        }
        result.texture = processed.Release();
        result.owned = true;
        return true;
    };

    if (captureAnalysis && result.texture != 0) {
        CaptureRawDevelopmentGraphScopeReadback(
            RawDevelopmentGraphScopeStage::FinishToneInput,
            result.texture,
            m_Width,
            m_Height,
            "scene-linear-pre-finish-tone-rgb",
            true);
    }
    const bool finishToneHasPendingAutoCalibration =
        recipe.finishTone.layerJson.is_object() &&
        recipe.finishTone.layerJson.contains("autoCalibratePending") &&
        recipe.finishTone.layerJson["autoCalibratePending"].is_boolean() &&
        recipe.finishTone.layerJson["autoCalibratePending"].get<bool>();
    const CachedGraphTexture cachedPostFinishTone =
        finishToneHasPendingAutoCalibration
            ? CachedGraphTexture{}
            : FindRawDevelopStageCacheEntry(
                  postFinishToneCacheKey,
                  postFinishToneFingerprint);
    if (!adoptCachedStage(cachedPostFinishTone)) {
        ++m_LastGraphExecutionStats.rawStageCacheMisses;
        if (renderRecipeLayer(recipe.finishTone.layerJson, "ToneCurve")) {
            if (!finishToneHasPendingAutoCalibration) {
                StoreRawDevelopStageCacheEntry(
                    postFinishToneCacheKey,
                    result.texture,
                    postFinishToneFingerprint);
            }
        }
    }
    if (captureAnalysis && result.texture != 0) {
        CaptureRawDevelopmentStageImageReadback(
            Stack::RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate,
            Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Complete,
            result.texture,
            m_Width,
            m_Height,
            "scene-linear-pre-display-srgb",
            true,
            false);
        m_RawDevelopmentViewTransformInputStats =
            ReadTextureStats(result.texture, m_Width, m_Height, "RawDevelopmentViewTransformInputStats");
        m_RawDevelopmentStageStatsReadbacks.push_back(
            MakeStageStatsReadback(
                Stack::RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate,
                m_RawDevelopmentViewTransformInputStats,
                "Post-Local-Range and post-Finish-Tone texture immediately before View Transform.",
                "scene-linear-pre-display-rgb",
                true,
                false));
    }
    const bool viewTransformEnabled =
        Stack::RawRecipe::IsViewTransformEnabled(recipe);
    if (viewTransformEnabled) {
        (void)renderRecipeLayer(recipe.viewTransform.layerJson, "ViewTransform");
    }
    if (viewTransformEnabled && captureAnalysis && result.texture != 0) {
        const bool encodedSrgbOutput = recipe.viewTransform.layerJson.value(
            "encodeSrgbOutput",
            recipe.technical.encodeSrgbOutput);
        const char* displayMeasurementDomain = encodedSrgbOutput
            ? "display-mapped-srgb-encoded"
            : "display-mapped-linear-srgb";
        CaptureRawDevelopmentStageImageReadback(
            Stack::RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate,
            Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Complete,
            result.texture,
            m_Width,
            m_Height,
            displayMeasurementDomain,
            false,
            true);
        m_RawDevelopmentFinalDisplayStats =
            ReadTextureStats(result.texture, m_Width, m_Height, "RawDevelopmentFinalDisplayStats");
        m_RawDevelopmentStageStatsReadbacks.push_back(
            MakeStageStatsReadback(
                Stack::RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate,
                m_RawDevelopmentFinalDisplayStats,
                "Post-View-Transform texture used for the RAW workspace preview.",
                displayMeasurementDomain,
                false,
                true));
    }
    (void)fingerprint;
    return result;
}
