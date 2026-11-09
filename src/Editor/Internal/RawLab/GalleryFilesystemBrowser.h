#pragma once
#include <filesystem>
#include <memory>
#include <optional>
namespace Stack::Editor::RawLabInternal {
// Owns asynchronous directory listings, never photo/catalog mutations.
class GalleryFilesystemBrowser {
public:
    GalleryFilesystemBrowser();
    ~GalleryFilesystemBrowser();
    std::optional<std::filesystem::path> Render(const std::filesystem::path& current);
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
