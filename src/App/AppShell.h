#pragma once

#include <string>
#include <memory>
#include "settings/AppearanceTheme.h"
#include "StyleModule.h"
#include "../Composite/CompositeModule.h"
#include "../Editor/EditorModule.h"
#include "../Library/LibraryModule.h"

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
    unsigned int m_EditorTabTexture = 0;
    unsigned int m_LibraryTabTexture = 0;
    unsigned int m_ProgramLogoTexture = 0;
    unsigned int m_StyleTabTexture = 0;
    bool m_IsRunning;
    bool m_FirstTimeLayout;
    int m_RequestedTab = 0; // 0 = Library, 1 = Editor
    int m_CurrentTabId = 0;
    std::string m_ActiveSyncLayerId;
    bool m_ShowEditorSavePrompt = false;
    bool m_ShowEditorNamePrompt = false;
    char m_SaveNameBuffer[256] = {};
    std::unique_ptr<StackAppearance::AppearanceManager> m_Appearance;
    EditorModule m_Editor;
    LibraryModule m_Library;
    CompositeModule m_Composite;
    StyleModule m_Style;
};
