#pragma once

#include <filesystem>
#include <string>

namespace AppPaths {

enum class InstallMode {
    Portable,
    Installed
};

struct RuntimeLayout {
    std::filesystem::path executablePath;
    std::filesystem::path executableDirectory;
    InstallMode installMode = InstallMode::Portable;
    std::filesystem::path roamingDataDirectory;
    std::filesystem::path localDataDirectory;
    std::filesystem::path settingsFilePath;
    std::filesystem::path settingsDirectory;
    std::filesystem::path libraryDirectory;
    std::filesystem::path presetsDirectory;
    std::filesystem::path cacheDirectory;
    std::filesystem::path updateCacheDirectory;
    std::filesystem::path logsDirectory;
    std::filesystem::path startupLogPath;
};

const RuntimeLayout& GetRuntimeLayout();
InstallMode GetInstallMode();
bool IsInstalledBuild();
const std::filesystem::path& GetExecutablePath();
const std::filesystem::path& GetExecutableDirectory();
const std::filesystem::path& GetSettingsDirectory();
const std::filesystem::path& GetSettingsFilePath();
const std::filesystem::path& GetLibraryDirectory();
const std::filesystem::path& GetPresetsDirectory();
const std::filesystem::path& GetCacheDirectory();
const std::filesystem::path& GetUpdateCacheDirectory();
const std::filesystem::path& GetLogsDirectory();
const std::filesystem::path& GetStartupLogPath();
void EnsureRuntimeDirectories();
void MigrateLegacyPortableDataIfNeeded();
std::string GetInstallModeLabel();

} // namespace AppPaths
