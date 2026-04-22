#include "HDRLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_HDRVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_HDRFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform float uTolerance;
uniform float uAmount;

void main() {
    vec4 color = texture(uInputTex, vUV);
    vec3 rgb = color.rgb;
    float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));

    if (luma < uTolerance && uTolerance > 0.0) {
        float factor = (uAmount / 100.0) * (1.0 - luma / uTolerance);
        rgb *= (1.0 - factor);
    }

    FragColor = vec4(clamp(rgb, 0.0, 1.0), color.a);
}
)";

HDRLayer::HDRLayer() {}

HDRLayer::~HDRLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void HDRLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_HDRVert, s_HDRFrag);
}

void HDRLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    (void)width;
    (void)height;

    glUseProgram(m_ShaderProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uTolerance"), m_Tolerance / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uAmount"), m_Amount);

    quad.Draw();
    glUseProgram(0);
}

void HDRLayer::RenderUI() {
    ImGui::SliderFloat("Tolerance", &m_Tolerance, 0.0f, 100.0f, "%.0f");
    ImGui::SliderFloat("Amount", &m_Amount, 0.0f, 100.0f, "%.0f");
}

json HDRLayer::Serialize() const {
    json j;
    j["type"] = "HDR";
    j["tolerance"] = m_Tolerance;
    j["amount"] = m_Amount;
    return j;
}

void HDRLayer::Deserialize(const json& j) {
    if (j.contains("tolerance")) m_Tolerance = j["tolerance"];
    if (j.contains("amount")) m_Amount = j["amount"];
}
