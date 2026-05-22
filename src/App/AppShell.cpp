#include "AppShell.h"
#include "Async/TaskSystem.h"
#include "Renderer/GLLoader.h"
#include "settings/AppearanceTheme.h"
#include "SettingsModule.h"
#include <GLFW/glfw3.h>
#include "imgui.h"
#include <imgui_internal.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <functional>
#include <iostream>
#include <vector>
#include "../Library/LibraryManager.h"
#include "ThirdParty/stb_image.h"
#include "Renderer/GLHelpers.h"
#include "Resources/EmbeddedSplash.h"
#include "Resources/EmbeddedTabIcons.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <thread>
#include <chrono>

namespace {

enum RootTabId {
    RootTabLibrary = 0,
    RootTabEditor = 1,
    RootTabComposite = 2,
    RootTabRender = 3,
    RootTabBundler = 4,
    RootTabSettings = 5
};

struct RootTabDescriptor {
    int id = -1;
    const char* label = "";
    unsigned int iconTexture = 0;
    std::function<void()> renderBody;
};

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
    , m_RenderTabTexture(0)
    , m_ProgramLogoTexture(0)
    , m_IsRunning(false)
    , m_FirstTimeLayout(true) {}

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
    m_RenderTabTexture = LoadEmbeddedPngTexture(
        EmbeddedTabIcons::Render_png_data,
        EmbeddedTabIcons::Render_png_size,
        "Render");
    m_ProgramLogoTexture = LoadEmbeddedPngTexture(
        EmbeddedTabIcons::CharmLogo_png_data,
        EmbeddedTabIcons::CharmLogo_png_size,
        "Charm logo");

    glfwSetWindowUserPointer(m_Window, this);
    glfwSetDropCallback(m_Window, OnFileDrop);

    Async::TaskSystem::Get().Initialize();
    m_Editor.Initialize(m_Window);
    m_Composite.Initialize();
    // m_Library.Initialize(); // We will call this manually in the splash screen
    m_RenderTab.Initialize();
    m_Bundler.Initialize();

    ShowSplashScreen();

    if (!loadedAppearance) {
        m_Appearance->Save();
    }

    m_Settings.Initialize(m_Appearance.get());

    // Now that we are back in the main context, upload the library textures
    LibraryManager::Get().UploadLibraryTextures();

    glfwMaximizeWindow(m_Window);
    glfwShowWindow(m_Window);
    m_IsRunning = true;
    return true;
}

void AppShell::Run() {
    while (!glfwWindowShouldClose(m_Window) && m_IsRunning) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        Async::TaskSystem::Get().PumpMainThreadTasks();
        LibraryManager::Get().UploadLibraryTextures();

        std::string savedProjectFileName;
        std::string savedProjectKind;
        if (LibraryManager::Get().ConsumeSavedProjectEvent(savedProjectFileName, savedProjectKind)) {
            (void)savedProjectKind;
            (void)savedProjectFileName;
        }

        RenderUI();

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
    }
}

void AppShell::RenderUI() {
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

    const std::vector<RootTabDescriptor> tabs = {
        { RootTabLibrary, "Library", m_LibraryTabTexture, [this]() { m_Library.RenderUI(&m_Editor, &m_RenderTab, &m_Composite, &m_RequestedTab); } },
        { RootTabEditor, "Editor", m_EditorTabTexture, [this]() { m_Editor.RenderUI(); } },
        { RootTabRender, "Render", m_RenderTabTexture, [this]() { m_RenderTab.RenderUI(); } },
        { RootTabBundler, "Bundler", 0, [this]() { m_Bundler.RenderUI(); } },
        { RootTabSettings, "Settings", 0, [this]() { m_Settings.RenderUI(); } }
    };

    if (m_RequestedTab != -1 && m_RequestedTab != m_CurrentTabId) {
        OnTabChanged(m_CurrentTabId, m_RequestedTab);
        m_CurrentTabId = m_RequestedTab;
    }
    m_RequestedTab = -1;

    auto renderTabButton = [&](const RootTabDescriptor& tab, bool selected) {
        const ImVec4 baseButton = selected
            ? ImVec4(0.20f, 0.22f, 0.26f, 1.0f)
            : ImVec4(0.12f, 0.13f, 0.15f, 1.0f);
        const ImVec4 hoverButton = ImVec4(0.24f, 0.26f, 0.30f, 1.0f);
        const ImVec4 activeButton = ImVec4(0.28f, 0.30f, 0.34f, 1.0f);
        const ImVec4 selectedAccent = ImVec4(0.65f, 0.72f, 0.85f, 1.0f);
        const ImU32 selectedAccentColor = ImGui::GetColorU32(selectedAccent);
        const float tabHeight = 28.0f;

        ImGui::PushID(tab.id);
        ImGui::PushStyleColor(ImGuiCol_Button, baseButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeButton);

        bool clicked = false;
        if (tab.iconTexture != 0) {
            clicked = ImGui::ImageButton("##TabIcon", (ImTextureID)(intptr_t)tab.iconTexture, ImVec2(18.0f, 18.0f));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tab.label);
            }
        } else {
            const float textWidth = ImGui::CalcTextSize(tab.label).x + 18.0f;
            clicked = ImGui::Button(tab.label, ImVec2(textWidth, tabHeight));
        }

        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        if (selected) {
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(min.x, max.y - 2.0f), ImVec2(max.x, max.y), selectedAccentColor);
        }

        ImGui::PopStyleColor(3);
        ImGui::PopID();
        return clicked;
    };

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 0.0f));
    for (size_t i = 0; i < tabs.size(); ++i) {
        const RootTabDescriptor& tab = tabs[i];
        if (i > 0) {
            ImGui::SameLine();
        }
        if (renderTabButton(tab, m_CurrentTabId == tab.id) && tab.id != m_CurrentTabId) {
            OnTabChanged(m_CurrentTabId, tab.id);
            m_CurrentTabId = tab.id;
        }
    }
    ImGui::PopStyleVar();

    if (m_ProgramLogoTexture != 0) {
        constexpr float logoSize = 20.0f;
        constexpr float logoRightPadding = 10.0f;
        const float logoX = ImGui::GetWindowContentRegionMax().x - logoSize - logoRightPadding;
        if (logoX > ImGui::GetCursorPosX()) {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetCursorPosX(logoX);
        }
        const float logoY = ImGui::GetCursorPosY() + 3.0f;
        ImGui::SetCursorPosY(logoY);
        ImGui::Image((ImTextureID)(intptr_t)m_ProgramLogoTexture, ImVec2(logoSize, logoSize));
    }

    m_Library.RenderGlobalPopups();
    RenderEditorSavePrompts();

    for (const RootTabDescriptor& tab : tabs) {
        if (tab.id == m_CurrentTabId) {
            tab.renderBody();
            break;
        }
    }

    ImGui::End(); // End ModularStudioMain
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
    if (m_RenderTabTexture) {
        glDeleteTextures(1, &m_RenderTabTexture);
        m_RenderTabTexture = 0;
    }
    if (m_ProgramLogoTexture) {
        glDeleteTextures(1, &m_ProgramLogoTexture);
        m_ProgramLogoTexture = 0;
    }
    m_Settings.Shutdown();
    m_Bundler.Shutdown();
    m_RenderTab.Shutdown();
    m_Composite.Shutdown();
    Async::TaskSystem::Get().Shutdown();
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
                LibraryManager::Get().RequestImportAndLoad(path, &m_Editor, &m_RenderTab, nullptr, [this](int tabId) {
                    RequestTabSwitch(tabId);
                });
            }
            return;
        }
    }

    for (int i = 0; i < count; ++i) {
        const std::string path = paths[i] ? paths[i] : "";
        LibraryManager::Get().RequestImportAndLoad(path, &m_Editor, &m_RenderTab, nullptr, [this](int tabId) {
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

    // Ensure the splash stays visible for at least 2 seconds total
    int totalCount = LibraryManager::Get().GetProjectCount();
    while (glfwGetTime() - startTime < 2.0) {
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
