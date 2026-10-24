#include "Utils/AtomicExportFile.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <limits>
#include <mutex>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <sys/stat.h>
#endif

namespace Stack::FileSave {
namespace {

// Publication is short; encoding and file writes stay parallel. A single
// publication lock also covers Windows path aliases without a path-key cache.
std::mutex publicationMutex;
std::atomic<std::uint64_t> stagingSequence{0};

bool CaptureFileIdentity(const std::filesystem::path& path,
    std::uint64_t& device, std::uint64_t& file, std::string& error) {
#if defined(_WIN32)
    const HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = "The export file identity could not be opened: " +
            std::system_category().message(static_cast<int>(GetLastError()));
        return false;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    const bool ok = GetFileInformationByHandle(handle, &info) != FALSE;
    const DWORD failure = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (!ok) {
        error = "The export file identity could not be checked: " +
            std::system_category().message(static_cast<int>(failure));
        return false;
    }
    device = info.dwVolumeSerialNumber;
    file = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
#else
    struct stat info{};
    if (::stat(path.c_str(), &info) != 0) {
        error = "The export file identity could not be checked: " +
            std::generic_category().message(errno);
        return false;
    }
    device = static_cast<std::uint64_t>(info.st_dev);
    file = static_cast<std::uint64_t>(info.st_ino);
#endif
    return true;
}

bool CheckSourceProtection(const ExportFileTarget& target, std::string& error) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(target.approval.path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        error = "The export destination could not be inspected: " + ec.message();
        return false;
    }
    if (!ec && std::filesystem::is_symlink(status)) {
        error = "Choose a regular export file, not a symbolic link.";
        return false;
    }
    ec.clear();
    const bool exists = std::filesystem::exists(target.approval.path, ec);
    if (ec) {
        error = "The export destination could not be inspected: " + ec.message();
        return false;
    }
    for (const auto& source : target.protectedSources) {
        if (source.empty()) continue;
        ec.clear();
        const auto normalizedSource = std::filesystem::weakly_canonical(source, ec);
        if (ec) {
            error = "An original source path could not be checked: " + ec.message();
            return false;
        }
        if (normalizedSource == target.approval.path) {
            error = "Export cannot replace an original camera source. Choose another file.";
            return false;
        }
        if (exists) {
            ec.clear();
            const bool same = std::filesystem::equivalent(source, target.approval.path, ec);
            if (same && !ec) {
                error = "Export cannot replace an alias of an original camera source. Choose another file.";
                return false;
            }
            // Missing originals are legitimate in managed projects. Other
            // inspection errors must not be interpreted as proof of safety.
            if (ec && ec != std::errc::no_such_file_or_directory) {
                error = "An original source alias could not be checked: " + ec.message();
                return false;
            }
        }
    }
    return true;
}

struct StagingDirectory {
    std::filesystem::path path;
    ~StagingDirectory() {
        if (path.empty()) return;
        std::error_code ignored;
        std::filesystem::remove(path / "image", ignored);
        std::filesystem::remove(path, ignored);
    }
};

bool ReplaceFile(const std::filesystem::path& staged,
    const std::filesystem::path& destination, bool replaceExisting, std::string& error) {
#if defined(_WIN32)
    if (MoveFileExW(staged.c_str(), destination.c_str(),
            (replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0) | MOVEFILE_WRITE_THROUGH)) return true;
    error = "Windows could not publish the completed export: " +
        std::system_category().message(static_cast<int>(GetLastError()));
#else
    std::error_code ec;
    if (replaceExisting) {
        std::filesystem::rename(staged, destination, ec);
    } else {
        // Unlike rename, link creation fails if another process wins the name.
        std::filesystem::create_hard_link(staged, destination, ec);
    }
    if (!ec) return true;
    error = "Could not publish the completed export: " + ec.message();
#endif
    return false;
}
} // namespace

bool PrepareExportFileTarget(const std::filesystem::path& destination,
    const std::vector<std::filesystem::path>& protectedSources,
    const std::vector<std::string>& allowedExtensions,
    ExportFileTarget& target, std::string& error) {
    error.clear();
    target = {};
    if (destination.empty() || destination.filename().empty()) {
        error = "The export destination does not name a file.";
        return false;
    }
    std::string extension = destination.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (std::find(allowedExtensions.begin(), allowedExtensions.end(), extension) == allowedExtensions.end()) {
        error = "Choose a filename with the export format's extension.";
        return false;
    }
    std::error_code ec;
    // Resolve the parent only: resolving the final symlink would hide it from
    // the protection check, and could overwrite a differently named original.
    const auto absolute = std::filesystem::absolute(destination, ec);
    if (ec) { error = "The export path could not be resolved: " + ec.message(); return false; }
    const auto parent = std::filesystem::weakly_canonical(absolute.parent_path(), ec);
    if (ec) { error = "The export folder could not be resolved: " + ec.message(); return false; }
    target.approval.path = parent / absolute.filename();
    target.protectedSources = protectedSources;
    if (!CheckSourceProtection(target, error)) return false;
    auto approval = CaptureTargetApproval(target.approval.path, ec);
    if (!approval) { error = "The export destination is unavailable: " + ec.message(); return false; }
    target.approval = std::move(*approval);
    return !target.approval.existed || CaptureFileIdentity(
        target.approval.path, target.deviceId, target.fileId, error);
}

bool WriteExportFileAtomically(const ExportFileTarget& target,
    const std::vector<unsigned char>& bytes, std::string& error,
    const std::function<bool()>& isCanceled) {
    error.clear();
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        error = "The encoded export is empty or too large to write."; return false;
    }
    if (target.approval.path.empty() || !CheckSourceProtection(target, error)) {
        if (error.empty()) error = "The export destination was not prepared.";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(target.approval.path.parent_path(), ec);
    if (ec) { error = "Could not create the export folder: " + ec.message(); return false; }
    StagingDirectory staging;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto candidate = target.approval.path.parent_path() /
            (".stack-export-" + std::to_string(stamp) + "-" + std::to_string(stagingSequence.fetch_add(1)));
        ec.clear();
        if (std::filesystem::create_directory(candidate, ec)) { staging.path = candidate; break; }
        if (ec && ec != std::errc::file_exists) { error = "Could not stage the export: " + ec.message(); return false; }
    }
    if (staging.path.empty()) { error = "Could not reserve a unique export staging folder."; return false; }
    const auto temporary = staging.path / "image";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) { error = "The temporary export file could not be opened."; return false; }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output.good()) { error = "The export bytes could not be completely written."; return false; }
    if (std::filesystem::file_size(temporary, ec) != bytes.size() || ec) {
        error = "The staged export size could not be verified."; return false;
    }
    std::lock_guard<std::mutex> lock(publicationMutex);
    if (isCanceled && isCanceled()) {
        error = "The export was canceled before publication."; return false;
    }
    if (!CheckSourceProtection(target, error)) return false;
    if (!TargetApprovalStillMatches(target.approval, ec)) {
        error = "The destination changed while exporting. Choose the destination again.";
        return false;
    }
    if (target.approval.existed) {
        std::uint64_t device = 0, file = 0;
        if (!CaptureFileIdentity(target.approval.path, device, file, error)) return false;
        if (device != target.deviceId || file != target.fileId) {
            error = "The destination file was replaced while exporting. Choose it again.";
            return false;
        }
    }
    return ReplaceFile(temporary, target.approval.path, target.approval.existed, error);
}
} // namespace Stack::FileSave
