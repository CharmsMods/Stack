#include "Library/Internal/LibraryProjectExport.h"
#include "Persistence/ProjectStore.h"
#include "Utils/AtomicExportFile.h"

#include <algorithm>
#include <array>
#include <fstream>

namespace Stack::Library {
namespace {
struct OwnedExportFolder {
    std::filesystem::path path;
    ~OwnedExportFolder() {
        if (!path.empty()) { std::error_code ec; std::filesystem::remove_all(path, ec); }
    }
};
}

bool ExportPortableProject(const std::filesystem::path& source,
    const std::filesystem::path& destination, std::string& error,
    const std::function<bool()>& isCanceled) try {
    error.clear();
    auto opened = Project::OpenProjectStore(source);
    if (!opened) { error = opened.message; return false; }
    if (opened.recoveredPreviousManifest) {
        error = "The saved project required recovery. Reopen, check, and save it before exporting.";
        return false;
    }
    std::vector<std::filesystem::path> protectedSources{source, opened.store->StoragePath()};
    if (opened.store->StorageKind() == Project::ProjectStorageKind::DirectoryBundle)
        protectedSources.push_back(Project::WorkingProjectDocumentPath(opened.store->StoragePath()));
    for (const auto& asset : opened.snapshot.embeddedAssets) {
        if (!asset.originalSourcePath.empty() && asset.originalSourcePath.find("://") == std::string::npos)
            protectedSources.push_back(std::filesystem::u8path(asset.originalSourcePath));
        const auto managed = opened.store->ResolveAssetFile(asset.assetId);
        if (!managed.empty()) protectedSources.push_back(managed);
    }
    FileSave::ExportFileTarget target;
    if (!FileSave::PrepareExportFileTarget(destination, protectedSources, {".stack"}, target, error)) return false;
    if (isCanceled && isCanceled()) { error = "Project export canceled."; return false; }

    std::error_code ec;
    std::filesystem::create_directories(target.approval.path.parent_path(), ec);
    if (ec) { error = "Could not create the project export folder: " + ec.message(); return false; }
    OwnedExportFolder staging;
    for (int attempt = 0; attempt < 64; ++attempt) {
        const auto candidate = target.approval.path.parent_path() /
            (".stack-project-export-" + Project::GenerateStableUuid());
        ec.clear();
        if (std::filesystem::create_directory(candidate, ec)) { staging.path = candidate; break; }
        if (ec && ec != std::errc::file_exists) { error = "Could not stage the project export: " + ec.message(); return false; }
    }
    if (staging.path.empty()) { error = "Could not reserve project export staging."; return false; }
    const auto packedPath = staging.path / "project.stack";
    auto packed = Project::ConvertProjectStore(opened.store, opened.snapshot, packedPath,
        Project::ProjectStorageKind::PortableFile);
    if (!packed) { error = packed.message; return false; }
    // The returned handle belongs only to the temporary copy, never the editor.
    packed.store.reset();
    const auto bytes = std::filesystem::file_size(packedPath, ec);
    if (ec || bytes == 0) { error = "Could not read the packaged project size."; return false; }
    return FileSave::WriteExportStreamAtomically(target, bytes, [&](std::ostream& output) {
        std::ifstream input(packedPath, std::ios::binary);
        if (!input) return false;
        std::array<char, 64 * 1024> buffer{};
        std::uintmax_t remaining = bytes;
        while (remaining != 0) {
            const auto count = static_cast<std::streamsize>(std::min<std::uintmax_t>(remaining, buffer.size()));
            input.read(buffer.data(), count);
            if (input.gcount() != count) return false;
            output.write(buffer.data(), count);
            if (!output.good()) return false;
            remaining -= static_cast<std::uintmax_t>(count);
        }
        return !input.bad();
    }, error, isCanceled);
} catch (const std::exception& exception) {
    error = "Could not export the project: " + std::string(exception.what());
    return false;
} catch (...) {
    error = "Could not export the project.";
    return false;
}
}
