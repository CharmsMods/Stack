#include "Library/Internal/LibraryImportPlan.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>
#include <system_error>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Stack::Library {
namespace {
struct DestinationLess {
    bool operator()(const std::filesystem::path& a, const std::filesystem::path& b) const {
#if defined(_WIN32)
        const int result = CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE);
        if (!result) throw std::runtime_error("Library destination names could not be compared.");
        return result == CSTR_LESS_THAN;
#else
        // Be conservative on cloud tests too: the product targets Windows.
        auto left = a.generic_u8string(), right = b.generic_u8string();
        const auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
        std::transform(left.begin(), left.end(), left.begin(), lower);
        std::transform(right.begin(), right.end(), right.begin(), lower);
        return left < right;
#endif
    }
};
bool IdenticalBytes(const std::filesystem::path& path,
    const std::vector<unsigned char>& bytes) {
    std::error_code error;
    if (std::filesystem::file_size(path, error) != bytes.size() || error) return false;
    std::ifstream input(path, std::ios::binary);
    std::array<char, 65536> buffer{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = std::min(buffer.size(), bytes.size() - offset);
        input.read(buffer.data(), static_cast<std::streamsize>(count));
        if (input.gcount() != static_cast<std::streamsize>(count) ||
            !std::equal(buffer.begin(), buffer.begin() + count, bytes.begin() + offset,
                [](char a, unsigned char b) { return static_cast<unsigned char>(a) == b; })) return false;
        offset += count;
    }
    return !input.bad();
}
bool Reject(std::string& error, const std::filesystem::path& path, const char* reason) {
    error = "Import stopped before writing files: " + path.filename().u8string() +
        " " + reason + " Existing Library files were kept. Resolve the conflicting name and retry.";
    return false;
}
}

bool PrepareLibraryImportPlan(const std::vector<ImportProjectDestination>& projects,
    const std::vector<ImportAssetDestination>& assets,
    LibraryImportPlan& plan, std::string& error) {
    LibraryImportPlan prepared;
    error.clear();
    std::set<std::filesystem::path, DestinationLess> projectNames, assetNames;
    std::set<std::pair<std::uint64_t, std::uint64_t>> assetIdentities;
    for (std::size_t i = 0; i < projects.size(); ++i) {
        const auto& project = projects[i];
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(project.path, ec);
        if ((ec && ec != std::errc::no_such_file_or_directory) ||
            (!ec && std::filesystem::is_symlink(status)))
            return Reject(error, project.path, "cannot be used as a regular project destination.");
        ec.clear();
        const auto absolute = std::filesystem::absolute(project.path, ec);
        if (ec) return Reject(error, project.path, "cannot be resolved.");
        const auto normalized = std::filesystem::weakly_canonical(absolute, ec);
        if (ec || !projectNames.insert(normalized).second)
            return Reject(error, project.path, "has a duplicate or unavailable project destination.");
        if (!project.deferred && (std::filesystem::exists(project.path, ec) || ec))
            return Reject(error, project.path, "already exists or cannot be checked.");
    }
    for (std::size_t i = 0; i < assets.size(); ++i) {
        const auto& asset = assets[i];
        if (!asset.bytes || asset.bytes->empty())
            return Reject(error, asset.path, "has no image bytes.");
        PlannedAssetImport item;
        item.index = i;
        item.deferred = asset.deferred;
        std::string targetError;
        if (!FileSave::PrepareExportFileTarget(asset.path, {}, {".png"}, item.target, targetError)) {
            error = "Import stopped before writing files: " + asset.path.filename().u8string() +
                ": " + targetError + " Existing Library files were kept.";
            return false;
        }
        const auto& normalized = item.target.approval.path;
        if (!assetNames.insert(normalized).second)
            return Reject(error, asset.path, "has more than one image destination in this bundle.");
        if (projectNames.count(normalized))
            return Reject(error, asset.path, "is also a project destination.");
        if (item.target.approval.existed) {
            if (!assetIdentities.insert({item.target.deviceId, item.target.fileId}).second)
                return Reject(error, asset.path, "aliases another image destination in this bundle.");
            item.reuse = IdenticalBytes(item.target.approval.path, *asset.bytes);
            if (!item.reuse && !(asset.deferred && asset.projectPreviewReplacement))
                return Reject(error, asset.path, "already contains a different Library image.");
        }
        prepared.assets.push_back(std::move(item));
    }
    plan = std::move(prepared);
    return true;
}
} // namespace Stack::Library
