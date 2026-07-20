#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <algorithm>
#include <cmath>
#include <functional>

using namespace Stack::Renderer::GraphExecution;

namespace {

#ifndef GL_RG32F
#define GL_RG32F 0x8230
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif

unsigned int CreateComplexTexture(int width, int height) {
    if (width <= 0 || height <= 0) {
        return 0;
    }

    unsigned int texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, width, height, 0, GL_RG, GL_FLOAT, nullptr);
    if (glGetError() != GL_NO_ERROR) {
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &texture);
        return 0;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

bool RenderIntoSizedTexture(
    unsigned int texture,
    int width,
    int height,
    const std::function<void(unsigned int)>& renderFn) {
    if (texture == 0 || width <= 0 || height <= 0) {
        return false;
    }

    unsigned int fbo = GLHelpers::CreateFBO(texture);
    if (fbo == 0) {
        return false;
    }

    GLint prevFBO = 0;
    GLint prevViewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT);
    renderFn(fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glDeleteFramebuffers(1, &fbo);
    return true;
}

int FrequencyMaskShapeToShader(RenderFrequencyMaskShape shape) {
    switch (shape) {
        case RenderFrequencyMaskShape::LowPass: return 0;
        case RenderFrequencyMaskShape::HighPass: return 1;
        case RenderFrequencyMaskShape::BandPass: return 2;
        case RenderFrequencyMaskShape::BandStop: return 3;
        case RenderFrequencyMaskShape::Notch: return 4;
        case RenderFrequencyMaskShape::Gaussian: return 5;
        case RenderFrequencyMaskShape::Butterworth: return 6;
    }
    return 0;
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
        uniform int uLut;
        uniform float uExposure;
        uniform float uGamma;
        uniform int uCenterDc;

        vec3 turbo(float x) {
            x = clamp(x, 0.0, 1.0);
            vec4 kRed = vec4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
            vec4 kGreen = vec4(0.09140261, 2.19418839, 4.84296658, -14.18503333);
            vec4 kBlue = vec4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
            vec2 v = vec2(1.0, x);
            vec4 vx = vec4(1.0, x, x * x, x * x * x);
            float r = dot(vx, kRed) + 59.28637943 * v.y * v.y * v.y * v.y;
            float g = dot(vx, kGreen) + 4.27729857 * v.y * v.y * v.y * v.y;
            float b = dot(vx, kBlue) - 26.10412138 * v.y * v.y * v.y * v.y;
            return clamp(vec3(r, g, b), 0.0, 1.0);
        }

        vec3 viridis(float x) {
            x = clamp(x, 0.0, 1.0);
            vec3 a = vec3(0.267, 0.005, 0.329);
            vec3 b = vec3(0.128, 0.567, 0.551);
            vec3 c = vec3(0.993, 0.906, 0.144);
            return mix(mix(a, b, smoothstep(0.0, 0.72, x)), c, smoothstep(0.55, 1.0, x));
        }

        vec3 inferno(float x) {
            x = clamp(x, 0.0, 1.0);
            vec3 a = vec3(0.002, 0.001, 0.014);
            vec3 b = vec3(0.520, 0.046, 0.510);
            vec3 c = vec3(0.988, 0.998, 0.645);
            return mix(mix(a, b, smoothstep(0.0, 0.62, x)), c, smoothstep(0.48, 1.0, x));
        }

        void main() {
            vec2 uv = uCenterDc != 0 ? fract(vTexCoord + vec2(0.5)) : vTexCoord;
            vec2 complexValue = texture(uSpectrum, uv).rg;
            float magnitude = length(complexValue);
            float value = log(1.0 + magnitude) * max(uExposure, 0.001) * 0.18;
            value = pow(clamp(value, 0.0, 1.0), 1.0 / max(uGamma, 0.001));

            vec3 color = vec3(value);
            if (uLut == 0) {
                color = turbo(value);
            } else if (uLut == 1) {
                color = viridis(value);
            } else if (uLut == 2) {
                color = inferno(value);
            }
            FragColor = vec4(color, 1.0);
        }
    )";

    static const char* frequencyMaskFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform int uShape;
        uniform float uCutoff;
        uniform float uWidth;
        uniform float uFeather;
        uniform float uOrder;
        uniform vec2 uCenter;
        uniform int uInvert;

        void main() {
            vec2 centered = vTexCoord - vec2(0.5);
            float radial = clamp(length(centered) / 0.70710678, 0.0, 1.0);
            float cutoff = clamp(uCutoff, 0.0, 1.0);
            float width = max(uWidth, 0.0001);
            float feather = max(uFeather, 0.0001);
            float value = 1.0;

            if (uShape == 0) {
                value = 1.0 - smoothstep(max(0.0, cutoff - feather), min(1.0, cutoff + feather), radial);
            } else if (uShape == 1) {
                value = smoothstep(max(0.0, cutoff - feather), min(1.0, cutoff + feather), radial);
            } else if (uShape == 2 || uShape == 3) {
                float halfWidth = width * 0.5;
                float lo = smoothstep(cutoff - halfWidth - feather, cutoff - halfWidth + feather, radial);
                float hi = 1.0 - smoothstep(cutoff + halfWidth - feather, cutoff + halfWidth + feather, radial);
                value = clamp(lo * hi, 0.0, 1.0);
                if (uShape == 3) {
                    value = 1.0 - value;
                }
            } else if (uShape == 4) {
                float d = distance(vTexCoord, uCenter);
                value = smoothstep(width, width + feather, d);
            } else if (uShape == 5) {
                float sigma = max(cutoff, 0.001);
                value = exp(-(radial * radial) / (2.0 * sigma * sigma));
            } else if (uShape == 6) {
                float order = clamp(uOrder, 1.0, 12.0);
                float ratio = radial / max(cutoff, 0.001);
                value = 1.0 / (1.0 + pow(ratio, 2.0 * order));
            }

            if (uInvert != 0) {
                value = 1.0 - value;
            }
            FragColor = vec4(value, value, value, 1.0);
        }
    )";

    static const char* spectrumMathFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uSpectrumA;
        uniform sampler2D uSpectrumB;
        uniform sampler2D uFilter;
        uniform int uHasB;
        uniform int uHasFilter;
        uniform int uMode;
        uniform float uAmount;

        vec2 cmul(vec2 a, vec2 b) {
            return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
        }

        void main() {
            vec2 a = texture(uSpectrumA, vTexCoord).rg;
            vec2 b = uHasB != 0 ? texture(uSpectrumB, vTexCoord).rg : vec2(0.0);
            float amount = clamp(uAmount, 0.0, 4.0);
            vec2 outValue = a;
            if (uMode == 0) {
                vec2 filterUv = fract(vTexCoord + vec2(0.5));
                float filterValue = uHasFilter != 0 ? clamp(texture(uFilter, filterUv).r, 0.0, 1.0) : 1.0;
                outValue = a * mix(1.0, filterValue, clamp(amount, 0.0, 1.0));
            } else if (uMode == 1) {
                outValue = a + b * amount;
            } else if (uMode == 2) {
                outValue = a - b * amount;
            } else if (uMode == 3) {
                outValue = abs(a - b) * amount;
            }
            FragColor = vec4(outValue, 0.0, 1.0);
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
        uniform float uExposure;
        uniform float uGamma;

        const float PI = 3.14159265358979323846;

        void main() {
            if (uMode == 2) {
                float magnitude = max(texture(uMagnitude, vTexCoord).r, 0.0);
                float phase = texture(uPhase, vTexCoord).r * 2.0 * PI - PI;
                FragColor = vec4(vec2(cos(phase), sin(phase)) * magnitude, 0.0, 1.0);
                return;
            }

            vec2 c = texture(uSpectrum, vTexCoord).rg;
            float value = 0.0;
            if (uMode == 0) {
                value = log(1.0 + length(c)) * max(uExposure, 0.001) * 0.18;
            } else {
                value = (atan(c.y, c.x) + PI) / (2.0 * PI);
            }
            value = pow(clamp(value, 0.0, 1.0), 1.0 / max(uGamma, 0.001));
            FragColor = vec4(value, value, value, 1.0);
        }
    )";

    static const char* ifftProjectFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uSpatial;
        uniform vec2 uSourceSize;
        uniform vec2 uPaddedSize;

        void main() {
            vec2 cropUv = vTexCoord * (uSourceSize / max(uPaddedSize, vec2(1.0)));
            float value = texture(uSpatial, cropUv).r;
            FragColor = vec4(value, value, value, 1.0);
        }
    )";

    if (!m_SpectrumViewProgram) {
        m_SpectrumViewProgram = GLHelpers::CreateShaderProgram(vertexSrc, spectrumViewFragSrc);
    }
    if (!m_FrequencyMaskProgram) {
        m_FrequencyMaskProgram = GLHelpers::CreateShaderProgram(vertexSrc, frequencyMaskFragSrc);
    }
    if (!m_SpectrumMathProgram) {
        m_SpectrumMathProgram = GLHelpers::CreateShaderProgram(vertexSrc, spectrumMathFragSrc);
    }
    if (!m_MagnitudePhaseProgram) {
        m_MagnitudePhaseProgram = GLHelpers::CreateShaderProgram(vertexSrc, magnitudePhaseFragSrc);
    }
    if (!m_FrequencyIfftProjectProgram) {
        m_FrequencyIfftProjectProgram = GLHelpers::CreateShaderProgram(vertexSrc, ifftProjectFragSrc);
    }
}

RenderPipeline::GraphNodeRenderResult RenderPipeline::RenderFrequencyGraphNode(
    const GraphExecutionContext& executionContext,
    const RenderGraphNode& node,
    const std::string& socketId,
    const std::function<unsigned int(int, const std::string&)>& evalImage,
    const std::function<unsigned int(int, const std::string&)>& evalMask) {
    GraphNodeRenderResult result;
    if (m_Width <= 0 || m_Height <= 0) {
        return result;
    }

    EnsureFrequencyPrograms();
    const int paddedW = Stack::Renderer::Frequency::GpuFft::NextPowerOfTwo(m_Width);
    const int paddedH = Stack::Renderer::Frequency::GpuFft::NextPowerOfTwo(m_Height);

    if (node.kind == RenderGraphNodeKind::FrequencyFft) {
        const RenderGraphLink* input = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kImageInputSocketId);
        const unsigned int inputTexture = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
        if (inputTexture != 0) {
            result.texture = m_GpuFft.Forward(
                inputTexture,
                m_Width,
                m_Height,
                paddedW,
                paddedH,
                node.frequencyFftSettings.luminanceOnly);
            result.owned = result.texture != 0;
        }
        return result;
    }

    if (node.kind == RenderGraphNodeKind::FrequencyIfft) {
        const RenderGraphLink* input = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kImageInputSocketId);
        const unsigned int spectrumTexture = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
        if (spectrumTexture == 0) {
            return result;
        }

        const unsigned int spatialTexture = m_GpuFft.Inverse(spectrumTexture, paddedW, paddedH);
        if (spatialTexture == 0 || !m_FrequencyIfftProjectProgram) {
            if (spatialTexture != 0) glDeleteTextures(1, &spatialTexture);
            return result;
        }

        result.texture = CreateGraphRenderTargetTexture();
        const bool rendered = RenderIntoGraphTargetTexture(result.texture, [&](unsigned int) {
            glUseProgram(m_FrequencyIfftProjectProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, spatialTexture);
            glUniform1i(glGetUniformLocation(m_FrequencyIfftProjectProgram, "uSpatial"), 0);
            glUniform2f(glGetUniformLocation(m_FrequencyIfftProjectProgram, "uSourceSize"), static_cast<float>(m_Width), static_cast<float>(m_Height));
            glUniform2f(glGetUniformLocation(m_FrequencyIfftProjectProgram, "uPaddedSize"), static_cast<float>(paddedW), static_cast<float>(paddedH));
            m_Quad.Draw();
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
        });
        glDeleteTextures(1, &spatialTexture);
        if (!rendered && result.texture != 0) {
            glDeleteTextures(1, &result.texture);
            result.texture = 0;
        }
        result.owned = result.texture != 0;
        return result;
    }

    if (node.kind == RenderGraphNodeKind::SpectrumView) {
        const RenderGraphLink* input = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kImageInputSocketId);
        const unsigned int spectrumTexture = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
        if (spectrumTexture == 0 || !m_SpectrumViewProgram) {
            return result;
        }

        result.texture = CreateGraphRenderTargetTexture();
        const bool rendered = RenderIntoGraphTargetTexture(result.texture, [&](unsigned int) {
            glUseProgram(m_SpectrumViewProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, spectrumTexture);
            glUniform1i(glGetUniformLocation(m_SpectrumViewProgram, "uSpectrum"), 0);
            glUniform1i(glGetUniformLocation(m_SpectrumViewProgram, "uLut"), static_cast<int>(node.spectrumViewSettings.lut));
            glUniform1f(glGetUniformLocation(m_SpectrumViewProgram, "uExposure"), node.spectrumViewSettings.exposure);
            glUniform1f(glGetUniformLocation(m_SpectrumViewProgram, "uGamma"), node.spectrumViewSettings.gamma);
            glUniform1i(glGetUniformLocation(m_SpectrumViewProgram, "uCenterDc"), node.spectrumViewSettings.centerDc ? 1 : 0);
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

    if (node.kind == RenderGraphNodeKind::FrequencyMask) {
        if (!m_FrequencyMaskProgram) {
            return result;
        }
        result.texture = CreateGraphRenderTargetTexture();
        const RenderFrequencyMaskSettings& settings = node.frequencyMaskSettings;
        const bool rendered = RenderIntoGraphTargetTexture(result.texture, [&](unsigned int) {
            glUseProgram(m_FrequencyMaskProgram);
            glUniform1i(glGetUniformLocation(m_FrequencyMaskProgram, "uShape"), FrequencyMaskShapeToShader(settings.shape));
            glUniform1f(glGetUniformLocation(m_FrequencyMaskProgram, "uCutoff"), settings.cutoff);
            glUniform1f(glGetUniformLocation(m_FrequencyMaskProgram, "uWidth"), settings.width);
            glUniform1f(glGetUniformLocation(m_FrequencyMaskProgram, "uFeather"), settings.feather);
            glUniform1f(glGetUniformLocation(m_FrequencyMaskProgram, "uOrder"), settings.order);
            glUniform2f(glGetUniformLocation(m_FrequencyMaskProgram, "uCenter"), settings.centerX, settings.centerY);
            glUniform1i(glGetUniformLocation(m_FrequencyMaskProgram, "uInvert"), settings.invert ? 1 : 0);
            m_Quad.Draw();
            glUseProgram(0);
        });
        if (!rendered && result.texture != 0) {
            glDeleteTextures(1, &result.texture);
            result.texture = 0;
        }
        result.owned = result.texture != 0;
        return result;
    }

    if (node.kind == RenderGraphNodeKind::SpectrumMath) {
        const RenderGraphLink* inputA = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kMixInputASocketId);
        const RenderGraphLink* inputB = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kMixInputBSocketId);
        const RenderGraphLink* filterInput = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kMaskInputSocketId);
        const unsigned int textureA = inputA ? evalImage(inputA->fromNodeId, inputA->fromSocketId) : 0;
        const unsigned int textureB = inputB ? evalImage(inputB->fromNodeId, inputB->fromSocketId) : 0;
        const unsigned int filterTexture = filterInput ? evalMask(filterInput->fromNodeId, filterInput->fromSocketId) : 0;
        if (textureA == 0 || !m_SpectrumMathProgram) {
            return result;
        }

        result.texture = CreateComplexTexture(paddedW, paddedH);
        const bool rendered = RenderIntoSizedTexture(result.texture, paddedW, paddedH, [&](unsigned int) {
            glUseProgram(m_SpectrumMathProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, textureA);
            glUniform1i(glGetUniformLocation(m_SpectrumMathProgram, "uSpectrumA"), 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, textureB);
            glUniform1i(glGetUniformLocation(m_SpectrumMathProgram, "uSpectrumB"), 1);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, filterTexture);
            glUniform1i(glGetUniformLocation(m_SpectrumMathProgram, "uFilter"), 2);
            glUniform1i(glGetUniformLocation(m_SpectrumMathProgram, "uHasB"), textureB != 0 ? 1 : 0);
            glUniform1i(glGetUniformLocation(m_SpectrumMathProgram, "uHasFilter"), filterTexture != 0 ? 1 : 0);
            glUniform1i(glGetUniformLocation(m_SpectrumMathProgram, "uMode"), static_cast<int>(node.spectrumMathMode));
            glUniform1f(glGetUniformLocation(m_SpectrumMathProgram, "uAmount"), node.spectrumMathSettings.amount);
            m_Quad.Draw();
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, 0);
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
        result.owned = result.texture != 0;
        return result;
    }

    if (node.kind == RenderGraphNodeKind::MagnitudePhase) {
        if (!m_MagnitudePhaseProgram) {
            return result;
        }
        const bool recombine =
            node.magnitudePhaseMode == RenderMagnitudePhaseMode::Recombine &&
            socketId == EditorNodeGraph::kImageOutputSocketId;

        if (recombine) {
            const RenderGraphLink* magInput = executionContext.FindInputLink(node.nodeId, "magnitude");
            const RenderGraphLink* phaseInput = executionContext.FindInputLink(node.nodeId, "phase");
            const unsigned int magTexture = magInput ? evalMask(magInput->fromNodeId, magInput->fromSocketId) : 0;
            const unsigned int phaseTexture = phaseInput ? evalMask(phaseInput->fromNodeId, phaseInput->fromSocketId) : 0;
            if (magTexture == 0 || phaseTexture == 0) {
                return result;
            }

            result.texture = CreateComplexTexture(paddedW, paddedH);
            const bool rendered = RenderIntoSizedTexture(result.texture, paddedW, paddedH, [&](unsigned int) {
                glUseProgram(m_MagnitudePhaseProgram);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, magTexture);
                glUniform1i(glGetUniformLocation(m_MagnitudePhaseProgram, "uMagnitude"), 1);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, phaseTexture);
                glUniform1i(glGetUniformLocation(m_MagnitudePhaseProgram, "uPhase"), 2);
                glUniform1i(glGetUniformLocation(m_MagnitudePhaseProgram, "uMode"), 2);
                glUniform1f(glGetUniformLocation(m_MagnitudePhaseProgram, "uExposure"), node.magnitudePhaseSettings.exposure);
                glUniform1f(glGetUniformLocation(m_MagnitudePhaseProgram, "uGamma"), node.magnitudePhaseSettings.gamma);
                m_Quad.Draw();
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE0);
                glUseProgram(0);
            });
            if (!rendered && result.texture != 0) {
                glDeleteTextures(1, &result.texture);
                result.texture = 0;
            }
            result.owned = result.texture != 0;
            return result;
        }

        const RenderGraphLink* input = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kImageInputSocketId);
        const unsigned int spectrumTexture = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
        if (spectrumTexture == 0) {
            return result;
        }

        result.texture = CreateGraphRenderTargetTexture();
        const bool rendered = RenderIntoGraphTargetTexture(result.texture, [&](unsigned int) {
            glUseProgram(m_MagnitudePhaseProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, spectrumTexture);
            glUniform1i(glGetUniformLocation(m_MagnitudePhaseProgram, "uSpectrum"), 0);
            glUniform1i(glGetUniformLocation(m_MagnitudePhaseProgram, "uMode"), static_cast<int>(node.magnitudePhaseMode));
            glUniform1f(glGetUniformLocation(m_MagnitudePhaseProgram, "uExposure"), node.magnitudePhaseSettings.exposure);
            glUniform1f(glGetUniformLocation(m_MagnitudePhaseProgram, "uGamma"), node.magnitudePhaseSettings.gamma);
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

    return result;
}
