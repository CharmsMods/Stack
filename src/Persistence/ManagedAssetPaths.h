#pragma once

#include "Persistence/RawProjectModel.h"
#include <filesystem>
#include <fstream>

namespace Stack::Project {

// Used only while opening/capturing/storing projects, never by a frame loop.
// Assets have a canonical content address. Names and original paths are
// provenance; moving the entire workspace does not change the binding.
inline std::filesystem::path ClosetAssetRoot(std::filesystem::path projectRoot) {
    for (auto parent = projectRoot.parent_path(); !parent.empty();) {
        if (parent.filename() == "Closet") {
            std::ifstream input(parent / "workspace.json", std::ios::binary);
            if (input) {
                const auto marker = json::parse(input, nullptr, false);
                if (marker.is_object() && marker.value("schema", std::string()) == "stack.rawWorkspace.closet")
                    return parent / "Assets";
            }
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

inline std::filesystem::path ManagedAssetFilePath(
    const std::filesystem::path& projectRoot, const EmbeddedAssetRecord& asset) {
    const auto shared = ClosetAssetRoot(projectRoot);
    return shared.empty() ? projectRoot / std::filesystem::u8path(asset.projectAssetPath)
                          : shared / ContentAssetName(asset);
}
} // namespace Stack::Project
