#include "SplitTransformLayers.h"

#include "Renderer/FullscreenQuad.h"
#include "Utils/ImGuiExtras.h"

namespace {

const char* kTransformVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kTransformFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform float uCropLeft;
uniform float uCropRight;
uniform float uCropTop;
uniform float uCropBottom;
uniform float uRotation;
uniform float uFlipH;
uniform float uFlipV;

void main() {
    vec2 uv = vUV;

    if (uFlipH > 0.5) uv.x = 1.0 - uv.x;
    if (uFlipV > 0.5) uv.y = 1.0 - uv.y;

    if (abs(uRotation) > 0.001) {
        float angle = radians(uRotation);
        vec2 center = vec2(0.5);
        uv -= center;
        float c = cos(angle);
        float s = sin(angle);
        uv = vec2(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
        uv += center;
    }

    float left = uCropLeft;
    float right = 1.0 - uCropRight;
    float top = uCropTop;
    float bottom = 1.0 - uCropBottom;

    if (uv.x < left || uv.x > right || uv.y < top || uv.y > bottom) {
        FragColor = vec4(0.0, 0.0, 0.0, 0.0);
    } else {
        FragColor = texture(uInputTex, uv);
    }
}
)";

} // namespace

SplitTransformLayerBase::~SplitTransformLayerBase() {
    if (m_ShaderProgram) {
        glDeleteProgram(m_ShaderProgram);
    }
}

void SplitTransformLayerBase::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(kTransformVert, kTransformFrag);
}

void SplitTransformLayerBase::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    (void)width;
    (void)height;

    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uCropLeft"), m_CropLeft / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uCropRight"), m_CropRight / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uCropTop"), m_CropTop / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uCropBottom"), m_CropBottom / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uRotation"), m_Rotation);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uFlipH"), m_FlipH ? 1.0f : 0.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uFlipV"), m_FlipV ? 1.0f : 0.0f);

    quad.Draw();
    glUseProgram(0);
}

json SplitTransformLayerBase::Serialize() const {
    json j;
    j["type"] = GetTypeId();
    j["cropLeft"] = m_CropLeft;
    j["cropRight"] = m_CropRight;
    j["cropTop"] = m_CropTop;
    j["cropBottom"] = m_CropBottom;
    j["rotation"] = m_Rotation;
    j["flipH"] = m_FlipH;
    j["flipV"] = m_FlipV;
    return j;
}

void SplitTransformLayerBase::Deserialize(const json& j) {
    if (j.contains("cropLeft")) m_CropLeft = j["cropLeft"];
    if (j.contains("cropRight")) m_CropRight = j["cropRight"];
    if (j.contains("cropTop")) m_CropTop = j["cropTop"];
    if (j.contains("cropBottom")) m_CropBottom = j["cropBottom"];
    if (j.contains("rotation")) m_Rotation = j["rotation"];
    if (j.contains("flipH")) m_FlipH = j["flipH"];
    if (j.contains("flipV")) m_FlipV = j["flipV"];
}

void CropLayer::RenderUI() {
    ImGuiExtras::NodeSliderFloat("Crop Left", "##CropLeft", &m_CropLeft, 0.0f, 50.0f, "%.1f %%");
    ImGuiExtras::NodeSliderFloat("Crop Right", "##CropRight", &m_CropRight, 0.0f, 50.0f, "%.1f %%");
    ImGuiExtras::NodeSliderFloat("Crop Top", "##CropTop", &m_CropTop, 0.0f, 50.0f, "%.1f %%");
    ImGuiExtras::NodeSliderFloat("Crop Bottom", "##CropBottom", &m_CropBottom, 0.0f, 50.0f, "%.1f %%");
}

void RotateLayer::RenderUI() {
    ImGuiExtras::NodeSliderFloat("Rotation", "##Rotation", &m_Rotation, -180.0f, 180.0f, "%.1f deg");
}

void FlipLayer::RenderUI() {
    ImGuiExtras::NodeCheckbox("Flip Horizontally", "##FlipHorizontally", &m_FlipH);
    ImGuiExtras::NodeCheckbox("Flip Vertically", "##FlipVertically", &m_FlipV);
}
