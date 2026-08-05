#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Utils/PixelBufferUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

using namespace Stack::Renderer::GraphExecution;

namespace {

#ifndef GL_RG32F
#define GL_RG32F 0x8230
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif

constexpr int kMaximumNotches = 16;
constexpr float kPi = 3.14159265358979323846f;

unsigned int CreateComplexTexture(int width, int height) {
    const unsigned int texture =
        GLHelpers::CreateStorageTexture(width, height, GL_RG32F);
    if (texture == 0) return 0;
    const Stack::Renderer::GLState::TextureBinding savedTexture(
        GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    savedTexture.Restore();
    return texture;
}

template <typename RenderFn>
bool RenderIntoSizedTexture(
    unsigned int texture,
    int width,
    int height,
    RenderFn&& renderFn) {
    if (texture == 0 || width <= 0 || height <= 0) return false;
    const ScopedFramebufferState savedState(true);
    const unsigned int fbo = GLHelpers::CreateFBO(texture);
    if (fbo == 0) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT);
    while (glGetError() != GL_NO_ERROR) {}
    renderFn(fbo);
    const bool ok = glGetError() == GL_NO_ERROR;
    savedState.Restore(true);
    glDeleteFramebuffers(1, &fbo);
    return ok;
}

Stack::Renderer::Frequency::FftEdgePolicy ToFftEdgePolicy(RenderFrequencyEdgePolicy policy) {
    switch (policy) {
        case RenderFrequencyEdgePolicy::Mirror:
            return Stack::Renderer::Frequency::FftEdgePolicy::Mirror;
        case RenderFrequencyEdgePolicy::Wrap:
            return Stack::Renderer::Frequency::FftEdgePolicy::Wrap;
        case RenderFrequencyEdgePolicy::ZeroPad:
            return Stack::Renderer::Frequency::FftEdgePolicy::ZeroPad;
    }
    return Stack::Renderer::Frequency::FftEdgePolicy::Mirror;
}

bool CompatibleFrequencyResources(
    const RenderFrequencyResource& a,
    const RenderFrequencyResource& b) {
    return a.valid && b.valid &&
        a.sourceWidth == b.sourceWidth &&
        a.sourceHeight == b.sourceHeight &&
        a.paddedWidth == b.paddedWidth &&
        a.paddedHeight == b.paddedHeight &&
        a.paddingOriginX == b.paddingOriginX &&
        a.paddingOriginY == b.paddingOriginY &&
        a.edgePolicy == b.edgePolicy &&
        a.sourceRole == b.sourceRole &&
        a.normalization == b.normalization &&
        a.precision == b.precision &&
        a.coordinateConvention == b.coordinateConvention;
}

} // namespace

void RenderPipeline::EnsureFrequencyPrograms() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* spectrumViewFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uSpectrum;
        uniform int uMode;
        uniform int uLut;
        uniform float uExposure;
        uniform float uGamma;
        uniform int uCenterDc;
        const float PI = 3.14159265358979323846;

        vec3 turbo(float x) {
            x = clamp(x, 0.0, 1.0);
            vec4 v = vec4(1.0, x, x * x, x * x * x);
            return clamp(vec3(
                dot(v, vec4(0.13572138, 4.61539260, -42.66032258, 132.13108234)) + 59.28637943 * pow(x, 4.0),
                dot(v, vec4(0.09140261, 2.19418839, 4.84296658, -14.18503333)) + 4.27729857 * pow(x, 4.0),
                dot(v, vec4(0.10667330, 12.64194608, -60.58204836, 110.36276771)) - 26.10412138 * pow(x, 4.0)),
                0.0, 1.0);
        }
        vec3 viridis(float x) {
            vec3 a = vec3(0.267, 0.005, 0.329);
            vec3 b = vec3(0.128, 0.567, 0.551);
            vec3 c = vec3(0.993, 0.906, 0.144);
            return mix(mix(a, b, smoothstep(0.0, 0.72, x)), c, smoothstep(0.55, 1.0, x));
        }
        vec3 inferno(float x) {
            vec3 a = vec3(0.002, 0.001, 0.014);
            vec3 b = vec3(0.520, 0.046, 0.510);
            vec3 c = vec3(0.988, 0.998, 0.645);
            return mix(mix(a, b, smoothstep(0.0, 0.62, x)), c, smoothstep(0.48, 1.0, x));
        }

        void main() {
            vec2 uv = uCenterDc != 0 ? fract(vTexCoord + vec2(0.5)) : vTexCoord;
            vec2 c = texture(uSpectrum, uv).rg;
            float value;
            if (uMode == 0) {
                value = log(1.0 + length(c)) * max(uExposure, 0.001) * 0.18;
            } else if (uMode == 1) {
                value = (atan(c.y, c.x) + PI) / (2.0 * PI);
            } else {
                float signedValue = uMode == 2 ? c.x : c.y;
                value = 0.5 + 0.5 * tanh(signedValue * max(uExposure, 0.001));
            }
            value = pow(clamp(value, 0.0, 1.0), 1.0 / max(uGamma, 0.001));
            vec3 color = vec3(value);
            if (uLut == 0) color = turbo(value);
            else if (uLut == 1) color = viridis(value);
            else if (uLut == 2) color = inferno(value);
            FragColor = vec4(color, 1.0);
        }
    )";

    static const char* frequencyResponseFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform vec2 uSize;
        uniform int uMode;
        uniform int uProfile;
        uniform float uLowCutoff;
        uniform float uHighCutoff;
        uniform float uTransitionWidth;
        uniform float uOrder;
        uniform int uNotchCount;
        uniform vec3 uNotches[16];
        const float PI = 3.14159265358979323846;

        float lowPass(float radius, float cutoff) {
            cutoff = clamp(cutoff, 0.000001, 0.70710678);
            float transition = max(uTransitionWidth, 0.000001);
            if (uProfile == 0) {
                return 1.0 - smoothstep(cutoff - transition * 0.5, cutoff + transition * 0.5, radius);
            }
            if (uProfile == 1) {
                float sigma = cutoff / sqrt(2.0 * log(2.0));
                return exp(-0.5 * radius * radius / max(sigma * sigma, 0.0000001));
            }
            if (uProfile == 2) {
                return 1.0 / (1.0 + pow(radius / cutoff, 2.0 * clamp(uOrder, 1.0, 12.0)));
            }
            return radius <= cutoff ? 1.0 : 0.0;
        }

        void main() {
            vec2 index = gl_FragCoord.xy - vec2(0.5);
            vec2 frequency = vec2(
                index.x <= uSize.x * 0.5 ? index.x / uSize.x : (index.x - uSize.x) / uSize.x,
                index.y <= uSize.y * 0.5 ? index.y / uSize.y : (index.y - uSize.y) / uSize.y);
            float radius = length(frequency);
            float low = lowPass(radius, uLowCutoff);
            float high = 1.0 - lowPass(radius, uHighCutoff);
            float value = 1.0;
            if (uMode == 1) value = low;
            else if (uMode == 2) value = 1.0 - lowPass(radius, uLowCutoff);
            else if (uMode == 3) value = (1.0 - lowPass(radius, uLowCutoff)) * lowPass(radius, uHighCutoff);
            else if (uMode == 4) value = 1.0 - ((1.0 - lowPass(radius, uLowCutoff)) * lowPass(radius, uHighCutoff));

            if (uMode == 5) {
                value = 1.0;
                for (int i = 0; i < 16; ++i) {
                    if (i >= uNotchCount) break;
                    float angle = radians(uNotches[i].y);
                    vec2 center = uNotches[i].x * vec2(cos(angle), sin(angle));
                    float distanceToPair = min(distance(frequency, center), distance(frequency, -center));
                    float width = max(uNotches[i].z, 0.000001);
                    float rejection;
                    if (uProfile == 1) {
                        rejection = 1.0 - exp(-0.5 * distanceToPair * distanceToPair / (width * width));
                    } else if (uProfile == 2) {
                        rejection = 1.0 / (1.0 + pow(width / max(distanceToPair, 0.000001), 2.0 * clamp(uOrder, 1.0, 12.0)));
                    } else if (uProfile == 3) {
                        rejection = distanceToPair >= width ? 1.0 : 0.0;
                    } else {
                        rejection = smoothstep(width, width + max(uTransitionWidth, 0.000001), distanceToPair);
                    }
                    value *= rejection;
                }
            }
            FragColor = vec4(clamp(value, 0.0, 1.0));
        }
    )";

    static const char* spectrumMathFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uSpectrumA;
        uniform sampler2D uSpectrumB;
        uniform sampler2D uResponse;
        uniform int uMode;
        uniform float uStrength;
        void main() {
            vec2 a = texture(uSpectrumA, vTexCoord).rg;
            vec2 outputValue = a;
            if (uMode == 0) {
                float response = clamp(texture(uResponse, vTexCoord).r, 0.0, 1.0);
                outputValue = a * mix(1.0, response, clamp(uStrength, 0.0, 1.0));
            } else {
                vec2 b = texture(uSpectrumB, vTexCoord).rg;
                outputValue = uMode == 1 ? a + b : a - b;
            }
            FragColor = vec4(outputValue, 0.0, 1.0);
        }
    )";

    static const char* magnitudePhaseFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uSpectrum;
        uniform sampler2D uMagnitude;
        uniform sampler2D uPhase;
        uniform int uMode;
        void main() {
            if (uMode == 2) {
                float magnitude = max(texture(uMagnitude, vTexCoord).r, 0.0);
                float phase = texture(uPhase, vTexCoord).r;
                FragColor = vec4(vec2(cos(phase), sin(phase)) * magnitude, 0.0, 1.0);
                return;
            }
            vec2 c = texture(uSpectrum, vTexCoord).rg;
            float rawValue = uMode == 0 ? length(c) : atan(c.y, c.x);
            FragColor = vec4(rawValue, 0.0, 0.0, 1.0);
        }
    )";

    static const char* ifftProjectFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uSpatial;
        uniform ivec2 uPaddingOrigin;
        void main() {
            ivec2 sourcePixel = ivec2(gl_FragCoord.xy);
            float value = texelFetch(uSpatial, sourcePixel + uPaddingOrigin, 0).r;
            FragColor = vec4(value, value, value, 1.0);
        }
    )";

    if (!m_SpectrumViewProgram)
        m_SpectrumViewProgram = GLHelpers::CreateShaderProgram(vertexSrc, spectrumViewFragSrc);
    if (!m_FrequencyMaskProgram)
        m_FrequencyMaskProgram = GLHelpers::CreateShaderProgram(vertexSrc, frequencyResponseFragSrc);
    if (!m_SpectrumMathProgram)
        m_SpectrumMathProgram = GLHelpers::CreateShaderProgram(vertexSrc, spectrumMathFragSrc);
    if (!m_MagnitudePhaseProgram)
        m_MagnitudePhaseProgram = GLHelpers::CreateShaderProgram(vertexSrc, magnitudePhaseFragSrc);
    if (!m_FrequencyIfftProjectProgram)
        m_FrequencyIfftProjectProgram = GLHelpers::CreateShaderProgram(vertexSrc, ifftProjectFragSrc);
}

RenderFrequencyResource RenderPipeline::RenderFourierTransform(
    unsigned int channelTexture,
    int sourceWidth,
    int sourceHeight,
    RenderFrequencyEdgePolicy edgePolicy,
    std::string sourceRole) {
    RenderFrequencyResource result;
    if (channelTexture == 0 || sourceWidth <= 0 || sourceHeight <= 0) return result;
    result.sourceWidth = sourceWidth;
    result.sourceHeight = sourceHeight;
    result.paddedWidth = Stack::Renderer::Frequency::GpuFft::NextPowerOfTwo(sourceWidth);
    result.paddedHeight = Stack::Renderer::Frequency::GpuFft::NextPowerOfTwo(sourceHeight);
    result.paddingOriginX = (result.paddedWidth - sourceWidth) / 2;
    result.paddingOriginY = (result.paddedHeight - sourceHeight) / 2;
    result.edgePolicy = edgePolicy;
    result.sourceRole = std::move(sourceRole);
    result.kind = RenderFrequencyResourceKind::Spectrum;
    result.hermitian = true;
    result.texture = m_GpuFft.Forward(
        channelTexture,
        sourceWidth,
        sourceHeight,
        result.paddedWidth,
        result.paddedHeight,
        result.paddingOriginX,
        result.paddingOriginY,
        ToFftEdgePolicy(edgePolicy));
    result.valid = result.texture != 0;
    return result;
}

RenderPipeline::GraphNodeRenderResult RenderPipeline::RenderInverseFourierTransform(
    const RenderFrequencyResource& spectrum) {
    GraphNodeRenderResult result;
    if (!spectrum.valid || spectrum.texture == 0 ||
        spectrum.kind != RenderFrequencyResourceKind::Spectrum ||
        !spectrum.hermitian ||
        spectrum.sourceWidth <= 0 || spectrum.sourceHeight <= 0) {
        return result;
    }
    EnsureFrequencyPrograms();
    const unsigned int spatial = m_GpuFft.Inverse(
        spectrum.texture, spectrum.paddedWidth, spectrum.paddedHeight);
    if (spatial == 0) return result;
    if (m_FrequencyIfftProjectProgram == 0) {
        glDeleteTextures(1, &spatial);
        return result;
    }
    result.texture = GLHelpers::CreateEmptyTexture(spectrum.sourceWidth, spectrum.sourceHeight);
    const bool rendered = RenderIntoSizedTexture(
        result.texture, spectrum.sourceWidth, spectrum.sourceHeight, [&](unsigned int) {
            glUseProgram(m_FrequencyIfftProjectProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, spatial);
            glUniform1i(glGetUniformLocation(m_FrequencyIfftProjectProgram, "uSpatial"), 0);
            glUniform2i(
                glGetUniformLocation(m_FrequencyIfftProjectProgram, "uPaddingOrigin"),
                spectrum.paddingOriginX,
                spectrum.paddingOriginY);
            m_Quad.Draw();
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
        });
    glDeleteTextures(1, &spatial);
    if (!rendered && result.texture != 0) {
        glDeleteTextures(1, &result.texture);
        result.texture = 0;
    }
    result.owned = result.texture != 0;
    return result;
}

unsigned int RenderPipeline::RenderFrequencyResponseTexture(
    const RenderFrequencyResponseSettings& settings,
    int paddedWidth,
    int paddedHeight) {
    EnsureFrequencyPrograms();
    if (m_FrequencyMaskProgram == 0 || paddedWidth <= 0 || paddedHeight <= 0) return 0;
    unsigned int result = CreateComplexTexture(paddedWidth, paddedHeight);
    std::array<float, static_cast<std::size_t>(kMaximumNotches) * 3u>
        notchData {};
    const int notchCount = std::min<int>(
        static_cast<int>(settings.notches.size()), kMaximumNotches);
    for (int i = 0; i < notchCount; ++i) {
        notchData[static_cast<std::size_t>(i) * 3u] =
            std::clamp(settings.notches[static_cast<std::size_t>(i)].frequency, 0.0f, 0.70710678f);
        notchData[static_cast<std::size_t>(i) * 3u + 1u] =
            settings.notches[static_cast<std::size_t>(i)].directionDegrees;
        notchData[static_cast<std::size_t>(i) * 3u + 2u] =
            std::max(settings.notches[static_cast<std::size_t>(i)].width, 0.000001f);
    }
    const bool rendered = RenderIntoSizedTexture(result, paddedWidth, paddedHeight, [&](unsigned int) {
        glUseProgram(m_FrequencyMaskProgram);
        glUniform2f(glGetUniformLocation(m_FrequencyMaskProgram, "uSize"),
            static_cast<float>(paddedWidth), static_cast<float>(paddedHeight));
        glUniform1i(glGetUniformLocation(m_FrequencyMaskProgram, "uMode"),
            static_cast<int>(settings.mode));
        glUniform1i(glGetUniformLocation(m_FrequencyMaskProgram, "uProfile"),
            static_cast<int>(settings.profile));
        glUniform1f(glGetUniformLocation(m_FrequencyMaskProgram, "uLowCutoff"), settings.lowCutoff);
        glUniform1f(glGetUniformLocation(m_FrequencyMaskProgram, "uHighCutoff"), settings.highCutoff);
        glUniform1f(glGetUniformLocation(m_FrequencyMaskProgram, "uTransitionWidth"), settings.transitionWidth);
        glUniform1f(glGetUniformLocation(m_FrequencyMaskProgram, "uOrder"), settings.butterworthOrder);
        glUniform1i(glGetUniformLocation(m_FrequencyMaskProgram, "uNotchCount"), notchCount);
        glUniform3fv(glGetUniformLocation(m_FrequencyMaskProgram, "uNotches"),
            kMaximumNotches, notchData.data());
        m_Quad.Draw();
        glUseProgram(0);
    });
    if (!rendered && result != 0) {
        glDeleteTextures(1, &result);
        result = 0;
    }
    return result;
}

RenderFrequencyResource RenderPipeline::RenderApplyFrequencyResponse(
    const RenderFrequencyResource& spectrum,
    const RenderFrequencyResponseSettings& response,
    float strength) {
    RenderFrequencyResource result = spectrum;
    result.texture = 0;
    result.valid = false;
    if (!spectrum.valid || spectrum.texture == 0 ||
        spectrum.kind != RenderFrequencyResourceKind::Spectrum) return result;
    EnsureFrequencyPrograms();
    if (m_SpectrumMathProgram == 0) return result;
    const unsigned int responseTexture = RenderFrequencyResponseTexture(
        response, spectrum.paddedWidth, spectrum.paddedHeight);
    if (responseTexture == 0) return result;
    const float canonicalStrength = std::isfinite(strength)
        ? std::clamp(strength, 0.0f, 1.0f)
        : 1.0f;
    result.texture = CreateComplexTexture(spectrum.paddedWidth, spectrum.paddedHeight);
    const bool rendered = RenderIntoSizedTexture(
        result.texture, spectrum.paddedWidth, spectrum.paddedHeight, [&](unsigned int) {
            glUseProgram(m_SpectrumMathProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, spectrum.texture);
            glUniform1i(glGetUniformLocation(m_SpectrumMathProgram, "uSpectrumA"), 0);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, responseTexture);
            glUniform1i(glGetUniformLocation(m_SpectrumMathProgram, "uResponse"), 2);
            glUniform1i(glGetUniformLocation(m_SpectrumMathProgram, "uMode"), 0);
            glUniform1f(glGetUniformLocation(m_SpectrumMathProgram, "uStrength"),
                canonicalStrength);
            m_Quad.Draw();
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
        });
    glDeleteTextures(1, &responseTexture);
    if (!rendered && result.texture != 0) {
        glDeleteTextures(1, &result.texture);
        result.texture = 0;
    }
    result.valid = result.texture != 0;
    result.hermitian = spectrum.hermitian;
    return result;
}

RenderFrequencyResource RenderPipeline::RenderCombineSpectra(
    const RenderFrequencyResource& a,
    const RenderFrequencyResource& b,
    RenderSpectrumCombineMode mode) {
    RenderFrequencyResource result = a;
    result.texture = 0;
    result.valid = false;
    if (a.kind != RenderFrequencyResourceKind::Spectrum ||
        b.kind != RenderFrequencyResourceKind::Spectrum ||
        !CompatibleFrequencyResources(a, b)) return result;
    EnsureFrequencyPrograms();
    if (m_SpectrumMathProgram == 0) return result;
    result.texture = CreateComplexTexture(a.paddedWidth, a.paddedHeight);
    const bool rendered = RenderIntoSizedTexture(
        result.texture, a.paddedWidth, a.paddedHeight, [&](unsigned int) {
            glUseProgram(m_SpectrumMathProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, a.texture);
            glUniform1i(glGetUniformLocation(m_SpectrumMathProgram, "uSpectrumA"), 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, b.texture);
            glUniform1i(glGetUniformLocation(m_SpectrumMathProgram, "uSpectrumB"), 1);
            glUniform1i(glGetUniformLocation(m_SpectrumMathProgram, "uMode"),
                mode == RenderSpectrumCombineMode::Add ? 1 : 2);
            m_Quad.Draw();
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
        });
    if (!rendered && result.texture != 0) {
        glDeleteTextures(1, &result.texture);
        result.texture = 0;
    }
    result.valid = result.texture != 0;
    result.hermitian = a.hermitian && b.hermitian;
    return result;
}

RenderFrequencyResource RenderPipeline::RenderSpectrumComponent(
    const RenderFrequencyResource& spectrum,
    RenderFrequencyResourceKind componentKind) {
    RenderFrequencyResource result = spectrum;
    result.texture = 0;
    result.valid = false;
    result.kind = componentKind;
    if (!spectrum.valid || spectrum.kind != RenderFrequencyResourceKind::Spectrum ||
        (componentKind != RenderFrequencyResourceKind::Magnitude &&
         componentKind != RenderFrequencyResourceKind::Phase)) return result;
    EnsureFrequencyPrograms();
    if (m_MagnitudePhaseProgram == 0) return result;
    result.texture = CreateComplexTexture(spectrum.paddedWidth, spectrum.paddedHeight);
    const bool rendered = RenderIntoSizedTexture(
        result.texture, spectrum.paddedWidth, spectrum.paddedHeight, [&](unsigned int) {
            glUseProgram(m_MagnitudePhaseProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, spectrum.texture);
            glUniform1i(glGetUniformLocation(m_MagnitudePhaseProgram, "uSpectrum"), 0);
            glUniform1i(glGetUniformLocation(m_MagnitudePhaseProgram, "uMode"),
                componentKind == RenderFrequencyResourceKind::Magnitude ? 0 : 1);
            m_Quad.Draw();
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
        });
    if (!rendered && result.texture != 0) {
        glDeleteTextures(1, &result.texture);
        result.texture = 0;
    }
    result.valid = result.texture != 0;
    return result;
}

RenderFrequencyResource RenderPipeline::RenderRecombineSpectrum(
    const RenderFrequencyResource& magnitude,
    const RenderFrequencyResource& phase) {
    RenderFrequencyResource result = magnitude;
    result.texture = 0;
    result.kind = RenderFrequencyResourceKind::Spectrum;
    result.valid = false;
    if (magnitude.kind != RenderFrequencyResourceKind::Magnitude ||
        phase.kind != RenderFrequencyResourceKind::Phase ||
        !CompatibleFrequencyResources(magnitude, phase)) return result;
    EnsureFrequencyPrograms();
    if (m_MagnitudePhaseProgram == 0) return result;
    result.texture = CreateComplexTexture(magnitude.paddedWidth, magnitude.paddedHeight);
    const bool rendered = RenderIntoSizedTexture(
        result.texture, magnitude.paddedWidth, magnitude.paddedHeight, [&](unsigned int) {
            glUseProgram(m_MagnitudePhaseProgram);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, magnitude.texture);
            glUniform1i(glGetUniformLocation(m_MagnitudePhaseProgram, "uMagnitude"), 1);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, phase.texture);
            glUniform1i(glGetUniformLocation(m_MagnitudePhaseProgram, "uPhase"), 2);
            glUniform1i(glGetUniformLocation(m_MagnitudePhaseProgram, "uMode"), 2);
            m_Quad.Draw();
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
        });
    if (!rendered && result.texture != 0) {
        glDeleteTextures(1, &result.texture);
        result.texture = 0;
    }
    result.valid = result.texture != 0;
    result.hermitian = magnitude.hermitian && phase.hermitian;
    return result;
}

RenderPipeline::GraphNodeRenderResult RenderPipeline::RenderSpectrumVisualization(
    const RenderFrequencyResource& spectrum,
    const RenderSpectrumViewSettings& settings) {
    GraphNodeRenderResult result;
    if (!spectrum.valid || spectrum.texture == 0 ||
        spectrum.kind != RenderFrequencyResourceKind::Spectrum) return result;
    EnsureFrequencyPrograms();
    if (m_SpectrumViewProgram == 0) return result;
    result.texture = GLHelpers::CreateEmptyTexture(spectrum.sourceWidth, spectrum.sourceHeight);
    const bool rendered = RenderIntoSizedTexture(
        result.texture, spectrum.sourceWidth, spectrum.sourceHeight, [&](unsigned int) {
            glUseProgram(m_SpectrumViewProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, spectrum.texture);
            glUniform1i(glGetUniformLocation(m_SpectrumViewProgram, "uSpectrum"), 0);
            glUniform1i(glGetUniformLocation(m_SpectrumViewProgram, "uMode"), static_cast<int>(settings.mode));
            glUniform1i(glGetUniformLocation(m_SpectrumViewProgram, "uLut"), static_cast<int>(settings.lut));
            glUniform1f(glGetUniformLocation(m_SpectrumViewProgram, "uExposure"), settings.exposure);
            glUniform1f(glGetUniformLocation(m_SpectrumViewProgram, "uGamma"), settings.gamma);
            glUniform1i(glGetUniformLocation(m_SpectrumViewProgram, "uCenterDc"), settings.centerDc ? 1 : 0);
            m_Quad.Draw();
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
        });
    if (!rendered && result.texture != 0) {
        glDeleteTextures(1, &result.texture);
        result.texture = 0;
    }
    result.owned = result.texture != 0;
    return result;
}

RenderSpectrumAnalysis RenderPipeline::AnalyzeSpectrum(
    const RenderFrequencyResource& spectrum,
    const RenderSpectrumAnalyzerSettings& settings,
    std::size_t fingerprint) {
    RenderSpectrumAnalysis result;
    result.fingerprint = fingerprint;
    if (!spectrum.valid || spectrum.texture == 0 ||
        spectrum.kind != RenderFrequencyResourceKind::Spectrum ||
        spectrum.paddedWidth <= 0 ||
        spectrum.paddedHeight <= 0) {
        result.error = "Spectrum Analyzer requires a valid complex spectrum.";
        return result;
    }

    std::size_t elementCount = 0;
    if (!Stack::PixelBuffer::TryComputePixelElementCount(
            spectrum.paddedWidth,
            spectrum.paddedHeight,
            2,
            elementCount)) {
        result.error =
            "Spectrum Analyzer dimensions exceed CPU readback limits.";
        return result;
    }
    std::vector<float> complexSamples;
    try {
        complexSamples.resize(elementCount);
    } catch (const std::bad_alloc&) {
        result.error =
            "Spectrum Analyzer could not allocate its CPU readback.";
        return result;
    } catch (const std::length_error&) {
        result.error =
            "Spectrum Analyzer dimensions exceed CPU readback limits.";
        return result;
    }
    const ScopedFramebufferState savedState(true);
    const Stack::Renderer::GLState::PixelPackState savedPackState;
    savedPackState.ConfigureTightCpuReadback();
    const unsigned int fbo = GLHelpers::CreateFBO(spectrum.texture);
    if (fbo == 0) {
        savedPackState.Restore();
        savedState.Restore(true);
        result.error = "Spectrum Analyzer could not create a readback target.";
        return result;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(
        0, 0, spectrum.paddedWidth, spectrum.paddedHeight,
        GL_RG, GL_FLOAT, complexSamples.data());
    const bool readbackOk = glGetError() == GL_NO_ERROR;
    savedPackState.Restore();
    savedState.Restore(true);
    glDeleteFramebuffers(1, &fbo);
    if (!readbackOk) {
        result.error = "Spectrum Analyzer texture readback failed.";
        return result;
    }

    std::array<double, 256> radialSums {};
    std::array<std::uint64_t, 256> radialCounts {};
    double nonDcPower = 0.0;
    double selectedPower = 0.0;
    double peakPower = -1.0;
    const double inner = std::clamp<double>(settings.innerRadius, 0.0, 0.70710678);
    const double outer = std::clamp<double>(
        std::max(settings.innerRadius, settings.outerRadius), 0.0, 0.70710678);

    for (int y = 0; y < spectrum.paddedHeight; ++y) {
        const double fy = y <= spectrum.paddedHeight / 2
            ? static_cast<double>(y) / spectrum.paddedHeight
            : static_cast<double>(y - spectrum.paddedHeight) / spectrum.paddedHeight;
        for (int x = 0; x < spectrum.paddedWidth; ++x) {
            const double fx = x <= spectrum.paddedWidth / 2
                ? static_cast<double>(x) / spectrum.paddedWidth
                : static_cast<double>(x - spectrum.paddedWidth) / spectrum.paddedWidth;
            const bool dc = x == 0 && y == 0;
            if (dc && settings.excludeDc) continue;
            const std::size_t offset =
                (static_cast<std::size_t>(y) * spectrum.paddedWidth + x) * 2u;
            const double real = complexSamples[offset];
            const double imaginary = complexSamples[offset + 1u];
            const double power = real * real + imaginary * imaginary;
            if (!std::isfinite(power)) continue;
            const double frequency = std::sqrt(fx * fx + fy * fy);
            const std::size_t bin = std::min<std::size_t>(
                255u, static_cast<std::size_t>(
                    std::floor(std::clamp(frequency / 0.70710678, 0.0, 1.0) * 255.0)));
            radialSums[bin] += power;
            ++radialCounts[bin];
            if (!dc) nonDcPower += power;
            if (frequency >= inner && frequency <= outer) {
                if (!dc) selectedPower += power;
                if (power > peakPower) {
                    peakPower = power;
                    result.peakFrequency = static_cast<float>(frequency);
                    result.peakDirectionDegrees = static_cast<float>(
                        std::atan2(fy, fx) * 180.0 / kPi);
                }
            }
        }
    }
    for (std::size_t i = 0; i < result.radialPower.size(); ++i) {
        result.radialPower[i] = radialCounts[i] == 0
            ? 0.0f
            : static_cast<float>(radialSums[i] / static_cast<double>(radialCounts[i]));
    }
    result.bandPower = nonDcPower > std::numeric_limits<double>::epsilon()
        ? static_cast<float>(selectedPower / nonDcPower)
        : 0.0f;
    result.valid = true;
    return result;
}

RenderPipeline::GraphNodeRenderResult RenderPipeline::RenderFrequencyGraphNode(
    const GraphExecutionContext&,
    const RenderGraphNode&,
    const std::string&,
    const std::function<unsigned int(int, const std::string&)>&,
    const std::function<unsigned int(int, const std::string&)>&) {
    // Schema-v6 frequency shells intentionally have no execution path.
    // Schema-v7 uses the typed evaluators in ExecuteGraphImpl.
    return {};
}
