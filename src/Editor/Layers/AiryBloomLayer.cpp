#include "AiryBloomLayer.h"

#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_AiryBloomVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_AiryBloomFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uRes;
uniform float uIntensity;
uniform float uAperture;
uniform float uThreshold;
uniform float uThresholdFade;
uniform float uCutoff;

float besselJ1(float x) {
    float ax = abs(x);
    if (ax < 8.0) {
        float y = x * x;
        float ans1 = x * (72362614232.0 + y * (-7895059235.0 + y * (242396853.1
            + y * (-2972611.439 + y * (15704.48260 + y * (-30.16036606))))));
        float ans2 = 144725228442.0 + y * (2300535178.0 + y * (18583304.74
            + y * (99447.43394 + y * (376.9991397 + y))));
        return ans1 / ans2;
    }

    float z = 8.0 / ax;
    float y = z * z;
    float xx = ax - 2.356194491;
    float ans1 = 1.0 + y * (0.183105e-2 + y * (-0.3516396496e-4
        + y * (0.2457520174e-5 + y * (-0.240337019e-6))));
    float ans2 = 0.04687499995 + y * (-0.2002690873e-3
        + y * (0.8449199096e-5 + y * (-0.88228987e-6 + y * 0.105787412e-6)));
    float ans = sqrt(0.636619772 / ax) * (cos(xx) * ans1 - z * sin(xx) * ans2);
    return x > 0.0 ? ans : -ans;
}

float airyPSF(float r, float aperture) {
    if (r < 0.001) return 1.0;
    float x = r * aperture * 3.14159265;
    float response = 2.0 * besselJ1(x) / x;
    return response * response;
}

void main() {
    vec2 texel = 1.0 / max(uRes, vec2(1.0));
    vec4 original = texture(uInputTex, vUV);

    vec3 blurred = vec3(0.0);
    float totalWeight = 0.0;
    const float renderRadius = 15.0;

    for (float x = -renderRadius; x <= renderRadius; x += 1.0) {
        for (float y = -renderRadius; y <= renderRadius; y += 1.0) {
            float dist = length(vec2(x, y));
            if (dist > renderRadius) continue;

            float weight = airyPSF(dist / renderRadius, uAperture);
            weight = max(0.0, weight - uCutoff) / max(0.0001, 1.0 - uCutoff);

            blurred += texture(uInputTex, vUV + vec2(x, y) * texel).rgb * weight;
            totalWeight += weight;
        }
    }

    blurred /= max(totalWeight, 0.0001);

    float luminance = dot(original.rgb, vec3(0.2126, 0.7152, 0.0722));
    float contribution = smoothstep(
        max(0.0, uThreshold - uThresholdFade),
        min(1.0, uThreshold + uThresholdFade + 0.001),
        luminance);

    vec3 bloomOnly = max(vec3(0.0), blurred - original.rgb);
    vec3 result = original.rgb + (bloomOnly * uIntensity * contribution);

    FragColor = vec4(clamp(result, 0.0, 1.0), original.a);
}
)";

AiryBloomLayer::AiryBloomLayer() {}

AiryBloomLayer::~AiryBloomLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void AiryBloomLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_AiryBloomVert, s_AiryBloomFrag);
}

void AiryBloomLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uRes"), static_cast<float>(width), static_cast<float>(height));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uIntensity"), m_Intensity);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uAperture"), m_Aperture);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uThreshold"), m_Threshold);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uThresholdFade"), m_ThresholdFade);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uCutoff"), m_Cutoff);

    quad.Draw();
    glUseProgram(0);
}

void AiryBloomLayer::RenderUI() {
    ImGui::SliderFloat("Intensity", &m_Intensity, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Aperture", &m_Aperture, 1.0f, 50.0f, "%.1f");
    ImGui::SliderFloat("Threshold", &m_Threshold, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Threshold Fade", &m_ThresholdFade, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Cutoff", &m_Cutoff, 0.01f, 1.0f, "%.2f");
    ImGui::TextDisabled("Native ports the core airy bloom pass here; the web layer's optional mask gates still need the future native mask system.");
}

json AiryBloomLayer::Serialize() const {
    json j;
    j["type"] = "AiryBloom";
    j["intensity"] = m_Intensity;
    j["aperture"] = m_Aperture;
    j["threshold"] = m_Threshold;
    j["thresholdFade"] = m_ThresholdFade;
    j["cutoff"] = m_Cutoff;
    return j;
}

void AiryBloomLayer::Deserialize(const json& j) {
    if (j.contains("intensity")) m_Intensity = j["intensity"];
    if (j.contains("airyBloomIntensity")) m_Intensity = j["airyBloomIntensity"];
    if (j.contains("aperture")) m_Aperture = j["aperture"];
    if (j.contains("airyBloomAperture")) m_Aperture = j["airyBloomAperture"];
    if (j.contains("threshold")) m_Threshold = j["threshold"];
    if (j.contains("airyBloomThreshold")) m_Threshold = j["airyBloomThreshold"];
    if (j.contains("thresholdFade")) m_ThresholdFade = j["thresholdFade"];
    if (j.contains("airyBloomThresholdFade")) m_ThresholdFade = j["airyBloomThresholdFade"];
    if (j.contains("cutoff")) m_Cutoff = j["cutoff"];
    if (j.contains("airyBloomCutoff")) m_Cutoff = j["airyBloomCutoff"];
}
