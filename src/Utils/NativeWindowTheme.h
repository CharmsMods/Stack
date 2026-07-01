#pragma once

#include <imgui.h>

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
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

namespace NativeWindowTheme {

struct CaptionThemeResult {
    HWND hwnd = nullptr;
    DWORD osBuild = 0;
    HRESULT darkMode = E_POINTER;
    HRESULT captionColor = E_POINTER;
    HRESULT textColor = E_POINTER;
    HRESULT borderColor = E_POINTER;
    bool frameStyleChanged = false;
    bool topMostCleared = false;
    LONG_PTR style = 0;
    LONG_PTR exStyle = 0;
};

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

inline DWORD GetWindowsBuildNumber() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return 0;
    }

    auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (!rtlGetVersion) {
        return 0;
    }

    OSVERSIONINFOW version {};
    version.dwOSVersionInfoSize = sizeof(version);
    if (rtlGetVersion(&version) != 0) {
        return 0;
    }
    return version.dwBuildNumber;
}

inline bool EnsureNotTopMost(HWND hwnd) {
    if (!hwnd) {
        return false;
    }

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOPMOST) == 0) {
        return false;
    }

    SetWindowPos(
        hwnd,
        HWND_NOTOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return true;
}

inline bool EnsureNotTopMost(GLFWwindow* window) {
    return EnsureNotTopMost(window ? glfwGetWin32Window(window) : nullptr);
}

inline CaptionThemeResult ApplyCaptionTheme(
    HWND hwnd,
    const ImVec4& captionColorValue,
    const ImVec4& textColorValue,
    const ImVec4& borderColorValue,
    bool hideVisibleBorder) {
    CaptionThemeResult result;
    result.hwnd = hwnd;
    result.osBuild = GetWindowsBuildNumber();
    if (!hwnd) {
        return result;
    }

    result.topMostCleared = EnsureNotTopMost(hwnd);

    ImVec4 caption = captionColorValue;
    ImVec4 text = textColorValue;
    ImVec4 border = borderColorValue;
    caption.w = 1.0f;
    text.w = 1.0f;
    border.w = 1.0f;
    const BOOL darkMode = IsDarkTitleColor(caption) ? TRUE : FALSE;
    const COLORREF captionColor = ToColorRef(caption);
    const COLORREF textColor = ToColorRef(text);
    const COLORREF borderColor = hideVisibleBorder ? static_cast<COLORREF>(DWMWA_COLOR_NONE) : ToColorRef(border);

    result.darkMode = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    result.captionColor = DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
    result.textColor = DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
    result.borderColor = DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
    if (hideVisibleBorder) {
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        const LONG_PTR updatedExStyle = exStyle & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME);
        if (updatedExStyle != exStyle) {
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, updatedExStyle);
            result.frameStyleChanged = true;
            SetWindowPos(
                hwnd,
                nullptr,
                0,
                0,
                0,
                0,
                SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    result.style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    result.exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    return result;
}

inline CaptionThemeResult Apply(GLFWwindow* window, const ImVec4& surfaceColor, bool hideVisibleBorder = true) {
    if (!window) {
        return {};
    }

    HWND hwnd = glfwGetWin32Window(window);
    const ImGuiStyle& style = ImGui::GetStyle();
    return ApplyCaptionTheme(hwnd, surfaceColor, style.Colors[ImGuiCol_Text], surfaceColor, hideVisibleBorder);
}

inline CaptionThemeResult Apply(GLFWwindow* window) {
    return Apply(window, ImGui::GetStyle().Colors[ImGuiCol_WindowBg], true);
}

inline CaptionThemeResult ApplyMainWindow(
    GLFWwindow* window,
    const ImVec4& captionColor,
    const ImVec4& textColor,
    const ImVec4& borderColor) {
    return ApplyCaptionTheme(window ? glfwGetWin32Window(window) : nullptr, captionColor, textColor, borderColor, false);
}

inline HWND GetNativeHandle(GLFWwindow* window) {
    return window ? glfwGetWin32Window(window) : nullptr;
}

inline HWND GetOwner(GLFWwindow* window) {
    HWND hwnd = GetNativeHandle(window);
    return hwnd ? GetWindow(hwnd, GW_OWNER) : nullptr;
}

inline bool SetOwner(GLFWwindow* window, GLFWwindow* ownerWindow) {
    HWND hwnd = GetNativeHandle(window);
    HWND owner = GetNativeHandle(ownerWindow);
    if (!hwnd || !owner || hwnd == owner) {
        return false;
    }

    if (GetWindow(hwnd, GW_OWNER) == owner) {
        return true;
    }

    SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    return GetWindow(hwnd, GW_OWNER) == owner;
}

inline bool IsForeground(GLFWwindow* window) {
    HWND hwnd = GetNativeHandle(window);
    return hwnd != nullptr && GetForegroundWindow() == hwnd;
}

inline bool IsTopMost(GLFWwindow* window) {
    HWND hwnd = GetNativeHandle(window);
    if (!hwnd) {
        return false;
    }
    return (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

inline bool IsFocusedOrForeground(GLFWwindow* window) {
    return window != nullptr &&
        (glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE || IsForeground(window));
}

inline bool HoldOpeningTopMost(GLFWwindow* window) {
    if (!window) {
        return false;
    }

    HWND hwnd = GetNativeHandle(window);
    if (!hwnd) {
        glfwShowWindow(window);
        glfwFocusWindow(window);
        return glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
    }

    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    } else if (!IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_SHOWNORMAL);
    }

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(hwnd);
    const BOOL foregroundSet = SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    glfwFocusWindow(window);
    return foregroundSet == TRUE || IsFocusedOrForeground(window);
}

inline void ReleaseOpeningTopMost(GLFWwindow* window) {
    HWND hwnd = GetNativeHandle(window);
    if (!hwnd || !IsTopMost(window)) {
        return;
    }

    SetWindowPos(
        hwnd,
        HWND_NOTOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

inline bool ShowAndFocus(GLFWwindow* window, bool pulseTopMost = false) {
    if (!window) {
        return false;
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) {
        glfwShowWindow(window);
        glfwFocusWindow(window);
        return glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
    }

    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    } else if (!IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_SHOWNORMAL);
    }

    if (pulseTopMost) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    } else {
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    }
    BringWindowToTop(hwnd);
    const BOOL foregroundSet = SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    glfwFocusWindow(window);
    return foregroundSet == TRUE || glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
}

} // namespace NativeWindowTheme
#else
struct GLFWwindow;
namespace NativeWindowTheme {
struct CaptionThemeResult {};
inline CaptionThemeResult Apply(GLFWwindow*) { return {}; }
inline CaptionThemeResult Apply(GLFWwindow*, const ImVec4&, bool = true) { return {}; }
inline CaptionThemeResult ApplyMainWindow(GLFWwindow*, const ImVec4&, const ImVec4&, const ImVec4&) { return {}; }
inline bool EnsureNotTopMost(GLFWwindow*) { return false; }
inline bool SetOwner(GLFWwindow*, GLFWwindow*) { return false; }
inline bool IsForeground(GLFWwindow*) { return false; }
inline bool IsTopMost(GLFWwindow*) { return false; }
inline bool IsFocusedOrForeground(GLFWwindow*) { return false; }
inline bool HoldOpeningTopMost(GLFWwindow*) { return false; }
inline void ReleaseOpeningTopMost(GLFWwindow*) {}
inline bool ShowAndFocus(GLFWwindow*, bool = false) { return false; }
} // namespace NativeWindowTheme
#endif
