#pragma once

#include "imgui.h"
#include "imgui_internal.h"

#include <string>
#include <vector>

struct GLFWwindow;

namespace AppWindowTitleBarBridge {

struct Metrics {
    bool requested = false;
    bool compiled = false;
    bool initialized = false;
    bool customizationSupported = false;
    bool active = false;
    bool passthroughSupported = false;
    int heightPx = 0;
    int leftInsetPx = 0;
    int rightInsetPx = 0;
    std::string fallbackReason;
};

bool RuntimeFlagEnabled();
void Initialize(GLFWwindow* window);
void Shutdown();
void UpdateTheme(
    GLFWwindow* window,
    const ImVec4& foreground,
    const ImVec4& hoverBackground,
    const ImVec4& pressedBackground,
    float captionButtonOpacity = 1.0f);
void SyncPassthroughRegions(GLFWwindow* window, const std::vector<ImRect>& screenRects);
void ClearPassthroughRegions();
// Override caption/system-menu hits inside declared client regions. Caption
// buttons and resize edges retain their native behavior.
bool IsCaptionPassthroughPoint(int screenX, int screenY);
const Metrics& GetMetrics();
bool IsActive();

} // namespace AppWindowTitleBarBridge
