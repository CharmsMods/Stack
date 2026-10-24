#include "Renderer/Internal/RenderPipelineGraphExecutionRuntime.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "NodeMath/ReductionMath.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawGraphParameters.h"
#include "Utils/PixelBufferUtils.h"
#include "Renderer/ScopedGLObjects.h"

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

void GraphExecutionRuntime::BindImageEvaluation() {
    evalImage = [this](int nodeId, const std::string& socketId) -> unsigned int {
        if (pipeline.m_ShouldCancelRender && pipeline.m_ShouldCancelRender()) return 0;
        if (const auto unavailable=graph.unavailableOutputs.find(nodeId);unavailable!=graph.unavailableOutputs.end()) {
            pipeline.m_LastGraphExecutionStats.lastSpecializedFailureNodeId=nodeId;
            pipeline.m_LastGraphExecutionStats.lastSpecializedFailure=unavailable->second;return 0;
        }
        const std::string key = MakeNodeSocketKey(nodeId, socketId);
        bool& outputDenoisePending = imageDenoisePending[key];
        DenoiseProvenance::Scope provenanceScope(denoiseProvenance, outputDenoisePending);
        if (const auto cached = imageCache.find(key); cached != imageCache.end()) {
            denoiseProvenance.pending = outputDenoisePending;
            if (const auto extent = localTextureExtents.find(key);
                extent != localTextureExtents.end()) {
                pipeline.m_Width = extent->second.first;
                pipeline.m_Height = extent->second.second;
            }
            return provenanceScope.Return(cached->second);
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

        const RenderGraphNode& authored = *it->second;
        std::optional<RenderGraphNode> evaluated;
        const bool operationActive = authored.kind == RenderGraphNodeKind::RawOperation ? authored.rawOperation.enabled :
            authored.kind == RenderGraphNodeKind::Layer
                ? authored.layerJson.value("enabled",true) && authored.layerJson.value("visible",true) : true;
        const bool hasUniformBindings = operationActive && socketId != "inputImageOut" &&
            std::any_of(authored.uniformParameters.begin(),authored.uniformParameters.end(),
            [&](const auto& parameter) { return findInputLink(nodeId,EditorNodeGraph::ParameterInputSocketId(parameter.id)) != nullptr; });
        if (hasUniformBindings) {
            evaluated=authored;
            if (!ApplyUniformInputs(authored,*evaluated)) { visitingImages.erase(key); return 0; }
        }
        const RenderGraphNode& node = evaluated ? *evaluated : authored;
        if (node.kind == RenderGraphNodeKind::RawOperation &&
            socketId == "inputImageOut") {
            const auto* input = findInputLink(nodeId, "imageIn");
            const auto value = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            visitingImages.erase(key);
            imageCache[key] = value;
            localTextureExtents[key] = {pipeline.m_Width, pipeline.m_Height};
            return provenanceScope.Return(value);
        }
        if (node.kind == RenderGraphNodeKind::MaskGenerator ||
            node.kind == RenderGraphNodeKind::MaskCombine ||
            node.kind == RenderGraphNodeKind::MaskUtility ||
            node.kind == RenderGraphNodeKind::CustomMask ||
            node.kind == RenderGraphNodeKind::ImageToMask ||
            node.kind == RenderGraphNodeKind::ChannelSplit ||
            node.kind == RenderGraphNodeKind::ConstantChannel ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId == "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId == "maskOut")) {
            unsigned int maskTex = evalMask(nodeId, socketId);
            visitingImages.erase(key);
            return provenanceScope.Return(maskTex);
        }
        const std::size_t fingerprint = fingerprintImage(nodeId, socketId);
        const bool rawDevelopStageSocket =
            node.kind == RenderGraphNodeKind::RawDevelop &&
            (socketId == "__rawDevelopBase" ||
             socketId == EditorNodeGraph::kPreFinishImageOutputSocketId);
        const bool rawBorrowedResultNeedsOwnedCache =
            !pipeline.m_RawDevelopmentCachePrewarmActive &&
            (node.kind == RenderGraphNodeKind::RawDevelopment ||
             node.kind == RenderGraphNodeKind::RawDecode ||
             node.kind == RenderGraphNodeKind::RawDevelop);
        const bool rawDevelopmentSideEffectsActive =
            node.kind == RenderGraphNodeKind::RawDevelopment &&
            (pipeline.m_RawDevelopmentCachePrewarmActive ||
             graph.rawWorkspaceLocalRangeTargetSampleRequested ||
             (!graph.rawWorkspaceLocalRangeOverlayMode.empty() &&
              graph.rawWorkspaceLocalRangeOverlayMode != "none"));
        const bool passThroughOutput =
            node.kind == RenderGraphNodeKind::Output &&
            findInputLink(node.nodeId, "imageIn") != nullptr &&
            !IsScalarRenderSocket(
                executionContext,
                findInputLink(node.nodeId, "imageIn")->fromNodeId,
                findInputLink(node.nodeId, "imageIn")->fromSocketId);
        if (!rawDevelopStageSocket &&
            !rawDevelopmentSideEffectsActive &&
            !passThroughOutput) {
            if (auto* cached = pipeline.FindGraphCacheEntry(pipeline.m_GraphImageCache,key,fingerprint)) {
                denoiseProvenance.pending = cached->denoisePending;
                ++pipeline.m_LastGraphExecutionStats.imageCacheHits;
                pipeline.TouchGraphCacheEntry(*cached);
                pipeline.m_RawViewportAppliedRegion = cached->viewportRegion;
                if (cached->graphGeometry.Valid()) {
                    viewportGeometry[key] = cached->graphGeometry;
                    pipeline.m_CurrentGraphImageGeometry = cached->graphGeometry;
                }
                if (cached->width > 0 && cached->height > 0) {
                    pipeline.m_Width = cached->width;
                    pipeline.m_Height = cached->height;
                }
                imageCache[key] = cached->texture;
                localTextureExtents[key] = { pipeline.m_Width, pipeline.m_Height };
                pipeline.m_LastGraphImageCacheHits.insert(key);
                visitingImages.erase(key);
                return provenanceScope.Return(cached->texture);
            }
        }
        ++pipeline.m_LastGraphExecutionStats.imageCacheMisses;

        unsigned int result = 0;
        bool resultOwned = false;
        Stack::Renderer::ScopedOwnedGLTextureExceptionCleanup cleanup(result, resultOwned);
        const auto withBorrowedInputProvenance = [&](const auto& render) {
            unsigned int inputTexture = 0;
            bool inputDenoisePending = false;
            const auto trackImageInput = [&](int inputNodeId, const std::string& inputSocketId) {
                const auto texture = evalImage(inputNodeId, inputSocketId);
                if (inputTexture == 0 && texture != 0) {
                    inputTexture = texture;
                    // Includes uniforms evaluated before this node's input.
                    inputDenoisePending = denoiseProvenance.pending;
                }
                return texture;
            };
            const auto rendered = render(trackImageInput);
            if (!rendered.owned && inputTexture != 0 && rendered.texture == inputTexture) {
                // These executors may inspect a mask before deciding to
                // return their untouched image input. Unused mask pixels do
                // not make that borrowed output provisional.
                denoiseProvenance.pending = inputDenoisePending;
            }
            return rendered;
        };
        bool pointwiseFused = false;
        if (const auto prepared = pointwiseFusionPlans.find(key);
            prepared != pointwiseFusionPlans.end() && !pointwiseFusionDisabledNodes.count(node.nodeId)) {
            const auto& fusionPlan = prepared->second;
            const unsigned int pointwiseInput = evalImage(
                fusionPlan.inputNodeId,
                fusionPlan.inputSocketId);
            const RenderPipeline::GraphNodeRenderResult fused = pipeline.RenderPointwiseFusionPlan(
                fusionPlan,
                pointwiseInput,
                graph.executionInspectionEnabled);
            if (fused.texture != 0) {
                result = fused.texture;
                resultOwned = fused.owned;
                pointwiseFused = true;
            } else {
                ++pipeline.m_LastGraphExecutionStats.pointwiseFallbacks;
                pointwiseFusionDisabledNodes.insert(
                    fusionPlan.authoredNodeIds.begin(), fusionPlan.authoredNodeIds.end());
            }
        }
        if (pointwiseFused) {
            // The semantic chain was emitted as one physical pass above.
        } else if (node.kind == RenderGraphNodeKind::Image) {
            if (!node.image.pixels.empty() && node.image.width > 0 && node.image.height > 0) {
                pipeline.m_Width = node.image.width;
                pipeline.m_Height = node.image.height;
                result = GLHelpers::CreateTextureFromPixels(
                    node.image.pixels.data(),
                    node.image.width,
                    node.image.height,
                    node.image.channels);
                resultOwned = result != 0;
            } else {
                pipeline.m_Width = pipeline.m_BaseCanvasWidth;
                pipeline.m_Height = pipeline.m_BaseCanvasHeight;
                result = pipeline.m_SourceTexture;
            }
        } else if (node.kind == RenderGraphNodeKind::RawSource) {
            result = 0;
        } else if (node.kind == RenderGraphNodeKind::RawDevelopment) {
            const RenderPipeline::GraphNodeRenderResult rawDevelopmentResult =
                pipeline.RenderRawDevelopmentGraphNode(node, fingerprint);
            result = rawDevelopmentResult.texture;
            resultOwned = rawDevelopmentResult.owned;
            denoiseProvenance.pending |= rawDevelopmentResult.denoisePending;
        } else if (node.kind == RenderGraphNodeKind::RawDecode) {
            const std::string rawBaseKey = std::to_string(node.nodeId) + ":__rawDecodeBase";
            const std::size_t rawBaseFingerprint = fingerprintImage(node.nodeId, "__rawDecodeBase");
            const RenderPipeline::SharedRawBaseStageResult rawBaseStage =
                pipeline.RenderSharedRawBaseStage(executionContext, node, node.rawDecode.settings, rawBaseKey, rawBaseFingerprint, fingerprintImage);
            result = rawBaseStage.texture;
            resultOwned = false;
        } else if (node.kind == RenderGraphNodeKind::RawDevelop) {
            const auto rawDevelopResult = withBorrowedInputProvenance([&](const auto& imageInput) {
                return pipeline.RenderRawDevelopGraphNode(
                    executionContext,
                    node,
                    socketId,
                    fingerprint,
                    imageCache,
                    imageInput,
                    evalMask,
                    fingerprintImage);
            });
            result = rawDevelopResult.texture;
            resultOwned = rawDevelopResult.owned;
        } else if (node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const RenderPipeline::GraphNodeRenderResult rawDetailResult =
                pipeline.RenderRawDetailGraphNode(executionContext, node, socketId, evalImage, evalMask);
            result = rawDetailResult.texture;
            resultOwned = rawDetailResult.owned;
        } else if (node.kind == RenderGraphNodeKind::HdrMerge) {
            const RenderGraphLink* input1 = findInputLink(node.nodeId, "image1");
            const RenderGraphLink* input2 = findInputLink(node.nodeId, "image2");
            const RenderGraphLink* input3 = findInputLink(node.nodeId, "image3");
            const unsigned int texture1 = input1 ? evalImage(input1->fromNodeId, input1->fromSocketId) : 0;
            const int referenceWidth = pipeline.m_Width;
            const int referenceHeight = pipeline.m_Height;
            const unsigned int texture2 = input2 ? evalImage(input2->fromNodeId, input2->fromSocketId) : 0;
            const unsigned int texture3 = input3 ? evalImage(input3->fromNodeId, input3->fromSocketId) : 0;
            const bool hasGap = input3 != nullptr && input2 == nullptr;
            const bool hasRequiredInputs = texture1 != 0 &&
                texture2 != 0 &&
                (input3 == nullptr || texture3 != 0);
            if (!hasGap && hasRequiredInputs) {
                pipeline.m_Width = referenceWidth;
                pipeline.m_Height = referenceHeight;
                std::array<bool, 3> activeInputs { input1 != nullptr, input2 != nullptr, input3 != nullptr };
                std::array<RenderPipeline::HdrMergeInputContext, 3> inputContexts {};
                if (input1) inputContexts[0] = pipeline.ResolveHdrMergeInputContext(executionContext, input1->fromNodeId);
                if (input2) inputContexts[1] = pipeline.ResolveHdrMergeInputContext(executionContext, input2->fromNodeId);
                if (input3) inputContexts[2] = pipeline.ResolveHdrMergeInputContext(executionContext, input3->fromNodeId);
                const RenderPipeline::HdrMergeResolvedSettings resolved = pipeline.ResolveHdrMergeSettings(node.hdrMerge.settings, inputContexts, activeInputs);
                result = createTarget();
                bool mergeRendered = false;
                renderToTexture(result, [&](unsigned int fbo) {
                    mergeRendered = pipeline.RenderHdrMerge(
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
        } else if (node.kind == RenderGraphNodeKind::RawProjectSourceSet) {
            if (node.rawProjectSourceSet.resultAvailable &&
                node.rawDevelopment.embeddedRawData &&
                !node.rawProjectSourceSet.quarantined) {
                RenderGraphNode developedNode = node;
                developedNode.kind =
                    RenderGraphNodeKind::RawDevelopment;
                const RenderPipeline::GraphNodeRenderResult developedResult =
                    pipeline.RenderRawDevelopmentGraphNode(
                        developedNode, fingerprint);
                result = developedResult.texture;
                resultOwned = developedResult.owned;
                denoiseProvenance.pending |= developedResult.denoisePending;
            } else {
                // Never substitute the reference frame for a missing, stale,
                // or failed fused result.
                result = 0;
                resultOwned = false;
                pipeline.m_LastGraphExecutionStats.lastSpecializedFailureNodeId = node.nodeId;
                pipeline.m_LastGraphExecutionStats.lastSpecializedFailure =
                    !node.rawProjectSourceSet.unavailableStatus.empty()
                        ? node.rawProjectSourceSet.unavailableStatus
                        : "The bracket result is unavailable for rendering.";
            }
        } else if (node.kind == RenderGraphNodeKind::Lut) {
            const RenderPipeline::GraphNodeRenderResult lutResult = pipeline.RenderLutGraphNode(executionContext, node, evalImage, evalMask);
            result = lutResult.texture;
            resultOwned = lutResult.owned;
        } else if (node.kind == RenderGraphNodeKind::ImageGenerator) {
            result = pipeline.GenerateImageTexture(node);
            resultOwned = result != 0;
        } else if (node.kind == RenderGraphNodeKind::RawOperation) {
            const auto operationResult = withBorrowedInputProvenance([&](const auto& imageInput) {
                return pipeline.RenderRawOperation(executionContext, node, imageInput, evalMask);
            });
            result = operationResult.texture;
            resultOwned = operationResult.owned;
        } else if (node.kind == RenderGraphNodeKind::Layer) {
            const RenderPipeline::GraphNodeRenderResult layerResult = pipeline.RenderLayerGraphNode(executionContext, node, evalImage, evalMask);
            result = layerResult.texture;
            resultOwned = layerResult.owned;
        } else if (node.kind == RenderGraphNodeKind::TechnicalImage) {
            const RenderGraphLink* input = findInputLink(node.nodeId, EditorNodeGraph::kImageInputSocketId);
            const unsigned int inputTexture = input
                ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            const int imageWidth = pipeline.m_Width, imageHeight = pipeline.m_Height;
            double exposureValue = static_cast<double>(node.technicalExposureValue);
            const RenderGraphLink* exposureInput =
                node.technicalImageOperation == Stack::NodeMath::TechnicalImageOperation::Exposure
                    ? findInputLink(node.nodeId, EditorNodeGraph::kExposureValueInputSocketId)
                    : nullptr;
            const bool exposureReady = exposureInput == nullptr ||
                evalScalar(exposureInput->fromNodeId, exposureInput->fromSocketId, exposureValue);
            if (inputTexture && exposureReady) {
                pipeline.m_Width = imageWidth; pipeline.m_Height = imageHeight;
                result = createTarget();
                renderPassToTexture(result, [&](unsigned int fbo) {
                    return pipeline.RenderTechnicalImage(
                        inputTexture,
                        node.technicalImageOperation,
                        static_cast<float>(exposureValue),
                        fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::Reformat) {
            const RenderGraphLink* input = findInputLink(
                node.nodeId, EditorNodeGraph::kImageInputSocketId);
            const unsigned int inputTexture = input
                ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            const int inputWidth = pipeline.m_Width;
            const int inputHeight = pipeline.m_Height;
            if (inputTexture &&
                Stack::NodeMath::ValidateReformatSettings(node.reformatSettings).empty()) {
                auto canvas = Stack::NodeMath::ResolveFramingCanvas(node.reformatSettings, inputWidth, inputHeight);
                // Original aspect follows authored input geometry, not a rounded preview raster.
                if (canvas.canvasRatio == 1 && node.semanticDescriptor.spatial.state == Stack::NodeMath::KnowledgeState::Known) {
                    canvas.width = int(node.semanticDescriptor.spatial.value.fullWindow.width);
                    canvas.height = int(node.semanticDescriptor.spatial.value.fullWindow.height);
                }
                canvas.canvasRatio = 0;
                pipeline.m_Width = canvas.width;
                pipeline.m_Height = canvas.height;
                result = createTarget();
                bool reformatRendered = false;
                const bool targetRendered = result != 0 && renderToTexture(result, [&](unsigned int fbo) {
                    reformatRendered = pipeline.RenderReformat(
                        inputTexture, inputWidth, inputHeight,
                        canvas, fbo);
                });
                if (!targetRendered || !reformatRendered) {
                    if (result != 0) glDeleteTextures(1, &result);
                    result = 0;
                    pipeline.m_LastGraphExecutionStats.lastSpecializedFailureNodeId = node.nodeId;
                    pipeline.m_LastGraphExecutionStats.lastSpecializedFailure =
                        "Reformat could not execute its declared sampling stage.";
                }
                resultOwned = result != 0;
            } else {
                pipeline.m_LastGraphExecutionStats.lastSpecializedFailureNodeId = node.nodeId;
                pipeline.m_LastGraphExecutionStats.lastSpecializedFailure = inputTexture == 0
                    ? "Reformat could not materialize its required image input."
                    : "Reformat settings do not satisfy the declared geometry contract.";
            }
        } else if (node.kind == RenderGraphNodeKind::Mix) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
            const unsigned int textureA = inputA ? evalImage(inputA->fromNodeId, inputA->fromSocketId) : 0;
            const int referenceWidth = pipeline.m_Width;
            const int referenceHeight = pipeline.m_Height;
            const unsigned int textureB = inputB ? evalImage(inputB->fromNodeId, inputB->fromSocketId) : 0;
            if (textureA && textureB) {
                const RenderGraphLink* factorLink = findInputLink(node.nodeId, "factor");
                pipeline.m_Width = referenceWidth;
                pipeline.m_Height = referenceHeight;
                const unsigned int factorTexture = factorLink ? evalMask(factorLink->fromNodeId, factorLink->fromSocketId) : 0;
                pipeline.m_Width = referenceWidth;
                pipeline.m_Height = referenceHeight;
                if (!factorLink || factorTexture != 0) {
                    result = createTarget();
                    renderPassToTexture(result, [&](unsigned int fbo) {
                        return pipeline.RenderMixBlend(
                            textureA,
                            textureB,
                            factorTexture,
                            node.mixFactor,
                            node.mixBlendMode,
                            fbo);
                    });
                    resultOwned = result != 0;
                } else {
                    std::cerr << "[RenderPipeline] Mix node " << node.nodeId
                              << " could not materialize its connected factor texture.\n";
                }
            } else if (inputA || inputB) {
                std::cerr << "[RenderPipeline] Mix node " << node.nodeId
                          << " could not materialize its connected image textures (A="
                          << textureA << ", B=" << textureB << ").\n";
            }
        } else if (node.kind == RenderGraphNodeKind::DataMath) {
            const RenderPipeline::GraphNodeRenderResult dataMathResult =
                pipeline.RenderDataMathGraphNode(executionContext, node, socketId, evalImage, evalMask, evalScalar);
            result = dataMathResult.texture;
            resultOwned = dataMathResult.owned;
        }  else if (node.kind == RenderGraphNodeKind::ChannelCombine) {
            const RenderGraphLink* linkR = findInputLink(node.nodeId, "r");
            const RenderGraphLink* linkG = findInputLink(node.nodeId, "g");
            const RenderGraphLink* linkB = findInputLink(node.nodeId, "b");
            const RenderGraphLink* linkA = findInputLink(node.nodeId, "a");

            int referenceWidth = 0;
            int referenceHeight = 0;
            const auto evaluateChannel = [&](const RenderGraphLink* link) {
                if (!link) {
                    return 0u;
                }
                const unsigned int texture =
                    evalMask(link->fromNodeId, link->fromSocketId);
                if (texture != 0 && referenceWidth <= 0) {
                    referenceWidth = pipeline.m_Width;
                    referenceHeight = pipeline.m_Height;
                }
                return texture;
            };
            const unsigned int texR = evaluateChannel(linkR);
            const unsigned int texG = evaluateChannel(linkG);
            const unsigned int texB = evaluateChannel(linkB);
            const unsigned int texA = evaluateChannel(linkA);
            const bool inputsReady =
                (!linkR || texR != 0) &&
                (!linkG || texG != 0) &&
                (!linkB || texB != 0) &&
                (!linkA || texA != 0);
            if (inputsReady && referenceWidth > 0 && referenceHeight > 0) {
                pipeline.m_Width = referenceWidth;
                pipeline.m_Height = referenceHeight;
                result = createTarget();
                renderPassToTexture(result, [&](unsigned int fbo) {
                    return pipeline.RenderChannelCombine(
                        texR, texG, texB, texA,
                        linkR != nullptr, linkG != nullptr,
                        linkB != nullptr, linkA != nullptr,
                        fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::Output) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            if (input) {
                const bool channelInput = IsScalarRenderSocket(
                    executionContext,
                    input->fromNodeId,
                    input->fromSocketId);
                if (!channelInput) {
                    result =
                        evalImage(
                            input->fromNodeId,
                            input->fromSocketId);
                } else {
                    const unsigned int channelTexture =
                        evalMask(
                            input->fromNodeId,
                            input->fromSocketId);
                    if (channelTexture != 0) {
                        unsigned int texR = 0;
                        unsigned int texG = 0;
                        unsigned int texB = 0;
                        bool hasR = false;
                        bool hasG = false;
                        bool hasB = false;
                        switch (node.outputChannelViewMode) {
                            case Stack::NodeMath::OutputChannelViewMode::Neutral:
                                texR = channelTexture;
                                texG = channelTexture;
                                texB = channelTexture;
                                hasR = hasG = hasB = true;
                                break;
                            case Stack::NodeMath::OutputChannelViewMode::Red:
                                texR = channelTexture;
                                hasR = true;
                                break;
                            case Stack::NodeMath::OutputChannelViewMode::Green:
                                texG = channelTexture;
                                hasG = true;
                                break;
                            case Stack::NodeMath::OutputChannelViewMode::Blue:
                                texB = channelTexture;
                                hasB = true;
                                break;
                        }
                        result = createTarget();
                        renderPassToTexture(result, [&](unsigned int fbo) {
                            return pipeline.RenderChannelCombine(
                                texR,
                                texG,
                                texB,
                                0,
                                hasR,
                                hasG,
                                hasB,
                                false,
                                fbo);
                        });
                        resultOwned = result != 0;
                    }
                }
            }
        }

        if (pipeline.m_ShouldCancelRender && pipeline.m_ShouldCancelRender()) {
            if (resultOwned && result != 0) glDeleteTextures(1, &result);
            visitingImages.erase(key);
            return 0;
        }
        if (result) {
            RecordViewportGeometry(nodeId, socketId, resultOwned);
            unsigned int borrowedRawFallback = 0;
            if (!resultOwned && rawBorrowedResultNeedsOwnedCache) {
                borrowedRawFallback = result;
                const unsigned int ownedCopy = pipeline.CloneTextureForGraphCache(result, pipeline.m_Width, pipeline.m_Height);
                if (ownedCopy != 0) {
                    result = ownedCopy;
                    resultOwned = true;
                } else {
                    pipeline.ReleaseGraphCacheEntry(pipeline.m_GraphImageCache, key);
                }
            }
            if (passThroughOutput || !resultOwned) {
                // A borrowed pass-through owns no texture. Persistently caching
                // its GLuint creates a dangling alias if budget pruning or an
                // upstream replacement deletes the actual owner.
                pipeline.ReleaseGraphCacheEntry(pipeline.m_GraphImageCache, key);
            } else if (!pipeline.StoreGraphCacheEntry(
                    pipeline.m_GraphImageCache,
                    key,
                    result,
                    fingerprint,
                    true,
                    denoiseProvenance.pending)) {
                glDeleteTextures(1, &result);
                result = borrowedRawFallback;
                resultOwned = false;
            }
            if (result != 0) {
                try {
                    imageCache[key] = result;
                    localTextureExtents[key] = { pipeline.m_Width, pipeline.m_Height };
                } catch (const std::bad_alloc&) {
                    imageCache.erase(key);
                    localTextureExtents.erase(key);
                } catch (const std::length_error&) {
                    imageCache.erase(key);
                    localTextureExtents.erase(key);
                }
            }
        } else {
            pipeline.ReleaseGraphCacheEntry(pipeline.m_GraphImageCache, key);
        }
        visitingImages.erase(key);
        return provenanceScope.Return(result);
    };

}

} // namespace Stack::Renderer::GraphExecution
