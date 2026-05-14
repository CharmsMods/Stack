#include "SplitHeatDistortionLayers.h"

#include "Renderer/FullscreenQuad.h"
#include "Utils/ImGuiExtras.h"
#include <imgui.h>

namespace {

const char* kHeatVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kHeatFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform float uIntensity;
uniform float uPhase;
uniform float uScale;
uniform int uDirection;

void main() {
    vec2 uv = vUV;
    vec2 offset = vec2(0.0);
    float phase = uPhase * 6.28318530718;

    if (uDirection == 0) {
        offset.x = sin(uv.y * uScale + phase) * uIntensity;
        offset.x += cos(uv.y * uScale * 2.5 - phase * 1.5) * (uIntensity * 0.3);
    } else if (uDirection == 1) {
        offset.y = sin(uv.x * uScale + phase) * uIntensity;
        offset.y += cos(uv.x * uScale * 2.5 - phase * 1.5) * (uIntensity * 0.3);
    } else {
        vec2 center = vec2(0.5, 0.5);
        vec2 delta = uv - center;
        float dist = length(delta);
        float wave = sin(dist * uScale - phase) * uIntensity;
        if (dist > 0.0001) {
            offset = normalize(delta) * wave;
        }
    }

    vec2 finalUV = clamp(uv + offset, 0.0, 1.0);
    FragColor = texture(uInputTex, finalUV);
}
)";

} // namespace

SplitHeatDistortionLayerBase::~SplitHeatDistortionLayerBase() {
    if (m_ShaderProgram) {
        glDeleteProgram(m_ShaderProgram);
    }
}

void SplitHeatDistortionLayerBase::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(kHeatVert, kHeatFrag);
}

void SplitHeatDistortionLayerBase::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    (void)width;
    (void)height;

    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uIntensity"), m_Intensity / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uPhase"), m_Phase / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uScale"), m_Scale);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uDirection"), GetDirection());

    quad.Draw();
    glUseProgram(0);
}

void SplitHeatDistortionLayerBase::RenderUI() {
    ImGuiExtras::NodeSliderFloat("Intensity", "##Intensity", &m_Intensity, 0.0f, 100.0f, "%.0f");
    ImGuiExtras::NodeSliderFloat("Phase", "##Phase", &m_Phase, 0.0f, 200.0f, "%.0f");
    ImGuiExtras::NodeSliderFloat("Scale", "##Scale", &m_Scale, 1.0f, 100.0f, "%.0f");

    if (HasDirectionSelector()) {
        const char* directions[] = { "Vertical (Heat)", "Horizontal" };
        ImGuiExtras::NodeCombo("Direction", "##Direction", &m_AxisDirection, directions, IM_ARRAYSIZE(directions));
    }
}

json SplitHeatDistortionLayerBase::Serialize() const {
    json j;
    j["type"] = GetTypeId();
    j["intensity"] = m_Intensity;
    j["phase"] = m_Phase;
    j["scale"] = m_Scale;
    if (HasDirectionSelector()) {
        j["direction"] = m_AxisDirection;
    }
    return j;
}

void SplitHeatDistortionLayerBase::Deserialize(const json& j) {
    if (j.contains("intensity")) m_Intensity = j["intensity"];
    if (j.contains("heatwaveIntensity")) m_Intensity = j["heatwaveIntensity"];
    if (j.contains("phase")) m_Phase = j["phase"];
    if (j.contains("heatwaveSpeed")) m_Phase = j["heatwaveSpeed"];
    if (j.contains("scale")) m_Scale = j["scale"];
    if (j.contains("heatwaveScale")) m_Scale = j["heatwaveScale"];
    if (HasDirectionSelector()) {
        if (j.contains("direction")) m_AxisDirection = j["direction"];
        if (j.contains("heatwaveDirection")) m_AxisDirection = j["heatwaveDirection"];
    }
}
