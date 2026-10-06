#pragma once
struct GLFWwindow;
namespace DisplayRefreshRate {
// UI-thread query. Returns zero when the current display mode is unavailable.
int ForWindow(GLFWwindow* window);
}
