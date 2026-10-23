#include "Renderer/Internal/RenderPipelineGraphExecutionRuntime.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "NodeMath/ReductionMath.h"
#include "Raw/RawDevelopmentRecipe.h"
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

void GraphExecutionRuntime::BindMaskEvaluation() {
    evalMask = [this](int nodeId, const std::string& socketId) -> unsigned int {
        if (pipeline.m_ShouldCancelRender && pipeline.m_ShouldCancelRender()) return 0;
        if (const auto unavailable=graph.unavailableOutputs.find(nodeId);unavailable!=graph.unavailableOutputs.end()) {
            pipeline.m_LastGraphExecutionStats.lastSpecializedFailureNodeId=nodeId;
            pipeline.m_LastGraphExecutionStats.lastSpecializedFailure=unavailable->second;return 0;
        }
        const std::string key = MakeNodeSocketKey(nodeId, socketId);
        if (const auto cached = maskCache.find(key); cached != maskCache.end()) {
            if (const auto extent = localTextureExtents.find(key);
                extent != localTextureExtents.end()) {
                pipeline.m_Width = extent->second.first;
                pipeline.m_Height = extent->second.second;
            }
            return cached->second;
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
            node.kind == RenderGraphNodeKind::RawDecode ||
            node.kind == RenderGraphNodeKind::RawDevelop ||
            (node.kind == RenderGraphNodeKind::RawDetailAutoMask && socketId != "maskOut") ||
            (node.kind == RenderGraphNodeKind::RawDetailFusion && socketId != "maskOut") ||
            node.kind == RenderGraphNodeKind::HdrMerge ||
            node.kind == RenderGraphNodeKind::Mfsr ||
            node.kind == RenderGraphNodeKind::RawProjectSourceSet ||
            node.kind == RenderGraphNodeKind::Lut ||
            node.kind == RenderGraphNodeKind::Layer ||
            node.kind == RenderGraphNodeKind::Mix ||
            node.kind == RenderGraphNodeKind::TechnicalImage ||
            node.kind == RenderGraphNodeKind::RawOperation ||
            node.kind == RenderGraphNodeKind::Reformat ||
            (node.kind == RenderGraphNodeKind::DataMath && !IsScalarRenderSocket(executionContext, nodeId, socketId)) ||
            node.kind == RenderGraphNodeKind::ImageGenerator ||
            node.kind == RenderGraphNodeKind::ChannelCombine ||
            node.kind == RenderGraphNodeKind::Output) {
            unsigned int imgTex = evalImage(nodeId, socketId);
            visitingMasks.erase(key);
            return imgTex;
        }
        if (node.kind == RenderGraphNodeKind::MaskGenerator || node.kind == RenderGraphNodeKind::CustomMask) {
            // The saved project association supplies geometry, not image pixels.
            // Do not render a consumer (or the camera image) to generate a shape.
            if (node.maskCanvasSourceNodeId > 0 || node.kind == RenderGraphNodeKind::CustomMask) {
                const auto extent = ProjectMaskCanvasExtent(node);
                if (extent.first <= 0 || extent.second <= 0) {
                    visitingMasks.erase(key);
                    return 0;
                }
                pipeline.m_Width = extent.first;
                pipeline.m_Height = extent.second;
            }
            if (const auto* extent = findInputLink(nodeId, EditorNodeGraph::kMatchExtentInputSocketId)) {
                if (!evalImage(extent->fromNodeId, extent->fromSocketId)) {
                    visitingMasks.erase(key);
                    return 0;
                }
            }
        }
        const std::size_t fingerprint = fingerprintMask(nodeId, socketId);
        if (auto* cached = pipeline.FindGraphCacheEntry(pipeline.m_GraphMaskCache,key,fingerprint)) {
            ++pipeline.m_LastGraphExecutionStats.maskCacheHits;
            pipeline.TouchGraphCacheEntry(*cached);
            if (cached->graphGeometry.Valid()) {
                viewportGeometry[key] = cached->graphGeometry;
                pipeline.m_CurrentGraphImageGeometry = cached->graphGeometry;
            }
            if (cached->width > 0 && cached->height > 0) {
                pipeline.m_Width = cached->width;
                pipeline.m_Height = cached->height;
            }
            maskCache[key] = cached->texture;
            localTextureExtents[key] = { pipeline.m_Width, pipeline.m_Height };
            visitingMasks.erase(key);
            return cached->texture;
        }
        ++pipeline.m_LastGraphExecutionStats.maskCacheMisses;

        unsigned int result = 0;
        bool resultOwned = false;
        Stack::Renderer::ScopedOwnedGLTextureExceptionCleanup cleanup(result, resultOwned);
        if (node.kind == RenderGraphNodeKind::MaskGenerator) {
            RenderMaskSource mask;
            mask.nodeId = node.nodeId;
            mask.kind = node.maskKind;
            mask.settings = node.maskSettings;
            if (node.maskKind == RenderMaskGeneratorKind::RawGradient || node.maskKind == RenderMaskGeneratorKind::PaintedArea) {
                const auto* extent = findInputLink(nodeId, EditorNodeGraph::kMatchExtentInputSocketId);
                const auto reference = extent ? evalImage(extent->fromNodeId, extent->fromSocketId) : 0;
                if (node.maskKind == RenderMaskGeneratorKind::RawGradient) {
                    auto serialized = Stack::RawRecipe::SerializeRecipe(Stack::RawRecipe::MakeDefaultRecipe({}));
                    serialized["evGradients"] = nlohmann::json::array({{{"mask", node.rawCoverage}}});
                    const auto recipe = Stack::RawRecipe::DeserializeRecipe(serialized);
                    if (!recipe.evGradients.empty()) {
                        RenderMaskSource fallback; fallback.kind = RenderMaskGeneratorKind::Solid; fallback.settings.value = 1;
                        const auto input = reference ? reference : pipeline.GenerateMaskTexture(fallback);
                        result = pipeline.RenderRawGradientBlend(input, input, recipe.evGradients.front().mask, 0, 4);
                        if (!reference && input) glDeleteTextures(1, &input);
                    }
                } else {
                    const auto areas = Stack::RawRecipe::DeserializeZoneAreas(nlohmann::json::array({node.rawCoverage}));
                    if (!areas.empty()) {
                        const auto coverage = pipeline.m_RawZoneAreaRenderer.Coverage(areas.front(), reference,
                            pipeline.m_Width, pipeline.m_Height, node.rawWorkingSpace, fingerprint, pipeline.m_ShouldCancelRender);
                        if (coverage) {
                            result = createTarget();
                            renderPassToTexture(result, [&](unsigned int fbo) { return pipeline.RenderChannelSplit(coverage, 0, fbo); });
                        }
                    }
                }
            } else result = pipeline.GenerateMaskTexture(mask);
            resultOwned = result != 0;
        } else if (node.kind ==
                   RenderGraphNodeKind::ConstantChannel) {
            const RenderGraphLink* extentInput =
                findInputLink(
                    node.nodeId,
                    EditorNodeGraph::kMatchExtentInputSocketId);
            const unsigned int extentTexture = extentInput
                ? evalMask(
                    extentInput->fromNodeId,
                    extentInput->fromSocketId)
                : 0;
            const int extentWidth = pipeline.m_Width;
            const int extentHeight = pipeline.m_Height;
            if (extentTexture != 0 &&
                extentWidth > 0 &&
                extentHeight > 0) {
                pipeline.m_Width = extentWidth;
                pipeline.m_Height = extentHeight;
                result = createTarget();
                const float value =
                    std::isfinite(node.constantChannelValue)
                        ? node.constantChannelValue
                        : 1.0f;
                renderToTexture(result, [&](unsigned int) {
                    GLfloat previousClearColor[4];
                    glGetFloatv(
                        GL_COLOR_CLEAR_VALUE,
                        previousClearColor);
                    glClearColor(value, value, value, value);
                    glClear(GL_COLOR_BUFFER_BIT);
                    glClearColor(
                        previousClearColor[0],
                        previousClearColor[1],
                        previousClearColor[2],
                        previousClearColor[3]);
                });
                resultOwned = result != 0;
            } else {
                pipeline.m_LastGraphExecutionStats
                    .lastSpecializedFailureNodeId = node.nodeId;
                pipeline.m_LastGraphExecutionStats
                    .lastSpecializedFailure =
                    "Constant Channel requires a resolvable Match Extent Channel input.";
            }
        } else if (node.kind == RenderGraphNodeKind::MaskCombine) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "maskA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "maskB");
            const unsigned int maskA = inputA ? evalMask(inputA->fromNodeId, inputA->fromSocketId) : 0;
            const int referenceWidth = pipeline.m_Width;
            const int referenceHeight = pipeline.m_Height;
            const unsigned int maskB = inputB ? evalMask(inputB->fromNodeId, inputB->fromSocketId) : 0;
            if (maskA && maskB) {
                pipeline.m_Width = referenceWidth;
                pipeline.m_Height = referenceHeight;
                result = createTarget();
                renderPassToTexture(result, [&](unsigned int fbo) {
                    return pipeline.RenderMaskCombine(maskA, maskB, node.maskCombineMode, fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::CustomMask) {
            result = pipeline.GenerateCustomMaskTexture(node.customMask);
            resultOwned = result != 0;
        } else if (node.kind == RenderGraphNodeKind::MaskUtility) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "maskIn");
            const unsigned int inputMask = input ? evalMask(input->fromNodeId, input->fromSocketId) : 0;
            if (inputMask) {
                result = createTarget();
                renderPassToTexture(result, [&](unsigned int fbo) {
                    return pipeline.RenderMaskUtility(inputMask, node, fbo);
                });
                resultOwned = result != 0;
            }
        } else if (node.kind == RenderGraphNodeKind::ImageToMask) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputImage = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
            if (inputImage) {
                result = createTarget();
                renderPassToTexture(result, [&](unsigned int fbo) {
                    return pipeline.RenderImageToMask(inputImage, node, fbo);
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
                if (channelIdx == 3 && node.splitMissingAlphaIsOpaque && input &&
                    input->semanticDescriptor.presentImageComponents.state == Stack::NodeMath::KnowledgeState::Known &&
                    !Stack::NodeMath::HasImageComponent(input->semanticDescriptor.presentImageComponents.value,Stack::NodeMath::ImageComponent::Alpha))
                    channelIdx = 4;
                renderPassToTexture(result, [&](unsigned int fbo) {
                    return pipeline.RenderChannelSplit(inputImage, channelIdx, fbo);
                });
                resultOwned = result != 0;
            }
        }   else if (node.kind == RenderGraphNodeKind::DataMath) {
            const RenderPipeline::GraphNodeRenderResult dataMathResult =
                pipeline.RenderDataMathGraphNode(executionContext, node, socketId, evalImage, evalMask, evalScalar);
            result = dataMathResult.texture;
            resultOwned = dataMathResult.owned;
        }  else if (node.kind == RenderGraphNodeKind::RawDetailAutoMask ||
                   node.kind == RenderGraphNodeKind::RawDetailFusion) {
            const RenderPipeline::GraphNodeRenderResult rawDetailResult =
                pipeline.RenderRawDetailGraphNode(executionContext, node, socketId, evalImage, evalMask);
            result = rawDetailResult.texture;
            resultOwned = rawDetailResult.owned;
        }

        if (pipeline.m_ShouldCancelRender && pipeline.m_ShouldCancelRender()) {
            if (resultOwned && result != 0) glDeleteTextures(1, &result);
            visitingMasks.erase(key);
            return 0;
        }
        if (result) {
            RecordViewportGeometry(nodeId, socketId, resultOwned);
            if (resultOwned &&
                !pipeline.StoreGraphCacheEntry(
                    pipeline.m_GraphMaskCache,
                    key,
                    result,
                    fingerprint,
                    true)) {
                glDeleteTextures(1, &result);
                result = 0;
            } else if (!resultOwned) {
                // Borrowed pass-through textures are valid only while their
                // owning upstream cache entry survives. Persisting the alias
                // would let budget pruning delete the owner independently.
                pipeline.ReleaseGraphCacheEntry(pipeline.m_GraphMaskCache, key);
            }
            if (result != 0) {
                try {
                    maskCache[key] = result;
                    localTextureExtents[key] = { pipeline.m_Width, pipeline.m_Height };
                } catch (const std::bad_alloc&) {
                    maskCache.erase(key);
                    localTextureExtents.erase(key);
                } catch (const std::length_error&) {
                    maskCache.erase(key);
                    localTextureExtents.erase(key);
                }
            }
        } else {
            pipeline.ReleaseGraphCacheEntry(pipeline.m_GraphMaskCache, key);
        }
        visitingMasks.erase(key);
        return result;
    };

}

} // namespace Stack::Renderer::GraphExecution
