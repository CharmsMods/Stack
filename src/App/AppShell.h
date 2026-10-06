#pragma once

#include <string>
#include <memory>
#include "AppLegal.h"
#include "AppSettingsPopup.h"
#include "StartupReveal.h"
#include "UpdateManager.h"
#include "settings/AppearanceTheme.h"
#include "../Composite/CompositeModule.h"
#include "../Editor/EditorModule.h"
#include "../Editor/LoadedProjectData.h"
#include "../Library/LibraryModule.h"
#include "../Queue/QueueModule.h"
#include "../Queue/QueueRenderCoordinator.h"
#include "../Utils/UiNotifications.h"
#include "Utils/UiActivity.h"
#include "Notifications/NotificationPresenter.h"
#include "../Utils/ImGuiExtras.h"
#include "imgui.h"
#include "WorkspaceSwitcher.h"
#include "ToolSwitcher.h"
#include "NavigationRail.h"
#include "RawMaskToolbar.h"
#include "ProjectWorkspace.h"
#include "Renderer/WorkspaceCompositor.h"
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>
#include <unordered_map>

struct GLFWwindow;

class AppShell {
public:
    AppShell();
    ~AppShell();

    bool Initialize(const std::string& title, int width, int height);
    void Run();
    void Shutdown();

    void ConfigureDiagnosticProjectOpen(
        std::filesystem::path projectPath,
        std::filesystem::path switchProjectPath = {});
    bool WasDiagnosticProjectOpenSuccessful() const;
    void ConfigureDiagnosticQueueExport(
        std::filesystem::path inputPath,
        std::filesystem::path destination,
        bool sourceImage = false);
    bool WasDiagnosticQueueExportSuccessful() const;
    void ConfigureDiagnosticGalleryInspection(
        std::filesystem::path firstSource,
        std::filesystem::path latestSource);
    bool WasDiagnosticGalleryInspectionSuccessful() const;

    void RequestTabSwitch(int tabId);
    void ConfigureDiagnosticWorkspaceSwitcher(std::filesystem::path output);
    bool WasDiagnosticWorkspaceSwitcherSuccessful() const { return m_WorkspaceDiagnosticPassed; }
    static void OnFileDrop(GLFWwindow* window, int count, const char** paths);
    void HandleDrop(int count, const char** paths);

private:
    void ApplyRailNavigation();
    void LayoutNavigationRail();
    void RenderNavigationRail(bool openFileMenu, bool openSettings, bool openActivity);
    void LayoutRawMaskToolbar();
    void RenderRawMaskToolbar();
    void RenderSharedPanelToggle();
    bool TopBarOwnsPanelToggle() const;
    bool RawMaskToolbarOwnsInput() const;
    void ToggleWorkspaceSectionPanel();
    void RenderStackMenu();
    void RenderLibraryViewSwitch();
    void RenderWorkspaceSectionPanel(int renderTab);
    void RenderProjectPills(const ImVec2& position, const ImVec2& size);
    void UpdateProjectPillPreview(std::uint64_t hovered);
    void RenderProjectPreviewFallback();
    void CancelProjectPillPreview();
    Stack::Navigation::RailState m_NavigationRail;
    Stack::Navigation::RawMaskToolbarState m_RawMaskToolbar;
    void TickToolSwitcher();
    void CancelToolSwitcher();
    void DrawToolSwitcher();
    Stack::Tools::Switcher m_ToolSwitcher;
    void TickWorkspaceSwitcher();
    bool TickDiagnosticWorkspaceSwitcher();
    void CaptureDiagnosticWorkspaceSwitcher();
    std::filesystem::path m_WorkspaceDiagnosticOutput;
    int m_WorkspaceDiagnosticFrame = 0, m_WorkspaceDiagnosticOrigin = 0;
    bool m_WorkspaceDiagnosticPassed = false, m_WorkspaceDiagnosticDirty = false;
    std::string m_WorkspaceDiagnosticSource;
    void EndWorkspaceSwitcher(bool commit, bool restoreFocus = true);
    void DrawWorkspaceSwitcher();
    Stack::Workspace::Switcher m_WorkspaceSwitcher;
    Stack::Renderer::WorkspaceCompositor m_WorkspaceCompositor;
    ImGuiID m_WorkspaceOriginViewport = 0;
    ImVec2 m_WorkspaceOriginCursor{}, m_WorkspaceMainCursor{};
    double m_WorkspacePointerX = 0, m_WorkspacePointerY = 0;
    bool m_WorkspaceAltWasDown = false, m_WorkspaceAltSuppressed = false;
    bool m_WorkspaceFinishPending = false, m_WorkspaceFinishing = false;
    bool m_WorkspaceDiscardMouse = false;
    using LibraryToEditorProjectLoadPhase = ProjectLoadPhase;

    enum class PendingFileAction {
        None,
        NewEditorProject,
        OpenProject,
        CloseCurrent,
        CloseWorkspace
    };

    enum class WorkspaceTabAction { None, Select, Create, Close };
    void InitializeProjectWorkspaces();
    std::uint64_t CreateProjectWorkspace();
    ProjectWorkspace* FindProjectWorkspace(std::uint64_t id) const;
    LibraryToEditorProjectLoadPhase ActiveProjectLoadPhase() const;
    void ConfigureWorkspaceEditor(EditorModule& editor);
    void SynchronizeRawWorkspaceFolder();
    void UpdateWorkspacePreferencesOwner();
    void TickAutoBracketWorkspaces();
    bool CloseAutoBracketWorkspace(std::uint64_t id);
    std::filesystem::path m_SharedRawWorkspaceRoot;
    struct GalleryTabOpen {
        enum class Kind { Source, Project, Bracket } kind = Kind::Source;
        std::filesystem::path path;
        std::vector<std::filesystem::path> bracketSources;
        std::uint64_t workspaceId = 0;
    };
    std::deque<GalleryTabOpen> m_GalleryTabOpenQueue;
    std::optional<GalleryTabOpen> m_PendingGalleryTabOpen;
    void TickProjectWorkspaces();
    bool CanSwitchProjectWorkspace() const;
    bool ActivateProjectWorkspace(std::uint64_t id, bool preserveNavigation = false);
    void QueueWorkspaceTabAction(WorkspaceTabAction action, std::uint64_t id = 0);
    void RetireCurrentProjectWorkspace();
    void FinishProjectWorkspaceRetirement();
    void ContinueMainWindowClose(const std::string& source);
    bool AllWorkspaceWorkersReadyForClose() const;
    std::vector<std::unique_ptr<ProjectWorkspace>> m_ProjectWorkspaces;
    std::vector<std::unique_ptr<ProjectWorkspace>> m_RetiredProjectWorkspaces;
    std::vector<std::uint64_t> m_CloseApprovedWorkspaces;
    std::uint64_t m_ActiveProjectWorkspace = 0;
    std::uint64_t m_GalleryWorkspaceId = 0;
    std::uint64_t m_NextProjectWorkspace = 1;
    WorkspaceTabAction m_WorkspaceTabAction = WorkspaceTabAction::None;
    std::uint64_t m_WorkspaceTabActionId = 0;
    bool m_ScrollActiveProjectTabIntoView = false;
    std::uint64_t m_PendingWorkspaceRetirement = 0;
    std::string m_ContinueMainWindowCloseSource;

    void ShowSplashScreen();
    void RenderUI();
    void RenderStartupReveal(
        ImGuiViewport* viewport,
        const Stack::StartupReveal::Visual& visual,
        const ImVec4& surfaceColor);
    void RenderLibraryWindow();
    bool IsLibraryWindowOpenAndHovered() const;
    void RenderLegalGate();
    void RenderClosingFrame();
    void StartAutomaticUpdateCheckIfAllowed();
    void RenderEditorSavePrompts();
    void RenderFileCommandPrompts();
    void DispatchPendingFileMenuSaveAs();
    void RequestFileMenuSave();
    void RequestFileMenuSaveAs();
    void QueueFileAction(
        PendingFileAction action,
        std::filesystem::path projectPath = {});
    bool ExecutePendingFileAction(bool discardCurrent, EditorModule* owner = nullptr,
        std::uint64_t workspaceId = 0);
    void ClearPendingFileAction();
    void RequestMainWindowClose(const char* source);
    void ProcessNativeCloseButtonHoverSave();
    void BeginMainWindowClose(const std::string& source);
    void CancelWorkForMainWindowClose();
    void BeginLibraryToEditorProjectLoad(const std::string& projectFileName);
    void BeginLibraryToEditorProjectLoad(ProjectWorkspace& workspace, const std::string& projectFileName);
    void RequestDeferredLibraryProjectLoad(ProjectWorkspace& workspace);
    void SetLibraryToEditorProjectLoadPhase(ProjectWorkspace& workspace, LibraryToEditorProjectLoadPhase phase);
    void TickLibraryToEditorProjectLoadTransition();
    void TickLibraryToEditorProjectLoadTransition(ProjectWorkspace& workspace);
    void OnFramePresented();
    void RenderLibraryLoadTransitionDiagnostics();
    void TraceLibraryLoadTransition(ProjectWorkspace& workspace, const std::string& event);
    void UpdateNotifications();
    void BindWorkspaceNotifications(ProjectWorkspace& workspace);
    Stack::Notifications::PresentationContext NotificationContext();
    void TickDiagnosticProjectOpen();
    void TickDiagnosticQueueExport();
    void TickDiagnosticGalleryInspection();
    void CompleteDiagnosticGalleryInspection(
        Stack::RawGalleryInspection::Result result);
    void SyncCursorCaptureRequest();
    void ReleaseLockedScrubCursor(bool restoreCursorPosition = true);
    void SyncBackgroundImageTexture();
    void ResetBackgroundImageDecodeState();
    void ReportBackgroundImageFailure(const std::string& path, std::uint64_t revision, const std::string& error);
    void ResolveBackgroundImageFailure();
    void ReleaseBackgroundImageTexture();
    bool LoadBackgroundImageTextureFromPath(const std::filesystem::path& path);
    void RenderBackgroundImage(
        const ImVec2& regionMin,
        const ImVec2& regionSize,
        float alphaMultiplier = 1.0f,
        ImDrawList* targetDrawList = nullptr);
    void PostNotification(UiNotificationSeverity severity, const std::string& message, const std::string& dedupeKey = "");
    void RenderNotifications();
    void ReportSaveFailure(EditorModule& owner, const std::string& message,
        std::function<void()> continuation = {});
    bool CanChangeRootTab(int oldTab, int newTab);
    void OnTabChanged(int oldTab, int newTab);
    void RequestRootTabTransition(int newTab);
    void BeginRootTabBodyFade(int oldTab, int newTab);
    float ConsumeRootTabBodyFadeAlpha(int* outRenderTabId);
    void RenderHeaderSettingsPopup(bool buttonHovered);
    float RenderActivityIndicator(float width, float height, bool openDetails);
    Stack::UiActivity::Snapshot CollectActivity() const;
    void ProcessGraphCaptureRequest(EditorModule* captureOwner);
    void InstallDetachedPreviewPlatformHooks();
    void UninstallDetachedPreviewPlatformHooks();
    void HandleDetachedPreviewPlatformCreateWindow(ImGuiViewport* viewport);
    void HandleDetachedPreviewPlatformShowWindow(ImGuiViewport* viewport);
    void ProcessDetachedPreviewNativeWindow();
    void CompleteDetachedPreviewPlatformPresent();
    void TraceDetachedPreviewNativeWindow(
        const char* event,
        const EditorModule::DetachedNativeWindowRequest* request = nullptr,
        bool themeApplied = false,
        bool focusAttempted = false,
        bool focused = false);
    void TraceDetachedPreviewFrame(double frameMs, double renderUiMs, double drawMs);
    void TraceMainWindowState(const char* event);
    void TraceMainWindowState(const char* event, const char* detail);
    void TraceShutdownPhase(const char* phase, double elapsedMs = -1.0, const char* detail = nullptr);
    bool IsDetachedSurfaceViewport(
        const ImGuiViewport* viewport,
        EditorModule::DetachedNativeWindowRequest* request = nullptr) const;
    static void OnWindowClose(GLFWwindow* window);
    static void DetachedPreviewPlatformCreateWindowHook(ImGuiViewport* viewport);
    static void DetachedPreviewPlatformShowWindowHook(ImGuiViewport* viewport);
    static AppShell* s_DetachedPreviewPlatformHookOwner;

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
    unsigned int m_RawTabTexture = 0;
    unsigned int m_RawLabTabTexture = 0;
    unsigned int m_FileNewTexture = 0;
    unsigned int m_FileOpenProjectTexture = 0;
    unsigned int m_FileSaveTexture = 0;
    unsigned int m_FileExitProgramTexture = 0;
    unsigned int m_ProgramIconTexture = 0;
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
    Stack::Notifications::EventId m_BackgroundImageFailureEvent = 0;
    std::string m_BackgroundImageReportedFailure;
    bool m_IsRunning;
    bool m_CloseRequested = false;
    bool m_NativeCloseButtonHoverActive = false;
    int m_ClosingPresentedFrames = 0;
    double m_CloseRequestedAt = 0.0;
    std::string m_CloseSource;
    bool m_FirstTimeLayout;
    double m_MainWindowShownTime = 0.0;
    Stack::StartupReveal::Controller m_StartupReveal;
    Stack::StartupReveal::Visual m_StartupRevealVisual;
    bool m_StartupRevealBorderVisible = true;
    bool m_StartupRevealWindowOpacityHidden = false;
    std::intptr_t m_StartupRevealOriginalExtendedStyle = 0;
    int m_RequestedTab = 1; // 0 = Library window, 1 = Editor, 3 = legacy RAW, 5 = RAW Lab, 6 = MultiFrame
    int m_CurrentTabId = 0;
    bool m_LibraryWindowOpen = false;
    bool m_LibraryWindowFocusRequested = false;
    bool m_LibraryWindowPlacementInitialized = false;
    GLFWwindow* m_LibraryNativeWindow = nullptr;
    std::uint64_t m_LibraryNativeAppearanceRevision = 0;
    bool m_RootTabBodyFadeActive = false;
    double m_RootTabBodyFadeStartedAt = 0.0;
    int m_RootTabBodyFadeFromTab = -1;
    int m_RootTabBodyFadeToTab = -1;
    int m_RootTabBodyFadeQueuedTab = -1;
    bool m_RootTabBodyFadeCommitted = false;
    std::string m_ActiveSyncLayerId;
    bool m_ShowEditorSavePrompt = false;
    bool m_ShowEditorNamePrompt = false;
    bool m_ProjectLoadSavePending = false;
    bool m_ProjectLoadCurrentProjectDispositionApproved = false;
    std::string m_PendingProjectLoadFileName;
    bool m_ShowRawWorkspaceSwitchPrompt = false;
    bool m_RawWorkspaceSwitchSavePending = false;
    bool m_ShowUnnamedEditorClosePrompt = false;
    bool m_MainWindowCloseSavePending = false;
    std::string m_PendingMainWindowCloseSource;
    bool m_SettingsPopupOpen = false;
    double m_SettingsPopupOpenedAt = 0.0;
    bool m_ShowLegalGateReview = false;
    bool m_ShowOpenProjectPrompt = false;
    bool m_ShowFileDispositionPrompt = false;
    bool m_FileActionSavePending = false;
    bool m_FileActionSaveFailed = false;
    PendingFileAction m_PendingFileAction = PendingFileAction::None;
    std::filesystem::path m_PendingFileProjectPath;
    std::filesystem::path m_PendingFileMenuSaveAsDestination;
    int m_PendingFileMenuSaveAsQueuedFrame = -1;
    char m_SaveNameBuffer[256] = {};
    std::shared_ptr<Stack::Notifications::NotificationStore> m_NotificationStore =
        std::make_shared<Stack::Notifications::NotificationStore>();
    Stack::Notifications::Notifier m_AppNotifier;
    Stack::Notifications::Presenter m_NotificationPresenter;
    struct LiveActivity {
        Stack::Notifications::Notifier notifier;
        Stack::Notifications::ActivityHandle handle;
    };
    std::unordered_map<std::string,LiveActivity> m_LiveActivities;
    struct SaveRecoveryOwner {
        std::string document;
        std::weak_ptr<Stack::Project::FileOperationState> files;
        std::uint64_t loadGeneration = 0;
        Stack::Notifications::OperationId operation = 0;
    };
    std::unordered_map<std::uint64_t, SaveRecoveryOwner> m_SaveRecoveryOwners;
    Stack::UiActivity::Presentation m_ActivityPresentation;
    std::filesystem::path m_DiagnosticProjectOpenPath;
    std::filesystem::path m_DiagnosticProjectSwitchPath;
    double m_DiagnosticProjectOpenStartedAt = 0.0;
    double m_DiagnosticProjectOpenLastTickAt = 0.0;
    double m_DiagnosticProjectOpenMaxTickGapMs = 0.0;
    int m_DiagnosticProjectOpenReadyFrames = 0;
    bool m_DiagnosticProjectSwitchRequested = false;
    bool m_DiagnosticProjectOpenActive = false;
    bool m_DiagnosticProjectOpenSuccessful = false;
    std::filesystem::path m_DiagnosticQueueProjectPath;
    std::filesystem::path m_DiagnosticQueueDestination;
    bool m_DiagnosticQueueSourceImage = false;
    double m_DiagnosticQueueStartedAt = 0.0;
    bool m_DiagnosticQueueActive = false;
    bool m_DiagnosticQueueSuccessful = false;
    int m_DiagnosticQueueLastItemState = -1;
    std::string m_DiagnosticQueueLastItemStatus;
    std::filesystem::path m_DiagnosticGalleryInspectionFirstSource;
    std::filesystem::path m_DiagnosticGalleryInspectionLatestSource;
    double m_DiagnosticGalleryInspectionStartedAt = 0.0;
    double m_DiagnosticGalleryInspectionLastTickAt = 0.0;
    double m_DiagnosticGalleryInspectionMaxTickGapMs = 0.0;
    bool m_DiagnosticGalleryInspectionActive = false;
    bool m_DiagnosticGalleryInspectionLatestRequested = false;
    bool m_DiagnosticGalleryInspectionStaleCompletion = false;
    bool m_DiagnosticGalleryInspectionSuccessful = false;
    void (*m_OriginalPlatformCreateWindow)(ImGuiViewport*) = nullptr;
    void (*m_OriginalPlatformShowWindow)(ImGuiViewport*) = nullptr;
    bool m_DetachedPreviewPlatformHooksInstalled = false;
    bool m_DetachedPreviewOpeningTopMostHeld = false;
    GLFWwindow* m_DetachedPreviewOpeningWindow = nullptr;
    int m_DetachedPreviewOpeningReleaseAttempts = 0;
    std::unique_ptr<StackAppearance::AppearanceManager> m_Appearance;
    std::uint64_t m_AppliedAppearanceRevision = 0;
    std::unique_ptr<AppLegal::Manager> m_LegalManager;
    std::unique_ptr<AppUpdate::UpdateManager> m_UpdateManager;
    bool m_StartupUpdateCheckStarted = false;
    std::string m_LegalActionError;
    AppSettingsPopup::State m_SettingsPopupState;
    EditorModule* m_Editor = nullptr;
    LibraryModule m_Library;
    CompositeModule m_Composite;
    Stack::Queue::QueueModule m_Queue;
    Stack::Queue::QueueRenderCoordinator m_QueueRenderer;
};
