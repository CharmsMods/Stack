#include "TextOverlayLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_TextVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_TextFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform sampler2D uOverlayTex;
uniform vec2 uRes;
uniform vec2 uOverlayPos;
uniform vec2 uOverlaySize;
uniform float uOpacity;
uniform float uRotation;

mat2 rotationMatrix(float rads) {
    float c = cos(rads);
    float s = sin(rads);
    return mat2(c, -s, s, c);
}

void main() {
    vec4 base = texture(uInputTex, vUV);
    vec2 pixel = vUV * uRes;
    vec2 center = uOverlayPos + (uOverlaySize * 0.5);
    vec2 localPixel = rotationMatrix(radians(uRotation)) * (pixel - center);
    vec2 samplePixel = localPixel + (uOverlaySize * 0.5);

    if (samplePixel.x < 0.0 || samplePixel.y < 0.0 || samplePixel.x > uOverlaySize.x || samplePixel.y > uOverlaySize.y) {
        FragColor = base;
        return;
    }

    vec2 overlayUv = samplePixel / uOverlaySize;
    vec4 overlay = texture(uOverlayTex, overlayUv);
    float alpha = clamp(overlay.a * uOpacity, 0.0, 1.0);
    vec3 rgb = mix(base.rgb, overlay.rgb, alpha);
    float outAlpha = max(base.a, alpha); 
    FragColor = vec4(rgb, outAlpha);
}
)";

TextOverlayLayer::TextOverlayLayer() {}

TextOverlayLayer::~TextOverlayLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
    if (m_OverlayTexture) glDeleteTextures(1, &m_OverlayTexture);
}

void TextOverlayLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_TextVert, s_TextFrag);
    
    // Create a tiny transparent placeholder texture for the overlay
    unsigned char pixels[] = { 0, 0, 0, 0 };
    m_OverlayTexture = GLHelpers::CreateTextureFromPixels(pixels, 1, 1, 4);
}

void TextOverlayLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_OverlayTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uOverlayTex"), 1);

    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uRes"), (float)width, (float)height);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uOverlayPos"), m_PosX, m_PosY);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uOverlaySize"), m_SizeX, m_SizeY);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uOpacity"), m_Opacity);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uRotation"), m_Rotation);

    quad.Draw();
    glUseProgram(0);
}

void TextOverlayLayer::RenderUI() {
    ImGui::Text("Text Layer Placeholder");
    ImGui::TextDisabled("(Texture generation not yet implemented in C++)");
    ImGui::SliderFloat("Position X", &m_PosX, -500.0f, 2000.0f);
    ImGui::SliderFloat("Position Y", &m_PosY, -500.0f, 2000.0f);
    ImGui::SliderFloat("Width", &m_SizeX, 1.0f, 1000.0f);
    ImGui::SliderFloat("Height", &m_SizeY, 1.0f, 1000.0f);
    ImGui::SliderFloat("Opacity", &m_Opacity, 0.0f, 1.0f);
    ImGui::SliderFloat("Rotation", &m_Rotation, -180.0f, 180.0f);
}

json TextOverlayLayer::Serialize() const {
    json j;
    j["type"] = "TextOverlay";
    j["posX"] = m_PosX;
    j["posY"] = m_PosY;
    j["sizeX"] = m_SizeX;
    j["sizeY"] = m_SizeY;
    j["opacity"] = m_Opacity;
    j["rotation"] = m_Rotation;
    return j;
}

void TextOverlayLayer::Deserialize(const json& j) {
    if (j.contains("posX")) m_PosX = j["posX"];
    if (j.contains("textX")) m_PosX = j["textX"];
    if (j.contains("posY")) m_PosY = j["posY"];
    if (j.contains("textY")) m_PosY = j["textY"];
    if (j.contains("sizeX")) m_SizeX = j["sizeX"];
    if (j.contains("sizeY")) m_SizeY = j["sizeY"];
    if (j.contains("opacity")) m_Opacity = j["opacity"];
    if (j.contains("textOpacity")) m_Opacity = j["textOpacity"];
    if (j.contains("rotation")) m_Rotation = j["rotation"];
    if (j.contains("textRotation")) m_Rotation = j["textRotation"];
}
