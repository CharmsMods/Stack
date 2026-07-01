#include "Renderer/RenderPipeline.h"

#include <algorithm>

unsigned int RenderPipeline::GenerateMaskTexture(const RenderMaskSource& mask) {
    EnsureMaskPrograms();
    if (!m_MaskProgram || m_Width <= 0 || m_Height <= 0) {
        return 0;
    }

    unsigned int texture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    unsigned int fbo = GLHelpers::CreateFBO(texture);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, m_Width, m_Height);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_MaskProgram);
    glUniform1i(glGetUniformLocation(m_MaskProgram, "uKind"), static_cast<int>(mask.kind));
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uValue"), std::clamp(mask.settings.value, 0.0f, 1.0f));
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uAngle"), mask.settings.angle);
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uOffset"), mask.settings.offset);
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uScale"), mask.settings.scale);
    glUniform2f(glGetUniformLocation(m_MaskProgram, "uCenter"), mask.settings.centerX, mask.settings.centerY);
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uRadius"), mask.settings.radius);
    glUniform1f(glGetUniformLocation(m_MaskProgram, "uFeather"), mask.settings.feather);
    glUniform1i(glGetUniformLocation(m_MaskProgram, "uInvert"), mask.settings.invert ? 1 : 0);
    m_Quad.Draw();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    return texture;
}

void RenderPipeline::RenderMaskBlend(unsigned int originalTexture, unsigned int processedTexture, unsigned int maskTexture, unsigned int targetFBO) {
    EnsureMaskPrograms();
    if (!m_MaskBlendProgram || !originalTexture || !processedTexture || !maskTexture) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_MaskBlendProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, originalTexture);
    glUniform1i(glGetUniformLocation(m_MaskBlendProgram, "uOriginal"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, processedTexture);
    glUniform1i(glGetUniformLocation(m_MaskBlendProgram, "uProcessed"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, maskTexture);
    glUniform1i(glGetUniformLocation(m_MaskBlendProgram, "uMask"), 2);

    m_Quad.Draw();

    glActiveTexture(GL_TEXTURE0);
}

void RenderPipeline::RenderMaskCombine(unsigned int maskA, unsigned int maskB, RenderMaskCombineMode mode, unsigned int targetFBO) {
    EnsureMaskPrograms();
    if (!m_MaskCombineProgram || !maskA || !maskB) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_MaskCombineProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, maskA);
    glUniform1i(glGetUniformLocation(m_MaskCombineProgram, "uMaskA"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, maskB);
    glUniform1i(glGetUniformLocation(m_MaskCombineProgram, "uMaskB"), 1);

    glUniform1i(glGetUniformLocation(m_MaskCombineProgram, "uMode"), static_cast<int>(mode));
    m_Quad.Draw();

    glActiveTexture(GL_TEXTURE0);
}

void RenderPipeline::RenderMixBlend(unsigned int textureA, unsigned int textureB, unsigned int factorTexture, float factor, RenderMixBlendMode mode, unsigned int targetFBO) {
    EnsureMixProgram();
    if (!m_MixProgram || !textureA || !textureB) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_MixProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureA);
    glUniform1i(glGetUniformLocation(m_MixProgram, "uImageA"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textureB);
    glUniform1i(glGetUniformLocation(m_MixProgram, "uImageB"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, factorTexture);
    glUniform1i(glGetUniformLocation(m_MixProgram, "uFactorMask"), 2);

    glUniform1i(glGetUniformLocation(m_MixProgram, "uHasFactorMask"), factorTexture ? 1 : 0);
    glUniform1f(glGetUniformLocation(m_MixProgram, "uFactor"), factor);
    glUniform1i(glGetUniformLocation(m_MixProgram, "uBlendMode"), static_cast<int>(mode));
    m_Quad.Draw();

    glActiveTexture(GL_TEXTURE0);
}

void RenderPipeline::RenderDataMath(
    unsigned int textureA,
    unsigned int textureB,
    bool hasA,
    bool hasB,
    bool scalarA,
    bool scalarB,
    RenderDataMathMode mode,
    const RenderDataMathSettings& settings,
    bool scalarOutput,
    unsigned int targetFBO) {
    EnsureDataMathProgram();
    if (!m_DataMathProgram || (!textureA && hasA) || (!textureB && hasB)) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_DataMathProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureA);
    glUniform1i(glGetUniformLocation(m_DataMathProgram, "uDataA"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textureB);
    glUniform1i(glGetUniformLocation(m_DataMathProgram, "uDataB"), 1);

    const float minValue = std::min(settings.minValue, settings.maxValue);
    const float maxValue = std::max(settings.minValue, settings.maxValue);
    glUniform1i(glGetUniformLocation(m_DataMathProgram, "uHasA"), hasA ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_DataMathProgram, "uHasB"), hasB ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_DataMathProgram, "uScalarA"), scalarA ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_DataMathProgram, "uScalarB"), scalarB ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_DataMathProgram, "uMode"), static_cast<int>(mode));
    glUniform1f(glGetUniformLocation(m_DataMathProgram, "uConstantA"), settings.constantA);
    glUniform1f(glGetUniformLocation(m_DataMathProgram, "uConstantB"), settings.constantB);
    glUniform1f(glGetUniformLocation(m_DataMathProgram, "uMinValue"), minValue);
    glUniform1f(glGetUniformLocation(m_DataMathProgram, "uMaxValue"), maxValue);
    glUniform1f(glGetUniformLocation(m_DataMathProgram, "uOutMin"), settings.outMin);
    glUniform1f(glGetUniformLocation(m_DataMathProgram, "uOutMax"), settings.outMax);
    glUniform1i(glGetUniformLocation(m_DataMathProgram, "uScalarOutput"), scalarOutput ? 1 : 0);
    m_Quad.Draw();

    glActiveTexture(GL_TEXTURE0);
}

void RenderPipeline::RenderMaskUtility(unsigned int inputMask, const RenderGraphNode& node, unsigned int targetFBO) {
    EnsureUtilityPrograms();
    if (!m_MaskUtilityProgram || !inputMask) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_MaskUtilityProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputMask);
    glUniform1i(glGetUniformLocation(m_MaskUtilityProgram, "uInputMask"), 0);
    glUniform1i(glGetUniformLocation(m_MaskUtilityProgram, "uKind"), static_cast<int>(node.maskUtilityKind));
    glUniform1f(glGetUniformLocation(m_MaskUtilityProgram, "uBlackPoint"), node.maskUtilitySettings.blackPoint);
    glUniform1f(glGetUniformLocation(m_MaskUtilityProgram, "uWhitePoint"), node.maskUtilitySettings.whitePoint);
    glUniform1f(glGetUniformLocation(m_MaskUtilityProgram, "uGamma"), node.maskUtilitySettings.gamma);
    glUniform1f(glGetUniformLocation(m_MaskUtilityProgram, "uThreshold"), node.maskUtilitySettings.threshold);
    glUniform1f(glGetUniformLocation(m_MaskUtilityProgram, "uSoftness"), node.maskUtilitySettings.softness);
    glUniform1i(glGetUniformLocation(m_MaskUtilityProgram, "uInvert"), node.maskUtilitySettings.invert ? 1 : 0);
    m_Quad.Draw();
    glActiveTexture(GL_TEXTURE0);
}

void RenderPipeline::RenderImageToMask(unsigned int inputImage, const RenderGraphNode& node, unsigned int targetFBO) {
    EnsureUtilityPrograms();
    if (!m_ImageToMaskProgram || !inputImage) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_ImageToMaskProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputImage);
    glUniform1i(glGetUniformLocation(m_ImageToMaskProgram, "uInputImage"), 0);
    glUniform1i(glGetUniformLocation(m_ImageToMaskProgram, "uKind"), static_cast<int>(node.imageToMaskKind));
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uLow"), node.imageToMaskSettings.low);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uHigh"), node.imageToMaskSettings.high);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uSoftness"), node.imageToMaskSettings.softness);
    glUniform1i(glGetUniformLocation(m_ImageToMaskProgram, "uInvert"), node.imageToMaskSettings.invert ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_ImageToMaskProgram, "uSampleCount"), std::clamp(node.imageToMaskSettings.sampleCount, 1, 5));
    glUniform3f(
        glGetUniformLocation(m_ImageToMaskProgram, "uSampleRgb"),
        node.imageToMaskSettings.sampleRgb[0],
        node.imageToMaskSettings.sampleRgb[1],
        node.imageToMaskSettings.sampleRgb[2]);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uSampleLuma"), node.imageToMaskSettings.sampleLuma);
    glUniform3fv(
        glGetUniformLocation(m_ImageToMaskProgram, "uExtraSampleRgb"),
        4,
        &node.imageToMaskSettings.extraSampleRgb[0][0]);
    glUniform1fv(
        glGetUniformLocation(m_ImageToMaskProgram, "uExtraSampleLuma"),
        4,
        node.imageToMaskSettings.extraSampleLuma);
    glUniform2f(glGetUniformLocation(m_ImageToMaskProgram, "uSampleUv"), node.imageToMaskSettings.sampleU, node.imageToMaskSettings.sampleV);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uToneSimilarity"), node.imageToMaskSettings.toneSimilarity);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uColorSimilarity"), node.imageToMaskSettings.colorSimilarity);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uRegionRadius"), node.imageToMaskSettings.regionRadius);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uRegionFeather"), node.imageToMaskSettings.regionFeather);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uEdgeSensitivity"), node.imageToMaskSettings.edgeSensitivity);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uLocalCoherence"), node.imageToMaskSettings.localCoherence);
    glUniform2f(
        glGetUniformLocation(m_ImageToMaskProgram, "uTexelSize"),
        m_Width > 0 ? 1.0f / static_cast<float>(m_Width) : 0.0f,
        m_Height > 0 ? 1.0f / static_cast<float>(m_Height) : 0.0f);
    m_Quad.Draw();
    glActiveTexture(GL_TEXTURE0);
}


void RenderPipeline::RenderChannelSplit(unsigned int inputTexture, int channel, unsigned int targetFBO) {
    EnsureChannelPrograms();
    if (!m_ChannelSplitProgram || !inputTexture) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_ChannelSplitProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ChannelSplitProgram, "uInputImage"), 0);
    glUniform1i(glGetUniformLocation(m_ChannelSplitProgram, "uChannel"), channel);

    m_Quad.Draw();
    glActiveTexture(GL_TEXTURE0);
}

void RenderPipeline::RenderChannelCombine(unsigned int texR, unsigned int texG, unsigned int texB, unsigned int texA,
                                         bool hasR, bool hasG, bool hasB, bool hasA, unsigned int targetFBO) {
    EnsureChannelPrograms();
    if (!m_ChannelCombineProgram) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_ChannelCombineProgram);

    int textureUnit = 0;
    if (hasR && texR) {
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        glBindTexture(GL_TEXTURE_2D, texR);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uTexR"), textureUnit);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasR"), 1);
        textureUnit++;
    } else {
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasR"), 0);
    }

    if (hasG && texG) {
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        glBindTexture(GL_TEXTURE_2D, texG);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uTexG"), textureUnit);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasG"), 1);
        textureUnit++;
    } else {
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasG"), 0);
    }

    if (hasB && texB) {
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        glBindTexture(GL_TEXTURE_2D, texB);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uTexB"), textureUnit);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasB"), 1);
        textureUnit++;
    } else {
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasB"), 0);
    }

    if (hasA && texA) {
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        glBindTexture(GL_TEXTURE_2D, texA);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uTexA"), textureUnit);
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasA"), 1);
        textureUnit++;
    } else {
        glUniform1i(glGetUniformLocation(m_ChannelCombineProgram, "uHasA"), 0);
    }

    m_Quad.Draw();
    glActiveTexture(GL_TEXTURE0);
}
