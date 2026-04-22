#include "ChromaticAberrationLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_ChromaticAberrationVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_ChromaticAberrationFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform float uAmount;
uniform float uBlur;
uniform vec2 uCenter;
uniform float uRadius;
uniform float uFalloff;
uniform float uZoomBlur;
uniform int uFalloffToBlur;

void main() {
    if (uAmount <= 0.0 && uBlur <= 0.0 && uZoomBlur <= 0.0) {
        FragColor = texture(uInputTex, vUV);
        return;
    }

    vec2 dir = vUV - uCenter;
    float dist = length(dir);

    float clearMask = 0.0;
    if (uRadius > 0.0 || uFalloff > 0.0) {
        clearMask = 1.0 - smoothstep(uRadius, uRadius + uFalloff, dist);
    }

    float blurStrength = uBlur;
    float zoomStrength = uZoomBlur;
    if (uFalloffToBlur == 1) {
        blurStrength *= (1.0 - clearMask);
        zoomStrength *= (1.0 - clearMask);
    }

    float aberrationStrength = dist * dist * (uAmount / 1000.0);
    aberrationStrength *= (1.0 - clearMask);

    vec4 result = vec4(0.0);
    if (blurStrength > 0.0 || zoomStrength > 0.0) {
        float totalWeight = 0.0;
        for (int i = -2; i <= 2; ++i) {
            float fi = float(i);
            float jitter = fi * blurStrength * 0.002;
            vec2 zoomOffset = dir * (fi * zoomStrength * 0.02);
            float weight = exp(-(fi * fi) / 2.0);

            float r = texture(uInputTex, vUV - dir * aberrationStrength + vec2(jitter, -jitter) + zoomOffset).r;
            float g = texture(uInputTex, vUV + vec2(jitter * 0.5, jitter * 0.5) + zoomOffset * 0.5).g;
            float b = texture(uInputTex, vUV + dir * aberrationStrength + vec2(-jitter, jitter) + zoomOffset * 1.5).b;

            result += vec4(r, g, b, 1.0) * weight;
            totalWeight += weight;
        }
        result /= max(totalWeight, 0.0001);
        result.a = texture(uInputTex, vUV).a;
    } else {
        float r = texture(uInputTex, vUV - dir * aberrationStrength).r;
        float g = texture(uInputTex, vUV).g;
        float b = texture(uInputTex, vUV + dir * aberrationStrength).b;
        float a = texture(uInputTex, vUV).a;
        result = vec4(r, g, b, a);
    }

    FragColor = result;
}
)";

ChromaticAberrationLayer::ChromaticAberrationLayer() {}

ChromaticAberrationLayer::~ChromaticAberrationLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void ChromaticAberrationLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_ChromaticAberrationVert, s_ChromaticAberrationFrag);
}

void ChromaticAberrationLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    (void)width;
    (void)height;

    glUseProgram(m_ShaderProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uAmount"), m_Amount);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBlur"), m_EdgeBlur);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uCenter"), m_CenterX, m_CenterY);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uRadius"), m_Radius / 1000.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uFalloff"), m_Falloff / 1000.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uZoomBlur"), m_ZoomBlur / 50.0f);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uFalloffToBlur"), m_LinkFalloffToBlur ? 1 : 0);

    quad.Draw();
    glUseProgram(0);
}

void ChromaticAberrationLayer::RenderUI() {
    ImGui::SliderFloat("Amount", &m_Amount, 0.0f, 100.0f, "%.1f");
    ImGui::SliderFloat("Edge Blur", &m_EdgeBlur, 0.0f, 100.0f, "%.0f");
    ImGui::SliderFloat("Zoom Blur", &m_ZoomBlur, 0.0f, 100.0f, "%.0f");
    ImGui::Checkbox("Link Falloff To Blur", &m_LinkFalloffToBlur);
    ImGui::SliderFloat("Radius", &m_Radius, 0.0f, 100.0f, "%.0f");
    ImGui::SliderFloat("Falloff", &m_Falloff, 0.0f, 100.0f, "%.0f");
    ImGui::SliderFloat("Center X", &m_CenterX, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("Center Y", &m_CenterY, 0.0f, 1.0f, "%.3f");
    if (ImGui::Button("Reset Center")) {
        m_CenterX = 0.5f;
        m_CenterY = 0.5f;
    }
}

json ChromaticAberrationLayer::Serialize() const {
    json j;
    j["type"] = "ChromaticAberration";
    j["amount"] = m_Amount;
    j["edgeBlur"] = m_EdgeBlur;
    j["zoomBlur"] = m_ZoomBlur;
    j["linkFalloffToBlur"] = m_LinkFalloffToBlur;
    j["center"] = {m_CenterX, m_CenterY};
    j["radius"] = m_Radius;
    j["falloff"] = m_Falloff;
    return j;
}

void ChromaticAberrationLayer::Deserialize(const json& j) {
    if (j.contains("amount")) m_Amount = j["amount"];
    if (j.contains("edgeBlur")) m_EdgeBlur = j["edgeBlur"];
    if (j.contains("zoomBlur")) m_ZoomBlur = j["zoomBlur"];
    if (j.contains("linkFalloffToBlur")) m_LinkFalloffToBlur = j["linkFalloffToBlur"];
    if (j.contains("center") && j["center"].is_array() && j["center"].size() == 2) {
        m_CenterX = j["center"][0];
        m_CenterY = j["center"][1];
    }
    if (j.contains("radius")) m_Radius = j["radius"];
    if (j.contains("falloff")) m_Falloff = j["falloff"];
}
