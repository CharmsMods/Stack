#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Editor/LayerRegistry.h"
#include "Editor/Layers/ToneLayers.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Renderer/ScopedGLObjects.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>

using namespace Stack::Renderer::GraphExecution;

RenderPipeline::GraphNodeRenderResult RenderPipeline::RenderRawDevelopGraphNode(
    GraphExecutionContext& executionContext,
    const RenderGraphNode& node,
    const std::string& socketId,
    std::size_t fingerprint,
    std::unordered_map<std::string, unsigned int>& imageCache,
    const std::function<unsigned int(int, const std::string&)>& evalImage,
    const std::function<unsigned int(int, const std::string&)>& evalMask,
    const std::function<std::size_t(int, const std::string&)>& fingerprintImage) {
    GraphNodeRenderResult result;
    Stack::Renderer::ScopedOwnedGLTextureExceptionCleanup
        resultExceptionCleanup(result.texture, result.owned);

    const std::string preFinishKey =
        std::to_string(node.nodeId) + ":" + EditorNodeGraph::kPreFinishImageOutputSocketId;
    const std::string rawBaseKey = std::to_string(node.nodeId) + ":__rawDevelopBase";
    const std::size_t rawBaseFingerprint = fingerprintImage(node.nodeId, "__rawDevelopBase");
    const bool wantsPreFinishSocket = socketId == EditorNodeGraph::kPreFinishImageOutputSocketId;
    const bool wantsIntegratedFinal =
        !wantsPreFinishSocket &&
        node.rawDevelop.integratedToneEnabled &&
        node.rawDevelop.settings.debugView == Raw::RawDebugView::FinalOutput &&
        node.rawDevelop.integratedToneLayerJson.is_object();

    if (wantsPreFinishSocket) {
        const CachedGraphTexture cachedPreFinish = FindRawDevelopStageCacheEntry(preFinishKey, fingerprint);
        if (cachedPreFinish.texture != 0 &&
            cachedPreFinish.width > 0 &&
            cachedPreFinish.height > 0) {
            ++m_LastGraphExecutionStats.rawStageCacheHits;
            m_Width = cachedPreFinish.width;
            m_Height = cachedPreFinish.height;
            m_LastGraphImageCacheHits.insert(preFinishKey);
            result.texture = cachedPreFinish.texture;
            result.owned = false;
            return result;
        }
    }

    const SharedRawBaseStageResult rawBaseStage =
        RenderSharedRawBaseStage(executionContext, node, node.rawDevelop.settings, rawBaseKey, rawBaseFingerprint, fingerprintImage);
    const unsigned int rawDevelopBase = rawBaseStage.texture;
    if (rawDevelopBase == 0) {
        return result;
    }

    if (wantsIntegratedFinal) {
        const unsigned int preToneTexture = evalImage(node.nodeId, EditorNodeGraph::kPreFinishImageOutputSocketId);
        if (preToneTexture == 0) {
            return result;
        }
        const int preToneWidth = m_Width;
        const int preToneHeight = m_Height;

        std::shared_ptr<LayerBase> integratedToneLayer = LayerRegistry::CreateLayerFromTypeId("ToneCurve");
        if (!integratedToneLayer) {
            result.texture = preToneTexture;
            result.owned = false;
            return result;
        }

        integratedToneLayer->InitializeGL();
        integratedToneLayer->Deserialize(node.rawDevelop.integratedToneLayerJson);
        if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(integratedToneLayer.get())) {
            toneCurve->SetAutoRewriteRenderContext(node.nodeId, node.requestRevision);
            toneCurve->SetDevelopScenePrepToneBudget(
                node.rawDevelop.scenePrepEnabled,
                node.rawDevelop.scenePrepSettings.strength,
                node.rawDevelop.scenePrepSettings.maxEvBias);
        }

        Stack::Renderer::ScopedGLTexture finishedResult(
            CreateGraphRenderTargetTexture());
        const bool renderedIntegratedTone = RenderIntoGraphTargetTexture(finishedResult.Get(), [&](unsigned int) {
            integratedToneLayer->ExecuteWithSource(preToneTexture, preToneTexture, m_Width, m_Height, m_Quad);
        });
        bool useFinishedResult = renderedIntegratedTone && finishedResult;
        if (useFinishedResult) {
            const QuickTextureStats inputStats = ProbeTextureStats(preToneTexture, m_Width, m_Height);
            const QuickTextureStats outputStats = ProbeTextureStats(finishedResult.Get(), m_Width, m_Height);
            const bool inputHasSignal = inputStats.valid && inputStats.p99Luma > 0.00001f;
            const bool outputIsBlank =
                outputStats.valid &&
                outputStats.p99Luma <= 0.000001f &&
                outputStats.maxRgb <= 0.00001f;
            if (inputHasSignal && outputIsBlank) {
                finishedResult.Reset();
                useFinishedResult = false;
                std::cerr << "[RenderPipeline] Integrated Develop ToneCurve produced a blank output for RawDevelop node "
                          << node.nodeId << " (input p99 luma " << inputStats.p99Luma
                          << ", output p99 luma " << outputStats.p99Luma
                          << "); passing pre-finish texture through.\n";
            }
        }

        if (useFinishedResult) {
            result.texture = finishedResult.Release();
            result.owned = true;
        } else {
            result.texture = preToneTexture;
            result.owned = false;
        }

        if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(integratedToneLayer.get());
            toneCurve && toneCurve->HasPendingAutoRewriteFeedback()) {
            m_ToneCurveAutoRewriteFeedback.push_back(toneCurve->TakePendingAutoRewriteFeedback());
        }

        if (useFinishedResult && result.texture != 0) {
            const RenderGraphLink* maskLink = executionContext.FindInputLink(node.nodeId, "maskIn");
            const unsigned int finishMaskTexture = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
            m_Width = preToneWidth;
            m_Height = preToneHeight;
            if (maskLink != nullptr && finishMaskTexture == 0) {
                glDeleteTextures(1, &result.texture);
                result.texture = 0;
                result.owned = false;
                result.texture = preToneTexture;
                std::cerr << "[RenderPipeline] Connected finish mask could not be rendered for RawDevelop node "
                          << node.nodeId << "; passing pre-finish texture through.\n";
            } else if (finishMaskTexture != 0) {
                EnsureMaskPrograms();
                Stack::Renderer::ScopedGLTexture blended(
                    CreateGraphRenderTargetTexture());
                bool blendPassExecuted = false;
                const bool renderedBlend =
                    m_MaskBlendProgram != 0 &&
                    RenderIntoGraphTargetTexture(blended.Get(), [&](unsigned int fbo) {
                        blendPassExecuted = RenderMaskBlend(
                            preToneTexture, result.texture, finishMaskTexture, fbo);
                    }) &&
                    blendPassExecuted;
                if (renderedBlend && blended && result.texture != 0) {
                    glDeleteTextures(1, &result.texture);
                    result.texture = blended.Release();
                    result.owned = true;
                } else {
                    glDeleteTextures(1, &result.texture);
                    result.texture = 0;
                    result.owned = false;
                    result.texture = preToneTexture;
                    std::cerr << "[RenderPipeline] Finish-mask blend failed for RawDevelop node "
                              << node.nodeId << "; passing pre-finish texture through.\n";
                }
            }
        }

        return result;
    }

    result.texture = rawDevelopBase;
    result.owned = false;
    if (result.texture != 0 &&
        node.rawDevelop.scenePrepEnabled &&
        node.rawDevelop.settings.debugView == Raw::RawDebugView::FinalOutput) {
        Raw::RawDetailFusionSettings prepSettings = node.rawDevelop.scenePrepSettings;
        prepSettings.mode = Raw::RawDetailFusionMode::AutoAnalyze;
        prepSettings.debugView = Raw::RawDetailFusionDebugView::FinalImage;
        prepSettings.invertMask = false;
        prepSettings.maskBlackPoint = 0.0f;
        prepSettings.maskWhitePoint = 1.0f;
        prepSettings.maskGamma = 1.0f;
        prepSettings.manualBlend = 0.0f;
        if (prepSettings.autoSafetyEnabled && !prepSettings.overrideBaseEv) {
            // Develop already chose the RAW baseline exposure. Keep Scene Prep from
            // recomputing a second global base that can cancel that authored intent.
            prepSettings.baseEv = std::clamp(prepSettings.baseEvBias, -1.0f, 1.0f);
            prepSettings.overrideBaseEv = true;
        }

        m_PreLocalExposureSummaries[node.nodeId] = BuildPreLocalExposureSummary(
            result.texture,
            prepSettings,
            false,
            !prepSettings.autoSafetyEnabled);

        RenderGraphNode prepMapNode;
        prepMapNode.nodeId = node.nodeId;
        prepMapNode.kind = RenderGraphNodeKind::RawDetailFusion;
        prepMapNode.rawDetailFusion.settings = prepSettings;
        const unsigned int preScenePrepTexture = result.texture;
        Stack::Renderer::ScopedGLTexture prepExposureMap(
            RenderRawDetailAutoMask(
                result.texture,
                prepMapNode,
                0,
                false));
        Stack::Renderer::ScopedGLTexture preparedResult(
            prepExposureMap
                ? RenderRawDetailFusion(
                    result.texture,
                    prepExposureMap.Get(),
                    prepSettings)
                : 0);
        if (preparedResult) {
            const QuickTextureStats inputStats = ProbeTextureStats(preScenePrepTexture, m_Width, m_Height);
            const QuickTextureStats outputStats = ProbeTextureStats(preparedResult.Get(), m_Width, m_Height);
            const bool inputHasSignal = inputStats.valid && inputStats.p99Luma > 0.00001f;
            const bool outputIsBlank =
                outputStats.valid &&
                outputStats.p99Luma <= 0.000001f &&
                outputStats.maxRgb <= 0.00001f;
            if (inputHasSignal && outputIsBlank) {
                std::cerr << "[RenderPipeline] Develop scene prep produced a blank output for RawDevelop node "
                          << node.nodeId << " (input p99 luma " << inputStats.p99Luma
                          << ", output p99 luma " << outputStats.p99Luma
                          << "); passing RAW base texture through.\n";
            } else {
                result.texture = preparedResult.Release();
                result.owned = true;
            }
        }
    }

    imageCache[preFinishKey] = result.texture;
    if (wantsPreFinishSocket) {
        // The hidden pre-finish output intentionally exposes Develop after RAW conversion
        // and scene prep, but before integrated finish tone or finish-mask blending.
        if (result.texture != 0) {
            StoreRawDevelopStageCacheEntry(preFinishKey, result.texture, fingerprint);
        }
    }

    return result;
}
