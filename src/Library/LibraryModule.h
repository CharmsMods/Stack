#pragma once
#include "App/IAppModule.h"
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class LibraryModule : public IAppModule {
public:
    LibraryModule();
    ~LibraryModule() override;

    void Initialize() override;
    void RenderUI() override {} // Dummy for IAppModule
    void RenderUI(
        class EditorModule* editor,
        class RenderTab* renderTab,
        class CompositeModule* composite,
        int* activeTab = nullptr);
    void RenderGlobalPopups();
    void OpenProjectPreviewByFileName(const std::string& fileName);
    const char* GetName() override { return "Library"; }

private:
    void RenderProjectCard(const struct ProjectEntry& project, class EditorModule* editor);
    void RenderAssetCard(const struct AssetEntry& asset, class EditorModule* editor);
    void RenderPreviewPopup(
        class EditorModule* editor,
        class RenderTab* renderTab,
        class CompositeModule* composite,
        int* activeTab = nullptr);
    void RenderAssetPreviewPopup(
        class EditorModule* editor,
        class RenderTab* renderTab,
        class CompositeModule* composite,
        int* activeTab = nullptr);
    void RenderConfirmRenderLoadPopup(class RenderTab* renderTab, int* activeTab = nullptr);
    void RenderConfirmLoadPopup();
    void RenderFolderImportPopup();
    void RenderImportConflictPopup();
    void RenderAssetConflictPopup();
    void RenderLibraryMenuOptions(bool importBusy, bool exportBusy);
    void RenderDeleteConfirmPopup();
    void SyncRenameBuffer();
    
    std::shared_ptr<struct ProjectEntry> m_PreviewProject = nullptr;
    std::shared_ptr<struct AssetEntry> m_PreviewAsset = nullptr;
    bool m_ShowAssets = false;
    char m_SearchFilter[128] = "";
    char m_RenameBuffer[256] = "";
    float m_CompareSplit = 0.5f;
    float m_ConflictCompareSplit = 0.5f;
    float m_AssetConflictCompareSplit = 0.5f;
    float m_FilterPanelWidth = 220.0f;
    std::string m_RenameTargetFileName;
    std::string m_PendingRenderProjectFileName;
    bool m_RenderLoadConfirmOpen = false;

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
};
