#include "GraphNativeCursor.h"
#include "Utils/GraphCursor.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "AppShell.h"
#include "HeaderActionRegistry.h"
#include "AppHeaderStyle.h"
#include "WorkspacePresentation.h"
#include "WorkspaceInputScope.h"
#include "AppWindowTitleBarBridge.h"
#include "AppPaths.h"
#include "AppSettingsPopup.h"
#include "AppLegalVersion.h"
#include "AppVersion.h"
#include "Async/TaskSystem.h"
#include "Presets/PresetManager.h"
#include "Persistence/ProjectStore.h"
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
#include <cstring>
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
        caption = palette.appSurface;
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
    RootTabRaw = 3,
    RootTabComposite = 4,
    RootTabRawLab = 5,
    RootTabMultiFrame = 6,
    RootTabQueue = 7
};

enum AppChromeCommandId {
    AppChromeCommandNone = 0,
    AppChromeCommandFile = 1,
    AppChromeCommandSettings = 2,
    AppChromeCommandGallery = 6,
    AppChromeCommandInfo = 7,
    AppChromeCommandOptions = 8,
    AppChromeCommandActivity = 9
};

bool IsRawWorkspaceRootTab(int tabId) {
    return tabId == RootTabRaw || tabId == RootTabRawLab;
}

bool IsRawProjectWorkspaceRootTab(int tabId) {
    return IsRawWorkspaceRootTab(tabId) || tabId == RootTabMultiFrame;
}

bool CrossesRawWorkspaceLifecycleBoundary(int oldTab, int newTab) {
    return IsRawProjectWorkspaceRootTab(oldTab) !=
        IsRawProjectWorkspaceRootTab(newTab);
}

bool IsFadeableRootTab(int tabId) {
    return tabId == RootTabLibrary ||
        tabId == RootTabEditor ||
        tabId == RootTabQueue ||
        IsRawProjectWorkspaceRootTab(tabId);
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
constexpr double kRootTabBodyFadeOutSeconds = 0.14;
constexpr double kRootTabBodyFadeInSeconds = 0.24;
constexpr float kRootTabWidgetFadeOutFraction = 0.70f;
constexpr float kRootTabWidgetFadeInDelayFraction = 0.16f;
constexpr double kClosingSurfaceMinVisibleSeconds = 0.95;
constexpr double kClosingSurfaceMaxDrainSeconds = 2.25;
constexpr int kClosingSurfaceMinPresentedFrames = 12;
constexpr double kClosingTextIntroSeconds = 0.24;
constexpr bool kLibraryLoadTransitionDiagnostics = false;
constexpr float kFloatingChromeRevealEdgeHeight = 7.0f;
constexpr float kFloatingChromeHiddenSliver = 3.0f;
constexpr double kFloatingChromeHoldSeconds = 0.62;
constexpr double kFloatingChromeStartupDiscoverySeconds = 1.8;
constexpr float kFloatingChromeShowSpeed = 13.0f;
constexpr float kFloatingChromeHideSpeed = 7.0f;
constexpr float kFloatingChromePillPaddingX = 10.0f;
constexpr float kFloatingChromePillPaddingY = 5.0f;

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
    std::vector<std::pair<RECT, int>> commandRects;
    Stack::HeaderActionRegistry actions;
    int pendingTab = -1;
    int pendingSecondaryTab = -1;
    int pendingCommand = 0;
};

AppWindowTitlebarNativeInputState g_AppWindowTitlebarNativeInputState;
bool g_ToolSwitcherOwnsWindow = false;

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
    g_AppWindowTitlebarNativeInputState.commandRects.clear();
    g_AppWindowTitlebarNativeInputState.actions.BeginFrame();
}

void AddAppWindowTitlebarNativeTabRect(const ImRect& rect, int tabId) {
    if (!g_AppWindowTitlebarNativeInputState.hwnd) {
        return;
    }
    g_AppWindowTitlebarNativeInputState.tabRects.emplace_back(ImRectToScreenRect(rect), tabId);
}

void AddAppWindowTitlebarNativeCommandRect(const ImRect& rect, int commandId) {
    if (!g_AppWindowTitlebarNativeInputState.hwnd) {
        return;
    }
    g_AppWindowTitlebarNativeInputState.commandRects.emplace_back(
        ImRectToScreenRect(rect),
        commandId);
}

int ConsumeAppWindowTitlebarNativeTabRequest() {
    const int tabId = g_AppWindowTitlebarNativeInputState.pendingTab;
    g_AppWindowTitlebarNativeInputState.pendingTab = -1;
    return tabId;
}

int ConsumeAppWindowTitlebarNativeSecondaryTabRequest() {
    const int tabId = g_AppWindowTitlebarNativeInputState.pendingSecondaryTab;
    g_AppWindowTitlebarNativeInputState.pendingSecondaryTab = -1;
    return tabId;
}

int ConsumeAppWindowTitlebarNativeCommandRequest() {
    const int command = g_AppWindowTitlebarNativeInputState.pendingCommand;
    g_AppWindowTitlebarNativeInputState.pendingCommand = 0;
    return command;
}

bool IsNativeMainWindowCloseButtonHovered(GLFWwindow* window) {
    if (!window || UseFramelessMainWindowChrome()) {
        return false;
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd || !IsWindowVisible(hwnd)) {
        return false;
    }

    POINT cursor {};
    if (!GetCursorPos(&cursor) || WindowFromPoint(cursor) != hwnd) {
        return false;
    }

    const LPARAM hitTestPoint = MAKELPARAM(
        static_cast<WORD>(static_cast<SHORT>(cursor.x)),
        static_cast<WORD>(static_cast<SHORT>(cursor.y)));
    return SendMessageW(hwnd, WM_NCHITTEST, 0, hitTestPoint) == HTCLOSE;
}

bool HandleAppWindowTitlebarNativeClick(POINT cursor) {
    if (g_AppWindowTitlebarNativeInputState.actions.QueueAt(
            static_cast<float>(cursor.x), static_cast<float>(cursor.y))) return true;
    for (const auto& [rect, commandId] :
         g_AppWindowTitlebarNativeInputState.commandRects) {
        if (PtInRect(&rect, cursor)) {
            g_AppWindowTitlebarNativeInputState.pendingCommand = commandId;
            TraceMainWindowNativeState(
                g_AppWindowTitlebarNativeInputState.hwnd,
                "appwindow-titlebar-native-command-click");
            return true;
        }
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

bool HandleAppWindowTitlebarNativeSecondaryClick(POINT cursor, bool doubleClick) {
    for (const auto& [rect, tabId] : g_AppWindowTitlebarNativeInputState.tabRects) {
        if (!PtInRect(&rect, cursor)) {
            continue;
        }
        if (doubleClick) {
            g_AppWindowTitlebarNativeInputState.pendingSecondaryTab = tabId;
            TraceMainWindowNativeState(
                g_AppWindowTitlebarNativeInputState.hwnd,
                "appwindow-titlebar-native-tab-secondary-double-click");
        }
        return true;
    }
    return false;
}

LRESULT CALLBACK AppWindowTitlebarNativeInputWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_ToolSwitcherOwnsWindow && message == WM_NCHITTEST) return HTCLIENT;
    // Some title-bar runtimes still deliver non-client button messages inside
    // a declared client region. Forward the complete gesture through GLFW so
    // ImGui owns activation, capture, release, popup blocking and keyboard use.
    // A separate command on mouse-down can disagree with ImGui's release path.
    UINT clientMessage = 0;
    switch (message) {
        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONDBLCLK: clientMessage = WM_LBUTTONDOWN; break;
        case WM_NCLBUTTONUP: clientMessage = WM_LBUTTONUP; break;
        case WM_NCRBUTTONDOWN:
        case WM_NCRBUTTONDBLCLK: clientMessage = WM_RBUTTONDOWN; break;
        case WM_NCRBUTTONUP: clientMessage = WM_RBUTTONUP; break;
        case WM_NCMBUTTONDOWN:
        case WM_NCMBUTTONDBLCLK: clientMessage = WM_MBUTTONDOWN; break;
        case WM_NCMBUTTONUP: clientMessage = WM_MBUTTONUP; break;
        default: break;
    }
    if (clientMessage && (wParam == HTCLIENT || wParam == HTCAPTION || wParam == HTSYSMENU)) {
        POINT point{static_cast<SHORT>(LOWORD(lParam)), static_cast<SHORT>(HIWORD(lParam))};
        if (AppWindowTitleBarBridge::IsCaptionPassthroughPoint(point.x, point.y) && ScreenToClient(hwnd, &point)) {
            WPARAM buttons = 0;
            if (GetKeyState(VK_LBUTTON) & 0x8000) buttons |= MK_LBUTTON;
            if (GetKeyState(VK_RBUTTON) & 0x8000) buttons |= MK_RBUTTON;
            if (GetKeyState(VK_MBUTTON) & 0x8000) buttons |= MK_MBUTTON;
            if (GetKeyState(VK_SHIFT) & 0x8000) buttons |= MK_SHIFT;
            if (GetKeyState(VK_CONTROL) & 0x8000) buttons |= MK_CONTROL;
            return CallAppWindowTitlebarBaseProc(hwnd, clientMessage, buttons, MAKELPARAM(point.x, point.y));
        }
    }
    switch (message) {
        case WM_NCHITTEST: {
            const LRESULT nativeHit = CallAppWindowTitlebarBaseProc(hwnd, message, wParam, lParam);
            if ((nativeHit == HTCAPTION || nativeHit == HTSYSMENU) && AppWindowTitleBarBridge::IsCaptionPassthroughPoint(
                    static_cast<SHORT>(LOWORD(lParam)), static_cast<SHORT>(HIWORD(lParam))))
                return HTCLIENT;
            return nativeHit;
        }
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
        case WM_NCRBUTTONDOWN:
        case WM_NCRBUTTONUP:
        case WM_NCRBUTTONDBLCLK: {
            POINT cursor {
                static_cast<LONG>(static_cast<SHORT>(LOWORD(lParam))),
                static_cast<LONG>(static_cast<SHORT>(HIWORD(lParam)))
            };
            if (HandleAppWindowTitlebarNativeSecondaryClick(
                    cursor,
                    message == WM_NCRBUTTONDBLCLK)) {
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
    if (g_ToolSwitcherOwnsWindow && message == WM_NCHITTEST) return HTCLIENT;
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
    , m_RawTabTexture(0)
    , m_RawLabTabTexture(0)
    , m_FileNewTexture(0)
    , m_FileOpenProjectTexture(0)
    , m_FileSaveTexture(0)
    , m_FileExitProgramTexture(0)
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
    , m_MainWindowShownTime(0.0) {}

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
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
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
    static const std::string imguiIniPath = AppPaths::GetImGuiIniPath().u8string();
    io.IniFilename = imguiIniPath.c_str();
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
    UpdateNotifications();
    m_LegalManager = std::make_unique<AppLegal::Manager>();
    m_LegalManager->Initialize();
    m_UpdateManager = std::make_unique<AppUpdate::UpdateManager>(
        [this](UiNotificationSeverity severity, const std::string& message, const std::string& dedupeKey) {
            PostUiNotification(m_AppNotifier, severity, message, dedupeKey);
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
    m_RawTabTexture = LoadEmbeddedPngTexture(
        EmbeddedTabIcons::ToneCurve_png_data,
        EmbeddedTabIcons::ToneCurve_png_size,
        "RAW");
    m_RawLabTabTexture = LoadEmbeddedPngTexture(
        EmbeddedTabIcons::RawLab_png_data,
        EmbeddedTabIcons::RawLab_png_size,
        "RAW Lab");
    m_FileNewTexture = LoadEmbeddedPngTexture(
        EmbeddedTabIcons::FileNew_png_data,
        EmbeddedTabIcons::FileNew_png_size,
        "File New");
    m_FileOpenProjectTexture = LoadEmbeddedPngTexture(
        EmbeddedTabIcons::FileOpenProject_png_data,
        EmbeddedTabIcons::FileOpenProject_png_size,
        "File Open Project");
    m_FileSaveTexture = LoadEmbeddedPngTexture(
        EmbeddedTabIcons::FileSave_png_data,
        EmbeddedTabIcons::FileSave_png_size,
        "File Save");
    m_FileExitProgramTexture = LoadEmbeddedPngTexture(
        EmbeddedTabIcons::FileExitProgram_png_data,
        EmbeddedTabIcons::FileExitProgram_png_size,
        "File Exit Program");

    const std::filesystem::path sourceIconPath = std::filesystem::path("Assets") / "Icons" / "Stack.png";
    if (std::filesystem::exists(sourceIconPath)) {
        int w = 0, h = 0, ch = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* pixels = stbi_load(sourceIconPath.string().c_str(), &w, &h, &ch, 4);
        if (pixels) {
            m_ProgramIconTexture = GLHelpers::CreateTextureFromPixels(pixels, w, h, 4);
            stbi_image_free(pixels);
        }
    }
    if (m_ProgramIconTexture == 0) {
        m_ProgramIconTexture = LoadEmbeddedPngTexture(
            EmbeddedTabIcons::ProgramIcon_png_data,
            EmbeddedTabIcons::ProgramIcon_png_size,
            "Program Icon");
    }

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
#if defined(_WIN32)
    if (AppWindowTitleBarBridge::IsActive()) {
        InstallAppWindowTitlebarNativeInput(m_Window);
    }
#endif
    if (AppWindowTitleBarBridge::RuntimeFlagEnabled()) {
        const AppWindowTitleBarBridge::Metrics& titlebarMetrics = AppWindowTitleBarBridge::GetMetrics();
        TraceMainWindowState(
            titlebarMetrics.active ? "main-appwindow-titlebar-active" : "main-appwindow-titlebar-fallback",
            titlebarMetrics.fallbackReason.c_str());
    }

    const bool transparentFramebufferAvailable =
        glfwGetWindowAttrib(m_Window, GLFW_TRANSPARENT_FRAMEBUFFER) == GLFW_TRUE;
    if (transparentFramebufferAvailable && AppWindowTitleBarBridge::IsActive()) {
        m_StartupReveal.Enable();
        m_StartupRevealVisual = m_StartupReveal.Update(
            0.0,
            ImVec2(static_cast<float>(width), static_cast<float>(height)));
        const StackAppearance::RuntimeSurfacePalette palette =
            m_Appearance->GetRuntimeSurfacePalette();
        AppWindowTitleBarBridge::UpdateTheme(
            m_Window,
            ImGui::GetStyleColorVec4(ImGuiCol_Text),
            palette.controlSurfaceHovered,
            palette.controlSurfaceActive,
            0.0f);
        NativeWindowTheme::SetMainWindowBorderVisible(
            m_Window,
            palette.border,
            false);
        m_StartupRevealBorderVisible = false;
        TraceMainWindowState("startup-reveal-enabled");
    } else {
        m_StartupReveal.Disable();
        std::string detail;
        if (!transparentFramebufferAvailable) {
            detail = "GLFW transparent framebuffer unavailable";
        } else {
            detail = AppWindowTitleBarBridge::GetMetrics().fallbackReason.empty()
                ? "AppWindow titlebar bridge inactive"
                : AppWindowTitleBarBridge::GetMetrics().fallbackReason;
        }
        TraceMainWindowState("startup-reveal-disabled", detail.c_str());
    }
    FileDialogs::SetOwnerWindow(m_Window, [this]() {
        ReleaseLockedScrubCursor(false);
    });
    glfwSetDropCallback(m_Window, OnFileDrop);

    Async::TaskSystem::Get().Initialize();
    InitializeProjectWorkspaces();
    m_QueueRenderer.Configure(m_Window, m_Appearance.get(), &m_Queue.Model());
    m_Queue.SetExportRequestHandler(
        [this](const std::filesystem::path& destination,
               const std::vector<Stack::Queue::Item>& items) {
            const auto startExport = [this,destination,items] {
            std::string error;
            if (!m_QueueRenderer.Start(destination, items, &error)) {
                const std::string message = error.empty()
                    ? "The Queue export could not be started."
                    : error;
                m_Queue.SetStatusText(message);
                PostUiNotification(
                    m_QueueRenderer.GetNotifier(),
                    UiNotificationSeverity::Error,
                    message,
                    "queue-export-start");
            }
            };
            if(!m_Editor->RequestAutoBracketForeground("export these images",startExport))startExport();
        });
    m_Library.Initialize();
    m_Composite.Initialize();
    // The Library tab populates asynchronously so the main window can appear quickly.
    PresetManager::Get();

    if (!loadedAppearance) {
        m_Appearance->Save();
    }

    if (m_StartupReveal.IsEnabled()) {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glfwSwapBuffers(m_Window);
        TraceMainWindowState("startup-reveal-transparent-frame-primed");
        m_StartupRevealWindowOpacityHidden =
            NativeWindowTheme::HideMainWindowUntilFirstFrame(
                m_Window,
                m_StartupRevealOriginalExtendedStyle);
        if (m_StartupRevealWindowOpacityHidden) {
            TraceMainWindowState("startup-reveal-window-hidden-until-first-frame");
        } else {
            TraceMainWindowState("startup-reveal-window-hide-unavailable");
        }
    }

    TraceMainWindowState("main-show-maximized-requested");
    ShowMainWindowMaximized(m_Window);
    TraceMainWindowState("main-shown");
    m_MainWindowShownTime = glfwGetTime();
    m_StartupReveal.Start(m_MainWindowShownTime);
    m_StartupRevealVisual = m_StartupReveal.Update(
        m_MainWindowShownTime,
        ImVec2(static_cast<float>(width), static_cast<float>(height)));
    m_IsRunning = true;
    if (!m_DiagnosticProjectOpenPath.empty()) {
        m_CurrentTabId = RootTabRawLab;
        m_RequestedTab = -1;
        m_Editor->EnterRawWorkspaceRootTab();
        m_DiagnosticProjectOpenActive =
            m_Editor->RequestOpenRawWorkspaceProjectFromGallery(
                m_DiagnosticProjectOpenPath);
        m_DiagnosticProjectOpenStartedAt = glfwGetTime();
        if (!m_DiagnosticProjectOpenActive) {
            std::cerr << "[ProjectOpenDiagnostic] Failed to queue "
                      << m_DiagnosticProjectOpenPath.string() << '\n';
        }
    }
    if (!m_DiagnosticQueueProjectPath.empty() &&
        !m_DiagnosticQueueDestination.empty()) {
        if (m_DiagnosticQueueSourceImage) {
            m_Queue.Model().AddSources({ m_DiagnosticQueueProjectPath });
        } else {
            m_Queue.Model().AddProjects({ m_DiagnosticQueueProjectPath });
        }
        std::string queueError;
        m_DiagnosticQueueActive = m_QueueRenderer.Start(
            m_DiagnosticQueueDestination,
            m_Queue.Model().Snapshot(),
            &queueError);
        m_DiagnosticQueueStartedAt = glfwGetTime();
        if (!m_DiagnosticQueueActive) {
            std::cerr << "[QueueExportDiagnostic] Failed to start: "
                      << queueError << '\n';
            m_IsRunning = false;
        }
    }
    if (!m_DiagnosticGalleryInspectionFirstSource.empty() &&
        !m_DiagnosticGalleryInspectionLatestSource.empty()) {
        Stack::RawGalleryInspection::Request request;
        request.requestId = 1;
        request.sourcePath = m_DiagnosticGalleryInspectionFirstSource;
        request.displayName = request.sourcePath.filename().string();
        request.version = Stack::RawGalleryInspection::Version::After;
        std::string inspectionError;
        m_DiagnosticGalleryInspectionActive =
            m_QueueRenderer.RequestInspection(
                std::move(request),
                [this](Stack::RawGalleryInspection::Result result) {
                    CompleteDiagnosticGalleryInspection(std::move(result));
                },
                &inspectionError);
        m_DiagnosticGalleryInspectionStartedAt = glfwGetTime();
        m_DiagnosticGalleryInspectionLastTickAt = 0.0;
        m_DiagnosticGalleryInspectionMaxTickGapMs = 0.0;
        if (!m_DiagnosticGalleryInspectionActive) {
            std::cerr
                << "[GalleryInspectionDiagnostic] First request failed: "
                << inspectionError << '\n';
            m_IsRunning = false;
        }
    }
    StartAutomaticUpdateCheckIfAllowed();
    return true;
}

void AppShell::StartAutomaticUpdateCheckIfAllowed() {
    if (m_StartupUpdateCheckStarted ||
        !m_LegalManager ||
        !m_LegalManager->IsAccepted() ||
        !m_UpdateManager ||
        !m_UpdateManager->IsAutomaticStartupCheckEnabled()) {
        return;
    }
    m_StartupUpdateCheckStarted = true;
    m_UpdateManager->StartBackgroundCheck();
}

void AppShell::ProcessNativeCloseButtonHoverSave() {
#if defined(_WIN32)
    const bool closeButtonHovered =
        IsNativeMainWindowCloseButtonHovered(m_Window);
    if (!closeButtonHovered) {
        m_NativeCloseButtonHoverActive = false;
        return;
    }
    if (m_NativeCloseButtonHoverActive) {
        return;
    }
    m_NativeCloseButtonHoverActive = true;
    TraceMainWindowState("native-close-button-hover");

    if (m_CloseRequested || m_MainWindowCloseSavePending) {
        return;
    }

    const EditorModule::ProjectFileCommandContext context =
        m_Editor->GetProjectFileCommandContext();
    if (!context.dirty) {
        TraceMainWindowState("native-close-hover-save-skipped", "project-clean");
        return;
    }
    if (context.busy) {
        TraceMainWindowState("native-close-hover-save-skipped", "project-operation-busy");
        return;
    }

    const std::string projectName = m_Editor->GetCurrentProjectName().empty()
        ? "Untitled Project"
        : m_Editor->GetCurrentProjectName();
    const bool requested = m_Editor->RequestSaveCurrentProject(projectName);
    TraceMainWindowState(
        requested ? "native-close-hover-save-requested" : "native-close-hover-save-rejected",
        requested ? "save-queued" : "save-not-started");
#endif
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
        m_WorkspaceCompositor.BeginFrame();
        m_NotificationPresenter.BeginFrame(*m_NotificationStore);
        TickToolSwitcher();
#if defined(_WIN32)
        g_ToolSwitcherOwnsWindow = m_ToolSwitcher.Visible();
        if (m_ToolSwitcher.Visible()) BeginAppWindowTitlebarNativeInputFrame();
#endif
        if (m_ToolSwitcher.Visible()) ClearFramelessMainWindowDragZone();
        if (!m_WorkspaceDiagnosticOutput.empty() && !m_NotificationPresenter.BlocksInput()) TickWorkspaceSwitcher();

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

            if (!m_ToolSwitcher.Visible() && !m_NotificationPresenter.BlocksInput()) ProcessNativeCloseButtonHoverSave();

            std::string savedProjectFileName;
            std::string savedProjectKind;
            if (LibraryManager::Get().ConsumeSavedProjectEvent(savedProjectFileName, savedProjectKind)) {
                (void)savedProjectKind;
                (void)savedProjectFileName;
            }
            UpdateNotifications();
            TickProjectWorkspaces();
            ApplyRailNavigation();

            const auto renderUiStarted = std::chrono::steady_clock::now();
            {
                const bool preview = m_WorkspaceSwitcher.Visible();
                Stack::Workspace::PresentationScope presentation(preview);
                const bool reservedAlt = IsRawWorkspaceRootTab(m_CurrentTabId) && ImGui::GetIO().KeyAlt &&
                    !(ImGui::GetIO().KeyCtrl && ImGui::IsKeyDown(ImGuiKey_RightAlt));
                const bool savingTransition = m_MainWindowCloseSavePending ||
                    m_FileActionSavePending || m_ProjectLoadSavePending || m_RawWorkspaceSwitchSavePending ||
                    !m_ContinueMainWindowCloseSource.empty();
                Stack::Workspace::InputScope input(preview || savingTransition || m_WorkspaceDiscardMouse || reservedAlt || m_NotificationPresenter.BlocksInput(), m_WorkspaceFinishing && !m_NotificationPresenter.BlocksInput());
                if (preview || savingTransition || m_NotificationPresenter.BlocksInput()) { ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.f); ImGui::BeginDisabled(); }
                {
                    Stack::Workspace::InputScope toolInput(m_ToolSwitcher.Visible() || m_ToolSwitcher.discardMouse, false);
                    RenderUI();
                }
                if (preview) m_Editor->RestoreWorkspacePreviewLayout();
                if (preview || savingTransition || m_NotificationPresenter.BlocksInput()) { ImGui::EndDisabled(); ImGui::PopStyleVar(); }
            }
            RenderNotifications();
            if (!m_NotificationPresenter.BlocksInput()) {
                DrawToolSwitcher();
                DrawWorkspaceSwitcher();
            }
            renderUiMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - renderUiStarted).count();
            TickDiagnosticProjectOpen();
        }
        const double secondsSinceMainWindowShown = glfwGetTime() - m_MainWindowShownTime;
        LibraryTextureUploadStats libraryUploadStats;
        if (!m_CloseRequested && (m_CurrentTabId == RootTabLibrary || (m_WorkspaceSwitcher.Visible() && m_WorkspaceSwitcher.preview == RootTabLibrary)) && secondsSinceMainWindowShown > 0.35) {
            libraryUploadStats = LibraryManager::Get().UploadLibraryTextures(2.0);
        }
        if (!m_CloseRequested && m_StartupRevealVisual.AllowsInput()) {
            if (!m_WorkspaceSwitcher.held && !m_ToolSwitcher.Visible()) SyncCursorCaptureRequest();
            if (!m_WorkspaceSwitcher.Visible() && !m_ToolSwitcher.Visible()) ImGuiExtras::RenderGraphCursor(glfwGetWindowAttrib(m_Window,GLFW_FOCUSED)==GLFW_TRUE,m_LockedScrubCursorActive);
            if (!m_WorkspaceSwitcher.held && !m_ToolSwitcher.Visible()) GraphNativeCursor::Apply(m_Window,ImGuiExtras::GetGraphCursorSnapshot(),m_LockedScrubCursorActive);
        } else if (!m_CloseRequested) {
            ReleaseLockedScrubCursor(false);
        }

        const auto drawStarted = std::chrono::steady_clock::now();
        ImGui::Render();
        if (!m_CloseRequested) {
            for (const auto& workspace : m_ProjectWorkspaces) ProcessGraphCaptureRequest(workspace->editor.get());
        }
        int display_w, display_h;
        glfwGetFramebufferSize(m_Window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);

        const ImVec4 clearColor = m_Appearance ? m_Appearance->GetClearColor() : ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
        if (m_StartupRevealVisual.HasTransparentBackdrop()) {
            // GLFW's Windows transparent framebuffer is composited as premultiplied
            // alpha.  Transparent pixels therefore need zero RGB as well as zero
            // alpha, otherwise the theme color leaks through as a full-window tint.
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        } else {
            glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
        }
        glClear(GL_COLOR_BUFFER_BIT);

        // Rendering and main-thread completions can both replace a document.
        // Invalidate before the old retained frame can be cached under its new identity.
        for (auto& workspace : m_ProjectWorkspaces) {
        const auto& document = workspace->editor->GetProjectDocumentId();
        const auto generation = workspace->editor->GetProjectFileOperations()->load.generation;
        if (workspace->previewDocumentId != document || workspace->previewLoadGeneration != generation) {
            m_WorkspaceCompositor.ForgetProjectPreview(workspace->id);
            workspace->previewDocumentId = document;
            workspace->previewLoadGeneration = generation;
            if (m_NavigationRail.previewProject == workspace->id) CancelProjectPillPreview();
        }
        }
        Stack::Renderer::WorkspaceCompositor::ProjectFrame projectFrame;
        projectFrame.reducedMotion = Stack::Notifications::SystemReducedMotion();
        projectFrame.rootView = m_CurrentTabId;
        projectFrame.bodyMin = m_NavigationRail.panelPosition;
        const auto* mainViewport = ImGui::GetMainViewport();
        projectFrame.bodyMax = ImVec2(mainViewport->Pos.x + mainViewport->Size.x,
            mainViewport->Pos.y + mainViewport->Size.y);
        projectFrame.captureAllowed = !m_RootTabBodyFadeActive && !m_ToolSwitcher.Visible() &&
            !m_SettingsPopupOpen && !m_NotificationPresenter.BlocksInput() &&
            ActiveProjectLoadPhase() == LibraryToEditorProjectLoadPhase::None &&
            !m_Editor->IsWorkspaceTransitionPending() && !m_Editor->IsBracketingPresentationActive() &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
            m_NavigationRail.previewAmount <= .001f &&
            (m_NavigationRail.panelAmount <= .001f || m_NavigationRail.panelAmount >= .999f) &&
            (m_NavigationRail.revealAmount <= .001f || m_NavigationRail.revealAmount >= .999f) &&
            (m_RawMaskToolbar.revealAmount <= .001f || m_RawMaskToolbar.revealAmount >= .999f);
        // Closing a popup can change its open flag after it already drew. Base
        // snapshot eligibility on what this frame actually contains as well.
        for (const auto* window : ImGui::GetCurrentContext()->Windows) {
            if (window->LastFrameActive == ImGui::GetFrameCount() &&
                (window->Flags & ImGuiWindowFlags_Tooltip))
                m_WorkspaceCompositor.KeepFixed(window->DrawList);
            if (window->LastFrameActive == ImGui::GetFrameCount() &&
                ((window->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip)) ||
                 std::strstr(window->Name, "GlobalHeaderSettingsPopup"))) {
                projectFrame.captureAllowed = false;
            }
        }
        projectFrame.previewWorkspace = m_NavigationRail.previewProject;
        projectFrame.previewRootView = m_NavigationRail.previewRootView;
        projectFrame.previewAmount = m_NavigationRail.previewAmount;
        if (!m_WorkspaceCompositor.Render(ImGui::GetDrawData(), m_WorkspaceSwitcher, clearColor,
                &m_ToolSwitcher, m_Editor->GetRawLabToolIndex(),
                m_StartupRevealVisual.AllowsInput() && !m_CloseRequested ? m_ActiveProjectWorkspace : 0,
                m_PendingWorkspaceRetirement == m_ActiveProjectWorkspace, &projectFrame))
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        CaptureDiagnosticWorkspaceSwitcher();
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            if (!m_CloseRequested && !m_WorkspaceSwitcher.Visible()) {
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
        if (m_StartupRevealWindowOpacityHidden) {
#if defined(_WIN32)
            DwmFlush();
#endif
            NativeWindowTheme::RevealMainWindowAfterFirstFrame(
                m_Window,
                m_StartupRevealOriginalExtendedStyle);
            m_StartupRevealWindowOpacityHidden = false;
            TraceMainWindowState("startup-reveal-first-frame-visible");
        }
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
                AllWorkspaceWorkersReadyForClose() &&
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

bool AppShell::IsDetachedSurfaceViewport(
    const ImGuiViewport* viewport,
    EditorModule::DetachedNativeWindowRequest* request) const {
    const EditorModule::DetachedSurfaceKind kinds[] = {
        EditorModule::DetachedSurfaceKind::EditorPreview,
        EditorModule::DetachedSurfaceKind::RawGallery
    };
    for (const EditorModule::DetachedSurfaceKind kind : kinds) {
        EditorModule::DetachedNativeWindowRequest localRequest;
        if (!m_Editor->QueryDetachedNativeWindow(kind, localRequest)) {
            continue;
        }
        if (viewport != nullptr &&
            localRequest.viewportId != 0 &&
            viewport->ID == localRequest.viewportId) {
            if (request) {
                *request = localRequest;
            }
            return true;
        }
    }
    return false;
}

void AppShell::HandleDetachedPreviewPlatformCreateWindow(ImGuiViewport* viewport) {
    EditorModule::DetachedNativeWindowRequest request;
    const bool detachedSurfaceViewport = IsDetachedSurfaceViewport(viewport, &request);
    if (m_OriginalPlatformCreateWindow) {
        m_OriginalPlatformCreateWindow(viewport);
    }

    if (!detachedSurfaceViewport || !viewport) {
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
        m_Editor->CompleteDetachedNativeWindowRequest(request, true, false);
    }
    TraceDetachedPreviewNativeWindow("platform-create", &request, themeApplied, false, false);
}

void AppShell::HandleDetachedPreviewPlatformShowWindow(ImGuiViewport* viewport) {
    EditorModule::DetachedNativeWindowRequest request;
    const bool detachedSurfaceViewport = IsDetachedSurfaceViewport(viewport, &request);
    if (m_OriginalPlatformShowWindow) {
        m_OriginalPlatformShowWindow(viewport);
    }

    if (!detachedSurfaceViewport || !viewport) {
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
        m_Editor->MarkDetachedNativeWindowShown(request, focused);
    }
    TraceDetachedPreviewNativeWindow("platform-show", &request, false, request.hasPlatformWindow, focused);
}

void AppShell::ProcessDetachedPreviewNativeWindow() {
    const EditorModule::DetachedSurfaceKind kinds[] = {
        EditorModule::DetachedSurfaceKind::EditorPreview,
        EditorModule::DetachedSurfaceKind::RawGallery
    };
    for (const EditorModule::DetachedSurfaceKind kind : kinds) {
        EditorModule::DetachedNativeWindowRequest request;
        if (!m_Editor->QueryDetachedNativeWindow(kind, request)) {
            continue;
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
            m_Editor->CompleteDetachedNativeWindowRequest(request, themeApplied, focused);
        }

        TraceDetachedPreviewNativeWindow(
            "post-platform-update",
            &request,
            themeApplied,
            focusAttempted,
            focused);
    }
}

void AppShell::CompleteDetachedPreviewPlatformPresent() {
    const EditorModule::DetachedSurfaceKind kinds[] = {
        EditorModule::DetachedSurfaceKind::EditorPreview,
        EditorModule::DetachedSurfaceKind::RawGallery
    };
    for (const EditorModule::DetachedSurfaceKind kind : kinds) {
        EditorModule::DetachedNativeWindowRequest request;
        if (!m_Editor->QueryDetachedNativeWindow(kind, request) ||
            !request.hasPlatformWindow ||
            request.window == nullptr) {
            continue;
        }

        const bool wasFirstPresented = request.firstPresented;
        if (!request.firstPresented) {
            m_Editor->MarkDetachedPlatformPresented(kind, request.window);
        }

        const bool focused = NativeWindowTheme::IsFocusedOrForeground(request.window);
        bool releasedTopMost = false;
        if (m_DetachedPreviewOpeningTopMostHeld && request.window == m_DetachedPreviewOpeningWindow) {
            NativeWindowTheme::ReleaseOpeningTopMost(request.window);
            m_DetachedPreviewOpeningTopMostHeld = false;
            m_DetachedPreviewOpeningWindow = nullptr;
            m_DetachedPreviewOpeningReleaseAttempts = 0;
            releasedTopMost = true;
        }

        if (!wasFirstPresented || releasedTopMost || m_DetachedPreviewOpeningTopMostHeld) {
            EditorModule::DetachedNativeWindowRequest updatedRequest;
            if (m_Editor->QueryDetachedNativeWindow(kind, updatedRequest)) {
                TraceDetachedPreviewNativeWindow(
                    releasedTopMost ? "opening-topmost-release" : "post-platform-present",
                    &updatedRequest,
                    false,
                    m_DetachedPreviewOpeningTopMostHeld,
                    focused);
            }
        }
    }
}

void AppShell::TraceDetachedPreviewNativeWindow(
    const char* event,
    const EditorModule::DetachedNativeWindowRequest* request,
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
           << " active=" << (m_Editor->IsDetachedPreviewActive() ? 1 : 0);
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
    if (!IsDetachedPreviewTraceEnabled() || !m_Editor->IsDetachedPreviewActive()) {
        return;
    }

    std::ofstream& stream = DetachedPreviewTraceStream();
    if (!stream.is_open()) {
        return;
    }

    stream << std::fixed << std::setprecision(2)
           << "frame=" << ImGui::GetFrameCount()
           << " event=frame"
           << " active=" << (m_Editor->IsDetachedPreviewActive() ? 1 : 0)
           << " layoutDetached=" << (m_Editor->IsDetachedPreviewLayoutDetached() ? 1 : 0)
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

    if (restoreCursorPosition && m_LockedCursorCaptureMode==ImGuiExtras::CursorCaptureMode::LinearScrub) {
        // Raw deltas adjust the value only. Release restores the saved pickup
        // position once; subsequent physical movement remains unrestricted.
        ImGuiExtras::SetGraphCursorReleaseTarget(m_LockedScrubCursorRestoreScreenPos);
    }

    if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
        glfwSetInputMode(m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
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
         request.mode == ImGuiExtras::CursorCaptureMode::LinearScrub ||
         request.mode == ImGuiExtras::CursorCaptureMode::LockedPan) &&
        windowFocused;

    if (!shouldCapture) {
        ReleaseLockedScrubCursor(windowFocused);
        return;
    }

    const bool lockedPan = request.mode == ImGuiExtras::CursorCaptureMode::LockedPan;
    const bool linearScrub=request.mode == ImGuiExtras::CursorCaptureMode::LinearScrub;
    const int glfwCursorMode = (lockedPan || linearScrub)
        ? GLFW_CURSOR_DISABLED
        : GLFW_CURSOR_HIDDEN;
    const bool captureModeChanged = m_LockedCursorCaptureMode != request.mode;
    if (!m_LockedScrubCursorActive || captureModeChanged) {
        m_LockedScrubCursorActive = true;
        m_LockedCursorCaptureMode = request.mode;
        m_LockedScrubCursorRestoreScreenPos = request.restoreScreenPos;
        glfwSetInputMode(m_Window, GLFW_CURSOR, glfwCursorMode);
        if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
            glfwSetInputMode(m_Window, GLFW_RAW_MOUSE_MOTION, (lockedPan || linearScrub) ? GLFW_TRUE : GLFW_FALSE);
        }
    }

    m_LockedScrubCursorAnchorScreenPos = request.anchorScreenPos;
    if (!lockedPan && !linearScrub) {
        const ImVec2 anchorLocal = ScreenToWindowCursorPos(m_Window, m_LockedScrubCursorAnchorScreenPos);
        glfwSetCursorPos(m_Window, anchorLocal.x, anchorLocal.y);
    }
}

void AppShell::RenderLegalGate() {
    if (m_ShowLegalGateReview && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        m_ShowLegalGateReview = false;
        return;
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30.0f, 24.0f));
    ImGui::Begin("Stack EULA##LegalGate", nullptr, flags);
    ImGui::PopStyleVar(3);

    const float contentWidth = std::min(900.0f, std::max(300.0f, ImGui::GetContentRegionAvail().x));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - contentWidth) * 0.5f));
    ImGui::BeginChild("##LegalGateContent", ImVec2(contentWidth, 0.0f), false);
    ImGui::TextUnformatted("STACK END USER LICENSE AGREEMENT");
    ImGui::Text("Version %s", AppLegalVersion::kEulaVersion);
    ImGui::TextDisabled("Published by Darynn Ho - Stack by CharmsMods");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    const float buttonAreaHeight = 105.0f;
    ImGui::BeginChild(
        "##LegalGateText",
        ImVec2(0.0f, std::max(140.0f, ImGui::GetContentRegionAvail().y - buttonAreaHeight)),
        true,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::PushTextWrapPos(0.0f);
    if (m_LegalManager && !m_LegalManager->GetEulaText().empty()) {
        ImGui::TextUnformatted(m_LegalManager->GetEulaText().c_str());
    } else {
        ImGui::TextWrapped("The Stack EULA file is missing or could not be read. Stack cannot continue from this package.");
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (!m_LegalActionError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.48f, 0.48f, 1.0f));
        ImGui::TextWrapped("%s", m_LegalActionError.c_str());
        ImGui::PopStyleColor();
    } else if (m_LegalManager) {
        ImGui::TextWrapped("%s", m_LegalManager->GetStatusMessage().c_str());
    }

    if (ImGui::Button("View EULA File", ImVec2(128.0f, 0.0f)) && m_LegalManager) {
        m_LegalActionError.clear();
        m_LegalManager->OpenDocument(AppLegal::Document::Eula, &m_LegalActionError);
    }
    ImGui::SameLine();
    if (ImGui::Button("View Privacy Notice", ImVec2(150.0f, 0.0f)) && m_LegalManager) {
        m_LegalActionError.clear();
        m_LegalManager->OpenDocument(AppLegal::Document::Privacy, &m_LegalActionError);
    }
    ImGui::SameLine();
    if (m_ShowLegalGateReview || (m_LegalManager && m_LegalManager->IsAccepted())) {
        if (ImGui::Button("Close Review", ImVec2(140.0f, 0.0f))) {
            m_ShowLegalGateReview = false;
        }
    } else {
        ImGui::BeginDisabled(!m_LegalManager || !m_LegalManager->IsEulaFileValid());
        if (ImGui::Button("Accept and Continue", ImVec2(160.0f, 0.0f))) {
            m_LegalActionError.clear();
            if (m_LegalManager->Accept(&m_LegalActionError)) {
                StartAutomaticUpdateCheckIfAllowed();
            }
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Exit Stack", ImVec2(110.0f, 0.0f))) {
        BeginMainWindowClose("eula-declined");
    }
    ImGui::EndChild();
    ImGui::End();
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

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const double startupNow = ImGui::GetTime();
    const StackAppearance::RuntimeSurfacePalette surfacePalette =
        m_Appearance ? m_Appearance->GetRuntimeSurfacePalette() : StackAppearance::RuntimeSurfacePalette{};
    const auto headerPalette = Stack::Header::ResolvePalette(
        m_Appearance ? m_Appearance->GetClearColor() : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg),
        !m_Window || glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) == GLFW_TRUE);
    m_StartupRevealVisual = m_StartupReveal.Update(
        startupNow,
        viewport ? viewport->Size : ImVec2(1280.0f, 800.0f));
    AppWindowTitleBarBridge::UpdateTheme(
        m_Window,
        ImGui::GetStyleColorVec4(ImGuiCol_Text),
        headerPalette.hover,
        headerPalette.pressed,
        m_WorkspaceSwitcher.Visible() ? 0.0f : m_StartupRevealVisual.captionButtonOpacity);
    const bool startupBorderVisible =
        m_StartupRevealVisual.captionButtonOpacity > 0.001f;
    if (startupBorderVisible != m_StartupRevealBorderVisible) {
        NativeWindowTheme::SetMainWindowBorderVisible(
            m_Window,
            surfacePalette.border,
            startupBorderVisible);
        m_StartupRevealBorderVisible = startupBorderVisible;
    }

    if (m_StartupRevealVisual.IsActive() &&
        !m_StartupRevealVisual.ShouldRenderInterface()) {
        RenderStartupReveal(
            viewport,
            m_StartupRevealVisual,
            m_Appearance ? m_Appearance->GetClearColor() : ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
        return;
    }

    StartAutomaticUpdateCheckIfAllowed();
    if (m_LegalManager && (!m_LegalManager->IsAccepted() || m_ShowLegalGateReview)) {
        RenderLegalGate();
        return;
    }

    SyncBackgroundImageTexture();
    const bool seamlessSurfaces = m_Appearance && m_Appearance->GetSeamlessSurfaceStylingEnabled();
    const bool rawImageSurround = IsRawProjectWorkspaceRootTab(m_CurrentTabId) &&
        m_ActiveProjectWorkspace != m_GalleryWorkspaceId && !m_Editor->IsAutoBracketWorkspace();

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    // Root Fullscreen Window for Tabbed Workspace
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar 
                                   | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize 
                                   | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus 
                                   | ImGuiWindowFlags_NoNavFocus;
    if (seamlessSurfaces || rawImageSurround) {
        window_flags |= ImGuiWindowFlags_NoBackground;
    }
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    
    ImGui::Begin("ModularStudioMain", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiIO& io = ImGui::GetIO();

    if (!rawImageSurround) RenderBackgroundImage(ImGui::GetWindowPos(), ImGui::GetWindowSize());
    if (rawImageSurround) ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));

    const std::vector<RootTabDescriptor> tabs = {
        { RootTabLibrary, "Library", m_LibraryTabTexture, [this]() {
            m_Library.RenderUI(
                m_Editor,
                &m_Composite,
                m_Appearance.get(),
                &m_RequestedTab,
                RootTabRawLab,
                [this](const std::string& projectFileName) {
                    BeginLibraryToEditorProjectLoad(projectFileName);
                });
        } },
        { RootTabRaw, "RAW", m_RawTabTexture, [this]() { m_Editor->RenderRawWorkspaceLabUI(); } },
        { RootTabRawLab, "RAW Lab", m_RawLabTabTexture != 0 ? m_RawLabTabTexture : m_RawTabTexture,
            [this]() { m_Editor->RenderRawWorkspaceLabUI(); } },
        { RootTabEditor, "Graph", m_EditorTabTexture, [this]() { m_Editor->RenderUI(); } },
        { RootTabQueue, "Queue", 0,
            [this]() { m_Queue.RenderUI(m_Appearance.get()); } }
    };

    TickLibraryToEditorProjectLoadTransition();
    if(m_CurrentTabId==RootTabMultiFrame){m_Editor->OpenBracketingTool();m_CurrentTabId=RootTabRawLab;}
    m_Editor->TickBracketing();
    m_Editor->UpdateBracketingPresentation();
    const bool bracketPresentation=m_Editor->IsBracketingPresentationActive();
    m_QueueRenderer.Tick();
    TickDiagnosticQueueExport();
    TickDiagnosticGalleryInspection();
    if (!m_QueueRenderer.StatusText().empty()) {
        m_Queue.SetStatusText(m_QueueRenderer.StatusText());
    }
    const bool loadTransitionActive = ActiveProjectLoadPhase() != LibraryToEditorProjectLoadPhase::None;

#if defined(_WIN32)
    const int appWindowNativeTabRequest = ConsumeAppWindowTitlebarNativeTabRequest();
    if (appWindowNativeTabRequest != -1) {
        RequestTabSwitch(appWindowNativeTabRequest);
    }
    const int appWindowNativeSecondaryTabRequest =
        ConsumeAppWindowTitlebarNativeSecondaryTabRequest();
    if (!m_NotificationPresenter.BlocksInput() && appWindowNativeSecondaryTabRequest == RootTabRawLab) {
        RequestTabSwitch(RootTabRaw);
    }
    const int consumedNativeCommand = ConsumeAppWindowTitlebarNativeCommandRequest();
    auto nativeWorkspaceAction = g_AppWindowTitlebarNativeInputState.actions.Consume();
    if (nativeWorkspaceAction && !m_NotificationPresenter.BlocksInput() && !m_WorkspaceSwitcher.Visible() &&
        m_StartupRevealVisual.AllowsInput()) nativeWorkspaceAction();
    const int appWindowNativeCommandRequest = (m_NotificationPresenter.BlocksInput() || m_WorkspaceSwitcher.Visible() ||
        m_MainWindowCloseSavePending || m_FileActionSavePending ||
        m_ProjectLoadSavePending || m_RawWorkspaceSwitchSavePending ||
        !m_ContinueMainWindowCloseSource.empty())
        ? AppChromeCommandNone : consumedNativeCommand;
#else
    const int appWindowNativeCommandRequest = AppChromeCommandNone;
#endif
    if (m_Editor->ConsumeOpenRawWorkspaceTabRequest()) {
        RequestTabSwitch(RootTabRawLab);
    }
    if (m_Editor->ConsumeOpenRawLabTabRequest()) {
        RequestTabSwitch(RootTabRawLab);
    }
    if (m_Editor->ConsumeOpenEditorTabRequest()) {
        RequestTabSwitch(RootTabEditor);
    }
    if (m_Editor->ConsumeOpenMultiFrameTabRequest()) {
        RequestTabSwitch(RootTabMultiFrame);
    }

    if (!loadTransitionActive && m_RequestedTab != -1) {
        RequestRootTabTransition(m_RequestedTab);
    }
    m_RequestedTab = -1;

    const bool isLibraryHovered = m_CurrentTabId == RootTabLibrary;
    m_Editor->SetLibraryWindowHovered(isLibraryHovered);
    m_Composite.SetLibraryWindowHovered(isLibraryHovered);

    int rootTabBodyRenderTabId = m_CurrentTabId;
    const float rootTabBodyAlpha = loadTransitionActive
        ? 1.0f
        : ConsumeRootTabBodyFadeAlpha(&rootTabBodyRenderTabId);
    if (m_WorkspaceSwitcher.Visible()) rootTabBodyRenderTabId = m_WorkspaceSwitcher.preview;

    auto blendColor = [](const ImVec4& from, const ImVec4& to, float t) {
        const float clamped = std::clamp(t, 0.0f, 1.0f);
        return ImVec4(
            from.x + (to.x - from.x) * clamped,
            from.y + (to.y - from.y) * clamped,
            from.z + (to.z - from.z) * clamped,
            from.w + (to.w - from.w) * clamped);
    };
    const auto smoothStep = [](float value) {
        const float t = std::clamp(value, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    float rootTabBodyWidgetAlpha = 1.0f;
    if (!loadTransitionActive && m_RootTabBodyFadeActive) {
        const double elapsed = ImGui::GetTime() - m_RootTabBodyFadeStartedAt;
        if (elapsed < kRootTabBodyFadeOutSeconds) {
            const float phaseT = static_cast<float>(elapsed / kRootTabBodyFadeOutSeconds);
            rootTabBodyWidgetAlpha = 1.0f - smoothStep(
                phaseT / kRootTabWidgetFadeOutFraction);
        } else {
            const float phaseT = static_cast<float>(
                (elapsed - kRootTabBodyFadeOutSeconds) / kRootTabBodyFadeInSeconds);
            rootTabBodyWidgetAlpha = smoothStep(
                (phaseT - kRootTabWidgetFadeInDelayFraction) /
                (1.0f - kRootTabWidgetFadeInDelayFraction));
        }
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    LayoutNavigationRail();
    // The root window spans the screen for presentation, but owns input only
    // to the right of the rail and section panel. Those sibling windows must
    // receive their clicks even if focus/display order changes during a load.
    ImGui::SetWindowHitTestHole(ImGui::GetCurrentWindow(), viewport->Pos,
        ImVec2(m_NavigationRail.visibleWidth + m_NavigationRail.visiblePanelWidth, viewport->Size.y));
    m_Library.SetSectionPanelHosted(true);
#if defined(_WIN32)
    if (AppWindowTitleBarBridge::IsActive()) BeginAppWindowTitlebarNativeInputFrame();
#endif
    RenderRawMaskToolbar();
    RenderNavigationRail(appWindowNativeCommandRequest == AppChromeCommandFile,
        appWindowNativeCommandRequest == AppChromeCommandSettings,
        appWindowNativeCommandRequest == AppChromeCommandActivity);
    RenderSharedPanelToggle();
    RenderHeaderSettingsPopup(false);

    // Empty caption space remains native window dragging, including the top
    // bar's empty span. Its reveal edge and actual controls pass through.
    const float chromeHeight = Stack::Header::CaptionHeight * m_NavigationRail.scale;
    std::vector<ImRect> inputRegions;
    inputRegions.emplace_back(viewport->Pos, ImVec2(viewport->Pos.x + std::max(
        2.f * m_NavigationRail.scale, m_NavigationRail.visibleWidth), viewport->Pos.y + viewport->Size.y));
    inputRegions.emplace_back(m_NavigationRail.panelPosition,
        ImVec2(m_NavigationRail.bodyPosition.x, viewport->Pos.y + viewport->Size.y));
    inputRegions.emplace_back(m_NavigationRail.bodyPosition,
        ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y));
    if (m_RawMaskToolbar.enabled) {
        inputRegions.emplace_back(viewport->Pos,
            ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + 2.f * m_NavigationRail.scale));
        if (m_RawMaskToolbar.visibleHeight > .5f) {
            const float size = 38.f * m_NavigationRail.scale;
            inputRegions.emplace_back(m_RawMaskToolbar.maskButtonPosition,
                ImVec2(m_RawMaskToolbar.maskButtonPosition.x + size, m_RawMaskToolbar.maskButtonPosition.y + size));
            inputRegions.emplace_back(m_RawMaskToolbar.panelTogglePosition,
                ImVec2(m_RawMaskToolbar.panelTogglePosition.x + size, m_RawMaskToolbar.panelTogglePosition.y + size));
        }
    }
    ClearFramelessMainWindowDragZone();
    if (AppWindowTitleBarBridge::IsActive()) {
        if (m_ToolSwitcher.Visible() || m_NotificationPresenter.BlocksInput() || m_SettingsPopupOpen ||
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            inputRegions = {ImRect(viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y))};
        AppWindowTitleBarBridge::SyncPassthroughRegions(m_Window, inputRegions);
    }

    const bool globalShortcutAllowed = m_StartupRevealVisual.AllowsInput() &&
        !m_ToolSwitcher.Visible() && m_NavigationRail.previewAmount <= .001f &&
        !io.WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    if (globalShortcutAllowed && io.KeyCtrl) {
        if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) RequestFileMenuSaveAs();
        else if (ImGui::IsKeyPressed(ImGuiKey_S, false)) RequestFileMenuSave();
        else if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
            if (m_Editor->GetProjectFileCommandContext().canOpen) m_ShowOpenProjectPrompt = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_N, false)) QueueFileAction(PendingFileAction::NewEditorProject);
        else if (ImGui::IsKeyPressed(ImGuiKey_W, false)) QueueWorkspaceTabAction(WorkspaceTabAction::Close, m_ActiveProjectWorkspace);
    }

    if (!loadTransitionActive && !m_WorkspaceSwitcher.Visible()) {
        m_Library.RenderGlobalPopups();
        RenderFileCommandPrompts();
        RenderEditorSavePrompts();
    }

    const ImVec2 projectBodyPosition = m_NavigationRail.bodyPosition;
    const ImVec2 projectBodySize = m_NavigationRail.bodySize;
    const bool pillPreview = m_NavigationRail.previewAmount > .001f;
    Stack::Workspace::InputScope previewInput(pillPreview, false);
    if (pillPreview) { ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.f); ImGui::BeginDisabled(); }
    const bool documentInputBlocked = bracketPresentation || m_Editor->IsWorkspaceTransitionPending() ||
        loadTransitionActive || m_RootTabBodyFadeActive;
    std::unique_ptr<Stack::Workspace::InputScope> documentInput;
    if (documentInputBlocked) {
        documentInput = std::make_unique<Stack::Workspace::InputScope>(true, false);
        ImGui::BeginDisabled();
    }
    {
        const bool lockPanel = loadTransitionActive || m_RootTabBodyFadeActive ||
            RawMaskToolbarOwnsInput() ||
            (IsRawProjectWorkspaceRootTab(rootTabBodyRenderTabId) && m_Editor->IsRawWorkspaceLockedByEditorProject());
        Stack::Workspace::InputScope panelInput(lockPanel, false);
        ImGui::BeginDisabled(lockPanel);
        RenderWorkspaceSectionPanel(rootTabBodyRenderTabId);
        ImGui::EndDisabled();
    }
    const bool overRail = m_NavigationRail.visibleWidth > .5f &&
        ImRect(viewport->Pos, ImVec2(viewport->Pos.x + m_NavigationRail.visibleWidth,
            viewport->Pos.y + viewport->Size.y)).Contains(io.MousePos);
    std::unique_ptr<Stack::Workspace::InputScope> railInput;
    if (overRail || RawMaskToolbarOwnsInput())
        railInput = std::make_unique<Stack::Workspace::InputScope>(true, false);
    ImGui::SetCursorScreenPos(projectBodyPosition);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f * m_NavigationRail.scale, 0.f));
    ImGui::BeginChild("StackWorkingArea", projectBodySize, ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    RenderLibraryViewSwitch();
    if (m_Editor->IsAutoBracketWorkspace()) {
        const auto position = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(position.x, std::max(position.y, viewport->Pos.y + 58.0f)));
        ImGui::BeginChild("AutoBracketWorkspace", ImVec2(0, 0), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        m_Editor->RenderAutoBracketWorkspace();
        ImGui::EndChild();
    } else if (loadTransitionActive) {
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

        if (ActiveProjectLoadPhase() == LibraryToEditorProjectLoadPhase::EditorReveal) {
            m_Editor->RenderUI();
        } else {
            ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
            ImGui::BeginDisabled();
            m_Library.RenderUI(m_Editor, &m_Composite, m_Appearance.get(),
                &m_RequestedTab, RootTabRawLab, {});
            ImGui::EndDisabled();
            ImGui::PopStyleVar();
        }

        RenderLibraryLoadTransitionDiagnostics();

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    } else {
        const ImVec2 rootBodyMin = ImGui::GetCursorScreenPos();
        const ImVec2 rootBodySize = ImGui::GetContentRegionAvail();
        ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
        if (m_RootTabBodyFadeActive) {
            ImGui::BeginDisabled();
        }
        for (const RootTabDescriptor& tab : tabs) {
            if (tab.id == rootTabBodyRenderTabId) {
                ImGui::PushStyleVar(
                    ImGuiStyleVar_Alpha,
                    rootTabBodyWidgetAlpha);
                const bool lockRawBody =
                    IsRawProjectWorkspaceRootTab(tab.id) &&
                    m_Editor->IsRawWorkspaceLockedByEditorProject();
                if (lockRawBody) {
                    ImGui::BeginDisabled();
                }
                tab.renderBody();
                if (lockRawBody) {
                    ImGui::EndDisabled();
                }
                ImGui::PopStyleVar();
                break;
            }
        }
        if (m_RootTabBodyFadeActive) {
            ImGui::EndDisabled();
        }
        ImGui::PopStyleVar();

        // Draw the base visual fade directly on the viewport foreground list.
        // This avoids creating a temporary ImGui window (and therefore avoids
        // its rounded border/shadow), while clipping below the persistent app
        // chrome keeps File, Settings, Library, Raw, and Graph fixed in place.
        const float veilAlpha = 1.0f - rootTabBodyAlpha;
        if (veilAlpha > 0.001f && rootBodySize.x > 0.0f && rootBodySize.y > 0.0f) {
            const ImVec2 mainWindowPos = ImGui::GetWindowPos();
            const ImVec2 mainWindowSize = ImGui::GetWindowSize();
            const ImVec2 transitionMin(
                projectBodyPosition.x,
                std::floor(std::max(rootBodyMin.y, viewport->Pos.y + chromeHeight)));
            const ImVec2 transitionMax(
                viewport->Pos.x + viewport->Size.x,
                viewport->Pos.y + viewport->Size.y);
            ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);
            drawList->PushClipRect(transitionMin, transitionMax, true);
            ImVec4 veilColor = m_Appearance
                ? m_Appearance->GetClearColor()
                : ImVec4(0.06f, 0.07f, 0.08f, 1.0f);
            veilColor.w = veilAlpha;
            drawList->AddRectFilled(
                transitionMin,
                transitionMax,
                ImGui::GetColorU32(veilColor),
                0.0f);
            RenderBackgroundImage(
                mainWindowPos,
                mainWindowSize,
                veilAlpha,
                drawList);
            drawList->PopClipRect();
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    railInput.reset();
    // The document disable scope began in ModularStudioMain. Close it there,
    // after StackWorkingArea has restored its own ImGui stack snapshot.
    if (documentInputBlocked) ImGui::EndDisabled();
    documentInput.reset();
    if (pillPreview) { ImGui::EndDisabled(); ImGui::PopStyleVar(); }
    RenderProjectPreviewFallback();
    ImVec2 toolMin, toolMax;
    if (m_Editor->GetRawActiveControlBounds(toolMin, toolMax))
        m_WorkspaceCompositor.SetToolBlurBounds(toolMin, toolMax);

    const float startupInterfaceVeil = std::clamp(
        1.0f - m_StartupRevealVisual.interfaceOpacity,
        0.0f,
        1.0f);
    if (startupInterfaceVeil > 0.001f) {
        ImVec4 overlayColor = m_Appearance ? m_Appearance->GetClearColor() : ImVec4(0.06f, 0.07f, 0.08f, 1.0f);
        overlayColor.w = startupInterfaceVeil;
        ImGui::GetForegroundDrawList(viewport)->AddRectFilled(
            viewport->Pos,
            ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
            ImGui::ColorConvertFloat4ToU32(overlayColor));
    }

    if (m_StartupRevealVisual.IsActive() && !m_StartupRevealVisual.AllowsInput()) {
        ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(
            "StartupRevealInputBlocker",
            nullptr,
            ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBackground |
                ImGuiWindowFlags_NoNav);
        ImGui::InvisibleButton("##StartupRevealInputBlocker", viewport->Size);
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // These overrides were pushed inside this window. Restore them before
    // End(), so ImGui's window recovery does not consume them a second time.
    if (rawImageSurround) ImGui::PopStyleColor();
    ImGui::PopStyleVar(); // Square edges where workspace children meet the panel.
    ImGui::End(); // End ModularStudioMain
    if (rawImageSurround) {
        m_WorkspaceCompositor.SetRawImageBackdrop(m_Editor->GetRawImageBackdrop());
    }

    if (m_StartupRevealVisual.AllowsInput()) {
        // Library is hosted by the main workspace.
        const bool detachedInputBlocked = documentInputBlocked || loadTransitionActive;
        Stack::Workspace::InputScope detachedInput(detachedInputBlocked, false);
        if (detachedInputBlocked) ImGui::BeginDisabled();

        if (m_Editor->IsDetachedPreviewActive()) {
            m_Editor->RenderDetachedPreviewWindow();
        }
        m_Editor->RenderRawWorkspaceDetachedWindows();
        if (detachedInputBlocked) ImGui::EndDisabled();
    }

    if (m_StartupRevealVisual.AllowsInput() &&
        ImGui::GetCurrentContext()->OpenPopupStack.empty()) {
        for (const char* name : {"StackRawMaskToolbar", "StackNavigationRail", "StackPanelToggle", "StackActivity"})
            if (auto* headerWindow = ImGui::FindWindowByName(name)) {
                ImGui::BringWindowToDisplayFront(headerWindow);
                m_WorkspaceCompositor.KeepChrome(headerWindow->DrawList);
            }
    }
    if(bracketPresentation) {
        if(glfwGetWindowAttrib(m_Window,GLFW_ICONIFIED)!=GLFW_TRUE)
            m_Editor->RenderBracketingPresentation(
                m_Appearance?m_Appearance->GetClearColor():ImVec4(.08f,.08f,.08f,1),
                projectBodyPosition, projectBodySize);
    }

}

void AppShell::RenderStartupReveal(
    ImGuiViewport* viewport,
    const Stack::StartupReveal::Visual& visual,
    const ImVec4& surfaceColor) {
    if (viewport == nullptr) {
        return;
    }

    Stack::StartupReveal::Render(
        visual,
        viewport->Pos,
        viewport->Size,
        surfaceColor,
        ImGui::GetForegroundDrawList(viewport));

    ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(
        "StartupRevealInputBlocker",
        nullptr,
        ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoNav);
    ImGui::InvisibleButton("##StartupRevealInputBlocker", viewport->Size);
    ImGui::End();
    ImGui::PopStyleVar();
}

bool AppShell::IsLibraryWindowOpenAndHovered() const {
    if (!m_LibraryWindowOpen) {
        return false;
    }
    ImGuiWindow* libraryWindow = ImGui::FindWindowByName("###StackLibraryWindow");
    if (libraryWindow == nullptr || libraryWindow->Hidden || !libraryWindow->Active) {
        return false;
    }
    const ImVec2 libPos = libraryWindow->Pos;
    const ImVec2 libSize = libraryWindow->Size;
    return ImGui::IsMouseHoveringRect(
        libPos,
        ImVec2(libPos.x + libSize.x, libPos.y + libSize.y),
        false);
}

void AppShell::RenderLibraryWindow() {
    if (!m_LibraryWindowOpen) {
        m_LibraryNativeWindow = nullptr;
        m_LibraryNativeAppearanceRevision = 0;
        return;
    }

    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    if (!m_LibraryWindowPlacementInitialized && mainViewport != nullptr) {
        const float availableWidth = std::max(420.0f, mainViewport->WorkSize.x - 96.0f);
        const float availableHeight = std::max(320.0f, mainViewport->WorkSize.y - 96.0f);
        const float minimumWidth = std::min(760.0f, availableWidth);
        const float minimumHeight = std::min(560.0f, availableHeight);
        const ImVec2 windowSize(
            std::clamp(mainViewport->WorkSize.x * 0.72f, minimumWidth, availableWidth),
            std::clamp(mainViewport->WorkSize.y * 0.76f, minimumHeight, availableHeight));
        const ImVec2 windowPosition(
            mainViewport->WorkPos.x +
                (mainViewport->WorkSize.x - windowSize.x) * 0.5f + 36.0f,
            mainViewport->WorkPos.y +
                (mainViewport->WorkSize.y - windowSize.y) * 0.5f + 36.0f);
        ImGui::SetNextWindowPos(windowPosition, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    }

    ImGuiWindowClass windowClass;
    windowClass.ClassId = ImHashStr("StackLibraryWindow");
    windowClass.DockingAllowUnclassed = false;
    windowClass.ParentViewportId = 0;
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    windowClass.ViewportFlagsOverrideClear =
        ImGuiViewportFlags_NoDecoration | ImGuiViewportFlags_NoTaskBarIcon;
    ImGui::SetNextWindowClass(&windowClass);
    if (m_LibraryWindowFocusRequested) {
        ImGui::SetNextWindowFocus();
        m_LibraryWindowFocusRequested = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool visible = ImGui::Begin(
        "###StackLibraryWindow",
        &m_LibraryWindowOpen,
        ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse);
    m_LibraryWindowPlacementInitialized = true;

    ImGuiViewport* libraryViewport = ImGui::GetWindowViewport();
    GLFWwindow* libraryNativeWindow = libraryViewport != nullptr
        ? static_cast<GLFWwindow*>(libraryViewport->PlatformHandle)
        : nullptr;
    const std::uint64_t appearanceRevision = m_Appearance
        ? m_Appearance->GetRevision()
        : 0;
    if (libraryNativeWindow != nullptr &&
        libraryNativeWindow != m_Window &&
        (libraryNativeWindow != m_LibraryNativeWindow ||
         appearanceRevision != m_LibraryNativeAppearanceRevision)) {
        NativeWindowTheme::SetOwner(libraryNativeWindow, m_Window);
        NativeWindowTheme::EnsureNotTopMost(libraryNativeWindow);
        ApplyNativeTitleBarTheme(libraryNativeWindow, m_Appearance.get());
#if defined(_WIN32)
        HWND hwnd = glfwGetWin32Window(libraryNativeWindow);
        if (hwnd) {
            SetWindowTextW(hwnd, L"");
            SendMessage(hwnd, WM_SETICON, ICON_SMALL, 0);
            SendMessage(hwnd, WM_SETICON, ICON_BIG, 0);
        }
#endif
        m_LibraryNativeWindow = libraryNativeWindow;
        m_LibraryNativeAppearanceRevision = appearanceRevision;
    }

    if (visible) {
        const bool projectLoadActive =
            ActiveProjectLoadPhase() != LibraryToEditorProjectLoadPhase::None;
        if (projectLoadActive) {
            ImGui::BeginDisabled();
        }
        m_Library.RenderUI(
            m_Editor,
            &m_Composite,
            m_Appearance.get(),
            &m_RequestedTab,
            RootTabRawLab,
            [this](const std::string& projectFileName) {
                BeginLibraryToEditorProjectLoad(projectFileName);
            });
        if (projectLoadActive) {
            ImGui::EndDisabled();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}



void AppShell::RenderHeaderSettingsPopup(bool buttonHovered) {
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
    // Graph is the tallest fixed settings page. Background libraries can grow
    // without bound and scroll independently, so they should not force the
    // shared window to occupy most of the display.
    const float popupHeight = std::clamp(viewport->Size.y * 0.70f, 620.0f, 780.0f);

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
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (ImGui::Button("Close", ImVec2(74.0f, 0.0f))) {
        popupOpen = false;
    }
    ImGui::PopStyleColor(3);
    ImGui::Dummy(ImVec2(0.0f, 12.0f));

    AppSettingsPopup::RenderContents(
        m_Appearance.get(),
        m_Editor,
        m_UpdateManager.get(),
        m_LegalManager.get(),
        m_SettingsPopupState);

    if (m_SettingsPopupState.requestShowLegalGate) {
        m_SettingsPopupState.requestShowLegalGate = false;
        m_SettingsPopupOpen = false;
        m_SettingsPopupOpenedAt = 0.0;
        m_ShowLegalGateReview = true;
    }

    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(4);

    const bool openedThisFrame = (ImGui::GetTime() - m_SettingsPopupOpenedAt) <= static_cast<double>(ImGui::GetIO().DeltaTime) + 0.001;
    const bool outsideClick =
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) &&
        !openedThisFrame &&
        !popupHovered &&
        !buttonHovered;
    m_SettingsPopupOpen = popupOpen && !outsideClick;
    if (!m_SettingsPopupOpen) {
        m_SettingsPopupOpenedAt = 0.0;
    }
}

void AppShell::OnTabChanged(int oldTab, int newTab) {
    (void)oldTab;
    (void)newTab;
    ImGui::ClosePopupsExceptModals();
    m_ActiveSyncLayerId.clear();
}

void AppShell::RequestRootTabTransition(int newTab) {
    if (m_WorkspaceSwitcher.held) EndWorkspaceSwitcher(false);
    m_WorkspaceSwitcher.progress = 0;
    m_Editor->EndWorkspacePreview();
    if (newTab == RootTabComposite) {
        newTab = RootTabEditor;
    }
    if (!IsFadeableRootTab(newTab)) {
        return;
    }
    if (ImGui::GetTopMostPopupModal() != nullptr) {
        return;
    }
    if (m_RootTabBodyFadeActive) {
        m_RootTabBodyFadeQueuedTab =
            newTab == m_RootTabBodyFadeToTab ? -1 : newTab;
        return;
    }
    if (newTab == m_CurrentTabId) {
        return;
    }
    BeginRootTabBodyFade(m_CurrentTabId, newTab);
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
    m_RootTabBodyFadeCommitted = false;
}

float AppShell::ConsumeRootTabBodyFadeAlpha(int* outRenderTabId) {
    if (outRenderTabId) {
        *outRenderTabId = m_CurrentTabId;
    }

    if (!m_RootTabBodyFadeActive) {
        return 1.0f;
    }

    const auto smoothStep = [](float value) {
        const float t = std::clamp(value, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    const double elapsed = ImGui::GetTime() - m_RootTabBodyFadeStartedAt;
    if (elapsed < kRootTabBodyFadeOutSeconds) {
        if (outRenderTabId) {
            *outRenderTabId = m_RootTabBodyFadeFromTab;
        }
        const float t = std::clamp(
            static_cast<float>(elapsed / kRootTabBodyFadeOutSeconds),
            0.0f,
            1.0f);
        return 1.0f - smoothStep(t);
    }

    if (!m_RootTabBodyFadeCommitted) {
        if (!CanChangeRootTab(m_RootTabBodyFadeFromTab, m_RootTabBodyFadeToTab)) {
            m_RootTabBodyFadeToTab = m_RootTabBodyFadeFromTab;
        } else {
            OnTabChanged(m_RootTabBodyFadeFromTab, m_RootTabBodyFadeToTab);
            m_CurrentTabId = m_RootTabBodyFadeToTab;
        }
        m_RootTabBodyFadeCommitted = true;
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
        m_RootTabBodyFadeCommitted = false;
        if (outRenderTabId) {
            *outRenderTabId = m_CurrentTabId;
        }
        const int queuedTab = m_RootTabBodyFadeQueuedTab;
        m_RootTabBodyFadeQueuedTab = -1;
        if (queuedTab != -1 && queuedTab != m_CurrentTabId) {
            BeginRootTabBodyFade(m_CurrentTabId, queuedTab);
        }
        return 1.0f;
    }

    return smoothStep(fadeInT);
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
        for (auto& workspace : m_ProjectWorkspaces)
            workspace->editor->CloseDetachedPreviewFullscreen();
    });
    runPhase("platform-hooks-uninstall", [&]() {
        UninstallDetachedPreviewPlatformHooks();
    });
    FileDialogs::SetOwnerWindow(nullptr);
#if defined(_WIN32)
    SetFramelessMainWindowCursorReleaseCallback({});
#endif
    // Shared-pool work captures module-owned cancellation state while it
    // finishes. Join those tasks before any module begins clearing the state
    // they can still observe. Main-thread completions are discarded by the
    // task-system shutdown and therefore cannot run against torn-down modules.
    runPhase("task-system-shutdown", [&]() {
        Async::TaskSystem::Get().Shutdown();
    });
    runPhase("queue-renderer-shutdown", [&]() {
        m_QueueRenderer.Shutdown();
    });
    runPhase("editor-shutdown", [&]() {
        for (auto& workspace : m_ProjectWorkspaces) workspace->editor->Shutdown();
        for (auto& workspace : m_RetiredProjectWorkspaces) workspace->editor->Shutdown();
        m_RetiredProjectWorkspaces.clear();
        m_ProjectWorkspaces.clear();
        m_Editor = nullptr;
    });
    runPhase("queue-shutdown", [&]() {
        m_Queue.Shutdown();
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
        if (m_RawTabTexture) {
            glDeleteTextures(1, &m_RawTabTexture);
            m_RawTabTexture = 0;
        }
        if (m_RawLabTabTexture) {
            glDeleteTextures(1, &m_RawLabTabTexture);
            m_RawLabTabTexture = 0;
        }
        if (m_FileNewTexture) {
            glDeleteTextures(1, &m_FileNewTexture);
            m_FileNewTexture = 0;
        }
        if (m_FileOpenProjectTexture) {
            glDeleteTextures(1, &m_FileOpenProjectTexture);
            m_FileOpenProjectTexture = 0;
        }
        if (m_FileSaveTexture) {
            glDeleteTextures(1, &m_FileSaveTexture);
            m_FileSaveTexture = 0;
        }
        if (m_FileExitProgramTexture) {
            glDeleteTextures(1, &m_FileExitProgramTexture);
            m_FileExitProgramTexture = 0;
        }
        if (m_ProgramIconTexture) {
            glDeleteTextures(1, &m_ProgramIconTexture);
            m_ProgramIconTexture = 0;
        }
        ReleaseBackgroundImageTexture();
    });
    runPhase("composite-shutdown", [&]() {
        m_Composite.Shutdown();
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
        GraphNativeCursor::Shutdown(m_Window);
        m_WorkspaceCompositor.Shutdown();
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
    if(m_Editor->IsAutoBracketWorkspace())return;
    if(m_Editor->IsBracketingPresentationActive())return;
    if(tabId==RootTabMultiFrame){m_Editor->OpenBracketingTool();tabId=RootTabRawLab;}
    m_RequestedTab = (tabId == RootTabComposite) ? RootTabEditor : tabId;
}

bool AppShell::CanChangeRootTab(int oldTab, int newTab) {
    if (m_ActiveProjectWorkspace == m_GalleryWorkspaceId && newTab == RootTabEditor) {
        // Starting a graph from the global browser also needs a project tab.
        // Preserve the in-flight Alt/root animation while assigning its owner.
        if (!ActivateProjectWorkspace(CreateProjectWorkspace(), true)) return false;
        return m_Editor->LeaveRawWorkspaceRootTab(true);
    }
    if (!CrossesRawWorkspaceLifecycleBoundary(oldTab, newTab)) {
        return true;
    }
    if (IsRawProjectWorkspaceRootTab(oldTab)) {
        return m_Editor->LeaveRawWorkspaceRootTab(newTab == RootTabEditor);
    }
    return m_Editor->EnterRawWorkspaceRootTab();
}

void AppShell::OnFileDrop(GLFWwindow* window, int count, const char** paths) {
    AppShell* app = static_cast<AppShell*>(glfwGetWindowUserPointer(window));
    if (app) {
        app->HandleDrop(count, paths);
    }
}

void AppShell::HandleDrop(int count, const char** paths) {
    if (!m_Editor || m_NotificationPresenter.BlocksInput()) return;
    if (m_Editor->IsAutoBracketWorkspace()) return;
    if(m_Editor->IsBracketingPresentationActive())return;
    if (m_WorkspaceSwitcher.Visible() || m_ToolSwitcher.Visible()) return;
    if (count <= 0 || !paths) return;
    if (m_ActiveProjectWorkspace == m_GalleryWorkspaceId) {
        if (!CanSwitchProjectWorkspace() ||
            !ActivateProjectWorkspace(CreateProjectWorkspace())) return;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    if (m_Window) {
        glfwGetCursorPos(m_Window, &cursorX, &cursorY);
    }

    if (m_CurrentTabId == RootTabMultiFrame || (IsRawWorkspaceRootTab(m_CurrentTabId) && m_Editor->IsBracketingToolActive())) {
        std::vector<std::string> multiFramePaths;
        multiFramePaths.reserve(count);
        for (int i = 0; i < count; ++i) {
            if (paths[i] && paths[i][0] != '\0') {
                multiFramePaths.emplace_back(paths[i]);
            }
        }
        if (m_Editor->HandleMultiFrameFileDrop(
                multiFramePaths,
                static_cast<float>(cursorX),
                static_cast<float>(cursorY))) {
            return;
        }
    }

    if (IsRawWorkspaceRootTab(m_CurrentTabId) &&
        m_Editor->IsRawWorkspaceLockedByEditorProject()) {
        m_Editor->ShowUiNotification(
            UiNotificationSeverity::Info,
            "Switch to the RAW workspace before importing files here.",
            "raw-workspace-locked-file-drop");
        return;
    }

    if (m_CurrentTabId == RootTabEditor || IsRawWorkspaceRootTab(m_CurrentTabId)) {
        std::vector<std::string> graphImagePaths;
        graphImagePaths.reserve(count);
        for (int i = 0; i < count; ++i) {
            const std::string path = paths[i] ? paths[i] : "";
            if (IsSupportedDroppedImagePath(path)) {
                graphImagePaths.push_back(path);
            }
        }

        if (!graphImagePaths.empty() &&
            m_Editor->HandleGraphFileDrop(graphImagePaths, static_cast<float>(cursorX), static_cast<float>(cursorY))) {
            for (int i = 0; i < count; ++i) {
                const std::string path = paths[i] ? paths[i] : "";
                if (IsSupportedDroppedImagePath(path)) {
                    continue;
                }
                LibraryManager::Get().RequestImportAndLoad(path, m_Editor, nullptr, [this](int tabId) {
                    RequestTabSwitch(tabId);
                });
            }
            return;
        }
    }

    for (int i = 0; i < count; ++i) {
        const std::string path = paths[i] ? paths[i] : "";
        LibraryManager::Get().RequestImportAndLoad(path, m_Editor, nullptr, [this](int tabId) {
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
    ImGui::GetIO().IniFilename = nullptr;
    
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
