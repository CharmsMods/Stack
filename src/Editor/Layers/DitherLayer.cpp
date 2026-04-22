#include "DitherLayer.h"

#include "Renderer/FullscreenQuad.h"
#include <algorithm>
#include <cstdlib>
#include <imgui.h>

static const char* s_DitherVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_DitherFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform int uType;
uniform float uBitDepth;
uniform float uPaletteSize;
uniform float uStrength;
uniform float uScale;
uniform vec2 uRes;
uniform float uSeed;
uniform int uUsePaletteBank;
uniform int uGammaCorrect;
uniform int uPaletteCount;
uniform vec3 uPaletteBank[8];

float getLuma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float bayer8x8(vec2 pos) {
    int x = int(mod(pos.x, 8.0));
    int y = int(mod(pos.y, 8.0));
    int index = x + y * 8;
    int pattern[64] = int[64](
         0, 32,  8, 40,  2, 34, 10, 42,
        48, 16, 56, 24, 50, 18, 58, 26,
        12, 44,  4, 36, 14, 46,  6, 38,
        60, 28, 52, 20, 62, 30, 54, 22,
         3, 35, 11, 43,  1, 33,  9, 41,
        51, 19, 59, 27, 49, 17, 57, 25,
        15, 47,  7, 39, 13, 45,  5, 37,
        63, 31, 55, 23, 61, 29, 53, 21
    );
    return float(pattern[index]) / 64.0;
}

float bayer4x4(vec2 pos) {
    int x = int(mod(pos.x, 4.0));
    int y = int(mod(pos.y, 4.0));
    int index = x + y * 4;
    int pattern[16] = int[16](
        0, 8, 2, 10,
        12, 4, 14, 6,
        3, 11, 1, 9,
        15, 7, 13, 5
    );
    return float(pattern[index]) / 16.0;
}

float bayer2x2(vec2 pos) {
    int x = int(mod(pos.x, 2.0));
    int y = int(mod(pos.y, 2.0));
    int index = x + y * 2;
    int pattern[4] = int[4](0, 2, 3, 1);
    return float(pattern[index]) / 4.0;
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float ign(vec2 p) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(p, magic.xy)));
}

float pseudoErrorDiffusion(vec2 uv, vec2 scaledPos) {
    vec2 px = 1.0 / uRes;
    float left = getLuma(texture(uInputTex, uv + vec2(-px.x, 0.0)).rgb);
    float downLeft = getLuma(texture(uInputTex, uv + vec2(-px.x, px.y)).rgb);
    float down = getLuma(texture(uInputTex, uv + vec2(0.0, px.y)).rgb);
    float downRight = getLuma(texture(uInputTex, uv + vec2(px.x, px.y)).rgb);
    float propagated = ((left * 7.0) + (downLeft * 3.0) + (down * 5.0) + downRight) / 16.0;
    float randomBias = hash12(scaledPos + vec2(uSeed, uSeed * 1.37)) * 0.2;
    return clamp((propagated - 0.5) + randomBias, -0.5, 0.5);
}

void main() {
    vec4 source = texture(uInputTex, vUV);
    vec3 color = source.rgb;

    if (uGammaCorrect == 1) {
        color = pow(color, vec3(2.2));
    }

    vec2 scaledPos = floor(vUV * uRes / max(1.0, uScale));
    float threshold = 0.0;
    if (uType == 0) {
        threshold = bayer8x8(scaledPos) - 0.5;
    } else if (uType == 1) {
        threshold = pseudoErrorDiffusion(vUV, scaledPos);
    } else if (uType == 2) {
        threshold = hash12(scaledPos + vec2(uSeed, uSeed * 1.91)) - 0.5;
    } else if (uType == 3) {
        threshold = bayer4x4(scaledPos) - 0.5;
    } else if (uType == 4) {
        threshold = bayer2x2(scaledPos) - 0.5;
    } else {
        threshold = ign(scaledPos + vec2(uSeed * 0.33, uSeed * 0.77)) - 0.5;
    }

    float levels = pow(2.0, uBitDepth);
    vec3 dithered = color + threshold * uStrength * (1.0 / max(levels, 1.0));

    vec3 result = dithered;
    if (uUsePaletteBank == 1 && uPaletteCount > 0) {
        float minDistance = 1e10;
        result = uPaletteBank[0];
        for (int i = 0; i < 8; ++i) {
            if (i >= uPaletteCount) break;
            float paletteDistance = distance(dithered, uPaletteBank[i]);
            if (paletteDistance < minDistance) {
                minDistance = paletteDistance;
                result = uPaletteBank[i];
            }
        }
    } else {
        result = floor(dithered * levels + 0.5) / max(levels, 1.0);
        float paletteSteps = max(2.0, uPaletteSize);
        result = floor(result * (paletteSteps - 1.0) + 0.5) / max(1.0, paletteSteps - 1.0);
    }

    if (uGammaCorrect == 1) {
        result = pow(clamp(result, 0.0, 1.0), vec3(1.0 / 2.2));
    }

    FragColor = vec4(clamp(result, 0.0, 1.0), source.a);
}
)";

DitherLayer::DitherLayer() {
    m_Seed = 0.371f;
    m_PaletteBank = {
        0.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 1.0f,
        0.89f, 0.20f, 0.24f,
        0.18f, 0.75f, 0.35f,
        0.20f, 0.45f, 0.92f,
        0.96f, 0.85f, 0.22f,
        0.82f, 0.26f, 0.84f,
        0.15f, 0.78f, 0.82f
    };
}

DitherLayer::~DitherLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void DitherLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_DitherVert, s_DitherFrag);
}

void DitherLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);

    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uType"), m_Type);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBitDepth"), static_cast<float>(m_BitDepth));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uPaletteSize"), static_cast<float>(m_PaletteSize));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uStrength"), m_Strength / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uScale"), m_Scale);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uRes"), static_cast<float>(width), static_cast<float>(height));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSeed"), m_Seed);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uUsePaletteBank"), m_UsePaletteBank ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uGammaCorrect"), m_UseGamma ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uPaletteCount"), 8);
    glUniform3fv(glGetUniformLocation(m_ShaderProgram, "uPaletteBank"), 8, m_PaletteBank.data());

    quad.Draw();
    glUseProgram(0);
}

void DitherLayer::RenderUI() {
    const char* types[] = {
        "Ordered (Bayer 8x8)",
        "Error Diffusion (Native Approximation)",
        "White Noise",
        "Bayer 4x4",
        "Bayer 2x2",
        "Interleaved Gradient"
    };

    ImGui::Combo("Type", &m_Type, types, IM_ARRAYSIZE(types));
    ImGui::SliderInt("Bit Depth", &m_BitDepth, 1, 8);
    ImGui::SliderInt("Palette Size", &m_PaletteSize, 2, 256);
    ImGui::SliderFloat("Strength", &m_Strength, 0.0f, 100.0f, "%.0f");
    ImGui::SliderFloat("Scale", &m_Scale, 1.0f, 8.0f, "%.0f");
    ImGui::Checkbox("Gamma Correct", &m_UseGamma);
    ImGui::Checkbox("Use Layer Palette Bank", &m_UsePaletteBank);

    if (m_UsePaletteBank && ImGui::TreeNode("Palette Bank")) {
        ImGui::TextDisabled("The web app uses a shared Studio palette. Native adapts that here to a layer-local palette bank.");
        for (int index = 0; index < 8; ++index) {
            float* color = &m_PaletteBank[index * 3];
            char label[32];
            snprintf(label, sizeof(label), "Color %d", index + 1);
            ImGui::ColorEdit3(label, color);
        }
        ImGui::TreePop();
    }

    if (ImGui::Button("Randomize Seed")) {
        m_Seed = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    }
}

json DitherLayer::Serialize() const {
    json j;
    j["type"] = "Dither";
    j["bitDepth"] = m_BitDepth;
    j["paletteSize"] = m_PaletteSize;
    j["strength"] = m_Strength;
    j["scale"] = m_Scale;
    j["ditherType"] = m_Type;
    j["gammaCorrect"] = m_UseGamma;
    j["usePaletteBank"] = m_UsePaletteBank;
    j["seed"] = m_Seed;

    json palette = json::array();
    for (int index = 0; index < 8; ++index) {
        palette.push_back({
            m_PaletteBank[index * 3 + 0],
            m_PaletteBank[index * 3 + 1],
            m_PaletteBank[index * 3 + 2]
        });
    }
    j["paletteBank"] = palette;
    return j;
}

void DitherLayer::Deserialize(const json& j) {
    if (j.contains("bitDepth")) m_BitDepth = j["bitDepth"];
    if (j.contains("paletteSize")) m_PaletteSize = j["paletteSize"];
    if (j.contains("strength")) m_Strength = j["strength"];
    if (j.contains("scale")) m_Scale = j["scale"];
    if (j.contains("ditherType")) m_Type = j["ditherType"];
    if (j.contains("gammaCorrect")) m_UseGamma = j["gammaCorrect"];
    if (j.contains("usePaletteBank")) m_UsePaletteBank = j["usePaletteBank"];
    if (j.contains("seed")) m_Seed = j["seed"];
    if (j.contains("paletteBank") && j["paletteBank"].is_array()) {
        const auto& palette = j["paletteBank"];
        for (int index = 0; index < 8 && index < static_cast<int>(palette.size()); ++index) {
            if (!palette[index].is_array() || palette[index].size() < 3) continue;
            m_PaletteBank[index * 3 + 0] = palette[index][0];
            m_PaletteBank[index * 3 + 1] = palette[index][1];
            m_PaletteBank[index * 3 + 2] = palette[index][2];
        }
    }
}
