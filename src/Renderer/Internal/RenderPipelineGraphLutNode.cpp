#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"

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
    unsigned int combinedInputTexture = 0;
    bool combinedInputOwned = false;
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

        const unsigned int texR = linkR ? evalMask(linkR->fromNodeId, linkR->fromSocketId) : 0;
        const unsigned int texG = linkG ? evalMask(linkG->fromNodeId, linkG->fromSocketId) : 0;
        const unsigned int texB = linkB ? evalMask(linkB->fromNodeId, linkB->fromSocketId) : 0;
        const unsigned int texA = linkA ? evalMask(linkA->fromNodeId, linkA->fromSocketId) : 0;

        combinedInputTexture = CreateGraphRenderTargetTexture();
        RenderIntoGraphTargetTexture(combinedInputTexture, [&](unsigned int fbo) {
            RenderChannelCombine(
                texR,
                texG,
                texB,
                texA,
                linkR != nullptr,
                linkG != nullptr,
                linkB != nullptr,
                linkA != nullptr,
                fbo);
        });
        combinedInputOwned = combinedInputTexture != 0;
        return combinedInputTexture;
    }();

    const std::string lut1DKey = std::to_string(node.nodeId) + ":lut1d";
    const std::string shaperKey = std::to_string(node.nodeId) + ":shaper1d";
    const std::string lut3DKey = std::to_string(node.nodeId) + ":lut3d";
    if (inputTexture == 0) {
        return result;
    }

    if (!ColorLut::HasAnyLutData(node.lut)) {
        ClearLutTextureKey(lut1DKey);
        ClearLutTextureKey(shaperKey);
        ClearLutTextureKey(lut3DKey);
        result.texture = inputTexture;
        result.owned = false;
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
        result.texture = inputTexture;
        result.owned = combinedInputOwned;
        return result;
    }

    unsigned int processed = CreateGraphRenderTargetTexture();
    const bool renderedLut = RenderIntoGraphTargetTexture(processed, [&](unsigned int) {
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

    if (!renderedLut || processed == 0) {
        if (processed != 0) {
            glDeleteTextures(1, &processed);
        }
        result.texture = inputTexture;
        result.owned = combinedInputOwned;
        return result;
    }

    result.texture = processed;
    result.owned = true;
    const RenderGraphLink* maskLink = executionContext.FindInputLink(node.nodeId, "maskIn");
    const unsigned int maskTexture = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
    if (maskTexture) {
        unsigned int blended = CreateGraphRenderTargetTexture();
        RenderIntoGraphTargetTexture(blended, [&](unsigned int fbo) {
            RenderMaskBlend(inputTexture, processed, maskTexture, fbo);
        });
        if (processed != 0) {
            glDeleteTextures(1, &processed);
        }
        result.texture = blended != 0 ? blended : inputTexture;
        if (blended != 0) {
            if (combinedInputOwned && combinedInputTexture != 0) {
                glDeleteTextures(1, &combinedInputTexture);
                combinedInputTexture = 0;
                combinedInputOwned = false;
            }
            result.owned = true;
        } else {
            result.owned = combinedInputOwned;
        }
    }
    if (maskTexture == 0 && combinedInputOwned && combinedInputTexture != 0) {
        glDeleteTextures(1, &combinedInputTexture);
        combinedInputTexture = 0;
        combinedInputOwned = false;
    }

    return result;
}
