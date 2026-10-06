#include "AppWindowTitleBarBridge.h"

#include "AppPaths.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

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
#include <commctrl.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

#ifndef STACK_ENABLE_APPWINDOW_TITLEBAR
#define STACK_ENABLE_APPWINDOW_TITLEBAR 0
#endif

#ifndef STACK_HAS_WINDOWS_APP_SDK
#define STACK_HAS_WINDOWS_APP_SDK 0
#endif

#if defined(_WIN32) && STACK_ENABLE_APPWINDOW_TITLEBAR && STACK_HAS_WINDOWS_APP_SDK
#include <winrt/base.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.UI.h>
#if __has_include(<winrt/Microsoft.ui.interop.h>)
#include <winrt/Microsoft.ui.interop.h>
#elif __has_include(<winrt/Microsoft.UI.Interop.h>)
#include <winrt/Microsoft.UI.Interop.h>
#endif
#if __has_include(<mddbootstrap.h>)
#include <mddbootstrap.h>
#elif __has_include(<MddBootstrap.h>)
#include <MddBootstrap.h>
#endif
#include <WindowsAppSDK-VersionInfo.h>
#endif

namespace AppWindowTitleBarBridge {
namespace {

Metrics g_Metrics;
bool g_TraceLoggedRuntimeDisabled = false;
bool g_TraceLoggedThemeUpdate = false;

#if defined(_WIN32) && STACK_ENABLE_APPWINDOW_TITLEBAR && STACK_HAS_WINDOWS_APP_SDK
winrt::Microsoft::UI::Windowing::AppWindow g_AppWindow { nullptr };
winrt::Microsoft::UI::Input::InputNonClientPointerSource g_NonClientSource { nullptr };
HWND g_PassthroughWindow = nullptr;
std::vector<RECT> g_PassthroughClientRects;
std::vector<HWND> g_PassthroughChildren;
bool g_BootstrapInitialized = false;
bool g_WinrtInitialized = false;
HMODULE g_BootstrapModule = nullptr;
using BootstrapInitialize2Fn = HRESULT (WINAPI*)(
    UINT32,
    PCWSTR,
    PACKAGE_VERSION,
    MddBootstrapInitializeOptions);
using BootstrapShutdownFn = void (WINAPI*)();
BootstrapInitialize2Fn g_BootstrapInitialize2 = nullptr;
BootstrapShutdownFn g_BootstrapShutdown = nullptr;
#endif

bool EnvFlagEnabled(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

bool TraceEnabled() {
    static const bool enabled = EnvFlagEnabled("STACK_APPWINDOW_TITLEBAR_TRACE");
    return enabled;
}

std::ofstream& TraceStream() {
    static std::ofstream stream;
    if (!stream.is_open()) {
        std::error_code ec;
        std::filesystem::create_directories(AppPaths::GetLogsDirectory(), ec);
        stream.open(AppPaths::GetLogsDirectory() / "appwindow_titlebar_trace.log", std::ios::app);
    }
    return stream;
}

#if defined(_WIN32)
void Trace(const char* event, GLFWwindow* window = nullptr, const char* detail = nullptr) {
    if (!TraceEnabled()) {
        return;
    }

    std::ofstream& stream = TraceStream();
    if (!stream.is_open()) {
        return;
    }

    HWND hwnd = window ? glfwGetWin32Window(window) : nullptr;
    const LONG_PTR style = hwnd ? GetWindowLongPtrW(hwnd, GWL_STYLE) : 0;
    const LONG_PTR exStyle = hwnd ? GetWindowLongPtrW(hwnd, GWL_EXSTYLE) : 0;
    stream << "event=" << (event ? event : "unknown")
           << " requested=" << (g_Metrics.requested ? 1 : 0)
           << " compiled=" << (g_Metrics.compiled ? 1 : 0)
           << " initialized=" << (g_Metrics.initialized ? 1 : 0)
           << " customizationSupported=" << (g_Metrics.customizationSupported ? 1 : 0)
           << " active=" << (g_Metrics.active ? 1 : 0)
           << " passthroughSupported=" << (g_Metrics.passthroughSupported ? 1 : 0)
           << " heightPx=" << g_Metrics.heightPx
           << " leftInsetPx=" << g_Metrics.leftInsetPx
           << " rightInsetPx=" << g_Metrics.rightInsetPx
           << " fallbackReason=\"" << g_Metrics.fallbackReason << "\""
           << " hwnd=" << reinterpret_cast<const void*>(hwnd)
           << " style=0x" << std::hex << static_cast<unsigned long long>(style)
           << " exStyle=0x" << static_cast<unsigned long long>(exStyle) << std::dec
           << " topMost=" << (hwnd && ((exStyle & WS_EX_TOPMOST) != 0) ? 1 : 0);
    if (detail && detail[0] != '\0') {
        stream << " detail=\"" << detail << "\"";
    }
    stream << '\n';
}
#else
void Trace(const char*, GLFWwindow* = nullptr, const char* = nullptr) {}
#endif

void SetFallback(const std::string& reason, GLFWwindow* window = nullptr) {
    g_Metrics.initialized = true;
    g_Metrics.customizationSupported = false;
    g_Metrics.active = false;
    g_Metrics.heightPx = 0;
    g_Metrics.leftInsetPx = 0;
    g_Metrics.rightInsetPx = 0;
    g_Metrics.fallbackReason = reason;
    Trace("fallback", window);
}

#if defined(_WIN32) && STACK_ENABLE_APPWINDOW_TITLEBAR && STACK_HAS_WINDOWS_APP_SDK
void UnloadBootstrapModule() {
    g_BootstrapInitialize2 = nullptr;
    g_BootstrapShutdown = nullptr;
    if (g_BootstrapModule != nullptr) {
        FreeLibrary(g_BootstrapModule);
        g_BootstrapModule = nullptr;
    }
}

bool LoadBootstrapModule(std::string& error) {
    if (g_BootstrapModule != nullptr &&
        g_BootstrapInitialize2 != nullptr &&
        g_BootstrapShutdown != nullptr) {
        return true;
    }
    const std::filesystem::path path =
        AppPaths::GetRuntimeDirectory() / "Microsoft.WindowsAppRuntime.Bootstrap.dll";
    g_BootstrapModule = LoadLibraryExW(
        path.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (g_BootstrapModule == nullptr) {
        std::ostringstream detail;
        detail << "managed bootstrap DLL unavailable (error " << GetLastError() << ")";
        error = detail.str();
        return false;
    }
    g_BootstrapInitialize2 = reinterpret_cast<BootstrapInitialize2Fn>(
        GetProcAddress(g_BootstrapModule, "MddBootstrapInitialize2"));
    g_BootstrapShutdown = reinterpret_cast<BootstrapShutdownFn>(
        GetProcAddress(g_BootstrapModule, "MddBootstrapShutdown"));
    if (g_BootstrapInitialize2 == nullptr || g_BootstrapShutdown == nullptr) {
        error = "managed bootstrap DLL is missing required exports";
        UnloadBootstrapModule();
        return false;
    }
    return true;
}

winrt::Windows::UI::Color ToWindowsColor(const ImVec4& color) {
    auto toByte = [](float value) {
        return static_cast<std::uint8_t>(std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    return winrt::Windows::UI::Color{
        toByte(color.w),
        toByte(color.x),
        toByte(color.y),
        toByte(color.z)
    };
}

template <typename T>
T UnsupportedPropertyValue() {
    winrt::throw_hresult(E_NOTIMPL);
    return T{};
}

struct ColorReference :
    winrt::implements<
        ColorReference,
        winrt::Windows::Foundation::IReference<winrt::Windows::UI::Color>,
        winrt::Windows::Foundation::IPropertyValue> {
    explicit ColorReference(winrt::Windows::UI::Color value)
        : m_Value(value) {}

    winrt::Windows::UI::Color Value() const {
        return m_Value;
    }

    winrt::Windows::Foundation::PropertyType Type() const {
        return winrt::Windows::Foundation::PropertyType::OtherType;
    }

    bool IsNumericScalar() const {
        return false;
    }

    uint8_t GetUInt8() const { return UnsupportedPropertyValue<uint8_t>(); }
    int16_t GetInt16() const { return UnsupportedPropertyValue<int16_t>(); }
    uint16_t GetUInt16() const { return UnsupportedPropertyValue<uint16_t>(); }
    int32_t GetInt32() const { return UnsupportedPropertyValue<int32_t>(); }
    uint32_t GetUInt32() const { return UnsupportedPropertyValue<uint32_t>(); }
    int64_t GetInt64() const { return UnsupportedPropertyValue<int64_t>(); }
    uint64_t GetUInt64() const { return UnsupportedPropertyValue<uint64_t>(); }
    float GetSingle() const { return UnsupportedPropertyValue<float>(); }
    double GetDouble() const { return UnsupportedPropertyValue<double>(); }
    char16_t GetChar16() const { return UnsupportedPropertyValue<char16_t>(); }
    bool GetBoolean() const { return UnsupportedPropertyValue<bool>(); }
    winrt::hstring GetString() const { return UnsupportedPropertyValue<winrt::hstring>(); }
    winrt::guid GetGuid() const { return UnsupportedPropertyValue<winrt::guid>(); }
    winrt::Windows::Foundation::DateTime GetDateTime() const { return UnsupportedPropertyValue<winrt::Windows::Foundation::DateTime>(); }
    winrt::Windows::Foundation::TimeSpan GetTimeSpan() const { return UnsupportedPropertyValue<winrt::Windows::Foundation::TimeSpan>(); }
    winrt::Windows::Foundation::Point GetPoint() const { return UnsupportedPropertyValue<winrt::Windows::Foundation::Point>(); }
    winrt::Windows::Foundation::Size GetSize() const { return UnsupportedPropertyValue<winrt::Windows::Foundation::Size>(); }
    winrt::Windows::Foundation::Rect GetRect() const { return UnsupportedPropertyValue<winrt::Windows::Foundation::Rect>(); }

    void GetUInt8Array(winrt::com_array<uint8_t>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetInt16Array(winrt::com_array<int16_t>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetUInt16Array(winrt::com_array<uint16_t>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetInt32Array(winrt::com_array<int32_t>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetUInt32Array(winrt::com_array<uint32_t>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetInt64Array(winrt::com_array<int64_t>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetUInt64Array(winrt::com_array<uint64_t>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetSingleArray(winrt::com_array<float>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetDoubleArray(winrt::com_array<double>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetChar16Array(winrt::com_array<char16_t>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetBooleanArray(winrt::com_array<bool>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetStringArray(winrt::com_array<winrt::hstring>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetInspectableArray(winrt::com_array<winrt::Windows::Foundation::IInspectable>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetGuidArray(winrt::com_array<winrt::guid>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetDateTimeArray(winrt::com_array<winrt::Windows::Foundation::DateTime>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetTimeSpanArray(winrt::com_array<winrt::Windows::Foundation::TimeSpan>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetPointArray(winrt::com_array<winrt::Windows::Foundation::Point>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetSizeArray(winrt::com_array<winrt::Windows::Foundation::Size>&) const { winrt::throw_hresult(E_NOTIMPL); }
    void GetRectArray(winrt::com_array<winrt::Windows::Foundation::Rect>&) const { winrt::throw_hresult(E_NOTIMPL); }

private:
    winrt::Windows::UI::Color m_Value;
};

winrt::Windows::Foundation::IReference<winrt::Windows::UI::Color> BoxColor(
    winrt::Windows::UI::Color value) {
    return winrt::make<ColorReference>(value);
}

void RefreshMetrics() {
    if (!g_AppWindow) {
        return;
    }

    const auto titleBar = g_AppWindow.TitleBar();
    g_Metrics.heightPx = titleBar.Height();
    g_Metrics.leftInsetPx = titleBar.LeftInset();
    g_Metrics.rightInsetPx = titleBar.RightInset();
}

constexpr UINT_PTR kCaptionPassthroughSubclass = 1;

LRESULT CALLBACK CaptionPassthroughChildProc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR) {
    if (message == WM_NCHITTEST) {
        const LRESULT nativeHit = DefSubclassProc(hwnd, message, wParam, lParam);
        if ((nativeHit == HTCLIENT || nativeHit == HTCAPTION || nativeHit == HTSYSMENU) &&
            IsCaptionPassthroughPoint(
                static_cast<SHORT>(LOWORD(lParam)), static_cast<SHORT>(HIWORD(lParam)))) {
            // The SDK's input sink is a separate HWND above the GLFW client.
            // HTCLIENT on the parent cannot bypass it. Let Windows deliver the
            // whole gesture to the underlying same-thread client instead.
            return HTTRANSPARENT;
        }
        return nativeHit;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, CaptionPassthroughChildProc, subclassId);
        g_PassthroughChildren.erase(
            std::remove(g_PassthroughChildren.begin(), g_PassthroughChildren.end(), hwnd),
            g_PassthroughChildren.end());
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

BOOL CALLBACK AttachCaptionPassthroughChild(HWND child, LPARAM parentValue) {
    const HWND parent = reinterpret_cast<HWND>(parentValue);
    // Do not depend on SDK-private class names or subclass another thread.
    // Only declared caption client regions change hit testing; native caption
    // buttons, drag areas and resize edges keep their original results.
    if (GetParent(child) != parent ||
        GetWindowThreadProcessId(child, nullptr) != GetCurrentThreadId() ||
        std::find(g_PassthroughChildren.begin(), g_PassthroughChildren.end(), child) !=
            g_PassthroughChildren.end()) return TRUE;
    if (SetWindowSubclass(child, CaptionPassthroughChildProc, kCaptionPassthroughSubclass, 0))
        g_PassthroughChildren.push_back(child);
    return TRUE;
}

void ClearCaptionPassthroughChildren() {
    for (HWND child : g_PassthroughChildren)
        RemoveWindowSubclass(child, CaptionPassthroughChildProc, kCaptionPassthroughSubclass);
    g_PassthroughChildren.clear();
}

void ClearPassthroughRegionsReal() {
    ClearCaptionPassthroughChildren();
    g_PassthroughWindow = nullptr;
    g_PassthroughClientRects.clear();
    if (!g_NonClientSource) {
        return;
    }

    std::vector<winrt::Windows::Graphics::RectInt32> empty;
    g_NonClientSource.SetRegionRects(
        winrt::Microsoft::UI::Input::NonClientRegionKind::Passthrough,
        winrt::array_view<const winrt::Windows::Graphics::RectInt32>(empty));
}
#endif

} // namespace

bool RuntimeFlagEnabled() {
#if defined(_WIN32) && STACK_ENABLE_APPWINDOW_TITLEBAR
    return !EnvFlagEnabled("STACK_DISABLE_APPWINDOW_TITLEBAR");
#else
    return false;
#endif
}

void Initialize(GLFWwindow* window) {
    g_Metrics = Metrics{};
    g_Metrics.requested = RuntimeFlagEnabled();
    g_Metrics.compiled =
#if defined(_WIN32) && STACK_ENABLE_APPWINDOW_TITLEBAR && STACK_HAS_WINDOWS_APP_SDK
        true;
#else
        false;
#endif

    if (!g_Metrics.requested) {
        if (!g_TraceLoggedRuntimeDisabled) {
            g_TraceLoggedRuntimeDisabled = true;
            SetFallback("runtime flag disabled", window);
        }
        return;
    }

#if !defined(_WIN32)
    SetFallback("not Windows", window);
#elif !STACK_ENABLE_APPWINDOW_TITLEBAR
    SetFallback("built with STACK_ENABLE_APPWINDOW_TITLEBAR=OFF", window);
#elif !STACK_HAS_WINDOWS_APP_SDK
    SetFallback("Windows App SDK package not found at build time", window);
#else
    HWND hwnd = window ? glfwGetWin32Window(window) : nullptr;
    if (!hwnd) {
        SetFallback("missing GLFW HWND", window);
        return;
    }

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & WS_CAPTION) == 0 || (style & WS_THICKFRAME) == 0) {
        SetFallback("main HWND is not a normal decorated resizable window", window);
        return;
    }

    try {
        if (!g_WinrtInitialized) {
            winrt::init_apartment(winrt::apartment_type::single_threaded);
            g_WinrtInitialized = true;
            Trace("winrt-initialized", window);
        }

        if (!g_BootstrapInitialized) {
            std::string bootstrapError;
            if (!LoadBootstrapModule(bootstrapError)) {
                SetFallback(bootstrapError, window);
                return;
            }
            PACKAGE_VERSION minVersion {};
            const HRESULT bootstrapHr = g_BootstrapInitialize2(
                WINDOWSAPPSDK_RELEASE_MAJORMINOR,
                WINDOWSAPPSDK_RELEASE_VERSION_TAG_W,
                minVersion,
                MddBootstrapInitializeOptions_None);
            if (FAILED(bootstrapHr)) {
                std::ostringstream detail;
                detail << "MddBootstrapInitialize2 hr=0x"
                       << std::hex << static_cast<unsigned long>(bootstrapHr) << std::dec;
                SetFallback(detail.str(), window);
                UnloadBootstrapModule();
                return;
            }
            g_BootstrapInitialized = true;
            Trace("bootstrap-initialized", window);
        }

        g_Metrics.customizationSupported =
            winrt::Microsoft::UI::Windowing::AppWindowTitleBar::IsCustomizationSupported();
        if (!g_Metrics.customizationSupported) {
            SetFallback("AppWindowTitleBar customization unsupported on this OS", window);
            return;
        }

        const auto windowId = winrt::Microsoft::UI::GetWindowIdFromWindow(hwnd);
        g_AppWindow = winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);
        if (!g_AppWindow) {
            SetFallback("AppWindow::GetFromWindowId returned null", window);
            return;
        }

        auto titleBar = g_AppWindow.TitleBar();
        titleBar.ExtendsContentIntoTitleBar(true);
        // The rail owns the top-left corner. A hidden native icon can still
        // reserve this hit region unless its system-menu area is removed too.
        titleBar.IconShowOptions(winrt::Microsoft::UI::Windowing::IconShowOptions::HideIconAndSystemMenu);
        titleBar.PreferredHeightOption(winrt::Microsoft::UI::Windowing::TitleBarHeightOption::Tall);
        g_Metrics.initialized = true;
        g_Metrics.active = true;
        g_Metrics.fallbackReason.clear();
        RefreshMetrics();
        Trace("content-extended", window);

        const winrt::Windows::UI::Color transparent { 0, 0, 0, 0 };
        try {
            titleBar.ButtonBackgroundColor(BoxColor(transparent));
            titleBar.ButtonInactiveBackgroundColor(BoxColor(transparent));
        } catch (const winrt::hresult_error& error) {
            std::ostringstream detail;
            detail << "button color hresult=0x"
                   << std::hex << static_cast<unsigned long>(error.code()) << std::dec;
            g_Metrics.fallbackReason = detail.str();
            Trace("button-color-fallback", window);
        }

        try {
            g_NonClientSource =
                winrt::Microsoft::UI::Input::InputNonClientPointerSource::GetForWindowId(windowId);
            g_Metrics.passthroughSupported = true;
            Trace("passthrough-enabled", window);
        } catch (const winrt::hresult_error& error) {
            g_NonClientSource = nullptr;
            g_Metrics.passthroughSupported = false;
            std::ostringstream detail;
            detail << "passthrough unavailable hresult=0x"
                   << std::hex << static_cast<unsigned long>(error.code()) << std::dec;
            g_Metrics.fallbackReason = detail.str();
            Trace("native-input-fallback", window);
        }

        Trace("enabled", window);
    } catch (const winrt::hresult_error& error) {
        std::ostringstream detail;
        detail << "winrt hresult=0x" << std::hex << static_cast<unsigned long>(error.code()) << std::dec;
        SetFallback(detail.str(), window);
    } catch (const std::exception& error) {
        SetFallback(std::string("exception: ") + error.what(), window);
    }
#endif
}

void Shutdown() {
    Trace("shutdown");
#if defined(_WIN32) && STACK_ENABLE_APPWINDOW_TITLEBAR && STACK_HAS_WINDOWS_APP_SDK
    ClearPassthroughRegionsReal();
    if (g_AppWindow) {
        try {
            g_AppWindow.TitleBar().ExtendsContentIntoTitleBar(false);
        } catch (...) {
        }
    }
    g_NonClientSource = nullptr;
    g_AppWindow = nullptr;
    if (g_BootstrapInitialized) {
        if (g_BootstrapShutdown != nullptr) {
            g_BootstrapShutdown();
        }
        g_BootstrapInitialized = false;
    }
    UnloadBootstrapModule();
#endif
    g_Metrics = Metrics{};
    g_TraceLoggedThemeUpdate = false;
}

void UpdateTheme(
    GLFWwindow* window,
    const ImVec4& foreground,
    const ImVec4& hoverBackground,
    const ImVec4& pressedBackground,
    float captionButtonOpacity) {
#if defined(_WIN32) && STACK_ENABLE_APPWINDOW_TITLEBAR && STACK_HAS_WINDOWS_APP_SDK
    if (!g_Metrics.active || !g_AppWindow) {
        return;
    }

    try {
        const float opacity = std::clamp(captionButtonOpacity, 0.0f, 1.0f);
        ImVec4 activeForeground = foreground;
        ImVec4 inactiveForeground = foreground;
        ImVec4 hover = hoverBackground;
        ImVec4 pressed = pressedBackground;
        activeForeground.w *= opacity;
        inactiveForeground.w *= 0.72f * opacity;
        hover.w *= opacity;
        pressed.w *= opacity;
        auto titleBar = g_AppWindow.TitleBar();
        titleBar.ButtonForegroundColor(BoxColor(ToWindowsColor(activeForeground)));
        titleBar.ButtonInactiveForegroundColor(BoxColor(ToWindowsColor(inactiveForeground)));
        titleBar.ButtonHoverBackgroundColor(BoxColor(ToWindowsColor(hover)));
        titleBar.ButtonPressedBackgroundColor(BoxColor(ToWindowsColor(pressed)));
        RefreshMetrics();
        if (!g_TraceLoggedThemeUpdate) {
            g_TraceLoggedThemeUpdate = true;
            Trace("theme-updated", window);
        }
    } catch (const winrt::hresult_error& error) {
        std::ostringstream detail;
        detail << "theme hresult=0x" << std::hex << static_cast<unsigned long>(error.code()) << std::dec;
        SetFallback(detail.str(), window);
    }
#else
    (void)window;
    (void)foreground;
    (void)hoverBackground;
    (void)pressedBackground;
    (void)captionButtonOpacity;
#endif
}

void SyncPassthroughRegions(GLFWwindow* window, const std::vector<ImRect>& screenRects) {
#if defined(_WIN32) && STACK_ENABLE_APPWINDOW_TITLEBAR && STACK_HAS_WINDOWS_APP_SDK
    if (!g_Metrics.active || !window) {
        ClearPassthroughRegionsReal();
        return;
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) {
        ClearPassthroughRegionsReal();
        return;
    }

    RefreshMetrics();

    RECT clientRect {};
    GetClientRect(hwnd, &clientRect);
    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

    const float scaleX = (windowWidth > 0 && framebufferWidth > 0)
        ? static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth)
        : 1.0f;
    const float scaleY = (windowHeight > 0 && framebufferHeight > 0)
        ? static_cast<float>(framebufferHeight) / static_cast<float>(windowHeight)
        : 1.0f;

    const int clientWidth = framebufferWidth > 0 ? framebufferWidth : (clientRect.right - clientRect.left);
    const int titlebarHeight = std::max(0, g_Metrics.heightPx);
    // HideIconAndSystemMenu gives this corner to the rail. Some runtime
    // versions retain a nonzero LeftInset metric after hiding the icon.
    // Clipping to that metric removes the panel toggle from passthrough.
    const int leftLimit = 0;
    const int rightLimit = std::max(leftLimit, clientWidth - std::max(0, g_Metrics.rightInsetPx));
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const ImVec2 viewportPos = mainViewport ? mainViewport->Pos : ImVec2(0.0f, 0.0f);

    std::vector<winrt::Windows::Graphics::RectInt32> passthroughRects;
    passthroughRects.reserve(screenRects.size());
    std::vector<RECT> clientRects;
    clientRects.reserve(screenRects.size());

    for (const ImRect& rect : screenRects) {
        const float minX = (rect.Min.x - viewportPos.x) * scaleX;
        const float minY = (rect.Min.y - viewportPos.y) * scaleY;
        const float maxX = (rect.Max.x - viewportPos.x) * scaleX;
        const float maxY = (rect.Max.y - viewportPos.y) * scaleY;

        const int left = std::clamp(static_cast<int>(std::floor(minX)), leftLimit, rightLimit);
        const int top = std::clamp(static_cast<int>(std::floor(minY)), 0, titlebarHeight);
        const int right = std::clamp(static_cast<int>(std::ceil(maxX)), leftLimit, rightLimit);
        const int bottom = std::clamp(static_cast<int>(std::ceil(maxY)), 0, titlebarHeight);
        if (right <= left || bottom <= top) {
            continue;
        }

        passthroughRects.push_back(winrt::Windows::Graphics::RectInt32{
            left,
            top,
            right - left,
            bottom - top
        });
        clientRects.push_back(RECT{left, top, right, bottom});
    }

    // Keep hit regions stable through a press/release. Reconfigure the native
    // title bar only when layout, DPI or popup coverage actually changes.
    if (g_PassthroughWindow && g_PassthroughWindow != hwnd)
        ClearCaptionPassthroughChildren();
    // The SDK may recreate its input children without changing these rectangles.
    EnumChildWindows(hwnd, AttachCaptionPassthroughChild, reinterpret_cast<LPARAM>(hwnd));
    if (g_PassthroughWindow == hwnd && clientRects.size() == g_PassthroughClientRects.size() &&
        std::equal(clientRects.begin(), clientRects.end(), g_PassthroughClientRects.begin(),
            [](const RECT& left, const RECT& right) { return EqualRect(&left, &right) != FALSE; }))
        return;
    g_PassthroughWindow = hwnd;
    g_PassthroughClientRects = std::move(clientRects);

    if (g_NonClientSource) {
        g_NonClientSource.SetRegionRects(
            winrt::Microsoft::UI::Input::NonClientRegionKind::Passthrough,
            winrt::array_view<const winrt::Windows::Graphics::RectInt32>(passthroughRects));
    }

    if (TraceEnabled()) {
        std::ostringstream detail;
        detail << "count=" << passthroughRects.size()
               << " scale=" << scaleX << "," << scaleY
               << " viewportPos=" << viewportPos.x << "," << viewportPos.y
               << " framebuffer=" << framebufferWidth << "," << framebufferHeight
               << " titlebarHeight=" << titlebarHeight
               << " insets=" << g_Metrics.leftInsetPx << "," << g_Metrics.rightInsetPx;
        for (const auto& rect : passthroughRects) {
            detail << " rect=" << rect.X << "," << rect.Y << "," << rect.Width << "," << rect.Height;
        }
        Trace("passthrough-synced", window, detail.str().c_str());
    }
#else
    (void)window;
    (void)screenRects;
#endif
}

void ClearPassthroughRegions() {
#if defined(_WIN32) && STACK_ENABLE_APPWINDOW_TITLEBAR && STACK_HAS_WINDOWS_APP_SDK
    ClearPassthroughRegionsReal();
#endif
}

bool IsCaptionPassthroughPoint(int screenX, int screenY) {
#if defined(_WIN32) && STACK_ENABLE_APPWINDOW_TITLEBAR && STACK_HAS_WINDOWS_APP_SDK
    // Apply the same declared client regions even when the SDK advertises
    // passthrough support but returns a native caption hit for this point.
    if (!g_Metrics.active || !g_PassthroughWindow)
        return false;
    POINT point{screenX, screenY};
    if (!ScreenToClient(g_PassthroughWindow, &point)) return false;
    return std::any_of(g_PassthroughClientRects.begin(), g_PassthroughClientRects.end(),
        [&](const RECT& rect) { return PtInRect(&rect, point) != FALSE; });
#else
    (void)screenX;
    (void)screenY;
    return false;
#endif
}

const Metrics& GetMetrics() {
    return g_Metrics;
}

bool IsActive() {
    return g_Metrics.active;
}

} // namespace AppWindowTitleBarBridge
