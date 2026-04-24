#include "AppShell.h"
#include "Async/TaskSystem.h"
#include "Renderer/GLLoader.h"
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
#include <thread>
#include <chrono>

namespace {

enum RootTabId {
    RootTabLibrary = 0,
    RootTabEditor = 1,
    RootTabComposite = 2,
    RootTabRender = 3
};

struct RootTabDescriptor {
    int id = -1;
    const char* label = "";
    std::function<void()> renderBody;
};

} // namespace

static void glfw_error_callback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << "\n";
}

AppShell::AppShell() : m_Window(nullptr), m_SplashWindow(nullptr), m_SplashTexture(0), m_IsRunning(false), m_FirstTimeLayout(true) {}

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

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
    ImGui_ImplOpenGL3_Init("#version 430 core");

    glfwSetWindowUserPointer(m_Window, this);
    glfwSetDropCallback(m_Window, OnFileDrop);

    Async::TaskSystem::Get().Initialize();
    m_Editor.Initialize();
    m_Composite.Initialize();
    // m_Library.Initialize(); // We will call this manually in the splash screen
    m_RenderTab.Initialize();

    ShowSplashScreen();

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
        
        glClearColor(0.1f, 0.11f, 0.12f, 1.0f);
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
        { RootTabLibrary, "Library", [this]() { m_Library.RenderUI(&m_Editor, &m_RenderTab, &m_Composite, &m_RequestedTab); } },
        { RootTabEditor, "Editor", [this]() { m_Editor.RenderUI(); } },
        { RootTabComposite, "Composite", [this]() { m_Composite.RenderUI(); } },
        { RootTabRender, "Render", [this]() { m_RenderTab.RenderUI(); } }
    };

    int selectedTab = -1;
    if (ImGui::BeginTabBar("ProgramContextTabs", ImGuiTabBarFlags_None)) {
        const int requestedTabAtFrameStart = m_RequestedTab;
        for (const RootTabDescriptor& tab : tabs) {
            const ImGuiTabItemFlags flags =
                (requestedTabAtFrameStart == tab.id) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;

            if (ImGui::BeginTabItem(tab.label, nullptr, flags)) {
                selectedTab = tab.id;
                tab.renderBody();
                ImGui::EndTabItem();
            }
        }

        if (selectedTab == RootTabComposite && m_Composite.ConsumePendingOpenInEditorRequest()) {
            if (m_Editor.GetPipeline().HasSourceImage()) {
                m_ShowEditorSavePrompt = true;
            } else {
                RequestTabSwitch(RootTabEditor);
            }
        }

        if (requestedTabAtFrameStart != -1 &&
            m_RequestedTab == requestedTabAtFrameStart &&
            selectedTab == requestedTabAtFrameStart) {
            m_RequestedTab = -1;
        }

        ImGui::EndTabBar();
    }

    if (selectedTab != -1 && selectedTab != m_CurrentTabId) {
        OnTabChanged(m_CurrentTabId, selectedTab);
        m_CurrentTabId = selectedTab;
    }

    m_Library.RenderGlobalPopups();
    RenderEditorSavePrompts();

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
    // 1. Leaving Composite, entering Editor
    if (oldTab == RootTabComposite && newTab == RootTabEditor) {
        CompositeLayer* selected = m_Composite.GetSelectedLayer();
        if (selected && selected->kind == LayerKind::EditorProject && !selected->originalSourcePng.empty()) {
            // Load this layer into the editor
            EditorModule::LoadedProjectData data;
            data.projectName = selected->name;
            try {
                data.pipelineData = nlohmann::json::parse(selected->embeddedProjectJson);
            } catch (...) {
                data.pipelineData = nlohmann::json::array();
            }
            
            // Re-decode the ORIGINAL source image for high-fidelity editing
            int sw = 0, sh = 0, sc = 4;
            if (LibraryManager::DecodeImageBytes(selected->originalSourcePng, data.sourcePixels, sw, sh, sc)) {
                data.width = sw;
                data.height = sh;
                data.channels = sc;
                
                if (m_Editor.ApplyLoadedProject(data)) {
                    m_ActiveSyncLayerId = selected->id;
                }
            }
        } else {
            m_ActiveSyncLayerId = "";
        }
    }
    // 2. Leaving Editor, entering Composite
    else if (oldTab == RootTabEditor && newTab == RootTabComposite) {
        if (!m_ActiveSyncLayerId.empty()) {
            // Push Editor changes back to Composite
            int rw = 0, rh = 0;
            auto pixels = m_Editor.GetPipeline().GetOutputPixels(rw, rh);
            if (!pixels.empty()) {
                std::string pipelineJson = m_Editor.SerializePipeline().dump();
                m_Composite.UpdateLayerData(m_ActiveSyncLayerId, pipelineJson, pixels, rw, rh);
            }
            m_ActiveSyncLayerId = "";
        }
    }
}

void AppShell::Shutdown() {
    if (!m_Window) return;

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
    m_RequestedTab = tabId;
}

void AppShell::OnFileDrop(GLFWwindow* window, int count, const char** paths) {
    AppShell* app = static_cast<AppShell*>(glfwGetWindowUserPointer(window));
    if (app) {
        app->HandleDrop(count, paths);
    }
}

void AppShell::HandleDrop(int count, const char** paths) {
    if (count <= 0 || !paths) return;

    for (int i = 0; i < count; ++i) {
        std::string path = paths[i];
        
        // Pass a lambda to handle tab switching
        LibraryManager::Get().RequestImportAndLoad(path, &m_Editor, &m_RenderTab, &m_Composite, [this](int tabId) {
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

    // Ensure Assets folder exists for user to put their splash image
    std::filesystem::create_directories("Assets/Splash");

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
    
    ImGui_ImplGlfw_InitForOpenGL(m_SplashWindow, true);
    ImGui_ImplOpenGL3_Init("#version 430 core");
    ImGui::StyleColorsDark();

    // Customize Style for splash
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.WindowBorderSize = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.2f, 0.6f, 1.0f, 1.0f);

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
