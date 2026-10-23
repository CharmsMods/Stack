#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <cstddef>
#include <algorithm>
#include <cmath>
#include <limits>
#include <functional>
#include <string>
#include <vector>

using namespace Stack::Renderer::GraphExecution;

RenderPipeline::GraphNodeRenderResult RenderPipeline::RenderDataMathGraphNode(
    const GraphExecutionContext& executionContext,
    const RenderGraphNode& node,
    const std::string& socketId,
    const std::function<unsigned int(int, const std::string&)>& evalImage,
    const std::function<unsigned int(int, const std::string&)>& evalMask,
    const std::function<bool(int, const std::string&, double&)>& evalScalar) {
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
            float uniform = 0;
        };

        std::vector<ResolvedImageInput> resolvedInputs;
        bool inputsReady = true;
        int referenceWidth = 0;
        int referenceHeight = 0;
        for (const DataMathInputLinkInfo& input : CollectDataMathAverageInputs(executionContext, node.nodeId)) {
            if (IsUniformRenderSocket(executionContext,input.link->fromNodeId,input.link->fromSocketId)) {
                double value=0;
                if (!evalScalar(input.link->fromNodeId,input.link->fromSocketId,value) ||
                    !std::isfinite(value) || std::abs(value)>std::numeric_limits<float>::max()) { inputsReady=false; break; }
                resolvedInputs.push_back({0,true,static_cast<float>(value)});
                continue;
            }
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
            } else if (referenceWidth != m_Width || referenceHeight != m_Height) {
                inputsReady = false;
                break;
            }
            resolvedInputs.push_back(ResolvedImageInput{ texture, scalarInput });
        }

        if (inputsReady && !resolvedInputs.empty() && referenceWidth > 0 && referenceHeight > 0) {
            // Start from spatial data. A prefix of uniform inputs must not
            // materialize a full-size constant image just to accumulate it.
            std::stable_partition(resolvedInputs.begin(),resolvedInputs.end(),
                [](const auto& input){return input.texture!=0;});
            m_Width = referenceWidth;
            m_Height = referenceHeight;
            unsigned int averagedTexture = resolvedInputs.front().texture;
            bool averagedOwned = false;

            if (resolvedInputs.size() > 1) {
                unsigned int runningTexture = resolvedInputs.front().texture;
                bool runningTransient = false;
                bool runningScalar = resolvedInputs.front().scalar;
                for (std::size_t index = 1; index < resolvedInputs.size(); ++index) {
                    auto sumSettings = node.dataMathSettings;
                    sumSettings.constantA = resolvedInputs.front().uniform;
                    sumSettings.constantB = resolvedInputs[index].uniform;
                    unsigned int accumulated = AcquireGraphTransientTarget();
                    bool accumulatedPassExecuted = false;
                    const bool accumulatedRendered =
                        RenderIntoGraphTargetTexture(accumulated, [&](unsigned int fbo) {
                            accumulatedPassExecuted = RenderDataMath(
                            runningTexture,
                            resolvedInputs[index].texture,
                            runningTexture != 0,
                            resolvedInputs[index].texture != 0,
                            runningScalar,
                            resolvedInputs[index].scalar,
                            RenderDataMathMode::Add,
                            sumSettings,
                            scalarOutput,
                            fbo);
                        }) &&
                        accumulatedPassExecuted;
                    if (runningTransient && runningTexture != 0) {
                        ReleaseGraphTransientTarget(runningTexture);
                    }
                    runningTexture = accumulatedRendered ? accumulated : 0;
                    runningTransient = runningTexture != 0;
                    runningScalar = scalarOutput;
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
                        if (runningTransient) ReleaseGraphTransientTarget(runningTexture);
                        // An allocation/pass failure is not a valid unscaled
                        // sum. Leave this output unavailable.
                        averagedTexture = 0;
                        averagedOwned = false;
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
                    if (maskTexture != 0) {
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
                    } else discardOwnedAverage();
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
    RenderDataMathSettings settings = node.dataMathSettings;
    const auto resolveUniform = [&](const RenderGraphLink*& input, float& value) {
        if (!input || !IsUniformRenderSocket(executionContext, input->fromNodeId, input->fromSocketId)) return true;
        double resolved = 0;
        if (!evalScalar(input->fromNodeId, input->fromSocketId, resolved) ||
            !std::isfinite(resolved) || std::abs(resolved) > std::numeric_limits<float>::max()) return false;
        value = static_cast<float>(resolved);
        input = nullptr;
        return true;
    };
    if (!resolveUniform(inputA, settings.constantA) || !resolveUniform(inputB, settings.constantB)) return result;
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
    if (textureA && textureB && (referenceWidthA != m_Width || referenceHeightA != m_Height)) {
        m_LastGraphExecutionStats.lastSpecializedFailureNodeId=node.nodeId;
        m_LastGraphExecutionStats.lastSpecializedFailure="Math inputs use different dimensions; insert an explicit Reformat.";
        return result;
    }
    if (node.dataMathMode == RenderDataMathMode::Divide &&
        !inputB && settings.constantB == 0) {
        m_LastGraphExecutionStats.lastSpecializedFailureNodeId=node.nodeId;
        m_LastGraphExecutionStats.lastSpecializedFailure="Divide by zero: returned zero.";
    }
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
                settings,
                scalarOutput,
                fbo);
            }) &&
            passExecuted;
        if (!rendered && result.texture != 0) {
            glDeleteTextures(1, &result.texture);
            result.texture = 0;
        }
        result.owned = rendered && result.texture != 0;
        if (result.texture != 0) {
            if (const auto* mask = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kMaskInputSocketId)) {
                const auto maskTexture = evalMask(mask->fromNodeId, mask->fromSocketId);
                if (!maskTexture || !textureA) {
                    glDeleteTextures(1, &result.texture);
                    return {};
                }
                m_Width = referenceWidthA; m_Height = referenceHeightA;
                EnsureMaskPrograms();
                auto blended = CreateGraphRenderTargetTexture();
                bool passed = false;
                const bool ready = blended && RenderIntoGraphTargetTexture(blended, [&](unsigned int fbo) {
                    passed = RenderMaskBlend(textureA, result.texture, maskTexture, fbo);
                }) && passed;
                glDeleteTextures(1, &result.texture);
                if (!ready && blended) glDeleteTextures(1, &blended);
                result.texture = ready ? blended : 0;
                result.owned = ready;
            }
        }
    }

    return result;
}
