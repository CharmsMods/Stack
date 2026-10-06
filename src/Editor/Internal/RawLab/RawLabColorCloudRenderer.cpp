#include "Editor/Internal/RawLab/RawLabColorCloudRenderer.h"

#include "Renderer/GLHelpers.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

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

bool EnsureRawLabColorCloudRenderer(
    EditorModuleTypes::RawWorkspaceLabUiState& state) {
    if (state.colorWarpCloudProgram != 0 &&
        state.colorWarpCloudVertexArray != 0 &&
        state.colorWarpCloudVertexBuffer != 0) {
        return true;
    }

    static constexpr const char* vertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec2 inAb;
layout(location = 1) in vec3 inSceneRgb;
layout(location = 2) in float inSceneEv;
layout(location = 3) in float inSelectedCatch;

uniform vec2 uViewCenter;
uniform vec2 uViewHalfExtent;
uniform vec2 uCanvasMin;
uniform vec2 uCanvasSize;
uniform vec2 uDisplayPos;
uniform vec2 uDisplaySize;
uniform float uPointScale;
uniform vec2 uFramebufferScale;
uniform int uWorkingRange;
uniform int uShowSelectedCatch;

out vec3 pointColor;
out float pointAlpha;
out vec2 discPosition;
out vec2 discPointExtent;

bool inWorkingRange(float sceneEv) {
    if (uWorkingRange == 1) return sceneEv >= 1.0;
    if (uWorkingRange == 2) return sceneEv >= -2.0 && sceneEv <= 2.0;
    if (uWorkingRange == 3) return sceneEv <= -1.0;
    return true;
}

void main() {
    vec2 clip = (inAb - uViewCenter) / max(uViewHalfExtent, vec2(1e-6));
    bool visible = inWorkingRange(inSceneEv);
    vec2 screen = uCanvasMin + vec2(
        clip.x * 0.5 + 0.5,
        0.5 - clip.y * 0.5) * uCanvasSize;
    vec2 displayUv = (screen - uDisplayPos) / max(uDisplaySize, vec2(1.0));
    vec2 displayClip = vec2(
        displayUv.x * 2.0 - 1.0,
        1.0 - displayUv.y * 2.0);
    gl_Position = visible
        ? vec4(displayClip, 0.0, 1.0)
        : vec4(2.0, 2.0, 0.0, 1.0);

    discPosition = inAb;
    float caught = step(0.5, inSelectedCatch);
    if (uShowSelectedCatch != 0) {
        gl_PointSize = mix(3.50, 5.88, caught) * uPointScale;
        pointAlpha = mix(0.022, 0.48, caught);
    } else {
        gl_PointSize = 5.32 * uPointScale;
        pointAlpha = 0.13;
    }

    discPointExtent = 2.0 * uViewHalfExtent / max(uCanvasSize, vec2(1.0)) *
        gl_PointSize / max(uFramebufferScale, vec2(0.01));
    vec3 positive = max(inSceneRgb, vec3(0.0));
    pointColor = pow(positive / (vec3(1.0) + positive), vec3(1.0 / 2.2));
}
)GLSL";
    static constexpr const char* fragmentShader = R"GLSL(
#version 330 core
in vec3 pointColor;
in float pointAlpha;
in vec2 discPosition;
in vec2 discPointExtent;
out vec4 fragColor;

void main() {
    vec2 offset = (gl_PointCoord - vec2(0.5)) * vec2(1.0, -1.0);
    // Point centers are bounded by the projection; clip their feather at the rim too.
    if (length(discPosition + offset * discPointExtent) > 0.45) discard;
    float distanceFromCenter = length(gl_PointCoord - vec2(0.5)) * 2.0;
    if (distanceFromCenter >= 1.0) discard;
    float core = 1.0 - smoothstep(0.34, 0.62, distanceFromCenter);
    float feather = 1.0 - smoothstep(0.15, 1.0, distanceFromCenter);
    float alpha = pointAlpha * max(core, feather * 0.16);
    fragColor = vec4(pointColor, alpha);
}
)GLSL";

    std::string shaderError;
    const unsigned int program = GLHelpers::CreateShaderProgram(
        vertexShader,
        fragmentShader,
        &shaderError);
    if (program == 0) {
        return false;
    }

    GLint previousVertexArray = 0;
    GLint previousArrayBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);

    unsigned int vertexArray = 0;
    unsigned int vertexBuffer = 0;
    glGenVertexArrays(1, &vertexArray);
    glGenBuffers(1, &vertexBuffer);
    if (vertexArray == 0 || vertexBuffer == 0) {
        if (vertexBuffer != 0) glDeleteBuffers(1, &vertexBuffer);
        if (vertexArray != 0) glDeleteVertexArrays(1, &vertexArray);
        glDeleteProgram(program);
        return false;
    }

    glBindVertexArray(vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    constexpr GLsizei stride = sizeof(RawLabColorCloudGpuVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(offsetof(RawLabColorCloudGpuVertex, a)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(offsetof(RawLabColorCloudGpuVertex, red)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2, 1, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(offsetof(RawLabColorCloudGpuVertex, sceneEv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(
        3, 1, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(offsetof(RawLabColorCloudGpuVertex, selectedCatch)));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned int>(previousArrayBuffer));
    glBindVertexArray(static_cast<unsigned int>(previousVertexArray));

    state.colorWarpCloudProgram = program;
    state.colorWarpCloudVertexArray = vertexArray;
    state.colorWarpCloudVertexBuffer = vertexBuffer;
    return true;
}

void DrawRawLabColorCloudCallback(
    const ImDrawList*,
    const ImDrawCmd* command) {
    auto* state = static_cast<EditorModuleTypes::RawWorkspaceLabUiState*>(
        command == nullptr ? nullptr : command->UserCallbackData);
    if (state == nullptr ||
        state->colorWarpCloudProgram == 0 ||
        state->colorWarpCloudVertexArray == 0 ||
        state->colorWarpCloudGpuPointCount <= 0) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    const float clipMinX =
        (command->ClipRect.x - state->colorWarpCloudDisplayPosX) *
        state->colorWarpCloudFramebufferScaleX;
    const float clipMinY =
        (command->ClipRect.y - state->colorWarpCloudDisplayPosY) *
        state->colorWarpCloudFramebufferScaleY;
    const float clipMaxX =
        (command->ClipRect.z - state->colorWarpCloudDisplayPosX) *
        state->colorWarpCloudFramebufferScaleX;
    const float clipMaxY =
        (command->ClipRect.w - state->colorWarpCloudDisplayPosY) *
        state->colorWarpCloudFramebufferScaleY;
    const int framebufferHeight = static_cast<int>(std::lround(
        state->colorWarpCloudDisplayHeight *
        state->colorWarpCloudFramebufferScaleY));
    glScissor(
        static_cast<int>(std::floor(clipMinX)),
        framebufferHeight - static_cast<int>(std::ceil(clipMaxY)),
        std::max(0, static_cast<int>(std::ceil(clipMaxX - clipMinX))),
        std::max(0, static_cast<int>(std::ceil(clipMaxY - clipMinY))));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glUseProgram(state->colorWarpCloudProgram);
    glUniform2f(
        glGetUniformLocation(state->colorWarpCloudProgram, "uViewCenter"),
        state->colorWarpCloudViewCenterA,
        state->colorWarpCloudViewCenterB);
    glUniform2f(
        glGetUniformLocation(state->colorWarpCloudProgram, "uViewHalfExtent"),
        state->colorWarpCloudViewHalfWidth,
        state->colorWarpCloudViewHalfHeight);
    glUniform2f(
        glGetUniformLocation(state->colorWarpCloudProgram, "uCanvasMin"),
        state->colorWarpCloudCanvasMinX,
        state->colorWarpCloudCanvasMinY);
    glUniform2f(
        glGetUniformLocation(state->colorWarpCloudProgram, "uCanvasSize"),
        state->colorWarpCloudCanvasWidth,
        state->colorWarpCloudCanvasHeight);
    glUniform2f(
        glGetUniformLocation(state->colorWarpCloudProgram, "uDisplayPos"),
        state->colorWarpCloudDisplayPosX,
        state->colorWarpCloudDisplayPosY);
    glUniform2f(
        glGetUniformLocation(state->colorWarpCloudProgram, "uDisplaySize"),
        state->colorWarpCloudDisplayWidth,
        state->colorWarpCloudDisplayHeight);
    glUniform2f(glGetUniformLocation(state->colorWarpCloudProgram, "uFramebufferScale"),
        state->colorWarpCloudFramebufferScaleX, state->colorWarpCloudFramebufferScaleY);
    glUniform1f(
        glGetUniformLocation(state->colorWarpCloudProgram, "uPointScale"),
        state->colorWarpCloudPointScale);
    glUniform1i(
        glGetUniformLocation(state->colorWarpCloudProgram, "uWorkingRange"),
        state->colorWarpCloudWorkingRange);
    glUniform1i(
        glGetUniformLocation(
            state->colorWarpCloudProgram,
            "uShowSelectedCatch"),
        state->colorWarpCloudShowSelectedCatch ? 1 : 0);
    glBindVertexArray(state->colorWarpCloudVertexArray);
    glDrawArrays(
        GL_POINTS,
        0,
        static_cast<GLsizei>(state->colorWarpCloudGpuPointCount));
}

} // namespace Stack::Editor::RawLabInternal
