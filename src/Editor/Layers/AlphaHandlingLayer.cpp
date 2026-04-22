#include "AlphaHandlingLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_AlphaVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_AlphaFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;     // Processed
uniform sampler2D uSourceTex;    // Original
uniform int uMode;

void main() {
    vec4 processedColor = texture(uInputTex, vUV);
    vec4 sourceColor = texture(uSourceTex, vUV);

    bool protectPixel = false;
    if (uMode == 1) {
        protectPixel = sourceColor.a <= 0.001;
    } else if (uMode == 2) {
        protectPixel = sourceColor.a < 0.999;
    }

    FragColor = protectPixel ? sourceColor : processedColor;
}
)";

AlphaHandlingLayer::AlphaHandlingLayer() {}

AlphaHandlingLayer::~AlphaHandlingLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void AlphaHandlingLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_AlphaVert, s_AlphaFrag);
}

void AlphaHandlingLayer::ExecuteWithSource(unsigned int inputTexture, unsigned int sourceTexture, int width, int height, FullscreenQuad& quad) {
    (void)width;
    (void)height;

    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uSourceTex"), 1);

    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uMode"), m_Mode);

    quad.Draw();
    glUseProgram(0);
}

void AlphaHandlingLayer::RenderUI() {
    const char* modes[] = { "Protect Transparent", "Protect Opaque" };
    int currentIdx = m_Mode - 1;
    if (ImGui::Combo("Protection Mode", &currentIdx, modes, IM_ARRAYSIZE(modes))) {
        m_Mode = currentIdx + 1;
    }
}

json AlphaHandlingLayer::Serialize() const {
    json j;
    j["type"] = "AlphaHandling";
    j["mode"] = m_Mode;
    return j;
}

void AlphaHandlingLayer::Deserialize(const json& j) {
    if (j.contains("mode")) m_Mode = j["mode"];
    if (j.contains("alphaMode")) m_Mode = j["alphaMode"];
}
