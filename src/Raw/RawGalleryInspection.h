#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Stack::RawGalleryInspection {

enum class Version {
    Before,
    After
};

struct Request {
    std::uint64_t requestId = 0;
    std::filesystem::path projectPath;
    std::filesystem::path sourcePath;
    std::string displayName;
    Version version = Version::After;
    bool cancel = false;
};

struct Result {
    std::uint64_t requestId = 0;
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    std::string status;
    std::string error;

    explicit operator bool() const {
        return error.empty() && !pixels.empty() && width > 0 && height > 0;
    }
};

} // namespace Stack::RawGalleryInspection
