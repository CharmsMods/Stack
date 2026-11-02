#pragma once
#include "Raw/RawImageData.h"
#include <filesystem>

namespace Stack::GalleryPresets {
constexpr int previewSize = 256;
std::shared_ptr<const Raw::RawImageData> Comparison(const std::filesystem::path& path);
std::filesystem::path Render(const std::filesystem::path& project, std::uint64_t revision,
    const std::filesystem::path& previews, const std::shared_ptr<const Raw::RawImageData>& comparison);
}
