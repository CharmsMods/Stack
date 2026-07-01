#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

using namespace Stack::Renderer::GraphExecution;

RenderPipeline::GraphNodeRenderResult RenderPipeline::RenderDataMathGraphNode(
    const GraphExecutionContext& executionContext,
    const RenderGraphNode& node,
    const std::string& socketId,
    const std::function<unsigned int(int, const std::string&)>& evalImage,
    const std::function<unsigned int(int, const std::string&)>& evalMask) {
    GraphNodeRenderResult result;

    const bool scalarOutput = IsScalarRenderSocket(executionContext, node.nodeId, socketId);
    if (node.dataMathMode == RenderDataMathMode::Average ||
        node.dataMathMode == RenderDataMathMode::ImageAverage) {
        struct ResolvedImageInput {
            unsigned int texture = 0;
            bool scalar = false;
        };

        std::vector<ResolvedImageInput> resolvedInputs;
        bool inputsReady = true;
        for (const DataMathInputLinkInfo& input : CollectDataMathAverageInputs(executionContext, node.nodeId)) {
            const bool scalarInput = IsScalarRenderSocket(executionContext, input.link->fromNodeId, input.link->fromSocketId);
            const unsigned int texture = scalarInput
                ? evalMask(input.link->fromNodeId, input.link->fromSocketId)
                : evalImage(input.link->fromNodeId, input.link->fromSocketId);
            if (texture == 0) {
                inputsReady = false;
                break;
            }
            resolvedInputs.push_back(ResolvedImageInput{ texture, scalarInput });
        }

        if (inputsReady && !resolvedInputs.empty()) {
            unsigned int averagedTexture = resolvedInputs.front().texture;
            bool averagedOwned = false;

            if (resolvedInputs.size() > 1) {
                unsigned int runningTexture = resolvedInputs.front().texture;
                bool runningOwned = false;
                for (std::size_t index = 1; index < resolvedInputs.size(); ++index) {
                    unsigned int accumulated = CreateGraphRenderTargetTexture();
                    RenderIntoGraphTargetTexture(accumulated, [&](unsigned int fbo) {
                        RenderDataMath(
                            runningTexture,
                            resolvedInputs[index].texture,
                            true,
                            true,
                            scalarOutput,
                            resolvedInputs[index].scalar,
                            RenderDataMathMode::Add,
                            node.dataMathSettings,
                            scalarOutput,
                            fbo);
                    });
                    if (runningOwned && runningTexture != 0) {
                        glDeleteTextures(1, &runningTexture);
                    }
                    runningTexture = accumulated;
                    runningOwned = accumulated != 0;
                    if (runningTexture == 0) {
                        break;
                    }
                }

                if (runningTexture != 0) {
                    RenderDataMathSettings divideSettings = node.dataMathSettings;
                    divideSettings.constantB = static_cast<float>(resolvedInputs.size());
                    unsigned int divided = CreateGraphRenderTargetTexture();
                    RenderIntoGraphTargetTexture(divided, [&](unsigned int fbo) {
                        RenderDataMath(
                            runningTexture,
                            0,
                            true,
                            false,
                            scalarOutput,
                            false,
                            RenderDataMathMode::Divide,
                            divideSettings,
                            scalarOutput,
                            fbo);
                    });
                    if (divided != 0) {
                        if (runningOwned) {
                            glDeleteTextures(1, &runningTexture);
                        }
                        averagedTexture = divided;
                        averagedOwned = true;
                    } else {
                        averagedTexture = runningTexture;
                        averagedOwned = runningOwned;
                    }
                } else {
                    averagedTexture = 0;
                    averagedOwned = false;
                }
            }

            if (averagedTexture != 0) {
                if (const RenderGraphLink* maskLink = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kMaskInputSocketId)) {
                    const unsigned int maskTexture = evalMask(maskLink->fromNodeId, maskLink->fromSocketId);
                    if (maskTexture != 0) {
                        unsigned int baseTexture = 0;
                        bool baseOwned = false;
                        if (const RenderGraphLink* baseLink = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kDataMathBaseInputSocketId)) {
                            const bool scalarBase = IsScalarRenderSocket(executionContext, baseLink->fromNodeId, baseLink->fromSocketId);
                            baseTexture = scalarBase
                                ? evalMask(baseLink->fromNodeId, baseLink->fromSocketId)
                                : evalImage(baseLink->fromNodeId, baseLink->fromSocketId);
                        } else {
                            baseTexture = CreateGraphRenderTargetTexture();
                            RenderIntoGraphTargetTexture(baseTexture, [&](unsigned int) {
                                GLfloat previousClearColor[4];
                                glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
                                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                                glClear(GL_COLOR_BUFFER_BIT);
                                glClearColor(
                                    previousClearColor[0],
                                    previousClearColor[1],
                                    previousClearColor[2],
                                    previousClearColor[3]);
                            });
                            baseOwned = baseTexture != 0;
                        }
                        if (baseTexture != 0) {
                            unsigned int blended = CreateGraphRenderTargetTexture();
                            RenderIntoGraphTargetTexture(blended, [&](unsigned int fbo) {
                                RenderMaskBlend(baseTexture, averagedTexture, maskTexture, fbo);
                            });
                            if (baseOwned) {
                                glDeleteTextures(1, &baseTexture);
                            }
                            if (blended != 0) {
                                if (averagedOwned) {
                                    glDeleteTextures(1, &averagedTexture);
                                }
                                averagedTexture = blended;
                                averagedOwned = true;
                            }
                        }
                    }
                }
                result.texture = averagedTexture;
                result.owned = averagedOwned;
            }
        }
        return result;
    }

    const RenderGraphLink* inputA = executionContext.FindInputLink(node.nodeId, "imageA");
    const RenderGraphLink* inputB = executionContext.FindInputLink(node.nodeId, "imageB");
    const bool scalarA = inputA && IsScalarRenderSocket(executionContext, inputA->fromNodeId, inputA->fromSocketId);
    const bool scalarB = inputB && IsScalarRenderSocket(executionContext, inputB->fromNodeId, inputB->fromSocketId);
    const unsigned int textureA = inputA
        ? (scalarA ? evalMask(inputA->fromNodeId, inputA->fromSocketId) : evalImage(inputA->fromNodeId, inputA->fromSocketId))
        : 0;
    const unsigned int textureB = inputB
        ? (scalarB ? evalMask(inputB->fromNodeId, inputB->fromSocketId) : evalImage(inputB->fromNodeId, inputB->fromSocketId))
        : 0;
    if ((!inputA || textureA) && (!inputB || textureB)) {
        result.texture = CreateGraphRenderTargetTexture();
        RenderIntoGraphTargetTexture(result.texture, [&](unsigned int fbo) {
            RenderDataMath(
                textureA,
                textureB,
                inputA != nullptr,
                inputB != nullptr,
                scalarA,
                scalarB,
                node.dataMathMode,
                node.dataMathSettings,
                scalarOutput,
                fbo);
        });
        result.owned = result.texture != 0;
    }

    return result;
}
