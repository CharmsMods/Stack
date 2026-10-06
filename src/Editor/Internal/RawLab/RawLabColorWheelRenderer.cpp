#include "Editor/Internal/RawLab/RawLabColorWheelRenderer.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
namespace {

constexpr const char* kVertexShader = R"GLSL(
#version 330 core
uniform vec4 rect;
uniform vec4 displayRect;
out vec2 fieldUv;
void main() {
    fieldUv = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1);
    vec2 screen = rect.xy + fieldUv * rect.zw;
    vec2 uv = (screen - displayRect.xy) / displayRect.zw;
    gl_Position = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
}
)GLSL";

constexpr const char* kFragmentShader = R"GLSL(
#version 330 core
in vec2 fieldUv;
uniform vec3 parameters; // lightness, extent, opacity
uniform bool rec2020;
out vec4 fragColor;

vec3 workingRgb(float lightness, vec2 ab) {
    float lRoot = lightness + 0.3963377774 * ab.x + 0.2158037573 * ab.y;
    float mRoot = lightness - 0.1055613458 * ab.x - 0.0638541728 * ab.y;
    float sRoot = lightness - 0.0894841775 * ab.x - 1.2914855480 * ab.y;
    float l = lRoot * lRoot * lRoot;
    float m = mRoot * mRoot * mRoot;
    float s = sRoot * sRoot * sRoot;
    float x = 1.2268798734 * l - 0.5578149966 * m + 0.2813910502 * s;
    float y = -0.0405757626 * l + 1.1122868294 * m - 0.0717110667 * s;
    float z = -0.0763729497 * l - 0.4214933240 * m + 1.5869240244 * s;
    if (rec2020) return vec3(
        1.7166512 * x - 0.3556708 * y - 0.2533663 * z,
        -0.6666844 * x + 1.6164812 * y + 0.0157685 * z,
        0.0176399 * x - 0.0427706 * y + 0.9421031 * z);
    return vec3(3.2404542 * x - 1.5371385 * y - 0.4985314 * z,
        -0.9692660 * x + 1.8760108 * y + 0.0415560 * z,
        0.0556434 * x - 0.2040259 * y + 1.0572252 * z);
}
void main() {
    float extent = parameters.y;
    vec2 ab = vec2(fieldUv.x * 2.0 - 1.0, 1.0 - fieldUv.y * 2.0) * extent;
    float radius = length(ab);
    if (radius > extent) discard;
    if (radius <= 0.000001) { fragColor = vec4(vec3(1.0), parameters.z); return; }
    // Inverse of the point/pin display projection. HDR values remain unbounded.
    vec2 sceneAb = ab * (0.12 / max(0.000001, extent - radius));
    vec3 rgb = workingRgb(parameters.x, sceneAb);
    if (rec2020) rgb = vec3(
        1.6604910*rgb.r-0.5876411*rgb.g-0.0728499*rgb.b,
        -0.1245505*rgb.r+1.1328999*rgb.g-0.0083494*rgb.b,
        -0.0181508*rgb.r-0.1005789*rgb.g+1.1187297*rgb.b);
    float minimum = min(0.0, min(rgb.r, min(rgb.g, rgb.b)));
    rgb = max(vec3(0.0), rgb - minimum);
    float peak = max(0.000001, max(rgb.r, max(rgb.g, rgb.b)));
    rgb = pow(clamp(rgb / peak, 0.0, 1.0), vec3(1.0 / 2.2));

    float fade = clamp((extent - radius) / (extent * 0.075), 0.0, 1.0);
    float opacity = fade * fade * (3.0 - 2.0 * fade) * parameters.z;
    fragColor = vec4(rgb, opacity);
}
)GLSL";

} // namespace

bool RawLabColorWheelRenderer::Initialize() {
    if (m_InitializationAttempted) return m_Program != 0 && m_VertexArray != 0;
    m_InitializationAttempted = true;
    m_Program = GLHelpers::CreateShaderProgram(kVertexShader, kFragmentShader);
    if (m_Program == 0) return false;
    glGenVertexArrays(1, &m_VertexArray);
    m_RectLocation = glGetUniformLocation(m_Program, "rect");
    m_DisplayLocation = glGetUniformLocation(m_Program, "displayRect");
    m_ParametersLocation = glGetUniformLocation(m_Program, "parameters");
    m_WorkingSpaceLocation = glGetUniformLocation(m_Program, "rec2020");
    return m_VertexArray != 0;
}

bool RawLabColorWheelRenderer::QueueDraw(ImDrawList& drawList, ImVec2 min, ImVec2 max,
    Raw::RawWorkingSpace workingSpace, float lightness, float extent) {
    if (max.x <= min.x || max.y <= min.y || !std::isfinite(lightness) ||
        !std::isfinite(extent) || extent <= 0 || !Initialize()) return false;
    const auto* viewport = ImGui::GetWindowViewport();
    DrawRequest request;
    request.renderer = this;
    request.min = min;
    request.size = ImVec2(max.x - min.x, max.y - min.y);
    request.displayPos = viewport->Pos;
    request.displaySize = viewport->Size;
    request.framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    request.workingSpace = workingSpace;
    request.lightness = lightness;
    request.extent = extent;
    request.alpha = ImGui::GetStyle().Alpha;
    drawList.AddCallback(DrawCallback, &request, sizeof(request));
    drawList.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    return true;
}

void RawLabColorWheelRenderer::DrawCallback(const ImDrawList*, const ImDrawCmd* command) {
    const auto& request = *static_cast<const DrawRequest*>(command->UserCallbackData);
    const auto& renderer = *request.renderer;
    if (renderer.m_Program == 0 || renderer.m_VertexArray == 0) return;
    const float sx = request.framebufferScale.x, sy = request.framebufferScale.y;
    const float width = request.displaySize.x * sx, height = request.displaySize.y * sy;
    const float x0 = std::clamp((command->ClipRect.x - request.displayPos.x) * sx, 0.0f, width);
    const float y0 = std::clamp((command->ClipRect.y - request.displayPos.y) * sy, 0.0f, height);
    const float x1 = std::clamp((command->ClipRect.z - request.displayPos.x) * sx, 0.0f, width);
    const float y1 = std::clamp((command->ClipRect.w - request.displayPos.y) * sy, 0.0f, height);
    if (x1 <= x0 || y1 <= y0) return;
    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<int>(x0), static_cast<int>(height - y1),
        static_cast<int>(std::ceil(x1 - x0)), static_cast<int>(std::ceil(y1 - y0)));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(renderer.m_Program);
    glUniform4f(renderer.m_RectLocation, request.min.x, request.min.y, request.size.x, request.size.y);
    glUniform4f(renderer.m_DisplayLocation, request.displayPos.x, request.displayPos.y,
        request.displaySize.x, request.displaySize.y);
    glUniform3f(renderer.m_ParametersLocation, request.lightness, request.extent, request.alpha);
    glUniform1i(renderer.m_WorkingSpaceLocation, request.workingSpace == Raw::RawWorkingSpace::LinearRec2020D65);
    glBindVertexArray(renderer.m_VertexArray);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void RawLabColorWheelRenderer::Shutdown() {
    if (m_Program != 0) glDeleteProgram(m_Program);
    if (m_VertexArray != 0) glDeleteVertexArrays(1, &m_VertexArray);
    m_Program = m_VertexArray = 0;
    m_InitializationAttempted = false;
}

} // namespace Stack::Editor::RawLabInternal
