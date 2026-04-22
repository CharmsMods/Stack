#pragma once

#include "Async/TaskState.h"
#include "Persistence/StackBinaryFormat.h"
#include "ProjectData.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class EditorModule;
class RenderTab;
class CompositeModule;

enum class ConflictAction {
    KeepBoth,
    Ignore,
    Replace
};

struct ImportConflict {
    int importedProjectIndex = -1; 
    std::string localProjectFileName;
    
    // Metadata for UI (to avoid re-loading documents just for text)
    std::string localName;
    std::string localTimestamp;
    int localWidth = 0;
    int localHeight = 0;
    
    std::string importedName;
    std::string importedTimestamp;
    int importedWidth = 0;
    int importedHeight = 0;

    unsigned int localPreviewTex = 0;
    unsigned int importedPreviewTex = 0;
    bool areIdentical = false;
    bool previewsReady = false;
};

class LibraryManager {
public:
    LibraryManager();
    ~LibraryManager();

    static LibraryManager& Get() {
        static LibraryManager instance;
        return instance;
    }

    void RefreshLibrary(std::function<void(int current, int total, const std::string& name)> progressCallback = {});
    void UploadLibraryTextures();
    void TickAutoRefresh();

    int GetProjectCount() const;

    void RequestSaveProject(const std::string& name, EditorModule* editor, const std::string& existingFileName = "");
    void RequestLoadProject(const std::string& fileName, EditorModule* editor, std::function<void(bool)> onComplete = {});
    void RequestSaveRenderProject(
        const std::string& name,
        const StackBinaryFormat::json& renderPayload,
        const std::vector<unsigned char>& beautyPixels,
        int width,
        int height,
        const std::string& existingFileName = "",
        std::function<void(bool, const std::string&, const std::string&)> onComplete = {});
    void RequestLoadRenderProject(const std::string& fileName, RenderTab* renderTab, std::function<void(bool)> onComplete = {});
    void RequestProjectPreview(const std::shared_ptr<ProjectEntry>& project);
    void RequestAssetPreview(const std::shared_ptr<AssetEntry>& asset);
    void CancelProjectPreviewRequests();
    void CancelAssetPreviewRequests();

    bool RenameProject(const std::string& fileName, const std::string& newName);
    bool DeleteProject(const std::string& fileName);
    bool ExportAsset(const std::string& fileName, const std::string& destinationPath);

    void RequestExportLibraryBundle(const std::string& destinationPath);
    void RequestImportLibraryBundle(const std::string& sourcePath);
    void RequestImportWebProject(const std::string& sourcePath);
    void RequestSaveCompositeProject(const std::string& name, CompositeModule* composite, const std::string& existingFileName = "");
    void RequestLoadCompositeProject(const std::string& fileName, CompositeModule* composite, std::function<void(bool)> onComplete = {});
    void RequestLoadCompositeProjectFromPath(const std::filesystem::path& absolutePath, CompositeModule* composite, std::function<void(bool)> onComplete = {});

    void RequestImportAndLoad(
        const std::string& sourcePath,
        EditorModule* editor,
        RenderTab* renderTab,
        CompositeModule* composite = nullptr,
        std::function<void(int)> onTabSwitchRequested = {});

    // New: Direct load from an absolute path (for projects already in the library or just dropped)
    void RequestLoadProjectFromPath(const std::filesystem::path& absolutePath, EditorModule* editor, std::function<void(bool)> onComplete = {});
    void RequestLoadRenderProjectFromPath(const std::filesystem::path& absolutePath, RenderTab* renderTab, std::function<void(bool)> onComplete = {});

    bool HasPendingConflicts() const { return !m_PendingConflicts.empty(); }
    const std::vector<ImportConflict>& GetPendingConflicts() const { return m_PendingConflicts; }
    void ResolveConflict(int index, ConflictAction action, const std::string& newName = "");
    void ClearConflicts();
    void PrepareConflictPreview(int index);
    void ProcessDeferredDeletions();

    const StackBinaryFormat::LibraryBundleDocument& GetActiveImportBundle() const { return m_ActiveImportBundle; }

    static bool DecodeImageBytes(
        const std::vector<unsigned char>& encodedImage,
        std::vector<unsigned char>& outPixels,
        int& outW,
        int& outH,
        int& outChannels);
    static void FlipImageRowsInPlace(std::vector<unsigned char>& pixels, int width, int height, int channels = 4);

    const std::vector<std::shared_ptr<ProjectEntry>>& GetProjects() const { return m_Projects; }
    const std::vector<std::shared_ptr<AssetEntry>>& GetAssets() const { return m_Assets; }

    Async::TaskState GetSaveTaskState() const { return m_SaveTaskState; }
    const std::string& GetSaveStatusText() const { return m_SaveStatusText; }

    Async::TaskState GetProjectLoadTaskState() const { return m_ProjectLoadTaskState; }
    const std::string& GetProjectLoadStatusText() const { return m_ProjectLoadStatusText; }

    Async::TaskState GetImportTaskState() const { return m_ImportTaskState; }
    const std::string& GetImportStatusText() const { return m_ImportStatusText; }

    Async::TaskState GetExportTaskState() const { return m_ExportTaskState; }
    const std::string& GetExportStatusText() const { return m_ExportStatusText; }

    const std::filesystem::path& GetLibraryPath() const { return m_LibraryPath; }

private:
    void InitializeThumbnail(std::shared_ptr<ProjectEntry> project);
    void InitializeAssetThumbnail(std::shared_ptr<AssetEntry> asset);
    std::vector<unsigned char> GenerateThumbnailBytes(const std::vector<unsigned char>& pixels, int width, int height);
    void ReleaseProjectTextures(std::shared_ptr<ProjectEntry> project);
    void ReleaseAssetTextures(std::shared_ptr<AssetEntry> asset);

    bool LoadProjectDocument(
        const std::string& fileName,
        StackBinaryFormat::ProjectDocument& outDocument,
        const StackBinaryFormat::ProjectLoadOptions& options);

    bool WriteLibraryBundle(const std::string& destinationPath);
    bool ImportLibraryBundle(const std::string& sourcePath);
    void FinalizeImport(const StackBinaryFormat::LibraryBundleDocument& bundle, const std::vector<int>& skippedProjectIndices);

    std::uintmax_t BuildLibrarySignature() const;
    std::filesystem::path BuildAssetPathForProjectFile(const std::string& projectFileName) const;

    std::vector<std::shared_ptr<ProjectEntry>> m_Projects;
    std::vector<std::shared_ptr<AssetEntry>> m_Assets;
    std::mutex m_ProjectsMutex;

    std::filesystem::path m_LibraryPath;
    std::filesystem::path m_AssetsPath;
    std::uintmax_t m_LastLibrarySignature = 0;

    Async::TaskState m_SaveTaskState = Async::TaskState::Idle;
    std::string m_SaveStatusText;
    std::uint64_t m_SaveGeneration = 0;

    Async::TaskState m_ProjectLoadTaskState = Async::TaskState::Idle;
    std::string m_ProjectLoadStatusText;
    std::uint64_t m_ProjectLoadGeneration = 0;

    Async::TaskState m_ImportTaskState = Async::TaskState::Idle;
    std::string m_ImportStatusText;
    std::uint64_t m_ImportGeneration = 0;

    Async::TaskState m_ExportTaskState = Async::TaskState::Idle;
    std::string m_ExportStatusText;
    std::uint64_t m_ExportGeneration = 0;

    std::uint64_t m_ProjectPreviewGeneration = 0;
    std::uint64_t m_AssetPreviewGeneration = 0;

    std::vector<ImportConflict> m_PendingConflicts;
    StackBinaryFormat::LibraryBundleDocument m_ActiveImportBundle;
    std::vector<unsigned int> m_DeferredTextureDeletions;
};
