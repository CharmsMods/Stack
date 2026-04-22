#include "LensDistortionLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_LensDistortionVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_LensDistortionFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform float uAmount;
uniform float uScale;

void main() {
    vec2 p = vUV * 2.0 - 1.0;
    float r2 = dot(p, p);
    float factor = 1.0 + r2 * uAmount;
    vec2 distorted = p * factor * uScale;
    vec2 uv = (distorted + 1.0) * 0.5;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    FragColor = texture(uInputTex, uv);
}
)";

LensDistortionLayer::LensDistortionLayer() {}

LensDistortionLayer::~LensDistortionLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void LensDistortionLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_LensDistortionVert, s_LensDistortionFrag);
}

void LensDistortionLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    (void)width;
    (void)height;

    glUseProgram(m_ShaderProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uAmount"), m_Amount / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uScale"), m_Scale / 100.0f);

    quad.Draw();
    glUseProgram(0);
}

void LensDistortionLayer::RenderUI() {
    ImGui::SliderFloat("Distortion Amount", &m_Amount, -100.0f, 100.0f, "%.0f");
    ImGui::TextDisabled("< Barrel (Fisheye) | Pincushion >");
    ImGui::SliderFloat("Scale Base", &m_Scale, 50.0f, 150.0f, "%.0f");
}

json LensDistortionLayer::Serialize() const {
    json j;
    j["type"] = "LensDistortion";
    j["amount"] = m_Amount;
    j["scale"] = m_Scale;
    return j;
}

void LensDistortionLayer::Deserialize(const json& j) {
    if (j.contains("amount")) m_Amount = j["amount"];
    if (j.contains("scale")) m_Scale = j["scale"];
}
