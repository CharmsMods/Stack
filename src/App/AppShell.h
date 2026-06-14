#pragma once

#include <string>
#include <memory>
#include "settings/AppearanceTheme.h"
#include "StyleModule.h"
#include "../Notes/NotesModule.h"
#include "../Composite/CompositeModule.h"
#include "../Editor/EditorModule.h"
#include "../Library/LibraryModule.h"
#include "../Utils/UiNotifications.h"
#include <functional>
#include <vector>

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
    enum class LibraryToEditorProjectLoadPhase {
        None,
        LibraryFadeOut,
        SpinnerFadeIn,
        ApplyProject,
        SpinnerFadeOut,
        EditorReveal
    };

    void ShowSplashScreen();
    void RenderUI();
    void RenderEditorSavePrompts();
    void BeginLibraryToEditorProjectLoad(const std::string& projectFileName);
    void RequestDeferredLibraryProjectLoad();
    void SetLibraryToEditorProjectLoadPhase(LibraryToEditorProjectLoadPhase phase);
    void TickLibraryToEditorProjectLoadTransition();
    void OnFramePresented();
    void RenderLibraryLoadTransitionDiagnostics();
    void TraceLibraryLoadTransition(const std::string& event);
    void ConsumeUiNotifications();
    void PushToast(UiNotificationSeverity severity, const std::string& message, const std::string& dedupeKey = "");
    void RenderToasts();
    void OnTabChanged(int oldTab, int newTab);

    struct ActiveToast {
        UiNotificationSeverity severity = UiNotificationSeverity::Info;
        std::string message;
        std::string dedupeKey;
        double startTime = 0.0;
        double duration = 3.4;
    };

    GLFWwindow* m_Window;
    GLFWwindow* m_SplashWindow;
    unsigned int m_SplashTexture = 0;
    unsigned int m_EditorTabTexture = 0;
    unsigned int m_LibraryTabTexture = 0;
    unsigned int m_ProgramLogoTexture = 0;
    unsigned int m_StyleTabTexture = 0;
    bool m_IsRunning;
    bool m_FirstTimeLayout;
    float m_ChromeHiddenT = 0.0f;
    double m_ChromeRevealTime = 0.0;
    double m_MainWindowShownTime = 0.0;
    int m_RequestedTab = 0; // 0 = Library, 1 = Editor
    int m_CurrentTabId = 0;
    LibraryToEditorProjectLoadPhase m_LoadTransitionPhase = LibraryToEditorProjectLoadPhase::None;
    double m_LoadTransitionPhaseStartTime = 0.0;
    double m_LoadTransitionSpinnerStartTime = 0.0;
    int m_LoadTransitionPhasePresentedFrames = 0;
    std::string m_LoadTransitionProjectFileName;
    bool m_LoadTransitionDecodeReady = false;
    bool m_LoadTransitionDecodeSucceeded = false;
    bool m_LoadTransitionApplyAttempted = false;
    bool m_LoadTransitionApplySucceeded = false;
    bool m_LoadTransitionDismissLibraryPreviewsPending = false;
    bool m_LoadTransitionLoadRequested = false;
    double m_LoadTransitionStartedAt = 0.0;
    double m_LoadTransitionDecodeRequestedAt = 0.0;
    double m_LoadTransitionDecodeReadyAt = 0.0;
    double m_LoadTransitionApplyStartedAt = 0.0;
    double m_LoadTransitionApplyFinishedAt = 0.0;
    std::function<bool()> m_LoadTransitionApplyProject;
    std::vector<std::string> m_LoadTransitionTrace;
    std::string m_ActiveSyncLayerId;
    bool m_ShowEditorSavePrompt = false;
    bool m_ShowEditorNamePrompt = false;
    char m_SaveNameBuffer[256] = {};
    std::vector<ActiveToast> m_ActiveToasts;
    std::unique_ptr<StackAppearance::AppearanceManager> m_Appearance;
    EditorModule m_Editor;
    LibraryModule m_Library;
    CompositeModule m_Composite;
    NotesModule m_Notes;
    StyleModule m_Style;
};
