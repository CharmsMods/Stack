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
    EnsureDataMathProgram();
    if (m_DataMathProgram == 0) {
        return result;
    }

    const bool scalarOutput = IsScalarRenderSocket(executionContext, node.nodeId, socketId);
    if (node.dataMathMode == RenderDataMathMode::Average ||
        node.dataMathMode == RenderDataMathMode::ImageAverage) {
        struct ResolvedImageInput {
            unsigned int texture = 0;
            bool scalar = false;
        };

        std::vector<ResolvedImageInput> resolvedInputs;
        bool inputsReady = true;
        int referenceWidth = 0;
        int referenceHeight = 0;
        for (const DataMathInputLinkInfo& input : CollectDataMathAverageInputs(executionContext, node.nodeId)) {
            const bool scalarInput = IsScalarRenderSocket(executionContext, input.link->fromNodeId, input.link->fromSocketId);
            const unsigned int texture = scalarInput
                ? evalMask(input.link->fromNodeId, input.link->fromSocketId)
                : evalImage(input.link->fromNodeId, input.link->fromSocketId);
            if (texture == 0) {
                inputsReady = false;
                break;
            }
            if (referenceWidth <= 0) {
                referenceWidth = m_Width;
                referenceHeight = m_Height;
            }
            resolvedInputs.push_back(ResolvedImageInput{ texture, scalarInput });
        }

        if (inputsReady && !resolvedInputs.empty()) {
            m_Width = referenceWidth;
            m_Height = referenceHeight;
            unsigned int averagedTexture = resolvedInputs.front().texture;
            bool averagedOwned = false;

            if (resolvedInputs.size() > 1) {
                unsigned int runningTexture = resolvedInputs.front().texture;
                bool runningTransient = false;
                for (std::size_t index = 1; index < resolvedInputs.size(); ++index) {
                    unsigned int accumulated = AcquireGraphTransientTarget();
                    bool accumulatedPassExecuted = false;
                    const bool accumulatedRendered =
                        RenderIntoGraphTargetTexture(accumulated, [&](unsigned int fbo) {
                            accumulatedPassExecuted = RenderDataMath(
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
                        }) &&
                        accumulatedPassExecuted;
                    if (runningTransient && runningTexture != 0) {
                        ReleaseGraphTransientTarget(runningTexture);
                    }
                    runningTexture = accumulatedRendered ? accumulated : 0;
                    runningTransient = runningTexture != 0;
                    if (!accumulatedRendered && accumulated != 0) {
                        ReleaseGraphTransientTarget(accumulated);
                    }
                    if (runningTexture == 0) {
                        break;
                    }
                }

                if (runningTexture != 0) {
                    RenderDataMathSettings divideSettings = node.dataMathSettings;
                    divideSettings.constantB = static_cast<float>(resolvedInputs.size());
                    unsigned int divided = CreateGraphRenderTargetTexture();
                    bool dividedPassExecuted = false;
                    const bool dividedRendered =
                        RenderIntoGraphTargetTexture(divided, [&](unsigned int fbo) {
                            dividedPassExecuted = RenderDataMath(
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
                        }) &&
                        dividedPassExecuted;
                    if (divided != 0 && dividedRendered) {
                        if (runningTransient) ReleaseGraphTransientTarget(runningTexture);
                        averagedTexture = divided;
                        averagedOwned = true;
                    } else {
                        if (divided != 0) glDeleteTextures(1, &divided);
                        averagedTexture = runningTexture;
                        averagedOwned = runningTransient && PromoteGraphTransientTarget(runningTexture);
                    }
                } else {
                    averagedTexture = 0;
                    averagedOwned = false;
                }
            }

            if (averagedTexture != 0) {
                const auto discardOwnedAverage = [&]() {
                    if (averagedOwned && averagedTexture != 0) {
                        glDeleteTextures(1, &averagedTexture);
                    }
                    averagedTexture = 0;
                    averagedOwned = false;
                };
                int outputWidth = referenceWidth;
                int outputHeight = referenceHeight;
                if (const RenderGraphLink* maskLink = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kMaskInputSocketId)) {
                    const unsigned int maskTexture = evalMask(maskLink->fromNodeId, maskLink->fromSocketId);
                    if (maskTexture == 0) {
                        discardOwnedAverage();
                    } else {
                        unsigned int baseTexture = 0;
                        bool baseTransient = false;
                        bool baseReady = true;
                        int baseWidth = referenceWidth;
                        int baseHeight = referenceHeight;
                        if (const RenderGraphLink* baseLink = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kDataMathBaseInputSocketId)) {
                            const bool scalarBase = IsScalarRenderSocket(executionContext, baseLink->fromNodeId, baseLink->fromSocketId);
                            baseTexture = scalarBase
                                ? evalMask(baseLink->fromNodeId, baseLink->fromSocketId)
                                : evalImage(baseLink->fromNodeId, baseLink->fromSocketId);
                            if (baseTexture != 0) {
                                baseWidth = m_Width;
                                baseHeight = m_Height;
                            } else {
                                baseReady = false;
                            }
                        } else {
                            m_Width = referenceWidth;
                            m_Height = referenceHeight;
                            baseTexture = AcquireGraphTransientTarget();
                            const bool baseRendered = RenderIntoGraphTargetTexture(baseTexture, [&](unsigned int) {
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
                            baseTransient = baseRendered && baseTexture != 0;
                            if (!baseRendered) {
                                if (baseTexture != 0) {
                                    ReleaseGraphTransientTarget(baseTexture);
                                }
                                baseTexture = 0;
                                baseReady = false;
                            }
                        }
                        if (baseReady && baseTexture != 0) {
                            m_Width = baseWidth;
                            m_Height = baseHeight;
                            EnsureMaskPrograms();
                            unsigned int blended = CreateGraphRenderTargetTexture();
                            bool blendPassExecuted = false;
                            const bool blendedRendered =
                                m_MaskBlendProgram != 0 &&
                                RenderIntoGraphTargetTexture(blended, [&](unsigned int fbo) {
                                    blendPassExecuted = RenderMaskBlend(
                                        baseTexture, averagedTexture, maskTexture, fbo);
                                }) &&
                                blendPassExecuted;
                            if (baseTransient && baseTexture != 0) {
                                ReleaseGraphTransientTarget(baseTexture);
                            }
                            if (blendedRendered && blended != 0) {
                                if (averagedOwned) {
                                    glDeleteTextures(1, &averagedTexture);
                                }
                                averagedTexture = blended;
                                averagedOwned = true;
                                outputWidth = baseWidth;
                                outputHeight = baseHeight;
                            } else {
                                if (blended != 0) {
                                    glDeleteTextures(1, &blended);
                                }
                                discardOwnedAverage();
                            }
                        } else {
                            discardOwnedAverage();
                        }
                    }
                }
                m_Width = outputWidth;
                m_Height = outputHeight;
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
    const int referenceWidthA = textureA != 0 ? m_Width : 0;
    const int referenceHeightA = textureA != 0 ? m_Height : 0;
    const unsigned int textureB = inputB
        ? (scalarB ? evalMask(inputB->fromNodeId, inputB->fromSocketId) : evalImage(inputB->fromNodeId, inputB->fromSocketId))
        : 0;
    if ((!inputA || textureA) && (!inputB || textureB)) {
        if (referenceWidthA > 0 && referenceHeightA > 0) {
            m_Width = referenceWidthA;
            m_Height = referenceHeightA;
        }
        result.texture = CreateGraphRenderTargetTexture();
        bool passExecuted = false;
        const bool rendered =
            RenderIntoGraphTargetTexture(result.texture, [&](unsigned int fbo) {
                passExecuted = RenderDataMath(
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
            }) &&
            passExecuted;
        if (!rendered && result.texture != 0) {
            glDeleteTextures(1, &result.texture);
            result.texture = 0;
        }
        result.owned = rendered && result.texture != 0;
    }

    return result;
}
