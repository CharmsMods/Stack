#include "Renderer/RenderPipeline.h"
#include "NodeMath/ArithmeticContract.h"
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
    // Mix and Remap are expanded into ordinary arithmetic before execution.
    if (node.dataMathMode == RenderDataMathMode::Mix) return result;
    const RenderGraphLink* inputA = executionContext.FindInputLink(node.nodeId, "imageA");
    const RenderGraphLink* inputB = executionContext.FindInputLink(node.nodeId, "imageB");
    RenderDataMathSettings settings = node.dataMathSettings;
    using namespace Stack::NodeMath;
    const auto operands = ResolveArithmetic(inputA ? &inputA->semanticDescriptor : nullptr,
                                           inputB ? &inputB->semanticDescriptor : nullptr);
    if (!operands.error.empty() || ArithmeticLimitsError(node.dataMathMode, settings)) return result;
    const auto resolveUniform = [&](const RenderGraphLink*& input, ArithmeticShape shape, float& value) {
        if (!input || shape != ArithmeticShape::Number) return true;
        double resolved = 0;
        if (!evalScalar(input->fromNodeId, input->fromSocketId, resolved) ||
            !std::isfinite(resolved) || std::abs(resolved) > std::numeric_limits<float>::max()) return false;
        value = static_cast<float>(resolved);
        input = nullptr;
        return true;
    };
    if (!resolveUniform(inputA, operands.a, settings.constantA) || !resolveUniform(inputB, operands.b, settings.constantB)) return result;
    const bool scalarA = inputA && operands.a == ArithmeticShape::Channel;
    const bool scalarB = inputB && operands.b == ArithmeticShape::Channel;
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
                fbo, ShapeOf(&node.semanticDescriptor) == ArithmeticShape::RGB);
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
