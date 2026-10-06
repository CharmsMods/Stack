#include "Renderer/Internal/RenderPipelineGraphExecutionRuntime.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "NodeMath/ReductionMath.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Utils/PixelBufferUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Stack::Renderer::GraphExecution {

void GraphExecutionRuntime::BindMaskFingerprint() {
    fingerprintMask = [this](int nodeId, const std::string& socketId) -> std::size_t {
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
            node.kind == RenderGraphNodeKind::RawProjectSourceSet ||
            node.kind == RenderGraphNodeKind::Lut ||
            node.kind == RenderGraphNodeKind::RawOperation ||
            node.kind == RenderGraphNodeKind::Layer ||
            node.kind == RenderGraphNodeKind::Mix ||
            node.kind == RenderGraphNodeKind::TechnicalImage ||
            node.kind == RenderGraphNodeKind::Reformat ||
            (node.kind == RenderGraphNodeKind::DataMath && !IsScalarRenderSocket(executionContext, nodeId, socketId)) ||
            node.kind == RenderGraphNodeKind::FrequencyFft ||
            node.kind == RenderGraphNodeKind::SpectrumView ||
            node.kind == RenderGraphNodeKind::SpectrumMath ||
            (node.kind == RenderGraphNodeKind::MagnitudePhase && socketId != EditorNodeGraph::kMaskOutputSocketId) ||
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
        HashCombine(fingerprint, HashValue(node.definitionId));
        HashCombine(fingerprint, HashValue(node.definitionVersion));
        HashCombine(fingerprint, HashValue(node.definitionHash));
        HashCombine(fingerprint, HashValue(socketId));
        HashCombine(fingerprint, HashValue(node.semanticDescriptorIdentity));

        if (node.kind == RenderGraphNodeKind::MaskGenerator) {
            if (const auto* extent = findInputLink(nodeId, EditorNodeGraph::kMatchExtentInputSocketId))
                HashCombine(fingerprint, fingerprintImage(extent->fromNodeId, extent->fromSocketId));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.maskKind)));
            HashCombine(fingerprint, HashJson(node.rawCoverage));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.rawWorkingSpace)));
            HashCombine(fingerprint, HashValue(node.maskSettings.value));
            HashCombine(fingerprint, HashValue(node.maskSettings.angle));
            HashCombine(fingerprint, HashValue(node.maskSettings.offset));
            HashCombine(fingerprint, HashValue(node.maskSettings.scale));
            HashCombine(fingerprint, HashValue(node.maskSettings.centerX));
            HashCombine(fingerprint, HashValue(node.maskSettings.centerY));
            HashCombine(fingerprint, HashValue(node.maskSettings.radius));
            HashCombine(fingerprint, HashValue(node.maskSettings.radiusY));
            HashCombine(fingerprint, HashValue(node.maskSettings.feather));
            HashCombine(fingerprint, HashValue(node.maskSettings.invert));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind ==
                   RenderGraphNodeKind::ConstantChannel) {
            HashCombine(
                fingerprint,
                HashValue(node.constantChannelValue));
            const RenderGraphLink* extentInput =
                findInputLink(
                    node.nodeId,
                    EditorNodeGraph::kMatchExtentInputSocketId);
            HashCombine(
                fingerprint,
                extentInput
                    ? fingerprintMask(
                        extentInput->fromNodeId,
                        extentInput->fromSocketId)
                    : 0);
        } else if (node.kind == RenderGraphNodeKind::MaskCombine) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "maskA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "maskB");
            HashCombine(fingerprint, inputA ? fingerprintMask(inputA->fromNodeId, inputA->fromSocketId) : 0);
            HashCombine(fingerprint, inputB ? fingerprintMask(inputB->fromNodeId, inputB->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.maskCombineMode)));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
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
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind == RenderGraphNodeKind::MaskUtility) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, input ? fingerprintMask(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.maskUtilityKind)));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.blackPoint));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.whitePoint));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.gamma));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.threshold));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.softness));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.enabled));
            HashCombine(fingerprint, HashValue(node.maskUtilitySettings.invert));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
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
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind == RenderGraphNodeKind::ChannelSplit) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
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
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind == RenderGraphNodeKind::FrequencyMask) {
            const RenderFrequencyMaskSettings& settings = node.frequencyMaskSettings;
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.shape)));
            HashCombine(fingerprint, HashValue(settings.cutoff));
            HashCombine(fingerprint, HashValue(settings.width));
            HashCombine(fingerprint, HashValue(settings.feather));
            HashCombine(fingerprint, HashValue(settings.order));
            HashCombine(fingerprint, HashValue(settings.centerX));
            HashCombine(fingerprint, HashValue(settings.centerY));
            HashCombine(fingerprint, HashValue(settings.invert));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind == RenderGraphNodeKind::MagnitudePhase) {
            const RenderGraphLink* input = findInputLink(node.nodeId, EditorNodeGraph::kImageInputSocketId);
            HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.magnitudePhaseMode)));
            HashCombine(fingerprint, HashValue(node.magnitudePhaseSettings.exposure));
            HashCombine(fingerprint, HashValue(node.magnitudePhaseSettings.gamma));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
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
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
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
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind == RenderGraphNodeKind::FrequencyFilter) {
            const RenderGraphLink* channel = findInputLink(
                node.nodeId, EditorNodeGraph::kChannelInputSocketId);
            const RenderGraphLink* response = findInputLink(
                node.nodeId, EditorNodeGraph::kFrequencyResponseInputSocketId);
            const RenderGraphLink* strength = findInputLink(
                node.nodeId,
                EditorNodeGraph::ParameterInputSocketId(
                    EditorNodeGraph::kStrengthParameterId));
            HashCombine(fingerprint, channel
                ? fingerprintMask(channel->fromNodeId, channel->fromSocketId) : 0);
            HashCombine(fingerprint, response
                ? fingerprintResponse(response->fromNodeId)
                : HashValue(static_cast<int>(node.frequencyFilterSettings.localResponse.mode)));
            if (!response) {
                const auto& settings = node.frequencyFilterSettings.localResponse;
                HashCombine(fingerprint, HashValue(static_cast<int>(settings.profile)));
                HashCombine(fingerprint, HashValue(settings.lowCutoff));
                HashCombine(fingerprint, HashValue(settings.highCutoff));
                HashCombine(fingerprint, HashValue(settings.transitionWidth));
                HashCombine(fingerprint, HashValue(settings.butterworthOrder));
                for (const RenderFrequencyNotch& notch : settings.notches) {
                    HashCombine(fingerprint, HashValue(notch.id));
                    HashCombine(fingerprint, HashValue(notch.frequency));
                    HashCombine(fingerprint, HashValue(notch.directionDegrees));
                    HashCombine(fingerprint, HashValue(notch.width));
                }
            }
            HashCombine(fingerprint, strength
                ? fingerprintScalar(strength->fromNodeId, strength->fromSocketId)
                : HashValue(node.frequencyFilterSettings.strength));
            HashCombine(fingerprint, HashValue(
                static_cast<int>(node.frequencyFilterSettings.edgePolicy)));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind == RenderGraphNodeKind::FrequencyIfft) {
            const RenderGraphLink* spectrum = findInputLink(
                node.nodeId, EditorNodeGraph::kSpectrumInputSocketId);
            HashCombine(fingerprint, spectrum
                ? fingerprintFrequency(spectrum->fromNodeId, spectrum->fromSocketId) : 0);
        }

        fingerprintingMasks.erase(key);
        maskFingerprintCache[key] = fingerprint;
        return fingerprint;
    };

}

} // namespace Stack::Renderer::GraphExecution
