#pragma once

#include <cstdint>
#include <string_view>

namespace Stack::Editor::RawLabInternal {
// Presentation-only source formats. Aliases share a switch; unknown formats
// remain reachable under Other as decoder support grows.
struct GalleryFileType { const char* label; std::string_view extension, alias; };
inline constexpr GalleryFileType GalleryFileTypes[]{
    {"JPEG", "jpg", "jpeg"}, {"PNG", "png", {}}, {"TIFF", "tif", "tiff"},
    {"AVIF", "avif", {}}, {"WebP", "webp", {}}, {"HEIC / HEIF", "heic", "heif"},
    {"BMP", "bmp", {}}, {"TGA", "tga", {}}, {"DNG", "dng", {}},
    {"ARW", "arw", {}}, {"CR2", "cr2", {}}, {"CR3", "cr3", {}},
    {"NEF", "nef", {}}, {"NRW", "nrw", {}}, {"ORF", "orf", {}},
    {"RAF", "raf", {}}, {"RW2", "rw2", {}}, {"PEF", "pef", {}},
    {"RAW", "raw", {}}, {"3FR", "3fr", {}}, {"FFF", "fff", {}},
    {"IIQ", "iiq", {}}, {"RWL", "rwl", {}}, {"SR2", "sr2", {}},
    {"SRF", "srf", {}}, {"Other", {}, {}}
};
inline constexpr unsigned GalleryFileTypeCount = sizeof(GalleryFileTypes) / sizeof(GalleryFileTypes[0]);
inline constexpr std::uint64_t AllGalleryFileTypes = (std::uint64_t{1} << GalleryFileTypeCount) - 1;

inline bool GalleryFileTypeMatches(std::string_view path, std::uint64_t enabled) {
    if (enabled == AllGalleryFileTypes) return true;
    const auto dot = path.find_last_of('.'), slash = path.find_last_of("/\\");
    const auto extension = dot != path.npos && (slash == path.npos || dot > slash)
        ? path.substr(dot + 1) : std::string_view{};
    const auto equal = [&](std::string_view expected) {
        if (expected.empty() || expected.size() != extension.size()) return false;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            const auto c = extension[i];
            if ((c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c) != expected[i]) return false;
        }
        return true;
    };
    for (unsigned i = 0; i + 1 < GalleryFileTypeCount; ++i)
        if (equal(GalleryFileTypes[i].extension) || equal(GalleryFileTypes[i].alias))
            return (enabled & (std::uint64_t{1} << i)) != 0;
    return (enabled & (std::uint64_t{1} << (GalleryFileTypeCount - 1))) != 0;
}
} // namespace Stack::Editor::RawLabInternal
