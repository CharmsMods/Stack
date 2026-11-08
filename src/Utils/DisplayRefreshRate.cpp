#include "Utils/DisplayRefreshRate.h"
#include "Platform/GlfwMonitorRefresh.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#include <GLFW/glfw3native.h>
#endif
#include <algorithm>
#include <chrono>

int DisplayRefreshRate::ForWindow(GLFWwindow* window) {
    if (!window) return 0;
    static GLFWwindow* previousWindow = nullptr;
    static std::chrono::steady_clock::time_point checkedAt {};
    static int cachedRate = 0;
    const auto now = std::chrono::steady_clock::now();
#if defined(_WIN32)
    static HMONITOR previousMonitor = nullptr;
    static std::uint64_t previousRevision = 0;
    const HMONITOR nativeMonitor = MonitorFromWindow(
        glfwGetWin32Window(window), MONITOR_DEFAULTTONEAREST);
    const auto revision=Stack::Platform::glfwMonitorRevision.load(std::memory_order_relaxed);
    // The backend already invalidates on display/settings/DPI and topology
    // changes. Mode enumeration can block in the driver; do not repeat it on a
    // timer for an unchanged display. Moving between monitors refreshes now.
    // Retry an unavailable initial mode, rather than caching failure forever.
    if (window == previousWindow && nativeMonitor == previousMonitor && revision == previousRevision &&
        (cachedRate > 0 || now - checkedAt < std::chrono::seconds(1))) return cachedRate;
    previousMonitor = nativeMonitor;
    previousRevision = revision;
#else
    if (window == previousWindow && now - checkedAt < std::chrono::seconds(1))
        return cachedRate;
#endif
    previousWindow = window;
    checkedAt = now;
#if defined(_WIN32)
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    DEVMODEW displayMode{};
    displayMode.dmSize = sizeof(displayMode);
    if (nativeMonitor && GetMonitorInfoW(nativeMonitor, reinterpret_cast<MONITORINFO*>(&monitorInfo)) &&
        EnumDisplaySettingsW(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &displayMode) &&
        displayMode.dmDisplayFrequency > 1) {
        cachedRate = static_cast<int>(displayMode.dmDisplayFrequency);
        return cachedRate;
    }
#endif
    GLFWmonitor* selected = glfwGetWindowMonitor(window);
    if (!selected) {
        int x = 0, y = 0, width = 0, height = 0, count = 0;
        glfwGetWindowPos(window, &x, &y);
        glfwGetWindowSize(window, &width, &height);
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        long long largestArea = -1;
        for (int i = 0; i < count; ++i) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            if (!mode) continue;
            int mx = 0, my = 0;
            glfwGetMonitorPos(monitors[i], &mx, &my);
            const long long area = static_cast<long long>(std::max(0,
                std::min(x + width, mx + mode->width) - std::max(x, mx))) *
                std::max(0, std::min(y + height, my + mode->height) - std::max(y, my));
            if (area > largestArea) {
                largestArea = area;
                selected = monitors[i];
            }
        }
    }
    if (!selected) selected = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = selected ? glfwGetVideoMode(selected) : nullptr;
    cachedRate = mode && mode->refreshRate > 0 ? mode->refreshRate : 0;
    return cachedRate;
}
