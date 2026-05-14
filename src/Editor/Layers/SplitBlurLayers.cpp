#include "SplitBlurLayers.h"

#include "Renderer/FullscreenQuad.h"
#include "Utils/ImGuiExtras.h"
#include <imgui.h>

namespace {

const char* kBlurVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kBlurFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uTexelSize;
uniform float uAmount;
uniform int uType;

void main() {
    vec4 result = vec4(0.0);
    int radius = int(max(1.0, uAmount));
    float count = 0.0;

    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            vec2 offset = vec2(float(x), float(y)) * uTexelSize;
            float weight = 1.0;

            if (uType == 1) {
                float distSq = dot(vec2(x, y), vec2(x, y));
                weight = exp(-distSq / (2.0 * uAmount * uAmount / 4.0));
            }

            result += texture(uInputTex, vUV + offset) * weight;
            count += weight;
        }
    }

    FragColor = result / count;
}
)";

} // namespace

SplitBlurLayerBase::~SplitBlurLayerBase() {
    if (m_ShaderProgram) {
        glDeleteProgram(m_ShaderProgram);
    }
}

void SplitBlurLayerBase::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(kBlurVert, kBlurFrag);
}

void SplitBlurLayerBase::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uTexelSize"), 1.0f / width, 1.0f / height);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uAmount"), m_Amount);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uType"), GetBlurType());

    quad.Draw();
    glUseProgram(0);
}

void SplitBlurLayerBase::RenderUI() {
    ImGuiExtras::NodeSliderFloat("Blur Radius / Amount", "##BlurAmount", &m_Amount, 0.5f, 16.0f, "%.1f");
}

json SplitBlurLayerBase::Serialize() const {
    json j;
    j["type"] = GetTypeId();
    j["amount"] = m_Amount;
    return j;
}

void SplitBlurLayerBase::Deserialize(const json& j) {
    if (j.contains("amount")) {
        m_Amount = j["amount"];
    }
    if (j.contains("blurRadius")) {
        m_Amount = j["blurRadius"];
    }
}
