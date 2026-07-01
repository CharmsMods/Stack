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
#include "../Tools/ToolsModule.h"
#include "../Utils/UiNotifications.h"
#include "../Utils/ImGuiExtras.h"
#include "imgui.h"
#include <cstdint>
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
    void RenderClosingFrame();
    void RenderEditorSavePrompts();
    void RequestMainWindowClose(const char* source);
    void CancelWorkForMainWindowClose();
    void BeginLibraryToEditorProjectLoad(const std::string& projectFileName);
    void RequestDeferredLibraryProjectLoad();
    void SetLibraryToEditorProjectLoadPhase(LibraryToEditorProjectLoadPhase phase);
    void TickLibraryToEditorProjectLoadTransition();
    void OnFramePresented();
    void RenderLibraryLoadTransitionDiagnostics();
    void TraceLibraryLoadTransition(const std::string& event);
    void ConsumeUiNotifications();
    void SyncCursorCaptureRequest();
    void ReleaseLockedScrubCursor(bool restoreCursorPosition = true);
    void SyncBackgroundImageTexture();
    void ResetBackgroundImageDecodeState();
    void ReleaseBackgroundImageTexture();
    bool LoadBackgroundImageTextureFromPath(const std::filesystem::path& path);
    void RenderBackgroundImage(const ImVec2& regionMin, const ImVec2& regionSize, float alphaMultiplier = 1.0f);
    void PushToast(UiNotificationSeverity severity, const std::string& message, const std::string& dedupeKey = "");
    void RenderToasts();
    bool CanChangeRootTab(int oldTab, int newTab);
    void OnTabChanged(int oldTab, int newTab);
    void BeginRootTabBodyFade(int oldTab, int newTab);
    float ConsumeRootTabBodyFadeAlpha(int* outRenderTabId);
    void RenderHeaderSettingsPopup(const ImVec2& gearButtonMin, const ImVec2& gearButtonMax, bool gearButtonHovered);
    void InstallDetachedPreviewPlatformHooks();
    void UninstallDetachedPreviewPlatformHooks();
    void HandleDetachedPreviewPlatformCreateWindow(ImGuiViewport* viewport);
    void HandleDetachedPreviewPlatformShowWindow(ImGuiViewport* viewport);
    void ProcessDetachedPreviewNativeWindow();
    void CompleteDetachedPreviewPlatformPresent();
    void TraceDetachedPreviewNativeWindow(
        const char* event,
        const EditorModule::DetachedPreviewNativeWindowRequest* request = nullptr,
        bool themeApplied = false,
        bool focusAttempted = false,
        bool focused = false);
    void TraceDetachedPreviewFrame(double frameMs, double renderUiMs, double drawMs);
    void TraceMainWindowState(const char* event);
    void TraceMainWindowState(const char* event, const char* detail);
    void TraceShutdownPhase(const char* phase, double elapsedMs = -1.0, const char* detail = nullptr);
    bool IsDetachedPreviewViewport(const ImGuiViewport* viewport, EditorModule::DetachedPreviewNativeWindowRequest* request = nullptr) const;
    static void OnWindowClose(GLFWwindow* window);
    static void DetachedPreviewPlatformCreateWindowHook(ImGuiViewport* viewport);
    static void DetachedPreviewPlatformShowWindowHook(ImGuiViewport* viewport);
    static AppShell* s_DetachedPreviewPlatformHookOwner;

    struct ActiveToast {
        UiNotificationSeverity severity = UiNotificationSeverity::Info;
        std::string message;
        std::string dedupeKey;
        double startTime = 0.0;
        double duration = 3.4;
    };

    enum class BackgroundImageDecodeState {
        Idle,
        Queued,
        Decoding,
        Ready,
        Failed
    };

    GLFWwindow* m_Window;
    GLFWwindow* m_SplashWindow;
    unsigned int m_SplashTexture = 0;
    unsigned int m_EditorTabTexture = 0;
    unsigned int m_LibraryTabTexture = 0;
    unsigned int m_ToolsTabTexture = 0;
    unsigned int m_RawTabTexture = 0;
    unsigned int m_HeaderSettingsTexture = 0;
    unsigned int m_BackgroundImageTexture = 0;
    int m_BackgroundImageWidth = 0;
    int m_BackgroundImageHeight = 0;
    float m_BackgroundImageTextureVisibleAlpha = 0.0f;
    bool m_LockedScrubCursorActive = false;
    ImGuiExtras::CursorCaptureMode m_LockedCursorCaptureMode = ImGuiExtras::CursorCaptureMode::None;
    ImVec2 m_LockedScrubCursorAnchorScreenPos = ImVec2(0.0f, 0.0f);
    ImVec2 m_LockedScrubCursorRestoreScreenPos = ImVec2(0.0f, 0.0f);
    std::string m_BackgroundImageTexturePath;
    std::uint64_t m_BackgroundImageTextureRevision = 0;
    BackgroundImageDecodeState m_BackgroundImageDecodeState = BackgroundImageDecodeState::Idle;
    std::uint64_t m_BackgroundImageDecodeGeneration = 0;
    std::uint64_t m_BackgroundImageDecodeRevision = 0;
    std::string m_BackgroundImageDecodePath;
    std::vector<unsigned char> m_BackgroundImageDecodedPixels;
    int m_BackgroundImageDecodedWidth = 0;
    int m_BackgroundImageDecodedHeight = 0;
    std::string m_BackgroundImageDecodeError;
    bool m_IsRunning;
    bool m_CloseRequested = false;
    int m_ClosingPresentedFrames = 0;
    double m_CloseRequestedAt = 0.0;
    std::string m_CloseSource;
    bool m_FirstTimeLayout;
    float m_ChromeHiddenT = 0.0f;
    double m_ChromeRevealTime = 0.0;
    double m_MainWindowShownTime = 0.0;
    bool m_AppStartupMotionActive = false;
    double m_AppStartupMotionStartedAt = 0.0;
    int m_RequestedTab = 0; // 0 = Library, 1 = Editor, 2 = Tools
    int m_CurrentTabId = 0;
    bool m_RootTabBodyFadeActive = false;
    double m_RootTabBodyFadeStartedAt = 0.0;
    int m_RootTabBodyFadeFromTab = -1;
    int m_RootTabBodyFadeToTab = -1;
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
    double m_SettingsPopupOpenedAt = 0.0;
    char m_SaveNameBuffer[256] = {};
    std::vector<ActiveToast> m_ActiveToasts;
    void (*m_OriginalPlatformCreateWindow)(ImGuiViewport*) = nullptr;
    void (*m_OriginalPlatformShowWindow)(ImGuiViewport*) = nullptr;
    bool m_DetachedPreviewPlatformHooksInstalled = false;
    bool m_DetachedPreviewOpeningTopMostHeld = false;
    GLFWwindow* m_DetachedPreviewOpeningWindow = nullptr;
    int m_DetachedPreviewOpeningReleaseAttempts = 0;
    std::unique_ptr<StackAppearance::AppearanceManager> m_Appearance;
    std::uint64_t m_AppliedAppearanceRevision = 0;
    std::unique_ptr<AppUpdate::UpdateManager> m_UpdateManager;
    AppSettingsPopup::State m_SettingsPopupState;
    EditorModule m_Editor;
    LibraryModule m_Library;
    ToolsModule m_Tools;
    CompositeModule m_Composite;
};
