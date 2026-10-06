#include "Renderer/Internal/RenderPipelineGraphExecutionRuntime.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "NodeMath/ReductionMath.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawGraphParameters.h"
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

void GraphExecutionRuntime::BindImageFingerprint() {
    fingerprintImage = [this](int nodeId, const std::string& socketId) -> std::size_t {
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
        if (node.kind == RenderGraphNodeKind::RawOperation &&
            (socketId == "inputImageOut" || socketId == "measurementImageOut")) {
            const auto* input = socketId == "measurementImageOut" ? findInputLink(nodeId, "referenceIn") : nullptr;
            if (!input) input = findInputLink(nodeId, "imageIn");
            const auto value = input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0;
            fingerprintingImages.erase(key);
            imageFingerprintCache[key] = value;
            return value;
        }
        if (node.kind == RenderGraphNodeKind::MaskGenerator ||
            node.kind == RenderGraphNodeKind::MaskCombine ||
            node.kind == RenderGraphNodeKind::MaskUtility ||
            node.kind == RenderGraphNodeKind::CustomMask ||
            node.kind == RenderGraphNodeKind::ImageToMask ||
            node.kind == RenderGraphNodeKind::ChannelSplit ||
            node.kind == RenderGraphNodeKind::ConstantChannel ||
            node.kind == RenderGraphNodeKind::FrequencyFilter ||
            node.kind == RenderGraphNodeKind::FrequencyIfft ||
            node.kind == RenderGraphNodeKind::FrequencyMask ||
            (node.kind == RenderGraphNodeKind::MagnitudePhase && socketId == EditorNodeGraph::kMaskOutputSocketId) ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId == "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId == "maskOut")) {
            std::size_t maskFp = fingerprintMask(nodeId, socketId);
            fingerprintingImages.erase(key);
            imageFingerprintCache[key] = maskFp;
            return maskFp;
        }
        std::size_t fingerprint = HashValue(static_cast<int>(node.kind));
        HashCombine(fingerprint, HashValue(node.nodeId));
        HashCombine(fingerprint, HashValue(node.definitionId));
        HashCombine(fingerprint, HashValue(node.definitionVersion));
        HashCombine(fingerprint, HashValue(node.definitionHash));
        HashCombine(fingerprint, HashValue(socketId));
        HashCombine(fingerprint, HashValue(node.semanticDescriptorIdentity));
        auto hashRawDevelopSettings = [&](const Raw::RawDevelopSettings& settings) {
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.processingVersion)));
            HashCombine(fingerprint, HashValue(static_cast<int>(settings.workingSpace)));
            HashCombine(fingerprint, HashValue(settings.applyBaselineExposure));
            HashCombine(fingerprint, HashValue(settings.encodeSrgbOutput));
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
            HashCombine(
                fingerprint,
                HashValue(static_cast<int>(settings.mosaicDenoise.mode)));
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
                HashCombine(fingerprint, pipeline.m_SourceFingerprint);
                HashCombine(fingerprint, HashValue(pipeline.m_BaseCanvasWidth));
                HashCombine(fingerprint, HashValue(pipeline.m_BaseCanvasHeight));
                HashCombine(fingerprint, HashValue(pipeline.m_SourceChannels));
            }
        } else if (node.kind == RenderGraphNodeKind::RawSource) {
            HashCombine(fingerprint, HashValue(node.rawSource.sourcePath));
            const bool hasEmbeddedRaw =
                !node.rawSource.embeddedRawData.rawBuffer.empty() ||
                !node.rawSource.embeddedRawData.linearUInt16Buffer.empty() ||
                !node.rawSource.embeddedRawData.linearFloatBuffer.empty() ||
                (node.rawSource.embeddedRawData.normalizedMosaicBuffer &&
                 !node.rawSource.embeddedRawData
                      .normalizedMosaicBuffer->empty());
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
                HashCombine(
                    fingerprint,
                    HashValue(embedded.normalizedMosaicContentHash));
                HashCombine(
                    fingerprint,
                    HashValue(
                        embedded.normalizedMosaicBuffer
                            ? embedded.normalizedMosaicBuffer->size()
                            : 0u));
            }
            hashRawMetadata(node.rawSource.metadata);
        } else if (node.kind == RenderGraphNodeKind::RawDevelopment) {
            HashCombine(fingerprint, HashValue(Stack::RawRecipe::SerializeRecipe(node.rawDevelopment.recipe).dump()));
            HashCombine(fingerprint, pipeline.m_RawViewportRequest.visible.Fingerprint());
            HashCombine(fingerprint, static_cast<std::size_t>(pipeline.m_RawViewportRequest.generation));
            HashCombine(fingerprint, HashValue(pipeline.m_PreviewMaxDimension));
            HashCombine(fingerprint, HashValue(pipeline.m_RawDevelopmentAnalysisEnabled));
            HashCombine(fingerprint, HashValue(pipeline.m_RawDevelopmentStageImageReadbackMaxDimension));
            HashCombine(fingerprint, HashValue(static_cast<int>(pipeline.m_RawDevelopmentGraphScopeStage)));
            HashCombine(fingerprint, HashValue(pipeline.m_RawDevelopmentGraphScopeReadbackMaxDimension));
            HashCombine(
                fingerprint,
                HashValue(static_cast<int>(pipeline.m_RawDevelopmentGradingScopeSource)));
            HashCombine(
                fingerprint,
                HashValue(pipeline.m_RawDevelopmentGradingScopeReadbackMaxDimension));
            HashCombine(fingerprint, HashValue(
                pipeline.m_RawDevelopmentGradingScopeSource == RawDevelopmentGradingScopeSource::None
                    ? std::uint64_t(0) : pipeline.m_RawDevelopmentGradingScopeRequestRevision));
        } else if (node.kind == RenderGraphNodeKind::RawNeuralDenoise) {
            const RenderGraphLink* rawInput = findInputLink(node.nodeId, "rawIn");
            HashCombine(fingerprint, rawInput ? fingerprintImage(rawInput->fromNodeId, rawInput->fromSocketId) : 0);
            HashCombine(fingerprint, HashJson(NeuralDenoise::SerializeSettings(node.rawNeuralDenoise.settings)));
        } else if (node.kind == RenderGraphNodeKind::RawDecode) {
            const RenderGraphLink* rawInput = findInputLink(node.nodeId, "rawIn");
            HashCombine(fingerprint, rawInput ? fingerprintImage(rawInput->fromNodeId, rawInput->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(pipeline.m_PreviewMaxDimension));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
            hashRawDevelopSettings(node.rawDecode.settings);
        } else if (node.kind == RenderGraphNodeKind::RawDevelop) {
            const RenderGraphLink* rawInput = findInputLink(node.nodeId, "rawIn");
            HashCombine(fingerprint, rawInput ? fingerprintImage(rawInput->fromNodeId, rawInput->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(pipeline.m_PreviewMaxDimension));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
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
            const Raw::RawDetailFusionSettings settings = pipeline.ResolveRawDetailFusionApplySettings(executionContext, node);
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
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
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
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
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
        } else if (node.kind == RenderGraphNodeKind::RawProjectSourceSet) {
            HashCombine(fingerprint, pipeline.m_RawViewportRequest.visible.Fingerprint());
            HashCombine(fingerprint, static_cast<std::size_t>(pipeline.m_RawViewportRequest.generation));
            HashCombine(fingerprint, HashValue(node.rawProjectSourceSet.sourceSetId));
            HashCombine(fingerprint, HashValue(node.rawProjectSourceSet.unavailableStatus));
            HashCombine(fingerprint, HashValue(
                node.rawProjectSourceSet.inputRevision));
            HashCombine(fingerprint, HashValue(
                node.rawProjectSourceSet.postRecipeRevision));
            HashCombine(fingerprint, HashValue(
                node.rawProjectSourceSet.contentHash));
            HashCombine(fingerprint, HashValue(
                node.rawProjectSourceSet.resultAvailable));
            if (node.rawProjectSourceSet.resultAvailable) {
                HashCombine(
                    fingerprint,
                    HashValue(Stack::RawRecipe::SerializeRecipe(
                        node.rawDevelopment.recipe).dump()));
                HashCombine(fingerprint, HashValue(pipeline.m_PreviewMaxDimension));
                HashCombine(
                    fingerprint,
                    HashValue(pipeline.m_RawDevelopmentAnalysisEnabled));
            }
            HashCombine(fingerprint, HashValue(node.rawProjectSourceSet.quarantined));
        } else if (node.kind == RenderGraphNodeKind::Lut) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, maskLink ? fingerprintMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.lut.importFormat)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.lut.useMode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.lut.inputTransform)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.lut.outputTransform)));
            HashCombine(fingerprint, pipeline.HashLut1DStage(node.lut.lut1D));
            HashCombine(fingerprint, pipeline.HashLut1DStage(node.lut.shaper1D));
            HashCombine(fingerprint, pipeline.HashLut3DStage(node.lut.lut3D));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
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
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind == RenderGraphNodeKind::RawOperation) {
            for (const char* port : {"imageIn", "referenceIn"}) {
                const auto* input = findInputLink(node.nodeId, port);
                HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            }
            for (const auto& link : graph.links) {
                if (link.toNodeId != node.nodeId || (link.toSocketId != "maskIn" &&
                    link.toSocketId.rfind("gradient:", 0) != 0 && link.toSocketId.rfind("area:", 0) != 0)) continue;
                HashCombine(fingerprint, HashValue(link.toSocketId));
                HashCombine(fingerprint, fingerprintMask(link.fromNodeId, link.fromSocketId));
            }
            for (const auto& parameter : Stack::RawRecipe::GraphParameters(node.rawOperation.kind)) {
                const auto* input = findInputLink(nodeId, EditorNodeGraph::ParameterInputSocketId(parameter.id));
                if (input) { HashCombine(fingerprint, HashValue(parameter.id)); HashCombine(fingerprint, fingerprintScalar(input->fromNodeId, input->fromSocketId)); }
            }
            HashCombine(fingerprint, HashJson(Stack::RawRecipe::SerializeGraphOperation(node.rawOperation)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.rawWorkingSpace)));
            HashCombine(fingerprint, HashValue(node.nativeWidth));
            HashCombine(fingerprint, HashValue(node.nativeHeight));
        } else if (node.kind == RenderGraphNodeKind::Layer) {
            const RenderGraphLink* imageLink = findInputLink(node.nodeId, "imageIn");
            const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
            HashCombine(fingerprint, imageLink ? fingerprintImage(imageLink->fromNodeId, imageLink->fromSocketId) : 0);
            HashCombine(fingerprint, maskLink ? fingerprintMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0);
            HashCombine(fingerprint, HashJson(node.layerJson));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind == RenderGraphNodeKind::TechnicalImage) {
            const RenderGraphLink* input = findInputLink(node.nodeId, EditorNodeGraph::kImageInputSocketId);
            HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.technicalImageOperation)));
            const RenderGraphLink* exposureInput = findInputLink(
                node.nodeId, EditorNodeGraph::kExposureValueInputSocketId);
            HashCombine(fingerprint, exposureInput
                ? fingerprintScalar(exposureInput->fromNodeId, exposureInput->fromSocketId)
                : HashValue(node.technicalExposureValue));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind == RenderGraphNodeKind::Reformat) {
            const RenderGraphLink* input = findInputLink(
                node.nodeId, EditorNodeGraph::kImageInputSocketId);
            HashCombine(fingerprint, input
                ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(Stack::NodeMath::kReformatAlgorithmVersion));
            HashCombine(fingerprint, HashValue(node.reformatSettings.width));
            HashCombine(fingerprint, HashValue(node.reformatSettings.height));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.reformatSettings.filter)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.reformatSettings.border)));
        } else if (node.kind == RenderGraphNodeKind::Mix) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
            const RenderGraphLink* factorLink = findInputLink(node.nodeId, "factor");
            HashCombine(fingerprint, inputA ? fingerprintImage(inputA->fromNodeId, inputA->fromSocketId) : 0);
            HashCombine(fingerprint, inputB ? fingerprintImage(inputB->fromNodeId, inputB->fromSocketId) : 0);
            HashCombine(fingerprint, factorLink ? fingerprintMask(factorLink->fromNodeId, factorLink->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.mixBlendMode)));
            HashCombine(fingerprint, HashValue(node.mixFactor));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
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
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind == RenderGraphNodeKind::SpectrumView) {
            const RenderGraphLink* input = findInputLink(
                node.nodeId, EditorNodeGraph::kSpectrumInputSocketId);
            HashCombine(fingerprint, input
                ? fingerprintFrequency(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.spectrumViewSettings.mode)));
            HashCombine(fingerprint, HashValue(static_cast<int>(node.spectrumViewSettings.lut)));
            HashCombine(fingerprint, HashValue(node.spectrumViewSettings.exposure));
            HashCombine(fingerprint, HashValue(node.spectrumViewSettings.gamma));
            HashCombine(fingerprint, HashValue(node.spectrumViewSettings.centerDc));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind == RenderGraphNodeKind::SpectrumMath) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, EditorNodeGraph::kMixInputASocketId);
            const RenderGraphLink* inputB = findInputLink(node.nodeId, EditorNodeGraph::kMixInputBSocketId);
            const RenderGraphLink* maskInput = findInputLink(node.nodeId, EditorNodeGraph::kMaskInputSocketId);
            HashCombine(fingerprint, inputA ? fingerprintImage(inputA->fromNodeId, inputA->fromSocketId) : 0);
            HashCombine(fingerprint, inputB ? fingerprintImage(inputB->fromNodeId, inputB->fromSocketId) : 0);
            HashCombine(fingerprint, maskInput ? fingerprintMask(maskInput->fromNodeId, maskInput->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.spectrumMathMode)));
            HashCombine(fingerprint, HashValue(node.spectrumMathSettings.amount));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind == RenderGraphNodeKind::MagnitudePhase) {
            HashCombine(fingerprint, HashValue(static_cast<int>(node.magnitudePhaseMode)));
            HashCombine(fingerprint, HashValue(node.magnitudePhaseSettings.exposure));
            HashCombine(fingerprint, HashValue(node.magnitudePhaseSettings.gamma));
            if (node.magnitudePhaseMode == RenderMagnitudePhaseMode::Recombine &&
                socketId == EditorNodeGraph::kImageOutputSocketId) {
                const RenderGraphLink* magInput = findInputLink(node.nodeId, "magnitude");
                const RenderGraphLink* phaseInput = findInputLink(node.nodeId, "phase");
                HashCombine(fingerprint, magInput ? fingerprintMask(magInput->fromNodeId, magInput->fromSocketId) : 0);
                HashCombine(fingerprint, phaseInput ? fingerprintMask(phaseInput->fromNodeId, phaseInput->fromSocketId) : 0);
            } else {
                const RenderGraphLink* input = findInputLink(node.nodeId, EditorNodeGraph::kImageInputSocketId);
                HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            }
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind == RenderGraphNodeKind::SpectrumAnalyzer) {
            const RenderGraphLink* input = findInputLink(node.nodeId, EditorNodeGraph::kImageInputSocketId);
            HashCombine(fingerprint, input ? fingerprintImage(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.spectrumAnalyzerMode)));
            HashCombine(fingerprint, HashValue(node.spectrumAnalyzerSettings.innerRadius));
            HashCombine(fingerprint, HashValue(node.spectrumAnalyzerSettings.outerRadius));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind == RenderGraphNodeKind::Output) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            if (input) {
                const bool channelInput = IsScalarRenderSocket(
                    executionContext,
                    input->fromNodeId,
                    input->fromSocketId);
                HashCombine(
                    fingerprint,
                    channelInput
                        ? fingerprintMask(
                            input->fromNodeId,
                            input->fromSocketId)
                        : fingerprintImage(
                            input->fromNodeId,
                            input->fromSocketId));
            }
            HashCombine(
                fingerprint,
                HashValue(static_cast<int>(node.outputChannelViewMode)));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind == RenderGraphNodeKind::ChannelCombine) {
            const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
            const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
            const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
            const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");
            HashCombine(fingerprint, linkR ? fingerprintMask(linkR->fromNodeId, linkR->fromSocketId) : 0);
            HashCombine(fingerprint, linkG ? fingerprintMask(linkG->fromNodeId, linkG->fromSocketId) : 0);
            HashCombine(fingerprint, linkB ? fingerprintMask(linkB->fromNodeId, linkB->fromSocketId) : 0);
            HashCombine(fingerprint, linkA ? fingerprintMask(linkA->fromNodeId, linkA->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        }

        fingerprintingImages.erase(key);
        imageFingerprintCache[key] = fingerprint;
        return fingerprint;
    };

}

} // namespace Stack::Renderer::GraphExecution
