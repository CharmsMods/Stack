#include "AdjustmentsLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

// ─────────────────────────────────────────────────────────────────────────────
// Inline GLSL sources (equivalent of adjust.frag + vs-quad.vert)
// ─────────────────────────────────────────────────────────────────────────────

static const char* s_AdjustVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_AdjustFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uTexelSize;
uniform float uBrightness;
uniform float uContrast;
uniform float uSaturation;
uniform float uWarmth;
uniform float uSharpening;
uniform float uSharpenThreshold;

void main() {
    vec4 color = texture(uInputTex, vUV);

    // --- Brightness ---
    color.rgb += uBrightness;

    // --- Contrast ---
    color.rgb = ((color.rgb - 0.5) * (1.0 + uContrast)) + 0.5;

    // --- Saturation ---
    float luminance = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
    color.rgb = mix(vec3(luminance), color.rgb, 1.0 + uSaturation);

    // --- Warmth (simple tint shift) ---
    color.r += uWarmth * 0.1;
    color.b -= uWarmth * 0.1;

    // --- Unsharp Mask Sharpening ---
    if (uSharpening > 0.001) {
        vec3 blurred = vec3(0.0);
        blurred += texture(uInputTex, vUV + vec2(-uTexelSize.x, 0.0)).rgb;
        blurred += texture(uInputTex, vUV + vec2( uTexelSize.x, 0.0)).rgb;
        blurred += texture(uInputTex, vUV + vec2(0.0, -uTexelSize.y)).rgb;
        blurred += texture(uInputTex, vUV + vec2(0.0,  uTexelSize.y)).rgb;
        blurred *= 0.25;

        vec3 sharpened = color.rgb + (color.rgb - blurred) * uSharpening * 4.0;

        // Apply threshold to prevent sharpening flat areas
        float diff = length(color.rgb - blurred);
        float mask = smoothstep(uSharpenThreshold * 0.1, uSharpenThreshold * 0.1 + 0.02, diff);
        color.rgb = mix(color.rgb, sharpened, mask);
    }

    color.rgb = clamp(color.rgb, 0.0, 1.0);
    FragColor = color;
}
)";

// ─────────────────────────────────────────────────────────────────────────────
// Implementation
// ─────────────────────────────────────────────────────────────────────────────

AdjustmentsLayer::AdjustmentsLayer() {}

AdjustmentsLayer::~AdjustmentsLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void AdjustmentsLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_AdjustVert, s_AdjustFrag);
}

void AdjustmentsLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);

    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uTexelSize"), 1.0f / width, 1.0f / height);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBrightness"), m_Brightness);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uContrast"), m_Contrast);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSaturation"), m_Saturation);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uWarmth"), m_Warmth);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSharpening"), m_Sharpening);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSharpenThreshold"), m_SharpenThreshold);

    quad.Draw();
    glUseProgram(0);
}

void AdjustmentsLayer::RenderUI() {
    ImGui::SliderFloat("Brightness", &m_Brightness, -1.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Contrast", &m_Contrast, -1.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Saturation", &m_Saturation, -1.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Warmth", &m_Warmth, -1.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Sharpening", &m_Sharpening, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Sharpen Threshold", &m_SharpenThreshold, 0.0f, 1.0f, "%.2f");
}

json AdjustmentsLayer::Serialize() const {
    json j;
    j["type"] = "Adjustments";
    j["brightness"] = m_Brightness;
    j["contrast"] = m_Contrast;
    j["saturation"] = m_Saturation;
    j["warmth"] = m_Warmth;
    j["sharpening"] = m_Sharpening;
    j["sharpenThreshold"] = m_SharpenThreshold;
    return j;
}

void AdjustmentsLayer::Deserialize(const json& j) {
    if (j.contains("brightness")) m_Brightness = j["brightness"];
    if (j.contains("contrast")) m_Contrast = j["contrast"];
    if (j.contains("saturation")) m_Saturation = j["saturation"];
    if (j.contains("warmth")) m_Warmth = j["warmth"];
    if (j.contains("sharpening")) m_Sharpening = j["sharpening"];
    if (j.contains("sharpenThreshold")) m_SharpenThreshold = j["sharpenThreshold"];
}
