#include "VignetteLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_VigVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_VigFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform float uIntensity;
uniform float uRadius;
uniform float uSoftness;
uniform vec3 uColor;

void main() {
    vec4 color = texture(uInputTex, vUV);

    vec2 center = vUV - vec2(0.5);
    float dist = length(center);
    float vignette = smoothstep(uRadius, uRadius - uSoftness, dist);

    color.rgb = mix(uColor, color.rgb, mix(1.0, vignette, uIntensity));

    FragColor = color;
}
)";

VignetteLayer::VignetteLayer() {}

VignetteLayer::~VignetteLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void VignetteLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_VigVert, s_VigFrag);
}

void VignetteLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);

    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uIntensity"), m_Intensity);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uRadius"),    m_Radius);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSoftness"),  m_Softness);
    glUniform3f(glGetUniformLocation(m_ShaderProgram, "uColor"), m_Color[0], m_Color[1], m_Color[2]);

    quad.Draw();
    glUseProgram(0);
}

void VignetteLayer::RenderUI() {
    ImGui::SliderFloat("Intensity", &m_Intensity, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Radius",    &m_Radius,    0.0f, 1.5f, "%.2f");
    ImGui::SliderFloat("Softness",  &m_Softness,  0.0f, 1.0f, "%.2f");
    ImGui::ColorEdit3("Color",      m_Color);
}

json VignetteLayer::Serialize() const {
    json j;
    j["type"] = "Vignette";
    j["intensity"] = m_Intensity;
    j["radius"] = m_Radius;
    j["softness"] = m_Softness;
    j["color"] = {m_Color[0], m_Color[1], m_Color[2]};
    return j;
}

void VignetteLayer::Deserialize(const json& j) {
    if (j.contains("intensity")) m_Intensity = j["intensity"];
    if (j.contains("vigIntensity")) m_Intensity = j["vigIntensity"];
    if (j.contains("radius")) m_Radius = j["radius"];
    if (j.contains("vigRadius")) m_Radius = j["vigRadius"];
    if (j.contains("softness")) m_Softness = j["softness"];
    if (j.contains("vigSoftness")) m_Softness = j["vigSoftness"];
    if (j.contains("color") && j["color"].is_array() && j["color"].size() == 3) {
        m_Color[0] = j["color"][0];
        m_Color[1] = j["color"][1];
        m_Color[2] = j["color"][2];
    }
}
