#include "Renderer/RenderPipeline.h"

#include <algorithm>

unsigned int RenderPipeline::GenerateMaskTexture(const RenderMaskSource& mask) {
    EnsureMaskPrograms();
    if (!m_MaskProgram || m_Width <= 0 || m_Height <= 0) {
        return 0;
    }

    unsigned int texture = CreateGraphRenderTargetTexture();
    if (texture == 0) {
        return 0;
    }
    const bool rendered = RenderIntoGraphTargetTexture(
        texture,
        [&](unsigned int fbo) {
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glUseProgram(m_MaskProgram);
            glUniform1i(glGetUniformLocation(m_MaskProgram, "uKind"), static_cast<int>(mask.kind));
            glUniform1f(glGetUniformLocation(m_MaskProgram, "uValue"), std::clamp(mask.settings.value, 0.0f, 1.0f));
            glUniform1f(glGetUniformLocation(m_MaskProgram, "uAngle"), mask.settings.angle);
            glUniform1f(glGetUniformLocation(m_MaskProgram, "uOffset"), mask.settings.offset);
            glUniform1f(glGetUniformLocation(m_MaskProgram, "uScale"), mask.settings.scale);
            glUniform2f(glGetUniformLocation(m_MaskProgram, "uCenter"), mask.settings.centerX, mask.settings.centerY);
            glUniform1f(glGetUniformLocation(m_MaskProgram, "uRadius"), mask.settings.radius);
            glUniform1f(glGetUniformLocation(m_MaskProgram, "uRadiusY"), mask.settings.radiusY);
            glUniform1f(glGetUniformLocation(m_MaskProgram, "uFeather"), mask.settings.feather);
            glUniform1i(glGetUniformLocation(m_MaskProgram, "uInvert"), mask.settings.invert ? 1 : 0);
            m_Quad.Draw();
        });
    if (!rendered) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    return texture;
}

bool RenderPipeline::RenderMaskBlend(unsigned int originalTexture, unsigned int processedTexture, unsigned int maskTexture, unsigned int targetFBO) {
    EnsureMaskPrograms();
    if (!m_MaskBlendProgram || !originalTexture || !processedTexture ||
        !maskTexture || !targetFBO) {
        return false;
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
    return true;
}

unsigned int RenderPipeline::RenderRawGradientBlend(
    unsigned int originalTexture, unsigned int processedTexture,
    const Stack::RawRecipe::RawGradientMask& mask,
    unsigned int evReferenceTexture, int evMode, unsigned int coverageTexture) {
    if (!originalTexture || !processedTexture || m_Width <= 0 || m_Height <= 0)
        return 0;
    if (!m_RawGradientBlendProgram) {
        static const char* vertex = R"(
            #version 330 core
            layout(location = 0) in vec2 aPos;
            layout(location = 1) in vec2 aTex;
            out vec2 vTexCoord;
            void main() { vTexCoord = aTex; gl_Position = vec4(aPos, 0.0, 1.0); }
        )";
        static const char* fragment = R"(
            #version 330 core
            in vec2 vTexCoord;
            out vec4 FragColor;
            uniform sampler2D uOriginal;
            uniform sampler2D uProcessed;
            uniform sampler2D uEvReference;
            uniform int uEvMode;
            uniform sampler2D uCoverage;
            uniform int uHasCoverage;
            uniform int uEnabled;
            uniform vec4 uImageRect;
            uniform float uAspect;
            uniform vec2 uCenter;
            uniform float uAngle;
            uniform vec2 uLinearBounds;
            uniform vec2 uRadii;
            uniform float uInnerScale;
            uniform int uShape;
            uniform int uInvert;
            void main() {
                vec2 uv = vec2(uImageRect.x + vTexCoord.x * uImageRect.z,
                    uImageRect.y + (1.0 - vTexCoord.y) * uImageRect.w);
                vec2 p = vec2((uv.x - uCenter.x) * uAspect, uv.y - uCenter.y);
                float cs = cos(uAngle), sn = sin(uAngle);
                float t;
                if (uShape == 0) {
                    float d = dot(p, vec2(cs, sn));
                    t = clamp((d - uLinearBounds.x) /
                        max(uLinearBounds.y - uLinearBounds.x, 0.0001), 0.0, 1.0);
                } else {
                    vec2 q = vec2(p.x * cs + p.y * sn, -p.x * sn + p.y * cs);
                    float radius = length(q / max(uRadii, vec2(0.0001)));
                    t = clamp((radius - uInnerScale) /
                        max(1.0 - uInnerScale, 0.0001), 0.0, 1.0);
                }
                float weight = 1.0 - t * t * (3.0 - 2.0 * t);
                if (uInvert != 0) weight = 1.0 - weight;
                if (uHasCoverage != 0) weight = clamp(texture(uCoverage, vTexCoord).r, 0.0, 1.0);
                if (uEnabled == 0) weight = 0.0;
                if (uEvMode == 4) { FragColor = vec4(weight, weight, weight, 1.0); return; }
                vec4 original = texture(uOriginal, vTexCoord);
                vec4 processed = texture(uProcessed, vTexCoord);
                if (uEvMode == 3) {
                    FragColor = vec4(original.rgb * exp2(clamp(processed.r, -24.0, 24.0)), original.a);
                } else if (uEvMode != 0) {
                    vec3 reference = max(texture(uEvReference, vTexCoord).rgb, vec3(0.0));
                    float baseSum = reference.r + reference.g + reference.b;
                    float adjustedSum = max(processed.r, 0.0) +
                        max(processed.g, 0.0) + max(processed.b, 0.0);
                    float deltaEv = baseSum > 0.0
                        ? log2(max(adjustedSum / baseSum, 0.00000001)) : 0.0;
                    float accumulatedEv = uEvMode == 1 ? 0.0 : original.r;
                    FragColor = vec4(accumulatedEv + deltaEv * weight, 0.0, 0.0, 1.0);
                } else {
                    FragColor = mix(original, processed, weight);
                }
            }
        )";
        m_RawGradientBlendProgram = GLHelpers::CreateShaderProgram(vertex, fragment);
    }
    if (!m_RawGradientBlendProgram) return 0;
    const unsigned int output = m_RawZoneAreasFloatOutput
        ? GLHelpers::CreateStorageTexture(m_Width, m_Height, GL_RGBA32F)
        : CreateGraphRenderTargetTexture();
    if (!output) return 0;
    const bool rendered = RenderIntoGraphTargetTexture(output, [&](unsigned int) {
        glUseProgram(m_RawGradientBlendProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, originalTexture);
        glUniform1i(glGetUniformLocation(m_RawGradientBlendProgram, "uOriginal"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, processedTexture);
        glUniform1i(glGetUniformLocation(m_RawGradientBlendProgram, "uProcessed"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, evReferenceTexture);
        glUniform1i(glGetUniformLocation(m_RawGradientBlendProgram, "uEvReference"), 2);
        glUniform1i(glGetUniformLocation(m_RawGradientBlendProgram, "uEvMode"), evMode);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, coverageTexture);
        glUniform1i(glGetUniformLocation(m_RawGradientBlendProgram, "uCoverage"), 3);
        glUniform1i(glGetUniformLocation(m_RawGradientBlendProgram, "uHasCoverage"), coverageTexture ? 1 : 0);
        glUniform1i(glGetUniformLocation(m_RawGradientBlendProgram, "uEnabled"), mask.enabled ? 1 : 0);
        const auto& region = m_RawViewportAppliedRegion;
        if (region.Valid()) {
            glUniform4f(glGetUniformLocation(m_RawGradientBlendProgram, "uImageRect"),
                float(region.x) / region.fullWidth, float(region.y) / region.fullHeight,
                float(region.width) / region.fullWidth, float(region.height) / region.fullHeight);
        } else {
            glUniform4f(glGetUniformLocation(m_RawGradientBlendProgram, "uImageRect"),
                0.0f, 0.0f, 1.0f, 1.0f);
        }
        const float aspect = region.Valid()
            ? float(region.fullWidth) / region.fullHeight : float(m_Width) / m_Height;
        glUniform1f(glGetUniformLocation(m_RawGradientBlendProgram, "uAspect"), aspect);
        glUniform2f(glGetUniformLocation(m_RawGradientBlendProgram, "uCenter"),
            mask.centerU, mask.centerV);
        glUniform1f(glGetUniformLocation(m_RawGradientBlendProgram, "uAngle"),
            mask.angleRadians);
        glUniform2f(glGetUniformLocation(m_RawGradientBlendProgram, "uLinearBounds"),
            mask.lowBoundary, mask.highBoundary);
        glUniform2f(glGetUniformLocation(m_RawGradientBlendProgram, "uRadii"),
            mask.radiusX, mask.radiusY);
        glUniform1f(glGetUniformLocation(m_RawGradientBlendProgram, "uInnerScale"),
            mask.innerScale);
        glUniform1i(glGetUniformLocation(m_RawGradientBlendProgram, "uShape"),
            mask.shape == Stack::RawRecipe::RawGradientShape::Linear ? 0 : 1);
        glUniform1i(glGetUniformLocation(m_RawGradientBlendProgram, "uInvert"),
            mask.inverted ? 1 : 0);
        m_Quad.Draw();
        glActiveTexture(GL_TEXTURE0);
        glUseProgram(0);
    });
    if (!rendered) { glDeleteTextures(1, &output); return 0; }
    return output;
}

bool RenderPipeline::RenderMaskCombine(unsigned int maskA, unsigned int maskB, RenderMaskCombineMode mode, unsigned int targetFBO) {
    EnsureMaskPrograms();
    if (!m_MaskCombineProgram || !maskA || !maskB || !targetFBO) {
        return false;
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
    return true;
}

bool RenderPipeline::RenderMixBlend(unsigned int textureA, unsigned int textureB, unsigned int factorTexture, float factor, RenderMixBlendMode mode, unsigned int targetFBO) {
    EnsureMixProgram();
    if (!m_MixProgram || !textureA || !textureB || !targetFBO) {
        return false;
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
    return true;
}

bool RenderPipeline::RenderTechnicalImage(
    unsigned int texture,
    Stack::NodeMath::TechnicalImageOperation operation,
    float exposureValue,
    unsigned int targetFBO) {
    EnsureTechnicalImageProgram();
    if (!m_TechnicalImageProgram || !texture || !targetFBO) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_TechnicalImageProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(glGetUniformLocation(m_TechnicalImageProgram, "uImage"), 0);
    glUniform1i(glGetUniformLocation(m_TechnicalImageProgram, "uOperation"), static_cast<int>(operation));
    glUniform1f(glGetUniformLocation(m_TechnicalImageProgram, "uExposureValue"), exposureValue);
    m_Quad.Draw();
    return true;
}

bool RenderPipeline::RenderReformat(
    unsigned int texture,
    int inputWidth,
    int inputHeight,
    const Stack::NodeMath::ReformatSettings& settings,
    unsigned int targetFBO) {
    EnsureReformatProgram();
    if (!m_ReformatProgram || !texture || inputWidth <= 0 || inputHeight <= 0 || targetFBO == 0) {
        return false;
    }
    while (glGetError() != GL_NO_ERROR) {}
    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_ReformatProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(glGetUniformLocation(m_ReformatProgram, "uImage"), 0);
    glUniform2i(glGetUniformLocation(m_ReformatProgram, "uInputSize"), inputWidth, inputHeight);
    glUniform1i(
        glGetUniformLocation(m_ReformatProgram, "uFilter"),
        settings.filter == Stack::NodeMath::ReconstructionFilter::Nearest ? 0 : 1);
    m_Quad.Draw();
    return glGetError() == GL_NO_ERROR;
}

bool RenderPipeline::RenderDataMath(
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
    if (!m_DataMathProgram || !targetFBO ||
        (!textureA && hasA) || (!textureB && hasB)) {
        return false;
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
    return true;
}

bool RenderPipeline::RenderMaskUtility(unsigned int inputMask, const RenderGraphNode& node, unsigned int targetFBO) {
    EnsureUtilityPrograms();
    if (!m_MaskUtilityProgram || !inputMask || !targetFBO) {
        return false;
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
    glUniform1i(glGetUniformLocation(m_MaskUtilityProgram, "uEnabled"), node.maskUtilitySettings.enabled ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_MaskUtilityProgram, "uInvert"), node.maskUtilitySettings.invert ? 1 : 0);
    m_Quad.Draw();
    glActiveTexture(GL_TEXTURE0);
    return true;
}

bool RenderPipeline::RenderImageToMask(unsigned int inputImage, const RenderGraphNode& node, unsigned int targetFBO) {
    EnsureUtilityPrograms();
    if (!m_ImageToMaskProgram || !inputImage || !targetFBO) {
        return false;
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
    return true;
}


bool RenderPipeline::RenderChannelSplit(unsigned int inputTexture, int channel, unsigned int targetFBO) {
    EnsureChannelPrograms();
    if (!m_ChannelSplitProgram || !inputTexture || !targetFBO) {
        return false;
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
    return true;
}

bool RenderPipeline::RenderChannelCombine(unsigned int texR, unsigned int texG, unsigned int texB, unsigned int texA,
                                         bool hasR, bool hasG, bool hasB, bool hasA, unsigned int targetFBO) {
    EnsureChannelPrograms();
    if (!m_ChannelCombineProgram || !targetFBO ||
        (hasR && !texR) || (hasG && !texG) ||
        (hasB && !texB) || (hasA && !texA)) {
        return false;
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
    return true;
}
