#pragma once
#include "App/IAppModule.h"
#include "LibraryManager.h"

namespace StackAppearance {
    class AppearanceManager;
    struct RuntimeSurfacePalette;
}
#include <memory>
#include <string>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <vector>

struct LibraryCardMotionState {
    float hover = 0.0f;
    float selected = 0.0f;
    float reveal = 1.0f;
    float entrance = 1.0f;
    double entranceStartTime = 0.0;
    bool entranceInitialized = false;
    int lastSeenFrame = 0;
};

struct LibraryPreviewLaunchRect {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    bool valid = false;
};

struct LibraryRenderStats {
    double autoRefreshMs = 0.0;
    double layoutMs = 0.0;
    double cardRenderMs = 0.0;
    int totalCards = 0;
    int packedCards = 0;
    int visibleCards = 0;
    bool layoutCacheHit = false;
    LibraryAutoRefreshStats autoRefresh;
};

struct LibraryCachedPackedCard {
    std::size_t index = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

class LibraryModule : public IAppModule {
public:
    LibraryModule();
    ~LibraryModule() override;

    void Initialize() override;
    void RenderUI() override {} // Dummy for IAppModule
    void RenderUI(
        class EditorModule* editor,
        class CompositeModule* composite,
        StackAppearance::AppearanceManager* appearance,
        int* activeTab = nullptr,
        int rawWorkspaceTabId = -1,
        std::function<void(const std::string&)> onLoadEditorProject = {});
    void RenderGlobalPopups();
    const LibraryRenderStats& GetLastRenderStats() const { return m_LastRenderStats; }
    void DismissPreviewsForProjectLoad();
    void OpenProjectPreviewByFileName(const std::string& fileName);
    const char* GetName() override { return "Library"; }

private:
    void RequestOpenEditorProject(const std::string& projectFileName);
    bool RenderProjectCard(const struct ProjectEntry& project, class EditorModule* editor);
    bool RenderAssetCard(const struct AssetEntry& asset, class EditorModule* editor);
    void RenderPreviewPopup(
        class EditorModule* editor,
        class CompositeModule* composite,
        int* activeTab = nullptr);
    void RenderAssetPreviewPopup(
        class EditorModule* editor,
        class CompositeModule* composite,
        int* activeTab = nullptr);
    void RenderConfirmLoadPopup();
    void RenderFolderImportPopup();
    void RenderImportConflictPopup();
    void RenderAssetConflictPopup();
    void RenderLibraryMenuOptions(bool importBusy, bool exportBusy);
    void RenderTagsDrawer(
        StackAppearance::AppearanceManager* appearance,
        bool wallpaperSurfaces,
        const StackAppearance::RuntimeSurfacePalette& surfacePalette,
        float dt);
    void RenderLibraryGrid(
        class EditorModule* editor,
        StackAppearance::AppearanceManager* appearance,
        bool wallpaperSurfaces,
        const StackAppearance::RuntimeSurfacePalette& surfacePalette,
        const LibraryRefreshSnapshot& refreshSnapshot,
        bool refreshBusy,
        bool importBusy,
        bool exportBusy,
        float dt);
    void RenderRawWorkspaceView(
        class EditorModule* editor,
        int* activeTab = nullptr,
        int rawWorkspaceTabId = -1);
    void RenderDeleteConfirmPopup();
    void SyncRenameBuffer();
    void OpenAssetPreviewByFileName(const std::string& fileName);

    std::shared_ptr<struct ProjectEntry> m_PreviewProject = nullptr;
    std::shared_ptr<struct AssetEntry> m_PreviewAsset = nullptr;
    bool m_ShowAssets = false;
    bool m_ShowRawWorkspace = false;
    bool m_BlockLibraryGridContextMenuThisFrame = false;
    char m_SearchFilter[128] = "";
    char m_RenameBuffer[256] = "";
    float m_CompareSplit = 0.5f;
    float m_ConflictCompareSplit = 0.5f;
    float m_AssetConflictCompareSplit = 0.5f;
    float m_FilterPanelWidth = 220.0f;
    float m_FilterPanelExpandedWidth = 220.0f;
    bool m_FilterPanelCollapsed = false;
    bool m_FilterPanelExpanded = false;
    float m_FilterPanelWidthAnim = 0.0f;
    std::string m_RenameTargetFileName;
    std::string m_PendingRenderProjectFileName;
    bool m_RenderLoadConfirmOpen = false;
    float m_ProjectPreviewTransition = 0.0f;
    bool m_ProjectPreviewClosing = false;
    bool m_ProjectPreviewRefreshAfterClose = false;
    float m_AssetPreviewTransition = 0.0f;
    bool m_AssetPreviewClosing = false;

    enum class PendingLoadTarget { None, Editor, Composite };
    PendingLoadTarget m_PendingLoadTarget = PendingLoadTarget::None;
    std::string m_PendingLoadProjectFileName;
    bool m_ConfirmLoadOpen = false;
    bool m_SaveNamePromptOpen = false;
    char m_SaveNameBuffer[256] = "";

    bool m_FolderImportPopupOpen = false;
    std::string m_PendingFolderImportPath;
    bool m_ImportExtPng = true;
    bool m_ImportExtJpg = true;
    bool m_ImportExtBmp = false;
    bool m_ImportExtTga = false;

    class EditorModule* m_CachedEditor = nullptr;
    class CompositeModule* m_CachedComposite = nullptr;
    int* m_CachedActiveTab = nullptr;
    int m_CachedRawWorkspaceTabId = -1;
    std::function<void(const std::string&)> m_OnLoadEditorProject;

    float m_ImportStatusAlpha = 0.0f;
    float m_ExportStatusAlpha = 0.0f;
    float m_SaveStatusAlpha = 0.0f;
    float m_LoadStatusAlpha = 0.0f;
    float m_EmptyStateAlpha = 0.0f;
    float m_LibraryLoadingStateAlpha = 0.0f;
    float m_FilterSplitterHover = 0.0f;
    float m_ProjectPreviewMenuHover = 0.0f;
    LibraryPreviewLaunchRect m_ProjectPreviewLaunchRect;
    float m_AssetPreviewMenuHover = 0.0f;
    LibraryPreviewLaunchRect m_AssetPreviewLaunchRect;

    // Multi-select state
    std::unordered_set<std::string> m_SelectedProjects;
    std::unordered_set<std::string> m_SelectedAssets;
    std::string m_LastClickedProject;
    std::string m_LastClickedAsset;

    // Delete confirmation
    bool m_DeleteConfirmOpen = false;
    bool m_DeletingAssets = false; // false = projects, true = assets
    std::vector<std::string> m_PendingDeleteFileNames;

    // Tag filtering
    std::unordered_set<std::string> m_ActiveTagFilters;
    bool m_FilterNoTag = false;
    char m_AddTagBuffer[128] = "";

    std::unordered_map<std::string, LibraryCardMotionState> m_ProjectCardMotion;
    std::unordered_map<std::string, LibraryCardMotionState> m_AssetCardMotion;
    std::unordered_set<std::string> m_IntroducedProjectCards;
    std::unordered_set<std::string> m_IntroducedAssetCards;
    LibraryRenderStats m_LastRenderStats;
    std::vector<LibraryCachedPackedCard> m_CachedPackedCards;
    std::string m_CachedLayoutKey;
    float m_CachedPackedHeight = 0.0f;

    unsigned int m_OptionsIconTex = 0;
    unsigned int m_AllProjectsIconTex = 0;
    unsigned int m_AssetsIconTex = 0;
    float m_ScrollTargetY = -1.0f;
    float m_ScrollCurrentY = -1.0f;
};
