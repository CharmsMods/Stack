#include "ExpanderLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_ExpanderVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_ExpanderFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uInputRes;
uniform vec2 uOutputRes;
uniform vec4 uFill;

void main() {
    vec2 pixelCoord = vUV * uOutputRes;
    vec2 inset = (uOutputRes - uInputRes) * 0.5;
    vec2 maxCoord = inset + uInputRes;

    if (
        pixelCoord.x >= inset.x &&
        pixelCoord.y >= inset.y &&
        pixelCoord.x < maxCoord.x &&
        pixelCoord.y < maxCoord.y
    ) {
        vec2 sampleUv = (pixelCoord - inset) / max(uInputRes, vec2(1.0));
        FragColor = texture(uInputTex, sampleUv);
    } else {
        FragColor = uFill;
    }
}
)";

ExpanderLayer::ExpanderLayer() {}

ExpanderLayer::~ExpanderLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void ExpanderLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_ExpanderVert, s_ExpanderFrag);
}

void ExpanderLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);

    // For now, in C++, we keep input and output at same resolution within the layer execute
    // Full parity would require resizing the pipeline fbos.
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uInputRes"), (float)width - m_Padding * 2.0f, (float)height - m_Padding * 2.0f);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uOutputRes"), (float)width, (float)height);
    glUniform4f(glGetUniformLocation(m_ShaderProgram, "uFill"), m_FillColor[0], m_FillColor[1], m_FillColor[2], m_FillColor[3]);

    quad.Draw();
    glUseProgram(0);
}

void ExpanderLayer::RenderUI() {
    ImGui::SliderFloat("Padding Offset", &m_Padding, 0.0f, 500.0f, "%.0f px");
    ImGui::ColorEdit4("Fill Color", m_FillColor);
}

json ExpanderLayer::Serialize() const {
    json j;
    j["type"] = "Expander";
    j["padding"] = m_Padding;
    j["fill"] = { m_FillColor[0], m_FillColor[1], m_FillColor[2], m_FillColor[3] };
    return j;
}

void ExpanderLayer::Deserialize(const json& j) {
    if (j.contains("padding")) m_Padding = j["padding"];
    if (j.contains("expanderPadding")) m_Padding = j["expanderPadding"];
    if (j.contains("fill") && j["fill"].is_array() && j["fill"].size() == 4) {
        for (int i = 0; i < 4; ++i) m_FillColor[i] = j["fill"][i];
    }
}
