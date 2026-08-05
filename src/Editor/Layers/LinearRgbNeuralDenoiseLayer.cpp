#include "LinearRgbNeuralDenoiseLayer.h"

#include "Renderer/FullscreenQuad.h"
#include "Renderer/GLHelpers.h"

#include <imgui.h>

namespace {

const char* kCopyVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kCopyFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uInputTex;
void main() {
    FragColor = texture(uInputTex, vUV);
}
)";

} // namespace

LinearRgbNeuralDenoiseLayer::~LinearRgbNeuralDenoiseLayer() {
    if (m_CopyProgram != 0) {
        glDeleteProgram(m_CopyProgram);
    }
}

void LinearRgbNeuralDenoiseLayer::InitializeGL() {
    if (m_CopyProgram == 0) {
        m_CopyProgram = GLHelpers::CreateShaderProgram(kCopyVert, kCopyFrag);
    }
}

void LinearRgbNeuralDenoiseLayer::Execute(
    unsigned int inputTexture,
    int width,
    int height,
    FullscreenQuad& quad) {
    (void)width;
    (void)height;
    DrawCopy(inputTexture, quad);
}

void LinearRgbNeuralDenoiseLayer::DrawCopy(unsigned int inputTexture, FullscreenQuad& quad) {
    if (m_CopyProgram == 0) {
        InitializeGL();
    }
    glUseProgram(m_CopyProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_CopyProgram, "uInputTex"), 0);
    quad.Draw();
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void LinearRgbNeuralDenoiseLayer::RenderUI() {
    RenderUI(nullptr);
}

void LinearRgbNeuralDenoiseLayer::RenderUI(EditorModule* editor) {
    (void)editor;
    ImGui::TextUnformatted("LEGACY NEURAL DENOISE");
    ImGui::TextWrapped(
        "This retired node is preserved only so older Stack projects continue "
        "to load. It always passes the incoming image through unchanged.");
    if (!m_Settings.selectedModelId.empty()) {
        ImGui::TextDisabled("Preserved model id: %s", m_Settings.selectedModelId.c_str());
    }
    ImGui::TextDisabled("External model and runtime loading is disabled.");
}

NodeSurfaceSpec LinearRgbNeuralDenoiseLayer::GetNodeSurfaceSpec() const {
    NodeSurfaceSpec spec;
    spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
    spec.density = NodeSurfaceDensity::Normal;
    spec.preferredWidth = 360.0f;
    spec.maxWidth = 440.0f;
    return spec;
}

void LinearRgbNeuralDenoiseLayer::RenderExpandedNodeSurface(
    EditorModule* editor,
    const NodeSurfaceContext& context) {
    (void)context;
    RenderUI(editor);
}

json LinearRgbNeuralDenoiseLayer::Serialize() const {
    json value = NeuralDenoise::SerializeSettings(m_Settings);
    value["type"] = "LinearRgbNeuralDenoise";
    value["retiredExecution"] = true;
    return value;
}

void LinearRgbNeuralDenoiseLayer::Deserialize(const json& j) {
    m_Settings = NeuralDenoise::DeserializeSettings(j);
}
