#include "BilateralFilterLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_BilateralVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_BilateralFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uRes;
uniform int uRadius;
uniform float uSigmaCol;
uniform float uSigmaSpace;
uniform int uKernel;
uniform int uEdgeMode;

float getDist(vec3 c1, vec3 c2) {
    if (uEdgeMode == 0) {
        float l1 = dot(c1, vec3(0.2126, 0.7152, 0.0722));
        float l2 = dot(c2, vec3(0.2126, 0.7152, 0.0722));
        return abs(l1 - l2);
    } else {
        return length(c1 - c2);
    }
}

void main() {
    vec4 centerCol = texture(uInputTex, vUV);
    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    
    int r = uRadius;
    float fs = uSigmaSpace;
    float fc = uSigmaCol;
    
    int step = (r > 15) ? 3 : ((r > 8) ? 2 : 1);
    
    for (int x = -r; x <= r; x += step) {
        for (int y = -r; y <= r; y += step) {
            vec2 offset = vec2(float(x), float(y));
            vec2 uv = vUV + offset / uRes;
            
            vec3 samp = texture(uInputTex, uv).rgb;
            
            float spaceDistSq = dot(offset, offset);
            float colorDist = getDist(centerCol.rgb, samp);
            
            float wSpace = 1.0;
            if (uKernel == 0) wSpace = exp(-spaceDistSq / (2.0 * fs * fs));
            
            float wColor = exp(-(colorDist * colorDist) / (2.0 * fc * fc));
            
            float w = wSpace * wColor;
            
            sum += samp * w;
            weightSum += w;
        }
    }
    
    FragColor = vec4(sum / weightSum, centerCol.a);
}
)";

BilateralFilterLayer::BilateralFilterLayer() {}

BilateralFilterLayer::~BilateralFilterLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void BilateralFilterLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_BilateralVert, s_BilateralFrag);
}

void BilateralFilterLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);

    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uRes"), (float)width, (float)height);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uRadius"), m_Radius);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSigmaCol"), m_SigmaCol);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSigmaSpace"), m_SigmaSpace);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uKernel"), m_Kernel);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uEdgeMode"), m_EdgeMode);

    quad.Draw();
    glUseProgram(0);
}

void BilateralFilterLayer::RenderUI() {
    ImGui::SliderInt("Radius", &m_Radius, 1, 30);
    ImGui::SliderFloat("Color Sigma", &m_SigmaCol, 0.01f, 1.0f);
    ImGui::SliderFloat("Spatial Sigma", &m_SigmaSpace, 0.5f, 15.0f);
    
    const char* kernels[] = { "Gaussian", "Box" };
    ImGui::Combo("Kernel", &m_Kernel, kernels, IM_ARRAYSIZE(kernels));
    
    const char* modes[] = { "Luminance", "RGB" };
    ImGui::Combo("Edge Mode", &m_EdgeMode, modes, IM_ARRAYSIZE(modes));
}

json BilateralFilterLayer::Serialize() const {
    json j;
    j["type"] = "BilateralFilter";
    j["radius"] = m_Radius;
    j["sigmaCol"] = m_SigmaCol;
    j["sigmaSpace"] = m_SigmaSpace;
    j["kernel"] = m_Kernel;
    j["edgeMode"] = m_EdgeMode;
    return j;
}

void BilateralFilterLayer::Deserialize(const json& j) {
    if (j.contains("radius")) m_Radius = j["radius"];
    if (j.contains("bilateralRadius")) m_Radius = j["bilateralRadius"];
    if (j.contains("sigmaCol")) m_SigmaCol = j["sigmaCol"];
    if (j.contains("bilateralColorSig")) m_SigmaCol = j["bilateralColorSig"];
    if (j.contains("sigmaSpace")) m_SigmaSpace = j["sigmaSpace"];
    if (j.contains("bilateralSpatialSig")) m_SigmaSpace = j["bilateralSpatialSig"];
    if (j.contains("kernel")) m_Kernel = j["kernel"];
    if (j.contains("bilateralKernel")) m_Kernel = j["bilateralKernel"];
    if (j.contains("edgeMode")) m_EdgeMode = j["edgeMode"];
    if (j.contains("bilateralEdgeMode")) m_EdgeMode = j["bilateralEdgeMode"];
}
