#include "HalftoningLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_HalftoneVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_HalftoneFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uRes;
uniform float uSize;
uniform float uIntensity;
uniform float uSharpness;
uniform int uPattern;
uniform int uColorMode;
uniform int uGray;
uniform int uInvert;

float getPattern(vec2 uv, float angle) {
    float s = sin(angle), c = cos(angle);
    vec2 p = vec2(c * uv.x - s * uv.y, s * uv.x + c * uv.y) * uRes / uSize;
    vec2 grid = fract(p) - 0.5;
    
    float d = 0.0;
    if (uPattern == 0) { // Circle
        d = length(grid) * 2.0;
    } else if (uPattern == 1) { // Line
        d = abs(grid.y) * 2.0;
    } else if (uPattern == 2) { // Cross
        d = min(abs(grid.x), abs(grid.y)) * 2.0;
    } else { // Diamond
        d = (abs(grid.x) + abs(grid.y));
    }
    
    return d;
}

void main() {
    vec4 col = texture(uInputTex, vUV);
    vec3 outRGB = vec3(0.0);
    
    if (uColorMode == 0) { // Luminance
        float l = dot(col.rgb, vec3(0.2126, 0.7152, 0.0722));
        float pat = getPattern(vUV, 0.785);
        float thresh = 1.0 - l * uIntensity;
        float softness = 1.0 - uSharpness;
        float val = smoothstep(thresh - softness, thresh + softness, pat);
        if (uInvert == 1) val = 1.0 - val;
        outRGB = vec3(val);
    } else if (uColorMode == 1) { // RGB
        float pR = getPattern(vUV, 0.26);
        float pG = getPattern(vUV, 1.30);
        float pB = getPattern(vUV, 0.0);
        float soft = 1.0 - uSharpness;
        float r = smoothstep((1.0 - col.r) - soft, (1.0 - col.r) + soft, pR);
        float g = smoothstep((1.0 - col.g) - soft, (1.0 - col.g) + soft, pG);
        float b = smoothstep((1.0 - col.b) - soft, (1.0 - col.b) + soft, pB);
        outRGB = vec3(r, g, b);
        if (uInvert == 1) outRGB = 1.0 - outRGB;
    } else { // CMY / CMYK
        vec3 cmy = 1.0 - col.rgb;
        float k = 0.0;
        if (uColorMode == 3) { // CMYK
            k = min(min(cmy.x, cmy.y), cmy.z);
            cmy = (cmy - k) / (1.0 - k);
        }
        float pC = getPattern(vUV, 0.26);
        float pM = getPattern(vUV, 1.30);
        float pY = getPattern(vUV, 0.0);
        float pK = getPattern(vUV, 0.785);
        float soft = 1.0 - uSharpness;
        float hC = 1.0 - smoothstep(cmy.x - soft, cmy.x + soft, pC);
        float hM = 1.0 - smoothstep(cmy.y - soft, cmy.y + soft, pM);
        float hY = 1.0 - smoothstep(cmy.z - soft, cmy.z + soft, pY);
        float hK = 1.0 - smoothstep(k - soft, k + soft, pK);
        vec3 resCMY = vec3(hC, hM, hY);
        if (uColorMode == 3) resCMY += vec3(hK);
        outRGB = 1.0 - clamp(resCMY, 0.0, 1.0);
        if (uInvert == 1) outRGB = 1.0 - outRGB;
    }
    
    if (uGray == 1) {
        float l = dot(outRGB, vec3(0.2126, 0.7152, 0.0722));
        outRGB = vec3(l);
    }
    
    FragColor = vec4(outRGB, col.a);
}
)";

HalftoningLayer::HalftoningLayer() {}

HalftoningLayer::~HalftoningLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void HalftoningLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_HalftoneVert, s_HalftoneFrag);
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
    ImGui::SliderFloat("Cell Size", &m_Size, 1.0f, 20.0f, "%.1f");
    ImGui::SliderFloat("Intensity", &m_Intensity, 0.0f, 1.0f);
    ImGui::SliderFloat("Sharpness", &m_Sharpness, 0.0f, 1.0f);
    
    const char* patterns[] = { "Circles", "Lines", "Cross", "Diamond" };
    ImGui::Combo("Pattern", &m_Pattern, patterns, IM_ARRAYSIZE(patterns));
    
    const char* modes[] = { "Luminance", "RGB Dots", "CMY Dots", "CMYK Dots" };
    ImGui::Combo("Color Mode", &m_ColorMode, modes, IM_ARRAYSIZE(modes));
    
    ImGui::Checkbox("Grayscale Output", &m_Gray);
    ImGui::Checkbox("Invert Pattern", &m_Invert);
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
    if (j.contains("size")) m_Size = j["size"];
    if (j.contains("halftoneSize")) m_Size = j["halftoneSize"];
    if (j.contains("intensity")) m_Intensity = j["intensity"];
    if (j.contains("halftoneIntensity")) m_Intensity = j["halftoneIntensity"];
    if (j.contains("sharpness")) m_Sharpness = j["sharpness"];
    if (j.contains("halftoneSharpness")) m_Sharpness = j["halftoneSharpness"];
    if (j.contains("pattern")) m_Pattern = j["pattern"];
    if (j.contains("halftonePattern")) m_Pattern = j["halftonePattern"];
    if (j.contains("colorMode")) m_ColorMode = j["colorMode"];
    if (j.contains("halftoneColorMode")) m_ColorMode = j["halftoneColorMode"];
    if (j.contains("gray")) m_Gray = j["gray"];
    if (j.contains("halftoneGray")) m_Gray = j["halftoneGray"];
    if (j.contains("invert")) m_Invert = j["invert"];
    if (j.contains("halftoneInvert")) m_Invert = j["invert"];
}
