#include "SplitAdjustmentsLayers.h"

#include "Renderer/FullscreenQuad.h"
#include "Utils/ImGuiExtras.h"

namespace {

const char* kAdjustVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kAdjustFrag = R"(
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

    color.rgb += uBrightness;
    color.rgb = ((color.rgb - 0.5) * (1.0 + uContrast)) + 0.5;

    float luminance = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
    color.rgb = mix(vec3(luminance), color.rgb, 1.0 + uSaturation);

    color.r += uWarmth * 0.1;
    color.b -= uWarmth * 0.1;

    if (uSharpening > 0.001) {
        vec3 blurred = vec3(0.0);
        blurred += texture(uInputTex, vUV + vec2(-uTexelSize.x, 0.0)).rgb;
        blurred += texture(uInputTex, vUV + vec2( uTexelSize.x, 0.0)).rgb;
        blurred += texture(uInputTex, vUV + vec2(0.0, -uTexelSize.y)).rgb;
        blurred += texture(uInputTex, vUV + vec2(0.0,  uTexelSize.y)).rgb;
        blurred *= 0.25;

        vec3 sharpened = color.rgb + (color.rgb - blurred) * uSharpening * 4.0;
        float diff = length(color.rgb - blurred);
        float mask = smoothstep(uSharpenThreshold * 0.1, uSharpenThreshold * 0.1 + 0.02, diff);
        color.rgb = mix(color.rgb, sharpened, mask);
    }

    color.rgb = clamp(color.rgb, 0.0, 1.0);
    FragColor = color;
}
)";

} // namespace

SplitAdjustmentsLayerBase::~SplitAdjustmentsLayerBase() {
    if (m_ShaderProgram) {
        glDeleteProgram(m_ShaderProgram);
    }
}

void SplitAdjustmentsLayerBase::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(kAdjustVert, kAdjustFrag);
}

void SplitAdjustmentsLayerBase::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
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

void SplitAdjustmentsLayerBase::Deserialize(const json& j) {
    if (j.contains("brightness")) m_Brightness = j["brightness"];
    if (j.contains("contrast")) m_Contrast = j["contrast"];
    if (j.contains("saturation")) m_Saturation = j["saturation"];
    if (j.contains("warmth")) m_Warmth = j["warmth"];
    if (j.contains("sharpening")) m_Sharpening = j["sharpening"];
    if (j.contains("sharpenThreshold")) m_SharpenThreshold = j["sharpenThreshold"];
}

void BrightnessLayer::RenderUI() {
    ImGuiExtras::NodeSliderFloat("Brightness", "##Brightness", &m_Brightness, -1.0f, 1.0f, "%.2f");
}

json BrightnessLayer::Serialize() const {
    return json{ {"type", "Brightness"}, {"brightness", m_Brightness} };
}

void ContrastLayer::RenderUI() {
    ImGuiExtras::NodeSliderFloat("Contrast", "##Contrast", &m_Contrast, -1.0f, 1.0f, "%.2f");
}

json ContrastLayer::Serialize() const {
    return json{ {"type", "Contrast"}, {"contrast", m_Contrast} };
}

void SaturationLayer::RenderUI() {
    ImGuiExtras::NodeSliderFloat("Saturation", "##Saturation", &m_Saturation, -1.0f, 1.0f, "%.2f");
}

json SaturationLayer::Serialize() const {
    return json{ {"type", "Saturation"}, {"saturation", m_Saturation} };
}

void WarmthLayer::RenderUI() {
    ImGuiExtras::NodeSliderFloat("Warmth", "##Warmth", &m_Warmth, -1.0f, 1.0f, "%.2f");
}

json WarmthLayer::Serialize() const {
    return json{ {"type", "Warmth"}, {"warmth", m_Warmth} };
}

void SharpenLayer::RenderUI() {
    ImGuiExtras::NodeSliderFloat("Sharpening", "##Sharpening", &m_Sharpening, 0.0f, 1.0f, "%.2f");
    ImGuiExtras::NodeSliderFloat("Sharpen Threshold", "##SharpenThreshold", &m_SharpenThreshold, 0.0f, 1.0f, "%.2f");
}

json SharpenLayer::Serialize() const {
    return json{ {"type", "Sharpen"}, {"sharpening", m_Sharpening}, {"sharpenThreshold", m_SharpenThreshold} };
}
