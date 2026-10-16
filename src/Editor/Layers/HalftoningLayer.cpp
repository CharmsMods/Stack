#include "HalftoningLayer.h"
#include "Renderer/Operations/StylizeShaders.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>
#include "Utils/ImGuiExtras.h"



HalftoningLayer::HalftoningLayer() {}

HalftoningLayer::~HalftoningLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void HalftoningLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(Stack::Renderer::Operations::StylizeVertex(), Stack::Renderer::Operations::HalftoningFragment());
}

void HalftoningLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);

    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uRes"), (float)width, (float)height);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSize"), m_Size);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uIntensity"), m_Intensity);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSharpness"), m_Sharpness);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uPattern"), m_Pattern);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uColorMode"), m_ColorMode);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uGray"), m_Gray ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInvert"), m_Invert ? 1 : 0);

    quad.Draw();
    glUseProgram(0);
}

void HalftoningLayer::RenderUI() {
    ImGuiExtras::NodeSliderFloat("Cell Size", "##CellSize", &m_Size, 1.0f, 20.0f, "%.1f");
    ImGuiExtras::NodeSliderFloat("Intensity", "##Intensity", &m_Intensity, 0.0f, 1.0f);
    ImGuiExtras::NodeSliderFloat("Sharpness", "##Sharpness", &m_Sharpness, 0.0f, 1.0f);
    
    const char* patterns[] = { "Circles", "Lines", "Cross", "Diamond" };
    ImGuiExtras::NodeCombo("Pattern", "##Pattern", &m_Pattern, patterns, IM_ARRAYSIZE(patterns));
    
    const char* modes[] = { "Luminance", "RGB Dots", "CMY Dots", "CMYK Dots" };
    ImGuiExtras::NodeCombo("Color Mode", "##ColorMode", &m_ColorMode, modes, IM_ARRAYSIZE(modes));
    
    ImGuiExtras::NodeCheckbox("Grayscale Output", "##GrayscaleOutput", &m_Gray);
    ImGuiExtras::NodeCheckbox("Invert Pattern", "##InvertPattern", &m_Invert);
}

json HalftoningLayer::Serialize() const {
    json j;
    j["type"] = "Halftoning";
    j["size"] = m_Size;
    j["intensity"] = m_Intensity;
    j["sharpness"] = m_Sharpness;
    j["pattern"] = m_Pattern;
    j["colorMode"] = m_ColorMode;
    j["gray"] = m_Gray;
    j["invert"] = m_Invert;
    return j;
}

void HalftoningLayer::Deserialize(const json& j) {
    m_ExecutionScalar=j.value("executionScalar",false);
    m_ExecutionLuma=j.value("executionLumaWeights",std::array<float,3>{.2126f,.7152f,.0722f});
    m_ExecutionComponents=j.value("executionComponents",std::array<float,3>{1,1,1});
    m_ExecutionSourceSize=j.value("executionSourceSize",std::array<int,2>{0,0});
    if (j.contains("size")) m_Size = j["size"];
    if (j.contains("intensity")) m_Intensity = j["intensity"];
    if (j.contains("sharpness")) m_Sharpness = j["sharpness"];
    if (j.contains("pattern")) m_Pattern = j["pattern"];
    if (j.contains("colorMode")) m_ColorMode = j["colorMode"];
    if (j.contains("gray")) m_Gray = j["gray"];
    if (j.contains("invert")) m_Invert = j["invert"];
}
