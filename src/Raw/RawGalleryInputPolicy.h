#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace Stack::RawWorkspace {
inline std::string RawGalleryPathOrderKey(std::string path) {
    std::transform(path.begin(), path.end(), path.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return path;
}
// Keep Gallery discovery and explicit selected-file analysis on the same
// supported workflow. Decoder format support alone does not expand this list.
inline bool IsGalleryRawPath(const std::filesystem::path& path) {
    const auto extension = RawGalleryPathOrderKey(path.extension().u8string());
    return extension == ".arw" || extension == ".srf" || extension == ".sr2" ||
        extension == ".raw" || extension == ".dng";
}
} // namespace Stack::RawWorkspace
