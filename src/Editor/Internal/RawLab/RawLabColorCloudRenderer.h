#pragma once

#include <cstddef>
#include <array>

#include <imgui.h>

namespace Stack::Editor::RawLabInternal {

struct RawLabColorCloudGpuVertex {
    float a = 0.0f;
    float b = 0.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float sceneEv = 0.0f;
    float selectedCatch = 0.0f;
};

struct ColorWarpCloudRenderState {
    unsigned int colorWarpCloudProgram = 0;
    unsigned int colorWarpCloudVertexArray = 0;
    unsigned int colorWarpCloudVertexBuffer = 0;
    std::size_t colorWarpCloudGpuFingerprint = 0;
    int colorWarpCloudGpuPointCount = 0;
    float colorWarpCloudCanvasMinX = 0.0f;
    float colorWarpCloudCanvasMinY = 0.0f;
    float colorWarpCloudCanvasWidth = 0.0f;
    float colorWarpCloudCanvasHeight = 0.0f;
    float colorWarpCloudDisplayPosX = 0.0f;
    float colorWarpCloudDisplayPosY = 0.0f;
    float colorWarpCloudDisplayWidth = 0.0f;
    float colorWarpCloudDisplayHeight = 0.0f;
    float colorWarpCloudFramebufferScaleX = 1.0f;
    float colorWarpCloudFramebufferScaleY = 1.0f;
    float colorWarpCloudViewCenterA = 0.0f;
    float colorWarpCloudViewCenterB = 0.0f;
    float colorWarpCloudViewHalfWidth = 1.0f;
    float colorWarpCloudViewHalfHeight = 1.0f;
    float colorWarpCloudPointScale = 1.0f;
    int colorWarpCloudWorkingRange = 0;
    bool colorWarpCloudShowSelectedCatch = false;
    bool initializationAttempted = false;
    std::array<int,11> locations{};
};
void ShutdownColorWarpCloud(ColorWarpCloudRenderState& state);

bool EnsureRawLabColorCloudRenderer(
    ColorWarpCloudRenderState& state);

void DrawRawLabColorCloudCallback(
    const ImDrawList* drawList,
    const ImDrawCmd* command);

} // namespace Stack::Editor::RawLabInternal
