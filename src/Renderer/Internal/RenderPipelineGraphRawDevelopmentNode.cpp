#include "Renderer/RenderPipeline.h"

#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawLoader.h"
#include "Editor/LayerRegistry.h"
#include "Editor/Layers/ToneLayers.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/GLHelpers.h"
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
        !rawData.metadata.error.empty() ||
        rawData.metadata.pixelLayout == Raw::RawPixelLayout::MosaicBayer) {
        // Keep the source CFA resident and let RawGpuPipeline demosaic from
        // sensor coordinates directly into the display-sized output. A
        // pre-demosaic proxy is both visibly soft and expensive to rebuild
        // whenever adaptive interaction changes the requested edge.
        return rawData;
    }

    const std::string previewCacheKey =
        Stack::Renderer::RawPreviewProxy::BuildCacheKey(
            sourceCacheKey,
            rawData,
            m_PreviewMaxDimension);
    std::string& cachedKey = m_RawPreviewDataCacheKeys[cacheNodeId];
    Raw::RawImageData& cachedPreview = m_RawPreviewDataCache[cacheNodeId];
    if (cachedKey == previewCacheKey &&
        Stack::Renderer::RawPreviewProxy::HasPixels(cachedPreview) &&
        cachedPreview.metadata.error.empty()) {
        return cachedPreview;
    }

    Raw::RawImageData preview;
    if (!Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(
            rawData,
            m_PreviewMaxDimension,
            preview)) {
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
    const auto cancellationRequested = [this]() {
        return m_ShouldCancelRender && m_ShouldCancelRender();
    };
    const auto recordFailure = [&](const std::string& error) {
        m_LastGraphExecutionStats.lastSpecializedFailureNodeId = node.nodeId;
        m_LastGraphExecutionStats.lastSpecializedFailure = error;
    };
    if (cancellationRequested()) {
        return result;
    }

    const Stack::RawRecipe::RawDevelopmentRecipe& recipe = node.rawDevelopment.recipe;
    const std::string& sourcePath = recipe.source.sourcePath;
    const std::shared_ptr<const Raw::RawImageData>& embeddedRawData =
        node.rawDevelopment.embeddedRawData;
    if (sourcePath.empty() && !embeddedRawData) {
        recordFailure("The RAW Development source image is unavailable.");
        return result;
    }

    const std::string cacheKeyPath =
        Stack::Renderer::RawDevelopmentCache::BuildSourceDataIdentity(
            recipe.source);
    auto sourceLease = embeddedRawData;
    if (!sourceLease) {
        const auto cached = m_RawSharedSourceData.find(cacheKeyPath);
        if (cached != m_RawSharedSourceData.end()) sourceLease = cached->second;
        else {
            auto loaded = std::make_shared<Raw::RawImageData>();
            Raw::RawLoader::LoadFile(sourcePath, *loaded);
            sourceLease = std::move(loaded);
            // Immutable decoder data is shared by Original and Current. Bound
            // retention when one renderer visits several independent sources.
            if (m_RawSharedSourceData.size() >= 8) m_RawSharedSourceData.erase(m_RawSharedSourceData.begin());
            m_RawSharedSourceData.emplace(cacheKeyPath, sourceLease);
        }
    }
    const Raw::RawImageData* rawData = sourceLease.get();

    const bool rawDataHasPixels =
        Stack::Renderer::RawPreviewProxy::HasPixels(*rawData);
    if (!rawDataHasPixels || !rawData->metadata.error.empty()) {
        const std::string error = !rawData->metadata.error.empty()
            ? rawData->metadata.error
            : "LibRaw did not produce a usable raw buffer.";
        recordFailure(error);
        std::cerr << "[RAW] Load failed for RAW Development node " << node.nodeId
                  << " (" << sourcePath << "): " << error << "\n";
        return result;
    }
    if (cancellationRequested()) {
        return result;
    }

    const Raw::RawDevelopSettings settings = Stack::RawRecipe::ToRawDevelopSettings(recipe);
    m_RawViewportAppliedRegion = {};
    m_RawViewportGpuTiming.Mark(Raw::ViewportStage::RawBase);
    const bool captureAnalysis = m_RawDevelopmentAnalysisEnabled;
    const bool detailedDamageValidation =
        m_RawDevelopmentViewportValidationEnabled &&
        !m_RawDevelopmentInteractivePreview;
    Stack::RawAutoStartPoint::RawAutoStartPointRawSafetyStats rawSafetyStats;
    if (captureAnalysis) {
        rawSafetyStats = BuildRawSafetyStats(*rawData, settings);
    }
    m_PreLocalExposureSummaries.erase(node.nodeId);
    Raw::RawDevelopSettings rawRenderSettings = settings;
    rawRenderSettings.toneCurvePoints.clear();
    const Raw::RawImageData& renderRawData =
        ResolveRawPreviewRenderData(
            node.nodeId, *rawData, cacheKeyPath);
    if (cancellationRequested()) {
        return result;
    }
    const std::string rawPlacementCacheKey =
        std::to_string(node.nodeId) + ":__rawDevelopmentPlacement";
    const std::string neutralPlacementCacheKey =
        std::to_string(node.nodeId) + ":__rawDevelopmentNeutral";
    const std::string rawBaseCacheKey =
        std::to_string(node.nodeId) + ":__rawDevelopmentRgbBase";
    const std::string rgbDenoiseCacheKey =
        std::to_string(node.nodeId) + ":__rawDevelopmentRgbDenoise";
    const std::string postLocalRangeCacheKey =
        std::to_string(node.nodeId) + ":__rawDevelopmentPostLocalRange";
    const std::string postFinishToneCacheKey =
        std::to_string(node.nodeId) + ":__rawDevelopmentPostFinishTone";
    const std::string postColorWarpCacheKey =
        std::to_string(node.nodeId) + ":__rawDevelopmentPostColorWarp";
    const std::string postViewTransformCacheKey =
        std::to_string(node.nodeId) + ":__rawDevelopmentPostViewTransform";
    const std::string postOutputCropCacheKey =
        std::to_string(node.nodeId) + ":__rawDevelopmentPostOutputCrop";
    const std::string denoiseDiagnosticOutputCropCacheKey =
        std::to_string(node.nodeId) +
        ":__rawDevelopmentDenoiseDiagnosticOutputCrop";
    Stack::Renderer::RawDevelopmentCache::StageFingerprints
        stageFingerprints =
            Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(
                recipe,
                m_PreviewMaxDimension);
    const bool regionalOutput = m_RawViewportRequest.visible.Partial() &&
        !captureAnalysis && m_RawDevelopmentGraphScopeStage == RawDevelopmentGraphScopeStage::None &&
        !m_RawDevelopmentViewportValidationEnabled;
    if (regionalOutput) {
        const int first = static_cast<int>(Raw::ViewportFirstRegionalStage(recipe));
        std::size_t* fingerprints[] = {&stageFingerprints.rawBase, &stageFingerprints.neutralPlacement,
            &stageFingerprints.rawPlacement, &stageFingerprints.postLocalRange, &stageFingerprints.postFinishTone,
            &stageFingerprints.postColorWarp, &stageFingerprints.postViewTransform, &stageFingerprints.postOutputCrop};
        for (int stage = first; stage < 8; ++stage)
            Stack::Renderer::RawDevelopmentCache::MixJsonHash(*fingerprints[stage], m_RawViewportRequest.visible.Fingerprint());
    }
    if (regionalOutput)
        Stack::Renderer::RawDevelopmentCache::MixJsonHash(
            stageFingerprints.calibratedNeutral, m_RawViewportRequest.visible.Fingerprint());
    const std::size_t calibratedNeutralFingerprint = stageFingerprints.calibratedNeutral;
    const auto calibration = Stack::RawRecipe::BuildColorCalibrationTransform(
        recipe.colorCalibration, recipe.technical.workingSpace);
    const std::size_t rawPlacementFingerprint =
        stageFingerprints.rawPlacement;
    const std::size_t neutralPlacementFingerprint =
        stageFingerprints.neutralPlacement;
    const std::size_t rawBaseFingerprint = stageFingerprints.rawBase;
    const std::size_t postLocalRangeFingerprint =
        stageFingerprints.postLocalRange;
    const std::size_t postFinishToneFingerprint =
        stageFingerprints.postFinishTone;
    const std::size_t postColorWarpFingerprint =
        stageFingerprints.postColorWarp;
    const std::size_t postViewTransformFingerprint =
        stageFingerprints.postViewTransform;
    const std::size_t postOutputCropFingerprint =
        stageFingerprints.postOutputCrop;
    const Stack::RawRecipe::RawRgbDenoiseRecipe rgbDenoiseSettings =
        Stack::RawRecipe::SanitizeRgbDenoiseRecipe(recipe.rgbDenoise);
    const bool rgbDenoiseDiagnosticRequested =
        rgbDenoiseSettings.diagnosticMode !=
            Stack::RawRecipe::RawDenoiseDiagnosticMode::None;
    const bool rgbDenoiseStageRequested =
        Stack::RawRecipe::IsRgbDenoiseActive(rgbDenoiseSettings) ||
        rgbDenoiseDiagnosticRequested;
    Raw::RawDevelopSettings neutralSceneSettings = rawRenderSettings;
    neutralSceneSettings.exposureStops = 0.0f;
    neutralSceneSettings.toneCurvePoints.clear();
    const bool currentRawRenderIsNeutralScene =
        std::fabs(rawRenderSettings.exposureStops) <= 0.0001f &&
        rawRenderSettings.toneCurvePoints.empty() && !calibration.active;
    const char* neutralGradingMeasurementDomain =
        recipe.technical.workingSpace == Raw::RawWorkingSpace::LinearRec2020D65
            ? "scene-linear-neutral-rec2020-d65"
            : "scene-linear-neutral-srgb-d65";
    RenderTextureStats neutralSceneStats;

    using RawCacheStage =
        Stack::Renderer::RawDevelopmentCache::Stage;
    const auto prewarmStageDescriptor =
        [&](RawCacheStage stage,
            const std::string*& outKey,
            const std::size_t*& outFingerprint) {
            outKey = nullptr;
            outFingerprint = nullptr;
            switch (stage) {
                case RawCacheStage::RawBase:
                    outKey = &rawBaseCacheKey;
                    outFingerprint = &rawBaseFingerprint;
                    break;
                case RawCacheStage::NeutralPlacement:
                    outKey = rgbDenoiseStageRequested
                        ? &rgbDenoiseCacheKey
                        : &neutralPlacementCacheKey;
                    outFingerprint = &neutralPlacementFingerprint;
                    break;
                case RawCacheStage::RawPlacement:
                    outKey = &rawPlacementCacheKey;
                    outFingerprint = &rawPlacementFingerprint;
                    break;
                case RawCacheStage::PostLocalRange:
                    outKey = &postLocalRangeCacheKey;
                    outFingerprint = &postLocalRangeFingerprint;
                    break;
                case RawCacheStage::PostFinishTone:
                    outKey = &postFinishToneCacheKey;
                    outFingerprint = &postFinishToneFingerprint;
                    break;
                case RawCacheStage::PostColorWarp:
                    outKey = &postColorWarpCacheKey;
                    outFingerprint = &postColorWarpFingerprint;
                    break;
                case RawCacheStage::PostViewTransform:
                    outKey = &postViewTransformCacheKey;
                    outFingerprint = &postViewTransformFingerprint;
                    break;
                case RawCacheStage::PostOutputCrop:
                    outKey = &postOutputCropCacheKey;
                    outFingerprint = &postOutputCropFingerprint;
                    break;
            }
        };
    const auto clearOwnedPrewarmResult = [&]() {
        if (result.owned && result.texture != 0) {
            glDeleteTextures(1, &result.texture);
        }
        result = {};
    };
    const auto cacheResultStage = [&result, this](
        const std::string& key,
        std::size_t fingerprint) {
        const bool transferOwnership = result.owned;
        if (StoreRawDevelopStageCacheEntry(
                key,
                result.texture,
                fingerprint,
                transferOwnership) &&
            transferOwnership) {
            result.owned = false;
        }
    };
    const auto finishCachePrewarmStage =
        [&](RawCacheStage stage,
            const std::string& cacheKey,
            std::size_t stageFingerprint,
            bool allowCurrentBorrowedTexture = false) {
            if (!m_RawDevelopmentCachePrewarmActive ||
                m_RawDevelopmentCachePrewarmStage != stage) {
                return false;
            }
            if (m_RawDevelopmentCachePrewarmFingerprint !=
                stageFingerprint) {
                clearOwnedPrewarmResult();
                return true;
            }
            const CachedGraphTexture cached =
                FindRawDevelopStageCacheEntry(
                    cacheKey,
                    stageFingerprint);
            const CachedGraphTexture neutralPlacementAlias =
                allowCurrentBorrowedTexture &&
                    stage == RawCacheStage::RawPlacement
                ? FindRawDevelopStageCacheEntry(
                      rgbDenoiseStageRequested
                          ? rgbDenoiseCacheKey
                          : neutralPlacementCacheKey,
                      neutralPlacementFingerprint)
                : CachedGraphTexture{};
            if (cached.texture != 0 &&
                cached.width > 0 &&
                cached.height > 0) {
                clearOwnedPrewarmResult();
                result.texture = cached.texture;
                result.owned = false;
                m_Width = cached.width;
                m_Height = cached.height;
                m_RawDevelopmentCachePrewarmCompleted = true;
            } else if (neutralPlacementAlias.texture != 0 &&
                       neutralPlacementAlias.width > 0 &&
                       neutralPlacementAlias.height > 0) {
                // At exactly 0 EV, NeutralPlacement and RawPlacement are the
                // same pixels. Avoid a duplicate cache clone while still
                // treating the requested upstream input as ready.
                clearOwnedPrewarmResult();
                result.texture = neutralPlacementAlias.texture;
                result.owned = false;
                m_Width = neutralPlacementAlias.width;
                m_Height = neutralPlacementAlias.height;
                m_RawDevelopmentCachePrewarmCompleted = true;
            } else {
                clearOwnedPrewarmResult();
            }
            return true;
        };

    if (m_RawDevelopmentCachePrewarmActive) {
        const std::string* targetKey = nullptr;
        const std::size_t* targetFingerprint = nullptr;
        prewarmStageDescriptor(
            m_RawDevelopmentCachePrewarmStage,
            targetKey,
            targetFingerprint);
        if (targetKey == nullptr ||
            targetFingerprint == nullptr ||
            *targetFingerprint == 0 ||
            m_RawDevelopmentCachePrewarmFingerprint !=
                *targetFingerprint) {
            return result;
        }
        const CachedGraphTexture ready =
            FindRawDevelopStageCacheEntry(
                *targetKey,
                *targetFingerprint);
        if (ready.texture != 0 && ready.width > 0 && ready.height > 0) {
            result.texture = ready.texture;
            result.owned = false;
            m_Width = ready.width;
            m_Height = ready.height;
            m_RawDevelopmentCachePrewarmCompleted = true;
            return result;
        }
        if (m_RawDevelopmentCachePrewarmStage ==
                RawCacheStage::RawPlacement &&
            currentRawRenderIsNeutralScene) {
            const CachedGraphTexture neutralReady =
                FindRawDevelopStageCacheEntry(
                    rgbDenoiseStageRequested
                        ? rgbDenoiseCacheKey
                        : neutralPlacementCacheKey,
                    neutralPlacementFingerprint);
            if (neutralReady.texture != 0 &&
                neutralReady.width > 0 &&
                neutralReady.height > 0) {
                result.texture = neutralReady.texture;
                result.owned = false;
                m_Width = neutralReady.width;
                m_Height = neutralReady.height;
                m_RawDevelopmentCachePrewarmCompleted = true;
                return result;
            }
        }
    }
    const QuickTextureStats authoredPlacementValidationStats =
        m_RawDevelopmentViewportValidationEnabled
            ? ProbeTextureStats(result.texture, m_Width, m_Height)
            : QuickTextureStats{};
    if (cancellationRequested()) {
        return result;
    }
    Stack::Renderer::ScopedGLTexture regionalNeutralOwner;
    Stack::Renderer::ScopedGLTexture zoneAreaReference;
    m_RawZoneAreasFloatOutput = !recipe.localRange.areas.empty();
    std::vector<Stack::RawRecipe::RawZoneAreaStatistics> zoneAreaStatistics;
    const auto selectNeutralRegion = [&](unsigned int& texture, int& width, int& height) {
        GraphNodeRenderResult regionInput;
        regionInput.texture = texture;
        regionInput.owned = false;
        m_Width = width;
        m_Height = height;
        ApplyRawViewportRegion(regionInput, recipe, Raw::ViewportStage::RawPlacement, neutralPlacementFingerprint, node.nodeId);
        texture = regionInput.texture;
        width = m_Width;
        height = m_Height;
        if (regionInput.owned) regionalNeutralOwner.Reset(texture);
    };
    if (!rgbDenoiseStageRequested) {
        const CachedGraphTexture cachedNeutral =
            FindRawDevelopStageCacheEntry(
                neutralPlacementCacheKey,
                neutralPlacementFingerprint);
        unsigned int neutralTexture = cachedNeutral.texture;
        int neutralWidth = cachedNeutral.width;
        int neutralHeight = cachedNeutral.height;
        if (neutralTexture != 0 && neutralWidth > 0 && neutralHeight > 0) {
            ++m_LastGraphExecutionStats.rawStageCacheHits;
        } else {
            ++m_LastGraphExecutionStats.rawStageCacheMisses;
            neutralTexture =
                RenderRawPipelineWithTelemetry(
                    node.nodeId,
                    renderRawData,
                    neutralSceneSettings,
                    m_PreviewMaxDimension);
            neutralWidth = m_RawPipelines[node.nodeId].GetOutputWidth();
            neutralHeight = m_RawPipelines[node.nodeId].GetOutputHeight();
            if (neutralTexture != 0 &&
                neutralWidth > 0 &&
                neutralHeight > 0) {
                m_Width = neutralWidth;
                m_Height = neutralHeight;
                StoreRawDevelopStageCacheEntry(
                    neutralPlacementCacheKey,
                    neutralTexture,
                    neutralPlacementFingerprint);
            }
        }
        if (neutralTexture != 0 &&
            neutralWidth > 0 &&
            neutralHeight > 0 &&
            !currentRawRenderIsNeutralScene) {
            if (captureAnalysis) {
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
            CaptureRawDevelopmentGradingScopeReadback(
                RawDevelopmentGradingScopeSource::NeutralScene,
                neutralTexture,
                neutralWidth,
                neutralHeight,
                neutralGradingMeasurementDomain,
                recipe.technical.workingSpace,
                true,
                false);
        }
        result.texture = neutralTexture;
        result.owned = false;
        m_Width = neutralWidth;
        m_Height = neutralHeight;
        if (finishCachePrewarmStage(
                RawCacheStage::NeutralPlacement,
                neutralPlacementCacheKey,
                neutralPlacementFingerprint)) {
            return result;
        }

        selectNeutralRegion(neutralTexture, neutralWidth, neutralHeight);
        if (m_RawZoneAreasFloatOutput && neutralTexture != 0) {
            zoneAreaReference.Reset(RenderRawDevelopmentExposure(neutralTexture, 0.0f, &calibration));
            if (!zoneAreaReference) throw std::runtime_error("Zones reference image unavailable");
        }
        const CachedGraphTexture cachedRawPlacement =
            FindRawDevelopStageCacheEntry(rawPlacementCacheKey, rawPlacementFingerprint);
        if (cachedRawPlacement.texture != 0 &&
            cachedRawPlacement.width > 0 &&
            cachedRawPlacement.height > 0) {
            ++m_LastGraphExecutionStats.rawStageCacheHits;
            result.texture = cachedRawPlacement.texture;
            result.owned = false;
            m_Width = cachedRawPlacement.width;
            m_Height = cachedRawPlacement.height;
        } else if (neutralTexture != 0 &&
                   neutralWidth > 0 &&
                   neutralHeight > 0 &&
                   currentRawRenderIsNeutralScene) {
            // Neutral placement is exactly the authored 0 EV placement. Keep
            // one cache owner instead of cloning the same texture under two
            // stage keys.
            result.texture = neutralTexture;
            result.owned = false;
            m_Width = neutralWidth;
            m_Height = neutralHeight;
        } else {
            ++m_LastGraphExecutionStats.rawStageCacheMisses;
            // Keep denoise OFF on the same neutral-scene -> Exposure boundary
            // used when denoise is ON. The only semantic difference between
            // the two toggle states should be the optional denoise pass; a
            // direct RAW render here otherwise makes the toggle switch both
            // the image operation and the materialization path. That can
            // expose stale or partially initialized intermediate textures as
            // apparently reversed brightness changes during rapid edits.
            m_Width = neutralWidth;
            m_Height = neutralHeight;
            m_RawViewportGpuTiming.Mark(Raw::ViewportStage::RawPlacement);
            result.texture = RenderRawDevelopmentExposure(
                neutralTexture,
                recipe.preToneExposureEv, &calibration);
            result.owned = result.texture != 0;
            if (result.texture != 0) {
                cacheResultStage(
                    rawPlacementCacheKey,
                    rawPlacementFingerprint);
            } else {
                if (calibration.active || m_RawViewportAppliedRegion.Valid())
                    throw std::runtime_error("RAW calibration/exposure rendering failed");
                // Preserve a last-resort path if the fullscreen exposure
                // program or target cannot be created.
                result.texture =
                    RenderRawPipelineWithTelemetry(
                        node.nodeId,
                        renderRawData,
                        rawRenderSettings,
                        m_PreviewMaxDimension);
                m_Width = m_RawPipelines[node.nodeId].GetOutputWidth();
                m_Height = m_RawPipelines[node.nodeId].GetOutputHeight();
                result.owned = false;
            }
        }
    } else {
        const bool aiDenoiseMethod =
            rgbDenoiseSettings.method !=
            Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1;
        if (aiDenoiseMethod) {
            const Stack::Restormer::ValidationResult package =
                Stack::Restormer::Client::Instance().Validate(
                    rgbDenoiseSettings);
            if (!package.ok) {
                m_RawRgbDenoiseState->error = package.error;
                std::cerr
                    << "[RAW] Restormer RGB denoise blocked RAW Development "
                    << "node " << node.nodeId << ": " << package.error << "\n";
                return result;
            }
        }
        bool temporaryDenoise = false;
        const CachedGraphTexture cachedDenoise = FindRawViewportDenoiseInput(
            rgbDenoiseCacheKey, recipe, temporaryDenoise);
        unsigned int neutralTexture = cachedDenoise.texture;
        Stack::Renderer::ScopedGLTexture neutralTextureOwner;
        if (temporaryDenoise) neutralTextureOwner.Reset(neutralTexture);
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
                    RenderRawPipelineWithTelemetry(
                        node.nodeId,
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

            if (cancellationRequested()) {
                return result;
            }
            if (baseTexture != 0 && m_Width > 0 && m_Height > 0) {
                m_RawViewportGpuTiming.Mark(Raw::ViewportStage::NeutralPlacement);
                ++m_LastGraphExecutionStats.rawRgbDenoisePasses;
                neutralTexture = RenderRawDevelopmentRgbDenoise(
                    baseTexture,
                    rgbDenoiseSettings,
                    recipe.technical.workingSpace,
                    rawBaseFingerprint,
                    Raw::Denoise::BuildRawRgbNoiseModel(
                        renderRawData,
                        neutralSceneSettings),
                    rgbDenoiseCacheKey,
                    renderRawData.metadata.visibleWidth > 0
                        ? renderRawData.metadata.visibleWidth
                        : renderRawData.metadata.rawWidth,
                    renderRawData.metadata.visibleHeight > 0
                        ? renderRawData.metadata.visibleHeight
                        : renderRawData.metadata.rawHeight,
                    &recipe);
                if (neutralTexture != 0) {
                    neutralTextureOwner.Reset(neutralTexture);
                    neutralWidth = m_Width;
                    neutralHeight = m_Height;
                    const bool storedDenoise = StoreRawDevelopStageCacheEntry(
                        rgbDenoiseCacheKey,
                        neutralTexture,
                        neutralPlacementFingerprint);
                    if (storedDenoise && m_PreviewMaxDimension == 0 &&
                        !rgbDenoiseDiagnosticRequested && !IsRawRgbDenoiseAsyncPending())
                        m_RawDevelopStageImageCache.at(rgbDenoiseCacheKey).front().viewportNativeDependency = true;
                } else {
                    const bool aiMethod =
                        rgbDenoiseSettings.method !=
                        Stack::RawRecipe::RawRgbDenoiseMethod::
                            ClassicalMultiscaleV1;
                    const bool denoiseRequested =
                        Stack::RawRecipe::IsRgbDenoiseActive(
                            rgbDenoiseSettings) ||
                        rgbDenoiseSettings.diagnosticMode !=
                            Stack::RawRecipe::RawDenoiseDiagnosticMode::None;
                    if (aiMethod && denoiseRequested) {
                        neutralTexture = 0;
                        neutralWidth = 0;
                        neutralHeight = 0;
                        std::cerr
                            << "[RAW] Restormer RGB denoise blocked RAW "
                            << "Development node " << node.nodeId << ": "
                            << (m_RawRgbDenoiseState->error.empty()
                                ? "external model inference failed"
                                : m_RawRgbDenoiseState->error)
                            << "\n";
                    } else {
                        neutralTexture = baseTexture;
                        neutralWidth = m_Width;
                        neutralHeight = m_Height;
                        if (denoiseRequested) {
                            // The built-in method may fail open because it
                            // does not represent an authored external model.
                            std::cerr
                                << "[RAW] Classical RGB denoise failed for RAW "
                                << "Development node " << node.nodeId
                                << "; passing neutral demosaic through.\n";
                        }
                    }
                }
            }
        }

        // Keep a completed native denoise dependency, but yield before
        // downstream exposure/analysis when foreground input supersedes it.
        if (cancellationRequested()) return result;

        if (rgbDenoiseDiagnosticRequested &&
            neutralTexture != 0 &&
            neutralWidth > 0 &&
            neutralHeight > 0) {
            result.texture = neutralTexture;
            result.owned = false;
            m_Width = neutralWidth;
            m_Height = neutralHeight;
            if (neutralTextureOwner.Get() == neutralTexture) {
                result.texture = neutralTextureOwner.Release();
                result.owned = true;
            }
            // A diagnostic is already a display image. Only preserve output
            // geometry; later exposure, tone, color, and view operations would
            // alter the mask and repeat unrelated work.
            RenderRawDevelopmentOutputCropStage(
                result,
                recipe.cropRotation,
                denoiseDiagnosticOutputCropCacheKey,
                postOutputCropFingerprint);
            return result;
        }

        if (neutralTexture != 0 &&
            neutralWidth > 0 &&
            neutralHeight > 0 &&
            !currentRawRenderIsNeutralScene) {
            if (captureAnalysis) {
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
            CaptureRawDevelopmentGradingScopeReadback(
                RawDevelopmentGradingScopeSource::NeutralScene,
                neutralTexture,
                neutralWidth,
                neutralHeight,
                neutralGradingMeasurementDomain,
                recipe.technical.workingSpace,
                true,
                false);
        }
        if (m_RawDevelopmentCachePrewarmActive &&
            m_RawDevelopmentCachePrewarmStage ==
                RawCacheStage::NeutralPlacement) {
            result.texture = neutralTexture;
            result.owned = false;
            m_Width = neutralWidth;
            m_Height = neutralHeight;
            if (finishCachePrewarmStage(
                    RawCacheStage::NeutralPlacement,
                    rgbDenoiseCacheKey,
                    neutralPlacementFingerprint)) {
                return result;
            }
        }

        selectNeutralRegion(neutralTexture, neutralWidth, neutralHeight);
        if (m_RawZoneAreasFloatOutput && neutralTexture != 0) {
            zoneAreaReference.Reset(RenderRawDevelopmentExposure(neutralTexture, 0.0f, &calibration));
            if (!zoneAreaReference) throw std::runtime_error("Zones reference image unavailable");
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
            m_RawViewportGpuTiming.Mark(Raw::ViewportStage::RawPlacement);
            result.texture = RenderRawDevelopmentExposure(
                neutralTexture,
                recipe.preToneExposureEv, &calibration);
            result.owned = result.texture != 0;
            if (result.texture != 0) {
                cacheResultStage(
                    rawPlacementCacheKey,
                    rawPlacementFingerprint);
            } else {
                if (calibration.active || m_RawViewportAppliedRegion.Valid())
                    throw std::runtime_error("RAW calibration/exposure rendering failed");
                // The exposure pass is deliberately simple, but preserve a
                // last-resort path through the established RAW renderer.
                result.texture =
                    RenderRawPipelineWithTelemetry(
                        node.nodeId,
                        renderRawData,
                        rawRenderSettings,
                        m_PreviewMaxDimension);
                m_Width = m_RawPipelines[node.nodeId].GetOutputWidth();
                m_Height = m_RawPipelines[node.nodeId].GetOutputHeight();
                result.owned = false;
            }
        }

    }
    if (regionalNeutralOwner.Get() && result.texture == regionalNeutralOwner.Get()) {
        result.texture = regionalNeutralOwner.Release();
        result.owned = true;
    }
    if (result.texture == 0) {
        const std::string& error = m_RawPipelines[node.nodeId].GetLastError();
        recordFailure(error.empty() ? "RAW processing did not produce a GPU image." : error);
        std::cerr << "[RAW] Render failed for RAW Development node " << node.nodeId
                  << " (" << sourcePath << "): "
                  << (error.empty() ? "unknown RAW GPU failure" : error)
                  << "\n";
        return result;
    }
    if (finishCachePrewarmStage(
            RawCacheStage::RawPlacement,
            rawPlacementCacheKey,
            rawPlacementFingerprint,
            currentRawRenderIsNeutralScene)) {
        return result;
    }

    if (currentRawRenderIsNeutralScene) {
        CaptureRawDevelopmentGradingScopeReadback(
            RawDevelopmentGradingScopeSource::NeutralScene,
            result.texture,
            m_Width,
            m_Height,
            neutralGradingMeasurementDomain,
            recipe.technical.workingSpace,
            true,
            false);
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
    if (cancellationRequested()) {
        return result;
    }
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
    if (cancellationRequested()) {
        return result;
    }
    int spatialSourceWidth = Raw::DisplayWidth(rawData->metadata);
    int spatialSourceHeight = Raw::DisplayHeight(rawData->metadata);
    if (std::abs(recipe.cropRotation.rotationDegrees) % 180 == 90)
        std::swap(spatialSourceWidth, spatialSourceHeight);
    ApplyRawViewportRegion(result, recipe, Raw::ViewportStage::PostLocalRange, rawPlacementFingerprint, node.nodeId);
    m_RawViewportGpuTiming.Mark(Raw::ViewportStage::PostLocalRange);
    const bool localRangeActive = Stack::RawRecipe::IsLocalRangeEnabled(recipe);
    const bool globalLocalRangeActive =
        Stack::RawRecipe::IsLocalRangeEnabled(recipe.localRange);
    if (cancellationRequested()) {
        return result;
    }
    RenderTextureStats preLocalRangeStats;
    bool hasPreLocalRangeStats = false;
    bool localRangeApplied = false;
    bool adoptedPostLocalRange = false;

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
                "Neutral scene analysis render using the current WB policy and optional RGB Denoise at 0 EV authored RAW Exposure before Local Range, Finish Tone, and View Transform.",
                "scene-linear-neutral-rgb",
                true,
                false));
        m_RawDevelopmentStageStatsReadbacks.push_back(
            MakeStageStatsReadback(
                Stack::RawAutoStartPoint::RawAutoStartPointStage::RawPlacement,
                rawPlacementStats,
                "Current converted RAW texture after optional RGB Denoise and authored RAW Exposure, before Local Range, Finish Tone, and View Transform.",
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
            recipe.technical.processingVersion,
            rawPlacementFingerprint);
    }
    if ((captureAnalysis ||
            m_RawDevelopmentGraphScopeStage == RawDevelopmentGraphScopeStage::LocalRangeInput) && result.texture != 0) {
        CaptureRawDevelopmentLocalRangeGraphScopeReadback(
            result.texture,
            recipe.localRange,
            m_Width,
            m_Height, recipe.technical.workingSpace, spatialSourceWidth, spatialSourceHeight);
        m_RawDevelopmentGraphScopeReadback.inputExposureEv = recipe.preToneExposureEv;
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
        if (cancellationRequested()) {
            return result;
        }
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
                    recipe.technical.processingVersion,
                    replacementOverlayMode,
                    rawPlacementFingerprint, spatialSourceWidth, spatialSourceHeight));
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
        if (globalLocalRangeActive) {
            const CachedGraphTexture cachedPostLocalRange =
                FindRawDevelopStageCacheEntry(
                    postLocalRangeCacheKey,
                    postLocalRangeFingerprint);
            if (adoptCachedStage(cachedPostLocalRange)) {
                localRangeApplied = true;
                adoptedPostLocalRange = true;
            } else {
                ++m_LastGraphExecutionStats.rawStageCacheMisses;
                Stack::Renderer::ScopedGLTexture localRangeResult(
                    RenderRawDevelopmentLocalRange(
                        preLocalRangeTexture,
                        recipe.localRange,
                        recipe.technical.workingSpace,
                        recipe.technical.processingVersion,
                        rawPlacementFingerprint, spatialSourceWidth, spatialSourceHeight));
                if (localRangeResult && zoneAreaReference &&
                    Stack::RawRecipe::HasZoneAreaGain(recipe.localRange)) {
                    const unsigned int adjusted = m_RawZoneAreaRenderer.Render(
                        localRangeResult.Get(), zoneAreaReference.Get(), m_Width, m_Height,
                        recipe, m_RawDevelopmentGraphScopeStage == RawDevelopmentGraphScopeStage::LocalRangeInput,
                        m_PreviewMaxDimension <= 0, zoneAreaStatistics, cancellationRequested, calibratedNeutralFingerprint);
                    if (!adjusted) {
                        if (cancellationRequested()) return result;
                        throw std::runtime_error("Zones area rendering failed");
                    }
                    localRangeResult.Reset(adjusted);
                }
                if (localRangeResult) {
                    QuickTextureStats inputStats;
                    QuickTextureStats outputStats;
                    bool damagedOutput = false;
                    // A deliberately large negative area gain can be very dark.
                    if (detailedDamageValidation && !Stack::RawRecipe::HasZoneAreaGain(recipe.localRange)) {
                        inputStats = ProbeTextureStats(
                            preLocalRangeTexture,
                            m_Width,
                            m_Height);
                        outputStats = ProbeTextureStats(
                            localRangeResult.Get(),
                            m_Width,
                            m_Height);
                        damagedOutput = IsImplausiblyDamagedTextureOutput(
                            inputStats,
                            outputStats);
                    }
                    if (damagedOutput) {
                        std::cerr << "[RenderPipeline] RAW Development local range produced an invalid or collapsed output for node "
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
                    }
                }
            }
        }
    }
    if (!globalLocalRangeActive && localRangeActive && result.texture != 0) {
        const CachedGraphTexture cachedPostLocalRange =
            FindRawDevelopStageCacheEntry(postLocalRangeCacheKey,
                postLocalRangeFingerprint);
        if (adoptCachedStage(cachedPostLocalRange)) {
            localRangeApplied = true;
            adoptedPostLocalRange = true;
        }
    }
    if (!adoptedPostLocalRange && result.texture != 0) {
        bool appliedGradient = false;
        bool gradientFailure = false;
        const unsigned int gradientInput = result.texture;
        Stack::Renderer::ScopedGLTexture accumulatedEv;
        Stack::RawRecipe::RawGradientMask lastMask;
        for (const auto& adjustment : recipe.evGradients) {
            if (!adjustment.mask.enabled ||
                !Stack::RawRecipe::IsLocalRangeEnabled(adjustment.curve)) continue;
            Stack::Renderer::ScopedGLTexture processed(
                RenderRawDevelopmentLocalRange(gradientInput, adjustment.curve,
                    recipe.technical.workingSpace,
                    recipe.technical.processingVersion,
                    rawPlacementFingerprint, spatialSourceWidth, spatialSourceHeight));
            if (!processed) { gradientFailure = true; break; }
            Stack::Renderer::ScopedGLTexture nextEv(
                RenderRawGradientBlend(
                    accumulatedEv ? accumulatedEv.Get() : gradientInput,
                    processed.Get(), adjustment.mask, gradientInput,
                    accumulatedEv ? 2 : 1));
            if (!nextEv) { gradientFailure = true; break; }
            accumulatedEv.Reset(nextEv.Release());
            lastMask = adjustment.mask;
            appliedGradient = true;
        }
        if (appliedGradient && !gradientFailure) {
            Stack::Renderer::ScopedGLTexture gained(
                RenderRawGradientBlend(gradientInput, accumulatedEv.Get(),
                    lastMask, 0, 3));
            if (gained) {
                if (result.owned) glDeleteTextures(1, &result.texture);
                result.texture = gained.Release();
                result.owned = true;
            } else {
                gradientFailure = true;
            }
        }
        if (localRangeApplied || (appliedGradient && !gradientFailure)) {
            localRangeApplied = true;
            if (!gradientFailure)
                cacheResultStage(postLocalRangeCacheKey, postLocalRangeFingerprint);
        }
    }
    if (zoneAreaReference && m_RawDevelopmentGraphScopeStage == RawDevelopmentGraphScopeStage::LocalRangeInput) {
        if (zoneAreaStatistics.empty()) {
            Stack::Renderer::ScopedGLTexture measured(m_RawZoneAreaRenderer.Render(
                zoneAreaReference.Get(), zoneAreaReference.Get(), m_Width, m_Height,
                recipe, true, m_PreviewMaxDimension <= 0, zoneAreaStatistics, cancellationRequested, calibratedNeutralFingerprint));
            if (!measured) {
                if (cancellationRequested()) return result;
                throw std::runtime_error("Zones area measurement failed");
            }
        }
        m_RawDevelopmentGraphScopeReadback.zoneAreas = std::move(zoneAreaStatistics);
        m_RawDevelopmentGraphScopeReadback.zoneReferenceExposureEv = recipe.preToneExposureEv;
        m_RawDevelopmentGraphScopeReadback.zoneGuide = m_RawZoneAreaRenderer.PreviewGuide();
    }
    if (finishCachePrewarmStage(
            RawCacheStage::PostLocalRange,
            postLocalRangeCacheKey,
            postLocalRangeFingerprint)) {
        return result;
    }
    if (cancellationRequested()) {
        return result;
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

    ApplyRawViewportRegion(result, recipe, Raw::ViewportStage::PostFinishTone, postLocalRangeFingerprint, node.nodeId);
    m_RawViewportGpuTiming.Mark(Raw::ViewportStage::PostFinishTone);
    auto renderRecipeLayer = [&](const nlohmann::json& layerJson, const char* fallbackType,
        const Stack::RawRecipe::RawGradientMask* gradientMask = nullptr,
        const std::string& cacheSuffix = std::string{}) {
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
            std::to_string(node.nodeId) + ":__rawDevelopmentRecipeLayer:" + fallbackType + cacheSuffix;
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
            // Upstream edits change pixels, not this module's settings.
            // Re-deserializing a retained tone layer invalidates its LUT.
            if (cacheIt->second.appliedPayload != layerPayload) {
                cacheIt->second.appliedPayload = nullptr;
                layer->Deserialize(layerPayload);
                cacheIt->second.appliedPayload = layerPayload;
            }
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
            // Auto analysis authored a different internal state. The next
            // recipe must still be applied, even if its input JSON repeats.
            m_RawDevelopmentRecipeLayerCache.at(layerCacheKey).appliedPayload = nullptr;
        }

        if (detailedDamageValidation) {
            const QuickTextureStats inputStats = ProbeTextureStats(inputTexture, m_Width, m_Height);
            const QuickTextureStats outputStats = ProbeTextureStats(processed.Get(), m_Width, m_Height);
            if (!LayerPayloadExplicitlyRequestsCollapsedOutput(layerPayload) &&
                IsImplausiblyDamagedTextureOutput(
                    inputStats,
                    outputStats)) {
                std::cerr << "[RenderPipeline] RAW Development " << type
                          << " produced an invalid or collapsed output for node "
                          << node.nodeId << " (input p99 luma " << inputStats.p99Luma
                          << ", output p99 luma " << outputStats.p99Luma
                          << "); passing input texture through.\n";
                return false;
            }
        }

        if (gradientMask) {
            Stack::Renderer::ScopedGLTexture blended(
                RenderRawGradientBlend(inputTexture, processed.Get(), *gradientMask));
            if (!blended) return false;
            processed.Reset(blended.Release());
        }
        if (result.owned && inputTexture != 0) {
            glDeleteTextures(1, &inputTexture);
        }
        result.texture = processed.Release();
        result.owned = true;
        return true;
    };

    if (cancellationRequested()) {
        return result;
    }
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
    nlohmann::json finishTonePayload = recipe.finishTone.layerJson.is_object()
        ? recipe.finishTone.layerJson
        : Stack::RawRecipe::DefaultFinishToneJson();
    finishTonePayload["inputWorkingSpace"] =
        Stack::RawRecipe::WorkingSpaceStableString(
            recipe.technical.workingSpace);
    finishTonePayload["truthfulV2SignedMath"] = true;
    const CachedGraphTexture cachedPostFinishTone =
        finishToneHasPendingAutoCalibration
            ? CachedGraphTexture{}
            : FindRawDevelopStageCacheEntry(
                  postFinishToneCacheKey,
                  postFinishToneFingerprint);
    if (!adoptCachedStage(cachedPostFinishTone)) {
        ++m_LastGraphExecutionStats.rawStageCacheMisses;
        if (renderRecipeLayer(finishTonePayload, "ToneCurve")) {
            bool toneGradientsComplete = true;
            for (std::size_t gradientIndex = 0;
                 gradientIndex < recipe.toneGradients.size(); ++gradientIndex) {
                const auto& adjustment = recipe.toneGradients[gradientIndex];
                if (!adjustment.mask.enabled) continue;
                nlohmann::json localCurve = adjustment.curveJson.is_object()
                    ? adjustment.curveJson : Stack::RawRecipe::DefaultFinishToneJson();
                localCurve["inputWorkingSpace"] =
                    Stack::RawRecipe::WorkingSpaceStableString(recipe.technical.workingSpace);
                localCurve["truthfulV2SignedMath"] = true;
                if (!renderRecipeLayer(localCurve, "ToneCurve", &adjustment.mask,
                    ":gradient:" + std::to_string(gradientIndex))) {
                    toneGradientsComplete = false;
                    break;
                }
            }
            if (!finishToneHasPendingAutoCalibration && toneGradientsComplete) {
                cacheResultStage(
                    postFinishToneCacheKey,
                    postFinishToneFingerprint);
            }
        }
    }
    if (finishCachePrewarmStage(
            RawCacheStage::PostFinishTone,
            postFinishToneCacheKey,
            postFinishToneFingerprint)) {
        return result;
    }
    if (cancellationRequested()) {
        return result;
    }
    if (captureAnalysis && result.texture != 0) {
        CaptureRawDevelopmentGraphScopeReadback(
            RawDevelopmentGraphScopeStage::ColorWarpInput,
            result.texture,
            m_Width,
            m_Height,
            "scene-linear-pre-color-warp-rgb",
            true);
    }
    ApplyRawViewportRegion(result, recipe, Raw::ViewportStage::PostColorWarp, postFinishToneFingerprint, node.nodeId);
    m_RawViewportGpuTiming.Mark(Raw::ViewportStage::PostColorWarp);
    if (Stack::RawRecipe::IsColorWarpEnabled(recipe.colorWarp) ||
        Stack::RawRecipe::IsDetailContrastActive(recipe.detailContrast)) {
        const CachedGraphTexture cachedPostColorWarp =
            FindRawDevelopStageCacheEntry(
                postColorWarpCacheKey,
                postColorWarpFingerprint);
        if (!adoptCachedStage(cachedPostColorWarp)) {
            ++m_LastGraphExecutionStats.rawStageCacheMisses;
            bool complete = !Stack::RawRecipe::IsColorWarpEnabled(recipe.colorWarp) ||
                RenderRawDevelopmentColorWarpStage(result, recipe);
            if (complete && Stack::RawRecipe::IsDetailContrastActive(recipe.detailContrast)) {
                int sourceWidth = Raw::DisplayWidth(rawData->metadata);
                int sourceHeight = Raw::DisplayHeight(rawData->metadata);
                if (std::abs(recipe.cropRotation.rotationDegrees) % 180 == 90) std::swap(sourceWidth, sourceHeight);
                Stack::Renderer::ScopedGLTexture detailed(RenderRawSceneDetail(result.texture,
                    recipe.detailContrast,recipe.technical.workingSpace,sourceWidth,sourceHeight));
                complete = bool(detailed);
                if (complete) {
                    if (result.owned) glDeleteTextures(1,&result.texture);
                    result.texture=detailed.Release(); result.owned=true;
                } else {
                    recordFailure("Detail Contrast rendering failed.");
                    return {};
                }
            }
            if (complete) {
                cacheResultStage(
                    postColorWarpCacheKey,
                    postColorWarpFingerprint);
            }
        }
    }
    if (finishCachePrewarmStage(
            RawCacheStage::PostColorWarp,
            postColorWarpCacheKey,
            postColorWarpFingerprint)) {
        return result;
    }
    if (cancellationRequested()) {
        return result;
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
            ReadTextureStats(result.texture, m_Width, m_Height, "RawDevelopmentViewTransformInputStats",
                recipe.technical.workingSpace == Raw::RawWorkingSpace::LinearRec2020D65);
        m_RawDevelopmentStageStatsReadbacks.push_back(
            MakeStageStatsReadback(
                Stack::RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate,
                m_RawDevelopmentViewTransformInputStats,
                "Post-Finish-Tone and post-Color-Warp texture immediately before View Transform.",
                "scene-linear-pre-display-rgb",
                true,
                false));
    }
    ApplyRawViewportRegion(result, recipe, Raw::ViewportStage::PostViewTransform, postColorWarpFingerprint, node.nodeId);
    m_RawViewportGpuTiming.Mark(Raw::ViewportStage::PostViewTransform);
    const bool viewTransformEnabled =
        Stack::RawRecipe::IsViewTransformEnabled(recipe);
    bool viewTransformRendered = false;
    bool encodedSrgbOutput = false;
    if (cancellationRequested()) {
        return result;
    }
    if (viewTransformEnabled) {
        const CachedGraphTexture cachedPostViewTransform =
            FindRawDevelopStageCacheEntry(
                postViewTransformCacheKey,
                postViewTransformFingerprint);
        if (adoptCachedStage(cachedPostViewTransform)) {
            viewTransformRendered = true;
        } else {
            ++m_LastGraphExecutionStats.rawStageCacheMisses;
            viewTransformRendered =
                renderRecipeLayer(
                    recipe.viewTransform.layerJson,
                    "ViewTransform");
            if (viewTransformRendered) {
                cacheResultStage(
                    postViewTransformCacheKey,
                    postViewTransformFingerprint);
            }
        }
        if (viewTransformRendered) {
            encodedSrgbOutput = recipe.viewTransform.layerJson.value(
                "encodeSrgbOutput",
                recipe.technical.encodeSrgbOutput);
        }
    }
    if (finishCachePrewarmStage(
            RawCacheStage::PostViewTransform,
            postViewTransformCacheKey,
            postViewTransformFingerprint)) {
        return result;
    }

    // Crop V2 is output geometry, not another color operation. Rotation and
    // flips have already established the oriented image in the RAW placement
    // stage; the normalized crop rectangle therefore applies after View.
    m_RawViewportGpuTiming.Mark(Raw::ViewportStage::PostOutputCrop);
    if (!m_RawViewportAppliedRegion.Valid()) RenderRawDevelopmentOutputCropStage(
        result,
        recipe.cropRotation,
        postOutputCropCacheKey,
        postOutputCropFingerprint);
    if (finishCachePrewarmStage(
            RawCacheStage::PostOutputCrop,
            postOutputCropCacheKey,
            postOutputCropFingerprint)) {
        return result;
    }
    if (cancellationRequested()) {
        return result;
    }
    const QuickTextureStats finalValidationStats =
        m_RawDevelopmentViewportValidationEnabled
            ? ProbeTextureStats(result.texture, m_Width, m_Height)
            : QuickTextureStats{};
    if (m_RawDevelopmentViewportValidationEnabled &&
        !LayerPayloadExplicitlyRequestsCollapsedOutput(
            recipe.finishTone.layerJson) &&
        IsImplausiblyDamagedTextureOutput(
            authoredPlacementValidationStats,
            finalValidationStats)) {
        std::cerr << "[RenderPipeline] RAW Development rejected an invalid or implausibly collapsed final output for node "
                  << node.nodeId << " (placement p99 luma "
                  << authoredPlacementValidationStats.p99Luma
                  << ", final p99 luma "
                  << finalValidationStats.p99Luma
                  << "); quarantining this recipe's stage entries.\n";
        InvalidateRawDevelopStageCacheEntry(
            rawPlacementCacheKey,
            rawPlacementFingerprint);
        InvalidateRawDevelopStageCacheEntry(
            postLocalRangeCacheKey,
            postLocalRangeFingerprint);
        InvalidateRawDevelopStageCacheEntry(
            postFinishToneCacheKey,
            postFinishToneFingerprint);
        InvalidateRawDevelopStageCacheEntry(
            postColorWarpCacheKey,
            postColorWarpFingerprint);
        InvalidateRawDevelopStageCacheEntry(
            postViewTransformCacheKey,
            postViewTransformFingerprint);
        InvalidateRawDevelopStageCacheEntry(
            postOutputCropCacheKey,
            postOutputCropFingerprint);
        if (result.owned && result.texture != 0) {
            glDeleteTextures(1, &result.texture);
        }
        result = {};
        return result;
    }
    const bool displayMapped = viewTransformEnabled && viewTransformRendered;
    const char* displayMeasurementDomain = displayMapped
        ? (encodedSrgbOutput
            ? "display-mapped-srgb-encoded"
            : "display-mapped-linear-srgb")
        : (recipe.technical.workingSpace == Raw::RawWorkingSpace::LinearRec2020D65
            ? "scene-linear-result-rec2020-d65"
            : "scene-linear-result-srgb-d65");
    CaptureRawDevelopmentGradingScopeReadback(
        RawDevelopmentGradingScopeSource::DisplayCandidate,
        result.texture,
        m_Width,
        m_Height,
        displayMeasurementDomain,
        displayMapped
            ? Raw::RawWorkingSpace::LinearSrgbD65
            : recipe.technical.workingSpace,
        !displayMapped || !encodedSrgbOutput,
        displayMapped && encodedSrgbOutput);
    if (viewTransformEnabled && captureAnalysis && result.texture != 0) {
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
