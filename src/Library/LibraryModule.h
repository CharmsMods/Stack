#pragma once
#include "App/IAppModule.h"
#include <memory>
#include <string>

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
    void RenderImportConflictPopup(class EditorModule* editor);
    void SyncRenameBuffer();
    
    std::shared_ptr<struct ProjectEntry> m_PreviewProject = nullptr;
    std::shared_ptr<struct AssetEntry> m_PreviewAsset = nullptr;
    bool m_ShowAssets = false;
    char m_SearchFilter[128] = "";
    char m_RenameBuffer[256] = "";
    float m_CompareSplit = 0.5f;
    float m_ConflictCompareSplit = 0.5f;
    float m_FilterPanelWidth = 220.0f;
    std::string m_RenameTargetFileName;
    std::string m_PendingRenderProjectFileName;
    bool m_RenderLoadConfirmOpen = false;
};
