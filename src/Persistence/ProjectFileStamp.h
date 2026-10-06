#pragma once
#include <filesystem>
#include <optional>

namespace Stack::Project {
// Lightweight saved-file baseline for sessions that have not adopted a store.
struct ProjectFileStamp {
    std::filesystem::path path;
    std::filesystem::file_time_type modified;
    std::uintmax_t size = 0;

    static std::optional<ProjectFileStamp> Read(const std::filesystem::path& path) {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) return std::nullopt;
        const auto modified = std::filesystem::last_write_time(path, error);
        if (error) return std::nullopt;
        return ProjectFileStamp{path, modified, size};
    }
    bool operator==(const ProjectFileStamp& other) const {
        return path == other.path && modified == other.modified && size == other.size;
    }
};
}
