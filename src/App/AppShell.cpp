#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "AppShell.h"
#include "AppWindowTitleBarBridge.h"
#include "AppPaths.h"
#include "AppSettingsPopup.h"
#include "AppVersion.h"
#include "Async/TaskSystem.h"
#include "Renderer/GLLoader.h"
#include "settings/AppearanceTheme.h"
#include <GLFW/glfw3.h>
#include "imgui.h"
#include <imgui_internal.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#endif
#include <functional>
#include <iostream>
#include <vector>
#include "../Library/LibraryManager.h"
#include "../Utils/FileDialogs.h"
#include "../Utils/ImGuiExtras.h"
#include "../Utils/NativeWindowTheme.h"
#include "ThirdParty/stb_image.h"
#include "Renderer/GLHelpers.h"
#include "Resources/EmbeddedSplash.h"
#include "Resources/EmbeddedTabIcons.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <thread>
#include <chrono>

namespace {

bool EnvFlagEnabled(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

ImVec4 OpaqueColor(ImVec4 color) {
    color.w = 1.0f;
    return color;
}

NativeWindowTheme::CaptionThemeResult ApplyNativeTitleBarTheme(
    GLFWwindow* window,
    const StackAppearance::AppearanceManager* appearance) {
    if (!window) {
        return {};
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 caption = style.Colors[ImGuiCol_WindowBg];
    ImVec4 text = style.Colors[ImGuiCol_Text];
    ImVec4 border = caption;
    if (appearance) {
        const StackAppearance::RuntimeSurfacePalette palette = appearance->GetRuntimeSurfacePalette();
        caption = palette.chromeSurface;
        text = appearance->GetWorkingTheme().colors[ImGuiCol_Text];
        border = palette.border;
    }

    return NativeWindowTheme::ApplyMainWindow(
        window,
        OpaqueColor(caption),
        OpaqueColor(text),
        OpaqueColor(border));
}

#if defined(_WIN32)
std::string FormatCaptionThemeResult(const NativeWindowTheme::CaptionThemeResult& result) {
    std::ostringstream stream;
    stream << "osBuild=" << result.osBuild
           << " topMostCleared=" << (result.topMostCleared ? 1 : 0)
           << " frameStyleChanged=" << (result.frameStyleChanged ? 1 : 0)
           << " darkHr=0x" << std::hex << static_cast<unsigned long>(result.darkMode)
           << " captionHr=0x" << static_cast<unsigned long>(result.captionColor)
           << " textHr=0x" << static_cast<unsigned long>(result.textColor)
           << " borderHr=0x" << static_cast<unsigned long>(result.borderColor)
           << " style=0x" << static_cast<unsigned long long>(result.style)
           << " exStyle=0x" << static_cast<unsigned long long>(result.exStyle)
           << std::dec;
    return stream.str();
}
#endif

void ApplyBaseOpenGlWindowHints() {
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
}

enum RootTabId {
    RootTabLibrary = 0,
    RootTabEditor = 1,
    RootTabTools = 2,
    RootTabRaw = 3,
    RootTabComposite = 4
};

bool IsFadeableRootTab(int tabId) {
    return tabId == RootTabLibrary ||
        tabId == RootTabEditor ||
        tabId == RootTabTools ||
        tabId == RootTabRaw;
}

struct RootTabDescriptor {
    int id = -1;
    const char* label = "";
    unsigned int iconTexture = 0;
    std::function<void()> renderBody;
};

constexpr double kLibraryLoadFadeOutSeconds = 0.48;
constexpr double kLibraryLoadSpinnerFadeInSeconds = 0.26;
constexpr double kLibraryLoadSpinnerMinVisibleSeconds = 0.85;
constexpr double kLibraryLoadSpinnerFadeOutSeconds = 0.32;
constexpr double kLibraryLoadEditorRevealSeconds = 1.90;
constexpr double kRootTabBodyFadeOutSeconds = 0.12;
constexpr double kRootTabBodyFadeInSeconds = 0.34;
constexpr float kRootTabBodyFadeMinAlpha = 0.08f;
constexpr double kAppStartupMotionSeconds = 1.10;
constexpr double kAppStartupBackgroundFadeSeconds = 1.55;
constexpr double kClosingSurfaceMinVisibleSeconds = 0.95;
constexpr double kClosingSurfaceMaxDrainSeconds = 2.25;
constexpr int kClosingSurfaceMinPresentedFrames = 12;
constexpr double kClosingTextIntroSeconds = 0.24;
constexpr bool kLibraryLoadTransitionDiagnostics = true;

float Saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float TimedEaseOutCubic(double elapsed, double duration) {
    if (duration <= 0.0) {
        return 1.0f;
    }
    return ImGuiExtras::EaseOutCubic(Saturate(static_cast<float>(elapsed / duration)));
}

bool IsLibraryPerfTraceEnabled() {
    static const bool enabled = []() {
        const char* value = std::getenv("STACK_LIBRARY_PERF_TRACE");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

std::ofstream& LibraryPerfTraceStream() {
    static std::ofstream stream;
    if (!stream.is_open()) {
        std::error_code ec;
        std::filesystem::create_directories(AppPaths::GetLogsDirectory(), ec);
        stream.open(AppPaths::GetLogsDirectory() / "library_perf_trace.log", std::ios::app);
    }
    return stream;
}

bool IsDetachedPreviewTraceEnabled() {
    static const bool enabled = []() {
        const char* value = std::getenv("STACK_DETACHED_PREVIEW_TRACE");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

std::ofstream& DetachedPreviewTraceStream() {
    static std::ofstream stream;
    if (!stream.is_open()) {
        std::error_code ec;
        std::filesystem::create_directories(AppPaths::GetLogsDirectory(), ec);
        stream.open(AppPaths::GetLogsDirectory() / "detached_preview_trace.log", std::ios::app);
    }
    return stream;
}

bool IsNativeMainChromeRequested() {
    return true;
}

bool IsExperimentalClientChromeEnabled() {
    static const bool enabled = EnvFlagEnabled("STACK_EXPERIMENTAL_CLIENT_CHROME");
    return enabled;
}

bool IsExperimentalExtendedChromeEnabled() {
    return false;
}

enum class CustomChromeExperimentStage {
    Full,
    DecoratedOffOnly,
    PassThroughWndProc,
    HitTestOnly,
    NonClientCalc,
};

CustomChromeExperimentStage GetCustomChromeExperimentStage() {
    static const CustomChromeExperimentStage stage = []() {
        const char* value = std::getenv("STACK_CUSTOM_MAIN_CHROME_STAGE");
        if (!value || value[0] == '\0') {
            return CustomChromeExperimentStage::Full;
        }

        std::string normalized(value);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        if (normalized == "decorated-off" || normalized == "decorated_off" || normalized == "decorated") {
            return CustomChromeExperimentStage::DecoratedOffOnly;
        }
        if (normalized == "pass-through" || normalized == "pass_through" || normalized == "wndproc") {
            return CustomChromeExperimentStage::PassThroughWndProc;
        }
        if (normalized == "hittest" || normalized == "hit-test" || normalized == "hit_test") {
            return CustomChromeExperimentStage::HitTestOnly;
        }
        if (normalized == "nccalc" || normalized == "nonclient" || normalized == "non-client") {
            return CustomChromeExperimentStage::NonClientCalc;
        }
        return CustomChromeExperimentStage::Full;
    }();
    return stage;
}

bool CustomChromeStageInstallsWndProc() {
    const CustomChromeExperimentStage stage = GetCustomChromeExperimentStage();
    return stage != CustomChromeExperimentStage::DecoratedOffOnly;
}

bool CustomChromeStageUsesFramelessStyle() {
    const CustomChromeExperimentStage stage = GetCustomChromeExperimentStage();
    return stage != CustomChromeExperimentStage::DecoratedOffOnly &&
        stage != CustomChromeExperimentStage::PassThroughWndProc;
}

bool CustomChromeStageHandlesHitTest() {
    const CustomChromeExperimentStage stage = GetCustomChromeExperimentStage();
    return stage == CustomChromeExperimentStage::HitTestOnly ||
        stage == CustomChromeExperimentStage::NonClientCalc ||
        stage == CustomChromeExperimentStage::Full;
}

bool CustomChromeStageHandlesNonClientCalc() {
    const CustomChromeExperimentStage stage = GetCustomChromeExperimentStage();
    return stage == CustomChromeExperimentStage::NonClientCalc ||
        stage == CustomChromeExperimentStage::Full;
}

bool CustomChromeStageRendersWindowControls() {
    return GetCustomChromeExperimentStage() == CustomChromeExperimentStage::Full;
}

const char* CustomChromeExperimentStageName() {
    switch (GetCustomChromeExperimentStage()) {
        case CustomChromeExperimentStage::DecoratedOffOnly:
            return "decorated-off";
        case CustomChromeExperimentStage::PassThroughWndProc:
            return "pass-through";
        case CustomChromeExperimentStage::HitTestOnly:
            return "hittest";
        case CustomChromeExperimentStage::NonClientCalc:
            return "nccalc";
        case CustomChromeExperimentStage::Full:
        default:
            return "full";
    }
}

bool IsMainWindowTraceEnabled() {
    static const bool enabled = []() {
        const char* value = std::getenv("STACK_MAIN_WINDOW_TRACE");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

std::ofstream& MainWindowTraceStream() {
    static std::ofstream stream;
    if (!stream.is_open()) {
        std::error_code ec;
        std::filesystem::create_directories(AppPaths::GetLogsDirectory(), ec);
        stream.open(AppPaths::GetLogsDirectory() / "main_window_trace.log", std::ios::app);
    }
    return stream;
}

bool IsShutdownTraceEnabled() {
    static const bool enabled = []() {
        const char* value = std::getenv("STACK_SHUTDOWN_TRACE");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

std::ofstream& ShutdownTraceStream() {
    static std::ofstream stream;
    if (!stream.is_open()) {
        std::error_code ec;
        std::filesystem::create_directories(AppPaths::GetLogsDirectory(), ec);
        stream.open(AppPaths::GetLogsDirectory() / "shutdown_trace.log", std::ios::app);
    }
    return stream;
}

void TraceLibraryPerfFrame(
    int frame,
    double secondsSinceWindowShown,
    double frameMs,
    double pumpMs,
    double renderUiMs,
    double drawMs,
    const LibraryTextureUploadStats& uploadStats,
    const LibraryRenderStats& renderStats) {
    if (!IsLibraryPerfTraceEnabled()) {
        return;
    }

    const bool shouldLog =
        frameMs > 33.0 ||
        uploadStats.budgetHit ||
        uploadStats.projectUploads > 0 ||
        uploadStats.assetUploads > 0 ||
        uploadStats.projectDecodeQueued > 0 ||
        uploadStats.assetDecodeQueued > 0 ||
        renderStats.autoRefresh.requestedSignature ||
        renderStats.autoRefresh.signatureBusy;
    if (!shouldLog) {
        return;
    }

    std::ofstream& stream = LibraryPerfTraceStream();
    if (!stream.is_open()) {
        return;
    }

    stream << std::fixed << std::setprecision(2)
           << "frame=" << frame
           << " t=" << secondsSinceWindowShown
           << " frameMs=" << frameMs
           << " pumpMs=" << pumpMs
           << " renderUiMs=" << renderUiMs
           << " drawMs=" << drawMs
           << " uploadMs=" << uploadStats.elapsedMs
           << " uploads=" << (uploadStats.projectUploads + uploadStats.assetUploads)
           << " projectUploads=" << uploadStats.projectUploads
           << " assetUploads=" << uploadStats.assetUploads
           << " queuedDecodes=" << (uploadStats.projectDecodeQueued + uploadStats.assetDecodeQueued)
           << " pendingDecodes=" << (uploadStats.pendingProjectDecodes + uploadStats.pendingAssetDecodes)
           << " pendingReadyUploads=" << uploadStats.pendingReadyUploads
           << " uploadBudgetHit=" << (uploadStats.budgetHit ? 1 : 0)
           << " libAutoRefreshMs=" << renderStats.autoRefreshMs
           << " libLayoutMs=" << renderStats.layoutMs
           << " libCardRenderMs=" << renderStats.cardRenderMs
           << " totalCards=" << renderStats.totalCards
           << " packedCards=" << renderStats.packedCards
           << " visibleCards=" << renderStats.visibleCards
           << " layoutCacheHit=" << (renderStats.layoutCacheHit ? 1 : 0)
           << " sigRequested=" << (renderStats.autoRefresh.requestedSignature ? 1 : 0)
           << " sigBusy=" << (renderStats.autoRefresh.signatureBusy ? 1 : 0)
           << " refreshBusySkip=" << (renderStats.autoRefresh.skippedForBusyWork ? 1 : 0)
           << " warmupSkip=" << (renderStats.autoRefresh.skippedForWarmup ? 1 : 0)
           << '\n';
}

unsigned char* LoadImagePixelsWithExplicitFlip(
    const std::filesystem::path& path,
    const bool flipVertically,
    int* outWidth,
    int* outHeight,
    int* outChannels) {
    stbi_set_flip_vertically_on_load_thread(flipVertically ? 1 : 0);
    unsigned char* pixels = stbi_load(path.string().c_str(), outWidth, outHeight, outChannels, 4);
    stbi_set_flip_vertically_on_load_thread(0);
    return pixels;
}

bool IsSupportedDroppedImagePath(const std::string& path) {
    std::string extension;
    try {
        extension = std::filesystem::path(path).extension().string();
    } catch (...) {
        return false;
    }

    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension == ".png" ||
           extension == ".jpg" ||
           extension == ".jpeg" ||
           extension == ".bmp" ||
           extension == ".tga";
}

ImVec2 ScreenToWindowCursorPos(GLFWwindow* window, const ImVec2& screenPos) {
    int windowX = 0;
    int windowY = 0;
    glfwGetWindowPos(window, &windowX, &windowY);
    return ImVec2(
        screenPos.x - static_cast<float>(windowX),
        screenPos.y - static_cast<float>(windowY));
}

bool UseFramelessMainWindowChrome() {
    return false;
}

enum class WindowControlKind {
    Minimize,
    Maximize,
    Close
};

#if defined(_WIN32)
struct FramelessMainWindowChromeState {
    HWND hwnd = nullptr;
    WNDPROC originalWndProc = nullptr;
    RECT captionRect = { 0, 0, 0, 0 };
    bool captionRectValid = false;
    std::vector<RECT> exclusionRects;
    std::function<void()> releaseCursorCapture;
};

FramelessMainWindowChromeState g_FramelessMainWindowChromeState;

struct AppWindowTitlebarNativeInputState {
    HWND hwnd = nullptr;
    WNDPROC originalWndProc = nullptr;
    std::vector<std::pair<RECT, int>> tabRects;
    RECT settingsRect = { 0, 0, 0, 0 };
    bool settingsRectValid = false;
    int pendingTab = -1;
    bool pendingSettings = false;
};

AppWindowTitlebarNativeInputState g_AppWindowTitlebarNativeInputState;

const char* DescribeMainWindowMessage(UINT message) {
    switch (message) {
        case WM_ACTIVATE: return "WM_ACTIVATE";
        case WM_ACTIVATEAPP: return "WM_ACTIVATEAPP";
        case WM_CAPTURECHANGED: return "WM_CAPTURECHANGED";
        case WM_ENTERSIZEMOVE: return "WM_ENTERSIZEMOVE";
        case WM_EXITSIZEMOVE: return "WM_EXITSIZEMOVE";
        case WM_KILLFOCUS: return "WM_KILLFOCUS";
        case WM_CLOSE: return "WM_CLOSE";
        case WM_NCACTIVATE: return "WM_NCACTIVATE";
        case WM_NCCALCSIZE: return "WM_NCCALCSIZE";
        case WM_NCLBUTTONDOWN: return "WM_NCLBUTTONDOWN";
        case WM_SETFOCUS: return "WM_SETFOCUS";
        case WM_SHOWWINDOW: return "WM_SHOWWINDOW";
        case WM_SIZE: return "WM_SIZE";
        case WM_SYSCOMMAND: return "WM_SYSCOMMAND";
        case WM_WINDOWPOSCHANGED: return "WM_WINDOWPOSCHANGED";
        case WM_WINDOWPOSCHANGING: return "WM_WINDOWPOSCHANGING";
        default: return "WM_UNKNOWN";
    }
}

void TraceMainWindowNativeState(
    HWND hwnd,
    const char* event,
    UINT message = 0,
    WPARAM wParam = 0,
    LPARAM lParam = 0,
    const char* detail = nullptr) {
    if (!IsMainWindowTraceEnabled()) {
        return;
    }

    std::ofstream& stream = MainWindowTraceStream();
    if (!stream.is_open()) {
        return;
    }

    const int frame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1;
    stream << "frame=" << frame
           << " event=" << (event ? event : "unknown")
           << " nativeChrome=" << (IsNativeMainChromeRequested() ? 1 : 0)
           << " customChrome=" << (UseFramelessMainWindowChrome() ? 1 : 0)
           << " customChromeStage=" << CustomChromeExperimentStageName()
           << " decoratedOffKnownBad="
           << (UseFramelessMainWindowChrome() &&
                   GetCustomChromeExperimentStage() == CustomChromeExperimentStage::DecoratedOffOnly
               ? 1
               : 0)
           << " experimentalClientChrome=" << (IsExperimentalClientChromeEnabled() ? 1 : 0)
           << " experimentalExtendedChrome=" << (IsExperimentalExtendedChromeEnabled() ? 1 : 0)
           << " hwnd=" << reinterpret_cast<const void*>(hwnd);
    if (detail && detail[0] != '\0') {
        stream << " detail=" << detail;
    }

    if (message != 0) {
        stream << " message=" << DescribeMainWindowMessage(message)
               << " messageHex=0x" << std::hex << static_cast<unsigned int>(message) << std::dec
               << " wParam=" << static_cast<unsigned long long>(wParam)
               << " lParam=" << static_cast<long long>(lParam);
    }

    if (!hwnd) {
        stream << '\n';
        return;
    }

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const HWND owner = GetWindow(hwnd, GW_OWNER);
    const HWND foreground = GetForegroundWindow();
    const HWND active = GetActiveWindow();
    const HWND focus = GetFocus();
    const UINT dpi = GetDpiForWindow(hwnd);
    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    RECT windowRect {};
    GetWindowRect(hwnd, &windowRect);
    MONITORINFO monitorInfo {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const bool hasMonitorInfo = monitor && GetMonitorInfoW(monitor, &monitorInfo);
    stream << " style=0x" << std::hex << static_cast<unsigned long long>(style)
           << " exStyle=0x" << static_cast<unsigned long long>(exStyle) << std::dec
           << " osBuild=" << NativeWindowTheme::GetWindowsBuildNumber()
           << " visible=" << (IsWindowVisible(hwnd) ? 1 : 0)
           << " zoomed=" << (IsZoomed(hwnd) ? 1 : 0)
           << " iconic=" << (IsIconic(hwnd) ? 1 : 0)
           << " foreground=" << (foreground == hwnd ? 1 : 0)
           << " active=" << (active == hwnd ? 1 : 0)
           << " focus=" << (focus == hwnd ? 1 : 0)
           << " topMost=" << ((exStyle & WS_EX_TOPMOST) != 0 ? 1 : 0)
           << " ownerHwnd=" << reinterpret_cast<const void*>(owner)
           << " foregroundHwnd=" << reinterpret_cast<const void*>(foreground)
           << " activeHwnd=" << reinterpret_cast<const void*>(active)
           << " focusHwnd=" << reinterpret_cast<const void*>(focus)
           << " dpi=" << dpi
           << " monitor=" << reinterpret_cast<const void*>(monitor)
           << " windowRect=" << windowRect.left << "," << windowRect.top << "," << windowRect.right << "," << windowRect.bottom;
    if (hasMonitorInfo) {
        stream << " monitorRect="
               << monitorInfo.rcMonitor.left << "," << monitorInfo.rcMonitor.top << ","
               << monitorInfo.rcMonitor.right << "," << monitorInfo.rcMonitor.bottom
               << " workRect="
               << monitorInfo.rcWork.left << "," << monitorInfo.rcWork.top << ","
               << monitorInfo.rcWork.right << "," << monitorInfo.rcWork.bottom;
    }
    stream
           << '\n';
}

LRESULT CallFramelessMainWindowBaseProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_FramelessMainWindowChromeState.originalWndProc) {
        return CallWindowProcW(g_FramelessMainWindowChromeState.originalWndProc, hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CallAppWindowTitlebarBaseProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_AppWindowTitlebarNativeInputState.originalWndProc) {
        return CallWindowProcW(g_AppWindowTitlebarNativeInputState.originalWndProc, hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

RECT ImRectToScreenRect(const ImRect& rect);

void BeginAppWindowTitlebarNativeInputFrame() {
    g_AppWindowTitlebarNativeInputState.tabRects.clear();
    g_AppWindowTitlebarNativeInputState.settingsRect = { 0, 0, 0, 0 };
    g_AppWindowTitlebarNativeInputState.settingsRectValid = false;
}

void AddAppWindowTitlebarNativeTabRect(const ImRect& rect, int tabId) {
    if (!g_AppWindowTitlebarNativeInputState.hwnd) {
        return;
    }
    g_AppWindowTitlebarNativeInputState.tabRects.emplace_back(ImRectToScreenRect(rect), tabId);
}

void SetAppWindowTitlebarNativeSettingsRect(const ImRect& rect) {
    if (!g_AppWindowTitlebarNativeInputState.hwnd) {
        return;
    }
    g_AppWindowTitlebarNativeInputState.settingsRect = ImRectToScreenRect(rect);
    g_AppWindowTitlebarNativeInputState.settingsRectValid = true;
}

int ConsumeAppWindowTitlebarNativeTabRequest() {
    const int tabId = g_AppWindowTitlebarNativeInputState.pendingTab;
    g_AppWindowTitlebarNativeInputState.pendingTab = -1;
    return tabId;
}

bool ConsumeAppWindowTitlebarNativeSettingsRequest() {
    const bool requested = g_AppWindowTitlebarNativeInputState.pendingSettings;
    g_AppWindowTitlebarNativeInputState.pendingSettings = false;
    return requested;
}

bool HandleAppWindowTitlebarNativeClick(POINT cursor) {
    if (g_AppWindowTitlebarNativeInputState.settingsRectValid &&
        PtInRect(&g_AppWindowTitlebarNativeInputState.settingsRect, cursor)) {
        g_AppWindowTitlebarNativeInputState.pendingSettings = true;
        TraceMainWindowNativeState(
            g_AppWindowTitlebarNativeInputState.hwnd,
            "appwindow-titlebar-native-settings-click");
        return true;
    }

    for (const auto& [rect, tabId] : g_AppWindowTitlebarNativeInputState.tabRects) {
        if (PtInRect(&rect, cursor)) {
            g_AppWindowTitlebarNativeInputState.pendingTab = tabId;
            TraceMainWindowNativeState(
                g_AppWindowTitlebarNativeInputState.hwnd,
                "appwindow-titlebar-native-tab-click");
            return true;
        }
    }

    return false;
}

LRESULT CALLBACK AppWindowTitlebarNativeInputWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONDBLCLK: {
            POINT cursor {
                static_cast<LONG>(static_cast<SHORT>(LOWORD(lParam))),
                static_cast<LONG>(static_cast<SHORT>(HIWORD(lParam)))
            };
            if (HandleAppWindowTitlebarNativeClick(cursor)) {
                return 0;
            }
            break;
        }
        default:
            break;
    }

    return CallAppWindowTitlebarBaseProc(hwnd, message, wParam, lParam);
}

void InstallAppWindowTitlebarNativeInput(GLFWwindow* window) {
    if (!window || g_AppWindowTitlebarNativeInputState.hwnd) {
        return;
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) {
        return;
    }

    g_AppWindowTitlebarNativeInputState.hwnd = hwnd;
    g_AppWindowTitlebarNativeInputState.originalWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(AppWindowTitlebarNativeInputWndProc)));
    TraceMainWindowNativeState(hwnd, "appwindow-titlebar-native-input-installed");
}

void UninstallAppWindowTitlebarNativeInput() {
    if (g_AppWindowTitlebarNativeInputState.hwnd && g_AppWindowTitlebarNativeInputState.originalWndProc) {
        SetWindowLongPtrW(
            g_AppWindowTitlebarNativeInputState.hwnd,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(g_AppWindowTitlebarNativeInputState.originalWndProc));
        TraceMainWindowNativeState(g_AppWindowTitlebarNativeInputState.hwnd, "appwindow-titlebar-native-input-uninstalled");
    }
    g_AppWindowTitlebarNativeInputState = {};
}

LONG GetFramelessResizeBorderX() {
    return static_cast<LONG>(std::max(6, GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER)));
}

LONG GetFramelessResizeBorderY() {
    return static_cast<LONG>(std::max(6, GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER)));
}

void ClearFramelessMainWindowDragZone() {
    g_FramelessMainWindowChromeState.captionRect = { 0, 0, 0, 0 };
    g_FramelessMainWindowChromeState.captionRectValid = false;
    g_FramelessMainWindowChromeState.exclusionRects.clear();
}

RECT ImRectToScreenRect(const ImRect& rect) {
    return RECT{
        static_cast<LONG>(std::floor(rect.Min.x)),
        static_cast<LONG>(std::floor(rect.Min.y)),
        static_cast<LONG>(std::ceil(rect.Max.x)),
        static_cast<LONG>(std::ceil(rect.Max.y))
    };
}

void UpdateFramelessMainWindowDragZone(const ImVec2& min, const ImVec2& max, const std::vector<ImRect>& exclusions) {
    if (!g_FramelessMainWindowChromeState.hwnd) {
        return;
    }
    if (max.x <= min.x || max.y <= min.y) {
        ClearFramelessMainWindowDragZone();
        return;
    }

    g_FramelessMainWindowChromeState.captionRect.left = static_cast<LONG>(std::floor(min.x));
    g_FramelessMainWindowChromeState.captionRect.top = static_cast<LONG>(std::floor(min.y));
    g_FramelessMainWindowChromeState.captionRect.right = static_cast<LONG>(std::ceil(max.x));
    g_FramelessMainWindowChromeState.captionRect.bottom = static_cast<LONG>(std::ceil(max.y));
    g_FramelessMainWindowChromeState.captionRectValid = true;
    g_FramelessMainWindowChromeState.exclusionRects.clear();
    g_FramelessMainWindowChromeState.exclusionRects.reserve(exclusions.size());
    for (const ImRect& exclusion : exclusions) {
        if (exclusion.Max.x > exclusion.Min.x && exclusion.Max.y > exclusion.Min.y) {
            g_FramelessMainWindowChromeState.exclusionRects.push_back(ImRectToScreenRect(exclusion));
        }
    }
}

void SetFramelessMainWindowCursorReleaseCallback(std::function<void()> callback) {
    g_FramelessMainWindowChromeState.releaseCursorCapture = std::move(callback);
}

void NotifyFramelessMainWindowNativeInteraction() {
    if (g_FramelessMainWindowChromeState.releaseCursorCapture) {
        g_FramelessMainWindowChromeState.releaseCursorCapture();
    }
}

void ApplyFramelessMainWindowStyle(HWND hwnd) {
    TraceMainWindowNativeState(hwnd, "frameless-style-before");

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style |= WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU;
    style &= ~(WS_CAPTION | WS_BORDER | WS_DLGFRAME);
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const bool wasTopMost = (exStyle & WS_EX_TOPMOST) != 0;
    exStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME | WS_EX_TOPMOST);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);

    const COLORREF borderColor = static_cast<COLORREF>(DWMWA_COLOR_NONE);
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
    const MARGINS clientOnlyMargins = { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(hwnd, &clientOnlyMargins);

    SetWindowPos(
        hwnd,
        wasTopMost ? HWND_NOTOPMOST : nullptr,
        0,
        0,
        0,
        0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | (wasTopMost ? 0 : SWP_NOZORDER));

    TraceMainWindowNativeState(hwnd, "frameless-style-after");
}

LRESULT CALLBACK FramelessMainWindowWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCCALCSIZE:
            TraceMainWindowNativeState(hwnd, "message", message, wParam, lParam);
            if (CustomChromeStageHandlesNonClientCalc() &&
                wParam == TRUE &&
                hwnd == g_FramelessMainWindowChromeState.hwnd) {
                return 0;
            }
            break;
        case WM_NCPAINT:
            if (CustomChromeStageHandlesNonClientCalc()) {
                return 0;
            }
            break;
        case WM_NCACTIVATE:
            TraceMainWindowNativeState(hwnd, "message", message, wParam, lParam);
            break;
        case WM_CLOSE:
        case WM_SYSCOMMAND:
            TraceMainWindowNativeState(hwnd, "message", message, wParam, lParam);
            break;
        case WM_NCLBUTTONDOWN:
        case WM_ENTERSIZEMOVE:
        case WM_EXITSIZEMOVE:
        case WM_CAPTURECHANGED:
            TraceMainWindowNativeState(hwnd, "native-interaction", message, wParam, lParam);
            NotifyFramelessMainWindowNativeInteraction();
            break;
        case WM_ACTIVATE:
        case WM_ACTIVATEAPP:
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_SHOWWINDOW:
        case WM_SIZE:
        case WM_WINDOWPOSCHANGING:
        case WM_WINDOWPOSCHANGED:
            TraceMainWindowNativeState(hwnd, "message", message, wParam, lParam);
            break;
        case WM_GETMINMAXINFO: {
            if (!CustomChromeStageUsesFramelessStyle()) {
                break;
            }
            MINMAXINFO* minMax = reinterpret_cast<MINMAXINFO*>(lParam);
            if (minMax) {
                MONITORINFO monitorInfo {};
                monitorInfo.cbSize = sizeof(monitorInfo);
                if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
                    const RECT& workArea = monitorInfo.rcWork;
                    const RECT& monitorArea = monitorInfo.rcMonitor;
                    minMax->ptMaxPosition.x = workArea.left - monitorArea.left;
                    minMax->ptMaxPosition.y = workArea.top - monitorArea.top;
                    minMax->ptMaxSize.x = workArea.right - workArea.left;
                    minMax->ptMaxSize.y = workArea.bottom - workArea.top;
                    minMax->ptMaxTrackSize = minMax->ptMaxSize;
                    return 0;
                }
            }
            break;
        }
        case WM_NCHITTEST: {
            if (!CustomChromeStageHandlesHitTest()) {
                break;
            }
            const LRESULT baseHit = CallFramelessMainWindowBaseProc(hwnd, message, wParam, lParam);
            if (baseHit != HTCLIENT) {
                return baseHit;
            }

            RECT windowRect {};
            if (!GetWindowRect(hwnd, &windowRect)) {
                return baseHit;
            }

            const POINT cursor = {
                static_cast<LONG>(static_cast<short>(LOWORD(lParam))),
                static_cast<LONG>(static_cast<short>(HIWORD(lParam)))
            };

            if (!IsZoomed(hwnd)) {
                const LONG borderX = GetFramelessResizeBorderX();
                const LONG borderY = GetFramelessResizeBorderY();
                const bool left = cursor.x >= windowRect.left && cursor.x < (windowRect.left + borderX);
                const bool right = cursor.x < windowRect.right && cursor.x >= (windowRect.right - borderX);
                const bool top = cursor.y >= windowRect.top && cursor.y < (windowRect.top + borderY);
                const bool bottom = cursor.y < windowRect.bottom && cursor.y >= (windowRect.bottom - borderY);

                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
            }

            if (g_FramelessMainWindowChromeState.captionRectValid &&
                PtInRect(&g_FramelessMainWindowChromeState.captionRect, cursor)) {
                for (const RECT& exclusion : g_FramelessMainWindowChromeState.exclusionRects) {
                    if (PtInRect(&exclusion, cursor)) {
                        return baseHit;
                    }
                }
                return HTCAPTION;
            }
            return baseHit;
        }
        default:
            break;
    }

    return CallFramelessMainWindowBaseProc(hwnd, message, wParam, lParam);
}

void InstallFramelessMainWindowChrome(GLFWwindow* window) {
    if (!window) {
        return;
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) {
        return;
    }

    TraceMainWindowNativeState(hwnd, "frameless-install-before");
    if (CustomChromeStageUsesFramelessStyle()) {
        ApplyFramelessMainWindowStyle(hwnd);
    } else {
        TraceMainWindowNativeState(hwnd, "frameless-style-skipped", 0, 0, 0, CustomChromeExperimentStageName());
    }

    ClearFramelessMainWindowDragZone();
    g_FramelessMainWindowChromeState.hwnd = hwnd;
    g_FramelessMainWindowChromeState.originalWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(FramelessMainWindowWndProc)));
    TraceMainWindowNativeState(hwnd, "frameless-install-after");
}

void UninstallFramelessMainWindowChrome() {
    if (g_FramelessMainWindowChromeState.hwnd && g_FramelessMainWindowChromeState.originalWndProc) {
        TraceMainWindowNativeState(g_FramelessMainWindowChromeState.hwnd, "frameless-uninstall-before");
        SetWindowLongPtrW(
            g_FramelessMainWindowChromeState.hwnd,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(g_FramelessMainWindowChromeState.originalWndProc));
    }
    TraceMainWindowNativeState(g_FramelessMainWindowChromeState.hwnd, "frameless-uninstall-after");
    g_FramelessMainWindowChromeState = {};
}

void BeginNativeWindowMove(GLFWwindow* window) {
    if (!window) {
        return;
    }
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) {
        return;
    }
    ReleaseCapture();
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

void MinimizeNativeWindow(GLFWwindow* window) {
    if (!window) {
        return;
    }
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) {
        return;
    }
    SendMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
}

void ToggleNativeWindowMaximize(GLFWwindow* window) {
    if (!window) {
        return;
    }
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) {
        return;
    }
    const WPARAM command = IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE;
    SendMessageW(hwnd, WM_SYSCOMMAND, command, 0);
}

void ShowMainWindowMaximized(GLFWwindow* window) {
    if (!window) {
        return;
    }
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) {
        glfwMaximizeWindow(window);
        glfwShowWindow(window);
        return;
    }

    NativeWindowTheme::EnsureNotTopMost(hwnd);
    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);
    NativeWindowTheme::EnsureNotTopMost(hwnd);
}

std::string ApplyExperimentalExtendedChromeFrame(GLFWwindow* window) {
    if (!window || !IsExperimentalExtendedChromeEnabled() || UseFramelessMainWindowChrome()) {
        return {};
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) {
        return "hwnd=null";
    }

    MARGINS margins { 0, 0, 1, 0 };
    const HRESULT hr = DwmExtendFrameIntoClientArea(hwnd, &margins);
    std::ostringstream detail;
    detail << "stage=extended-frame margins=0,0,1,0 hr=0x"
           << std::hex << static_cast<unsigned long>(hr) << std::dec;
    return detail.str();
}

void DrawWindowControlGlyph(
    ImDrawList* drawList,
    const ImRect& rect,
    WindowControlKind kind,
    ImU32 color,
    bool maximized) {
    if (!drawList) {
        return;
    }

    const ImVec2 center((rect.Min.x + rect.Max.x) * 0.5f, (rect.Min.y + rect.Max.y) * 0.5f);
    const float half = 6.0f;
    switch (kind) {
        case WindowControlKind::Minimize:
            drawList->AddLine(
                ImVec2(center.x - half, center.y + 3.5f),
                ImVec2(center.x + half, center.y + 3.5f),
                color,
                1.6f);
            break;
        case WindowControlKind::Maximize:
            if (maximized) {
                drawList->AddRect(
                    ImVec2(center.x - half + 1.5f, center.y - half + 2.0f),
                    ImVec2(center.x + half + 1.5f, center.y + half + 2.0f),
                    color,
                    0.0f,
                    0,
                    1.2f);
                drawList->AddRectFilled(
                    ImVec2(center.x - half + 3.0f, center.y - half - 1.0f),
                    ImVec2(center.x + half + 3.0f, center.y - half + 1.5f),
                    IM_COL32(0, 0, 0, 0));
                drawList->AddRect(
                    ImVec2(center.x - half - 1.5f, center.y - half - 2.0f),
                    ImVec2(center.x + half - 1.5f, center.y + half - 2.0f),
                    color,
                    0.0f,
                    0,
                    1.2f);
            } else {
                drawList->AddRect(
                    ImVec2(center.x - half, center.y - half),
                    ImVec2(center.x + half, center.y + half),
                    color,
                    0.0f,
                    0,
                    1.2f);
            }
            break;
        case WindowControlKind::Close:
            drawList->AddLine(
                ImVec2(center.x - half, center.y - half),
                ImVec2(center.x + half, center.y + half),
                color,
                1.5f);
            drawList->AddLine(
                ImVec2(center.x + half, center.y - half),
                ImVec2(center.x - half, center.y + half),
                color,
                1.5f);
            break;
    }
}
#else
void BeginNativeWindowMove(GLFWwindow*) {}
void MinimizeNativeWindow(GLFWwindow* window) {
    if (window) {
        glfwIconifyWindow(window);
    }
}
void ToggleNativeWindowMaximize(GLFWwindow*) {}
void ShowMainWindowMaximized(GLFWwindow* window) {
    if (window) {
        glfwMaximizeWindow(window);
        glfwShowWindow(window);
    }
}
std::string ApplyExperimentalExtendedChromeFrame(GLFWwindow*) { return {}; }
void DrawWindowControlGlyph(
    ImDrawList*,
    const ImRect&,
    WindowControlKind,
    ImU32,
    bool) {}
void UpdateFramelessMainWindowDragZone(const ImVec2&, const ImVec2&, const std::vector<ImRect>&) {}
void ClearFramelessMainWindowDragZone() {}
void InstallFramelessMainWindowChrome(GLFWwindow*) {}
void UninstallFramelessMainWindowChrome() {}
void SetFramelessMainWindowCursorReleaseCallback(std::function<void()>) {}
#endif

unsigned int LoadEmbeddedPngTexture(const unsigned char* data, unsigned int size, const char* debugName) {
    if (!data || size == 0) {
        return 0;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
    if (!pixels) {
        std::cerr << "[AppShell] Failed to decode embedded " << debugName << " icon.\n";
        return 0;
    }

    const unsigned int texture = GLHelpers::CreateTextureFromPixels(pixels, width, height, 4);
    stbi_image_free(pixels);
    return texture;
}

void SetWindowIconFromEmbeddedPng(GLFWwindow* window, const unsigned char* data, unsigned int size) {
    if (!window || !data || size == 0) {
        return;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
    if (!pixels) {
        std::cerr << "[AppShell] Failed to decode embedded window icon.\n";
        return;
    }
    GLFWimage image;
    image.width = width;
    image.height = height;
    image.pixels = pixels;
    glfwSetWindowIcon(window, 1, &image);
    stbi_image_free(pixels);
}

} // namespace

static void glfw_error_callback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << "\n";
}

AppShell* AppShell::s_DetachedPreviewPlatformHookOwner = nullptr;

void AppShell::OnWindowClose(GLFWwindow* window) {
    AppShell* app = static_cast<AppShell*>(glfwGetWindowUserPointer(window));
    if (!app) {
        return;
    }
    glfwSetWindowShouldClose(window, GLFW_FALSE);
    app->RequestMainWindowClose("native-window-close");
}

AppShell::AppShell()
    : m_Window(nullptr)
    , m_SplashWindow(nullptr)
    , m_SplashTexture(0)
    , m_EditorTabTexture(0)
    , m_LibraryTabTexture(0)
    , m_ToolsTabTexture(0)
    , m_RawTabTexture(0)
    , m_HeaderSettingsTexture(0)
    , m_BackgroundImageTexture(0)
    , m_BackgroundImageWidth(0)
    , m_BackgroundImageHeight(0)
    , m_BackgroundImageTextureVisibleAlpha(0.0f)
    , m_LockedScrubCursorActive(false)
    , m_LockedCursorCaptureMode(ImGuiExtras::CursorCaptureMode::None)
    , m_LockedScrubCursorAnchorScreenPos(0.0f, 0.0f)
    , m_LockedScrubCursorRestoreScreenPos(0.0f, 0.0f)
    , m_BackgroundImageTexturePath()
    , m_BackgroundImageTextureRevision(0)
    , m_IsRunning(false)
    , m_FirstTimeLayout(true)
    , m_MainWindowShownTime(0.0)
    , m_AppStartupMotionActive(false)
    , m_AppStartupMotionStartedAt(0.0) {}

AppShell::~AppShell() {
    Shutdown();
}

bool AppShell::Initialize(const std::string& title, int width, int height) {
    glfwSetErrorCallback(glfw_error_callback);
    
    if (!glfwInit())
        return false;

    ApplyBaseOpenGlWindowHints();
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#if defined(_WIN32)
    if (UseFramelessMainWindowChrome()) {
        // Legacy diagnostic path only. On affected systems, GLFW undecorated main
        // windows break native file-dialog z-order even without custom WndProc hooks.
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    }
#endif

    m_Window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    ApplyBaseOpenGlWindowHints();
    if (!m_Window) {
        std::cerr << "Failed to create an OpenGL 4.3 core window/context.\n";
        glfwTerminate();
        return false;
    }
    TraceMainWindowState(UseFramelessMainWindowChrome() ? "main-created-frameless" : "main-created-native-chrome");
    if (IsExperimentalClientChromeEnabled()) {
        TraceMainWindowState("main-experimental-client-chrome-enabled");
    }
    if (IsExperimentalExtendedChromeEnabled()) {
        TraceMainWindowState("main-experimental-extended-chrome-enabled");
    }

    SetWindowIconFromEmbeddedPng(m_Window, EmbeddedTabIcons::ProgramIcon_png_data, EmbeddedTabIcons::ProgramIcon_png_size);

    glfwMakeContextCurrent(m_Window);
    glfwSwapInterval(1); 

    int glMajor = 0;
    int glMinor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &glMajor);
    glGetIntegerv(GL_MINOR_VERSION, &glMinor);
    if (glMajor < 4 || (glMajor == 4 && glMinor < 3)) {
        std::cerr << "Render Phase 2 requires OpenGL 4.3 core. Created context was "
                  << glMajor << "." << glMinor << ".\n";
        glfwDestroyWindow(m_Window);
        glfwTerminate();
        m_Window = nullptr;
        return false;
    }

    if (!LoadGLFunctions()) {
        std::cerr << "Failed to load required OpenGL 4.3 functions.\n";
        glfwDestroyWindow(m_Window);
        glfwTerminate();
        m_Window = nullptr;
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   

    m_Appearance = std::make_unique<StackAppearance::AppearanceManager>();
    const bool loadedAppearance = m_Appearance->Load();
    m_Appearance->SetupFonts(io);
    m_Appearance->ApplyCurrentTheme(io, ImGui::GetStyle());
    {
        const NativeWindowTheme::CaptionThemeResult titleTheme =
            ApplyNativeTitleBarTheme(m_Window, m_Appearance.get());
#if defined(_WIN32)
        const std::string detail = FormatCaptionThemeResult(titleTheme);
        TraceMainWindowState("main-title-theme-applied", detail.c_str());
#else
        TraceMainWindowState("main-title-theme-applied");
#endif
        const std::string extendedDetail = ApplyExperimentalExtendedChromeFrame(m_Window);
        if (!extendedDetail.empty()) {
            TraceMainWindowState("main-experimental-extended-chrome-applied", extendedDetail.c_str());
        }
    }
    m_AppliedAppearanceRevision = m_Appearance->GetRevision();
    m_UpdateManager = std::make_unique<AppUpdate::UpdateManager>(
        [this](UiNotificationSeverity severity, const std::string& message, const std::string& dedupeKey) {
            PushToast(severity, message, dedupeKey);
        },
        [this]() {
            RequestMainWindowClose("update-manager");
        });
    m_UpdateManager->Initialize();

    ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
    ImGui_ImplOpenGL3_Init("#version 430 core");
    InstallDetachedPreviewPlatformHooks();

    m_EditorTabTexture = LoadEmbeddedPngTexture(
        EmbeddedTabIcons::Editor_png_data,
        EmbeddedTabIcons::Editor_png_size,
        "Editor");
    m_LibraryTabTexture = LoadEmbeddedPngTexture(
        EmbeddedTabIcons::Library_png_data,
        EmbeddedTabIcons::Library_png_size,
        "Library");
    m_ToolsTabTexture = LoadEmbeddedPngTexture(
        EmbeddedTabIcons::Tools_png_data,
        EmbeddedTabIcons::Tools_png_size,
        "Tools");
    m_RawTabTexture = LoadEmbeddedPngTexture(
        EmbeddedTabIcons::ToneCurve_png_data,
        EmbeddedTabIcons::ToneCurve_png_size,
        "RAW");
    m_HeaderSettingsTexture = LoadEmbeddedPngTexture(
        EmbeddedTabIcons::Settings_png_data,
        EmbeddedTabIcons::Settings_png_size,
        "HeaderSettings");

    glfwSetWindowUserPointer(m_Window, this);
    glfwSetWindowCloseCallback(m_Window, OnWindowClose);
#if defined(_WIN32)
    if (UseFramelessMainWindowChrome()) {
        if (CustomChromeStageInstallsWndProc()) {
            InstallFramelessMainWindowChrome(m_Window);
            const NativeWindowTheme::CaptionThemeResult titleTheme =
                ApplyNativeTitleBarTheme(m_Window, m_Appearance.get());
            const std::string detail = FormatCaptionThemeResult(titleTheme);
            TraceMainWindowState("main-frameless-title-theme-applied", detail.c_str());
            SetFramelessMainWindowCursorReleaseCallback([this]() {
                ReleaseLockedScrubCursor(false);
            });
            TraceMainWindowState("main-frameless-installed", CustomChromeExperimentStageName());
        } else {
            TraceMainWindowState("main-frameless-decorated-off-only", CustomChromeExperimentStageName());
            TraceMainWindowState("main-frameless-decorated-off-known-bad", "GLFW_DECORATED_false_diagnostic_only");
        }
    } else {
        TraceMainWindowState("main-native-chrome-selected");
    }
#endif
    AppWindowTitleBarBridge::Initialize(m_Window);
    if (AppWindowTitleBarBridge::IsActive()) {
#if defined(_WIN32)
        InstallAppWindowTitlebarNativeInput(m_Window);
#endif
    }
    if (AppWindowTitleBarBridge::RuntimeFlagEnabled()) {
        const AppWindowTitleBarBridge::Metrics& titlebarMetrics = AppWindowTitleBarBridge::GetMetrics();
        TraceMainWindowState(
            titlebarMetrics.active ? "main-appwindow-titlebar-active" : "main-appwindow-titlebar-fallback",
            titlebarMetrics.fallbackReason.c_str());
    }
    FileDialogs::SetOwnerWindow(m_Window, [this]() {
        ReleaseLockedScrubCursor(false);
    });
    glfwSetDropCallback(m_Window, OnFileDrop);

    Async::TaskSystem::Get().Initialize();
    m_Editor.Initialize(m_Window, m_Appearance.get());
    m_Tools.Initialize();
    m_Composite.Initialize();
    // The Library tab populates asynchronously so the main window can appear quickly.

    LibraryManager::Get().RequestRefreshLibraryAsync();

    if (!loadedAppearance) {
        m_Appearance->Save();
    }

    TraceMainWindowState("main-show-maximized-requested");
    ShowMainWindowMaximized(m_Window);
    TraceMainWindowState("main-shown");
    m_MainWindowShownTime = glfwGetTime();
    m_AppStartupMotionActive = true;
    m_AppStartupMotionStartedAt = 0.0;
    m_ChromeHiddenT = 1.0f;
    m_IsRunning = true;
    if (m_UpdateManager) {
        m_UpdateManager->StartBackgroundCheck();
    }
    return true;
}

void AppShell::RequestMainWindowClose(const char* source) {
    if (m_CloseRequested) {
        return;
    }

    m_CloseRequested = true;
    m_ClosingPresentedFrames = 0;
    m_CloseRequestedAt = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    m_CloseSource = (source && source[0] != '\0') ? source : "unknown";
    if (m_Window) {
        glfwSetWindowShouldClose(m_Window, GLFW_FALSE);
    }
    TraceMainWindowState("close-request", m_CloseSource.c_str());
    TraceShutdownPhase("close-request");
    CancelWorkForMainWindowClose();
}

void AppShell::CancelWorkForMainWindowClose() {
    ReleaseLockedScrubCursor(false);
    ResetBackgroundImageDecodeState();
    m_Editor.RequestWorkerShutdownForAppClose();
    m_SettingsPopupOpen = false;
    m_SettingsPopupOpenedAt = 0.0;
    m_ShowEditorSavePrompt = false;
    m_ShowEditorNamePrompt = false;
    m_ActiveToasts.clear();
    m_DetachedPreviewOpeningTopMostHeld = false;
    m_DetachedPreviewOpeningWindow = nullptr;
    m_DetachedPreviewOpeningReleaseAttempts = 0;
    m_Editor.CloseDetachedPreviewFullscreen();
    LibraryManager::Get().CancelProjectPreviewRequests();
    LibraryManager::Get().CancelAssetPreviewRequests();
    LibraryManager::Get().CancelLibraryRefreshRequests();
    Async::TaskSystem::Get().RequestStopDiscardQueued();
}

void AppShell::RenderClosingFrame() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoSavedSettings;

    const double now = ImGui::GetTime();
    const double closeElapsed = m_CloseRequestedAt > 0.0 ? (now - m_CloseRequestedAt) : kClosingSurfaceMinVisibleSeconds;
    const float introEase = TimedEaseOutCubic(closeElapsed, kClosingTextIntroSeconds);
    const float detailEase = TimedEaseOutCubic(closeElapsed - 0.05, kClosingTextIntroSeconds);

    const ImVec4 bg = m_Appearance
        ? m_Appearance->GetClearColor()
        : ImVec4(0.06f, 0.07f, 0.08f, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
    ImGui::Begin("StackClosingSurface", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    const char* title = "Closing Stack...";
    const char* detail = "Finishing background work and releasing windows.";
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    const ImVec2 detailSize = ImGui::CalcTextSize(detail);
    const float spinnerRadius = 13.0f;
    const float blockHeight = spinnerRadius * 2.0f + titleSize.y + detailSize.y + 24.0f;
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const float blockTop = windowPos.y + std::max(0.0f, (windowSize.y - blockHeight) * 0.5f) + (1.0f - introEase) * 10.0f;
    const float centerX = windowPos.x + windowSize.x * 0.5f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImVec4 disabledColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const float spinnerAlpha = 0.64f + 0.36f * std::sin(static_cast<float>(now * 6.0));
    const ImU32 spinnerColor = ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, accent.w * introEase * spinnerAlpha));
    const float spinnerStart = static_cast<float>(now * 4.8);
    const float spinnerEnd = spinnerStart + IM_PI * 1.55f;
    const ImVec2 spinnerCenter(centerX, blockTop + spinnerRadius);
    drawList->PathClear();
    drawList->PathArcTo(spinnerCenter, spinnerRadius, spinnerStart, spinnerEnd, 32);
    drawList->PathStroke(spinnerColor, false, 3.0f);

    const ImVec2 titlePos(centerX - titleSize.x * 0.5f, blockTop + spinnerRadius * 2.0f + 14.0f);
    drawList->AddText(
        titlePos,
        ImGui::ColorConvertFloat4ToU32(ImVec4(textColor.x, textColor.y, textColor.z, textColor.w * introEase)),
        title);

    const ImVec2 detailPos(centerX - detailSize.x * 0.5f, titlePos.y + titleSize.y + 10.0f);
    disabledColor.w *= detailEase;
    drawList->AddText(detailPos, ImGui::ColorConvertFloat4ToU32(disabledColor), detail);

    const float trackWidth = 170.0f;
    const float trackY = detailPos.y + detailSize.y + 18.0f;
    const ImVec2 trackMin(centerX - trackWidth * 0.5f, trackY);
    const ImVec2 trackMax(centerX + trackWidth * 0.5f, trackY + 2.0f);
    ImVec4 trackColor = disabledColor;
    trackColor.w *= 0.38f;
    drawList->AddRectFilled(trackMin, trackMax, ImGui::ColorConvertFloat4ToU32(trackColor), 1.0f);
    const float sweep = std::fmod(static_cast<float>(now * 0.9), 1.0f);
    const float sweepWidth = trackWidth * 0.32f;
    const float sweepStart = trackMin.x + (trackWidth + sweepWidth) * sweep - sweepWidth;
    drawList->AddRectFilled(
        ImVec2(std::max(trackMin.x, sweepStart), trackMin.y),
        ImVec2(std::min(trackMax.x, sweepStart + sweepWidth), trackMax.y),
        ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, accent.w * introEase * 0.88f)),
        1.0f);
    ImGui::End();
}

void AppShell::Run() {
    while (m_Window && m_IsRunning) {
        const auto frameStarted = std::chrono::steady_clock::now();
        glfwPollEvents();
        if (glfwWindowShouldClose(m_Window)) {
            glfwSetWindowShouldClose(m_Window, GLFW_FALSE);
            RequestMainWindowClose("glfw-close-flag");
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuiExtras::BeginFrameInputRouting();

        double pumpMs = 0.0;
        double renderUiMs = 0.0;
        bool renderedClosingFrame = false;
        if (m_CloseRequested) {
            const auto renderUiStarted = std::chrono::steady_clock::now();
            RenderClosingFrame();
            renderUiMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - renderUiStarted).count();
            renderedClosingFrame = true;
        } else {
            const auto pumpStarted = std::chrono::steady_clock::now();
            Async::TaskSystem::Get().PumpMainThreadTasks(4);
            pumpMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pumpStarted).count();

            std::string savedProjectFileName;
            std::string savedProjectKind;
            if (LibraryManager::Get().ConsumeSavedProjectEvent(savedProjectFileName, savedProjectKind)) {
                (void)savedProjectKind;
                (void)savedProjectFileName;
            }
            ConsumeUiNotifications();

            const auto renderUiStarted = std::chrono::steady_clock::now();
            RenderUI();
            renderUiMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - renderUiStarted).count();
        }
        const double secondsSinceMainWindowShown = glfwGetTime() - m_MainWindowShownTime;
        LibraryTextureUploadStats libraryUploadStats;
        if (!m_CloseRequested && m_CurrentTabId == RootTabLibrary && secondsSinceMainWindowShown > 0.35) {
            libraryUploadStats = LibraryManager::Get().UploadLibraryTextures(2.0);
        }
        if (!m_CloseRequested) {
            SyncCursorCaptureRequest();
        }

        const auto drawStarted = std::chrono::steady_clock::now();
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(m_Window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);

        const ImVec4 clearColor = m_Appearance ? m_Appearance->GetClearColor() : ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
        glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            if (!m_CloseRequested) {
                ProcessDetachedPreviewNativeWindow();
            }
            ImGui::RenderPlatformWindowsDefault();
            if (!m_CloseRequested) {
                CompleteDetachedPreviewPlatformPresent();
            }
            glfwMakeContextCurrent(backup_current_context);
        }

        if (renderedClosingFrame) {
            glFlush();
        }
        const auto swapStarted = std::chrono::steady_clock::now();
        glfwSwapBuffers(m_Window);
        const double swapMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - swapStarted).count();
        if (renderedClosingFrame) {
            TraceShutdownPhase("closing-swap", swapMs);
#if defined(_WIN32)
            if (IsShutdownTraceEnabled()) {
                const auto dwmFlushStarted = std::chrono::steady_clock::now();
                const HRESULT flushHr = DwmFlush();
                const double dwmFlushMs =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - dwmFlushStarted).count();
                std::ostringstream detail;
                detail << "hr=0x" << std::hex << static_cast<unsigned long>(flushHr) << std::dec;
                const std::string detailText = detail.str();
                TraceShutdownPhase("closing-dwm-flush", dwmFlushMs, detailText.c_str());
            }
#endif
        }
        const double drawMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - drawStarted).count();
        const double frameMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStarted).count();
        TraceLibraryPerfFrame(
            ImGui::GetFrameCount(),
            secondsSinceMainWindowShown,
            frameMs,
            pumpMs,
            renderUiMs,
            drawMs,
            libraryUploadStats,
            m_Library.GetLastRenderStats());
        TraceDetachedPreviewFrame(frameMs, renderUiMs, drawMs);
        OnFramePresented();
        if (renderedClosingFrame) {
            ++m_ClosingPresentedFrames;
            TraceShutdownPhase("closing-frame-presented", frameMs);
            const double closeElapsed = ImGui::GetTime() - m_CloseRequestedAt;
            const bool minimumCloseAcknowledged =
                m_ClosingPresentedFrames >= kClosingSurfaceMinPresentedFrames &&
                closeElapsed >= kClosingSurfaceMinVisibleSeconds;
            const bool shutdownWorkLooksDrained =
                m_Editor.IsWorkerShutdownReadyForAppClose() &&
                Async::TaskSystem::Get().IsDrainedForShutdown();
            if (minimumCloseAcknowledged &&
                (shutdownWorkLooksDrained || closeElapsed >= kClosingSurfaceMaxDrainSeconds)) {
                m_IsRunning = false;
            }
        }
    }
}

void AppShell::InstallDetachedPreviewPlatformHooks() {
    if (m_DetachedPreviewPlatformHooksInstalled) {
        return;
    }

    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    if (!platformIo.Platform_CreateWindow || !platformIo.Platform_ShowWindow) {
        return;
    }
    m_OriginalPlatformCreateWindow = platformIo.Platform_CreateWindow;
    m_OriginalPlatformShowWindow = platformIo.Platform_ShowWindow;
    platformIo.Platform_CreateWindow = DetachedPreviewPlatformCreateWindowHook;
    platformIo.Platform_ShowWindow = DetachedPreviewPlatformShowWindowHook;
    s_DetachedPreviewPlatformHookOwner = this;
    m_DetachedPreviewPlatformHooksInstalled = true;
}

void AppShell::UninstallDetachedPreviewPlatformHooks() {
    if (!m_DetachedPreviewPlatformHooksInstalled) {
        return;
    }

    if (ImGui::GetCurrentContext()) {
        ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
        if (platformIo.Platform_CreateWindow == DetachedPreviewPlatformCreateWindowHook) {
            platformIo.Platform_CreateWindow = m_OriginalPlatformCreateWindow;
        }
        if (platformIo.Platform_ShowWindow == DetachedPreviewPlatformShowWindowHook) {
            platformIo.Platform_ShowWindow = m_OriginalPlatformShowWindow;
        }
    }

    if (s_DetachedPreviewPlatformHookOwner == this) {
        s_DetachedPreviewPlatformHookOwner = nullptr;
    }
    m_OriginalPlatformCreateWindow = nullptr;
    m_OriginalPlatformShowWindow = nullptr;
    m_DetachedPreviewPlatformHooksInstalled = false;
}

void AppShell::DetachedPreviewPlatformCreateWindowHook(ImGuiViewport* viewport) {
    AppShell* app = s_DetachedPreviewPlatformHookOwner;
    if (!app) {
        return;
    }
    app->HandleDetachedPreviewPlatformCreateWindow(viewport);
}

void AppShell::DetachedPreviewPlatformShowWindowHook(ImGuiViewport* viewport) {
    AppShell* app = s_DetachedPreviewPlatformHookOwner;
    if (!app) {
        return;
    }
    app->HandleDetachedPreviewPlatformShowWindow(viewport);
}

bool AppShell::IsDetachedPreviewViewport(
    const ImGuiViewport* viewport,
    EditorModule::DetachedPreviewNativeWindowRequest* request) const {
    EditorModule::DetachedPreviewNativeWindowRequest localRequest;
    if (!m_Editor.QueryDetachedPreviewNativeWindow(localRequest)) {
        return false;
    }
    if (request) {
        *request = localRequest;
    }
    return viewport != nullptr &&
        localRequest.viewportId != 0 &&
        viewport->ID == localRequest.viewportId;
}

void AppShell::HandleDetachedPreviewPlatformCreateWindow(ImGuiViewport* viewport) {
    EditorModule::DetachedPreviewNativeWindowRequest request;
    const bool detachedPreviewViewport = IsDetachedPreviewViewport(viewport, &request);
    if (m_OriginalPlatformCreateWindow) {
        m_OriginalPlatformCreateWindow(viewport);
    }

    if (!detachedPreviewViewport || !viewport) {
        return;
    }

    request.window = static_cast<GLFWwindow*>(viewport->PlatformHandle);
    request.hasPlatformWindow = request.window != nullptr;
    bool themeApplied = false;
    if (request.hasPlatformWindow) {
        NativeWindowTheme::SetOwner(request.window, m_Window);
        NativeWindowTheme::EnsureNotTopMost(request.window);
        NativeWindowTheme::Apply(request.window, request.surfaceColor, true);
        themeApplied = true;
        request.requestFocus = false;
        m_Editor.CompleteDetachedPreviewNativeWindowRequest(request, true, false);
    }
    TraceDetachedPreviewNativeWindow("platform-create", &request, themeApplied, false, false);
}

void AppShell::HandleDetachedPreviewPlatformShowWindow(ImGuiViewport* viewport) {
    EditorModule::DetachedPreviewNativeWindowRequest request;
    const bool detachedPreviewViewport = IsDetachedPreviewViewport(viewport, &request);
    if (m_OriginalPlatformShowWindow) {
        m_OriginalPlatformShowWindow(viewport);
    }

    if (!detachedPreviewViewport || !viewport) {
        return;
    }

    request.window = static_cast<GLFWwindow*>(viewport->PlatformHandle);
    request.hasPlatformWindow = request.window != nullptr;
    bool focused = false;
    if (request.hasPlatformWindow) {
        NativeWindowTheme::SetOwner(request.window, m_Window);
        NativeWindowTheme::EnsureNotTopMost(request.window);
        focused = NativeWindowTheme::ShowAndFocus(request.window, false);
        m_DetachedPreviewOpeningTopMostHeld = false;
        m_DetachedPreviewOpeningWindow = request.window;
        m_DetachedPreviewOpeningReleaseAttempts = 0;
        m_Editor.MarkDetachedPreviewNativeWindowShown(request, focused);
    }
    TraceDetachedPreviewNativeWindow("platform-show", &request, false, request.hasPlatformWindow, focused);
}

void AppShell::ProcessDetachedPreviewNativeWindow() {
    EditorModule::DetachedPreviewNativeWindowRequest request;
    if (!m_Editor.QueryDetachedPreviewNativeWindow(request)) {
        return;
    }

    bool themeApplied = false;
    bool focusAttempted = false;
    bool focused = false;
    if (request.hasPlatformWindow && request.window != nullptr) {
        NativeWindowTheme::SetOwner(request.window, m_Window);
        NativeWindowTheme::EnsureNotTopMost(request.window);
        if (request.applyTheme) {
            NativeWindowTheme::Apply(request.window, request.surfaceColor, true);
            themeApplied = true;
        }
        if (request.requestFocus) {
            focusAttempted = true;
            focused = NativeWindowTheme::ShowAndFocus(request.window, false);
        } else {
            focused = glfwGetWindowAttrib(request.window, GLFW_FOCUSED) == GLFW_TRUE;
        }
        m_Editor.CompleteDetachedPreviewNativeWindowRequest(request, themeApplied, focused);
    }

    TraceDetachedPreviewNativeWindow(
        "post-platform-update",
        &request,
        themeApplied,
        focusAttempted,
        focused);
}

void AppShell::CompleteDetachedPreviewPlatformPresent() {
    EditorModule::DetachedPreviewNativeWindowRequest request;
    if (!m_Editor.QueryDetachedPreviewNativeWindow(request) ||
        !request.hasPlatformWindow ||
        request.window == nullptr) {
        return;
    }

    const bool wasFirstPresented = request.firstPresented;
    if (!request.firstPresented) {
        m_Editor.MarkDetachedPreviewPlatformPresented(request.window);
    }

    bool focused = NativeWindowTheme::IsFocusedOrForeground(request.window);
    bool releasedTopMost = false;
    if (m_DetachedPreviewOpeningTopMostHeld && request.window == m_DetachedPreviewOpeningWindow) {
        NativeWindowTheme::ReleaseOpeningTopMost(request.window);
        m_DetachedPreviewOpeningTopMostHeld = false;
        m_DetachedPreviewOpeningWindow = nullptr;
        m_DetachedPreviewOpeningReleaseAttempts = 0;
        releasedTopMost = true;
    }

    if (!wasFirstPresented || releasedTopMost || m_DetachedPreviewOpeningTopMostHeld) {
        EditorModule::DetachedPreviewNativeWindowRequest updatedRequest;
        if (m_Editor.QueryDetachedPreviewNativeWindow(updatedRequest)) {
            TraceDetachedPreviewNativeWindow(
                releasedTopMost ? "opening-topmost-release" : "post-platform-present",
                &updatedRequest,
                false,
                m_DetachedPreviewOpeningTopMostHeld,
                focused);
        }
    }
}

void AppShell::TraceDetachedPreviewNativeWindow(
    const char* event,
    const EditorModule::DetachedPreviewNativeWindowRequest* request,
    bool themeApplied,
    bool focusAttempted,
    bool focused) {
    std::string mainEvent = "popout-";
    mainEvent += event ? event : "unknown";
    TraceMainWindowState(mainEvent.c_str());

    if (!IsDetachedPreviewTraceEnabled()) {
        return;
    }

    std::ofstream& stream = DetachedPreviewTraceStream();
    if (!stream.is_open()) {
        return;
    }

    stream << "frame=" << ImGui::GetFrameCount()
           << " event=" << (event ? event : "unknown")
           << " active=" << (m_Editor.IsDetachedPreviewActive() ? 1 : 0);
    if (request) {
        const int visible = request->window ? glfwGetWindowAttrib(request->window, GLFW_VISIBLE) : -1;
        const int nativeFocused = request->window ? glfwGetWindowAttrib(request->window, GLFW_FOCUSED) : -1;
        const int iconified = request->window ? glfwGetWindowAttrib(request->window, GLFW_ICONIFIED) : -1;
        const int foreground = request->window ? (NativeWindowTheme::IsForeground(request->window) ? 1 : 0) : -1;
        const int topMost = request->window ? (NativeWindowTheme::IsTopMost(request->window) ? 1 : 0) : -1;
#if defined(_WIN32)
        const HWND owner = request->window ? NativeWindowTheme::GetOwner(request->window) : nullptr;
#endif
        stream << " viewport=" << request->viewportId
               << " window=" << request->window
#if defined(_WIN32)
               << " ownerHwnd=" << reinterpret_cast<const void*>(owner)
#endif
               << " hasPlatformWindow=" << (request->hasPlatformWindow ? 1 : 0)
               << " nativeShown=" << (request->nativeShown ? 1 : 0)
               << " firstPresented=" << (request->firstPresented ? 1 : 0)
               << " layoutDetached=" << (request->layoutDetached ? 1 : 0)
               << " applyTheme=" << (request->applyTheme ? 1 : 0)
               << " themeApplied=" << (themeApplied ? 1 : 0)
               << " requestFocus=" << (request->requestFocus ? 1 : 0)
               << " focusAttempt=" << request->focusAttempt
               << " focusAttempted=" << (focusAttempted ? 1 : 0)
               << " focusResult=" << (focused ? 1 : 0)
               << " nativeFocused=" << nativeFocused
               << " foreground=" << foreground
               << " topMost=" << topMost
               << " visible=" << visible
               << " iconified=" << iconified
               << " waitFrames=" << request->waitFrames
               << " openingTopMostHeld=" << (m_DetachedPreviewOpeningTopMostHeld ? 1 : 0)
               << " openingReleaseAttempts=" << m_DetachedPreviewOpeningReleaseAttempts;
    }
    stream << '\n';
}

void AppShell::TraceMainWindowState(const char* event) {
#if defined(_WIN32)
    HWND hwnd = m_Window ? glfwGetWin32Window(m_Window) : nullptr;
    TraceMainWindowNativeState(hwnd, event);
#else
    (void)event;
#endif
}

void AppShell::TraceMainWindowState(const char* event, const char* detail) {
#if defined(_WIN32)
    HWND hwnd = m_Window ? glfwGetWin32Window(m_Window) : nullptr;
    TraceMainWindowNativeState(hwnd, event, 0, 0, 0, detail);
#else
    (void)event;
    (void)detail;
#endif
}

void AppShell::TraceShutdownPhase(const char* phase, double elapsedMs, const char* detail) {
    if (!IsShutdownTraceEnabled()) {
        return;
    }

    std::ofstream& stream = ShutdownTraceStream();
    if (!stream.is_open()) {
        return;
    }

    stream << std::fixed << std::setprecision(2)
           << "frame=" << (ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1)
           << " phase=" << (phase ? phase : "unknown")
           << " closeRequested=" << (m_CloseRequested ? 1 : 0)
           << " closingFrames=" << m_ClosingPresentedFrames
           << " closeSource=" << (m_CloseSource.empty() ? "none" : m_CloseSource);
    if (elapsedMs >= 0.0) {
        stream << " elapsedMs=" << elapsedMs;
    }
    if (detail && detail[0] != '\0') {
        stream << " detail=" << detail;
    }
    stream << '\n';
    stream.flush();
}

void AppShell::TraceDetachedPreviewFrame(double frameMs, double renderUiMs, double drawMs) {
    if (!IsDetachedPreviewTraceEnabled() || !m_Editor.IsDetachedPreviewActive()) {
        return;
    }

    std::ofstream& stream = DetachedPreviewTraceStream();
    if (!stream.is_open()) {
        return;
    }

    stream << std::fixed << std::setprecision(2)
           << "frame=" << ImGui::GetFrameCount()
           << " event=frame"
           << " active=" << (m_Editor.IsDetachedPreviewActive() ? 1 : 0)
           << " layoutDetached=" << (m_Editor.IsDetachedPreviewLayoutDetached() ? 1 : 0)
           << " openingTopMostHeld=" << (m_DetachedPreviewOpeningTopMostHeld ? 1 : 0)
           << " frameMs=" << frameMs
           << " renderUiMs=" << renderUiMs
           << " drawMs=" << drawMs
           << '\n';
}

void AppShell::ReleaseLockedScrubCursor(bool restoreCursorPosition) {
    if (!m_Window) {
        return;
    }

    if (!m_LockedScrubCursorActive) {
        return;
    }

    glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    if (restoreCursorPosition) {
        const ImVec2 restoreLocal = ScreenToWindowCursorPos(m_Window, m_LockedScrubCursorRestoreScreenPos);
        glfwSetCursorPos(m_Window, restoreLocal.x, restoreLocal.y);
    }
    m_LockedScrubCursorActive = false;
    m_LockedCursorCaptureMode = ImGuiExtras::CursorCaptureMode::None;
    m_LockedScrubCursorAnchorScreenPos = ImVec2(0.0f, 0.0f);
    m_LockedScrubCursorRestoreScreenPos = ImVec2(0.0f, 0.0f);
}

void AppShell::SyncCursorCaptureRequest() {
    if (!m_Window) {
        return;
    }

    ImGuiExtras::CursorCaptureRequest request {};
    const bool hasRequest = ImGuiExtras::ConsumeCursorCaptureRequest(&request);
    const bool windowFocused = glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) == GLFW_TRUE;
    const bool shouldCapture =
        hasRequest &&
        (request.mode == ImGuiExtras::CursorCaptureMode::LockedScrub ||
         request.mode == ImGuiExtras::CursorCaptureMode::LockedPan) &&
        windowFocused;

    if (!shouldCapture) {
        ReleaseLockedScrubCursor(windowFocused);
        return;
    }

    const bool lockedPan = request.mode == ImGuiExtras::CursorCaptureMode::LockedPan;
    const int glfwCursorMode = lockedPan
        ? GLFW_CURSOR_DISABLED
        : GLFW_CURSOR_HIDDEN;
    const bool captureModeChanged = m_LockedCursorCaptureMode != request.mode;
    if (!m_LockedScrubCursorActive || captureModeChanged) {
        m_LockedScrubCursorActive = true;
        m_LockedCursorCaptureMode = request.mode;
        m_LockedScrubCursorRestoreScreenPos = request.restoreScreenPos;
        glfwSetInputMode(m_Window, GLFW_CURSOR, glfwCursorMode);
    }

    m_LockedScrubCursorAnchorScreenPos = request.anchorScreenPos;
    if (!lockedPan) {
        const ImVec2 anchorLocal = ScreenToWindowCursorPos(m_Window, m_LockedScrubCursorAnchorScreenPos);
        glfwSetCursorPos(m_Window, anchorLocal.x, anchorLocal.y);
    }
}

void AppShell::ReleaseBackgroundImageTexture() {
    if (m_BackgroundImageTexture != 0) {
        glDeleteTextures(1, &m_BackgroundImageTexture);
        m_BackgroundImageTexture = 0;
    }
    m_BackgroundImageWidth = 0;
    m_BackgroundImageHeight = 0;
    m_BackgroundImageTextureVisibleAlpha = 0.0f;
    m_BackgroundImageTexturePath.clear();
}

void AppShell::ResetBackgroundImageDecodeState() {
    ++m_BackgroundImageDecodeGeneration;
    m_BackgroundImageDecodeState = BackgroundImageDecodeState::Idle;
    m_BackgroundImageDecodeRevision = 0;
    m_BackgroundImageDecodePath.clear();
    m_BackgroundImageDecodedPixels.clear();
    m_BackgroundImageDecodedWidth = 0;
    m_BackgroundImageDecodedHeight = 0;
    m_BackgroundImageDecodeError.clear();
}

bool AppShell::LoadBackgroundImageTextureFromPath(const std::filesystem::path& path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    // Keep the decoded texture orientation untouched so the wallpaper draws upright in ImGui.
    unsigned char* pixels = LoadImagePixelsWithExplicitFlip(path, false, &width, &height, &channels);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        return false;
    }

    const unsigned int texture = GLHelpers::CreateTextureFromPixels(pixels, width, height, 4);
    stbi_image_free(pixels);
    if (texture == 0) {
        return false;
    }

    ReleaseBackgroundImageTexture();
    m_BackgroundImageTexture = texture;
    m_BackgroundImageWidth = width;
    m_BackgroundImageHeight = height;
    m_BackgroundImageTextureVisibleAlpha = 0.0f;
    m_BackgroundImageTexturePath = path.lexically_normal().string();
    return true;
}

void AppShell::SyncBackgroundImageTexture() {
    if (m_CloseRequested) {
        ResetBackgroundImageDecodeState();
        return;
    }

    if (m_Appearance == nullptr) {
        ReleaseBackgroundImageTexture();
        ResetBackgroundImageDecodeState();
        m_BackgroundImageTextureRevision = 0;
        return;
    }

    const bool enabled = m_Appearance->GetBackgroundImageEnabled();
    const std::filesystem::path resolvedPath = enabled ? m_Appearance->GetResolvedBackgroundImagePath() : std::filesystem::path();
    const std::string normalizedPath = resolvedPath.empty() ? std::string() : resolvedPath.lexically_normal().string();
    const std::uint64_t revision = m_Appearance->GetBackgroundImageRevision();

    if (!enabled || normalizedPath.empty()) {
        ReleaseBackgroundImageTexture();
        ResetBackgroundImageDecodeState();
        m_BackgroundImageTextureRevision = revision;
        if (!enabled) {
            m_Appearance->SetBackgroundImageRuntimeStatus("");
        }
        return;
    }

    if (m_BackgroundImageTexture != 0 &&
        m_BackgroundImageTexturePath == normalizedPath &&
        m_BackgroundImageTextureRevision == revision) {
        return;
    }

    if (m_BackgroundImageDecodeState == BackgroundImageDecodeState::Ready &&
        m_BackgroundImageDecodePath == normalizedPath &&
        m_BackgroundImageDecodeRevision == revision) {
        if (m_BackgroundImageDecodedPixels.empty() ||
            m_BackgroundImageDecodedWidth <= 0 ||
            m_BackgroundImageDecodedHeight <= 0) {
            ReleaseBackgroundImageTexture();
            m_BackgroundImageTextureRevision = revision;
            m_Appearance->SetBackgroundImageRuntimeStatus("Failed to decode or upload the background image.");
            ResetBackgroundImageDecodeState();
            return;
        }

        const unsigned int texture = GLHelpers::CreateTextureFromPixels(
            m_BackgroundImageDecodedPixels.data(),
            m_BackgroundImageDecodedWidth,
            m_BackgroundImageDecodedHeight,
            4);
        if (texture == 0) {
            ReleaseBackgroundImageTexture();
            m_BackgroundImageTextureRevision = revision;
            m_Appearance->SetBackgroundImageRuntimeStatus("Failed to upload the background image.");
            ResetBackgroundImageDecodeState();
            return;
        }

        ReleaseBackgroundImageTexture();
        m_BackgroundImageTexture = texture;
        m_BackgroundImageWidth = m_BackgroundImageDecodedWidth;
        m_BackgroundImageHeight = m_BackgroundImageDecodedHeight;
        m_BackgroundImageTextureVisibleAlpha = 0.0f;
        m_BackgroundImageTexturePath = normalizedPath;
        m_BackgroundImageTextureRevision = revision;
        m_Appearance->SetBackgroundImageRuntimeStatus("");
        ResetBackgroundImageDecodeState();
        return;
    }

    if (m_BackgroundImageDecodeState == BackgroundImageDecodeState::Failed &&
        m_BackgroundImageDecodePath == normalizedPath &&
        m_BackgroundImageDecodeRevision == revision) {
        ReleaseBackgroundImageTexture();
        m_BackgroundImageTextureRevision = revision;
        m_Appearance->SetBackgroundImageRuntimeStatus(
            m_BackgroundImageDecodeError.empty()
                ? "Failed to decode the background image."
                : m_BackgroundImageDecodeError);
        return;
    }

    if ((m_BackgroundImageDecodeState == BackgroundImageDecodeState::Queued ||
         m_BackgroundImageDecodeState == BackgroundImageDecodeState::Decoding) &&
        m_BackgroundImageDecodePath == normalizedPath &&
        m_BackgroundImageDecodeRevision == revision) {
        return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec) || ec) {
        ReleaseBackgroundImageTexture();
        ResetBackgroundImageDecodeState();
        m_BackgroundImageTextureRevision = revision;
        m_Appearance->SetBackgroundImageRuntimeStatus("Managed background image file is missing.");
        return;
    }

    ++m_BackgroundImageDecodeGeneration;
    const std::uint64_t generation = m_BackgroundImageDecodeGeneration;
    m_BackgroundImageDecodeState = BackgroundImageDecodeState::Queued;
    m_BackgroundImageDecodeRevision = revision;
    m_BackgroundImageDecodePath = normalizedPath;
    m_BackgroundImageDecodedPixels.clear();
    m_BackgroundImageDecodedWidth = 0;
    m_BackgroundImageDecodedHeight = 0;
    m_BackgroundImageDecodeError.clear();
    m_Appearance->SetBackgroundImageRuntimeStatus("Loading background image...");

    Async::TaskSystem::Get().Submit([this, generation, resolvedPath, normalizedPath, revision]() {
        int width = 0;
        int height = 0;
        int channels = 0;
        std::vector<unsigned char> decodedPixels;
        std::string errorMessage;

        unsigned char* pixels = LoadImagePixelsWithExplicitFlip(resolvedPath, false, &width, &height, &channels);
        if (pixels && width > 0 && height > 0) {
            decodedPixels.assign(pixels, pixels + (static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u));
            stbi_image_free(pixels);
        } else {
            if (pixels) {
                stbi_image_free(pixels);
            }
            errorMessage = "Failed to decode the background image.";
        }

        Async::TaskSystem::Get().PostToMain([
            this,
            generation,
            normalizedPath,
            revision,
            decodedPixels = std::move(decodedPixels),
            width,
            height,
            errorMessage = std::move(errorMessage)]() mutable {
            if (generation != m_BackgroundImageDecodeGeneration ||
                normalizedPath != m_BackgroundImageDecodePath ||
                revision != m_BackgroundImageDecodeRevision) {
                return;
            }

            if (decodedPixels.empty() || width <= 0 || height <= 0) {
                m_BackgroundImageDecodeState = BackgroundImageDecodeState::Failed;
                m_BackgroundImageDecodeError = errorMessage.empty()
                    ? "Failed to decode the background image."
                    : std::move(errorMessage);
                return;
            }

            m_BackgroundImageDecodedPixels = std::move(decodedPixels);
            m_BackgroundImageDecodedWidth = width;
            m_BackgroundImageDecodedHeight = height;
            m_BackgroundImageDecodeError.clear();
            m_BackgroundImageDecodeState = BackgroundImageDecodeState::Ready;
        });
    });

    m_BackgroundImageDecodeState = BackgroundImageDecodeState::Decoding;
}

void AppShell::RenderBackgroundImage(const ImVec2& regionMin, const ImVec2& regionSize, float alphaMultiplier) {
    if (m_Appearance == nullptr ||
        !m_Appearance->GetBackgroundImageEnabled() ||
        m_BackgroundImageTexture == 0 ||
        m_BackgroundImageWidth <= 0 ||
        m_BackgroundImageHeight <= 0 ||
        regionSize.x <= 0.0f ||
        regionSize.y <= 0.0f) {
        return;
    }

    const float scale = std::max(
        regionSize.x / static_cast<float>(m_BackgroundImageWidth),
        regionSize.y / static_cast<float>(m_BackgroundImageHeight));
    const ImVec2 drawSize(
        static_cast<float>(m_BackgroundImageWidth) * scale,
        static_cast<float>(m_BackgroundImageHeight) * scale);
    const ImVec2 drawMin(
        regionMin.x + (regionSize.x - drawSize.x) * 0.5f,
        regionMin.y + (regionSize.y - drawSize.y) * 0.5f);
    const ImVec2 drawMax(drawMin.x + drawSize.x, drawMin.y + drawSize.y);
    m_BackgroundImageTextureVisibleAlpha = ImGuiExtras::AnimateTowards(
        m_BackgroundImageTextureVisibleAlpha,
        1.0f,
        ImGui::GetIO().DeltaTime,
        1.65f);
    const float strength = std::clamp(
        m_Appearance->GetBackgroundImageStrength() *
            std::clamp(alphaMultiplier, 0.0f, 1.0f) *
            std::clamp(m_BackgroundImageTextureVisibleAlpha, 0.0f, 1.0f),
        0.0f,
        1.0f);
    if (strength <= 0.001f) {
        return;
    }
    const ImU32 tint = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, strength));

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(regionMin, ImVec2(regionMin.x + regionSize.x, regionMin.y + regionSize.y), true);
    drawList->AddImage(
        (ImTextureID)(intptr_t)m_BackgroundImageTexture,
        drawMin,
        drawMax,
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        tint);
    drawList->PopClipRect();
}

void AppShell::RenderUI() {
    if (m_Appearance) {
        m_Appearance->UpdateThemeTransition(ImGui::GetTime());
    }
    if (m_Appearance && m_AppliedAppearanceRevision != m_Appearance->GetRevision()) {
        m_Appearance->ApplyCurrentTheme(ImGui::GetIO(), ImGui::GetStyle());
        const NativeWindowTheme::CaptionThemeResult titleTheme =
            ApplyNativeTitleBarTheme(m_Window, m_Appearance.get());
#if defined(_WIN32)
        const std::string detail = FormatCaptionThemeResult(titleTheme);
        TraceMainWindowState("main-title-theme-updated", detail.c_str());
#endif
        const std::string extendedDetail = ApplyExperimentalExtendedChromeFrame(m_Window);
        if (!extendedDetail.empty()) {
            TraceMainWindowState("main-experimental-extended-chrome-updated", extendedDetail.c_str());
        }
        m_AppliedAppearanceRevision = m_Appearance->GetRevision();
    }

    SyncBackgroundImageTexture();
    const bool seamlessSurfaces = m_Appearance && m_Appearance->GetSeamlessSurfaceStylingEnabled();
    const StackAppearance::RuntimeSurfacePalette surfacePalette =
        m_Appearance ? m_Appearance->GetRuntimeSurfacePalette() : StackAppearance::RuntimeSurfacePalette{};
    AppWindowTitleBarBridge::UpdateTheme(
        m_Window,
        ImGui::GetStyleColorVec4(ImGuiCol_Text),
        surfacePalette.controlSurfaceHovered,
        surfacePalette.controlSurfaceActive);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    // Root Fullscreen Window for Tabbed Workspace
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar 
                                   | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize 
                                   | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus 
                                   | ImGuiWindowFlags_NoNavFocus;
    if (seamlessSurfaces) {
        window_flags |= ImGuiWindowFlags_NoBackground;
    }
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    
    ImGui::Begin("ModularStudioMain", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiIO& io = ImGui::GetIO();
    const double now = ImGui::GetTime();
    m_ChromeRevealTime = now;
    double startupElapsed = kAppStartupMotionSeconds;
    if (m_AppStartupMotionActive) {
        if (m_AppStartupMotionStartedAt <= 0.0) {
            m_AppStartupMotionStartedAt = now;
        }
        startupElapsed = now - m_AppStartupMotionStartedAt;
        if (startupElapsed >= kAppStartupMotionSeconds) {
            m_AppStartupMotionActive = false;
            startupElapsed = kAppStartupMotionSeconds;
        }
    }
    const float startupEase = TimedEaseOutCubic(startupElapsed, kAppStartupMotionSeconds);
    const float startupBackgroundAlpha = m_AppStartupMotionActive
        ? TimedEaseOutCubic(startupElapsed, kAppStartupBackgroundFadeSeconds)
        : 1.0f;
    const float startupContentAlpha = m_AppStartupMotionActive ? (0.12f + 0.88f * startupEase) : 1.0f;
    const float startupBodyOffsetY = m_AppStartupMotionActive ? (1.0f - startupEase) * 16.0f : 0.0f;
    const float startupOverlayAlpha = m_AppStartupMotionActive ? (1.0f - startupEase) * 0.10f : 0.0f;

    RenderBackgroundImage(ImGui::GetWindowPos(), ImGui::GetWindowSize(), startupBackgroundAlpha);

    const std::vector<RootTabDescriptor> tabs = {
        { RootTabLibrary, "Library", m_LibraryTabTexture, [this]() {
            m_Library.RenderUI(
                &m_Editor,
                &m_Composite,
                m_Appearance.get(),
                &m_RequestedTab,
                RootTabRaw,
                [this](const std::string& projectFileName) {
                    BeginLibraryToEditorProjectLoad(projectFileName);
                });
        } },
        { RootTabRaw, "RAW", m_RawTabTexture, [this]() { m_Editor.RenderRawWorkspaceUI(); } },
        { RootTabEditor, "Editor", m_EditorTabTexture, [this]() { m_Editor.RenderUI(); } },
        { RootTabTools, "Tools", m_ToolsTabTexture, [this]() {
            m_Tools.RenderUI([this](ColorLut::LutPayload payload) {
                if (m_Editor.AddGeneratedLutNodeFromPayload(std::move(payload))) {
                    RequestTabSwitch(RootTabEditor);
                }
            });
        } }
    };

    TickLibraryToEditorProjectLoadTransition();
    const bool loadTransitionActive = m_LoadTransitionPhase != LibraryToEditorProjectLoadPhase::None;

#if defined(_WIN32)
    const int appWindowNativeTabRequest = ConsumeAppWindowTitlebarNativeTabRequest();
    if (appWindowNativeTabRequest != -1) {
        RequestTabSwitch(appWindowNativeTabRequest);
    }
#endif
    if (m_Editor.ConsumeOpenRawWorkspaceTabRequest()) {
        RequestTabSwitch(RootTabRaw);
    }
    if (m_Editor.ConsumeOpenEditorTabRequest()) {
        RequestTabSwitch(RootTabEditor);
    }

    if (!loadTransitionActive && m_RequestedTab != -1 && m_RequestedTab != m_CurrentTabId) {
        if (CanChangeRootTab(m_CurrentTabId, m_RequestedTab)) {
            BeginRootTabBodyFade(m_CurrentTabId, m_RequestedTab);
            OnTabChanged(m_CurrentTabId, m_RequestedTab);
            m_CurrentTabId = m_RequestedTab;
        }
    }
    m_RequestedTab = -1;

    auto blendColor = [](const ImVec4& from, const ImVec4& to, float t) {
        const float clamped = std::clamp(t, 0.0f, 1.0f);
        return ImVec4(
            from.x + (to.x - from.x) * clamped,
            from.y + (to.y - from.y) * clamped,
            from.z + (to.z - from.z) * clamped,
            from.w + (to.w - from.w) * clamped);
    };
    auto withAlpha = [](ImVec4 color, float alpha) {
        color.w = std::clamp(alpha, 0.0f, 1.0f);
        return color;
    };

    m_ChromeHiddenT = std::max(0.0f, m_ChromeHiddenT - io.DeltaTime * 4.0f);

    const bool customChrome = UseFramelessMainWindowChrome();
    const bool customChromeDrag = customChrome && CustomChromeStageHandlesHitTest();
    const bool customChromeWindowControls = customChrome && CustomChromeStageRendersWindowControls();
    const AppWindowTitleBarBridge::Metrics& appWindowTitlebarMetrics = AppWindowTitleBarBridge::GetMetrics();
    const bool appWindowTitlebarActive = AppWindowTitleBarBridge::IsActive() && !customChrome;
    const bool experimentalClientChrome = (IsExperimentalClientChromeEnabled() || appWindowTitlebarActive) && !customChrome;
    bool appWindowTitlebarLeftPressedThisFrame = false;
#if defined(_WIN32)
    static bool s_AppWindowTitlebarLeftButtonDown = false;
    if (appWindowTitlebarActive) {
        const bool leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        appWindowTitlebarLeftPressedThisFrame = leftButtonDown && !s_AppWindowTitlebarLeftButtonDown;
        s_AppWindowTitlebarLeftButtonDown = leftButtonDown;
    } else {
        s_AppWindowTitlebarLeftButtonDown = false;
    }
    if (appWindowTitlebarActive) {
        BeginAppWindowTitlebarNativeInputFrame();
    }
#endif
    std::vector<ImRect> chromeHitExclusionRects;
    auto addChromeHitExclusion = [&](const ImVec2& min, const ImVec2& max) {
        chromeHitExclusionRects.emplace_back(min, max);
    };

    auto renderTabButton = [&](const RootTabDescriptor& tab, bool selected) {
        const ImVec4 button = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        const ImVec4 hovered = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
        const ImVec4 active = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        const ImVec4 header = ImGui::GetStyleColorVec4(ImGuiCol_Header);
        const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
        const ImVec4 baseButton = selected ? blendColor(button, header, 0.55f) : button;
        const ImVec4 hoverButton = blendColor(hovered, accent, selected ? 0.18f : 0.08f);
        const ImVec4 activeButton = blendColor(active, accent, 0.14f);
        const ImVec4 selectedAccent = accent;
        const ImU32 selectedAccentColor = ImGui::GetColorU32(selectedAccent);
        const float tabHeight = 28.0f;

        ImGui::PushID(tab.id);

        bool clicked = false;
        if (tab.iconTexture != 0) {
            constexpr float hitSize = 28.0f;
            constexpr float iconSize = 18.0f;
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const ImRect hitRect(cursor, ImVec2(cursor.x + hitSize, cursor.y + hitSize));
            ImGui::InvisibleButton("##TabIcon", ImVec2(hitSize, hitSize));
            addChromeHitExclusion(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
#if defined(_WIN32)
            if (appWindowTitlebarActive) {
                AddAppWindowTitlebarNativeTabRect(hitRect, tab.id);
            }
#endif
            const bool iconHovered = ImGui::IsItemHovered();
            const bool iconHeld = ImGui::IsItemActive();
            clicked = ImGui::IsItemClicked() || (appWindowTitlebarLeftPressedThisFrame && iconHovered);
            if (experimentalClientChrome && (selected || iconHovered || iconHeld)) {
                const ImVec4 fill = selected || iconHeld
                    ? withAlpha(surfacePalette.controlSurfaceActive, std::max(surfacePalette.controlSurfaceActive.w, 0.70f))
                    : withAlpha(surfacePalette.controlSurfaceHovered, std::max(surfacePalette.controlSurfaceHovered.w, 0.48f));
                ImGui::GetWindowDrawList()->AddRectFilled(hitRect.Min, hitRect.Max, ImGui::GetColorU32(fill), 8.0f);
                if (selected) {
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImVec2(hitRect.Min.x + 7.0f, hitRect.Max.y - 2.0f),
                        ImVec2(hitRect.Max.x - 7.0f, hitRect.Max.y),
                        selectedAccentColor,
                        1.0f);
                }
            }
            const float iconOffset = (hitSize - iconSize) * 0.5f;
            const ImVec2 iconMin(cursor.x + iconOffset, cursor.y + iconOffset);
            const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
            const ImU32 iconTint = StackAppearance::ResolveThemedMonochromeIconTint(
                m_Appearance.get(),
                selected || iconHeld,
                iconHovered);
            ImGui::GetWindowDrawList()->AddImage(
                (ImTextureID)(intptr_t)tab.iconTexture,
                iconMin,
                iconMax,
                ImVec2(0, 0),
                ImVec2(1, 1),
                iconTint);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tab.label);
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, baseButton);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverButton);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeButton);
            const float textWidth = ImGui::CalcTextSize(tab.label).x + 18.0f;
            clicked = ImGui::Button(tab.label, ImVec2(textWidth, tabHeight));
            addChromeHitExclusion(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
#if defined(_WIN32)
            if (appWindowTitlebarActive) {
                AddAppWindowTitlebarNativeTabRect(ImRect(min, max), tab.id);
            }
#endif
            if (selected) {
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(min.x, max.y - 2.0f), ImVec2(max.x, max.y), selectedAccentColor);
            }
            ImGui::PopStyleColor(3);
        }

        ImGui::PopID();
        return clicked;
    };

    float appWindowTitlebarScaleX = 1.0f;
    float appWindowTitlebarScaleY = 1.0f;
    if (appWindowTitlebarActive && m_Window) {
        int windowWidth = 0;
        int windowHeight = 0;
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetWindowSize(m_Window, &windowWidth, &windowHeight);
        glfwGetFramebufferSize(m_Window, &framebufferWidth, &framebufferHeight);
        if (windowWidth > 0 && framebufferWidth > 0) {
            appWindowTitlebarScaleX = static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth);
        }
        if (windowHeight > 0 && framebufferHeight > 0) {
            appWindowTitlebarScaleY = static_cast<float>(framebufferHeight) / static_cast<float>(windowHeight);
        }
    }
    const float appWindowTitlebarHeight = appWindowTitlebarActive
        ? static_cast<float>(std::max(0, appWindowTitlebarMetrics.heightPx)) / std::max(0.001f, appWindowTitlebarScaleY)
        : 0.0f;
    const float chromeHeight = appWindowTitlebarActive
        ? std::max(50.0f, appWindowTitlebarHeight + 10.0f)
        : 50.0f;
    const float chromeOffset = -chromeHeight * (1.0f - std::pow(1.0f - m_ChromeHiddenT, 3.0f));
    const float chromeAlpha = 1.0f - m_ChromeHiddenT;

    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, chromeAlpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImVec4 chromeChildBg = seamlessSurfaces ? ImVec4(0.0f, 0.0f, 0.0f, 0.0f) : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    if (experimentalClientChrome && !seamlessSurfaces) {
        chromeChildBg = blendColor(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg), ImGui::GetStyleColorVec4(ImGuiCol_Header), 0.28f);
    }
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        chromeChildBg);
    ImGui::SetCursorPosY(chromeOffset);
    ImGui::BeginChild("GlobalProgramBar", ImVec2(0.0f, chromeHeight), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (experimentalClientChrome && !seamlessSurfaces) {
        const ImVec2 chromeMin = ImGui::GetWindowPos();
        const ImVec2 chromeMax = ImVec2(chromeMin.x + ImGui::GetWindowSize().x, chromeMin.y + ImGui::GetWindowSize().y);
        const ImU32 separatorColor = ImGui::GetColorU32(withAlpha(
            ImGui::GetStyleColorVec4(ImGuiCol_Separator),
            0.55f));
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(chromeMin.x, chromeMax.y - 0.5f),
            ImVec2(chromeMax.x, chromeMax.y - 0.5f),
            separatorColor,
            1.0f);
    }

    constexpr float chromeButtonHitSize = 28.0f;
    constexpr float chromeButtonGap = 8.0f;
    constexpr float windowControlHitWidth = 36.0f;
    constexpr float nativeCaptionFallbackReserveWidth = 150.0f;
    const float chromeControlTopY = ImGui::GetCursorPosY();

    ImVec2 settingsGearMin(0.0f, 0.0f);
    ImVec2 settingsGearMax(0.0f, 0.0f);
    bool settingsGearHovered = false;

    auto openSettingsPopup = [&]() {
        if (!m_SettingsPopupOpen) {
            m_SettingsPopupOpen = true;
            m_SettingsPopupOpenedAt = ImGui::GetTime();
        }
    };

    auto toggleSettingsPopup = [&]() {
        if (m_SettingsPopupOpen) {
            m_SettingsPopupOpen = false;
            m_SettingsPopupOpenedAt = 0.0;
        } else {
            openSettingsPopup();
        }
    };

    auto renderSettingsGear = [&](const char* id) {
        if (m_HeaderSettingsTexture == 0) {
            return;
        }

        constexpr float iconSize = 18.0f;
        ImGui::SetCursorPosY(chromeControlTopY);

        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        settingsGearMin = cursor;
        settingsGearMax = ImVec2(cursor.x + chromeButtonHitSize, cursor.y + chromeButtonHitSize);
        ImGui::InvisibleButton(id, ImVec2(chromeButtonHitSize, chromeButtonHitSize));
        addChromeHitExclusion(settingsGearMin, settingsGearMax);
#if defined(_WIN32)
        if (appWindowTitlebarActive) {
            SetAppWindowTitlebarNativeSettingsRect(ImRect(settingsGearMin, settingsGearMax));
        }
#endif
        settingsGearHovered = ImGui::IsItemHovered();
        const bool gearHeld = ImGui::IsItemActive();
        const bool gearClicked = ImGui::IsItemClicked() || (appWindowTitlebarLeftPressedThisFrame && settingsGearHovered);

        const ImRect gearRect(settingsGearMin, settingsGearMax);
        const bool gearSelected = m_SettingsPopupOpen;
        ImVec4 bgColor(0.0f, 0.0f, 0.0f, 0.0f);
        if (seamlessSurfaces) {
            bgColor = gearSelected || gearHeld
                ? surfacePalette.controlSurfaceActive
                : (settingsGearHovered ? surfacePalette.controlSurfaceHovered : bgColor);
        } else if (gearSelected || settingsGearHovered || gearHeld) {
            bgColor = gearSelected || gearHeld
                ? blendColor(ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive), 0.55f)
                : blendColor(ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered), 0.35f);
        }
        if (bgColor.w > 0.001f) {
            ImGui::GetWindowDrawList()->AddRectFilled(
                gearRect.Min,
                gearRect.Max,
                ImGui::GetColorU32(bgColor),
                8.0f);
        }

        const float iconOffset = (chromeButtonHitSize - iconSize) * 0.5f;
        const ImVec2 iconMin(cursor.x + iconOffset, cursor.y + iconOffset);
        const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
        const ImU32 iconTint = StackAppearance::ResolveThemedMonochromeIconTint(
            m_Appearance.get(),
            gearSelected || gearHeld,
            settingsGearHovered);
        ImGui::GetWindowDrawList()->AddImage(
            (ImTextureID)(intptr_t)m_HeaderSettingsTexture,
            iconMin,
            iconMax,
            ImVec2(0, 0),
            ImVec2(1, 1),
            iconTint);
        if (settingsGearHovered) {
            ImGui::SetTooltip("Settings");
        }
        if (gearClicked) {
            toggleSettingsPopup();
        }
    };

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 0.0f));
    for (size_t i = 0; i < tabs.size(); ++i) {
        const RootTabDescriptor& tab = tabs[i];
        if (i > 0) {
            ImGui::SameLine();
        }
        if (renderTabButton(tab, m_CurrentTabId == tab.id) &&
            !loadTransitionActive &&
            tab.id != m_CurrentTabId &&
            CanChangeRootTab(m_CurrentTabId, tab.id)) {
            BeginRootTabBodyFade(m_CurrentTabId, tab.id);
            OnTabChanged(m_CurrentTabId, tab.id);
            m_CurrentTabId = tab.id;
        }
    }
    if (appWindowTitlebarActive && m_HeaderSettingsTexture != 0) {
        ImGui::SameLine();
        renderSettingsGear("##HeaderSettingsGearLeft");
    }

    if (m_CurrentTabId == RootTabRaw) {
        std::string rawProgramStatus = m_Editor.GetRawWorkspaceProgramBarStatus();
        if (!rawProgramStatus.empty()) {
            auto clippedText = [](const std::string& text, float maxWidth) {
                if (maxWidth <= 0.0f || ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
                    return text;
                }

                std::string clipped = text;
                constexpr const char* suffix = "...";
                const float suffixWidth = ImGui::CalcTextSize(suffix).x;
                while (!clipped.empty() &&
                    ImGui::CalcTextSize(clipped.c_str()).x + suffixWidth > maxWidth) {
                    clipped.pop_back();
                }
                return clipped.empty() ? std::string(suffix) : clipped + suffix;
            };

            ImGui::SameLine(0.0f, 18.0f);
            const float statusMaxWidth = std::max(
                80.0f,
                ImGui::GetWindowContentRegionMax().x -
                    ImGui::GetCursorPosX() -
                    (customChromeWindowControls ? 170.0f : 70.0f));
            rawProgramStatus = clippedText(rawProgramStatus, statusMaxWidth);
            const ImVec2 statusSize = ImGui::CalcTextSize(rawProgramStatus.c_str());
            ImGui::SetCursorPosY(chromeControlTopY + std::max(0.0f, (chromeButtonHitSize - statusSize.y) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(rawProgramStatus.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::PopStyleVar();

    const float windowControlsWidth = customChromeWindowControls ? (windowControlHitWidth * 3.0f) : 0.0f;
    const float reportedNativeCaptionReservedWidth = appWindowTitlebarActive
        ? static_cast<float>(std::max(0, appWindowTitlebarMetrics.rightInsetPx)) / std::max(0.001f, appWindowTitlebarScaleX)
        : 0.0f;
    const float nativeCaptionReservedWidth = appWindowTitlebarActive
        ? std::max(nativeCaptionFallbackReserveWidth, reportedNativeCaptionReservedWidth)
        : reportedNativeCaptionReservedWidth;
    const float settingsBlockWidth = (m_HeaderSettingsTexture != 0 && !appWindowTitlebarActive) ? chromeButtonHitSize : 0.0f;
    const bool hasRightClusterContent = settingsBlockWidth > 0.0f || customChromeWindowControls;
    float rightClusterWidth = settingsBlockWidth +
        (settingsBlockWidth > 0.0f && windowControlsWidth > 0.0f ? chromeButtonGap : 0.0f) +
        windowControlsWidth;
    if (nativeCaptionReservedWidth > 0.0f) {
        rightClusterWidth += nativeCaptionReservedWidth + (rightClusterWidth > 0.0f ? chromeButtonGap : 0.0f);
    }
    const float rightClusterStartX = std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - rightClusterWidth);

    if (customChromeDrag || hasRightClusterContent) {
        ImGui::SameLine(0.0f, 0.0f);
        const float dragZoneMinX = ImGui::GetCursorPosX() + 8.0f;
        const float dragZoneWidth = std::max(0.0f, rightClusterStartX - dragZoneMinX - 12.0f);
        if (customChromeDrag && dragZoneWidth > 6.0f) {
            ImGui::SetCursorPosX(dragZoneMinX);
            ImGui::SetCursorPosY((chromeHeight - chromeButtonHitSize) * 0.5f);
            ImGui::Dummy(ImVec2(dragZoneWidth, chromeButtonHitSize));
        }

        if (hasRightClusterContent) {
            const float rightContentStartX = std::max(ImGui::GetCursorPosX(), rightClusterStartX);
            if (rightContentStartX > ImGui::GetCursorPosX()) {
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::SetCursorPosX(rightContentStartX);
            }
        }
    }

    auto controlFillColor = [&](bool selected, bool hovered, bool held, bool closeButton) {
        if (seamlessSurfaces) {
            ImVec4 fill = selected || held
                ? surfacePalette.controlSurfaceActive
                : (hovered ? surfacePalette.controlSurfaceHovered : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            if (closeButton && hovered && !selected && !held) {
                fill = blendColor(surfacePalette.controlSurfaceHovered, ImVec4(0.78f, 0.22f, 0.24f, 0.34f), 0.46f);
            }
            return fill;
        }
        return selected
            ? blendColor(ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive), 0.55f)
            : (hovered
                ? blendColor(ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered), 0.35f)
                : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    };

    if (m_HeaderSettingsTexture != 0 && !appWindowTitlebarActive) {
        renderSettingsGear("##HeaderSettingsGear");
    }

    if (customChromeWindowControls) {
        const bool maximized = m_Window && glfwGetWindowAttrib(m_Window, GLFW_MAXIMIZED) == GLFW_TRUE;
        if (m_HeaderSettingsTexture != 0) {
            ImGui::SameLine(0.0f, chromeButtonGap);
        }

        auto renderWindowControl = [&](const char* id, WindowControlKind kind, const char* tooltip) {
            ImGui::SetCursorPosY((chromeHeight - chromeButtonHitSize) * 0.5f);
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const ImRect rect(cursor, ImVec2(cursor.x + windowControlHitWidth, cursor.y + chromeButtonHitSize));
            ImGui::InvisibleButton(id, ImVec2(windowControlHitWidth, chromeButtonHitSize));
            addChromeHitExclusion(rect.Min, rect.Max);
            const bool hovered = ImGui::IsItemHovered();
            const bool held = ImGui::IsItemActive();
            const ImVec4 fill = controlFillColor(false, hovered, held, kind == WindowControlKind::Close);
            if (fill.w > 0.001f) {
                ImGui::GetWindowDrawList()->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(fill), 8.0f);
            }
            const ImU32 glyphColor = StackAppearance::ResolveThemedMonochromeIconTint(
                m_Appearance.get(),
                hovered || held,
                hovered);
            DrawWindowControlGlyph(ImGui::GetWindowDrawList(), rect, kind, glyphColor, maximized);
            if (hovered && tooltip && tooltip[0] != '\0') {
                ImGui::SetTooltip("%s", tooltip);
            }
            return ImGui::IsItemClicked();
        };

        if (renderWindowControl("##WindowMinimize", WindowControlKind::Minimize, "Minimize")) {
            ReleaseLockedScrubCursor(false);
            MinimizeNativeWindow(m_Window);
        }
        ImGui::SameLine(0.0f, 0.0f);
        if (renderWindowControl("##WindowMaximize", WindowControlKind::Maximize, maximized ? "Restore" : "Maximize")) {
            ReleaseLockedScrubCursor(false);
#if defined(_WIN32)
            ToggleNativeWindowMaximize(m_Window);
#endif
        }
        ImGui::SameLine(0.0f, 0.0f);
        if (renderWindowControl("##WindowClose", WindowControlKind::Close, "Close")) {
            RequestMainWindowClose("custom-window-close");
        }
    }
    if (customChromeDrag) {
        const ImVec2 chromeMin = ImGui::GetWindowPos();
        const ImVec2 chromeMax(chromeMin.x + ImGui::GetWindowSize().x, chromeMin.y + chromeHeight);
        UpdateFramelessMainWindowDragZone(chromeMin, chromeMax, chromeHitExclusionRects);
    } else {
        ClearFramelessMainWindowDragZone();
    }
    if (appWindowTitlebarActive) {
        AppWindowTitleBarBridge::SyncPassthroughRegions(m_Window, chromeHitExclusionRects);
    } else {
        AppWindowTitleBarBridge::ClearPassthroughRegions();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

#if defined(_WIN32)
    if (ConsumeAppWindowTitlebarNativeSettingsRequest()) {
        openSettingsPopup();
    }
#endif

    RenderHeaderSettingsPopup(settingsGearMin, settingsGearMax, settingsGearHovered);

    if (!loadTransitionActive) {
        m_Library.RenderGlobalPopups();
        RenderEditorSavePrompts();
    }

    int rootTabBodyRenderTabId = m_CurrentTabId;
    const float rootTabBodyAlpha = loadTransitionActive ? 1.0f : ConsumeRootTabBodyFadeAlpha(&rootTabBodyRenderTabId);

    if (loadTransitionActive) {
        ImGui::Dummy(ImVec2(0.0f, seamlessSurfaces ? 0.0f : 8.0f));
        const ImVec2 bodyPos = ImGui::GetCursorScreenPos();
        const ImVec2 bodySize = ImGui::GetContentRegionAvail();
        (void)bodyPos;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            seamlessSurfaces
                ? ImVec4(0.0f, 0.0f, 0.0f, 0.0f)
                : (m_Appearance ? ImGui::GetStyleColorVec4(ImGuiCol_WindowBg) : ImVec4(0.02f, 0.08f, 0.09f, 1.0f)));
        ImGui::BeginChild("LibraryToEditorProjectLoadTransition", bodySize, false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);

        const double phaseElapsed = ImGui::GetTime() - m_LoadTransitionPhaseStartTime;
        if (m_LoadTransitionPhase == LibraryToEditorProjectLoadPhase::LibraryFadeOut) {
            const float t = ImGuiExtras::EaseOutCubic(std::clamp(static_cast<float>(phaseElapsed / kLibraryLoadFadeOutSeconds), 0.0f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f - t);
            m_Library.RenderUI(
                &m_Editor,
                &m_Composite,
                m_Appearance.get(),
                &m_RequestedTab,
                RootTabRaw,
                [this](const std::string& projectFileName) {
                    BeginLibraryToEditorProjectLoad(projectFileName);
                });
            ImGui::PopStyleVar();
        } else if (m_LoadTransitionPhase == LibraryToEditorProjectLoadPhase::SpinnerFadeIn ||
            m_LoadTransitionPhase == LibraryToEditorProjectLoadPhase::WaitForEditorReady ||
            m_LoadTransitionPhase == LibraryToEditorProjectLoadPhase::SpinnerFadeOut) {
            float spinnerAlpha = 1.0f;
            if (m_LoadTransitionPhase == LibraryToEditorProjectLoadPhase::SpinnerFadeIn) {
                spinnerAlpha = ImGuiExtras::EaseOutCubic(std::clamp(static_cast<float>(phaseElapsed / kLibraryLoadSpinnerFadeInSeconds), 0.0f, 1.0f));
            } else if (m_LoadTransitionPhase == LibraryToEditorProjectLoadPhase::SpinnerFadeOut) {
                spinnerAlpha = 1.0f - ImGuiExtras::EaseOutCubic(std::clamp(static_cast<float>(phaseElapsed / kLibraryLoadSpinnerFadeOutSeconds), 0.0f, 1.0f));
            }
            ImGuiExtras::RenderSpinnerOnlyOverlay(spinnerAlpha);
            std::string spinnerStatus = LibraryManager::Get().GetProjectLoadStatusText();
            if (spinnerStatus.empty()) {
                spinnerStatus = m_Editor.GetDeferredLoadedProjectStatusText();
            }
            if (!spinnerStatus.empty() && spinnerAlpha > 0.01f) {
                const ImVec2 textSize = ImGui::CalcTextSize(spinnerStatus.c_str());
                const ImVec2 windowPos = ImGui::GetWindowPos();
                const ImVec2 windowSize = ImGui::GetWindowSize();
                const ImVec2 textPos(
                    windowPos.x + (windowSize.x - textSize.x) * 0.5f,
                    windowPos.y + windowSize.y * 0.5f + 34.0f);
                ImGui::GetWindowDrawList()->AddText(
                    textPos,
                    IM_COL32(255, 255, 255, static_cast<int>(235.0f * spinnerAlpha)),
                    spinnerStatus.c_str());
            }
        } else if (m_LoadTransitionPhase == LibraryToEditorProjectLoadPhase::EditorReveal) {
            m_Editor.RenderUI();
        }

        RenderLibraryLoadTransitionDiagnostics();

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    } else {
        ImGui::Dummy(ImVec2(0.0f, seamlessSurfaces ? 0.0f : 8.0f));
        for (const RootTabDescriptor& tab : tabs) {
            if (tab.id == rootTabBodyRenderTabId) {
                if (startupBodyOffsetY > 0.001f) {
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + startupBodyOffsetY);
                }
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, rootTabBodyAlpha * startupContentAlpha);
                tab.renderBody();
                ImGui::PopStyleVar();
                break;
            }
        }
    }

    RenderToasts();

    if (startupOverlayAlpha > 0.001f) {
        ImVec4 overlayColor = m_Appearance ? m_Appearance->GetClearColor() : ImVec4(0.06f, 0.07f, 0.08f, 1.0f);
        overlayColor.w = startupOverlayAlpha;
        ImGui::GetWindowDrawList()->AddRectFilled(
            viewport->Pos,
            ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
            ImGui::ColorConvertFloat4ToU32(overlayColor));
    }

    ImGui::End(); // End ModularStudioMain

    if (m_Editor.IsDetachedPreviewActive()) {
        m_Editor.RenderDetachedPreviewWindow();
    }
}

void AppShell::RenderHeaderSettingsPopup(const ImVec2& gearButtonMin, const ImVec2& gearButtonMax, bool gearButtonHovered) {
    if (!m_SettingsPopupOpen || !m_Appearance) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        m_SettingsPopupOpen = false;
        m_SettingsPopupOpenedAt = 0.0;
        return;
    }

    if (m_SettingsPopupOpenedAt <= 0.0) {
        m_SettingsPopupOpenedAt = ImGui::GetTime();
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float popupWidth = std::clamp(viewport->Size.x * 0.66f, 820.0f, 1080.0f);
    const float popupHeight = std::clamp(viewport->Size.y * 0.82f, 620.0f, 940.0f);

    ImVec2 popupPos(
        viewport->Pos.x + (viewport->Size.x - popupWidth) * 0.5f,
        viewport->Pos.y + (viewport->Size.y - popupHeight) * 0.5f
    );
    popupPos.x = std::clamp(
        popupPos.x,
        viewport->Pos.x + 20.0f,
        viewport->Pos.x + viewport->Size.x - popupWidth - 20.0f);
    popupPos.y = std::clamp(
        popupPos.y,
        viewport->Pos.y + 20.0f,
        viewport->Pos.y + viewport->Size.y - popupHeight - 20.0f);

    constexpr double kSettingsPopupFadeInSeconds = 0.15; // Quick fade-in
    const float alpha = std::clamp(
        static_cast<float>((ImGui::GetTime() - m_SettingsPopupOpenedAt) / kSettingsPopupFadeInSeconds),
        0.0f,
        1.0f);
    const float easeAlpha = ImGuiExtras::EaseOutCubic(alpha);

    ImGui::SetNextWindowPos(popupPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(popupWidth, popupHeight), ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, easeAlpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 22.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, m_Appearance->GetEffectivePopupBackgroundColor());
    ImGui::PushStyleColor(ImGuiCol_Border, m_Appearance->GetRuntimeSurfacePalette().border);
    ImGui::PushStyleColor(ImGuiCol_Separator, m_Appearance->GetRuntimeSurfacePalette().separator);

    bool popupOpen = m_SettingsPopupOpen;
    ImGui::Begin(
        "##GlobalHeaderSettingsPopup",
        &popupOpen,
        ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoScrollbar);

    const bool popupHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

    // Make the header area draggable (excluding the Close button area on the right)
    {
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();
        ImVec2 savedCursorPos = ImGui::GetCursorPos();
        
        ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
        ImGui::InvisibleButton("##HeaderDragZone", ImVec2(std::max(10.0f, windowSize.x - 80.0f), 40.0f));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(ImVec2(windowPos.x + delta.x, windowPos.y + delta.y));
        }
        
        ImGui::SetCursorPos(savedCursorPos);
    }

    ImGui::TextUnformatted("Settings");
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - 74.0f));
    if (ImGui::Button("Close", ImVec2(74.0f, 0.0f))) {
        popupOpen = false;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Tune appearance, graph behavior, viewport rendering, and app updates without leaving the workspace.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    AppSettingsPopup::RenderContents(m_Appearance.get(), &m_Editor, m_UpdateManager.get(), m_SettingsPopupState);

    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(4);

    const bool openedThisFrame = (ImGui::GetTime() - m_SettingsPopupOpenedAt) <= static_cast<double>(ImGui::GetIO().DeltaTime) + 0.001;
    const bool outsideClick =
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) &&
        !openedThisFrame &&
        !popupHovered &&
        !gearButtonHovered;
    m_SettingsPopupOpen = popupOpen && !outsideClick;
    if (!m_SettingsPopupOpen) {
        m_SettingsPopupOpenedAt = 0.0;
    }
}

void AppShell::BeginLibraryToEditorProjectLoad(const std::string& projectFileName) {
    if (projectFileName.empty() ||
        m_LoadTransitionPhase != LibraryToEditorProjectLoadPhase::None ||
        Async::IsBusy(LibraryManager::Get().GetProjectLoadTaskState())) {
        return;
    }

    m_RootTabBodyFadeActive = false;
    m_RootTabBodyFadeStartedAt = 0.0;
    m_RootTabBodyFadeFromTab = -1;
    m_RootTabBodyFadeToTab = -1;
    m_LoadTransitionProjectFileName = projectFileName;
    m_LoadTransitionDecodeReady = false;
    m_LoadTransitionDecodeSucceeded = false;
    m_LoadTransitionApplySucceeded = false;
    m_LoadTransitionDismissLibraryPreviewsPending = true;
    m_LoadTransitionLoadRequested = false;
    m_LoadTransitionFirstRenderReady = false;
    m_LoadTransitionNodeBrowserThumbnailsReady = false;
    m_LoadTransitionStartedAt = ImGui::GetTime();
    m_LoadTransitionDecodeRequestedAt = 0.0;
    m_LoadTransitionDecodeReadyAt = 0.0;
    m_LoadTransitionApplyStartedAt = 0.0;
    m_LoadTransitionApplyFinishedAt = 0.0;
    m_LoadTransitionFirstRenderReadyAt = 0.0;
    m_LoadTransitionThumbnailsReadyAt = 0.0;
    m_LoadTransitionReadyToRevealAt = 0.0;
    m_LoadTransitionDecodedProject.reset();
    m_LoadTransitionTrace.clear();
    TraceLibraryLoadTransition("load clicked");
    SetLibraryToEditorProjectLoadPhase(LibraryToEditorProjectLoadPhase::LibraryFadeOut);
}

void AppShell::RequestDeferredLibraryProjectLoad() {
    if (m_LoadTransitionLoadRequested || m_LoadTransitionProjectFileName.empty()) {
        return;
    }

    m_LoadTransitionLoadRequested = true;
    m_LoadTransitionDecodeRequestedAt = ImGui::GetTime();
    TraceLibraryLoadTransition("decode requested");
    LibraryManager::Get().SetProjectLoadApplyingStatus("Loading project data...");
    LibraryManager::Get().RequestLoadProjectDeferredApply(
        m_LoadTransitionProjectFileName,
        [this](bool success, std::shared_ptr<EditorLoadedProjectData> decodedProject) {
            m_LoadTransitionDecodeReady = true;
            m_LoadTransitionDecodeSucceeded = success;
            m_LoadTransitionDecodedProject = std::move(decodedProject);
            m_LoadTransitionDecodeReadyAt = ImGui::GetTime();
            TraceLibraryLoadTransition(success ? "decode ready" : "decode failed");
        });
}

void AppShell::SetLibraryToEditorProjectLoadPhase(LibraryToEditorProjectLoadPhase phase) {
    if (m_LoadTransitionPhase == phase && phase != LibraryToEditorProjectLoadPhase::None) {
        return;
    }

    auto phaseName = [](LibraryToEditorProjectLoadPhase value) {
        switch (value) {
        case LibraryToEditorProjectLoadPhase::LibraryFadeOut: return "LibraryFadeOut";
        case LibraryToEditorProjectLoadPhase::SpinnerFadeIn: return "SpinnerFadeIn";
        case LibraryToEditorProjectLoadPhase::WaitForEditorReady: return "WaitForEditorReady";
        case LibraryToEditorProjectLoadPhase::SpinnerFadeOut: return "SpinnerFadeOut";
        case LibraryToEditorProjectLoadPhase::EditorReveal: return "EditorReveal";
        case LibraryToEditorProjectLoadPhase::None: default: return "None";
        }
    };

    m_LoadTransitionPhase = phase;
    m_LoadTransitionPhaseStartTime = ImGui::GetTime();
    m_LoadTransitionPhasePresentedFrames = 0;
    if (phase == LibraryToEditorProjectLoadPhase::SpinnerFadeIn) {
        m_LoadTransitionSpinnerStartTime = m_LoadTransitionPhaseStartTime;
    }

    std::string event = "phase ";
    event += phaseName(phase);
    TraceLibraryLoadTransition(event);
}

void AppShell::TickLibraryToEditorProjectLoadTransition() {
    if (m_LoadTransitionPhase == LibraryToEditorProjectLoadPhase::None) {
        return;
    }

    m_Editor.PumpNonRenderingWork(2.5);

    const double now = ImGui::GetTime();
    const double elapsed = now - m_LoadTransitionPhaseStartTime;
    auto finishToLibrary = [&]() {
        SetLibraryToEditorProjectLoadPhase(LibraryToEditorProjectLoadPhase::None);
        m_LoadTransitionProjectFileName.clear();
        m_LoadTransitionDecodedProject.reset();
        m_LoadTransitionDecodeReady = false;
        m_LoadTransitionDecodeSucceeded = false;
        m_LoadTransitionApplySucceeded = false;
        m_LoadTransitionDismissLibraryPreviewsPending = false;
        m_LoadTransitionLoadRequested = false;
        m_LoadTransitionFirstRenderReady = false;
        m_LoadTransitionNodeBrowserThumbnailsReady = false;
        m_CurrentTabId = RootTabLibrary;
    };

    if (m_LoadTransitionDismissLibraryPreviewsPending &&
        m_LoadTransitionPhase != LibraryToEditorProjectLoadPhase::LibraryFadeOut) {
        m_Library.DismissPreviewsForProjectLoad();
        m_LoadTransitionDismissLibraryPreviewsPending = false;
    }

    switch (m_LoadTransitionPhase) {
    case LibraryToEditorProjectLoadPhase::LibraryFadeOut:
        if (elapsed >= kLibraryLoadFadeOutSeconds) {
            SetLibraryToEditorProjectLoadPhase(LibraryToEditorProjectLoadPhase::SpinnerFadeIn);
        }
        break;
    case LibraryToEditorProjectLoadPhase::SpinnerFadeIn: {
        const bool minimumSpinnerShown = m_LoadTransitionSpinnerStartTime > 0.0 &&
            (now - m_LoadTransitionSpinnerStartTime) >= kLibraryLoadSpinnerMinVisibleSeconds;
        if (m_LoadTransitionDecodeReady && !m_LoadTransitionDecodeSucceeded && elapsed >= kLibraryLoadSpinnerFadeInSeconds && minimumSpinnerShown) {
            SetLibraryToEditorProjectLoadPhase(LibraryToEditorProjectLoadPhase::SpinnerFadeOut);
        } else if (m_LoadTransitionDecodeReady && elapsed >= kLibraryLoadSpinnerFadeInSeconds && minimumSpinnerShown) {
            m_LoadTransitionApplyStartedAt = ImGui::GetTime();
            TraceLibraryLoadTransition("apply start");
            if (!m_Editor.BeginDeferredLoadedProjectApply(m_LoadTransitionDecodedProject)) {
                m_LoadTransitionApplySucceeded = false;
                m_LoadTransitionApplyFinishedAt = ImGui::GetTime();
                TraceLibraryLoadTransition("apply start failed");
                LibraryManager::Get().FinishDeferredProjectLoad(
                    false,
                    m_Editor.GetDeferredLoadedProjectStatusText().empty()
                        ? "Failed to apply the loaded project."
                        : m_Editor.GetDeferredLoadedProjectStatusText());
                SetLibraryToEditorProjectLoadPhase(LibraryToEditorProjectLoadPhase::SpinnerFadeOut);
                break;
            }
            LibraryManager::Get().SetProjectLoadApplyingStatus("Applying editor state...");
            SetLibraryToEditorProjectLoadPhase(LibraryToEditorProjectLoadPhase::WaitForEditorReady);
        }
        break;
    }
    case LibraryToEditorProjectLoadPhase::WaitForEditorReady: {
        if (m_LoadTransitionApplyFinishedAt <= 0.0 && m_Editor.HasDeferredLoadedProjectApplyCoreFinished()) {
            m_LoadTransitionApplyFinishedAt = now;
            TraceLibraryLoadTransition("apply state finished");
        }

        const bool firstRenderReady = m_Editor.HasDeferredLoadedProjectFirstRenderReady();
        if (!m_LoadTransitionFirstRenderReady && firstRenderReady) {
            m_LoadTransitionFirstRenderReady = true;
            m_LoadTransitionFirstRenderReadyAt = now;
            TraceLibraryLoadTransition("first render ready");
        }

        const bool thumbnailsReady =
            m_Editor.HasDeferredLoadedProjectApplyCoreFinished() &&
            m_Editor.GetPendingNodeBrowserThumbnailWarmCount() == 0 &&
            m_Editor.GetPendingNodeBrowserThumbnailGenerationCount() == 0;
        if (!m_LoadTransitionNodeBrowserThumbnailsReady && thumbnailsReady) {
            m_LoadTransitionNodeBrowserThumbnailsReady = true;
            m_LoadTransitionThumbnailsReadyAt = now;
            TraceLibraryLoadTransition("node browser thumbnails ready");
        }

        if (m_Editor.HasDeferredLoadedProjectApplyFailed()) {
            m_LoadTransitionApplySucceeded = false;
            if (m_LoadTransitionApplyFinishedAt <= 0.0) {
                m_LoadTransitionApplyFinishedAt = ImGui::GetTime();
            }
            TraceLibraryLoadTransition("apply end failed");
            LibraryManager::Get().FinishDeferredProjectLoad(
                false,
                m_Editor.GetDeferredLoadedProjectStatusText().empty()
                    ? "Failed to apply the loaded project."
                    : m_Editor.GetDeferredLoadedProjectStatusText());
            SetLibraryToEditorProjectLoadPhase(LibraryToEditorProjectLoadPhase::SpinnerFadeOut);
            break;
        }

        const std::string statusText = m_Editor.GetDeferredLoadedProjectStatusText();
        if (!statusText.empty()) {
            LibraryManager::Get().SetProjectLoadApplyingStatus(statusText);
        }

        const bool readyForReveal =
            m_Editor.HasDeferredLoadedProjectApplyCoreFinished() &&
            firstRenderReady;
        if (readyForReveal) {
            m_LoadTransitionApplySucceeded = true;
            if (m_LoadTransitionApplyFinishedAt <= 0.0) {
                m_LoadTransitionApplyFinishedAt = ImGui::GetTime();
            }
            m_LoadTransitionReadyToRevealAt = ImGui::GetTime();
            TraceLibraryLoadTransition("apply end ok");
            TraceLibraryLoadTransition("ready to reveal");
            LibraryManager::Get().FinishDeferredProjectLoad(true, "Project loaded into the editor.");
            SetLibraryToEditorProjectLoadPhase(LibraryToEditorProjectLoadPhase::SpinnerFadeOut);
        }
        break;
    }
    case LibraryToEditorProjectLoadPhase::SpinnerFadeOut:
        if (elapsed >= kLibraryLoadSpinnerFadeOutSeconds) {
            if (m_LoadTransitionApplySucceeded) {
                OnTabChanged(m_CurrentTabId, RootTabEditor);
                m_CurrentTabId = RootTabEditor;
                m_Editor.BeginLibraryLoadReveal();
                TraceLibraryLoadTransition("editor reveal start");
                SetLibraryToEditorProjectLoadPhase(LibraryToEditorProjectLoadPhase::EditorReveal);
            } else {
                finishToLibrary();
            }
        }
        break;
    case LibraryToEditorProjectLoadPhase::EditorReveal:
        if (elapsed >= kLibraryLoadEditorRevealSeconds) {
            SetLibraryToEditorProjectLoadPhase(LibraryToEditorProjectLoadPhase::None);
            m_LoadTransitionProjectFileName.clear();
            m_LoadTransitionDecodeReady = false;
            m_LoadTransitionDecodeSucceeded = false;
            m_LoadTransitionApplySucceeded = false;
            m_LoadTransitionDismissLibraryPreviewsPending = false;
            m_LoadTransitionLoadRequested = false;
            m_LoadTransitionDecodedProject.reset();
            m_LoadTransitionFirstRenderReady = false;
            m_LoadTransitionNodeBrowserThumbnailsReady = false;
        }
        break;
    case LibraryToEditorProjectLoadPhase::None:
        break;
    }
}

void AppShell::OnFramePresented() {
    if (m_LoadTransitionPhase == LibraryToEditorProjectLoadPhase::None) {
        return;
    }

    auto phaseName = [](LibraryToEditorProjectLoadPhase value) {
        switch (value) {
        case LibraryToEditorProjectLoadPhase::LibraryFadeOut: return "LibraryFadeOut";
        case LibraryToEditorProjectLoadPhase::SpinnerFadeIn: return "SpinnerFadeIn";
        case LibraryToEditorProjectLoadPhase::WaitForEditorReady: return "WaitForEditorReady";
        case LibraryToEditorProjectLoadPhase::SpinnerFadeOut: return "SpinnerFadeOut";
        case LibraryToEditorProjectLoadPhase::EditorReveal: return "EditorReveal";
        case LibraryToEditorProjectLoadPhase::None: default: return "None";
        }
    };

    ++m_LoadTransitionPhasePresentedFrames;
    if (m_LoadTransitionPhasePresentedFrames == 1) {
        std::string event = "first presented ";
        event += phaseName(m_LoadTransitionPhase);
        TraceLibraryLoadTransition(event);
    }

    if (m_LoadTransitionPhase == LibraryToEditorProjectLoadPhase::SpinnerFadeIn &&
        m_LoadTransitionPhasePresentedFrames >= 1 &&
        !m_LoadTransitionLoadRequested) {
        RequestDeferredLibraryProjectLoad();
    }
}

void AppShell::RenderLibraryLoadTransitionDiagnostics() {
    if (!kLibraryLoadTransitionDiagnostics ||
        m_LoadTransitionPhase == LibraryToEditorProjectLoadPhase::None) {
        return;
    }

    auto phaseName = [](LibraryToEditorProjectLoadPhase value) {
        switch (value) {
        case LibraryToEditorProjectLoadPhase::LibraryFadeOut: return "LibraryFadeOut";
        case LibraryToEditorProjectLoadPhase::SpinnerFadeIn: return "SpinnerFadeIn";
        case LibraryToEditorProjectLoadPhase::WaitForEditorReady: return "WaitForEditorReady";
        case LibraryToEditorProjectLoadPhase::SpinnerFadeOut: return "SpinnerFadeOut";
        case LibraryToEditorProjectLoadPhase::EditorReveal: return "EditorReveal";
        case LibraryToEditorProjectLoadPhase::None: default: return "None";
        }
    };

    const double now = ImGui::GetTime();
    const double elapsed = m_LoadTransitionStartedAt > 0.0 ? now - m_LoadTransitionStartedAt : 0.0;
    const double decodeMs = (m_LoadTransitionDecodeRequestedAt > 0.0 && m_LoadTransitionDecodeReadyAt > 0.0)
        ? (m_LoadTransitionDecodeReadyAt - m_LoadTransitionDecodeRequestedAt) * 1000.0
        : 0.0;
    const double applyMs = (m_LoadTransitionApplyStartedAt > 0.0 && m_LoadTransitionApplyFinishedAt > 0.0)
        ? (m_LoadTransitionApplyFinishedAt - m_LoadTransitionApplyStartedAt) * 1000.0
        : 0.0;
    const double firstRenderMs = (m_LoadTransitionApplyStartedAt > 0.0 && m_LoadTransitionFirstRenderReadyAt > 0.0)
        ? (m_LoadTransitionFirstRenderReadyAt - m_LoadTransitionApplyStartedAt) * 1000.0
        : 0.0;
    const double thumbnailReadyMs = (m_LoadTransitionApplyStartedAt > 0.0 && m_LoadTransitionThumbnailsReadyAt > 0.0)
        ? (m_LoadTransitionThumbnailsReadyAt - m_LoadTransitionApplyStartedAt) * 1000.0
        : 0.0;
    const double revealReadyMs = (m_LoadTransitionStartedAt > 0.0 && m_LoadTransitionReadyToRevealAt > 0.0)
        ? (m_LoadTransitionReadyToRevealAt - m_LoadTransitionStartedAt) * 1000.0
        : 0.0;

    const ImVec2 basePos = ImGui::GetWindowPos();
    ImGui::SetCursorScreenPos(ImVec2(basePos.x + 12.0f, basePos.y + 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(8, 28, 34, 210));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(112, 190, 176, 110));
    ImGui::BeginChild("LibraryLoadTransitionDiagnostics", ImVec2(380.0f, 232.0f), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
    ImGui::Text("Library -> Editor load");
    ImGui::Text("Phase: %s  frames: %d", phaseName(m_LoadTransitionPhase), m_LoadTransitionPhasePresentedFrames);
    ImGui::Text("Editor phase: %s", m_Editor.GetDeferredLoadedProjectPhaseLabel());
    ImGui::Text("Elapsed: %.2fs", elapsed);
    ImGui::Text("Decode: requested=%s ready=%s ok=%s %.1fms",
        m_LoadTransitionLoadRequested ? "yes" : "no",
        m_LoadTransitionDecodeReady ? "yes" : "no",
        m_LoadTransitionDecodeSucceeded ? "yes" : "no",
        decodeMs);
    ImGui::Text("Apply: started=%s ok=%s %.1fms",
        m_LoadTransitionApplyStartedAt > 0.0 ? "yes" : "no",
        m_LoadTransitionApplySucceeded ? "yes" : "no",
        applyMs);
    ImGui::Text("First render: ready=%s %.1fms", m_LoadTransitionFirstRenderReady ? "yes" : "no", firstRenderMs);
    ImGui::Text(
        "Thumbs: warm=%zu pending=%zu ready=%s %.1fms",
        m_Editor.GetPendingNodeBrowserThumbnailWarmCount(),
        m_Editor.GetPendingNodeBrowserThumbnailGenerationCount(),
        m_LoadTransitionNodeBrowserThumbnailsReady ? "yes" : "no",
        thumbnailReadyMs);
    ImGui::Text("Ready to reveal: %s %.1fms",
        m_LoadTransitionReadyToRevealAt > 0.0 ? "yes" : "no",
        revealReadyMs);
    if (!LibraryManager::Get().GetProjectLoadStatusText().empty()) {
        ImGui::TextWrapped("Status: %s", LibraryManager::Get().GetProjectLoadStatusText().c_str());
    }
    ImGui::Separator();
    const int firstTrace = std::max(0, static_cast<int>(m_LoadTransitionTrace.size()) - 5);
    for (int i = firstTrace; i < static_cast<int>(m_LoadTransitionTrace.size()); ++i) {
        ImGui::TextDisabled("%s", m_LoadTransitionTrace[static_cast<std::size_t>(i)].c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void AppShell::TraceLibraryLoadTransition(const std::string& event) {
    if (!kLibraryLoadTransitionDiagnostics) {
        return;
    }

    const double now = ImGui::GetTime();
    const double elapsed = m_LoadTransitionStartedAt > 0.0 ? now - m_LoadTransitionStartedAt : 0.0;
    char buffer[256] = {};
    std::snprintf(buffer, sizeof(buffer), "%.3fs  %s", elapsed, event.c_str());
    m_LoadTransitionTrace.emplace_back(buffer);
    if (m_LoadTransitionTrace.size() > 12) {
        m_LoadTransitionTrace.erase(m_LoadTransitionTrace.begin());
    }
}

void AppShell::ConsumeUiNotifications() {
    UiNotificationEvent event;
    while (LibraryManager::Get().ConsumeUiNotification(event)) {
        PushToast(event.severity, event.message, event.dedupeKey);
    }
    while (m_Editor.ConsumeUiNotification(event)) {
        PushToast(event.severity, event.message, event.dedupeKey);
    }
}

void AppShell::PushToast(UiNotificationSeverity severity, const std::string& message, const std::string& dedupeKey) {
    if (message.empty()) {
        return;
    }

    const double now = ImGui::GetTime();
    if (!dedupeKey.empty()) {
        for (ActiveToast& toast : m_ActiveToasts) {
            if (toast.dedupeKey == dedupeKey && toast.message == message) {
                toast.severity = severity;
                toast.startTime = now;
                toast.duration = 3.4;
                return;
            }
        }
    }

    m_ActiveToasts.push_back(ActiveToast{
        severity,
        message,
        dedupeKey,
        now,
        3.4
    });

    constexpr std::size_t kMaxToasts = 4;
    if (m_ActiveToasts.size() > kMaxToasts) {
        m_ActiveToasts.erase(m_ActiveToasts.begin(), m_ActiveToasts.begin() + static_cast<std::ptrdiff_t>(m_ActiveToasts.size() - kMaxToasts));
    }
}

void AppShell::RenderToasts() {
    if (m_ActiveToasts.empty()) {
        return;
    }

    const double now = ImGui::GetTime();
    m_ActiveToasts.erase(
        std::remove_if(
            m_ActiveToasts.begin(),
            m_ActiveToasts.end(),
            [now](const ActiveToast& toast) { return (now - toast.startTime) >= toast.duration; }),
        m_ActiveToasts.end());

    if (m_ActiveToasts.empty()) {
        return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float centerX = viewport->Pos.x + viewport->Size.x * 0.5f;
    const float topY = viewport->Pos.y + 24.0f;

    constexpr float kSlideDuration = 0.24f;
    constexpr float kFadeOutDuration = 0.28f;
    constexpr float kToastHeight = 42.0f;
    constexpr float kToastSpacing = 10.0f;
    constexpr float kToastMaxWidth = 520.0f;
    constexpr float kToastMinWidth = 220.0f;
    constexpr float kHorizontalPadding = 20.0f;
    constexpr float kRounding = 21.0f;

    for (std::size_t index = 0; index < m_ActiveToasts.size(); ++index) {
        const ActiveToast& toast = m_ActiveToasts[index];
        const float elapsed = static_cast<float>(now - toast.startTime);
        const float appearT = std::clamp(elapsed / kSlideDuration, 0.0f, 1.0f);
        const float appearEase = 1.0f - std::pow(1.0f - appearT, 3.0f);
        const float fadeOutStart = static_cast<float>(toast.duration) - kFadeOutDuration;
        const float fadeOutT = (elapsed <= fadeOutStart)
            ? 0.0f
            : std::clamp((elapsed - fadeOutStart) / kFadeOutDuration, 0.0f, 1.0f);
        const float alpha = appearEase * (1.0f - fadeOutT);
        if (alpha <= 0.001f) {
            continue;
        }

        const ImVec2 textSize = ImGui::CalcTextSize(toast.message.c_str());
        const float toastWidth = std::clamp(textSize.x + (kHorizontalPadding * 2.0f), kToastMinWidth, kToastMaxWidth);
        const float targetY = topY + static_cast<float>(index) * (kToastHeight + kToastSpacing);
        const float y = targetY - ((1.0f - appearEase) * 22.0f);
        const ImVec2 min(centerX - toastWidth * 0.5f, y);
        const ImVec2 max(centerX + toastWidth * 0.5f, y + kToastHeight);

        ImU32 fill = IM_COL32(30, 36, 44, static_cast<int>(220.0f * alpha));
        ImU32 border = IM_COL32(92, 136, 182, static_cast<int>(210.0f * alpha));
        ImU32 textColor = IM_COL32(245, 248, 252, static_cast<int>(255.0f * alpha));
        if (toast.severity == UiNotificationSeverity::Success) {
            fill = IM_COL32(26, 52, 38, static_cast<int>(228.0f * alpha));
            border = IM_COL32(115, 205, 158, static_cast<int>(215.0f * alpha));
        } else if (toast.severity == UiNotificationSeverity::Error) {
            fill = IM_COL32(72, 28, 34, static_cast<int>(228.0f * alpha));
            border = IM_COL32(238, 126, 141, static_cast<int>(220.0f * alpha));
        }

        drawList->AddRectFilled(min, max, fill, kRounding);
        drawList->AddRect(min, max, border, kRounding, 0, 1.2f);
        drawList->AddText(
            ImVec2(centerX - textSize.x * 0.5f, y + (kToastHeight - textSize.y) * 0.5f),
            textColor,
            toast.message.c_str());
    }
}

void AppShell::RenderEditorSavePrompts() {
    if (m_ShowEditorSavePrompt) {
        ImGui::OpenPopup("Editor Project Already Open##AppShell");
        m_ShowEditorSavePrompt = false;
    }

    if (m_ShowEditorNamePrompt) {
        ImGui::OpenPopup("Save New Editor Project##AppShell");
        m_ShowEditorNamePrompt = false;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Editor Project Already Open##AppShell", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("There is already a project open in the Editor tab. Would you like to save it before loading the new project?");
        ImGui::Spacing();

        if (ImGui::Button("Save & Continue", ImVec2(140.0f, 0.0f))) {
            if (m_Editor.GetCurrentProjectFileName().empty()) {
                m_ShowEditorNamePrompt = true;
            } else {
                m_Editor.RequestSaveCurrentProject(m_Editor.GetCurrentProjectName());
                RequestTabSwitch(RootTabEditor);
            }
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Discard & Continue", ImVec2(140.0f, 0.0f))) {
            RequestTabSwitch(RootTabEditor);
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save New Editor Project##AppShell", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter a name for the current project:");
        ImGui::Spacing();
        ImGui::InputText("##ProjectName", m_SaveNameBuffer, sizeof(m_SaveNameBuffer));
        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(100.0f, 0.0f))) {
            std::string newName = m_SaveNameBuffer;
            if (newName.empty()) {
                newName = "Untitled Project";
            }
            m_Editor.RequestSaveCurrentProject(newName);
            RequestTabSwitch(RootTabEditor);
            m_SaveNameBuffer[0] = '\0';
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void AppShell::OnTabChanged(int oldTab, int newTab) {
    if (oldTab == RootTabRaw && newTab != RootTabRaw) {
        m_Editor.ReleaseRawWorkspacePreviewForTabChange();
    }
    m_ActiveSyncLayerId.clear();
}

void AppShell::BeginRootTabBodyFade(int oldTab, int newTab) {
    const bool supportedPair =
        oldTab != newTab &&
        IsFadeableRootTab(oldTab) &&
        IsFadeableRootTab(newTab);
    m_RootTabBodyFadeActive = supportedPair;
    m_RootTabBodyFadeStartedAt = supportedPair ? ImGui::GetTime() : 0.0;
    m_RootTabBodyFadeFromTab = supportedPair ? oldTab : -1;
    m_RootTabBodyFadeToTab = supportedPair ? newTab : -1;
}

float AppShell::ConsumeRootTabBodyFadeAlpha(int* outRenderTabId) {
    if (outRenderTabId) {
        *outRenderTabId = m_CurrentTabId;
    }

    if (!m_RootTabBodyFadeActive) {
        return 1.0f;
    }

    const double elapsed = ImGui::GetTime() - m_RootTabBodyFadeStartedAt;
    if (elapsed < kRootTabBodyFadeOutSeconds) {
        if (outRenderTabId) {
            *outRenderTabId = m_RootTabBodyFadeFromTab;
        }
        const float t = std::clamp(
            static_cast<float>(elapsed / kRootTabBodyFadeOutSeconds),
            0.0f,
            1.0f);
        return 1.0f -
            ((1.0f - kRootTabBodyFadeMinAlpha) * ImGuiExtras::EaseOutCubic(t));
    }

    if (outRenderTabId) {
        *outRenderTabId = m_RootTabBodyFadeToTab;
    }

    const float fadeInT = std::clamp(
        static_cast<float>((elapsed - kRootTabBodyFadeOutSeconds) / kRootTabBodyFadeInSeconds),
        0.0f,
        1.0f);
    if (fadeInT >= 1.0f) {
        m_RootTabBodyFadeActive = false;
        m_RootTabBodyFadeStartedAt = 0.0;
        m_RootTabBodyFadeFromTab = -1;
        m_RootTabBodyFadeToTab = -1;
        if (outRenderTabId) {
            *outRenderTabId = m_CurrentTabId;
        }
        return 1.0f;
    }

    return kRootTabBodyFadeMinAlpha +
        (1.0f - kRootTabBodyFadeMinAlpha) * ImGuiExtras::EaseOutCubic(fadeInT);
}

void AppShell::Shutdown() {
    if (!m_Window) return;

    TraceShutdownPhase("shutdown-begin");
    auto runPhase = [&](const char* phase, const std::function<void()>& action) {
        TraceShutdownPhase((std::string(phase) + "-begin").c_str());
        const auto started = std::chrono::steady_clock::now();
        action();
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        TraceShutdownPhase(phase, elapsedMs);
    };

    runPhase("detached-preview-close", [&]() {
        m_Editor.CloseDetachedPreviewFullscreen();
    });
    runPhase("platform-hooks-uninstall", [&]() {
        UninstallDetachedPreviewPlatformHooks();
    });
    FileDialogs::SetOwnerWindow(nullptr);
#if defined(_WIN32)
    SetFramelessMainWindowCursorReleaseCallback({});
#endif
    runPhase("editor-shutdown", [&]() {
        m_Editor.Shutdown();
    });
    runPhase("appearance-save", [&]() {
        if (m_Appearance) {
            m_Appearance->Save();
        }
    });
    runPhase("app-texture-cleanup", [&]() {
        if (m_EditorTabTexture) {
            glDeleteTextures(1, &m_EditorTabTexture);
            m_EditorTabTexture = 0;
        }
        if (m_LibraryTabTexture) {
            glDeleteTextures(1, &m_LibraryTabTexture);
            m_LibraryTabTexture = 0;
        }
        if (m_ToolsTabTexture) {
            glDeleteTextures(1, &m_ToolsTabTexture);
            m_ToolsTabTexture = 0;
        }
        if (m_RawTabTexture) {
            glDeleteTextures(1, &m_RawTabTexture);
            m_RawTabTexture = 0;
        }
        if (m_HeaderSettingsTexture) {
            glDeleteTextures(1, &m_HeaderSettingsTexture);
            m_HeaderSettingsTexture = 0;
        }
        ReleaseBackgroundImageTexture();
    });
    runPhase("composite-shutdown", [&]() {
        m_Composite.Shutdown();
    });
    runPhase("task-system-shutdown", [&]() {
        Async::TaskSystem::Get().Shutdown();
    });
    runPhase("cursor-release", [&]() {
        ReleaseLockedScrubCursor(false);
    });
#if defined(_WIN32)
    runPhase("appwindow-titlebar-native-input-uninstall", [&]() {
        UninstallAppWindowTitlebarNativeInput();
    });
    runPhase("frameless-uninstall", [&]() {
        UninstallFramelessMainWindowChrome();
    });
#endif
    runPhase("imgui-shutdown", [&]() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    });

    runPhase("main-window-callback-detach", [&]() {
        if (m_Window) {
            glfwSetWindowCloseCallback(m_Window, nullptr);
            glfwSetWindowUserPointer(m_Window, nullptr);
        }
    });
    runPhase("window-destroy", [&]() {
        glfwDestroyWindow(m_Window);
    });
    runPhase("appwindow-titlebar-shutdown", [&]() {
        AppWindowTitleBarBridge::Shutdown();
    });
    runPhase("glfw-terminate", [&]() {
        glfwTerminate();
    });
    m_Window = nullptr;
    TraceShutdownPhase("shutdown-complete");
}

void AppShell::RequestTabSwitch(int tabId) {
    m_RequestedTab = (tabId == RootTabComposite) ? RootTabEditor : tabId;
}

bool AppShell::CanChangeRootTab(int oldTab, int newTab) {
    if (oldTab == RootTabRaw && newTab != RootTabRaw) {
        return m_Editor.FlushActiveRawWorkspaceProjectIfDirty();
    }
    return true;
}

void AppShell::OnFileDrop(GLFWwindow* window, int count, const char** paths) {
    AppShell* app = static_cast<AppShell*>(glfwGetWindowUserPointer(window));
    if (app) {
        app->HandleDrop(count, paths);
    }
}

void AppShell::HandleDrop(int count, const char** paths) {
    if (count <= 0 || !paths) return;

    double cursorX = 0.0;
    double cursorY = 0.0;
    if (m_Window) {
        glfwGetCursorPos(m_Window, &cursorX, &cursorY);
    }

    if (m_CurrentTabId == RootTabEditor || m_CurrentTabId == RootTabRaw) {
        std::vector<std::string> graphImagePaths;
        graphImagePaths.reserve(count);
        for (int i = 0; i < count; ++i) {
            const std::string path = paths[i] ? paths[i] : "";
            if (IsSupportedDroppedImagePath(path)) {
                graphImagePaths.push_back(path);
            }
        }

        if (!graphImagePaths.empty() &&
            m_Editor.HandleGraphFileDrop(graphImagePaths, static_cast<float>(cursorX), static_cast<float>(cursorY))) {
            for (int i = 0; i < count; ++i) {
                const std::string path = paths[i] ? paths[i] : "";
                if (IsSupportedDroppedImagePath(path)) {
                    continue;
                }
                LibraryManager::Get().RequestImportAndLoad(path, &m_Editor, nullptr, [this](int tabId) {
                    RequestTabSwitch(tabId);
                });
            }
            return;
        }
    }

    for (int i = 0; i < count; ++i) {
        const std::string path = paths[i] ? paths[i] : "";
        LibraryManager::Get().RequestImportAndLoad(path, &m_Editor, nullptr, [this](int tabId) {
            RequestTabSwitch(tabId);
        });
    }
}

void AppShell::ShowSplashScreen() {
    double startTime = glfwGetTime();

    // 1. Peek at image dimensions to set window size
    int texW = 0, texH = 0, texCh = 0;
    stbi_set_flip_vertically_on_load(0);
    
    // We try to load just the info first if possible, or just load the whole thing if it's small.
    // stbi_info_from_memory is better.
    if (splash_size > 0) {
        stbi_info_from_memory(splash_data, splash_size, &texW, &texH, &texCh);
    }

    // Default if image fails or is empty
    if (texW <= 0 || texH <= 0) {
        texW = 800;
        texH = 450;
    }

    // Scale to a reasonable screen size if too large
    float scale = 1.0f;
    const int maxW = 600;
    const int maxH = 400;
    if (texW > maxW) scale = (float)maxW / (float)texW;
    if (texH * scale > maxH) scale = (float)maxH / (float)texH;

    int splashW = (int)((float)texW * scale);
    int splashH = (int)((float)texH * scale);

    // 2. Create a borderless banner window
    ApplyBaseOpenGlWindowHints();
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); 
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    m_SplashWindow = glfwCreateWindow(splashW, splashH, "Stack Loading", nullptr, nullptr);
    ApplyBaseOpenGlWindowHints();
    if (!m_SplashWindow) return;

    SetWindowIconFromEmbeddedPng(m_SplashWindow, EmbeddedTabIcons::ProgramIcon_png_data, EmbeddedTabIcons::ProgramIcon_png_size);
    
    glfwSetWindowPos(m_SplashWindow, (mode->width - splashW) / 2, (mode->height - splashH) / 2);
    glfwShowWindow(m_SplashWindow);
    glfwMakeContextCurrent(m_SplashWindow);
    glfwSwapInterval(1);

    // Initialize GL for this context
    if (!LoadGLFunctions()) return;

    // Load splash texture from compiled-in memory
    unsigned char* pixels = nullptr;
    if (splash_size > 0) {
        pixels = stbi_load_from_memory(splash_data, splash_size, &texW, &texH, &texCh, 4);
        if (pixels) {
            m_SplashTexture = GLHelpers::CreateTextureFromPixels(pixels, texW, texH, 4);
            stbi_image_free(pixels);
        }
    }

    // Save main ImGui context and create a temporary one for splash
    ImGuiContext* mainContext = ImGui::GetCurrentContext();
    ImGuiContext* splashContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(splashContext);
    
    if (m_Appearance) {
        m_Appearance->SetupFonts(ImGui::GetIO());
    }
    ImGui_ImplGlfw_InitForOpenGL(m_SplashWindow, true);
    ImGui_ImplOpenGL3_Init("#version 430 core");
    if (m_Appearance) {
        m_Appearance->ApplyCurrentTheme(ImGui::GetIO(), ImGui::GetStyle());
    }

    // Customize Style for splash
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.WindowBorderSize = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    auto progressCallback = [&](int current, int total, const std::string& name) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuiExtras::BeginFrameInputRouting();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)splashW, (float)splashH));
        ImGui::Begin("Splash", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
        
        // Render Image fill
        if (m_SplashTexture) {
           ImDrawList* drawList = ImGui::GetWindowDrawList();
           ImVec2 pMin = ImGui::GetCursorScreenPos();
           ImVec2 pMax = ImVec2(pMin.x + splashW, pMin.y + splashH);
           drawList->AddImage((ImTextureID)(intptr_t)m_SplashTexture, pMin, pMax, ImVec2(0, 0), ImVec2(1, 1));
        }

        // Progress bar at the bottom
        ImGui::SetCursorPos(ImVec2(0, (float)splashH - 4));
        float progress = total > 0 ? (float)current / (float)total : 0.0f;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::ProgressBar(progress, ImVec2((float)splashW, 4), "");
        ImGui::PopStyleColor();
        
        // Status text
        ImGui::SetCursorPos(ImVec2(20, (float)splashH - 30));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.8f)); // Brighter text for transparency
        ImGui::Text("%s", name.c_str());
        ImGui::PopStyleColor();

        ImGui::End();
        ImGui::Render();
        
        int display_w, display_h;
        glfwGetFramebufferSize(m_SplashWindow, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        glfwSwapBuffers(m_SplashWindow);
    };

    // Start the library refresh asynchronously; do not block app launch on project scanning.
    LibraryManager::Get().RequestRefreshLibraryAsync();
    progressCallback(0, 1, "Starting library scan...");

    // Keep the splash perceptible without making a fast startup wait on decoration.
    while (glfwGetTime() - startTime < 0.35) {
        progressCallback(1, 1, "Ready");
        
        // Prevent 100% CPU usage in the wait loop
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Cleanup Splash
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(splashContext);
    
    if (m_SplashTexture) {
        glDeleteTextures(1, &m_SplashTexture);
        m_SplashTexture = 0;
    }

    glfwDestroyWindow(m_SplashWindow);
    m_SplashWindow = nullptr;

    // Restore Main Context
    ImGui::SetCurrentContext(mainContext);
    glfwMakeContextCurrent(m_Window);
}
