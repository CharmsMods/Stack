#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "AppShell.h"
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
#include <filesystem>
#include <thread>
#include <chrono>

namespace {

void ApplyNativeTitleBarTheme(GLFWwindow* window) {
    NativeWindowTheme::Apply(window);
}

enum RootTabId {
    RootTabLibrary = 0,
    RootTabEditor = 1,
    RootTabComposite = 2
};

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
constexpr bool kLibraryLoadTransitionDiagnostics = true;

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

AppShell::AppShell()
    : m_Window(nullptr)
    , m_SplashWindow(nullptr)
    , m_SplashTexture(0)
    , m_EditorTabTexture(0)
    , m_LibraryTabTexture(0)
    , m_HeaderSettingsTexture(0)
    , m_BackgroundImageTexture(0)
    , m_BackgroundImageWidth(0)
    , m_BackgroundImageHeight(0)
    , m_LockedScrubCursorActive(false)
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

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    m_Window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_Window) {
        std::cerr << "Failed to create an OpenGL 4.3 core window/context.\n";
        glfwTerminate();
        return false;
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
    ApplyNativeTitleBarTheme(m_Window);
    m_UpdateManager = std::make_unique<AppUpdate::UpdateManager>(
        [this](UiNotificationSeverity severity, const std::string& message, const std::string& dedupeKey) {
            PushToast(severity, message, dedupeKey);
        },
        [this]() {
            if (m_Window != nullptr) {
                glfwSetWindowShouldClose(m_Window, GLFW_TRUE);
            }
            m_IsRunning = false;
        });
    m_UpdateManager->Initialize();

    ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
    ImGui_ImplOpenGL3_Init("#version 430 core");

    m_EditorTabTexture = LoadEmbeddedPngTexture(
        EmbeddedTabIcons::Editor_png_data,
        EmbeddedTabIcons::Editor_png_size,
        "Editor");
    m_LibraryTabTexture = LoadEmbeddedPngTexture(
        EmbeddedTabIcons::Library_png_data,
        EmbeddedTabIcons::Library_png_size,
        "Library");
    m_HeaderSettingsTexture = LoadEmbeddedPngTexture(
        EmbeddedTabIcons::Settings_png_data,
        EmbeddedTabIcons::Settings_png_size,
        "HeaderSettings");

    glfwSetWindowUserPointer(m_Window, this);
    glfwSetDropCallback(m_Window, OnFileDrop);

    Async::TaskSystem::Get().Initialize();
    m_Editor.Initialize(m_Window, m_Appearance.get());
    m_Composite.Initialize();
    // m_Library.Initialize(); // We will call this manually in the splash screen

    ShowSplashScreen();

    if (!loadedAppearance) {
        m_Appearance->Save();
    }

    glfwMaximizeWindow(m_Window);
    glfwShowWindow(m_Window);
    m_MainWindowShownTime = glfwGetTime();
    m_IsRunning = true;
    if (m_UpdateManager) {
        m_UpdateManager->StartBackgroundCheck();
    }
    return true;
}

void AppShell::Run() {
    while (!glfwWindowShouldClose(m_Window) && m_IsRunning) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuiExtras::BeginFrameInputRouting();
        Async::TaskSystem::Get().PumpMainThreadTasks(4);

        const double secondsSinceMainWindowShown = glfwGetTime() - m_MainWindowShownTime;
        const bool allowAssetThumbnailUpload =
            m_CurrentTabId == RootTabLibrary && secondsSinceMainWindowShown > 2.0;
        LibraryManager::Get().UploadLibraryTextures(2, allowAssetThumbnailUpload ? 1 : 0);

        std::string savedProjectFileName;
        std::string savedProjectKind;
        if (LibraryManager::Get().ConsumeSavedProjectEvent(savedProjectFileName, savedProjectKind)) {
            (void)savedProjectKind;
            (void)savedProjectFileName;
        }
        ConsumeUiNotifications();

        RenderUI();
        SyncCursorCaptureRequest();

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
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwSwapBuffers(m_Window);
        OnFramePresented();
    }
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
        request.mode == ImGuiExtras::CursorCaptureMode::LockedScrub &&
        windowFocused;

    const auto releaseCapture = [this]() {
        if (!m_LockedScrubCursorActive || !m_Window) {
            return;
        }
        glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        const ImVec2 restoreLocal = ScreenToWindowCursorPos(m_Window, m_LockedScrubCursorRestoreScreenPos);
        glfwSetCursorPos(m_Window, restoreLocal.x, restoreLocal.y);
        m_LockedScrubCursorActive = false;
        m_LockedScrubCursorAnchorScreenPos = ImVec2(0.0f, 0.0f);
        m_LockedScrubCursorRestoreScreenPos = ImVec2(0.0f, 0.0f);
    };

    if (!shouldCapture) {
        releaseCapture();
        return;
    }

    if (!m_LockedScrubCursorActive) {
        m_LockedScrubCursorActive = true;
        m_LockedScrubCursorRestoreScreenPos = request.restoreScreenPos;
        glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    }

    m_LockedScrubCursorAnchorScreenPos = request.anchorScreenPos;
    const ImVec2 anchorLocal = ScreenToWindowCursorPos(m_Window, m_LockedScrubCursorAnchorScreenPos);
    glfwSetCursorPos(m_Window, anchorLocal.x, anchorLocal.y);
}

void AppShell::ReleaseBackgroundImageTexture() {
    if (m_BackgroundImageTexture != 0) {
        glDeleteTextures(1, &m_BackgroundImageTexture);
        m_BackgroundImageTexture = 0;
    }
    m_BackgroundImageWidth = 0;
    m_BackgroundImageHeight = 0;
    m_BackgroundImageTexturePath.clear();
}

bool AppShell::LoadBackgroundImageTextureFromPath(const std::filesystem::path& path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    // Match the direct UI image convention used by other upright image surfaces:
    // decode without an stb flip, then render through flipped V UVs in ImGui.
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
    m_BackgroundImageTexturePath = path.lexically_normal().string();
    return true;
}

void AppShell::SyncBackgroundImageTexture() {
    if (m_Appearance == nullptr) {
        ReleaseBackgroundImageTexture();
        m_BackgroundImageTextureRevision = 0;
        return;
    }

    const bool enabled = m_Appearance->GetBackgroundImageEnabled();
    const std::filesystem::path resolvedPath = enabled ? m_Appearance->GetResolvedBackgroundImagePath() : std::filesystem::path();
    const std::string normalizedPath = resolvedPath.empty() ? std::string() : resolvedPath.lexically_normal().string();
    const std::uint64_t revision = m_Appearance->GetBackgroundImageRevision();

    if (!enabled || normalizedPath.empty()) {
        ReleaseBackgroundImageTexture();
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

    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec) || ec) {
        ReleaseBackgroundImageTexture();
        m_BackgroundImageTextureRevision = revision;
        m_Appearance->SetBackgroundImageRuntimeStatus("Managed background image file is missing.");
        return;
    }

    if (!LoadBackgroundImageTextureFromPath(resolvedPath)) {
        ReleaseBackgroundImageTexture();
        m_BackgroundImageTextureRevision = revision;
        m_Appearance->SetBackgroundImageRuntimeStatus("Failed to decode or upload the background image.");
        return;
    }

    m_BackgroundImageTextureRevision = revision;
    m_Appearance->SetBackgroundImageRuntimeStatus("");
}

void AppShell::RenderBackgroundImage(const ImVec2& regionMin, const ImVec2& regionSize) {
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
    const float strength = std::clamp(m_Appearance->GetBackgroundImageStrength(), 0.0f, 1.0f);
    const ImU32 tint = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, strength));

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(regionMin, ImVec2(regionMin.x + regionSize.x, regionMin.y + regionSize.y), true);
    drawList->AddImage(
        (ImTextureID)(intptr_t)m_BackgroundImageTexture,
        drawMin,
        drawMax,
        ImVec2(0.0f, 1.0f),
        ImVec2(1.0f, 0.0f),
        tint);
    drawList->PopClipRect();
}

void AppShell::RenderUI() {
    if (m_Appearance) {
        m_Appearance->ApplyCurrentTheme(ImGui::GetIO(), ImGui::GetStyle());
        ApplyNativeTitleBarTheme(m_Window);
    }

    SyncBackgroundImageTexture();
    const bool wallpaperSurfaces = m_Appearance && m_Appearance->GetBackgroundImageEnabled();
    const StackAppearance::RuntimeSurfacePalette surfacePalette =
        m_Appearance ? m_Appearance->GetRuntimeSurfacePalette() : StackAppearance::RuntimeSurfacePalette{};

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    // Root Fullscreen Window for Tabbed Workspace
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar 
                                   | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize 
                                   | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus 
                                   | ImGuiWindowFlags_NoNavFocus;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    
    ImGui::Begin("ModularStudioMain", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    RenderBackgroundImage(ImGui::GetWindowPos(), ImGui::GetWindowSize());

    const std::vector<RootTabDescriptor> tabs = {
        { RootTabLibrary, "Library", m_LibraryTabTexture, [this]() {
            m_Library.RenderUI(
                &m_Editor,
                &m_Composite,
                m_Appearance.get(),
                &m_RequestedTab,
                [this](const std::string& projectFileName) {
                    BeginLibraryToEditorProjectLoad(projectFileName);
                });
        } },
        { RootTabEditor, "Editor", m_EditorTabTexture, [this]() { m_Editor.RenderUI(); } }
    };

    TickLibraryToEditorProjectLoadTransition();
    const bool loadTransitionActive = m_LoadTransitionPhase != LibraryToEditorProjectLoadPhase::None;

    if (!loadTransitionActive && m_RequestedTab != -1 && m_RequestedTab != m_CurrentTabId) {
        OnTabChanged(m_CurrentTabId, m_RequestedTab);
        m_CurrentTabId = m_RequestedTab;
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

    ImGuiIO& io = ImGui::GetIO();
    const double now = ImGui::GetTime();
    m_ChromeRevealTime = now;
    m_ChromeHiddenT = std::max(0.0f, m_ChromeHiddenT - io.DeltaTime * 8.0f);

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
            ImGui::InvisibleButton("##TabIcon", ImVec2(hitSize, hitSize));
            const bool iconHovered = ImGui::IsItemHovered();
            const bool iconHeld = ImGui::IsItemActive();
            clicked = ImGui::IsItemClicked();
            const float iconOffset = (hitSize - iconSize) * 0.5f;
            const ImVec2 iconMin(cursor.x + iconOffset, cursor.y + iconOffset);
            const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
            const ImU32 iconTint = (selected || iconHeld)
                ? IM_COL32(255, 255, 255, 255)
                : (iconHovered ? IM_COL32(220, 220, 220, 230) : IM_COL32(150, 150, 150, 165));
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

            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            if (selected) {
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(min.x, max.y - 2.0f), ImVec2(max.x, max.y), selectedAccentColor);
            }
            ImGui::PopStyleColor(3);
        }

        ImGui::PopID();
        return clicked;
    };

    const float chromeHeight = 50.0f;
    const float chromeOffset = -chromeHeight * (1.0f - std::pow(1.0f - m_ChromeHiddenT, 3.0f));
    const float chromeAlpha = 1.0f - m_ChromeHiddenT;

    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, chromeAlpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        wallpaperSurfaces ? surfacePalette.chromeSurface : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::SetCursorPosY(chromeOffset);
    ImGui::BeginChild("GlobalProgramBar", ImVec2(0.0f, chromeHeight), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 0.0f));
    for (size_t i = 0; i < tabs.size(); ++i) {
        const RootTabDescriptor& tab = tabs[i];
        if (i > 0) {
            ImGui::SameLine();
        }
        if (renderTabButton(tab, m_CurrentTabId == tab.id) && !loadTransitionActive && tab.id != m_CurrentTabId) {
            OnTabChanged(m_CurrentTabId, tab.id);
            m_CurrentTabId = tab.id;
        }
    }
    ImGui::PopStyleVar();

    ImVec2 settingsGearMin(0.0f, 0.0f);
    ImVec2 settingsGearMax(0.0f, 0.0f);
    bool settingsGearHovered = false;
    if (m_HeaderSettingsTexture != 0) {
        constexpr float hitSize = 28.0f;
        constexpr float iconSize = 18.0f;
        const float gearX = ImGui::GetWindowContentRegionMax().x - hitSize;
        if (gearX > ImGui::GetCursorPosX()) {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetCursorPosX(gearX);
        }
        const float gearY = (chromeHeight - hitSize) * 0.5f;
        ImGui::SetCursorPosY(gearY);

        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        settingsGearMin = cursor;
        settingsGearMax = ImVec2(cursor.x + hitSize, cursor.y + hitSize);
        ImGui::InvisibleButton("##HeaderSettingsGear", ImVec2(hitSize, hitSize));
        settingsGearHovered = ImGui::IsItemHovered();
        const bool gearHeld = ImGui::IsItemActive();
        const bool gearClicked = ImGui::IsItemClicked();

        const ImRect gearRect(settingsGearMin, settingsGearMax);
        const bool gearSelected = m_SettingsPopupOpen;
        const ImVec4 bgColor = gearSelected
            ? blendColor(ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive), 0.55f)
            : (settingsGearHovered
                ? blendColor(ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered), 0.35f)
                : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        if (bgColor.w > 0.001f) {
            ImGui::GetWindowDrawList()->AddRectFilled(
                gearRect.Min,
                gearRect.Max,
                ImGui::GetColorU32(bgColor),
                8.0f);
        }

        const float iconOffset = (hitSize - iconSize) * 0.5f;
        const ImVec2 iconMin(cursor.x + iconOffset, cursor.y + iconOffset);
        const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
        const ImU32 iconTint = (gearSelected || gearHeld)
            ? IM_COL32(255, 255, 255, 255)
            : (settingsGearHovered ? IM_COL32(220, 220, 220, 230) : IM_COL32(150, 150, 150, 165));
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
            m_SettingsPopupOpen = !m_SettingsPopupOpen;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    RenderHeaderSettingsPopup(settingsGearMin, settingsGearMax, settingsGearHovered);

    if (!loadTransitionActive) {
        m_Library.RenderGlobalPopups();
        RenderEditorSavePrompts();
    }

    if (loadTransitionActive) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        const ImVec2 bodyPos = ImGui::GetCursorScreenPos();
        const ImVec2 bodySize = ImGui::GetContentRegionAvail();
        (void)bodyPos;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            wallpaperSurfaces
                ? surfacePalette.panelSurface
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
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        for (const RootTabDescriptor& tab : tabs) {
            if (tab.id == m_CurrentTabId) {
                tab.renderBody();
                break;
            }
        }
    }

    RenderToasts();

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
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float popupWidth = std::clamp(viewport->WorkSize.x * 0.65f, 780.0f, 1024.0f);
    const float popupHeight = std::clamp(viewport->WorkSize.y * 0.80f, 580.0f, 920.0f);

    ImVec2 popupPos(gearButtonMax.x - popupWidth, gearButtonMax.y + 10.0f);
    popupPos.x = std::clamp(
        popupPos.x,
        viewport->WorkPos.x + 16.0f,
        viewport->WorkPos.x + viewport->WorkSize.x - popupWidth - 16.0f);
    popupPos.y = std::clamp(
        popupPos.y,
        viewport->WorkPos.y + 16.0f,
        viewport->WorkPos.y + viewport->WorkSize.y - popupHeight - 16.0f);

    ImGui::SetNextWindowPos(popupPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(popupWidth, popupHeight), ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, m_Appearance->GetEffectivePopupBackgroundColor());
    ImGui::PushStyleColor(ImGuiCol_Border, m_Appearance->GetRuntimeSurfacePalette().border);

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
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - 58.0f));
    if (ImGui::Button("Close", ImVec2(58.0f, 0.0f))) {
        popupOpen = false;
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    AppSettingsPopup::RenderContents(m_Appearance.get(), &m_Editor, m_UpdateManager.get(), m_SettingsPopupState);

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    const bool outsideClick =
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) &&
        !popupHovered &&
        !gearButtonHovered;
    m_SettingsPopupOpen = popupOpen && !outsideClick;
}

void AppShell::BeginLibraryToEditorProjectLoad(const std::string& projectFileName) {
    if (projectFileName.empty() ||
        m_LoadTransitionPhase != LibraryToEditorProjectLoadPhase::None ||
        Async::IsBusy(LibraryManager::Get().GetProjectLoadTaskState())) {
        return;
    }

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

        if (m_Editor.IsDeferredLoadedProjectReadyForReveal()) {
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
                LibraryManager::Get().RequestSaveProject(
                    m_Editor.GetCurrentProjectName(),
                    &m_Editor,
                    m_Editor.GetCurrentProjectFileName());
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
            LibraryManager::Get().RequestSaveProject(newName, &m_Editor, "");
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
    (void)oldTab;
    (void)newTab;
    m_ActiveSyncLayerId.clear();
}

void AppShell::Shutdown() {
    if (!m_Window) return;

    m_Editor.CloseDetachedPreviewFullscreen();
    if (m_Appearance) {
        m_Appearance->Save();
    }
    if (m_EditorTabTexture) {
        glDeleteTextures(1, &m_EditorTabTexture);
        m_EditorTabTexture = 0;
    }
    if (m_LibraryTabTexture) {
        glDeleteTextures(1, &m_LibraryTabTexture);
        m_LibraryTabTexture = 0;
    }
    if (m_HeaderSettingsTexture) {
        glDeleteTextures(1, &m_HeaderSettingsTexture);
        m_HeaderSettingsTexture = 0;
    }
    ReleaseBackgroundImageTexture();
    m_Composite.Shutdown();
    Async::TaskSystem::Get().Shutdown();
    if (m_LockedScrubCursorActive) {
        glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        m_LockedScrubCursorActive = false;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(m_Window);
    glfwTerminate();
    m_Window = nullptr;
}

void AppShell::RequestTabSwitch(int tabId) {
    m_RequestedTab = (tabId == RootTabComposite) ? RootTabEditor : tabId;
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

    if (m_CurrentTabId == RootTabEditor) {
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
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); 
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    m_SplashWindow = glfwCreateWindow(splashW, splashH, "Stack Loading", nullptr, nullptr);
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

    // Trigger the library refresh with our HUD callback
    LibraryManager::Get().RefreshLibrary(progressCallback);

    // Keep the splash perceptible without making a fast startup wait on decoration.
    int totalCount = LibraryManager::Get().GetProjectCount();
    while (glfwGetTime() - startTime < 0.35) {
        progressCallback(totalCount, totalCount, "Ready");
        
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
