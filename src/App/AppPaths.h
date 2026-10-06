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
    std::filesystem::path managedDirectory;
    std::filesystem::path appDirectory;
    std::filesystem::path runtimeDirectory;
    std::filesystem::path resourcesDirectory;
    std::filesystem::path toolsDirectory;
    std::filesystem::path legalDirectory;
    std::filesystem::path roamingDataDirectory;
    std::filesystem::path localDataDirectory;
    std::filesystem::path userDirectory;
    std::filesystem::path settingsFilePath;
    std::filesystem::path settingsDirectory;
    std::filesystem::path backgroundMediaDirectory;
    std::filesystem::path libraryDirectory;
    std::filesystem::path projectsDirectory;
    std::filesystem::path presetsDirectory;
    std::filesystem::path tempDirectory;
    std::filesystem::path cacheDirectory;
    std::filesystem::path updateCacheDirectory;
    std::filesystem::path logsDirectory;
    std::filesystem::path startupLogPath;
    std::filesystem::path imguiIniPath;
    std::filesystem::path migrationMarkerPath;
    std::filesystem::path migrationLogPath;
};

struct MigrationReport {
    bool completed = false;
    bool alreadyCompleted = false;
    std::size_t movedFiles = 0;
    std::size_t removedDuplicateFiles = 0;
    std::size_t conflicts = 0;
    std::size_t errors = 0;
    std::filesystem::path logPath;
};

struct DirectoryWriteProbe {
    bool writable = false;
    std::filesystem::path directory;
    std::string message;
};

const RuntimeLayout& GetRuntimeLayout();
InstallMode GetInstallMode();
bool IsInstalledBuild();
bool IsLocalTestBuild();
const std::filesystem::path& GetExecutablePath();
const std::filesystem::path& GetExecutableDirectory();
const std::filesystem::path& GetManagedDirectory();
const std::filesystem::path& GetAppDirectory();
const std::filesystem::path& GetRuntimeDirectory();
const std::filesystem::path& GetResourcesDirectory();
const std::filesystem::path& GetToolsDirectory();
const std::filesystem::path& GetLegalDirectory();
const std::filesystem::path& GetUserDirectory();
const std::filesystem::path& GetSettingsDirectory();
const std::filesystem::path& GetSettingsFilePath();
const std::filesystem::path& GetBackgroundMediaDirectory();
const std::filesystem::path& GetLibraryDirectory();
const std::filesystem::path& GetProjectsDirectory();
const std::filesystem::path& GetPresetsDirectory();
const std::filesystem::path& GetTempDirectory();
const std::filesystem::path& GetCacheDirectory();
const std::filesystem::path& GetUpdateCacheDirectory();
const std::filesystem::path& GetLogsDirectory();
const std::filesystem::path& GetStartupLogPath();
const std::filesystem::path& GetImGuiIniPath();
const std::filesystem::path& GetMigrationLogPath();

InstallMode DetectInstallMode(const std::filesystem::path& executableDirectory);
RuntimeLayout BuildRuntimeLayoutForExecutable(
    const std::filesystem::path& executablePath,
    InstallMode installMode,
    const std::filesystem::path& roamingAppDataRoot = {},
    const std::filesystem::path& localAppDataRoot = {},
    bool localTestBuild = false);
MigrationReport MigrateLegacyData(const RuntimeLayout& layout);
void EnsureRuntimeDirectories();
DirectoryWriteProbe ProbeProjectsDirectoryWritable();
void MigrateLegacyPortableDataIfNeeded();
std::string GetInstallModeLabel();

} // namespace AppPaths
