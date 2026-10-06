#include "Editor/Internal/RawLab/RawGradingScopeRenderer.h"

#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_ARRAY_BUFFER_BINDING
#define GL_ARRAY_BUFFER_BINDING 0x8894
#endif

namespace Stack::Editor::RawLabInternal {
namespace {

constexpr const char* kVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
uniform vec4 plotRect;
uniform vec4 displayRect;
uniform float diameter;
uniform float alpha;
out vec4 traceColor;
void main() {
    vec2 screen = plotRect.xy + position * plotRect.zw;
    vec2 uv = (screen - displayRect.xy) / displayRect.zw;
    gl_Position = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    gl_PointSize = diameter;
    traceColor = vec4(color.rgb, color.a * alpha);
}
)GLSL";

constexpr const char* kFragmentShader = R"GLSL(
#version 330 core
in vec4 traceColor;
out vec4 fragColor;
void main() {
    float radius = length(gl_PointCoord * 2.0 - 1.0);
    float feather = fwidth(radius) * 0.5;
    float coverage = 1.0 - smoothstep(1.0 - feather, 1.0 + feather, radius);
    if (coverage <= 0.0) discard;
    fragColor = vec4(traceColor.rgb, traceColor.a * coverage);
}
)GLSL";

} // namespace

bool RawGradingScopeRenderer::Upload(
    const std::shared_ptr<const RawGradingScopeVisualization>& packet) {
    if (!packet) return false;
    if (m_Packet == packet) return true;
    // Any replacement invalidates the previous packet, including partial upload failure.
    m_Packet.reset();
    if (m_Program == 0) {
        m_Program = GLHelpers::CreateShaderProgram(kVertexShader, kFragmentShader);
        if (m_Program == 0) return false;
        m_PlotRectLocation = glGetUniformLocation(m_Program, "plotRect");
        m_DisplayRectLocation = glGetUniformLocation(m_Program, "displayRect");
        m_DiameterLocation = glGetUniformLocation(m_Program, "diameter");
        m_AlphaLocation = glGetUniformLocation(m_Program, "alpha");
    }

    GLint previousVao = 0, previousBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    auto upload = [](PointBuffer& target, const std::vector<RawGradingScopePoint>& points) {
        target.count = 0;
        while (glGetError() != GL_NO_ERROR) {}
        if (target.vertexArray == 0) glGenVertexArrays(1, &target.vertexArray);
        if (target.buffer == 0) glGenBuffers(1, &target.buffer);
        if (target.vertexArray == 0 || target.buffer == 0) return false;
        glBindVertexArray(target.vertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, target.buffer);
        glBufferData(GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(points.size() * sizeof(RawGradingScopePoint)),
            points.data(), GL_DYNAMIC_DRAW);
        if (glGetError() != GL_NO_ERROR) return false;
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(RawGradingScopePoint),
            reinterpret_cast<const void*>(offsetof(RawGradingScopePoint, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(RawGradingScopePoint),
            reinterpret_cast<const void*>(offsetof(RawGradingScopePoint, color)));
        target.count = static_cast<int>(points.size());
        return true;
    };
    const bool uploaded = upload(m_Vector, packet->vectorscopePoints) &&
        upload(m_Parade, packet->paradePoints);
    glBindVertexArray(static_cast<unsigned int>(previousVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned int>(previousBuffer));
    if (uploaded) m_Packet = packet;
    else m_Vector.count = m_Parade.count = 0;
    return uploaded;
}

bool RawGradingScopeRenderer::QueueDraw(ImDrawList& drawList,
    const std::shared_ptr<const RawGradingScopeVisualization>& packet,
    GradingScopePlot plot, ImVec2 min, ImVec2 max) {
    if (max.x <= min.x || max.y <= min.y || !Upload(packet)) return false;
    const ImGuiViewport* viewport = ImGui::GetWindowViewport();
    DrawRequest request;
    request.renderer = this;
    request.plot = plot;
    request.min = min;
    request.size = ImVec2(max.x - min.x, max.y - min.y);
    request.displayPos = viewport->Pos;
    request.displaySize = viewport->Size;
    request.framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    request.alpha = ImGui::GetStyle().Alpha;
    // Copy geometry into the draw list; the callback runs after UI layout.
    drawList.AddCallback(DrawCallback, &request, sizeof(request));
    drawList.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    return true;
}

void RawGradingScopeRenderer::DrawCallback(const ImDrawList*, const ImDrawCmd* command) {
    const auto& request = *static_cast<const DrawRequest*>(command->UserCallbackData);
    const auto& renderer = *request.renderer;
    const bool vector = request.plot == GradingScopePlot::Vectorscope;
    const auto& points = vector ? renderer.m_Vector : renderer.m_Parade;
    if (points.count == 0) return;
    const float sx = request.framebufferScale.x, sy = request.framebufferScale.y;
    const float width = request.displaySize.x * sx;
    const float height = request.displaySize.y * sy;
    const float x0 = std::clamp((command->ClipRect.x - request.displayPos.x) * sx, 0.0f, width);
    const float y0 = std::clamp((command->ClipRect.y - request.displayPos.y) * sy, 0.0f, height);
    const float x1 = std::clamp((command->ClipRect.z - request.displayPos.x) * sx, 0.0f, width);
    const float y1 = std::clamp((command->ClipRect.w - request.displayPos.y) * sy, 0.0f, height);
    if (x1 <= x0 || y1 <= y0) return;
    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<int>(x0), static_cast<int>(height - y1),
        static_cast<int>(std::ceil(x1 - x0)), static_cast<int>(std::ceil(y1 - y0)));
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const bool pointSizeWasEnabled = glIsEnabled(GL_PROGRAM_POINT_SIZE) != GL_FALSE;
    glEnable(GL_PROGRAM_POINT_SIZE);
    glUseProgram(renderer.m_Program);
    glUniform4f(renderer.m_PlotRectLocation, request.min.x, request.min.y,
        request.size.x, request.size.y);
    glUniform4f(renderer.m_DisplayRectLocation, request.displayPos.x, request.displayPos.y,
        request.displaySize.x, request.displaySize.y);
    const float binWidth = request.size.x / (vector
        ? RawGradingScopeVisualization::kVectorscopeResolution
        : 3 * RawGradingScopeVisualization::kParadeColumns);
    const float binHeight = request.size.y / (vector
        ? RawGradingScopeVisualization::kVectorscopeResolution
        : RawGradingScopeVisualization::kParadeRows);
    const float radius = std::max(1.0f, std::max(binWidth, binHeight) * (vector ? 0.78f : 0.72f));
    glUniform1f(renderer.m_DiameterLocation, radius * 2.0f * std::max(sx, sy));
    glUniform1f(renderer.m_AlphaLocation, request.alpha);
    glBindVertexArray(points.vertexArray);
    glDrawArrays(GL_POINTS, 0, points.count);
    if (!pointSizeWasEnabled) glDisable(GL_PROGRAM_POINT_SIZE);
}

void RawGradingScopeRenderer::Shutdown() {
    for (PointBuffer* buffer : { &m_Vector, &m_Parade }) {
        if (buffer->buffer != 0) glDeleteBuffers(1, &buffer->buffer);
        if (buffer->vertexArray != 0) glDeleteVertexArrays(1, &buffer->vertexArray);
        *buffer = {};
    }
    if (m_Program != 0) glDeleteProgram(m_Program);
    m_Program = 0;
    m_Packet.reset();
}

} // namespace Stack::Editor::RawLabInternal
