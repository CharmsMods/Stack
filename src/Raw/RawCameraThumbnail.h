#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace Stack::RawWorkspace {

// A bounded JPEG inside the original. Coordinates and orientation describe
// the embedded image, which can differ from the active sensor area.
struct CameraThumbnail {
    std::uint64_t offset = 0;
    std::uint32_t bytes = 0;
    int width = 0, height = 0, orientation = 1;

    bool Fits(std::uintmax_t fileSize) const;
    void DisplaySize(int maxEdge, int& outWidth, int& outHeight) const;
};

CameraThumbnail FindCameraThumbnail(const std::filesystem::path& path, std::uintmax_t fileSize, int targetEdge);
bool DecodeCameraThumbnail(const std::filesystem::path& path, const CameraThumbnail& preview,
    int maxEdge, std::vector<unsigned char>& rgba, int& width, int& height);
bool DecodeRasterThumbnail(const std::filesystem::path& path, int maxEdge,
    std::vector<unsigned char>& rgba, int& width, int& height);
enum class GalleryPixelSource { Unavailable, CameraJpeg, RawSamples };
GalleryPixelSource LoadOriginalGalleryPixels(const std::filesystem::path& path, const CameraThumbnail& preview,
    int maxEdge, bool largeView, std::vector<unsigned char>& rgba, int& width, int& height,
    const std::function<bool()>& cancelled = {});
} // namespace Stack::RawWorkspace
