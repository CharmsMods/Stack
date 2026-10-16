#include "CellShadingLayer.h"
#include "Renderer/Operations/StylizeShaders.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>
#include "Utils/ImGuiExtras.h"



CellShadingLayer::CellShadingLayer() {}

CellShadingLayer::~CellShadingLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void CellShadingLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(Stack::Renderer::Operations::StylizeVertex(), Stack::Renderer::Operations::CellShadingFragment());
}

void CellShadingLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uRes"), static_cast<float>(width), static_cast<float>(height));
    glUniform1i(glGetUniformLocation(m_ShaderProgram,"uScalar"),m_ExecutionScalar ? 1 : 0);
    glUniform3fv(glGetUniformLocation(m_ShaderProgram,"uLumaWeights"),1,m_ExecutionLuma.data());
    glUniform3fv(glGetUniformLocation(m_ShaderProgram,"uComponents"),1,m_ExecutionComponents.data());
    glUniform2f(glGetUniformLocation(m_ShaderProgram,"uSourceSize"),
        float(m_ExecutionSourceSize[0]>0 ? m_ExecutionSourceSize[0] : width),
        float(m_ExecutionSourceSize[1]>0 ? m_ExecutionSourceSize[1] : height));
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uLevels"), m_Levels);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBias"), m_Bias);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uGamma"), m_Gamma);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uQuantMode"), m_QuantMode);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uBandMap"), m_BandMap);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uEdgeMethod"), m_EdgeMethod);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uEdgeStr"), m_EdgeStrength / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uEdgeThick"), m_EdgeThickness);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uColorPreserve"), m_ColorPreserve / 100.0f);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uEdgeEnable"), m_ShowEdges ? 1 : 0);

    quad.Draw();
    glUseProgram(0);
}

void CellShadingLayer::RenderUI() {
    const char* quantModes[] = { "Luminance", "Per-Channel", "HSV Value" };
    const char* bandMaps[] = { "Linear", "Smooth", "Hard" };
    const char* edgeMethods[] = { "Sobel", "Roberts Cross", "Laplacian" };

    ImGuiExtras::NodeSliderInt("Shading Levels", "##ShadingLevels", &m_Levels, 2, 12);
    ImGuiExtras::NodeSliderFloat("Contrast Bias", "##ContrastBias", &m_Bias, -1.0f, 1.0f, "%.2f");
    ImGuiExtras::NodeSliderFloat("Gamma", "##Gamma", &m_Gamma, 0.1f, 3.0f, "%.2f");
    ImGuiExtras::NodeCombo("Quantization Mode", "##QuantizationMode", &m_QuantMode, quantModes, IM_ARRAYSIZE(quantModes));
    ImGuiExtras::NodeCombo("Band Mapping", "##BandMapping", &m_BandMap, bandMaps, IM_ARRAYSIZE(bandMaps));
    ImGui::Separator();
    ImGuiExtras::NodeCombo("Edge Method", "##EdgeMethod", &m_EdgeMethod, edgeMethods, IM_ARRAYSIZE(edgeMethods));
    ImGuiExtras::NodeSliderFloat("Edge Strength", "##EdgeStrength", &m_EdgeStrength, 0.0f, 200.0f, "%.0f");
    ImGuiExtras::NodeSliderFloat("Edge Thickness", "##EdgeThickness", &m_EdgeThickness, 0.5f, 5.0f, "%.1f");
    ImGuiExtras::NodeSliderFloat("Color Preserve", "##ColorPreserve", &m_ColorPreserve, 0.0f, 100.0f, "%.0f");
    ImGuiExtras::NodeCheckbox("Show Edges", "##ShowEdges", &m_ShowEdges);
}

json CellShadingLayer::Serialize() const {
    json j;
    j["type"] = "CellShading";
    j["levels"] = m_Levels;
    j["bias"] = m_Bias;
    j["gamma"] = m_Gamma;
    j["quantMode"] = m_QuantMode;
    j["bandMap"] = m_BandMap;
    j["edgeMethod"] = m_EdgeMethod;
    j["edgeStrength"] = m_EdgeStrength;
    j["edgeThickness"] = m_EdgeThickness;
    j["colorPreserve"] = m_ColorPreserve;
    j["showEdges"] = m_ShowEdges;
    return j;
}

void CellShadingLayer::Deserialize(const json& j) {
    m_ExecutionScalar=j.value("executionScalar",false);
    m_ExecutionLuma=j.value("executionLumaWeights",std::array<float,3>{.2126f,.7152f,.0722f});
    m_ExecutionComponents=j.value("executionComponents",std::array<float,3>{1,1,1});
    m_ExecutionSourceSize=j.value("executionSourceSize",std::array<int,2>{0,0});
    if (j.contains("levels")) m_Levels = j["levels"];
    if (j.contains("bias")) m_Bias = j["bias"];
    if (j.contains("gamma")) m_Gamma = j["gamma"];
    if (j.contains("quantMode")) m_QuantMode = j["quantMode"];
    if (j.contains("bandMap")) m_BandMap = j["bandMap"];
    if (j.contains("edgeMethod")) m_EdgeMethod = j["edgeMethod"];
    if (j.contains("edgeStrength")) m_EdgeStrength = j["edgeStrength"];
    if (j.contains("edgeThickness")) m_EdgeThickness = j["edgeThickness"];
    if (j.contains("colorPreserve")) m_ColorPreserve = j["colorPreserve"];
    if (j.contains("showEdges")) m_ShowEdges = j["showEdges"];
}
