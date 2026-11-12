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
constexpr float kExposureShoulderKneeEv = 2.f;
constexpr float kExposureShoulderWidthEv = 5.f;
}

unsigned int RenderPipeline::EnsureRawDevelopmentExposureProgram(bool protectHighlights) {
    auto& program = protectHighlights ? m_RawDevelopmentProtectedExposureProgram : m_RawDevelopmentExposureProgram;
    if (program) return program;
    static const char* fragment = R"(
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform float uExposureScale;
        uniform bool uCalibrationActive;
        uniform mat3 uCalibration;
        uniform ivec2 uOutputSize;

        #ifdef PROTECT_HIGHLIGHTS
        uniform vec3 uExposureLuma;
        uniform vec4 uExposureShoulder; // Requested EV, knee EV, width EV, lower luminance cutoff.

        float exposureGain(vec3 rgb) {
            float luminance = dot(rgb, uExposureLuma);
            if (!(luminance > uExposureShoulder.w)) return uExposureScale;
            float requestedEv = uExposureShoulder.x;
            float kneeEv = uExposureShoulder.y;
            float widthEv = uExposureShoulder.z;
            float inputEv = log2(luminance) - log2(0.18);
            float shoulderEv;
            if (inputEv < kneeEv) {
                float excess = max(0.0, inputEv + requestedEv - kneeEv);
                shoulderEv = requestedEv - excess * (excess / (widthEv + excess));
            } else {
                float gap = max(0.0, kneeEv + widthEv - inputEv);
                shoulderEv = requestedEv * gap * gap / (widthEv * widthEv + requestedEv * gap);
            }
            // Retain half of the requested EV even above the shoulder. Both
            // mappings preserve tone order, and their slopes blend smoothly.
            return exp2(mix(requestedEv, shoulderEv, 0.5));
        }
        #endif

        void main() {
            // Exposure preserves the pixel grid, including on native crops.
            vec4 color = all(equal(textureSize(uInputImage, 0), uOutputSize))
                ? texelFetch(uInputImage, ivec2(gl_FragCoord.xy), 0)
                : texture(uInputImage, vTexCoord);
            if (uCalibrationActive) color.rgb = uCalibration * color.rgb;
            #ifdef PROTECT_HIGHLIGHTS
            FragColor = vec4(color.rgb * exposureGain(color.rgb), color.a);
            #else
            FragColor = vec4(color.rgb * uExposureScale, color.a);
            #endif
        }
    )";
    std::string source = "#version 330 core\n";
    if (protectHighlights) source += "#define PROTECT_HIGHLIGHTS\n";
    source += fragment;
    program = GLHelpers::CreateShaderProgram(kFullscreenVertexShader, source.c_str());
    return program;
}

unsigned int RenderPipeline::RenderRawDevelopmentExposure(
    unsigned int inputTexture,
    float exposureEv,
    const Stack::RawRecipe::RawColorCalibrationTransform* calibration,
    std::optional<Raw::RawWorkingSpace> protectedWorkingSpace) {
    if (inputTexture == 0 || m_Width <= 0 || m_Height <= 0) {
        return 0;
    }
    if (!std::isfinite(exposureEv) || exposureEv < -32.f || exposureEv > 32.f) return 0;
    const bool protectHighlights = protectedWorkingSpace.has_value() && exposureEv > 0.f;
    const auto program = EnsureRawDevelopmentExposureProgram(protectHighlights);
    if (!program) return 0;
    unsigned int outputTexture = GLHelpers::CreateStorageTexture(m_Width,m_Height,GL_RGBA32F);
    const float exposureScale=std::exp2(exposureEv);
    const bool rendered =
        outputTexture != 0 &&
        RenderIntoGraphTargetTexture(outputTexture, [&](unsigned int) {
            glUseProgram(program);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, inputTexture);
            glUniform1i(
                glGetUniformLocation(program, "uInputImage"),
                0);
            glUniform1f(
                glGetUniformLocation(program, "uExposureScale"),
                exposureScale);
            const bool calibrated = calibration && calibration->active;
            glUniform1i(glGetUniformLocation(program, "uCalibrationActive"), calibrated);
            if (calibrated)
                glUniformMatrix3fv(glGetUniformLocation(program, "uCalibration"),
                    1, GL_TRUE, calibration->matrix.data());
            glUniform2i(
                glGetUniformLocation(program, "uOutputSize"),
                m_Width, m_Height);
            if (protectHighlights) {
                const auto lumaLocation = glGetUniformLocation(program, "uExposureLuma");
                if (*protectedWorkingSpace == Raw::RawWorkingSpace::LinearRec2020D65)
                    glUniform3f(lumaLocation, .2627002f, .6779981f, .0593017f);
                else
                    glUniform3f(lumaLocation, .2126729f, .7151522f, .0721750f);
                const float cutoff = .18f * std::exp2(kExposureShoulderKneeEv - exposureEv);
                glUniform4f(glGetUniformLocation(program, "uExposureShoulder"), exposureEv,
                    kExposureShoulderKneeEv, kExposureShoulderWidthEv, cutoff);
            }
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
