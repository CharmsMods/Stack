#pragma once

#include "Persistence/RawProjectModel.h"
#include <filesystem>
#include <fstream>

namespace Stack::Project {

inline bool IsPhotoCloset(const std::filesystem::path& directory) {
    std::ifstream input(directory / "workspace.json", std::ios::binary);
    if (!input) return false;
    const auto marker = json::parse(input, nullptr, false);
    return marker.is_object() && marker.contains("schema") && marker["schema"] == "stack.rawWorkspace.closet" &&
        marker.contains("schemaVersion") && marker["schemaVersion"] == 1;
}

inline std::filesystem::path UniqueWorkingProjectPath(const std::filesystem::path& directory, std::string name) {
    for (auto& ch : name) {
        if (static_cast<unsigned char>(ch) < 32 || std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos)
            ch = '_';
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    if (name.empty()) name = "Project";
    for (unsigned n = 1; n <= 10000; ++n) {
        const auto suffix = n == 1 ? std::string{} : "-" + std::to_string(n);
        const auto path = directory / std::filesystem::u8path(name + suffix + ".stack");
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) && !ec) return path;
    }
    return directory / std::filesystem::u8path(name + "-" + GenerateStableUuid() + ".stack");
}
// Used only while opening/capturing/storing projects, never by a frame loop.
// Assets have a canonical content address. Names and original paths are
// provenance; moving the entire workspace does not change the binding.
inline std::filesystem::path ClosetAssetRoot(std::filesystem::path projectRoot) {
    for (auto parent = projectRoot.parent_path(); !parent.empty();) {
        if (parent.filename() == "Closet") {
            if (IsPhotoCloset(parent)) return parent / "Assets";
            return {};
        }
        const auto next = parent.parent_path();
        if (next == parent) break;
        parent = next;
    }
    return {};
}

inline std::filesystem::path ContentAssetName(const EmbeddedAssetRecord& asset) {
    return asset.sha256 + "-" + std::to_string(asset.byteLength) + ".original";
}

inline void SetSourceLocation(const std::filesystem::path& project,
    const std::filesystem::path& path, EmbeddedAssetRecord& asset) {
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    asset.originalSourcePath = absolute.u8string();
    asset.workspaceRelativeSourcePath.clear();
    const auto shared = ClosetAssetRoot(project);
    if (shared.empty()) return;
    const auto relative = absolute.lexically_relative(shared.parent_path().parent_path());
    if (!relative.empty() && !relative.has_root_path() && *relative.begin() != "..")
        asset.workspaceRelativeSourcePath = relative.generic_u8string();
}

inline std::filesystem::path ExpectedSourcePath(
    const std::filesystem::path& projectRoot, const EmbeddedAssetRecord& asset) {
    const auto shared = ClosetAssetRoot(projectRoot);
    if (!asset.workspaceRelativeSourcePath.empty() && !shared.empty())
        return (shared.parent_path().parent_path() /
            std::filesystem::u8path(asset.workspaceRelativeSourcePath)).lexically_normal();
    return std::filesystem::u8path(asset.originalSourcePath);
}

inline std::filesystem::path ManagedAssetFilePath(
    const std::filesystem::path& projectRoot, const EmbeddedAssetRecord& asset) {
    const auto shared = ClosetAssetRoot(projectRoot);
    if (asset.linkedSource) return ExpectedSourcePath(projectRoot, asset);
    return shared.empty() ? projectRoot.parent_path() / "assets" / ContentAssetName(asset)
                          : shared / ContentAssetName(asset);
}
} // namespace Stack::Project
