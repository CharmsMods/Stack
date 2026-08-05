#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/ScopedGLObjects.h"

#include <functional>
#include <string>

#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D 0x806F
#endif

using namespace Stack::Renderer::GraphExecution;

RenderPipeline::GraphNodeRenderResult RenderPipeline::RenderLutGraphNode(
    const GraphExecutionContext& executionContext,
    const RenderGraphNode& node,
    const std::function<unsigned int(int, const std::string&)>& evalImage,
    const std::function<unsigned int(int, const std::string&)>& evalMask) {
    GraphNodeRenderResult result;

    const RenderGraphLink* input = executionContext.FindInputLink(node.nodeId, "imageIn");
    Stack::Renderer::ScopedGLTexture combinedInputTexture;
    const unsigned int inputTexture = [&]() -> unsigned int {
        if (input) {
            return evalImage(input->fromNodeId, input->fromSocketId);
        }

        const RenderGraphLink* linkR = executionContext.FindInputLink(node.nodeId, "r");
        const RenderGraphLink* linkG = executionContext.FindInputLink(node.nodeId, "g");
        const RenderGraphLink* linkB = executionContext.FindInputLink(node.nodeId, "b");
        const RenderGraphLink* linkA = executionContext.FindInputLink(node.nodeId, "a");
        if (!linkR && !linkG && !linkB && !linkA) {
            return 0;
        }

        int referenceWidth = 0;
        int referenceHeight = 0;
        const auto evaluateChannel = [&](const RenderGraphLink* link) {
            if (!link) {
                return 0u;
            }
            const unsigned int texture =
                evalMask(link->fromNodeId, link->fromSocketId);
            if (texture != 0 && referenceWidth <= 0) {
                referenceWidth = m_Width;
                referenceHeight = m_Height;
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
        if (!inputsReady || referenceWidth <= 0 || referenceHeight <= 0) {
            return 0;
        }
        m_Width = referenceWidth;
        m_Height = referenceHeight;

        combinedInputTexture.Reset(
            CreateGraphRenderTargetTexture());
        bool combinePassExecuted = false;
        const bool combined =
            RenderIntoGraphTargetTexture(combinedInputTexture.Get(), [&](unsigned int fbo) {
                combinePassExecuted = RenderChannelCombine(
                texR,
                texG,
                texB,
                texA,
                linkR != nullptr,
                linkG != nullptr,
                linkB != nullptr,
                linkA != nullptr,
                fbo);
            }) &&
            combinePassExecuted;
        if (!combined || !combinedInputTexture) {
            combinedInputTexture.Reset();
            return 0;
        }
        return combinedInputTexture.Get();
    }();

    const std::string lut1DKey = std::to_string(node.nodeId) + ":lut1d";
    const std::string shaperKey = std::to_string(node.nodeId) + ":shaper1d";
    const std::string lut3DKey = std::to_string(node.nodeId) + ":lut3d";
    if (inputTexture == 0) {
        return result;
    }
    const auto publishInput = [&]() {
        result.texture = inputTexture;
        result.owned = static_cast<bool>(combinedInputTexture);
        if (combinedInputTexture) {
            result.texture = combinedInputTexture.Release();
        }
    };
    const int inputWidth = m_Width;
    const int inputHeight = m_Height;

    if (!ColorLut::HasAnyLutData(node.lut)) {
        ClearLutTextureKey(lut1DKey);
        ClearLutTextureKey(shaperKey);
        ClearLutTextureKey(lut3DKey);
        publishInput();
        return result;
    }

    EnsureLutProgram();
    const bool hasLut1D = ColorLut::HasLut1D(node.lut);
    const bool hasShaper1D = ColorLut::HasShaper1D(node.lut);
    const bool hasLut3D = ColorLut::HasLut3D(node.lut);
    if (!hasLut1D) ClearLutTextureKey(lut1DKey);
    if (!hasShaper1D) ClearLutTextureKey(shaperKey);
    if (!hasLut3D) ClearLutTextureKey(lut3DKey);

    const unsigned int lut1DTexture = hasLut1D
        ? GetOrCreateLut1DTexture(
            lut1DKey,
            node.lut.lut1D,
            HashLut1DStage(node.lut.lut1D))
        : 0;
    const unsigned int shaperTexture = hasShaper1D
        ? GetOrCreateLut1DTexture(
            shaperKey,
            node.lut.shaper1D,
            HashLut1DStage(node.lut.shaper1D))
        : 0;
    const unsigned int lut3DTexture = hasLut3D
        ? GetOrCreateLut3DTexture(
            lut3DKey,
            node.lut.lut3D,
            HashLut3DStage(node.lut.lut3D))
        : 0;

    const bool missingRequiredTexture =
        (hasLut1D && lut1DTexture == 0) ||
        (hasShaper1D && shaperTexture == 0) ||
        (hasLut3D && lut3DTexture == 0) ||
        m_LutProgram == 0;
    if (missingRequiredTexture) {
        publishInput();
        return result;
    }

    Stack::Renderer::ScopedGLTexture processed(
        CreateGraphRenderTargetTexture());
    const bool renderedLut = RenderIntoGraphTargetTexture(processed.Get(), [&](unsigned int) {
        glUseProgram(m_LutProgram);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_LutProgram, "uImage"), 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, lut1DTexture);
        glUniform1i(glGetUniformLocation(m_LutProgram, "uLut1D"), 1);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, shaperTexture);
        glUniform1i(glGetUniformLocation(m_LutProgram, "uShaper1D"), 2);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_3D, lut3DTexture);
        glUniform1i(glGetUniformLocation(m_LutProgram, "uLut3D"), 3);

        glUniform1i(glGetUniformLocation(m_LutProgram, "uHasLut1D"), hasLut1D ? 1 : 0);
        glUniform1i(glGetUniformLocation(m_LutProgram, "uHasShaper1D"), hasShaper1D ? 1 : 0);
        glUniform1i(glGetUniformLocation(m_LutProgram, "uHasLut3D"), hasLut3D ? 1 : 0);
        glUniform1i(
            glGetUniformLocation(m_LutProgram, "uInputTransform"),
            static_cast<int>(node.lut.inputTransform));
        glUniform1i(
            glGetUniformLocation(m_LutProgram, "uOutputTransform"),
            static_cast<int>(node.lut.outputTransform));
        glUniform3fv(glGetUniformLocation(m_LutProgram, "uLut1DDomainMin"), 1, node.lut.lut1D.domainMin.data());
        glUniform3fv(glGetUniformLocation(m_LutProgram, "uLut1DDomainMax"), 1, node.lut.lut1D.domainMax.data());
        glUniform3fv(glGetUniformLocation(m_LutProgram, "uShaperDomainMin"), 1, node.lut.shaper1D.domainMin.data());
        glUniform3fv(glGetUniformLocation(m_LutProgram, "uShaperDomainMax"), 1, node.lut.shaper1D.domainMax.data());
        glUniform3fv(glGetUniformLocation(m_LutProgram, "uLut3DDomainMin"), 1, node.lut.lut3D.domainMin.data());
        glUniform3fv(glGetUniformLocation(m_LutProgram, "uLut3DDomainMax"), 1, node.lut.lut3D.domainMax.data());
        m_Quad.Draw();

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_3D, 0);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
    });

    if (!renderedLut || !processed) {
        publishInput();
        return result;
    }

    const RenderGraphLink* maskLink = executionContext.FindInputLink(node.nodeId, "maskIn");
    const unsigned int maskTexture = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
    m_Width = inputWidth;
    m_Height = inputHeight;
    if (maskLink != nullptr && maskTexture == 0) {
        publishInput();
        return result;
    }
    if (maskTexture) {
        EnsureMaskPrograms();
        Stack::Renderer::ScopedGLTexture blended(
            CreateGraphRenderTargetTexture());
        bool blendPassExecuted = false;
        const bool renderedBlend =
            m_MaskBlendProgram != 0 &&
            RenderIntoGraphTargetTexture(blended.Get(), [&](unsigned int fbo) {
                blendPassExecuted = RenderMaskBlend(
                    inputTexture, processed.Get(), maskTexture, fbo);
            }) &&
            blendPassExecuted;
        if (renderedBlend && blended) {
            result.texture = blended.Release();
            result.owned = true;
        } else {
            publishInput();
        }
        return result;
    }

    result.texture = processed.Release();
    result.owned = true;
    return result;
}
