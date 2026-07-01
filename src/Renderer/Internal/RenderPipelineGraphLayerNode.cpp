#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
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

    const RenderGraphLink* input = executionContext.FindInputLink(node.nodeId, "imageIn");
    const unsigned int inputTexture = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
    if (inputTexture == 0 || !node.layerJson.is_object()) {
        return result;
    }

    const std::string type = node.layerJson.value("type", std::string());
    std::shared_ptr<LayerBase> layer = LayerRegistry::CreateLayerFromTypeId(type);
    if (!layer) {
        return result;
    }

    layer->InitializeGL();
    layer->Deserialize(node.layerJson);
    if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(layer.get())) {
        toneCurve->SetAutoRewriteRenderContext(node.nodeId, node.requestRevision);
    }

    unsigned int processed = CreateGraphRenderTargetTexture();
    const unsigned int sourceTexture = m_SourceTexture != 0 ? m_SourceTexture : inputTexture;
    const bool renderedLayer = RenderIntoGraphTargetTexture(processed, [&](unsigned int) {
        layer->ExecuteWithSource(inputTexture, sourceTexture, m_Width, m_Height, m_Quad);
    });
    if (!renderedLayer || processed == 0) {
        if (processed != 0) {
            glDeleteTextures(1, &processed);
        }
        result.texture = inputTexture;
        result.owned = false;
        std::cerr << "[RenderPipeline] Layer target allocation failed for graph node "
                  << node.nodeId << "; passing input texture through.\n";
        return result;
    }

    if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(layer.get());
        toneCurve && toneCurve->HasPendingAutoRewriteFeedback()) {
        m_ToneCurveAutoRewriteFeedback.push_back(toneCurve->TakePendingAutoRewriteFeedback());
    }

    result.texture = processed;
    result.owned = true;
    if (type == "ToneCurve" && IsDefaultToneCurvePayload(node.layerJson)) {
        const QuickTextureStats inputStats = ProbeTextureStats(inputTexture, m_Width, m_Height);
        const QuickTextureStats outputStats = ProbeTextureStats(processed, m_Width, m_Height);
        const bool inputHasSignal = inputStats.valid && inputStats.p99Luma > 0.00001f;
        const bool outputIsBlank =
            outputStats.valid &&
            outputStats.p99Luma <= 0.000001f &&
            outputStats.maxRgb <= 0.00001f;
        if (inputHasSignal && outputIsBlank) {
            glDeleteTextures(1, &processed);
            result.texture = inputTexture;
            result.owned = false;
            std::cerr << "[RenderPipeline] Default Tone Curve produced a blank output for graph node "
                      << node.nodeId << " (input p99 luma " << inputStats.p99Luma
                      << ", output p99 luma " << outputStats.p99Luma
                      << "); passing input texture through.\n";
            return result;
        }
    }

    const RenderGraphLink* maskLink = executionContext.FindInputLink(node.nodeId, "maskIn");
    const unsigned int maskTexture = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
    if (maskTexture) {
        unsigned int blended = CreateGraphRenderTargetTexture();
        RenderIntoGraphTargetTexture(blended, [&](unsigned int fbo) {
            RenderMaskBlend(inputTexture, processed, maskTexture, fbo);
        });
        if (processed != 0) {
            glDeleteTextures(1, &processed);
        }
        result.texture = blended;
        result.owned = result.texture != 0;
    }

    return result;
}
