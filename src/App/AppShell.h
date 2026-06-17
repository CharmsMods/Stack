#pragma once

#include <string>
#include <memory>
#include "AppSettingsPopup.h"
#include "UpdateManager.h"
#include "settings/AppearanceTheme.h"
#include "../Composite/CompositeModule.h"
#include "../Editor/EditorModule.h"
#include "../Editor/LoadedProjectData.h"
#include "../Library/LibraryModule.h"
#include "../Utils/UiNotifications.h"
#include "imgui.h"
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
        WaitForEditorReady,
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
    void SyncCursorCaptureRequest();
    void SyncBackgroundImageTexture();
    void ReleaseBackgroundImageTexture();
    bool LoadBackgroundImageTextureFromPath(const std::filesystem::path& path);
    void RenderBackgroundImage(const ImVec2& regionMin, const ImVec2& regionSize);
    void PushToast(UiNotificationSeverity severity, const std::string& message, const std::string& dedupeKey = "");
    void RenderToasts();
    void OnTabChanged(int oldTab, int newTab);
    void RenderHeaderSettingsPopup(const ImVec2& gearButtonMin, const ImVec2& gearButtonMax, bool gearButtonHovered);

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
    unsigned int m_HeaderSettingsTexture = 0;
    unsigned int m_BackgroundImageTexture = 0;
    int m_BackgroundImageWidth = 0;
    int m_BackgroundImageHeight = 0;
    bool m_LockedScrubCursorActive = false;
    ImVec2 m_LockedScrubCursorAnchorScreenPos = ImVec2(0.0f, 0.0f);
    ImVec2 m_LockedScrubCursorRestoreScreenPos = ImVec2(0.0f, 0.0f);
    std::string m_BackgroundImageTexturePath;
    std::uint64_t m_BackgroundImageTextureRevision = 0;
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
    bool m_LoadTransitionApplySucceeded = false;
    bool m_LoadTransitionDismissLibraryPreviewsPending = false;
    bool m_LoadTransitionLoadRequested = false;
    bool m_LoadTransitionFirstRenderReady = false;
    bool m_LoadTransitionNodeBrowserThumbnailsReady = false;
    double m_LoadTransitionStartedAt = 0.0;
    double m_LoadTransitionDecodeRequestedAt = 0.0;
    double m_LoadTransitionDecodeReadyAt = 0.0;
    double m_LoadTransitionApplyStartedAt = 0.0;
    double m_LoadTransitionApplyFinishedAt = 0.0;
    double m_LoadTransitionFirstRenderReadyAt = 0.0;
    double m_LoadTransitionThumbnailsReadyAt = 0.0;
    double m_LoadTransitionReadyToRevealAt = 0.0;
    std::shared_ptr<EditorLoadedProjectData> m_LoadTransitionDecodedProject;
    std::vector<std::string> m_LoadTransitionTrace;
    std::string m_ActiveSyncLayerId;
    bool m_ShowEditorSavePrompt = false;
    bool m_ShowEditorNamePrompt = false;
    bool m_SettingsPopupOpen = false;
    char m_SaveNameBuffer[256] = {};
    std::vector<ActiveToast> m_ActiveToasts;
    std::unique_ptr<StackAppearance::AppearanceManager> m_Appearance;
    std::unique_ptr<AppUpdate::UpdateManager> m_UpdateManager;
    AppSettingsPopup::State m_SettingsPopupState;
    EditorModule m_Editor;
    LibraryModule m_Library;
    CompositeModule m_Composite;
};
