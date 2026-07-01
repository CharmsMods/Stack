#include "Renderer/RenderPipeline.h"

#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawLoader.h"
#include "Editor/LayerRegistry.h"
#include "Editor/Layers/ToneLayers.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/RawPreviewProxy.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

using namespace Stack::Renderer::GraphExecution;

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
    const bool localExposureEnabled = Stack::RawRecipe::IsLocalExposureEnabled(recipe);
    if (!localExposureEnabled) {
        m_PreLocalExposureSummaries.erase(node.nodeId);
    }
    Raw::RawDevelopSettings rawRenderSettings = settings;
    rawRenderSettings.toneCurvePoints.clear();
    const Raw::RawImageData& renderRawData =
        ResolveRawPreviewRenderData(node.nodeId, rawData, cacheKeyPath);
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

    if (result.texture != 0) {
        m_RawDevelopmentLocalSuggestionImage =
            ReadLocalSuggestionAnalysisImage(
                result.texture,
                m_Width,
                m_Height,
                512,
                "RawDevelopmentLocalSuggestionImage");
        CaptureRawDevelopmentLocalRangeTargetSample(result.texture, recipe.localRange);
    }

    const bool localRangeActive = Stack::RawRecipe::IsLocalRangeEnabled(recipe);
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
                }
            }
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
        m_RawDevelopmentViewTransformInputStats =
            ReadTextureStats(result.texture, m_Width, m_Height, "RawDevelopmentViewTransformInputStats");
    }
    renderRecipeLayer(recipe.viewTransform.layerJson, "ViewTransform");
    (void)fingerprint;
    return result;
}
