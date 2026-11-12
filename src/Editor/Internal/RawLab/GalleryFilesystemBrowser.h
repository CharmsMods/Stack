#pragma once
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
    struct Action { std::optional<std::filesystem::path> folder,image; };
    Action RenderPopup();
private:
    std::optional<std::filesystem::path> Render(const std::filesystem::path& current);
    void RenderTiles(std::optional<std::filesystem::path>& navigate);
    bool openRequested_ = false;
    ImVec2 popupSize_{};
    struct State;
    std::shared_ptr<State> state_;
};
}
