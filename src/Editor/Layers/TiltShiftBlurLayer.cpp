#include "TiltShiftBlurLayer.h"

#include "Renderer/FullscreenQuad.h"
#include <algorithm>
#include <imgui.h>

static const char* s_TiltShiftBlurVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_TiltShiftBlurFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uDir;
uniform float uRadius;
uniform int uBlurType;
uniform vec2 uCenter;
uniform float uFocusRadius;
uniform float uTransition;
uniform float uAspect;

void main() {
    vec4 original = texture(uInputTex, vUV);

    vec2 position = vec2(vUV.x, 1.0 - vUV.y);
    vec2 offsetFromCenter = position - uCenter;
    offsetFromCenter.x *= uAspect;

    float distanceFromFocus = length(offsetFromCenter);
    float fadeStart = uFocusRadius;
    float fadeEnd = uFocusRadius + max(0.001, uTransition);
    float blurScale = smoothstep(fadeStart, fadeEnd, distanceFromFocus);

    if (blurScale < 0.01 || uRadius <= 0.0001) {
        FragColor = original;
        return;
    }

    vec4 color = vec4(0.0);
    float total = 0.0;

    if (uBlurType == 1) {
        for (float i = -15.0; i <= 16.0; i += 1.0) {
            vec4 sampleColor = texture(uInputTex, vUV + uDir * i * uRadius * blurScale * 0.5);
            color += sampleColor;
            total += 1.0;
        }
    } else if (uBlurType == 2) {
        for (float i = -15.0; i <= 16.0; i += 1.0) {
            float weight = 1.0 - abs(i) / 16.0;
            vec4 sampleColor = texture(uInputTex, vUV + uDir * i * uRadius * blurScale);
            color += sampleColor * weight;
            total += weight;
        }
    } else {
        for (float i = -15.0; i <= 16.0; i += 1.0) {
            float weight = exp(-(i * i) / 50.0);
            vec4 sampleColor = texture(uInputTex, vUV + uDir * i * uRadius * blurScale * 0.5);
            color += sampleColor * weight;
            total += weight;
        }
    }

    FragColor = color / max(total, 0.001);
}
)";

TiltShiftBlurLayer::TiltShiftBlurLayer() {}

TiltShiftBlurLayer::~TiltShiftBlurLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
    if (m_IntermediateFbo) glDeleteFramebuffers(1, &m_IntermediateFbo);
    if (m_IntermediateTexture) glDeleteTextures(1, &m_IntermediateTexture);
}

void TiltShiftBlurLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_TiltShiftBlurVert, s_TiltShiftBlurFrag);
}

void TiltShiftBlurLayer::EnsureIntermediateTarget(int width, int height) {
    if (m_IntermediateTexture != 0 &&
        m_IntermediateFbo != 0 &&
        m_IntermediateWidth == width &&
        m_IntermediateHeight == height) {
        return;
    }

    if (m_IntermediateFbo) {
        glDeleteFramebuffers(1, &m_IntermediateFbo);
        m_IntermediateFbo = 0;
    }
    if (m_IntermediateTexture) {
        glDeleteTextures(1, &m_IntermediateTexture);
        m_IntermediateTexture = 0;
    }

    m_IntermediateTexture = GLHelpers::CreateEmptyTexture(width, height);
    m_IntermediateFbo = GLHelpers::CreateFBO(m_IntermediateTexture);
    m_IntermediateWidth = width;
    m_IntermediateHeight = height;
}

void TiltShiftBlurLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    EnsureIntermediateTarget(width, height);

    GLint previousViewport[4] {};
    GLint targetFbo = 0;
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &targetFbo);

    glUseProgram(m_ShaderProgram);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uBlurType"), m_BlurType);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uCenter"), m_CenterX, m_CenterY);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uFocusRadius"), m_FocusRadius / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uTransition"), m_Transition / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uAspect"), static_cast<float>(width) / std::max(1.0f, static_cast<float>(height)));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uRadius"), (m_BlurStrength / 100.0f) * 2.0f);

    glViewport(0, 0, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, m_IntermediateFbo);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uDir"), 1.0f / std::max(1, width), 0.0f);
    quad.Draw();

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(targetFbo));
    glBindTexture(GL_TEXTURE_2D, m_IntermediateTexture);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uDir"), 0.0f, 1.0f / std::max(1, height));
    quad.Draw();

    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
}

void TiltShiftBlurLayer::RenderUI() {
    const char* blurTypes[] = { "Gaussian", "Box", "Motion" };
    ImGui::Combo("Blur Filter Type", &m_BlurType, blurTypes, IM_ARRAYSIZE(blurTypes));
    ImGui::SliderFloat("Blur Strength", &m_BlurStrength, 0.0f, 100.0f, "%.0f");
    ImGui::SliderFloat("Focus Radius", &m_FocusRadius, 0.0f, 150.0f, "%.0f");
    ImGui::SliderFloat("Focus Falloff Transition", &m_Transition, 1.0f, 100.0f, "%.0f");
    ImGui::Separator();
    ImGui::TextDisabled("Web focus-pin editing is adapted here to direct position sliders.");
    ImGui::SliderFloat("Focus X", &m_CenterX, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("Focus Y", &m_CenterY, 0.0f, 1.0f, "%.3f");
    if (ImGui::Button("Center Focus")) {
        m_CenterX = 0.5f;
        m_CenterY = 0.5f;
    }
}

json TiltShiftBlurLayer::Serialize() const {
    json j;
    j["type"] = "TiltShiftBlur";
    j["blurType"] = m_BlurType;
    j["amount"] = m_BlurStrength;
    j["focusRadius"] = m_FocusRadius;
    j["transition"] = m_Transition;
    j["centerX"] = m_CenterX;
    j["centerY"] = m_CenterY;
    return j;
}

void TiltShiftBlurLayer::Deserialize(const json& j) {
    if (j.contains("blurType")) m_BlurType = j["blurType"];
    if (j.contains("amount")) m_BlurStrength = j["amount"];
    if (j.contains("focusRadius")) m_FocusRadius = j["focusRadius"];
    if (j.contains("transition")) m_Transition = j["transition"];
    if (j.contains("centerX")) m_CenterX = j["centerX"];
    if (j.contains("centerY")) m_CenterY = j["centerY"];
}
