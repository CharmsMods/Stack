#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Stack::RawGalleryFileActions {

struct Result {
    std::size_t moved = 0;
    std::vector<std::filesystem::path> destinations;
    std::vector<std::string> errors;

    explicit operator bool() const {
        return moved > 0 && errors.empty();
    }
};

Result RevertProjectsToTrash(
    const std::vector<std::filesystem::path>& projectPaths,
    const std::filesystem::path& workspaceRoot = {});

// Only project-owned bundles may be moved. Original camera files are never
// accepted by this API, including when they are referenced by a saved project.
Result DeleteProjectsToTrash(
    const std::filesystem::path& workspaceRoot,
    const std::vector<std::filesystem::path>& projectPaths);

} // namespace Stack::RawGalleryFileActions
