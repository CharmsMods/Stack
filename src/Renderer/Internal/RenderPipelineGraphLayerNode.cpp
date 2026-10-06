#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/ScopedGLObjects.h"
#include "Editor/LayerRegistry.h"
#include "Editor/Layers/ToneLayers.h"

#include <functional>
#include <iostream>
#include <memory>
#include <string>

using namespace Stack::Renderer::GraphExecution;

RenderPipeline::GraphNodeRenderResult RenderPipeline::RenderLayerGraphNode(
    const GraphExecutionContext& executionContext,
    const RenderGraphNode& node,
    const std::function<unsigned int(int, const std::string&)>& evalImage,
    const std::function<unsigned int(int, const std::string&)>& evalMask) {
    GraphNodeRenderResult result;
    const auto fail = [&](const char* message, bool allocationFailed = false) {
        m_LastGraphExecutionStats.allocationFailed |= allocationFailed;
        m_LastGraphExecutionStats.lastSpecializedFailureNodeId = node.nodeId;
        m_LastGraphExecutionStats.lastSpecializedFailure = message;
        return GraphNodeRenderResult{};
    };

    const RenderGraphLink* input = executionContext.FindInputLink(node.nodeId, "imageIn");
    const unsigned int inputTexture = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
    if (inputTexture == 0 || !node.layerJson.is_object()) {
        return result;
    }
    if (!node.layerJson.value("enabled", true)) {
        result.texture = inputTexture;
        result.owned = false;
        return result;
    }
    const int inputWidth = m_Width;
    const int inputHeight = m_Height;

    const std::string type = node.layerJson.value("type", std::string());
    if (type == "RawOutputCrop")
        return RenderRawSpatialLayer(executionContext, node, inputTexture);
    std::shared_ptr<LayerBase> layer = LayerRegistry::CreateLayerFromTypeId(type);
    if (!layer) {
        return result;
    }

    layer->InitializeGL();
    layer->Deserialize(node.layerJson);
    if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(layer.get())) {
        toneCurve->SetAutoRewriteRenderContext(node.nodeId, node.requestRevision);
    }

    Stack::Renderer::ScopedGLTexture processed(
        CreateGraphRenderTargetTexture());
    const unsigned int sourceTexture = m_SourceTexture != 0 ? m_SourceTexture : inputTexture;
    const bool renderedLayer = RenderIntoGraphTargetTexture(processed.Get(), [&](unsigned int) {
        layer->ExecuteWithSource(inputTexture, sourceTexture, m_Width, m_Height, m_Quad);
    });
    if (!renderedLayer || !processed) {
        return fail("Layer rendering failed; previous presentation retained.", !processed);
    }

    if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(layer.get());
        toneCurve && toneCurve->HasPendingAutoRewriteFeedback()) {
        m_ToneCurveAutoRewriteFeedback.push_back(toneCurve->TakePendingAutoRewriteFeedback());
    }

    if (type == "ToneCurve" || type == "ViewTransform") {
        const QuickTextureStats inputStats = ProbeTextureStats(inputTexture, m_Width, m_Height);
        const QuickTextureStats outputStats = ProbeTextureStats(processed.Get(), m_Width, m_Height);
        if (!LayerPayloadExplicitlyRequestsCollapsedOutput(node.layerJson) &&
            IsImplausiblyDamagedTextureOutput(inputStats, outputStats)) {
            return fail("Layer produced invalid output; previous presentation retained.");
        }
    }

    const RenderGraphLink* maskLink = executionContext.FindInputLink(node.nodeId, "maskIn");
    const unsigned int maskTexture = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
    m_Width = inputWidth;
    m_Height = inputHeight;
    if (maskLink != nullptr && maskTexture == 0) {
        result.texture = inputTexture;
        result.owned = false;
        std::cerr << "[RenderPipeline] Connected mask could not be rendered for graph node "
                  << node.nodeId << "; passing input texture through.\n";
    } else if (maskTexture) {
        EnsureMaskPrograms();
        Stack::Renderer::ScopedGLTexture blended(
            CreateGraphRenderTargetTexture());
        bool blendPassExecuted = false;
        const bool renderedBlend =
            m_MaskBlendProgram != 0 &&
            RenderIntoGraphTargetTexture(blended.Get(), [&](unsigned int fbo) {
                blendPassExecuted = RenderMaskBlend(
                    inputTexture, processed.Get(), maskTexture, fbo);
            }) &&
            blendPassExecuted;
        if (renderedBlend && blended) {
            result.texture = blended.Release();
            result.owned = true;
        } else {
            return fail("Layer mask blending failed; previous presentation retained.", !blended);
        }
    } else {
        result.texture = processed.Release();
        result.owned = true;
    }

    return result;
}
