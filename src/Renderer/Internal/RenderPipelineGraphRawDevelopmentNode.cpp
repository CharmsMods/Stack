#include "Renderer/RenderPipeline.h"

#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawLoader.h"
#include "Editor/LayerRegistry.h"
#include "Editor/Layers/ToneLayers.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/RawPreviewProxy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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

    if (metadata.pixelLayout == Raw::RawPixelLayout::MosaicBayer &&
        metadata.rawWidth > 0 &&
        metadata.rawHeight > 0 &&
        rawData.rawBuffer.size() >=
            static_cast<std::size_t>(metadata.rawWidth) * static_cast<std::size_t>(metadata.rawHeight)) {
        int left = std::max(0, metadata.leftMargin);
        int top = std::max(0, metadata.topMargin);
        int right = std::min(metadata.rawWidth, left + std::max(1, metadata.visibleWidth));
        int bottom = std::min(metadata.rawHeight, top + std::max(1, metadata.visibleHeight));
        if (right <= left || bottom <= top) {
            left = 0;
            top = 0;
            right = metadata.rawWidth;
            bottom = metadata.rawHeight;
        }
        const int width = std::max(1, right - left);
        const int height = std::max(1, bottom - top);
        const int stepX = std::max(1, (width + 511) / 512);
        const int stepY = std::max(1, (height + 511) / 512);
        for (int y = top; y < bottom; y += stepY) {
            for (int x = left; x < right; x += stepX) {
                const int plane = RawPlaneAt(metadata.cfaPattern, x - left, y - top);
                if (plane < 0) {
                    continue;
                }
                const int channel = RawSafetyChannelFromPlane(plane);
                const float black = RawSafetyBlackForPlane(metadata, plane);
                const float value = static_cast<float>(
                    rawData.rawBuffer[static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(metadata.rawWidth) +
                        static_cast<std::size_t>(x)]);
                const bool clipped = recordChannel(
                    channel,
                    NormalizeRawSafetyValue(value, black, metadataWhite));
                ++sampledPixels;
                if (clipped) {
                    ++singleClipPixels;
                }
            }
        }
        const float expectedActive = static_cast<float>(std::max(1, width * height));
        stats.activeValidFraction = std::clamp(
            static_cast<float>(sampledPixels * stepX * stepY) / expectedActive,
            0.0f,
            1.0f);
        stats.statusMessage =
            "Partial raw safety ledger from active Bayer samples and metadata levels. "
            "Masked optical-black drift, true linear response limits, and noise profiles are not captured yet; multi-channel clip fractions are unavailable for single-photosite mosaic samples.";
    } else {
        const int width = std::max(1, metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth);
        const int height = std::max(1, metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight);
        const int channels = std::clamp(metadata.linearChannels > 0 ? metadata.linearChannels : 3, 3, 4);
        const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        const bool useUInt16 =
            rawData.linearUInt16Buffer.size() >= pixelCount * static_cast<std::size_t>(channels);
        const bool useFloat =
            rawData.linearFloatBuffer.size() >= pixelCount * static_cast<std::size_t>(channels);
        const int stepX = std::max(1, (width + 511) / 512);
        const int stepY = std::max(1, (height + 511) / 512);
        if (useUInt16 || useFloat) {
            const bool floatLooksUnitScale =
                useFloat && metadata.rawMaximum > 0.0f && metadata.rawMaximum <= 4.0f;
            for (int y = 0; y < height; y += stepY) {
                for (int x = 0; x < width; x += stepX) {
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
            const float expectedActive = static_cast<float>(std::max(1, width * height));
            stats.activeValidFraction = std::clamp(
                static_cast<float>(sampledPixels * stepX * stepY) / expectedActive,
                0.0f,
                1.0f);
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

    const Stack::RawRecipe::RawDevelopmentRecipe& recipe = node.rawDevelopment.recipe;
    const std::string& sourcePath = recipe.source.sourcePath;
    if (sourcePath.empty()) {
        return result;
    }

    Raw::RawImageData& rawData = m_RawDataCache[node.nodeId];
    std::string& cachedPath = m_RawDataCachePaths[node.nodeId];
    const std::string cacheKeyPath =
        sourcePath + "#" + recipe.source.fingerprint + "#" + std::to_string(recipe.source.fileSizeBytes);
    if (cachedPath != cacheKeyPath ||
        (rawData.rawBuffer.empty() && rawData.linearUInt16Buffer.empty() && rawData.linearFloatBuffer.empty())) {
        Raw::RawImageData loadedRaw;
        if (Raw::RawLoader::LoadFile(sourcePath, loadedRaw)) {
            rawData = std::move(loadedRaw);
            cachedPath = cacheKeyPath;
        } else {
            rawData = std::move(loadedRaw);
            cachedPath = cacheKeyPath;
        }
    }

    const bool rawDataHasPixels = Stack::Renderer::RawPreviewProxy::HasPixels(rawData);
    if (!rawDataHasPixels || !rawData.metadata.error.empty()) {
        const std::string error = !rawData.metadata.error.empty()
            ? rawData.metadata.error
            : "LibRaw did not produce a usable raw buffer.";
        std::cerr << "[RAW] Load failed for RAW Development node " << node.nodeId
                  << " (" << sourcePath << "): " << error << "\n";
        return result;
    }

    const Raw::RawDevelopSettings settings = Stack::RawRecipe::ToRawDevelopSettings(recipe);
    const Stack::RawAutoStartPoint::RawAutoStartPointRawSafetyStats rawSafetyStats =
        BuildRawSafetyStats(rawData, settings);
    const bool localExposureEnabled = Stack::RawRecipe::IsLocalExposureEnabled(recipe);
    if (!localExposureEnabled) {
        m_PreLocalExposureSummaries.erase(node.nodeId);
    }
    Raw::RawDevelopSettings rawRenderSettings = settings;
    rawRenderSettings.toneCurvePoints.clear();
    const Raw::RawImageData& renderRawData =
        ResolveRawPreviewRenderData(node.nodeId, rawData, cacheKeyPath);
    Raw::RawDevelopSettings neutralSceneSettings = rawRenderSettings;
    neutralSceneSettings.exposureStops = 0.0f;
    neutralSceneSettings.toneCurvePoints.clear();
    const bool currentRawRenderIsNeutralScene =
        std::fabs(rawRenderSettings.exposureStops) <= 0.0001f &&
        rawRenderSettings.toneCurvePoints.empty();
    RenderTextureStats neutralSceneStats;
    if (!currentRawRenderIsNeutralScene) {
        if (const unsigned int neutralTexture =
                m_RawPipelines[node.nodeId].Render(renderRawData, neutralSceneSettings, m_PreviewMaxDimension)) {
            const int neutralWidth = m_RawPipelines[node.nodeId].GetOutputWidth();
            const int neutralHeight = m_RawPipelines[node.nodeId].GetOutputHeight();
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
    result.texture = m_RawPipelines[node.nodeId].Render(renderRawData, rawRenderSettings, m_PreviewMaxDimension);
    result.owned = false;
    if (result.texture == 0) {
        const std::string& error = m_RawPipelines[node.nodeId].GetLastError();
        std::cerr << "[RAW] Render failed for RAW Development node " << node.nodeId
                  << " (" << sourcePath << "): "
                  << (error.empty() ? "unknown RAW GPU failure" : error)
                  << "\n";
        return result;
    }

    m_Width = m_RawPipelines[node.nodeId].GetOutputWidth();
    m_Height = m_RawPipelines[node.nodeId].GetOutputHeight();
    const RenderTextureStats rawPlacementStats =
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

        m_PreLocalExposureSummaries[node.nodeId] = BuildPreLocalExposureSummary(
            result.texture,
            localSettings,
            false,
            !localSettings.autoSafetyEnabled);

        RenderGraphNode localMapNode;
        localMapNode.nodeId = node.nodeId;
        localMapNode.kind = RenderGraphNodeKind::RawDetailFusion;
        localMapNode.rawDetailFusion.settings = localSettings;

        const unsigned int preLocalTexture = result.texture;
        unsigned int exposureMap = RenderRawDetailAutoMask(preLocalTexture, localMapNode, 0, false);
        if (unsigned int localResult = exposureMap != 0
            ? RenderRawDetailFusion(preLocalTexture, exposureMap, localSettings)
            : 0) {
            const QuickTextureStats inputStats = ProbeTextureStats(preLocalTexture, m_Width, m_Height);
            const QuickTextureStats outputStats = ProbeTextureStats(localResult, m_Width, m_Height);
            const bool inputHasSignal = inputStats.valid && inputStats.p99Luma > 0.00001f;
            const bool outputIsBlank =
                outputStats.valid &&
                outputStats.p99Luma <= 0.000001f &&
                outputStats.maxRgb <= 0.00001f;
            if (inputHasSignal && outputIsBlank) {
                glDeleteTextures(1, &localResult);
                std::cerr << "[RenderPipeline] RAW Workspace local exposure produced a blank output for RAW Development node "
                          << node.nodeId << " (input p99 luma " << inputStats.p99Luma
                          << ", output p99 luma " << outputStats.p99Luma
                          << "); passing pre-local texture through.\n";
            } else {
                result.texture = localResult;
                result.owned = true;
            }
        }
        if (exposureMap != 0) {
            glDeleteTextures(1, &exposureMap);
        }
    }

    const bool localRangeActive = Stack::RawRecipe::IsLocalRangeEnabled(recipe);
    RenderTextureStats preLocalRangeStats;
    bool hasPreLocalRangeStats = false;
    bool localRangeApplied = false;

    if (result.texture != 0) {
        m_RawDevelopmentLocalSuggestionImage =
            ReadLocalSuggestionAnalysisImage(
                result.texture,
                m_Width,
                m_Height,
                512,
                "RawDevelopmentLocalSuggestionImage");
        CaptureRawDevelopmentLocalRangeTargetSample(result.texture, recipe.localRange);
        preLocalRangeStats =
            ReadTextureStats(result.texture, m_Width, m_Height, "RawDevelopmentPreLocalRangeStats");
        hasPreLocalRangeStats = true;
        m_RawDevelopmentStageStatsReadbacks.push_back(MakeRawSafetyStageReadback(rawSafetyStats));
        m_RawDevelopmentStageStatsReadbacks.push_back(
            MakeStageStatsReadback(
                Stack::RawAutoStartPoint::RawAutoStartPointStage::NeutralScene,
                neutralSceneStats,
                "Neutral scene analysis render using the current WB policy at 0 EV RAW Exposure before Local Exposure, Local Range, Finish Tone, and View Transform.",
                "scene-linear-neutral-rgb",
                true,
                false));
        m_RawDevelopmentStageStatsReadbacks.push_back(
            MakeStageStatsReadback(
                Stack::RawAutoStartPoint::RawAutoStartPointStage::RawPlacement,
                rawPlacementStats,
                "Current RAW Exposure/WB texture immediately after RAW GPU render and before legacy Local Exposure, Local Range, Finish Tone, and View Transform.",
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

    const bool regionMaskOverlayActive =
        m_RawDevelopmentLocalRangeOverlayRequestMode == "region-mask" &&
        (Stack::RawRecipe::SanitizeLocalRangeRecipe(recipe.localRange).regionMaskEnabled ||
            Stack::RawRecipe::SanitizeLocalRangeRecipe(recipe.localRange).colorMaskEnabled);
    if ((localRangeActive || regionMaskOverlayActive) && result.texture != 0) {
        const unsigned int preLocalRangeTexture = result.texture;
        if (!m_RawDevelopmentLocalRangeOverlayRequestMode.empty() &&
            m_RawDevelopmentLocalRangeOverlayRequestMode != "none") {
            if (const unsigned int overlayTexture = RenderRawDevelopmentLocalRangeOverlay(
                    preLocalRangeTexture,
                    recipe.localRange,
                    m_RawDevelopmentLocalRangeOverlayRequestMode)) {
                ClearRawDevelopmentLocalRangeOverlay();
                m_RawDevelopmentLocalRangeOverlayTexture = overlayTexture;
                m_RawDevelopmentLocalRangeOverlayWidth = m_Width;
                m_RawDevelopmentLocalRangeOverlayHeight = m_Height;
                m_RawDevelopmentLocalRangeOverlayMode = m_RawDevelopmentLocalRangeOverlayRequestMode;
            }
        }
        if (localRangeActive) {
            if (const unsigned int localRangeResult =
                    RenderRawDevelopmentLocalRange(preLocalRangeTexture, recipe.localRange)) {
                const QuickTextureStats inputStats = ProbeTextureStats(preLocalRangeTexture, m_Width, m_Height);
                const QuickTextureStats outputStats = ProbeTextureStats(localRangeResult, m_Width, m_Height);
                const bool inputHasSignal = inputStats.valid && inputStats.p99Luma > 0.00001f;
                const bool outputIsBlank =
                    outputStats.valid &&
                    outputStats.p99Luma <= 0.000001f &&
                    outputStats.maxRgb <= 0.00001f;
                if (inputHasSignal && outputIsBlank) {
                    glDeleteTextures(1, &localRangeResult);
                    std::cerr << "[RenderPipeline] RAW Development local range produced a blank output for node "
                              << node.nodeId << " (input p99 luma " << inputStats.p99Luma
                              << ", output p99 luma " << outputStats.p99Luma
                              << "); passing pre-local-range texture through.\n";
                } else {
                    if (result.owned && preLocalRangeTexture != 0) {
                        glDeleteTextures(1, &preLocalRangeTexture);
                    }
                    result.texture = localRangeResult;
                    result.owned = true;
                    localRangeApplied = true;
                }
            }
        }
    }
    if (localRangeActive && result.texture != 0) {
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
            return;
        }
        nlohmann::json layerPayload = layerJson.is_object() ? layerJson : nlohmann::json::object();
        const std::string type = layerPayload.value("type", std::string(fallbackType));
        layerPayload["type"] = type;
        std::shared_ptr<LayerBase> layer = LayerRegistry::CreateLayerFromTypeId(type);
        if (!layer) {
            return;
        }

        layer->InitializeGL();
        layer->Deserialize(layerPayload);
        if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(layer.get())) {
            toneCurve->SetAutoRewriteRenderContext(node.nodeId, node.requestRevision);
        }

        const unsigned int inputTexture = result.texture;
        unsigned int processed = CreateGraphRenderTargetTexture();
        const unsigned int sourceTexture = m_SourceTexture != 0 ? m_SourceTexture : inputTexture;
        const bool renderedLayer = RenderIntoGraphTargetTexture(processed, [&](unsigned int) {
            layer->ExecuteWithSource(inputTexture, sourceTexture, m_Width, m_Height, m_Quad);
        });
        if (!renderedLayer || processed == 0) {
            if (processed != 0) {
                glDeleteTextures(1, &processed);
            }
            std::cerr << "[RenderPipeline] RAW Development " << type
                      << " finish pass failed for node " << node.nodeId
                      << "; passing input texture through.\n";
            return;
        }

        if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(layer.get());
            toneCurve && toneCurve->HasPendingAutoRewriteFeedback()) {
            m_ToneCurveAutoRewriteFeedback.push_back(toneCurve->TakePendingAutoRewriteFeedback());
        }

        if (type == "ToneCurve" && IsDefaultToneCurvePayload(layerPayload)) {
            const QuickTextureStats inputStats = ProbeTextureStats(inputTexture, m_Width, m_Height);
            const QuickTextureStats outputStats = ProbeTextureStats(processed, m_Width, m_Height);
            const bool inputHasSignal = inputStats.valid && inputStats.p99Luma > 0.00001f;
            const bool outputIsBlank =
                outputStats.valid &&
                outputStats.p99Luma <= 0.000001f &&
                outputStats.maxRgb <= 0.00001f;
            if (inputHasSignal && outputIsBlank) {
                glDeleteTextures(1, &processed);
                std::cerr << "[RenderPipeline] RAW Development default Tone Curve produced a blank output for node "
                          << node.nodeId << " (input p99 luma " << inputStats.p99Luma
                          << ", output p99 luma " << outputStats.p99Luma
                          << "); passing input texture through.\n";
                return;
            }
        }

        if (result.owned && inputTexture != 0) {
            glDeleteTextures(1, &inputTexture);
        }
        result.texture = processed;
        result.owned = true;
    };

    renderRecipeLayer(recipe.finishTone.layerJson, "ToneCurve");
    if (result.texture != 0) {
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
    renderRecipeLayer(recipe.viewTransform.layerJson, "ViewTransform");
    if (result.texture != 0) {
        CaptureRawDevelopmentStageImageReadback(
            Stack::RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate,
            Stack::RawAutoStartPoint::RawAutoStartPointStageStatus::Complete,
            result.texture,
            m_Width,
            m_Height,
            "display-mapped-linear-srgb",
            false,
            true);
        m_RawDevelopmentFinalDisplayStats =
            ReadTextureStats(result.texture, m_Width, m_Height, "RawDevelopmentFinalDisplayStats");
        m_RawDevelopmentStageStatsReadbacks.push_back(
            MakeStageStatsReadback(
                Stack::RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate,
                m_RawDevelopmentFinalDisplayStats,
                "Post-View-Transform texture used for the RAW workspace preview.",
                "display-mapped-linear-rgb",
                false,
                true));
    }
    (void)fingerprint;
    return result;
}
