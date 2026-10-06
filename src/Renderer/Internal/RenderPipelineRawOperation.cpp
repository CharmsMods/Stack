#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/ScopedGLObjects.h"

#include <stdexcept>

RenderPipeline::GraphNodeRenderResult RenderPipeline::RenderRawOperation(
    const Stack::Renderer::GraphExecution::GraphExecutionContext& context,
    const RenderGraphNode& node,
    const std::function<unsigned int(int, const std::string&)>& evalImage,
    const std::function<unsigned int(int, const std::string&)>& evalMask) {
    using namespace Stack::RawRecipe;
    using namespace Stack::Renderer;
    const auto* inputLink = context.FindInputLink(node.nodeId, "imageIn");
    const unsigned int input = inputLink ? evalImage(inputLink->fromNodeId, inputLink->fromSocketId) : 0;
    if (!input) return {};
    GraphNodeRenderResult result;
    result.texture = input;
    if (!node.rawOperation.enabled) return result;
    auto recipe = ReadGraphOperation(node.rawOperation);
    recipe.technical.workingSpace = node.rawWorkingSpace;
    const int width = m_Width, height = m_Height;
    const auto native = ResolveGraphNativeExtent(context, inputLink->fromNodeId, inputLink->fromSocketId);
    const int nativeWidth = native.first, nativeHeight = native.second;
    const auto* maskLink = context.FindInputLink(node.nodeId, "maskIn");
    const unsigned int mask = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
    m_Width = width; m_Height = height;
    // An attached unfinished mask is zero coverage, never full application.
    if (maskLink && !mask) return result;
    ScopedGLTexture processed;
    const auto adopt = [&](unsigned int texture) {
        if (!texture) throw std::runtime_error("Photo operation could not produce its image.");
        processed.Reset(texture);
        result.texture = processed.Get();
    };
    switch (node.rawOperation.kind) {
        case GraphOperationKind::Calibration: {
            const auto calibration = BuildColorCalibrationTransform(recipe.colorCalibration, node.rawWorkingSpace);
            if (calibration.active) adopt(RenderRawDevelopmentExposure(input, 0, &calibration));
            break;
        }
        case GraphOperationKind::Exposure:
            if (recipe.preToneExposureEv != 0) adopt(RenderRawDevelopmentExposure(input, recipe.preToneExposureEv));
            break;
        case GraphOperationKind::LuminanceTone:
        case GraphOperationKind::RgbCurves: {
            if (node.rawOperation.kind == GraphOperationKind::LuminanceTone &&
                !IsSceneToneActive(ReadSceneTone(node.rawOperation.parameters))) break;
            if (node.rawOperation.kind == GraphOperationKind::RgbCurves &&
                recipe.finishTone.layerJson == DefaultFinishToneJson()) break;
            auto tone = node;
            tone.kind = RenderGraphNodeKind::Layer;
            tone.layerJson = recipe.finishTone.layerJson;
            tone.layerJson["type"] = "ToneCurve";
            tone.layerJson["enabled"] = true;
            tone.layerJson["truthfulV2SignedMath"] = true;
            tone.layerJson["inputWorkingSpace"] = WorkingSpaceStableString(node.rawWorkingSpace);
            // The existing curve executor also applies this node's mask.
            return RenderLayerGraphNode(context, tone, evalImage, evalMask);
        }
        case GraphOperationKind::LocalEv: {
            const auto* referenceLink = context.FindInputLink(node.nodeId, "referenceIn");
            const auto reference = referenceLink ? evalImage(referenceLink->fromNodeId, referenceLink->fromSocketId) : input;
            m_Width = width; m_Height = height;
            if (!reference) return {};
            const auto key = Stack::Renderer::GraphExecution::MakeNodeSocketKey(node.nodeId, "imageOut");
            const auto cached = context.imageFingerprintCache.find(key);
            const auto fingerprint = cached == context.imageFingerprintCache.end() ? 0 : cached->second;
            if (IsLocalRangeEnabled(recipe.localRange))
                adopt(RenderRawDevelopmentLocalRange(input, recipe.localRange, node.rawWorkingSpace,
                    recipe.technical.processingVersion, fingerprint, nativeWidth, nativeHeight));
            const bool graphCoverage = node.rawOperation.parameters.value("graphCoverage", false);
            const auto coverage = [&](const std::string& port) -> unsigned int {
                const auto* link = context.FindInputLink(node.nodeId, port);
                const auto texture = link ? evalMask(link->fromNodeId, link->fromSocketId) : 0;
                m_Width = width; m_Height = height;
                return texture;
            };
            if (HasZoneAreaGain(recipe.localRange) || (graphCoverage && recipe.localRange.enabled && !recipe.localRange.areas.empty())) {
                std::vector<RawZoneAreaStatistics> statistics;
                adopt(m_RawZoneAreaRenderer.Render(result.texture, reference, width, height,
                    recipe, false, m_PreviewMaxDimension <= 0, statistics, m_ShouldCancelRender, fingerprint,
                    graphCoverage ? std::function<unsigned int(const std::string&)>([&](const auto& id) { return coverage("area:" + id); })
                        : std::function<unsigned int(const std::string&)>{}));
            }
            const auto gradientInput = result.texture;
            ScopedGLTexture accumulated;
            RawGradientMask lastMask;
            for (const auto& gradient : recipe.evGradients) {
                if (!gradient.mask.enabled || !IsLocalRangeEnabled(gradient.curve)) continue;
                const auto maskTexture = graphCoverage ? coverage("gradient:" + gradient.mask.id) : 0;
                if (graphCoverage && !maskTexture) continue;
                ScopedGLTexture adjustment(RenderRawDevelopmentLocalRange(gradientInput, gradient.curve,
                    node.rawWorkingSpace, recipe.technical.processingVersion, fingerprint, nativeWidth, nativeHeight));
                if (!adjustment) throw std::runtime_error("Local EV gradient failed.");
                ScopedGLTexture next(RenderRawGradientBlend(accumulated ? accumulated.Get() : gradientInput,
                    adjustment.Get(), gradient.mask, gradientInput, accumulated ? 2 : 1, maskTexture));
                if (!next) throw std::runtime_error("Local EV accumulation failed.");
                accumulated.Reset(next.Release()); lastMask = gradient.mask;
            }
            if (accumulated) adopt(RenderRawGradientBlend(gradientInput, accumulated.Get(), lastMask, 0, 3));
            break;
        }
        case GraphOperationKind::ColorWarp:
            if (IsColorWarpEnabled(recipe.colorWarp)) {
                if (!RenderRawDevelopmentColorWarpStage(result, recipe)) return {};
                if (result.owned) processed.Reset(result.texture);
            }
            break;
        case GraphOperationKind::DetailContrast:
            if (IsDetailContrastActive(recipe.detailContrast))
                adopt(RenderRawSceneDetail(input, recipe.detailContrast, node.rawWorkingSpace, nativeWidth, nativeHeight));
            break;
        default: throw std::runtime_error("Unknown photo operation.");
    }
    if (!processed) return result;
    if (mask) {
        EnsureMaskPrograms();
        ScopedGLTexture blended(CreateGraphRenderTargetTexture());
        bool executed = false;
        if (!blended || !RenderIntoGraphTargetTexture(blended.Get(), [&](unsigned int fbo) {
                executed = RenderMaskBlend(input, processed.Get(), mask, fbo);
            }) || !executed) throw std::runtime_error("Photo operation mask blend failed.");
        processed.Reset(blended.Release());
    }
    result.texture = processed.Release(); result.owned = true;
    return result;
}
