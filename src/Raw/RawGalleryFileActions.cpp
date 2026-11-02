#include "Raw/RawGalleryFileActions.h"

#include "Persistence/ProjectStore.h"
#include "Raw/RawWorkspace.h"
#include "Raw/RawLoader.h"

#include <algorithm>
#include <cctype>
#include <windows.h>
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
    std::string value = NormalizePath(path).generic_u8string();
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
    const std::string stem = fileName.stem().u8string();
    const std::string extension = fileName.extension().u8string();
    for (unsigned int index = 2; index < 10000u; ++index) {
        candidate = folder /
            std::filesystem::u8path(stem + " " + std::to_string(index) + extension);
        error.clear();
        if (!std::filesystem::exists(candidate, error)) return candidate;
    }
    return folder / std::filesystem::u8path(stem + " recovered" + extension);
}

bool MoveOne(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::string& errorMessage) {
    // The destination may have appeared since UniqueDestination checked it.
    if (MoveFileExW(source.c_str(), destination.c_str(), 0)) return true;
    const std::error_code error(GetLastError(), std::system_category());
    errorMessage = "Could not move " + source.u8string() + " to " +
        destination.u8string() + ": " + error.message();
    return false;
}

bool HasManagedAncestor(std::filesystem::path parent, const std::filesystem::path& root) {
    while (IsPathInside(parent, root)) {
        if (ComparablePath(parent) == ComparablePath(parent.parent_path() / "Closet") ||
            Stack::Project::IsWorkingProjectFile(parent)) return true;
        parent = parent.parent_path();
    }
    return false;
}

Result MoveProjectBundlesToTrash(
    const std::vector<std::filesystem::path>& projectPaths,
    const char* category, const std::filesystem::path& workspaceRoot, bool allowPortable = false) {
    Result result;
    for (const std::filesystem::path& requested : projectPaths) {
        const auto requestedPath = NormalizePath(requested);
        const bool bundle = !requestedPath.empty() && Stack::Project::IsWorkingProjectFile(requestedPath);
        const bool portable = !bundle && allowPortable && Stack::Project::IsPortableV3Project(requestedPath);
        if (!bundle && !portable) {
            result.errors.push_back("No saved project at " + requestedPath.u8string());
            continue;
        }
        const auto project = bundle ? Stack::Project::ResolveProjectStoreRoot(requestedPath) : requestedPath;
        const auto layout = Stack::RawWorkspace::BuildManagedLayout(workspaceRoot);
        if (!workspaceRoot.empty()) {
            std::error_code ec;
            const auto resolved = std::filesystem::canonical(project, ec);
            if (ec || ComparablePath(resolved) != ComparablePath(project) ||
                !(IsPathInside(project, layout.projectsDirectory) ||
                  (allowPortable && IsPathInside(project, layout.workspaceRoot) &&
                   !HasManagedAncestor(project.parent_path(), layout.workspaceRoot)))) {
                result.errors.push_back("The project is outside the open folder or inside another managed store.");
                continue;
            }
        }
        const std::filesystem::path trash = workspaceRoot.empty()
            ? project.parent_path() / "Trash" / category
            : layout.projectTrashDirectory / category;
        std::error_code error;
        if (std::filesystem::weakly_canonical(trash, error) != NormalizePath(trash) || error) {
            result.errors.emplace_back("The project Trash folder is redirected or inaccessible.");
            continue;
        }
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
    return MoveProjectBundlesToTrash(projectPaths, "Deleted", workspaceRoot, true);
}

Result DeleteSourcesToTrash(const std::filesystem::path& workspaceRoot,
    const std::vector<std::filesystem::path>& paths) {
    Result result;
    if (paths.empty()) return result;
    std::error_code error;
    const auto root = std::filesystem::canonical(workspaceRoot, error);
    if (workspaceRoot.empty() || error) {
        result.errors.emplace_back("Open the source folder before moving captures to Trash.");
        return result;
    }
    const auto layout = Stack::RawWorkspace::BuildManagedLayout(root);
    std::string storageError;
    if (!Stack::RawWorkspace::IsMarkedCloset(layout.dataDirectory) && !Stack::RawWorkspace::EnsureManagedFolders(root, &storageError)) {
        result.errors.push_back(storageError);
        return result;
    }
    const auto trash = layout.sourceTrashDirectory;
    if (std::filesystem::weakly_canonical(trash, error) != trash || error) {
        result.errors.emplace_back("The source Trash folder is redirected or inaccessible.");
        return result;
    }
    std::filesystem::create_directories(trash, error);
    if (error) {
        result.errors.push_back("Could not open source Trash: " + error.message());
        return result;
    }
    for (const auto& path : paths) {
        const auto source = std::filesystem::canonical(path, error);
        if (error || !IsPathInside(source, root) || !IsPathInside(path, workspaceRoot) ||
            ComparablePath(source) != ComparablePath(path) ||
            !std::filesystem::is_regular_file(source, error) || error ||
            (!Raw::RawLoader::IsRawPath(source.u8string()) && !Stack::RawWorkspace::DefaultRawPathPredicate(source)) || HasManagedAncestor(source.parent_path(), root)) {
            result.errors.push_back("Not an original capture in the open source folder: " + path.u8string());
            continue;
        }
        const auto destination = UniqueDestination(trash, source.filename());
        std::string message;
        if (MoveOne(source, destination, message)) {
            ++result.moved;
            result.destinations.push_back(destination);
        } else {
            result.errors.push_back(std::move(message));
        }
    }
    return result;
}

} // namespace Stack::RawGalleryFileActions
