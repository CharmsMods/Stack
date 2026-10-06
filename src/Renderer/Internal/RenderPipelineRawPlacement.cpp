#include "Renderer/RenderPipeline.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr const char* kFullscreenVertexShader = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    layout (location = 1) in vec2 aTex;
    out vec2 vTexCoord;
    void main() { vTexCoord = aTex; gl_Position = vec4(aPos, 0.0, 1.0); }
)";
}

void RenderPipeline::EnsureRawDevelopmentExposureProgram() {
    static const char* fragment = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform float uExposureScale;
        uniform bool uCalibrationActive;
        uniform mat3 uCalibration;
        uniform ivec2 uOutputSize;

        void main() {
            // Exposure preserves the pixel grid, including on native crops.
            vec4 color = all(equal(textureSize(uInputImage, 0), uOutputSize))
                ? texelFetch(uInputImage, ivec2(gl_FragCoord.xy), 0)
                : texture(uInputImage, vTexCoord);
            if (uCalibrationActive) color.rgb = uCalibration * color.rgb;
            FragColor = vec4(color.rgb * uExposureScale, color.a);
        }
    )";
    if (!m_RawDevelopmentExposureProgram) {
        m_RawDevelopmentExposureProgram =
            GLHelpers::CreateShaderProgram(kFullscreenVertexShader, fragment);
    }
}

unsigned int RenderPipeline::RenderRawDevelopmentExposure(
    unsigned int inputTexture,
    float exposureEv,
    const Stack::RawRecipe::RawColorCalibrationTransform* calibration) {
    if (inputTexture == 0 || m_Width <= 0 || m_Height <= 0) {
        return 0;
    }
    EnsureRawDevelopmentExposureProgram();
    if (!m_RawDevelopmentExposureProgram) {
        return 0;
    }

    unsigned int outputTexture = m_RawZoneAreasFloatOutput
        ? GLHelpers::CreateStorageTexture(m_Width, m_Height, GL_RGBA32F)
        : CreateGraphRenderTargetTexture();
    const float exposureScale =
        std::exp2(std::clamp(exposureEv, -24.0f, 24.0f));
    const bool rendered =
        outputTexture != 0 &&
        RenderIntoGraphTargetTexture(outputTexture, [&](unsigned int) {
            glUseProgram(m_RawDevelopmentExposureProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, inputTexture);
            glUniform1i(
                glGetUniformLocation(m_RawDevelopmentExposureProgram, "uInputImage"),
                0);
            glUniform1f(
                glGetUniformLocation(m_RawDevelopmentExposureProgram, "uExposureScale"),
                exposureScale);
            const bool calibrated = calibration && calibration->active;
            glUniform1i(glGetUniformLocation(m_RawDevelopmentExposureProgram, "uCalibrationActive"), calibrated);
            if (calibrated)
                glUniformMatrix3fv(glGetUniformLocation(m_RawDevelopmentExposureProgram, "uCalibration"),
                    1, GL_TRUE, calibration->matrix.data());
            glUniform2i(
                glGetUniformLocation(m_RawDevelopmentExposureProgram, "uOutputSize"),
                m_Width, m_Height);
            m_Quad.Draw();
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
        });
    if (!rendered) {
        if (outputTexture != 0) {
            glDeleteTextures(1, &outputTexture);
        }
        return 0;
    }
    return outputTexture;
}
