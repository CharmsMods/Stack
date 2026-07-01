#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Editor/LayerRegistry.h"
#include "Editor/Layers/ToneLayers.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"

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

        unsigned int finishedResult = CreateGraphRenderTargetTexture();
        const bool renderedIntegratedTone = RenderIntoGraphTargetTexture(finishedResult, [&](unsigned int) {
            integratedToneLayer->ExecuteWithSource(preToneTexture, preToneTexture, m_Width, m_Height, m_Quad);
        });
        bool useFinishedResult = renderedIntegratedTone && finishedResult != 0;
        if (useFinishedResult) {
            const QuickTextureStats inputStats = ProbeTextureStats(preToneTexture, m_Width, m_Height);
            const QuickTextureStats outputStats = ProbeTextureStats(finishedResult, m_Width, m_Height);
            const bool inputHasSignal = inputStats.valid && inputStats.p99Luma > 0.00001f;
            const bool outputIsBlank =
                outputStats.valid &&
                outputStats.p99Luma <= 0.000001f &&
                outputStats.maxRgb <= 0.00001f;
            if (inputHasSignal && outputIsBlank) {
                glDeleteTextures(1, &finishedResult);
                finishedResult = 0;
                useFinishedResult = false;
                std::cerr << "[RenderPipeline] Integrated Develop ToneCurve produced a blank output for RawDevelop node "
                          << node.nodeId << " (input p99 luma " << inputStats.p99Luma
                          << ", output p99 luma " << outputStats.p99Luma
                          << "); passing pre-finish texture through.\n";
            }
        }

        if (useFinishedResult) {
            result.texture = finishedResult;
            result.owned = true;
        } else {
            if (finishedResult != 0) {
                glDeleteTextures(1, &finishedResult);
            }
            result.texture = preToneTexture;
            result.owned = false;
        }

        if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(integratedToneLayer.get());
            toneCurve && toneCurve->HasPendingAutoRewriteFeedback()) {
            m_ToneCurveAutoRewriteFeedback.push_back(toneCurve->TakePendingAutoRewriteFeedback());
        }

        if (useFinishedResult && finishedResult != 0) {
            const RenderGraphLink* maskLink = executionContext.FindInputLink(node.nodeId, "maskIn");
            const unsigned int finishMaskTexture = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
            if (finishMaskTexture != 0) {
                unsigned int blended = CreateGraphRenderTargetTexture();
                RenderIntoGraphTargetTexture(blended, [&](unsigned int fbo) {
                    RenderMaskBlend(preToneTexture, finishedResult, finishMaskTexture, fbo);
                });
                if (blended != 0 && finishedResult != 0) {
                    glDeleteTextures(1, &finishedResult);
                }
                result.texture = blended != 0 ? blended : finishedResult;
                result.owned = true;
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
        unsigned int prepExposureMap = RenderRawDetailAutoMask(result.texture, prepMapNode, 0, false);
        if (unsigned int preparedResult = prepExposureMap != 0
            ? RenderRawDetailFusion(result.texture, prepExposureMap, prepSettings)
            : 0) {
            const QuickTextureStats inputStats = ProbeTextureStats(preScenePrepTexture, m_Width, m_Height);
            const QuickTextureStats outputStats = ProbeTextureStats(preparedResult, m_Width, m_Height);
            const bool inputHasSignal = inputStats.valid && inputStats.p99Luma > 0.00001f;
            const bool outputIsBlank =
                outputStats.valid &&
                outputStats.p99Luma <= 0.000001f &&
                outputStats.maxRgb <= 0.00001f;
            if (inputHasSignal && outputIsBlank) {
                glDeleteTextures(1, &preparedResult);
                std::cerr << "[RenderPipeline] Develop scene prep produced a blank output for RawDevelop node "
                          << node.nodeId << " (input p99 luma " << inputStats.p99Luma
                          << ", output p99 luma " << outputStats.p99Luma
                          << "); passing RAW base texture through.\n";
            } else {
                result.texture = preparedResult;
                result.owned = true;
            }
        }
        if (prepExposureMap != 0) {
            glDeleteTextures(1, &prepExposureMap);
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
