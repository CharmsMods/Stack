#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/ScopedGLObjects.h"
#include "Renderer/GLHelpers.h"
#include "NodeMath/ImageOperationInput.h"
#include "NodeMath/ToneCurveParameters.h"
#include "Raw/Curves/CurveAuthoring.h"

#include <stdexcept>

namespace {
RenderGraphNode BuildToneLayer(const RenderGraphNode& node, const Stack::NodeMath::ValueDescriptor& input) {
    using namespace Stack::RawRecipe;
    const auto capability=Stack::NodeMath::DescribeImageOperationInput(input);
    const auto mode=Stack::NodeMath::ToneCurveMode(node.rawOperation.parameters);
    if (mode!="components" && !capability.colorRelationships)
        throw std::runtime_error(capability.colorUnavailableReason);
    auto tone = node;
    tone.kind = RenderGraphNodeKind::Layer;
    tone.layerJson = mode=="advanced" ? node.rawOperation.parameters.at("advanced") : DefaultFinishToneJson();
    if (mode!="advanced") {
        for (const auto& value:node.rawOperation.parameters.items()) tone.layerJson[value.key()]=value.value();
        auto scene = Stack::NodeMath::EffectiveToneCurveSceneTone(node.rawOperation.parameters);
        tone.layerJson["luminanceTone"] = SerializeSceneTone(scene);
        if (mode=="components" || mode=="luminance") {
            // Photo curves run in one pass: luminance gain, then R/G/B.
            // The numerical master belongs exclusively to Numeric channels.
            RawPointCurveComponent identity; identity.points={{0,0,1},{1,1,1}};
            for (int channel=0;channel<4;++channel) {
                const auto curveChannel=static_cast<RawPointCurveChannel>(channel);
                if (mode == "luminance" && channel == 0) {
                    StorePointCurveComponentInFinishToneJson(tone.layerJson,curveChannel,identity);
                    continue;
                }
                const auto key=RawPointCurveChannelKey(curveChannel);
                const auto amount=ReadCurveAmount(tone.layerJson["curveAmounts"][key]);
                const float strength=tone.layerJson["curveAmounts"][key].value("amount",1.f);
                auto component=PointCurveComponentFromFinishToneJson(tone.layerJson,curveChannel);
                const auto points=AttenuateCurve(amount.locked ? amount.reference : RawPointCurveBezierPoints(component),strength,true);
                component.points.clear();
                for (const auto& point:points) component.points.push_back({point.x,point.y,1,point.incoming,point.outgoing,point.tonal});
                StorePointCurveComponentInFinishToneJson(tone.layerJson,curveChannel,component);
            }
            // The stored component curves survive rewiring. Only real input
            // components participate in this execution; scalar uses Value.
            for (int channel=1;channel<4;++channel)
                if (!Stack::NodeMath::HasImageComponent(capability.components,
                    static_cast<Stack::NodeMath::ImageComponent>(1u<<(channel-1))))
                    StorePointCurveComponentInFinishToneJson(tone.layerJson,
                        static_cast<RawPointCurveChannel>(channel),identity);
        }
        else {
            tone.layerJson.erase("pointCurves");tone.layerJson.erase("pointCurveSetVersion");
        }
    }
    std::array<float,3> components{};
    for (int i=0;i<3;++i) components[i]=capability.scalar ? (i==0 ? 1.f : 0.f) :
        Stack::NodeMath::HasImageComponent(capability.components,
            static_cast<Stack::NodeMath::ImageComponent>(1u<<i)) ? 1.f : 0.f;
    tone.layerJson["executionComponents"]=components;
    tone.layerJson["type"] = "ToneCurve";
    tone.layerJson["enabled"] = true;
    tone.layerJson["truthfulV2SignedMath"] = true;
    tone.layerJson["inputWorkingSpace"] = capability.lumaWeights[0]>.25f ? "linear-rec2020-d65" : "linear-srgb-d65";
    return tone;
}
}

RenderPipeline::GraphNodeRenderResult RenderPipeline::RenderRawOperation(
    const Stack::Renderer::GraphExecution::GraphExecutionContext& context,
    const RenderGraphNode& node,
    const std::function<unsigned int(int, const std::string&)>& evalImage,
    const std::function<unsigned int(int, const std::string&)>& evalMask) {
    using namespace Stack::RawRecipe;
    using namespace Stack::Renderer;
    const auto* inputLink = context.FindInputLink(node.nodeId, "imageIn");
    if (inputLink && node.rawOperation.enabled && node.rawOperation.kind == GraphOperationKind::ToneCurve) {
        // The layer owns input evaluation and the single coverage blend.
        return RenderLayerGraphNode(context, BuildToneLayer(node, inputLink->semanticDescriptor), evalImage, evalMask);
    }
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
    // Capture input identity before evaluating optional masks, which can
    // change the pipeline's current image geometry.
    const auto inputRegion = m_CurrentGraphImageGeometry.resident;
    const auto* maskLink = context.FindInputLink(node.nodeId, "maskIn");
    const unsigned int mask = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
    m_Width = width; m_Height = height;
    if (maskLink && !mask) throw std::runtime_error("The connected mask is unavailable; this operation cannot render unrestricted.");
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
        case GraphOperationKind::LocalEv: {
            const float inputExposureEv = recipe.LocalEvInputExposureEv();
            if (inputExposureEv != 0.f) adopt(RenderRawDevelopmentExposure(input, inputExposureEv));
            const auto key = Stack::Renderer::GraphExecution::MakeNodeSocketKey(inputLink->fromNodeId, inputLink->fromSocketId);
            const auto cached = context.imageFingerprintCache.find(key);
            auto fingerprint = cached == context.imageFingerprintCache.end() ? 0 : cached->second;
            if(fingerprint) Stack::Renderer::RawDevelopmentCache::HashTypedValue(fingerprint, inputExposureEv);
            if (IsLocalRangeEnabled(recipe.localRange))
                adopt(RenderRawDevelopmentLocalRange(result.texture, recipe.localRange, node.rawWorkingSpace,
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
                const auto sceneColorGuide=inputRegion.Partial() ? SceneColorGuideCache::Lease{} :
                    ResolveSceneColorGuidance(context,inputLink->fromNodeId,inputLink->fromSocketId);
                adopt(m_RawZoneAreaRenderer.Render(result.texture, input, width, height,
                    recipe, false, m_PreviewMaxDimension <= 0, statistics, m_ShouldCancelRender, fingerprint,
                    graphCoverage ? std::function<unsigned int(const std::string&)>([&](const auto& id) { return coverage("area:" + id); })
                        : std::function<unsigned int(const std::string&)>{},sceneColorGuide.view));
            }
            const auto gradientInput = result.texture;
            ScopedGLTexture accumulated;
            RawGradientMask lastMask;
            for (const auto& gradient : recipe.evGradients) {
                if (!gradient.mask.enabled || !IsLocalRangeEnabled(gradient.curve)) continue;
                const auto maskTexture = graphCoverage ? coverage("gradient:" + gradient.mask.id) : 0;
                if (graphCoverage && !maskTexture) continue;
                ScopedGLTexture adjustment(RenderRawDevelopmentLocalRange(gradientInput, gradient.curve,
                    node.rawWorkingSpace, recipe.technical.processingVersion, 0, nativeWidth, nativeHeight));
                if (!adjustment) throw std::runtime_error("Local EV gradient failed.");
                ScopedGLTexture next(RenderRawGradientBlend(accumulated ? accumulated.Get() : gradientInput,
                    adjustment.Get(), gradient.mask, gradientInput, accumulated ? 2 : 1, maskTexture));
                if (!next) throw std::runtime_error("Local EV accumulation failed.");
                accumulated.Reset(next.Release()); lastMask = gradient.mask;
            }
            if (accumulated) adopt(RenderRawGradientBlend(gradientInput, accumulated.Get(), lastMask, 0, 3));
            if (recipe.exposureAfterLight && recipe.preToneExposureEv != 0.f) {
                const auto protectedSpace = recipe.protectExposureHighlights
                    ? std::optional{node.rawWorkingSpace} : std::nullopt;
                adopt(RenderRawDevelopmentExposure(result.texture, recipe.preToneExposureEv, nullptr, protectedSpace));
            }
            break;
        }
        case GraphOperationKind::ColorWarp:
            if (IsColorWarpEnabled(recipe.colorWarp)) {
                const auto key = GraphExecution::MakeNodeSocketKey(
                    inputLink->fromNodeId, inputLink->fromSocketId);
                const auto cached = context.imageFingerprintCache.find(key);
                const auto inputFingerprint = cached == context.imageFingerprintCache.end()
                    ? 0u : cached->second;
                if (!RenderRawDevelopmentColorWarpStage(result, recipe,
                    inputFingerprint, nativeWidth, nativeHeight, inputRegion)) return {};
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
        ScopedGLTexture blended(GLHelpers::CreateStorageTexture(m_Width,m_Height,GL_RGBA32F));
        bool executed = false;
        if (!blended || !RenderIntoGraphTargetTexture(blended.Get(), [&](unsigned int fbo) {
                executed = RenderMaskBlend(input, processed.Get(), mask, fbo);
            }) || !executed) throw std::runtime_error("Photo operation mask blend failed.");
        processed.Reset(blended.Release());
    }
    result.texture = processed.Release(); result.owned = true;
    return result;
}
