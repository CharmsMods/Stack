#include "Utils/DisplayRefreshRate.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>

int DisplayRefreshRate::ForWindow(GLFWwindow* window) {
    if (!window) return 0;
    static GLFWwindow* previousWindow = nullptr;
    static std::chrono::steady_clock::time_point checkedAt {};
    static int cachedRate = 0;
    const auto now = std::chrono::steady_clock::now();
    if (window == previousWindow && now - checkedAt < std::chrono::seconds(1))
        return cachedRate;
    previousWindow = window;
    checkedAt = now;
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
