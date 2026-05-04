#include "RenderPipeline.h"
#include "Editor/LayerRegistry.h"
#include "ThirdParty/stb_image.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <set>
#include <unordered_map>
#include <utility>

RenderPipeline::RenderPipeline()
    : m_Width(0), m_Height(0),
      m_SourceChannels(4),
      m_SourceTexture(0), m_PingTexture(0), m_PongTexture(0),
      m_PingFBO(0), m_PongFBO(0), m_OutputTexture(0), m_ExternalOutputTexture(0),
      m_MaskProgram(0), m_MaskBlendProgram(0), m_MixProgram(0),
      m_MaskUtilityProgram(0), m_ImageToMaskProgram(0), m_ImageGeneratorProgram(0)
{}

RenderPipeline::~RenderPipeline() {
    CleanupFBOs();
    if (m_SourceTexture) glDeleteTextures(1, &m_SourceTexture);
    if (m_ExternalOutputTexture) glDeleteTextures(1, &m_ExternalOutputTexture);
    if (m_MaskProgram) glDeleteProgram(m_MaskProgram);
    if (m_MaskBlendProgram) glDeleteProgram(m_MaskBlendProgram);
    if (m_MixProgram) glDeleteProgram(m_MixProgram);
    if (m_MaskUtilityProgram) glDeleteProgram(m_MaskUtilityProgram);
    if (m_ImageToMaskProgram) glDeleteProgram(m_ImageToMaskProgram);
    if (m_ImageGeneratorProgram) glDeleteProgram(m_ImageGeneratorProgram);
}

void RenderPipeline::Initialize() {
    m_Quad.Initialize();
}

void RenderPipeline::CleanupFBOs() {
    if (m_PingFBO)     { glDeleteFramebuffers(1, &m_PingFBO);  m_PingFBO = 0; }
    if (m_PongFBO)     { glDeleteFramebuffers(1, &m_PongFBO);  m_PongFBO = 0; }
    if (m_PingTexture) { glDeleteTextures(1, &m_PingTexture); m_PingTexture = 0; }
    if (m_PongTexture) { glDeleteTextures(1, &m_PongTexture); m_PongTexture = 0; }
}

void RenderPipeline::Resize(int width, int height) {
    if (width == m_Width && height == m_Height) return;
    m_Width = width;
    m_Height = height;

    CleanupFBOs();

    m_PingTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    m_PongTexture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    m_PingFBO = GLHelpers::CreateFBO(m_PingTexture);
    m_PongFBO = GLHelpers::CreateFBO(m_PongTexture);
}

bool RenderPipeline::LoadSourceImage(const std::string& filepath) {
    int w, h, ch;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* data = stbi_load(filepath.c_str(), &w, &h, &ch, 4);
    if (!data) {
        std::cerr << "[RenderPipeline] Failed to load image: " << filepath
                  << " (" << stbi_failure_reason() << ")\n";
        return false;
    }

    if (m_SourceTexture) glDeleteTextures(1, &m_SourceTexture);
    m_SourceTexture = GLHelpers::CreateTextureFromPixels(data, w, h, 4);
    m_SourceChannels = 4;
    m_SourcePixels.assign(data, data + (w * h * 4));
    stbi_image_free(data);

    Resize(w, h);

    std::cout << "[RenderPipeline] Loaded image " << w << "x" << h << " from: " << filepath << "\n";
    return true;
}

void RenderPipeline::LoadSourceFromPixels(const unsigned char* data, int w, int h, int ch) {
    if (m_SourceTexture) glDeleteTextures(1, &m_SourceTexture);
    m_SourceTexture = GLHelpers::CreateTextureFromPixels(data, w, h, ch);
    m_SourceChannels = ch;
    const int clampedChannels = std::max(1, ch);
    m_SourcePixels.assign(data, data + (w * h * clampedChannels));
    Resize(w, h);
    ClearOutput();
}

void RenderPipeline::Clear() {
    if (m_SourceTexture) {
        glDeleteTextures(1, &m_SourceTexture);
        m_SourceTexture = 0;
    }
    if (m_ExternalOutputTexture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
        m_ExternalOutputTexture = 0;
    }
    m_OutputTexture = 0;
    m_SourcePixels.clear();
    m_SourceChannels = 4;
    m_Width = 0;
    m_Height = 0;
    CleanupFBOs();
}

void RenderPipeline::ClearOutput() {
    m_OutputTexture = 0;
}

void RenderPipeline::UploadOutputFromPixels(const unsigned char* data, int w, int h, int ch) {
    if (!data || w <= 0 || h <= 0) {
        ClearOutput();
        return;
    }
    if (m_ExternalOutputTexture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
        m_ExternalOutputTexture = 0;
    }
    m_ExternalOutputTexture = GLHelpers::CreateTextureFromPixels(data, w, h, ch);
    m_OutputTexture = m_ExternalOutputTexture;
    m_Width = w;
    m_Height = h;
}

void RenderPipeline::Execute(const std::vector<std::shared_ptr<LayerBase>>& layers) {
    if (!m_SourceTexture || m_Width == 0 || m_Height == 0) {
        m_OutputTexture = m_SourceTexture; // Nothing to process
        return;
    }

    // Save previous GL state
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    
    // Save and disable states that might interfere with full-screen quad rendering
    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);

    glViewport(0, 0, m_Width, m_Height);

    int activeCount = 0;
    for (auto& layer : layers) {
        if (layer->IsVisible()) activeCount++;
    }

    if (activeCount == 0) {
        // No layers active, output is just the source
        m_OutputTexture = m_SourceTexture;
        
        // Restore
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        if (prevScissor) glEnable(GL_SCISSOR_TEST);
        if (prevDepth) glEnable(GL_DEPTH_TEST);
        if (prevStencil) glEnable(GL_STENCIL_TEST);
        if (prevBlend) glEnable(GL_BLEND);
        return;
    }

    // Sequential ping-pong execution:
    // Layer 1 reads Source → writes Ping
    // Layer 2 reads Ping → writes Pong
    // Layer 3 reads Pong → writes Ping ... etc.
    unsigned int currentInput = m_SourceTexture;
    bool usePing = true;

    for (auto& layer : layers) {
        if (!layer->IsVisible()) continue;

        unsigned int targetFBO = usePing ? m_PingFBO : m_PongFBO;
        unsigned int targetTex = usePing ? m_PingTexture : m_PongTexture;

        glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
        glClear(GL_COLOR_BUFFER_BIT);

        layer->ExecuteWithSource(currentInput, m_SourceTexture, m_Width, m_Height, m_Quad);

        currentInput = targetTex;
        usePing = !usePing;
    }

    m_OutputTexture = currentInput;

    // Restore previous GL state
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    if (prevScissor) glEnable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST);
    if (prevStencil) glEnable(GL_STENCIL_TEST);
    if (prevBlend) glEnable(GL_BLEND);
}

void RenderPipeline::EnsureMaskPrograms() {
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

    static const char* maskFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform int uKind;
        uniform float uValue;
        uniform float uAngle;
        uniform float uOffset;
        uniform float uScale;
        uniform vec2 uCenter;
        uniform float uRadius;
        uniform float uFeather;
        uniform int uInvert;
        float hash(vec2 p) {
            return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
        }
        float noise(vec2 p) {
            vec2 i = floor(p);
            vec2 f = fract(p);
            vec2 u = f * f * (3.0 - 2.0 * f);
            return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
                       mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
        }
        void main() {
            vec2 uv = vTexCoord;
            float maskValue = clamp(uValue, 0.0, 1.0);
            if (uKind == 1) {
                float radiansAngle = radians(uAngle);
                vec2 dir = vec2(cos(radiansAngle), sin(radiansAngle));
                maskValue = dot(uv - vec2(0.5), dir) * max(uScale, 0.001) + 0.5 + uOffset;
                maskValue = clamp(maskValue, 0.0, 1.0);
            } else if (uKind == 2) {
                float d = distance(uv, uCenter);
                float feather = max(uFeather, 0.0001);
                maskValue = 1.0 - smoothstep(max(0.0, uRadius - feather), uRadius + feather, d);
                maskValue = clamp(maskValue, 0.0, 1.0);
            } else if (uKind == 3) {
                float n = noise(uv * max(uScale * 96.0, 1.0) + vec2(uOffset * 37.0, uAngle * 0.071));
                maskValue = clamp((n - 0.5) * max(uValue * 4.0, 0.001) + 0.5, 0.0, 1.0);
            }
            if (uInvert != 0) {
                maskValue = 1.0 - maskValue;
            }
            FragColor = vec4(maskValue, maskValue, maskValue, 1.0);
        }
    )";

    static const char* blendFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uOriginal;
        uniform sampler2D uProcessed;
        uniform sampler2D uMask;
        void main() {
            vec4 originalColor = texture(uOriginal, vTexCoord);
            vec4 processedColor = texture(uProcessed, vTexCoord);
            float maskValue = clamp(texture(uMask, vTexCoord).r, 0.0, 1.0);
            FragColor = mix(originalColor, processedColor, maskValue);
        }
    )";

    if (!m_MaskProgram) {
        m_MaskProgram = GLHelpers::CreateShaderProgram(vertexSrc, maskFragSrc);
    }
    if (!m_MaskBlendProgram) {
        m_MaskBlendProgram = GLHelpers::CreateShaderProgram(vertexSrc, blendFragSrc);
    }
}

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

void RenderPipeline::EnsureMixProgram() {
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

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uImageA;
        uniform sampler2D uImageB;
        uniform sampler2D uFactorMask;
        uniform int uHasFactorMask;
        uniform float uFactor;
        uniform int uBlendMode;
        void main() {
            vec4 a = texture(uImageA, vTexCoord);
            vec4 b = texture(uImageB, vTexCoord);
            float factor = clamp(uFactor, 0.0, 1.0);
            if (uHasFactorMask != 0) {
                factor *= clamp(texture(uFactorMask, vTexCoord).r, 0.0, 1.0);
            }

            vec4 blended = b;
            if (uBlendMode == 1) {
                blended = a + b;
            } else if (uBlendMode == 2) {
                blended = a * b;
            } else if (uBlendMode == 3) {
                blended = 1.0 - (1.0 - a) * (1.0 - b);
            } else if (uBlendMode == 4) {
                float outA = b.a + a.a * (1.0 - b.a);
                vec3 outRgb = b.rgb * b.a + a.rgb * (1.0 - b.a);
                if (outA > 0.0001) {
                    outRgb /= outA;
                }
                blended = vec4(outRgb, outA);
            }
            FragColor = mix(a, blended, factor);
        }
    )";

    if (!m_MixProgram) {
        m_MixProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
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

void RenderPipeline::EnsureUtilityPrograms() {
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

    static const char* maskUtilityFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputMask;
        uniform int uKind;
        uniform float uBlackPoint;
        uniform float uWhitePoint;
        uniform float uGamma;
        uniform float uThreshold;
        uniform float uSoftness;
        uniform int uInvert;
        void main() {
            float v = clamp(texture(uInputMask, vTexCoord).r, 0.0, 1.0);
            if (uKind == 0) {
                v = 1.0 - v;
            } else if (uKind == 1) {
                float denom = max(uWhitePoint - uBlackPoint, 0.0001);
                v = clamp((v - uBlackPoint) / denom, 0.0, 1.0);
                v = pow(v, 1.0 / max(uGamma, 0.001));
                if (uInvert != 0) v = 1.0 - v;
            } else if (uKind == 2) {
                float softness = max(uSoftness, 0.0001);
                v = smoothstep(uThreshold - softness, uThreshold + softness, v);
                if (uInvert != 0) v = 1.0 - v;
            }
            FragColor = vec4(v, v, v, 1.0);
        }
    )";

    static const char* imageToMaskFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform float uLow;
        uniform float uHigh;
        uniform float uSoftness;
        uniform int uInvert;
        void main() {
            vec4 c = texture(uInputImage, vTexCoord);
            float lum = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
            float denom = max(uHigh - uLow, 0.0001);
            float v = clamp((lum - uLow) / denom, 0.0, 1.0);
            if (uSoftness > 0.0001) {
                v = smoothstep(0.5 - uSoftness, 0.5 + uSoftness, v);
            }
            if (uInvert != 0) v = 1.0 - v;
            FragColor = vec4(v, v, v, 1.0);
        }
    )";

    static const char* imageGeneratorFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform int uKind;
        uniform vec4 uColorA;
        uniform vec4 uColorB;
        uniform float uAngle;
        uniform float uOffset;
        void main() {
            if (uKind == 0) {
                FragColor = uColorA;
                return;
            }
            float radiansAngle = radians(uAngle);
            vec2 dir = vec2(cos(radiansAngle), sin(radiansAngle));
            float t = clamp(dot(vTexCoord - vec2(0.5), dir) + 0.5 + uOffset, 0.0, 1.0);
            FragColor = mix(uColorA, uColorB, t);
        }
    )";

    if (!m_MaskUtilityProgram) {
        m_MaskUtilityProgram = GLHelpers::CreateShaderProgram(vertexSrc, maskUtilityFragSrc);
    }
    if (!m_ImageToMaskProgram) {
        m_ImageToMaskProgram = GLHelpers::CreateShaderProgram(vertexSrc, imageToMaskFragSrc);
    }
    if (!m_ImageGeneratorProgram) {
        m_ImageGeneratorProgram = GLHelpers::CreateShaderProgram(vertexSrc, imageGeneratorFragSrc);
    }
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
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uLow"), node.imageToMaskSettings.low);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uHigh"), node.imageToMaskSettings.high);
    glUniform1f(glGetUniformLocation(m_ImageToMaskProgram, "uSoftness"), node.imageToMaskSettings.softness);
    glUniform1i(glGetUniformLocation(m_ImageToMaskProgram, "uInvert"), node.imageToMaskSettings.invert ? 1 : 0);
    m_Quad.Draw();
    glActiveTexture(GL_TEXTURE0);
}

unsigned int RenderPipeline::GenerateImageTexture(const RenderGraphNode& node) {
    EnsureUtilityPrograms();
    if (!m_ImageGeneratorProgram || m_Width <= 0 || m_Height <= 0) {
        return 0;
    }
    unsigned int texture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
    unsigned int fbo = GLHelpers::CreateFBO(texture);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, m_Width, m_Height);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_ImageGeneratorProgram);
    glUniform1i(glGetUniformLocation(m_ImageGeneratorProgram, "uKind"), static_cast<int>(node.imageGeneratorKind));
    glUniform4f(
        glGetUniformLocation(m_ImageGeneratorProgram, "uColorA"),
        node.imageGeneratorSettings.colorA[0],
        node.imageGeneratorSettings.colorA[1],
        node.imageGeneratorSettings.colorA[2],
        node.imageGeneratorSettings.colorA[3]);
    glUniform4f(
        glGetUniformLocation(m_ImageGeneratorProgram, "uColorB"),
        node.imageGeneratorSettings.colorB[0],
        node.imageGeneratorSettings.colorB[1],
        node.imageGeneratorSettings.colorB[2],
        node.imageGeneratorSettings.colorB[3]);
    glUniform1f(glGetUniformLocation(m_ImageGeneratorProgram, "uAngle"), node.imageGeneratorSettings.angle);
    glUniform1f(glGetUniformLocation(m_ImageGeneratorProgram, "uOffset"), node.imageGeneratorSettings.offset);
    m_Quad.Draw();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    return texture;
}

void RenderPipeline::ExecuteGraph(const RenderGraphSnapshot& graph) {
    if (!m_SourceTexture || m_Width == 0 || m_Height == 0 || graph.outputNodeId <= 0) {
        m_OutputTexture = m_SourceTexture;
        return;
    }

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, m_Width, m_Height);

    if (m_ExternalOutputTexture) {
        glDeleteTextures(1, &m_ExternalOutputTexture);
        m_ExternalOutputTexture = 0;
    }

    std::unordered_map<int, const RenderGraphNode*> nodes;
    for (const RenderGraphNode& node : graph.nodes) {
        nodes[node.nodeId] = &node;
    }

    auto findInputLink = [&](int nodeId, const std::string& socketId) -> const RenderGraphLink* {
        for (const RenderGraphLink& link : graph.links) {
            if (link.toNodeId == nodeId && link.toSocketId == socketId) {
                return &link;
            }
        }
        return nullptr;
    };

    std::unordered_map<int, unsigned int> imageCache;
    std::unordered_map<int, unsigned int> maskCache;
    std::vector<unsigned int> ownedTextures;
    std::set<int> visitingImages;
    std::set<int> visitingMasks;

    auto createTarget = [&]() {
        unsigned int texture = GLHelpers::CreateEmptyTexture(m_Width, m_Height);
        ownedTextures.push_back(texture);
        return texture;
    };

    auto renderToTexture = [&](unsigned int texture, const std::function<void(unsigned int)>& renderFn) {
        unsigned int fbo = GLHelpers::CreateFBO(texture);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glClear(GL_COLOR_BUFFER_BIT);
        renderFn(fbo);
        glDeleteFramebuffers(1, &fbo);
    };

    std::function<unsigned int(int)> evalMask;
    std::function<unsigned int(int)> evalImage;

    evalMask = [&](int nodeId) -> unsigned int {
        if (maskCache.count(nodeId)) {
            return maskCache[nodeId];
        }
        if (visitingMasks.count(nodeId)) {
            return 0;
        }
        visitingMasks.insert(nodeId);
        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            visitingMasks.erase(nodeId);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        unsigned int result = 0;
        if (node.kind == RenderGraphNodeKind::MaskGenerator) {
            RenderMaskSource mask;
            mask.nodeId = node.nodeId;
            mask.kind = node.maskKind;
            mask.settings = node.maskSettings;
            result = GenerateMaskTexture(mask);
            if (result) {
                ownedTextures.push_back(result);
            }
        } else if (node.kind == RenderGraphNodeKind::MaskUtility) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "maskIn");
            const unsigned int inputMask = input ? evalMask(input->fromNodeId) : 0;
            if (inputMask) {
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderMaskUtility(inputMask, node, fbo);
                });
            }
        } else if (node.kind == RenderGraphNodeKind::ImageToMask) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputImage = input ? evalImage(input->fromNodeId) : 0;
            if (inputImage) {
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderImageToMask(inputImage, node, fbo);
                });
            }
        }
        if (result) {
            maskCache[nodeId] = result;
        }
        visitingMasks.erase(nodeId);
        return result;
    };

    evalImage = [&](int nodeId) -> unsigned int {
        if (imageCache.count(nodeId)) {
            return imageCache[nodeId];
        }
        if (visitingImages.count(nodeId)) {
            return 0;
        }
        visitingImages.insert(nodeId);

        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            visitingImages.erase(nodeId);
            return 0;
        }

        const RenderGraphNode& node = *it->second;
        unsigned int result = 0;
        if (node.kind == RenderGraphNodeKind::Image) {
            if (!node.image.pixels.empty() && node.image.width > 0 && node.image.height > 0) {
                result = GLHelpers::CreateTextureFromPixels(node.image.pixels.data(), node.image.width, node.image.height, node.image.channels);
                ownedTextures.push_back(result);
            } else {
                result = m_SourceTexture;
            }
        } else if (node.kind == RenderGraphNodeKind::ImageGenerator) {
            result = GenerateImageTexture(node);
            if (result) {
                ownedTextures.push_back(result);
            }
        } else if (node.kind == RenderGraphNodeKind::Layer) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            const unsigned int inputTexture = input ? evalImage(input->fromNodeId) : 0;
            if (inputTexture && node.layerJson.is_object()) {
                const std::string type = node.layerJson.value("type", std::string());
                std::shared_ptr<LayerBase> layer = LayerRegistry::CreateLayerFromTypeId(type);
                if (layer) {
                    layer->InitializeGL();
                    layer->Deserialize(node.layerJson);
                    unsigned int processed = createTarget();
                    renderToTexture(processed, [&](unsigned int) {
                        layer->ExecuteWithSource(inputTexture, m_SourceTexture, m_Width, m_Height, m_Quad);
                    });
                    result = processed;
                    const RenderGraphLink* maskLink = findInputLink(node.nodeId, "maskIn");
                    const unsigned int maskTexture = maskLink ? evalMask(maskLink->fromNodeId) : 0;
                    if (maskTexture) {
                        unsigned int blended = createTarget();
                        renderToTexture(blended, [&](unsigned int fbo) {
                            RenderMaskBlend(inputTexture, processed, maskTexture, fbo);
                        });
                        result = blended;
                    }
                }
            }
        } else if (node.kind == RenderGraphNodeKind::Mix) {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
            const unsigned int textureA = inputA ? evalImage(inputA->fromNodeId) : 0;
            const unsigned int textureB = inputB ? evalImage(inputB->fromNodeId) : 0;
            if (textureA && textureB) {
                const RenderGraphLink* factorLink = findInputLink(node.nodeId, "factor");
                const unsigned int factorTexture = factorLink ? evalMask(factorLink->fromNodeId) : 0;
                result = createTarget();
                renderToTexture(result, [&](unsigned int fbo) {
                    RenderMixBlend(textureA, textureB, factorTexture, node.mixFactor, node.mixBlendMode, fbo);
                });
            }
        } else if (node.kind == RenderGraphNodeKind::Output) {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            result = input ? evalImage(input->fromNodeId) : 0;
        }

        if (result) {
            imageCache[nodeId] = result;
        }
        visitingImages.erase(nodeId);
        return result;
    };

    unsigned int finalTexture = 0;
    const auto outputIt = nodes.find(graph.outputNodeId);
    if (outputIt != nodes.end() &&
        (outputIt->second->kind == RenderGraphNodeKind::MaskGenerator ||
         outputIt->second->kind == RenderGraphNodeKind::MaskUtility ||
         outputIt->second->kind == RenderGraphNodeKind::ImageToMask)) {
        finalTexture = evalMask(graph.outputNodeId);
    } else {
        finalTexture = evalImage(graph.outputNodeId);
    }
    if (finalTexture && finalTexture != m_SourceTexture) {
        m_ExternalOutputTexture = finalTexture;
        ownedTextures.erase(std::remove(ownedTextures.begin(), ownedTextures.end(), finalTexture), ownedTextures.end());
        m_OutputTexture = m_ExternalOutputTexture;
    } else {
        m_OutputTexture = finalTexture ? finalTexture : 0;
    }

    for (unsigned int texture : ownedTextures) {
        if (texture && texture != m_SourceTexture && texture != m_OutputTexture) {
            glDeleteTextures(1, &texture);
        }
    }

    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    if (prevScissor) glEnable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST);
    if (prevStencil) glEnable(GL_STENCIL_TEST);
    if (prevBlend) glEnable(GL_BLEND);
}

void RenderPipeline::ExecuteMasked(const std::vector<RenderLayerStep>& steps, const std::vector<RenderMaskSource>& masks) {
    bool hasConnectedMask = false;
    for (const RenderLayerStep& step : steps) {
        if (step.maskNodeId > 0) {
            hasConnectedMask = true;
            break;
        }
    }
    if (!hasConnectedMask) {
        std::vector<std::shared_ptr<LayerBase>> layers;
        layers.reserve(steps.size());
        for (const RenderLayerStep& step : steps) {
            if (step.layer) {
                layers.push_back(step.layer);
            }
        }
        Execute(layers);
        return;
    }

    if (!m_SourceTexture || m_Width == 0 || m_Height == 0) {
        m_OutputTexture = m_SourceTexture;
        return;
    }

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, m_Width, m_Height);

    std::vector<std::pair<int, unsigned int>> maskTextures;
    for (const RenderMaskSource& mask : masks) {
        unsigned int texture = GenerateMaskTexture(mask);
        if (texture) {
            maskTextures.push_back({ mask.nodeId, texture });
        }
    }

    auto findMaskTexture = [&](int nodeId) -> unsigned int {
        for (const auto& item : maskTextures) {
            if (item.first == nodeId) {
                return item.second;
            }
        }
        return 0;
    };

    unsigned int currentInput = m_SourceTexture;
    bool usePing = true;
    int activeCount = 0;
    for (const RenderLayerStep& step : steps) {
        if (step.layer && step.layer->IsVisible()) {
            ++activeCount;
        }
    }

    if (activeCount == 0) {
        m_OutputTexture = m_SourceTexture;
    } else {
        for (const RenderLayerStep& step : steps) {
            if (!step.layer || !step.layer->IsVisible()) {
                continue;
            }

            const unsigned int originalInput = currentInput;
            unsigned int targetFBO = usePing ? m_PingFBO : m_PongFBO;
            unsigned int targetTex = usePing ? m_PingTexture : m_PongTexture;
            usePing = !usePing;

            glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
            glClear(GL_COLOR_BUFFER_BIT);
            step.layer->ExecuteWithSource(originalInput, m_SourceTexture, m_Width, m_Height, m_Quad);

            const unsigned int maskTexture = findMaskTexture(step.maskNodeId);
            if (maskTexture) {
                unsigned int blendFBO = usePing ? m_PingFBO : m_PongFBO;
                unsigned int blendTex = usePing ? m_PingTexture : m_PongTexture;
                usePing = !usePing;
                RenderMaskBlend(originalInput, targetTex, maskTexture, blendFBO);
                currentInput = blendTex;
            } else {
                currentInput = targetTex;
            }
        }
        m_OutputTexture = currentInput;
    }

    for (const auto& item : maskTextures) {
        unsigned int texture = item.second;
        glDeleteTextures(1, &texture);
    }

    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    if (prevScissor) glEnable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST);
    if (prevStencil) glEnable(GL_STENCIL_TEST);
    if (prevBlend) glEnable(GL_BLEND);
}

std::vector<unsigned char> RenderPipeline::GetOutputPixels(int& outW, int& outH) {
    outW = m_Width;
    outH = m_Height;
    if (m_OutputTexture == 0 || m_Width == 0 || m_Height == 0) return {};

    std::vector<unsigned char> pixels(m_Width * m_Height * 4);
    
    // Restore previous FBO binding
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    
    // Create a temporary FBO for reading
    unsigned int tempFBO;
    glGenFramebuffers(1, &tempFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTexture, 0);

    glReadPixels(0, 0, m_Width, m_Height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Flip vertically
    int rowSize = m_Width * 4;
    std::vector<unsigned char> tempRow(rowSize);
    for (int y = 0; y < m_Height / 2; y++) {
        unsigned char* row1 = &pixels[y * rowSize];
        unsigned char* row2 = &pixels[(m_Height - 1 - y) * rowSize];
        std::memcpy(tempRow.data(), row1, rowSize);
        std::memcpy(row1, row2, rowSize);
        std::memcpy(row2, tempRow.data(), rowSize);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glDeleteFramebuffers(1, &tempFBO);

    return pixels;
}

std::vector<unsigned char> RenderPipeline::GetScopesPixels(int& outW, int& outH) {
    if (m_OutputTexture == 0 || m_Width == 0 || m_Height == 0) return {};

    // Target a small size for analysis efficiency
    outW = 256;
    outH = 256;
    if (m_Width < outW) outW = m_Width;
    if (m_Height < outH) outH = m_Height;

    std::vector<unsigned char> pixels(outW * outH * 4);
    
    // Create a temporary FBO/Texture for downsampling
    unsigned int tempTex = GLHelpers::CreateEmptyTexture(outW, outH);
    unsigned int tempFBO;
    glGenFramebuffers(1, &tempFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tempTex, 0);

    // Blit from current output to small target (GPU downsample)
    GLint prevReadFBO, prevDrawFBO;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);

    unsigned int srcFBO;
    glGenFramebuffers(1, &srcFBO);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTexture, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tempFBO);
    glBlitFramebuffer(0, 0, m_Width, m_Height, 0, 0, outW, outH, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // Read small pixels
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    glReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Cleanup
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFBO);
    glDeleteFramebuffers(1, &srcFBO);
    glDeleteFramebuffers(1, &tempFBO);
    glDeleteTextures(1, &tempTex);

    return pixels;
}

std::vector<unsigned char> RenderPipeline::GetSourcePixels(int& outW, int& outH) {
    if (m_SourceTexture == 0 || m_Width == 0 || m_Height == 0 || m_SourcePixels.empty()) {
        outW = outH = 0;
        return {};
    }

    outW = m_Width;
    outH = m_Height;

    std::vector<unsigned char> pixels = m_SourcePixels;
    const int rowSize = m_Width * std::max(1, m_SourceChannels);
    std::vector<unsigned char> tempRow(rowSize);
    for (int y = 0; y < m_Height / 2; ++y) {
        unsigned char* row1 = &pixels[y * rowSize];
        unsigned char* row2 = &pixels[(m_Height - 1 - y) * rowSize];
        std::memcpy(tempRow.data(), row1, rowSize);
        std::memcpy(row1, row2, rowSize);
        std::memcpy(row2, tempRow.data(), rowSize);
    }

    return pixels;
}
