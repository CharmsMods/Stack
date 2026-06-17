#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifdef APIENTRY
#undef APIENTRY
#endif
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <imgui.h>
#include <cmath>
#include <algorithm>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

namespace NativeWindowTheme {

inline BYTE ColorByte(float value) {
    return static_cast<BYTE>(std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

inline COLORREF ToColorRef(const ImVec4& color) {
    return RGB(ColorByte(color.x), ColorByte(color.y), ColorByte(color.z));
}

inline bool IsDarkTitleColor(const ImVec4& color) {
    const float luma = (0.2126f * color.x) + (0.7152f * color.y) + (0.0722f * color.z);
    return luma < 0.50f;
}

inline void Apply(GLFWwindow* window) {
    if (!window) {
        return;
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) {
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec4 caption = style.Colors[ImGuiCol_WindowBg];
    const ImVec4 text = style.Colors[ImGuiCol_Text];
    const ImVec4 border = style.Colors[ImGuiCol_WindowBg];
    const BOOL darkMode = IsDarkTitleColor(caption) ? TRUE : FALSE;
    const COLORREF captionColor = ToColorRef(caption);
    const COLORREF textColor = ToColorRef(text);
    const COLORREF borderColor = ToColorRef(border);

    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
}

} // namespace NativeWindowTheme
#else
struct GLFWwindow;
namespace NativeWindowTheme {
inline void Apply(GLFWwindow*) {}
} // namespace NativeWindowTheme
#endif
