#include "AppPaths.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shlobj.h>
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <system_error>

namespace AppPaths {
namespace {

constexpr const char* kAppDirectoryName = "Stack";
constexpr const char* kSettingsFileName = "StackSettings.json";
constexpr const char* kInstalledMarkerFileName = "StackInstalledBuild.marker";

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::filesystem::path GetModulePath() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(buffer);
    }
#endif

    std::error_code ec;
    return std::filesystem::current_path(ec) / "Stack.exe";
}

std::filesystem::path GetKnownFolder(REFKNOWNFOLDERID folderId) {
#if defined(_WIN32)
    PWSTR rawPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(folderId, KF_FLAG_CREATE, nullptr, &rawPath)) && rawPath != nullptr) {
        std::filesystem::path result(rawPath);
        CoTaskMemFree(rawPath);
        return result;
    }
#else
    (void)folderId;
#endif
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

bool PathStartsWithCaseInsensitive(const std::filesystem::path& candidate, const std::filesystem::path& prefix) {
    const std::string candidateText = ToLowerAscii(candidate.lexically_normal().string());
    const std::string prefixText = ToLowerAscii(prefix.lexically_normal().string());
    if (prefixText.empty()) {
        return false;
    }
    if (candidateText.size() < prefixText.size()) {
        return false;
    }
    if (candidateText.compare(0, prefixText.size(), prefixText) != 0) {
        return false;
    }
    if (candidateText.size() == prefixText.size()) {
        return true;
    }
    const char next = candidateText[prefixText.size()];
    return next == '\\' || next == '/';
}

bool DirectoryContainsInstalledMarker(const std::filesystem::path& directory) {
    std::error_code ec;
    if (directory.empty()) {
        return false;
    }
    return std::filesystem::exists(directory / kInstalledMarkerFileName, ec) ||
           std::filesystem::exists(directory / "unins000.exe", ec);
}

InstallMode ResolveInstallMode(const std::filesystem::path& executableDirectory) {
    if (DirectoryContainsInstalledMarker(executableDirectory)) {
        return InstallMode::Installed;
    }

#if defined(_WIN32)
    const std::filesystem::path programFiles = GetKnownFolder(FOLDERID_ProgramFiles);
    const std::filesystem::path programFilesX86 = GetKnownFolder(FOLDERID_ProgramFilesX86);
    if (PathStartsWithCaseInsensitive(executableDirectory, programFiles) ||
        PathStartsWithCaseInsensitive(executableDirectory, programFilesX86)) {
        return InstallMode::Installed;
    }
#endif

    return InstallMode::Portable;
}

RuntimeLayout BuildRuntimeLayout() {
    RuntimeLayout layout;
    layout.executablePath = GetModulePath();
    layout.executableDirectory = layout.executablePath.has_parent_path()
        ? layout.executablePath.parent_path()
        : std::filesystem::path();
    layout.installMode = ResolveInstallMode(layout.executableDirectory);

    if (layout.installMode == InstallMode::Installed) {
#if defined(_WIN32)
        layout.roamingDataDirectory = GetKnownFolder(FOLDERID_RoamingAppData) / kAppDirectoryName;
        layout.localDataDirectory = GetKnownFolder(FOLDERID_LocalAppData) / kAppDirectoryName;
#else
        layout.roamingDataDirectory = layout.executableDirectory / kAppDirectoryName;
        layout.localDataDirectory = layout.executableDirectory / kAppDirectoryName;
#endif
        layout.settingsDirectory = layout.roamingDataDirectory;
        layout.settingsFilePath = layout.settingsDirectory / kSettingsFileName;
        layout.libraryDirectory = layout.roamingDataDirectory / "Library";
        layout.presetsDirectory = layout.roamingDataDirectory / "Presets";
        layout.cacheDirectory = layout.localDataDirectory / "Cache";
        layout.updateCacheDirectory = layout.localDataDirectory / "Updates";
        layout.logsDirectory = layout.localDataDirectory / "Logs";
        layout.startupLogPath = layout.logsDirectory / "StackStartup.log";
    } else {
        layout.roamingDataDirectory = layout.executableDirectory;
        layout.localDataDirectory = layout.executableDirectory;
        layout.settingsDirectory = layout.executableDirectory;
        layout.settingsFilePath = layout.executableDirectory / kSettingsFileName;
        layout.libraryDirectory = layout.executableDirectory / "Library";
        layout.presetsDirectory = layout.executableDirectory / "Presets";
        layout.cacheDirectory = layout.executableDirectory / "Cache";
        layout.updateCacheDirectory = layout.executableDirectory / "UpdateCache";
        layout.logsDirectory = layout.executableDirectory / "Logs";
        layout.startupLogPath = layout.logsDirectory / "StackStartup.log";
    }

    return layout;
}

RuntimeLayout& MutableRuntimeLayout() {
    static RuntimeLayout layout = BuildRuntimeLayout();
    return layout;
}

void EnsureDirectory(const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(path, ec);
}

void CopyFileIfMissing(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (source.empty() || destination.empty()) {
        return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(source, ec) || ec) {
        return;
    }

    ec.clear();
    if (std::filesystem::exists(destination, ec) && !ec) {
        return;
    }

    EnsureDirectory(destination.parent_path());
    ec.clear();
    std::filesystem::copy_file(
        source,
        destination,
        std::filesystem::copy_options::skip_existing,
        ec);
}

void CopyDirectoryConservatively(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (source.empty() || destination.empty()) {
        return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(source, ec) || ec) {
        return;
    }

    EnsureDirectory(destination);
    ec.clear();
    std::filesystem::copy(
        source,
        destination,
        std::filesystem::copy_options::recursive |
            std::filesystem::copy_options::skip_existing,
        ec);
}

void MigrateLegacyDataImpl() {
    RuntimeLayout& layout = MutableRuntimeLayout();
    if (layout.installMode != InstallMode::Installed || layout.executableDirectory.empty()) {
        return;
    }

    const std::filesystem::path legacySettings = layout.executableDirectory / kSettingsFileName;
    const std::filesystem::path legacyLibrary = layout.executableDirectory / "Library";
    const std::filesystem::path legacyLogs = layout.executableDirectory / "Logs";
    const std::filesystem::path legacyCache = layout.executableDirectory / "UpdateCache";

    EnsureRuntimeDirectories();
    CopyFileIfMissing(legacySettings, layout.settingsFilePath);
    CopyDirectoryConservatively(legacyLibrary, layout.libraryDirectory);
    CopyDirectoryConservatively(legacyLogs, layout.logsDirectory);
    CopyDirectoryConservatively(legacyCache, layout.updateCacheDirectory);

    std::error_code ec;
    if (std::filesystem::exists(layout.executableDirectory, ec) && !ec) {
        for (const auto& entry : std::filesystem::directory_iterator(layout.executableDirectory, ec)) {
            if (ec) {
                break;
            }

            const std::string fileName = entry.path().filename().string();
            if (fileName.rfind("StackBackgroundImage", 0) == 0 && entry.is_regular_file(ec) && !ec) {
                CopyFileIfMissing(entry.path(), layout.settingsDirectory / fileName);
            }
            ec.clear();
        }
    }
}

} // namespace

const RuntimeLayout& GetRuntimeLayout() {
    return MutableRuntimeLayout();
}

InstallMode GetInstallMode() {
    return GetRuntimeLayout().installMode;
}

bool IsInstalledBuild() {
    return GetInstallMode() == InstallMode::Installed;
}

const std::filesystem::path& GetExecutablePath() {
    return GetRuntimeLayout().executablePath;
}

const std::filesystem::path& GetExecutableDirectory() {
    return GetRuntimeLayout().executableDirectory;
}

const std::filesystem::path& GetSettingsDirectory() {
    return GetRuntimeLayout().settingsDirectory;
}

const std::filesystem::path& GetSettingsFilePath() {
    return GetRuntimeLayout().settingsFilePath;
}

const std::filesystem::path& GetLibraryDirectory() {
    return GetRuntimeLayout().libraryDirectory;
}

const std::filesystem::path& GetPresetsDirectory() {
    return GetRuntimeLayout().presetsDirectory;
}

const std::filesystem::path& GetCacheDirectory() {
    return GetRuntimeLayout().cacheDirectory;
}

const std::filesystem::path& GetUpdateCacheDirectory() {
    return GetRuntimeLayout().updateCacheDirectory;
}

const std::filesystem::path& GetLogsDirectory() {
    return GetRuntimeLayout().logsDirectory;
}

const std::filesystem::path& GetStartupLogPath() {
    return GetRuntimeLayout().startupLogPath;
}

void EnsureRuntimeDirectories() {
    const RuntimeLayout& layout = GetRuntimeLayout();
    EnsureDirectory(layout.settingsDirectory);
    EnsureDirectory(layout.libraryDirectory);
    EnsureDirectory(layout.presetsDirectory);
    EnsureDirectory(layout.cacheDirectory);
    EnsureDirectory(layout.updateCacheDirectory);
    EnsureDirectory(layout.logsDirectory);
}

void MigrateLegacyPortableDataIfNeeded() {
    static std::once_flag once;
    std::call_once(once, []() {
        MigrateLegacyDataImpl();
    });
}

std::string GetInstallModeLabel() {
    return IsInstalledBuild() ? "Installed" : "Portable";
}

} // namespace AppPaths
