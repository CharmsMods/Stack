#include "Raw/RawGalleryFileActions.h"

#include "Persistence/ProjectStore.h"
#include "Raw/RawWorkspace.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <system_error>

namespace Stack::RawGalleryFileActions {
namespace {

std::filesystem::path NormalizePath(const std::filesystem::path& path) {
    if (path.empty()) return {};
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

std::string ComparablePath(const std::filesystem::path& path) {
    std::string value = NormalizePath(path).generic_string();
#if defined(_WIN32)
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
#endif
    return value;
}

bool IsPathInside(
    const std::filesystem::path& candidate,
    const std::filesystem::path& root) {
    const std::string candidateKey = ComparablePath(candidate);
    std::string rootKey = ComparablePath(root);
    if (candidateKey.empty() || rootKey.empty()) return false;
    if (rootKey.back() != '/') rootKey.push_back('/');
    return candidateKey.rfind(rootKey, 0) == 0;
}

std::filesystem::path UniqueDestination(
    const std::filesystem::path& folder,
    const std::filesystem::path& fileName) {
    std::filesystem::path candidate = folder / fileName;
    std::error_code error;
    if (!std::filesystem::exists(candidate, error)) return candidate;
    const std::string stem = fileName.stem().string();
    const std::string extension = fileName.extension().string();
    for (unsigned int index = 2; index < 10000u; ++index) {
        candidate = folder /
            (stem + " " + std::to_string(index) + extension);
        error.clear();
        if (!std::filesystem::exists(candidate, error)) return candidate;
    }
    return folder / (stem + " recovered" + extension);
}

bool MoveOne(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::string& errorMessage) {
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    if (!error) return true;
    errorMessage = "Could not move " + source.string() + " to " +
        destination.string() + ": " + error.message();
    return false;
}

Result MoveProjectBundlesToTrash(
    const std::vector<std::filesystem::path>& projectPaths,
    const char* category, const std::filesystem::path& workspaceRoot) {
    Result result;
    for (const std::filesystem::path& requested : projectPaths) {
        const std::filesystem::path project = NormalizePath(requested);
        if (project.empty() ||
            !Stack::Project::IsDirectoryProjectBundle(project)) {
            result.errors.push_back(
                "Only complete managed project bundles can be moved to Trash: " +
                project.string());
            continue;
        }
        const auto layout = Stack::RawWorkspace::BuildManagedLayout(workspaceRoot);
        if (!workspaceRoot.empty() && !IsPathInside(project, layout.projectsDirectory)) {
            result.errors.push_back("The project is outside the open workspace.");
            continue;
        }
        const std::filesystem::path trash = workspaceRoot.empty()
            ? project.parent_path() / "Trash" / category
            : layout.projectTrashDirectory / category;
        std::error_code error;
        std::filesystem::create_directories(trash, error);
        if (error) {
            result.errors.push_back(
                "The project Trash folder could not be created: " +
                error.message());
            continue;
        }
        const std::filesystem::path destination =
            UniqueDestination(trash, project.filename());
        std::string moveError;
        if (!MoveOne(project, destination, moveError)) {
            result.errors.push_back(std::move(moveError));
            continue;
        }
        ++result.moved;
        result.destinations.push_back(destination);
    }
    return result;
}

} // namespace

Result RevertProjectsToTrash(
    const std::vector<std::filesystem::path>& projectPaths,
    const std::filesystem::path& workspaceRoot) {
    return MoveProjectBundlesToTrash(projectPaths, "Reverted", workspaceRoot);
}

Result DeleteProjectsToTrash(
    const std::filesystem::path& workspaceRoot,
    const std::vector<std::filesystem::path>& projectPaths) {
    return MoveProjectBundlesToTrash(projectPaths, "Deleted", workspaceRoot);
}

} // namespace Stack::RawGalleryFileActions
