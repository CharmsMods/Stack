#pragma once

#include <string>
#include "../Composite/CompositeModule.h"
#include "../Editor/EditorModule.h"
#include "../Library/LibraryModule.h"
#include "../RenderTab/RenderTab.h"

struct GLFWwindow;

class AppShell {
public:
    AppShell();
    ~AppShell();

    bool Initialize(const std::string& title, int width, int height);
    void Run();
    void Shutdown();

    void RequestTabSwitch(int tabId);
    static void OnFileDrop(GLFWwindow* window, int count, const char** paths);
    void HandleDrop(int count, const char** paths);

private:
    void ShowSplashScreen();
    void RenderUI();
    void RenderEditorSavePrompts();
    void OnTabChanged(int oldTab, int newTab);

    GLFWwindow* m_Window;
    GLFWwindow* m_SplashWindow;
    unsigned int m_SplashTexture = 0;
    bool m_IsRunning;
    bool m_FirstTimeLayout;
    int m_RequestedTab = 0; // 0 = Library, 1 = Editor
    int m_CurrentTabId = 0;
    std::string m_ActiveSyncLayerId;
    bool m_ShowEditorSavePrompt = false;
    bool m_ShowEditorNamePrompt = false;
    char m_SaveNameBuffer[256] = {};
    EditorModule m_Editor;
    LibraryModule m_Library;
    RenderTab m_RenderTab;
    CompositeModule m_Composite;
};
