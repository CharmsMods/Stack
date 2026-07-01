#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Raw/RawDevelopmentRecipe.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>

using namespace Stack::Renderer::GraphExecution;

void RenderPipeline::ExecuteGraphImpl(const RenderGraphSnapshot& graph) {
    m_GraphSourceTexture = 0;
    m_LastGraphImageCacheHits.clear();
    m_LastGraphExecutionStats = {};
    m_AutoGainSceneStatsCache.clear();
    m_PreLocalExposureSummaries.clear();
    m_ToneCurveAutoRewriteFeedback.clear();
    ClearRawDevelopmentLocalRangeOverlay();
    ClearRawDevelopmentLocalRangeTargetSample();
    m_RawDevelopmentViewTransformInputStats = {};
    m_RawDevelopmentLocalSuggestionImage = {};
    m_RawDevelopmentLocalRangeOverlayRequestMode = graph.rawWorkspaceLocalRangeOverlayMode;
    m_RawDevelopmentLocalRangeTargetSampleRequested =
        graph.rawWorkspaceLocalRangeTargetSampleRequested;
    m_RawDevelopmentLocalRangeTargetSampleRequestU =
        std::clamp(graph.rawWorkspaceLocalRangeTargetSampleU, 0.0f, 1.0f);
    m_RawDevelopmentLocalRangeTargetSampleRequestV =
        std::clamp(graph.rawWorkspaceLocalRangeTargetSampleV, 0.0f, 1.0f);
    if (m_Width == 0 || m_Height == 0 || graph.outputNodeId <= 0) {
        m_OutputTexture = 0;
        return;
    }

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, m_Width, m_Height);

    if (m_ExternalOutputTexture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
        m_ExternalOutputTexture = 0;
    }

    GraphExecutionContext executionContext(graph);
    auto& nodes = executionContext.nodes;
    auto& imageCache = executionContext.imageCache;
    auto& maskCache = executionContext.maskCache;
    auto& imageFingerprintCache = executionContext.imageFingerprintCache;
    auto& maskFingerprintCache = executionContext.maskFingerprintCache;
    auto& visitingImages = executionContext.visitingImages;
    auto& visitingMasks = executionContext.visitingMasks;
    auto& fingerprintingImages = executionContext.fingerprintingImages;
    auto& fingerprintingMasks = executionContext.fingerprintingMasks;

    auto findInputLink = [&](int nodeId, const std::string& socketId) -> const RenderGraphLink* {
        return executionContext.FindInputLink(nodeId, socketId);
    };

    auto createTarget = [&]() {
        return CreateGraphRenderTargetTexture();
    };

    auto renderToTexture = [&](unsigned int texture, const std::function<void(unsigned int)>& renderFn) -> bool {
        return RenderIntoGraphTargetTexture(texture, renderFn);
    };

    std::function<unsigned int(int, const std::string&)> evalMask;
    std::function<unsigned int(int, const std::string&)> evalImage;
    std::function<std::size_t(int, const std::string&)> fingerprintMask;
    std::function<std::size_t(int, const std::string&)> fingerprintImage;

    fingerprintMask = [&](int nodeId, const std::string& socketId) -> std::size_t {
        std::string key = std::to_string(nodeId) + ":" + socketId;
        auto cached = maskFingerprintCache.find(key);
        if (cached != maskFingerprintCache.end()) {
            return cached->second;
        }
        if (fingerprintingMasks.count(key)) {
            return 0;
        }
        fingerprintingMasks.insert(key);

        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            fingerprintingMasks.erase(key);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        if (node.kind == RenderGraphNodeKind::Image ||
            node.kind == RenderGraphNodeKind::RawDevelopment ||
            node.kind == RenderGraphNodeKind::RawNeuralDenoise ||
            node.kind == RenderGraphNodeKind::RawDecode ||
            node.kind == RenderGraphNodeKind::RawDevelop ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId != "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId != "maskOut") ||
            node.kind == RenderGraphNodeKind::HdrMerge ||
            node.kind == RenderGraphNodeKind::Mfsr ||
            node.kind == RenderGraphNodeKind::Lut ||
            node.kind == RenderGraphNodeKind::Layer ||
            node.kind == RenderGraphNodeKind::Mix ||
            (node.kind == RenderGraphNodeKind::DataMath && !IsScalarRenderSocket(executionContext, nodeId, socketId)) ||
            node.kind == RenderGraphNodeKind::ImageGenerator ||
            node.kind == RenderGraphNodeKind::ChannelCombine ||
            node.kind == RenderGraphNodeKind::Output) {
            std::size_t imgFp = fingerprintImage(nodeId, socketId);
            fingerprintingMasks.erase(key);
            maskFingerprintCache[key] = imgFp;
            return imgFp;
        }
        std::size_t fingerprint = HashValue(static_cast<int>(node.kind));
        HashCombine(fingerprint, HashValue(node.nodeId));
        HashCombine(fingerprint, HashValue(socketId));

        if (node.kind == RenderGraphNodeKind::MaskGenerator) {
            HashCombine(fingerprint, HashValue(static_cast<int>(node.maskKind)));
            HashCombine(fingerprint, HashValue(node.maskSettings.value));
            HashCombine(fingerprint, HashValue(node.maskSettings.angle));
            HashCombine(fingerprint, HashValue(node.maskSettings.offset));
            HashCombine(fingerprint, HashValue(node.maskSettings.scale));
            HashCombine(fingerprint, HashValue(node.maskSettings.centerX));
            HashCombine(fingerprint, HashValue(node.maskSettings.centerY));
            HashCombine(fingerprint, HashValue(node.maskSettings.radius));
            HashCombine(fingerprint, HashValue(node.maskSettings.feather));
            HashCombine(fingerprint, HashValue(node.maskSettings.invert));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::MaskCombine) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "maskA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "maskB");
            HashCombine(fingerprint, inputA ? fingerprintMask(inputA->fromNodeId, inputA->fromSocketId) : 0);
            HashCombine(fingerprint, inputB ? fingerprintMask(inputB->fromNodeId, inputB->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.maskCombineMode)));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::CustomMask) {
            const RenderCustomMaskPayload& payload = node.customMask;
            HashCombine(fingerprint, HashValue(payload.width));
            HashCombine(fingerprint, HashValue(payload.height));
            HashCombine(fingerprint, HashValue(payload.invert));
            HashCombine(fingerprint, HashValue(payload.blurRadius));
            HashCombine(fingerprint, HashValue(payload.expandContract));
            HashCombine(fingerprint, HashValue(payload.rasterLayer.size()));
            if (!payload.rasterLayer.empty()) {
                HashCombine(
                    fingerprint,
                    HashBytes(
                        reinterpret_cast<const unsigned char*>(payload.rasterLayer.data()),
                        payload.rasterLayer.size() * sizeof(float)));
            }
            HashCombine(fingerprint, HashValue(payload.objects.size()));
            for (const RenderCustomMaskObject& object : payload.objects) {
                HashCombine(fingerprint, HashValue(object.id));
                HashCombine(fingerprint, HashValue(static_cast<int>(object.type)));
                HashCombine(fingerprint, HashValue(static_cast<int>(object.operation)));
                HashCombine(fingerprint, HashValue(object.enabled));
                HashCombine(fingerprint, HashValue(object.invert));
                HashCombine(fingerprint, HashValue(object.strength));
                HashCombine(fingerprint, HashValue(object.feather));
                HashCombine(fingerprint, HashValue(object.blur));
                HashCombine(fingerprint, HashValue(object.points.size()));
                for (const RenderCustomMaskPoint& point : object.points) {
                    HashCombine(fingerprint, HashValue(point.x));
                    HashCombine(fingerprint, HashValue(point.y));
                }
            }
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::MaskUtility) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, input ? fingerprintMask(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.maskUtilityKind)));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.blackPoint));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.whitePoint));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.gamma));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.threshold));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.softness));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.invert));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ImageToMask) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.imageToMaskKind)));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.low));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.high));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.softness));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.invert));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleCount));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleRgb[0]));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleRgb[1]));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleRgb[2]));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleLuma));
            for (int i = 0; i < 4; ++i) {
                HashCombine(fingerprint, HashValue(node.imageToMaskSettings.extraSampleRgb[i][0]));
                HashCombine(fingerprint, HashValue(node.imageToMaskSettings.extraSampleRgb[i][1]));
                HashCombine(fingerprint, HashValue(node.imageToMaskSettings.extraSampleRgb[i][2]));
                HashCombine(fingerprint, HashValue(node.imageToMaskSettings.extraSampleLuma[i]));
            }
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleU));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.sampleV));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.toneSimilarity));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.colorSimilarity));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.regionRadius));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.regionFeather));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.edgeSensitivity));
            HashCombine(fingerprint, HashValue(node.imageToMaskSettings.localCoherence));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ChannelSplit) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::DataMath) {
            for (const DataMathInputLinkInfo& input : CollectDataMathAverageInputs(executionContext, node.nodeId)) {
                HashCombine(fingerprint, HashValue(input.socketId));
                HashCombine(fingerprint, fingerprintMask(input.link->fromNodeId, input.link->fromSocketId));
            }
            if (const RenderGraphLink* baseInput = findInputLink(node.nodeId, EditorNodeGraph::kDataMathBaseInputSocketId)) {
                HashCombine(fingerprint, HashValue(std::string(EditorNodeGraph::kDataMathBaseInputSocketId)));
                HashCombine(fingerprint, fingerprintMask(baseInput->fromNodeId, baseInput->fromSocketId));
            }
            if (const RenderGraphLink* maskInput = findInputLink(node.nodeId, EditorNodeGraph::kMaskInputSocketId)) {
                HashCombine(fingerprint, HashValue(std::string(EditorNodeGraph::kMaskInputSocketId)));
                HashCombine(fingerprint, fingerprintMask(maskInput->fromNodeId, maskInput->fromSocketId));
            }
            HashCombine(fingerprint, HashValue(static_cast<int>(node.dataMathMode)));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.constantA));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.constantB));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.minValue));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.maxValue));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.outMin));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.outMax));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::RawDetailAutoMask) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            const Raw::RawDetailFusionSettings& settings = node.rawDetailAutoMask.settings;
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.mode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.debugView)));
            HashCombine(fingerprint, HashValue(settings.autoSafetyEnabled));
            HashCombine(fingerprint, HashValue(settings.overrideMinEv));
            HashCombine(fingerprint, HashValue(settings.overrideMaxEv));
            HashCombine(fingerprint, HashValue(settings.overrideBaseEv));
            HashCombine(fingerprint, HashValue(settings.overrideNoiseProtection));
            HashCombine(fingerprint, HashValue(settings.overrideHighlightProtection));
            HashCombine(fingerprint, HashValue(settings.overrideShadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.overrideWellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.minEvBias));
            HashCombine(fingerprint, HashValue(settings.maxEvBias));
            HashCombine(fingerprint, HashValue(settings.baseEvBias));
            HashCombine(fingerprint, HashValue(settings.noiseProtectionBias));
            HashCombine(fingerprint, HashValue(settings.highlightProtectionBias));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimitBias));
            HashCombine(fingerprint, HashValue(settings.wellExposedTargetBias));
            HashCombine(fingerprint, HashValue(settings.minEv));
            HashCombine(fingerprint, HashValue(settings.maxEv));
            HashCombine(fingerprint, HashValue(settings.baseEv));
            HashCombine(fingerprint, HashValue(settings.strength));
            HashCombine(fingerprint, HashValue(settings.sampleCount));
            HashCombine(fingerprint, HashValue(settings.baseRadiusPercent));
            HashCombine(fingerprint, HashValue(settings.highlightProtection));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.noiseProtection));
            HashCombine(fingerprint, HashValue(settings.detailWeight));
            HashCombine(fingerprint, HashValue(settings.wellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.smoothGradientProtection));
            HashCombine(fingerprint, HashValue(settings.textureSensitivity));
            HashCombine(fingerprint, HashValue(settings.skyBias));
            HashCombine(fingerprint, HashValue(settings.invertMask));
            HashCombine(fingerprint, HashValue(settings.maskBlackPoint));
            HashCombine(fingerprint, HashValue(settings.maskWhitePoint));
            HashCombine(fingerprint, HashValue(settings.maskGamma));
            HashCombine(fingerprint, HashValue(settings.smoothnessRadius));
            HashCombine(fingerprint, HashValue(settings.smoothAreaRadius));
            HashCombine(fingerprint, HashValue(settings.edgeAwareness));
            HashCombine(fingerprint, HashValue(settings.haloGuard));
            HashCombine(fingerprint, HashValue(settings.maskDebandDither));
            HashCombine(fingerprint, HashValue(settings.manualBlend));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, maskLink ? fingerprintMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0);
            const Raw::RawDetailFusionSettings& settings = node.rawDetailFusion.settings;
            HashCombine(fingerprint, HashValue(settings.autoSafetyEnabled));
            HashCombine(fingerprint, HashValue(settings.overrideMinEv));
            HashCombine(fingerprint, HashValue(settings.overrideMaxEv));
            HashCombine(fingerprint, HashValue(settings.overrideBaseEv));
            HashCombine(fingerprint, HashValue(settings.overrideNoiseProtection));
            HashCombine(fingerprint, HashValue(settings.overrideHighlightProtection));
            HashCombine(fingerprint, HashValue(settings.overrideShadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.overrideWellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.minEvBias));
            HashCombine(fingerprint, HashValue(settings.maxEvBias));
            HashCombine(fingerprint, HashValue(settings.baseEvBias));
            HashCombine(fingerprint, HashValue(settings.noiseProtectionBias));
            HashCombine(fingerprint, HashValue(settings.highlightProtectionBias));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimitBias));
            HashCombine(fingerprint, HashValue(settings.wellExposedTargetBias));
            HashCombine(fingerprint, HashValue(settings.minEv));
            HashCombine(fingerprint, HashValue(settings.maxEv));
            HashCombine(fingerprint, HashValue(settings.baseEv));
            HashCombine(fingerprint, HashValue(settings.sampleCount));
            HashCombine(fingerprint, HashValue(settings.baseRadiusPercent));
            HashCombine(fingerprint, HashValue(settings.highlightProtection));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.noiseProtection));
            HashCombine(fingerprint, HashValue(settings.detailWeight));
            HashCombine(fingerprint, HashValue(settings.wellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.smoothGradientProtection));
            HashCombine(fingerprint, HashValue(settings.textureSensitivity));
            HashCombine(fingerprint, HashValue(settings.skyBias));
            HashCombine(fingerprint, HashValue(settings.invertMask));
            HashCombine(fingerprint, HashValue(settings.maskBlackPoint));
            HashCombine(fingerprint, HashValue(settings.maskWhitePoint));
            HashCombine(fingerprint, HashValue(settings.maskGamma));
            HashCombine(fingerprint, HashValue(settings.smoothnessRadius));
            HashCombine(fingerprint, HashValue(settings.smoothAreaRadius));
            HashCombine(fingerprint, HashValue(settings.edgeAwareness));
            HashCombine(fingerprint, HashValue(settings.haloGuard));
            HashCombine(fingerprint, HashValue(settings.maskDebandDither));
            HashCombine(fingerprint, HashValue(settings.manualBlend));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        }

        fingerprintingMasks.erase(key);
        maskFingerprintCache[key] = fingerprint;
        return fingerprint;
    };

    fingerprintImage = [&](int nodeId, const std::string& socketId) -> std::size_t {
        std::string key = std::to_string(nodeId) + ":" + socketId;
        auto cached = imageFingerprintCache.find(key);
        if (cached != imageFingerprintCache.end()) {
            return cached->second;
        }
        if (fingerprintingImages.count(key)) {
            return 0;
        }
        fingerprintingImages.insert(key);

        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            fingerprintingImages.erase(key);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        if (node.kind == RenderGraphNodeKind::MaskGenerator ||
            node.kind == RenderGraphNodeKind::MaskCombine ||
            node.kind == RenderGraphNodeKind::MaskUtility ||
            node.kind == RenderGraphNodeKind::CustomMask ||
            node.kind == RenderGraphNodeKind::ImageToMask ||
            node.kind == RenderGraphNodeKind::ChannelSplit ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId == "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId == "maskOut")) {
            std::size_t maskFp = fingerprintMask(nodeId, socketId);
            fingerprintingImages.erase(key);
            imageFingerprintCache[key] = maskFp;
            return maskFp;
        }
        std::size_t fingerprint = HashValue(static_cast<int>(node.kind));
        HashCombine(fingerprint, HashValue(node.nodeId));
        HashCombine(fingerprint, HashValue(socketId));
        auto hashRawDevelopSettings = [&](const Raw::RawDevelopSettings& settings) {
            HashCombine(fingerprint, HashValue(settings.exposureStops));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.whiteBalanceMode)));
            for (float value : settings.manualWhiteBalance) {
                HashCombine(fingerprint, HashValue(value));
            }
            HashCombine(fingerprint, HashValue(settings.overrideBlackLevel));
            HashCombine(fingerprint, HashValue(settings.blackLevelOverride));
            HashCombine(fingerprint, HashValue(settings.overrideWhiteLevel));
            HashCombine(fingerprint, HashValue(settings.whiteLevelOverride));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.highlightMode)));
            HashCombine(fingerprint, HashValue(settings.highlightStrength));
            HashCombine(fingerprint, HashValue(settings.highlightThreshold));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.demosaicMethod)));
            HashCombine(fingerprint, HashValue(settings.cameraTransformEnabled));
            HashCombine(fingerprint, HashValue(settings.debugBypassCameraTransform));
            HashCombine(fingerprint, HashValue(settings.debugTransposeCameraMatrix));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.debugView)));
            HashCombine(fingerprint, HashValue(settings.rotationDegrees));
            HashCombine(fingerprint, HashValue(settings.rotateToFitFrame));
            HashCombine(fingerprint, HashValue(settings.flipHorizontally));
            HashCombine(fingerprint, HashValue(settings.flipVertically));
            HashCombine(fingerprint, HashValue(settings.falseColorSuppression));
            HashCombine(fingerprint, HashValue(settings.defringeStrength));
            HashCombine(fingerprint, HashValue(settings.highlightEdgeCleanup));
            HashCombine(fingerprint, HashValue(settings.chromaRadius));
            HashCombine(fingerprint, HashValue(settings.preserveRealColor));
            HashCombine(fingerprint, HashValue(settings.lateralRedCyan));
            HashCombine(fingerprint, HashValue(settings.lateralBlueYellow));
            HashCombine(fingerprint, HashValue(settings.toneCurvePoints.size()));
            for (const Raw::RawToneCurvePoint& point : settings.toneCurvePoints) {
                HashCombine(fingerprint, HashValue(point.input));
                HashCombine(fingerprint, HashValue(point.output));
            }
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.cameraTransformSource)));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.enabled));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.hotPixelSuppression));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.hotPixelThreshold));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.lumaStrength));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.chromaStrength));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.radius));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.edgeProtection));
            HashCombine(fingerprint, HashValue(settings.mosaicDenoise.iterations));
        };
        auto hashDngGainMaps = [&](const std::vector<Raw::DngGainMapOpcode>& maps) {
            HashCombine(fingerprint, HashValue(maps.size()));
            for (const Raw::DngGainMapOpcode& map : maps) {
                HashCombine(fingerprint, HashValue(map.top));
                HashCombine(fingerprint, HashValue(map.left));
                HashCombine(fingerprint, HashValue(map.bottom));
                HashCombine(fingerprint, HashValue(map.right));
                HashCombine(fingerprint, HashValue(map.plane));
                HashCombine(fingerprint, HashValue(map.planes));
                HashCombine(fingerprint, HashValue(map.rowPitch));
                HashCombine(fingerprint, HashValue(map.colPitch));
                HashCombine(fingerprint, HashValue(map.mapPointsV));
                HashCombine(fingerprint, HashValue(map.mapPointsH));
                HashCombine(fingerprint, HashValue(map.mapPlanes));
                HashCombine(fingerprint, HashValue(map.mapSpacingV));
                HashCombine(fingerprint, HashValue(map.mapSpacingH));
                HashCombine(fingerprint, HashValue(map.mapOriginV));
                HashCombine(fingerprint, HashValue(map.mapOriginH));
                HashCombine(fingerprint, HashValue(map.gains.size()));
                if (!map.gains.empty()) {
                    HashCombine(
                        fingerprint,
                        HashBytes(
                            reinterpret_cast<const unsigned char*>(map.gains.data()),
                            map.gains.size() * sizeof(float)));
                }
            }
        };
        auto hashRawMetadata = [&](const Raw::RawMetadata& metadata) {
            HashCombine(fingerprint, HashValue(metadata.sourcePath));
            HashCombine(fingerprint, HashValue(metadata.rawWidth));
            HashCombine(fingerprint, HashValue(metadata.rawHeight));
            HashCombine(fingerprint, HashValue(metadata.visibleWidth));
            HashCombine(fingerprint, HashValue(metadata.visibleHeight));
            HashCombine(fingerprint, HashValue(metadata.leftMargin));
            HashCombine(fingerprint, HashValue(metadata.topMargin));
            HashCombine(fingerprint, HashValue(metadata.orientation));
            HashCombine(fingerprint, HashValue(metadata.bitDepth));
            HashCombine(fingerprint, HashValue(static_cast<int>(metadata.cfaPattern)));
            HashCombine(fingerprint, HashValue(static_cast<int>(metadata.pixelLayout)));
            HashCombine(fingerprint, HashValue(metadata.mosaiced));
            HashCombine(fingerprint, HashValue(metadata.isDng));
            HashCombine(fingerprint, HashValue(metadata.blackLevel));
            for (float value : metadata.perChannelBlack) {
                HashCombine(fingerprint, HashValue(value));
            }
            HashCombine(fingerprint, HashValue(metadata.whiteLevel));
            HashCombine(fingerprint, HashValue(metadata.rawMinimum));
            HashCombine(fingerprint, HashValue(metadata.rawMaximum));
            HashCombine(fingerprint, HashValue(metadata.defaultWhiteClipPercent));
            for (float value : metadata.cameraWhiteBalance) {
                HashCombine(fingerprint, HashValue(value));
            }
            for (float value : metadata.daylightWhiteBalance) {
                HashCombine(fingerprint, HashValue(value));
            }
            for (float value : metadata.cameraToSrgb) {
                HashCombine(fingerprint, HashValue(value));
            }
            HashCombine(fingerprint, HashValue(metadata.hasCameraMatrix));
            HashCombine(fingerprint, HashValue(metadata.hasDngAsShotNeutral));
            for (float value : metadata.dngAsShotNeutral) {
                HashCombine(fingerprint, HashValue(value));
            }
            HashCombine(fingerprint, HashValue(metadata.dngGainMapCount));
            HashCombine(fingerprint, HashValue(metadata.dngUnsupportedOpcodeCount));
            hashDngGainMaps(metadata.dngGainMaps);
            HashCombine(fingerprint, HashValue(metadata.uploadFormat));
            HashCombine(fingerprint, HashValue(metadata.linearChannels));
            HashCombine(fingerprint, HashValue(static_cast<int>(metadata.linearSampleFormat)));
        };

        if (node.kind == RenderGraphNodeKind::Image) {
            if (!node.image.pixels.empty() && node.image.width > 0 && node.image.height > 0) {
                HashCombine(fingerprint, HashValue(node.image.width));
                HashCombine(fingerprint, HashValue(node.image.height));
                HashCombine(fingerprint, HashValue(node.image.channels));
                HashCombine(
                    fingerprint,
                    node.image.pixels.fingerprint != 0
                        ? node.image.pixels.fingerprint
                        : StackHash::HashBytes(*node.image.pixels.bytes));
            } else {
                HashCombine(fingerprint, m_SourceFingerprint);
                HashCombine(fingerprint, HashValue(m_Width));
                HashCombine(fingerprint, HashValue(m_Height));
                HashCombine(fingerprint, HashValue(m_SourceChannels));
            }
        } else if (node.kind == RenderGraphNodeKind::RawSource) {
            HashCombine(fingerprint, HashValue(node.rawSource.sourcePath));
            const bool hasEmbeddedRaw =
                !node.rawSource.embeddedRawData.rawBuffer.empty() ||
                !node.rawSource.embeddedRawData.linearUInt16Buffer.empty() ||
                !node.rawSource.embeddedRawData.linearFloatBuffer.empty();
            HashCombine(fingerprint, HashValue(hasEmbeddedRaw));
            if (!hasEmbeddedRaw && !node.rawSource.sourcePath.empty()) {
                std::error_code sizeError;
                const std::uintmax_t fileSize = std::filesystem::file_size(node.rawSource.sourcePath, sizeError);
                std::error_code timeError;
                const auto modifiedTime = std::filesystem::last_write_time(node.rawSource.sourcePath, timeError);
                HashCombine(fingerprint, HashValue(!sizeError));
                HashCombine(fingerprint, HashValue(sizeError ? 0ull : static_cast<unsigned long long>(fileSize)));
                HashCombine(fingerprint, HashValue(!timeError));
                HashCombine(
                    fingerprint,
                    HashValue(timeError
                        ? 0ll
                        : static_cast<long long>(modifiedTime.time_since_epoch().count())));
            }
            if (hasEmbeddedRaw) {
                const Raw::RawImageData& embedded = node.rawSource.embeddedRawData;
                hashRawMetadata(embedded.metadata);
                HashCombine(fingerprint, HashValue(embedded.rawBuffer.size()));
                if (!embedded.rawBuffer.empty()) {
                    HashCombine(
                        fingerprint,
                        HashBytes(
                            reinterpret_cast<const unsigned char*>(embedded.rawBuffer.data()),
                            embedded.rawBuffer.size() * sizeof(std::uint16_t)));
                }
                HashCombine(fingerprint, HashValue(embedded.linearUInt16Buffer.size()));
                if (!embedded.linearUInt16Buffer.empty()) {
                    HashCombine(
                        fingerprint,
                        HashBytes(
                            reinterpret_cast<const unsigned char*>(embedded.linearUInt16Buffer.data()),
                            embedded.linearUInt16Buffer.size() * sizeof(std::uint16_t)));
                }
                HashCombine(fingerprint, HashValue(embedded.linearFloatBuffer.size()));
                if (!embedded.linearFloatBuffer.empty()) {
                    HashCombine(
                        fingerprint,
                        HashBytes(
                            reinterpret_cast<const unsigned char*>(embedded.linearFloatBuffer.data()),
                            embedded.linearFloatBuffer.size() * sizeof(float)));
                }
            }
            hashRawMetadata(node.rawSource.metadata);
        } else if (node.kind == RenderGraphNodeKind::RawDevelopment) {
            HashCombine(fingerprint, HashValue(Stack::RawRecipe::SerializeRecipe(node.rawDevelopment.recipe).dump()));
            HashCombine(fingerprint, HashValue(m_PreviewMaxDimension));
        } else if (node.kind == RenderGraphNodeKind::RawNeuralDenoise) {
            const RenderGraphLink* rawInput = findInputLink(node.nodeId, "rawIn");
            HashCombine(fingerprint, rawInput ? fingerprintImage(rawInput->fromNodeId, rawInput->fromSocketId) : 0);
            HashCombine(fingerprint, HashJson(NeuralDenoise::SerializeSettings(node.rawNeuralDenoise.settings)));
        } else if (node.kind == RenderGraphNodeKind::RawDecode) {
            const RenderGraphLink* rawInput = findInputLink(node.nodeId, "rawIn");
            HashCombine(fingerprint, rawInput ? fingerprintImage(rawInput->fromNodeId, rawInput->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(m_PreviewMaxDimension));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
            hashRawDevelopSettings(node.rawDecode.settings);
        } else if (node.kind == RenderGraphNodeKind::RawDevelop) {
            const RenderGraphLink* rawInput = findInputLink(node.nodeId, "rawIn");
            HashCombine(fingerprint, rawInput ? fingerprintImage(rawInput->fromNodeId, rawInput->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(m_PreviewMaxDimension));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
            hashRawDevelopSettings(node.rawDevelop.settings);
            const bool rawBaseSocket = socketId == "__rawDevelopBase";
            if (!rawBaseSocket) {
                HashCombine(fingerprint, HashValue(node.rawDevelop.scenePrepEnabled));
            }
            if (!rawBaseSocket && node.rawDevelop.scenePrepEnabled) {
                const Raw::RawDetailFusionSettings& prep = node.rawDevelop.scenePrepSettings;
                HashCombine(fingerprint, HashValue(prep.autoSafetyEnabled));
                HashCombine(fingerprint, HashValue(prep.overrideMinEv));
                HashCombine(fingerprint, HashValue(prep.overrideMaxEv));
                HashCombine(fingerprint, HashValue(prep.overrideBaseEv));
                HashCombine(fingerprint, HashValue(prep.overrideNoiseProtection));
                HashCombine(fingerprint, HashValue(prep.overrideHighlightProtection));
                HashCombine(fingerprint, HashValue(prep.overrideShadowLiftLimit));
                HashCombine(fingerprint, HashValue(prep.overrideWellExposedTarget));
                HashCombine(fingerprint, HashValue(prep.minEvBias));
                HashCombine(fingerprint, HashValue(prep.maxEvBias));
                HashCombine(fingerprint, HashValue(prep.baseEvBias));
                HashCombine(fingerprint, HashValue(prep.noiseProtectionBias));
                HashCombine(fingerprint, HashValue(prep.highlightProtectionBias));
                HashCombine(fingerprint, HashValue(prep.shadowLiftLimitBias));
                HashCombine(fingerprint, HashValue(prep.wellExposedTargetBias));
                HashCombine(fingerprint, HashValue(prep.minEv));
                HashCombine(fingerprint, HashValue(prep.maxEv));
                HashCombine(fingerprint, HashValue(prep.baseEv));
                HashCombine(fingerprint, HashValue(prep.strength));
                HashCombine(fingerprint, HashValue(prep.sampleCount));
                HashCombine(fingerprint, HashValue(prep.baseRadiusPercent));
                HashCombine(fingerprint, HashValue(prep.highlightProtection));
                HashCombine(fingerprint, HashValue(prep.shadowLiftLimit));
                HashCombine(fingerprint, HashValue(prep.noiseProtection));
                HashCombine(fingerprint, HashValue(prep.detailWeight));
                HashCombine(fingerprint, HashValue(prep.wellExposedTarget));
                HashCombine(fingerprint, HashValue(prep.smoothGradientProtection));
                HashCombine(fingerprint, HashValue(prep.textureSensitivity));
                HashCombine(fingerprint, HashValue(prep.skyBias));
                HashCombine(fingerprint, HashValue(prep.smoothnessRadius));
                HashCombine(fingerprint, HashValue(prep.smoothAreaRadius));
                HashCombine(fingerprint, HashValue(prep.edgeAwareness));
                HashCombine(fingerprint, HashValue(prep.haloGuard));
                HashCombine(fingerprint, HashValue(prep.maskDebandDither));
            }
            const bool preFinishSocket = socketId == EditorNodeGraph::kPreFinishImageOutputSocketId;
            if (!preFinishSocket && !rawBaseSocket) {
                HashCombine(fingerprint, HashValue(node.rawDevelop.integratedToneEnabled));
            }
            if (!preFinishSocket && !rawBaseSocket && node.rawDevelop.integratedToneEnabled) {
                HashCombine(fingerprint, HashValue(node.rawDevelop.integratedToneLayerJson.dump()));
                HashCombine(fingerprint, fingerprintMask(node.nodeId, "maskIn"));
            }
        } else if (node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, fingerprintMask(node.nodeId, "maskOut"));
            const Raw::RawDetailFusionSettings settings = ResolveRawDetailFusionApplySettings(executionContext, node);
            HashCombine(fingerprint, HashValue(settings.autoSafetyEnabled));
            HashCombine(fingerprint, HashValue(settings.overrideMinEv));
            HashCombine(fingerprint, HashValue(settings.overrideMaxEv));
            HashCombine(fingerprint, HashValue(settings.overrideBaseEv));
            HashCombine(fingerprint, HashValue(settings.overrideNoiseProtection));
            HashCombine(fingerprint, HashValue(settings.overrideHighlightProtection));
            HashCombine(fingerprint, HashValue(settings.overrideShadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.overrideWellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.minEvBias));
            HashCombine(fingerprint, HashValue(settings.maxEvBias));
            HashCombine(fingerprint, HashValue(settings.baseEvBias));
            HashCombine(fingerprint, HashValue(settings.noiseProtectionBias));
            HashCombine(fingerprint, HashValue(settings.highlightProtectionBias));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimitBias));
            HashCombine(fingerprint, HashValue(settings.wellExposedTargetBias));
            HashCombine(fingerprint, HashValue(settings.minEv));
            HashCombine(fingerprint, HashValue(settings.maxEv));
            HashCombine(fingerprint, HashValue(settings.baseEv));
            HashCombine(fingerprint, HashValue(settings.noiseProtection));
            HashCombine(fingerprint, HashValue(settings.highlightProtection));
            HashCombine(fingerprint, HashValue(settings.shadowLiftLimit));
            HashCombine(fingerprint, HashValue(settings.wellExposedTarget));
            HashCombine(fingerprint, HashValue(settings.strength));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::HdrMerge) {
            const RenderGraphLink* input1 = findInputLink(node.nodeId, "image1");
            const RenderGraphLink* input2 = findInputLink(node.nodeId, "image2");
            const RenderGraphLink* input3 = findInputLink(node.nodeId, "image3");
            HashCombine(fingerprint, input1 ? fingerprintImage(input1->fromNodeId, input1->fromSocketId) : 0);
            HashCombine(fingerprint, input2 ? fingerprintImage(input2->fromNodeId, input2->fromSocketId) : 0);
            HashCombine(fingerprint, input3 ? fingerprintImage(input3->fromNodeId, input3->fromSocketId) : 0);
            const Raw::HdrMergeSettings& settings = node.hdrMerge.settings;
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.debugView)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.alignmentMode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.exposureMode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.referenceMode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.deghostMode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.motionPriority)));
            for (float exposureEv : settings.manualExposureEv) {
                HashCombine(fingerprint, HashValue(exposureEv));
            }
            for (float exposureEv : settings.exposureOffsetEv) {
                HashCombine(fingerprint, HashValue(exposureEv));
            }
            HashCombine(fingerprint, HashValue(settings.autoReliability));
            HashCombine(fingerprint, HashValue(settings.clipThreshold));
            HashCombine(fingerprint, HashValue(settings.clipFeather));
            HashCombine(fingerprint, HashValue(settings.blackThreshold));
            HashCombine(fingerprint, HashValue(settings.blackFeather));
            HashCombine(fingerprint, HashValue(settings.readNoise));
            HashCombine(fingerprint, HashValue(settings.noiseAware));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::Mfsr) {
            for (int inputIndex = 0; inputIndex < EditorNodeGraph::kMaxMfsrInputCount; ++inputIndex) {
                const std::string inputSocketId = EditorNodeGraph::MfsrInputSocketId(inputIndex);
                const RenderGraphLink* input = findInputLink(node.nodeId, inputSocketId);
                HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            }
            const Stack::Mfsr::MfsrSettings& settings = node.mfsr.settings;
            HashCombine(fingerprint, HashValue(settings.schemaVersion));
            HashCombine(fingerprint, HashValue(settings.algorithmVersion));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.scalePreset)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.qualityPreset)));
            HashCombine(fingerprint, HashValue(settings.preferRawMosaicPath));
            HashCombine(fingerprint, HashValue(settings.maxInputFrames));
        } else if (node.kind == RenderGraphNodeKind::Lut) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, maskLink ? fingerprintMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.lut.importFormat)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.lut.useMode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.lut.inputTransform)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.lut.outputTransform)));
            HashCombine(fingerprint, HashLut1DStage(node.lut.lut1D));
            HashCombine(fingerprint, HashLut1DStage(node.lut.shaper1D));
            HashCombine(fingerprint, HashLut3DStage(node.lut.lut3D));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ImageGenerator) {
            HashCombine(fingerprint, HashValue(static_cast<int>(node.imageGeneratorKind)));
            for (float channel : node.imageGeneratorSettings.colorA) {
                HashCombine(fingerprint, HashValue(channel));
            }
            for (float channel : node.imageGeneratorSettings.colorB) {
                HashCombine(fingerprint, HashValue(channel));
            }
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.angle));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.offset));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.text));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.fontSize));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.textBackdropBlur));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.textBackdropOpacity));
            HashCombine(fingerprint, HashValue(node.imageGeneratorSettings.textBackdropPadding));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::Layer) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, maskLink ? fingerprintMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0);
            HashCombine(fingerprint, HashJson(node.layerJson));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::Mix) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
            const RenderGraphLink* factorLink = findInputLink(node.nodeId, "factor");
            HashCombine(fingerprint, inputA ? fingerprintImage(inputA->fromNodeId, inputA->fromSocketId) : 0);
            HashCombine(fingerprint, inputB ? fingerprintImage(inputB->fromNodeId, inputB->fromSocketId) : 0);
            HashCombine(fingerprint, factorLink ? fingerprintMask(factorLink->fromNodeId, factorLink->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.mixBlendMode)));
            HashCombine(fingerprint, HashValue(node.mixFactor));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::DataMath) {
            for (const DataMathInputLinkInfo& input : CollectDataMathAverageInputs(executionContext, node.nodeId)) {
                const bool scalarInput = IsScalarRenderSocket(executionContext, input.link->fromNodeId, input.link->fromSocketId);
                HashCombine(fingerprint, HashValue(input.socketId));
                HashCombine(
                    fingerprint,
                    scalarInput
                        ? fingerprintMask(input.link->fromNodeId, input.link->fromSocketId)
                        : fingerprintImage(input.link->fromNodeId, input.link->fromSocketId));
                HashCombine(fingerprint, HashValue(scalarInput));
            }
            if (const RenderGraphLink* baseInput = findInputLink(node.nodeId, EditorNodeGraph::kDataMathBaseInputSocketId)) {
                const bool scalarBase = IsScalarRenderSocket(executionContext, baseInput->fromNodeId, baseInput->fromSocketId);
                HashCombine(fingerprint, HashValue(std::string(EditorNodeGraph::kDataMathBaseInputSocketId)));
                HashCombine(
                    fingerprint,
                    scalarBase
                        ? fingerprintMask(baseInput->fromNodeId, baseInput->fromSocketId)
                        : fingerprintImage(baseInput->fromNodeId, baseInput->fromSocketId));
                HashCombine(fingerprint, HashValue(scalarBase));
            } else {
                HashCombine(fingerprint, HashValue(false));
            }
            if (const RenderGraphLink* maskInput = findInputLink(node.nodeId, EditorNodeGraph::kMaskInputSocketId)) {
                HashCombine(fingerprint, HashValue(std::string(EditorNodeGraph::kMaskInputSocketId)));
                HashCombine(fingerprint, fingerprintMask(maskInput->fromNodeId, maskInput->fromSocketId));
            } else {
                HashCombine(fingerprint, std::size_t{ 0 });
            }
            HashCombine(fingerprint, HashValue(IsScalarRenderSocket(executionContext, node.nodeId, socketId)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.dataMathMode)));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.constantA));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.constantB));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.minValue));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.maxValue));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.outMin));
            HashCombine(fingerprint, HashValue(node.dataMathSettings.outMax));
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::Output) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            if (input) {
                HashCombine(fingerprint, fingerprintImage(input->fromNodeId, input->fromSocketId));
            } else {
                const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
                const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
                const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
                const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");
                HashCombine(fingerprint, linkR ? fingerprintMask(linkR->fromNodeId, linkR->fromSocketId) : 0);
                HashCombine(fingerprint, linkG ? fingerprintMask(linkG->fromNodeId, linkG->fromSocketId) : 0);
                HashCombine(fingerprint, linkB ? fingerprintMask(linkB->fromNodeId, linkB->fromSocketId) : 0);
                HashCombine(fingerprint, linkA ? fingerprintMask(linkA->fromNodeId, linkA->fromSocketId) : 0);
            }
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        } else if (node.kind == RenderGraphNodeKind::ChannelCombine) {
            const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
            const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
            const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
            const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");
            HashCombine(fingerprint, linkR ? fingerprintMask(linkR->fromNodeId, linkR->fromSocketId) : 0);
            HashCombine(fingerprint, linkG ? fingerprintMask(linkG->fromNodeId, linkG->fromSocketId) : 0);
            HashCombine(fingerprint, linkB ? fingerprintMask(linkB->fromNodeId, linkB->fromSocketId) : 0);
            HashCombine(fingerprint, linkA ? fingerprintMask(linkA->fromNodeId, linkA->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(m_Width));
            HashCombine(fingerprint, HashValue(m_Height));
        }

        fingerprintingImages.erase(key);
        imageFingerprintCache[key] = fingerprint;
        return fingerprint;
    };

    evalMask = [&](int nodeId, const std::string& socketId) -> unsigned int {
        std::string key = std::to_string(nodeId) + ":" + socketId;
        if (maskCache.count(key)) {
            return maskCache[key];
        }
        if (visitingMasks.count(key)) {
            return 0;
        }
        visitingMasks.insert(key);
        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            visitingMasks.erase(key);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        if (node.kind == RenderGraphNodeKind::Image ||
            node.kind == RenderGraphNodeKind::RawDevelopment ||
            node.kind == RenderGraphNodeKind::RawNeuralDenoise ||
            node.kind == RenderGraphNodeKind::RawDecode ||
            node.kind == RenderGraphNodeKind::RawDevelop ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId != "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId != "maskOut") ||
            node.kind == RenderGraphNodeKind::HdrMerge ||
            node.kind == RenderGraphNodeKind::Mfsr ||
            node.kind == RenderGraphNodeKind::Lut ||
            node.kind == RenderGraphNodeKind::Layer ||
            node.kind == RenderGraphNodeKind::Mix ||
            (node.kind == RenderGraphNodeKind::DataMath && !IsScalarRenderSocket(executionContext, nodeId, socketId)) ||
            node.kind == RenderGraphNodeKind::ImageGenerator ||
            node.kind == RenderGraphNodeKind::ChannelCombine ||
            node.kind == RenderGraphNodeKind::Output) {
            unsigned int imgTex = evalImage(nodeId, socketId);
            visitingMasks.erase(key);
            return imgTex;
        }
        const std::size_t fingerprint = fingerprintMask(nodeId, socketId);
        if (const auto cached = m_GraphMaskCache.find(key);
            cached != m_GraphMaskCache.end() &&
            cached->second.fingerprint == fingerprint &&
            cached->second.texture != 0) {
            ++m_LastGraphExecutionStats.maskCacheHits;
            maskCache[key] = cached->second.texture;
            visitingMasks.erase(key);
            return cached->second.texture;
        }
        ++m_LastGraphExecutionStats.maskCacheMisses;

        unsigned int result = 0;
        bool resultOwned = false;
        if (node.kind == RenderGraphNodeKind::MaskGenerator) {
            RenderMaskSource mask;
            mask.nodeId = node.nodeId;
            mask.kind = node.maskKind;
            mask.settings = node.maskSettings;
            result = GenerateMaskTexture(mask);
            resultOwned = result != 0;
        } else if (node.kind == RenderGraphNodeKind::MaskCombine) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "maskA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "maskB");
            const unsigned int maskA = inputA ? evalMask(inputA->fromNodeId, inputA->fromSocketId) : 0;
            const unsigned int maskB = inputB ? evalMask(inputB->fromNodeId, inputB->fromSocketId) : 0;
            if (maskA && maskB) {
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderMaskCombine(maskA, maskB, node.maskCombineMode, fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::CustomMask) {
            result = GenerateCustomMaskTexture(node.customMask);
            resultOwned = result != 0;
        } else if (node.kind == RenderGraphNodeKind::MaskUtility) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "maskIn");
            const unsigned int inputMask = input ? evalMask(input->fromNodeId, input->fromSocketId) : 0;
            if (inputMask) {
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderMaskUtility(inputMask, node, fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::ImageToMask) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            if (inputImage) {
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderImageToMask(inputImage, node, fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::ChannelSplit) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            if (inputImage) {
                result = createTarget();
                int channelIdx = 0;
                if (socketId == "g") channelIdx = 1;
                else if (socketId == "b") channelIdx = 2;
                else if (socketId == "a") channelIdx = 3;
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderChannelSplit(inputImage, channelIdx, fbo);
                });
                resultOwned = result != 0;
            } else {
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    GLfloat previousClearColor[4];
                    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
                    if (socketId == "a") {
                        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
                    } else {
                        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    }
                    glClear(GL_COLOR_BUFFER_BIT);
                    glClearColor(
                        previousClearColor[0],
                        previousClearColor[1],
                        previousClearColor[2],
                        previousClearColor[3]);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::DataMath) {
            const GraphNodeRenderResult dataMathResult =
                RenderDataMathGraphNode(executionContext, node, socketId, evalImage, evalMask);
            result = dataMathResult.texture;
            resultOwned = dataMathResult.owned;
        } else if (node.kind == RenderGraphNodeKind::RawDetailAutoMask ||
                   node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const GraphNodeRenderResult rawDetailResult =
                RenderRawDetailGraphNode(executionContext, node, socketId, evalImage, evalMask);
            result = rawDetailResult.texture;
            resultOwned = rawDetailResult.owned;
        }

        if (result) {
            maskCache[key] = result;
            StoreGraphCacheEntry(m_GraphMaskCache, key, result, fingerprint, resultOwned);
        } else {
            ReleaseGraphCacheEntry(m_GraphMaskCache, key);
        }
        visitingMasks.erase(key);
        return result;
    };

    evalImage = [&](int nodeId, const std::string& socketId) -> unsigned int {
        std::string key = std::to_string(nodeId) + ":" + socketId;
        if (imageCache.count(key)) {
            return imageCache[key];
        }
        if (visitingImages.count(key)) {
            return 0;
        }
        visitingImages.insert(key);

        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            visitingImages.erase(key);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        if (node.kind == RenderGraphNodeKind::MaskGenerator ||
            node.kind == RenderGraphNodeKind::MaskCombine ||
            node.kind == RenderGraphNodeKind::MaskUtility ||
            node.kind == RenderGraphNodeKind::CustomMask ||
            node.kind == RenderGraphNodeKind::ImageToMask ||
            node.kind == RenderGraphNodeKind::ChannelSplit ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId == "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId == "maskOut")) {
            unsigned int maskTex = evalMask(nodeId, socketId);
            visitingImages.erase(key);
            return maskTex;
        }
        const std::size_t fingerprint = fingerprintImage(nodeId, socketId);
        const bool rawDevelopStageSocket =
            node.kind == RenderGraphNodeKind::RawDevelop &&
            (socketId == "__rawDevelopBase" ||
             socketId == EditorNodeGraph::kPreFinishImageOutputSocketId);
        const bool rawBorrowedResultNeedsOwnedCache =
            node.kind == RenderGraphNodeKind::RawDevelopment ||
            node.kind == RenderGraphNodeKind::RawDecode ||
            node.kind == RenderGraphNodeKind::RawDevelop;
        const bool rawDevelopmentOverlayActive =
            node.kind == RenderGraphNodeKind::RawDevelopment &&
            !graph.rawWorkspaceLocalRangeOverlayMode.empty() &&
            graph.rawWorkspaceLocalRangeOverlayMode != "none";
        if (!rawDevelopStageSocket && !rawDevelopmentOverlayActive) {
            if (const auto cached = m_GraphImageCache.find(key);
                cached != m_GraphImageCache.end() &&
                cached->second.fingerprint == fingerprint &&
                cached->second.texture != 0) {
                ++m_LastGraphExecutionStats.imageCacheHits;
                if (cached->second.width > 0 && cached->second.height > 0) {
                    m_Width = cached->second.width;
                    m_Height = cached->second.height;
                }
                imageCache[key] = cached->second.texture;
                m_LastGraphImageCacheHits.insert(key);
                visitingImages.erase(key);
                return cached->second.texture;
            }
        }
        ++m_LastGraphExecutionStats.imageCacheMisses;

        unsigned int result = 0;
        bool resultOwned = false;
        if (node.kind == RenderGraphNodeKind::Image) {
            if (!node.image.pixels.empty() && node.image.width > 0 && node.image.height > 0) {
                result = GLHelpers::CreateTextureFromPixels(
                    node.image.pixels.data(),
                    node.image.width,
                    node.image.height,
                    node.image.channels);
                resultOwned = result != 0;
            } else {
                result = m_SourceTexture;
            }
        } else if (node.kind == RenderGraphNodeKind::RawSource) {
            result = 0;
        } else if (node.kind == RenderGraphNodeKind::RawDevelopment) {
            const GraphNodeRenderResult rawDevelopmentResult =
                RenderRawDevelopmentGraphNode(node, fingerprint);
            result = rawDevelopmentResult.texture;
            resultOwned = rawDevelopmentResult.owned;
        } else if (node.kind == RenderGraphNodeKind::RawNeuralDenoise) {
            result = 0;
        } else if (node.kind == RenderGraphNodeKind::RawDecode) {
            const std::string rawBaseKey = std::to_string(node.nodeId) + ":__rawDecodeBase";
            const std::size_t rawBaseFingerprint = fingerprintImage(node.nodeId, "__rawDecodeBase");
            const SharedRawBaseStageResult rawBaseStage =
                RenderSharedRawBaseStage(executionContext, node, node.rawDecode.settings, rawBaseKey, rawBaseFingerprint, fingerprintImage);
            result = rawBaseStage.texture;
            resultOwned = false;
        } else if (node.kind == RenderGraphNodeKind::RawDevelop) {
            const GraphNodeRenderResult rawDevelopResult = RenderRawDevelopGraphNode(
                executionContext,
                node,
                socketId,
                fingerprint,
                imageCache,
                evalImage,
                evalMask,
                fingerprintImage);
            result = rawDevelopResult.texture;
            resultOwned = rawDevelopResult.owned;
        } else if (node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const GraphNodeRenderResult rawDetailResult =
                RenderRawDetailGraphNode(executionContext, node, socketId, evalImage, evalMask);
            result = rawDetailResult.texture;
            resultOwned = rawDetailResult.owned;
        } else if (node.kind == RenderGraphNodeKind::HdrMerge) {
            const RenderGraphLink* input1 = findInputLink(node.nodeId, "image1");
            const RenderGraphLink* input2 = findInputLink(node.nodeId, "image2");
            const RenderGraphLink* input3 = findInputLink(node.nodeId, "image3");
            const unsigned int texture1 = input1 ? evalImage(input1->fromNodeId, input1->fromSocketId) : 0;
            const unsigned int texture2 = input2 ? evalImage(input2->fromNodeId, input2->fromSocketId) : 0;
            const unsigned int texture3 = input3 ? evalImage(input3->fromNodeId, input3->fromSocketId) : 0;
            const bool hasGap = input3 != nullptr && input2 == nullptr;
            const bool hasRequiredInputs = texture1 != 0 &&
                texture2 != 0 &&
                (input3 == nullptr || texture3 != 0);
            if (!hasGap && hasRequiredInputs) {
                std::array<bool, 3> activeInputs { input1 != nullptr, input2 != nullptr, input3 != nullptr };
                std::array<HdrMergeInputContext, 3> inputContexts {};
                if (input1) inputContexts[0] = ResolveHdrMergeInputContext(executionContext, input1->fromNodeId);
                if (input2) inputContexts[1] = ResolveHdrMergeInputContext(executionContext, input2->fromNodeId);
                if (input3) inputContexts[2] = ResolveHdrMergeInputContext(executionContext, input3->fromNodeId);
                const HdrMergeResolvedSettings resolved = ResolveHdrMergeSettings(node.hdrMerge.settings, inputContexts, activeInputs);
                result = createTarget();
                bool mergeRendered = false;
                renderToTexture(result, [&](unsigned int fbo) {
                    mergeRendered = RenderHdrMerge(
                        texture1,
                        texture2,
                        texture3,
                        input2 != nullptr,
                        input3 != nullptr,
                        node.hdrMerge.settings,
                        resolved,
                        fbo);
                });
                if (!mergeRendered && result != 0) {
                    glDeleteTextures(1, &result);
                    result = 0;
                }
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::Mfsr) {
            const RenderGraphLink* reference = findInputLink(node.nodeId, EditorNodeGraph::kMfsrReferenceInputSocketId);
            if (reference) {
                result = evalImage(reference->fromNodeId, reference->fromSocketId);
                resultOwned = false;
            }
        } else if (node.kind == RenderGraphNodeKind::Lut) {
            const GraphNodeRenderResult lutResult = RenderLutGraphNode(executionContext, node, evalImage, evalMask);
            result = lutResult.texture;
            resultOwned = lutResult.owned;
        } else if (node.kind == RenderGraphNodeKind::ImageGenerator) {
            result = GenerateImageTexture(node);
            resultOwned = result != 0;
        } else if (node.kind == RenderGraphNodeKind::Layer) {
            const GraphNodeRenderResult layerResult = RenderLayerGraphNode(executionContext, node, evalImage, evalMask);
            result = layerResult.texture;
            resultOwned = layerResult.owned;
        } else if (node.kind == RenderGraphNodeKind::Mix) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
            const unsigned int textureA = inputA ? evalImage(inputA->fromNodeId, inputA->fromSocketId) : 0;
            const unsigned int textureB = inputB ? evalImage(inputB->fromNodeId, inputB->fromSocketId) : 0;
            if (textureA && textureB) {
                const RenderGraphLink* factorLink = findInputLink(node.nodeId, "factor");
                const unsigned int factorTexture = factorLink ? evalMask(factorLink->fromNodeId, factorLink->fromSocketId) : 0;
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderMixBlend(textureA, textureB, factorTexture, node.mixFactor, node.mixBlendMode, fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::DataMath) {
            const GraphNodeRenderResult dataMathResult =
                RenderDataMathGraphNode(executionContext, node, socketId, evalImage, evalMask);
            result = dataMathResult.texture;
            resultOwned = dataMathResult.owned;
        } else if (node.kind == RenderGraphNodeKind::ChannelCombine) {
            const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
            const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
            const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
            const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");

            const unsigned int texR = linkR ? evalMask(linkR->fromNodeId, linkR->fromSocketId) : 0;
            const unsigned int texG = linkG ? evalMask(linkG->fromNodeId, linkG->fromSocketId) : 0;
            const unsigned int texB = linkB ? evalMask(linkB->fromNodeId, linkB->fromSocketId) : 0;
            const unsigned int texA = linkA ? evalMask(linkA->fromNodeId, linkA->fromSocketId) : 0;

            result = createTarget();
            renderToTexture(result, [&](unsigned int fbo) {
                RenderChannelCombine(texR, texG, texB, texA,
                                     linkR != nullptr, linkG != nullptr, linkB != nullptr, linkA != nullptr,
                                     fbo);
            });
            resultOwned = result != 0;
        } else if (node.kind == RenderGraphNodeKind::Output) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            if (input) {
                result = evalImage(input->fromNodeId, input->fromSocketId);
            } else {
                const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
                const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
                const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
                const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");

                const unsigned int texR = linkR ? evalMask(linkR->fromNodeId, linkR->fromSocketId) : 0;
                const unsigned int texG = linkG ? evalMask(linkG->fromNodeId, linkG->fromSocketId) : 0;
                const unsigned int texB = linkB ? evalMask(linkB->fromNodeId, linkB->fromSocketId) : 0;
                const unsigned int texA = linkA ? evalMask(linkA->fromNodeId, linkA->fromSocketId) : 0;

                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderChannelCombine(texR, texG, texB, texA,
                                         linkR != nullptr, linkG != nullptr, linkB != nullptr, linkA != nullptr,
                                         fbo);
                });
                resultOwned = result != 0;
            }
        }

        if (result) {
            if (!resultOwned && rawBorrowedResultNeedsOwnedCache) {
                const unsigned int ownedCopy = CloneTextureForGraphCache(result, m_Width, m_Height);
                if (ownedCopy != 0) {
                    result = ownedCopy;
                    resultOwned = true;
                } else {
                    ReleaseGraphCacheEntry(m_GraphImageCache, key);
                    imageCache[key] = result;
                    visitingImages.erase(key);
                    return result;
                }
            }
            imageCache[key] = result;
            StoreGraphCacheEntry(m_GraphImageCache, key, result, fingerprint, resultOwned);
        } else {
            ReleaseGraphCacheEntry(m_GraphImageCache, key);
        }
        visitingImages.erase(key);
        return result;
    };

    unsigned int finalTexture = 0;
    const auto outputIt = nodes.find(graph.outputNodeId);
    if (outputIt != nodes.end() &&
        (outputIt->second->kind == RenderGraphNodeKind::MaskGenerator ||
         outputIt->second->kind == RenderGraphNodeKind::MaskUtility ||
         outputIt->second->kind == RenderGraphNodeKind::ImageToMask ||
         outputIt->second->kind == RenderGraphNodeKind::MaskCombine ||
         outputIt->second->kind == RenderGraphNodeKind::CustomMask ||
         outputIt->second->kind == RenderGraphNodeKind::ChannelSplit ||
         (outputIt->second->kind == RenderGraphNodeKind::DataMath && IsScalarRenderSocket(executionContext, graph.outputNodeId, graph.outputSocketId)) ||
         (outputIt->second->kind == RenderGraphNodeKind::RawDetailAutoMask && graph.outputSocketId == "maskOut") ||
         (outputIt->second->kind == RenderGraphNodeKind::RawDetailFusion && graph.outputSocketId == "maskOut"))) {
        finalTexture = evalMask(graph.outputNodeId, graph.outputSocketId);
    } else {
        finalTexture = evalImage(graph.outputNodeId, graph.outputSocketId);
    }
    m_OutputTexture = finalTexture ? finalTexture : 0;
    m_GraphSourceTexture = 0;
    const int referenceSourceNodeId = FindReferenceSourceNode(executionContext, graph.outputNodeId);
    if (referenceSourceNodeId > 0) {
        const auto referenceIt = nodes.find(referenceSourceNodeId);
        if (referenceIt != nodes.end() &&
            referenceIt->second &&
            referenceIt->second->kind == RenderGraphNodeKind::RawSource) {
            m_GraphSourceTexture = m_SourceTexture;
        } else {
            m_GraphSourceTexture = evalImage(referenceSourceNodeId, "imageOut");
        }
    }

    PruneInactiveGraphCache(m_GraphImageCache, executionContext);
    PruneInactiveGraphCache(m_GraphMaskCache, executionContext);
    PruneInactiveLutTextureCache(executionContext);
    PruneInactiveRawDevelopStageCache(executionContext);

    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    if (prevScissor) glEnable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST);
    if (prevStencil) glEnable(GL_STENCIL_TEST);
    if (prevBlend) glEnable(GL_BLEND);
}
