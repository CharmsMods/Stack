#pragma once
#include "Platform/BrowserFileHistory.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <imgui.h>
namespace Stack::Editor::RawLabInternal {
// Owns asynchronous directory listings, never photo/catalog mutations.
class GalleryFilesystemBrowser {
public:
    GalleryFilesystemBrowser();
    ~GalleryFilesystemBrowser();
    void Open(const std::filesystem::path& current);
    struct Action {
        std::optional<std::filesystem::path> folder, image;
        std::vector<BrowserFiles::Change> changes;
    };
    using ProtectionProvider = std::function<std::vector<std::filesystem::path>()>;
    void SetProtectionProvider(ProtectionProvider provider) { protectionProvider_ = std::move(provider); }
    bool FileOperationBusy() const;
    Action RenderPopup();
private:
    std::optional<std::filesystem::path> Render(const std::filesystem::path& current);
    void RenderTiles(std::optional<std::filesystem::path>& navigate);
    void RenderFileDialogs();
    void RenderFileStatus();
    void RenderFileHistory();
    void RenderFolderCommands(const std::filesystem::path&);
    void BeginFolderName(BrowserFiles::Action, const std::filesystem::path&);
    void SubmitFile(BrowserFiles::Request);
    void PumpFiles(Action&);
    void FinishGridInput();
    bool FileInputAvailable() const;
    bool FolderDropTarget(const std::filesystem::path&, bool hovered);
    struct FileSession;
    std::shared_ptr<FileSession> files_;
    ProtectionProvider protectionProvider_;
    bool openRequested_ = false;
    ImVec2 popupSize_{};
    struct State;
    std::shared_ptr<State> state_;
};
}
