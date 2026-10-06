#include "AppPaths.h"

#include "ThirdParty/json.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shlobj.h>
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace AppPaths {
namespace {

constexpr const char* kAppDirectoryName = "Stack";
constexpr const char* kLocalTestDataDirectoryName = "Stack Local Test";
constexpr const char* kSettingsFileName = "StackSettings.json";
constexpr const char* kInstalledMarkerFileName = "StackInstalledBuild.marker";
constexpr const char* kLocalTestMarkerFileName = "StackLocalTestBuild.marker";
constexpr const char* kMigrationMarkerFileName = ".runtime-layout-v2.migrated";
constexpr const char* kMigrationLogFileName = "runtime-layout-migration-v2.log";

using json = nlohmann::json;

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::filesystem::path GetModulePath() {
#if defined(_WIN32)
    std::vector<wchar_t> buffer(512, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length > 0 && length < buffer.size() - 1) {
            return std::filesystem::path(std::wstring(buffer.data(), length));
        }
        if (buffer.size() >= 32768) {
            break;
        }
        buffer.resize(buffer.size() * 2, L'\0');
    }
    throw std::runtime_error("Stack could not resolve its executable path.");
#else
    std::error_code ec;
    return std::filesystem::current_path(ec) / "Stack.exe";
#endif
}

#if defined(_WIN32)
std::filesystem::path GetKnownFolder(REFKNOWNFOLDERID folderId) {
    PWSTR rawPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &rawPath)) &&
        rawPath != nullptr) {
        std::filesystem::path result(rawPath);
        CoTaskMemFree(rawPath);
        return result;
    }

    const wchar_t* variable = nullptr;
    if (IsEqualGUID(folderId, FOLDERID_RoamingAppData)) {
        variable = L"APPDATA";
    } else if (IsEqualGUID(folderId, FOLDERID_LocalAppData)) {
        variable = L"LOCALAPPDATA";
    } else if (IsEqualGUID(folderId, FOLDERID_ProgramFiles)) {
        variable = L"ProgramFiles";
    } else if (IsEqualGUID(folderId, FOLDERID_ProgramFilesX86)) {
        variable = L"ProgramFiles(x86)";
    }
    if (variable != nullptr) {
        const DWORD length = GetEnvironmentVariableW(variable, nullptr, 0);
        if (length > 1) {
            std::vector<wchar_t> value(length, L'\0');
            if (GetEnvironmentVariableW(variable, value.data(), length) > 0) {
                return std::filesystem::path(value.data());
            }
        }
    }
    return {};
}
#endif

bool PathStartsWithCaseInsensitive(
    const std::filesystem::path& candidate,
    const std::filesystem::path& prefix) {
    if (prefix.empty()) {
        return false;
    }
    const auto normalizedCandidate = candidate.lexically_normal();
    const auto normalizedPrefix = prefix.lexically_normal();
    auto candidatePart = normalizedCandidate.begin();
    for (const auto& prefixPart : normalizedPrefix) {
        if (prefixPart.empty()) {
            continue;
        }
        if (candidatePart == normalizedCandidate.end()) {
            return false;
        }
#if defined(_WIN32)
        const auto& left = candidatePart->native();
        const auto& right = prefixPart.native();
        if (CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
                right.c_str(), static_cast<int>(right.size()), TRUE) != CSTR_EQUAL) {
            return false;
        }
#else
        if (ToLowerAscii(candidatePart->native()) != ToLowerAscii(prefixPart.native())) {
            return false;
        }
#endif
        ++candidatePart;
    }
    return true;
}

bool DirectoryContainsInstalledMarker(const std::filesystem::path& directory) {
    std::error_code ec;
    if (directory.empty()) {
        return false;
    }
    return std::filesystem::exists(directory / kInstalledMarkerFileName, ec) ||
        std::filesystem::exists(
            directory / kAppDirectoryName / "App" / kInstalledMarkerFileName,
            ec) ||
        std::filesystem::exists(directory / "unins000.exe", ec);
}

bool DirectoryContainsLocalTestMarker(const std::filesystem::path& directory) {
    std::error_code ec;
    if (directory.empty()) {
        return false;
    }
    return std::filesystem::exists(
        directory / kAppDirectoryName / "App" / kLocalTestMarkerFileName,
        ec) && !ec;
}

RuntimeLayout BuildRuntimeLayout() {
    const std::filesystem::path executablePath = GetModulePath();
    const std::filesystem::path executableDirectory = executablePath.has_parent_path()
        ? executablePath.parent_path()
        : std::filesystem::path();
    const InstallMode mode = DetectInstallMode(executableDirectory);
    const bool localTestBuild = DirectoryContainsLocalTestMarker(executableDirectory);

#if defined(_WIN32)
    return BuildRuntimeLayoutForExecutable(
        executablePath,
        mode,
        GetKnownFolder(FOLDERID_RoamingAppData),
        GetKnownFolder(FOLDERID_LocalAppData),
        localTestBuild);
#else
    return BuildRuntimeLayoutForExecutable(executablePath, mode, {}, {}, localTestBuild);
#endif
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

bool FilesEqual(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(left, ec) || ec) {
        return false;
    }
    ec.clear();
    if (!std::filesystem::is_regular_file(right, ec) || ec) {
        return false;
    }
    ec.clear();
    if (std::filesystem::file_size(left, ec) != std::filesystem::file_size(right, ec) || ec) {
        return false;
    }

    std::ifstream leftStream(left, std::ios::binary);
    std::ifstream rightStream(right, std::ios::binary);
    if (!leftStream || !rightStream) {
        return false;
    }
    std::vector<char> leftBytes(1024 * 1024);
    std::vector<char> rightBytes(1024 * 1024);
    while (leftStream && rightStream) {
        leftStream.read(leftBytes.data(), static_cast<std::streamsize>(leftBytes.size()));
        rightStream.read(rightBytes.data(), static_cast<std::streamsize>(rightBytes.size()));
        const std::streamsize leftCount = leftStream.gcount();
        const std::streamsize rightCount = rightStream.gcount();
        if (leftCount != rightCount ||
            !std::equal(leftBytes.begin(), leftBytes.begin() + leftCount, rightBytes.begin())) {
            return false;
        }
    }
    return leftStream.eof() && rightStream.eof();
}

bool IsInsideDirectory(
    const std::filesystem::path& candidate,
    const std::filesystem::path& directory) {
    if (candidate.empty() || directory.empty()) {
        return false;
    }
    return PathStartsWithCaseInsensitive(
        std::filesystem::absolute(candidate).lexically_normal(),
        std::filesystem::absolute(directory).lexically_normal());
}

struct MigrationContext {
    explicit MigrationContext(const RuntimeLayout& targetLayout)
        : layout(targetLayout) {}

    const RuntimeLayout& layout;
    MigrationReport report;
    std::vector<std::string> lines;
};

void Record(MigrationContext& context, const std::string& message) {
    context.lines.push_back(message);
}

void MigrateFile(
    MigrationContext& context,
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const std::filesystem::path& allowedDestinationRoot) {
    if (source.empty() || destination.empty() || source == destination) {
        return;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(source, ec) || ec) {
        return;
    }
    if (!IsInsideDirectory(destination, allowedDestinationRoot)) {
        ++context.report.errors;
        Record(context, "ERROR unsafe destination rejected: " + destination.u8string());
        return;
    }

    ec.clear();
    if (std::filesystem::exists(destination, ec) && !ec) {
        if (FilesEqual(source, destination)) {
            ec.clear();
            if (std::filesystem::remove(source, ec) && !ec) {
                ++context.report.removedDuplicateFiles;
                Record(context, "DUPLICATE removed legacy copy: " + source.u8string());
            } else {
                ++context.report.errors;
                Record(context, "ERROR could not remove verified duplicate: " + source.u8string());
            }
        } else {
            ++context.report.conflicts;
            Record(
                context,
                "CONFLICT destination retained and legacy copy left untouched: " +
                    source.u8string() + " -> " + destination.u8string());
        }
        return;
    }

    EnsureDirectory(destination.parent_path());
    std::filesystem::path temporary = destination;
    temporary += ".stack-migration-v2.tmp";
    ec.clear();
    std::filesystem::remove(temporary, ec);
    ec.clear();
    std::filesystem::copy_file(
        source,
        temporary,
        std::filesystem::copy_options::overwrite_existing,
        ec);
    if (ec || !FilesEqual(source, temporary)) {
        ++context.report.errors;
        Record(context, "ERROR copy verification failed: " + source.u8string());
        std::error_code removeEc;
        std::filesystem::remove(temporary, removeEc);
        return;
    }

    ec.clear();
    std::filesystem::rename(temporary, destination, ec);
    if (ec) {
        ++context.report.errors;
        Record(context, "ERROR could not commit migrated file: " + destination.u8string());
        std::error_code removeEc;
        std::filesystem::remove(temporary, removeEc);
        return;
    }
    if (!FilesEqual(source, destination)) {
        ++context.report.errors;
        Record(context, "ERROR committed file failed verification: " + destination.u8string());
        return;
    }

    ec.clear();
    if (!std::filesystem::remove(source, ec) || ec) {
        ++context.report.errors;
        Record(context, "ERROR source could not be removed after verification: " + source.u8string());
        return;
    }
    ++context.report.movedFiles;
    Record(context, "MOVED " + source.u8string() + " -> " + destination.u8string());
}

void RemoveEmptyDirectoryTree(const std::filesystem::path& root) {
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec) {
        return;
    }
    std::vector<std::filesystem::path> directories;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end;
         it != end && !ec;
         it.increment(ec)) {
        if (it->is_directory(ec) && !ec) {
            directories.push_back(it->path());
        }
        ec.clear();
    }
    std::sort(directories.begin(), directories.end(), [](const auto& left, const auto& right) {
        return left.native().size() > right.native().size();
    });
    for (const auto& directory : directories) {
        ec.clear();
        std::filesystem::remove(directory, ec);
    }
    ec.clear();
    std::filesystem::remove(root, ec);
}

void MigrateDirectory(
    MigrationContext& context,
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const std::filesystem::path& allowedDestinationRoot) {
    std::error_code ec;
    if (!std::filesystem::is_directory(source, ec) || ec ||
        source.lexically_normal() == destination.lexically_normal()) {
        return;
    }

    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator it(source, ec), end;
         it != end && !ec;
         it.increment(ec)) {
        if (it->is_regular_file(ec) && !ec) {
            files.push_back(it->path());
        }
        ec.clear();
    }
    if (ec) {
        ++context.report.errors;
        Record(context, "ERROR could not enumerate legacy directory: " + source.u8string());
        return;
    }
    for (const auto& file : files) {
        const std::filesystem::path relative = file.lexically_relative(source);
        MigrateFile(context, file, destination / relative, allowedDestinationRoot);
    }
    RemoveEmptyDirectoryTree(source);
}

std::set<std::filesystem::path> ReferencedBackgroundPaths(
    const std::filesystem::path& settingsPath,
    const std::filesystem::path& legacyRoot) {
    std::set<std::filesystem::path> result;
    std::ifstream input(settingsPath, std::ios::binary);
    if (!input) {
        return result;
    }
    const json root = json::parse(input, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return result;
    }
    const auto appearance = root.find("appearance");
    if (appearance == root.end() || !appearance->is_object()) {
        return result;
    }
    const auto addStoredPath = [&](const json& object) {
        if (!object.is_object()) {
            return;
        }
        const auto storedPath = object.find("backgroundImagePath");
        if (storedPath == object.end() || !storedPath->is_string() ||
            storedPath->get_ref<const std::string&>().empty()) {
            return;
        }
        const auto relative = std::filesystem::u8path(storedPath->get_ref<const std::string&>());
        if (relative.is_absolute()) {
            return;
        }
        const std::filesystem::path source = (legacyRoot / relative).lexically_normal();
        if (IsInsideDirectory(source, legacyRoot)) {
            result.insert(source);
        }
    };
    addStoredPath(*appearance);
    const auto images = appearance->find("backgroundImages");
    if (images != appearance->end() && images->is_array()) {
        for (const json& image : *images) {
            addStoredPath(image);
        }
    }
    return result;
}

void MigrateLegacyRoot(
    MigrationContext& context,
    const std::filesystem::path& durableRoot,
    const std::filesystem::path& disposableRoot) {
    if (durableRoot.empty()) {
        return;
    }

    std::set<std::filesystem::path> backgrounds =
        ReferencedBackgroundPaths(durableRoot / kSettingsFileName, durableRoot);
    std::error_code ec;
    if (std::filesystem::is_directory(durableRoot, ec) && !ec) {
        for (std::filesystem::directory_iterator it(durableRoot, ec), end;
             it != end && !ec;
             it.increment(ec)) {
            if (it->is_regular_file(ec) && !ec &&
                it->path().filename().u8string().rfind("StackBackgroundImage", 0) == 0) {
                backgrounds.insert(it->path());
            }
            ec.clear();
        }
    }

    const std::array<const char*, 5> settingsFiles {
        kSettingsFileName,
        "RawWorkspaceState.json",
        "LibraryViewState.json",
        "StackUpdateState.json",
        "imgui.ini"
    };
    for (const char* fileName : settingsFiles) {
        MigrateFile(
            context,
            durableRoot / fileName,
            context.layout.settingsDirectory / fileName,
            context.layout.userDirectory);
    }
    for (const auto& source : backgrounds) {
        MigrateFile(
            context,
            source,
            context.layout.backgroundMediaDirectory / source.filename(),
            context.layout.userDirectory);
    }
    MigrateDirectory(
        context,
        durableRoot / "Library",
        context.layout.libraryDirectory,
        context.layout.userDirectory);
    MigrateDirectory(
        context,
        durableRoot / "Presets",
        context.layout.presetsDirectory,
        context.layout.userDirectory);

    const std::filesystem::path actualDisposableRoot =
        disposableRoot.empty() ? durableRoot : disposableRoot;
    MigrateDirectory(
        context,
        actualDisposableRoot / "Cache",
        context.layout.cacheDirectory,
        context.layout.tempDirectory);
    MigrateDirectory(
        context,
        actualDisposableRoot / "Logs",
        context.layout.logsDirectory,
        context.layout.tempDirectory);
    MigrateDirectory(
        context,
        actualDisposableRoot / "UpdateCache",
        context.layout.updateCacheDirectory,
        context.layout.tempDirectory);
    MigrateDirectory(
        context,
        actualDisposableRoot / "Updates",
        context.layout.updateCacheDirectory,
        context.layout.tempDirectory);
}

void WriteMigrationLog(MigrationContext& context) {
    EnsureDirectory(context.layout.logsDirectory);
    std::ofstream output(context.layout.migrationLogPath, std::ios::app);
    if (!output) {
        ++context.report.errors;
        return;
    }
    output << "Stack runtime layout migration v2\n";
    output << "mode=" << (context.layout.installMode == InstallMode::Installed ? "installed" : "portable") << '\n';
    for (const std::string& line : context.lines) {
        output << line << '\n';
    }
    output << "moved=" << context.report.movedFiles
           << " duplicates=" << context.report.removedDuplicateFiles
           << " conflicts=" << context.report.conflicts
           << " errors=" << context.report.errors << "\n\n";
}

} // namespace

InstallMode DetectInstallMode(const std::filesystem::path& executableDirectory) {
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

RuntimeLayout BuildRuntimeLayoutForExecutable(
    const std::filesystem::path& executablePath,
    InstallMode installMode,
    const std::filesystem::path& roamingAppDataRoot,
    const std::filesystem::path& localAppDataRoot,
    bool localTestBuild) {
    RuntimeLayout layout;
    layout.executablePath = executablePath;
    layout.executableDirectory = executablePath.has_parent_path()
        ? executablePath.parent_path()
        : std::filesystem::path();
    layout.installMode = installMode;
    layout.managedDirectory = layout.executableDirectory / kAppDirectoryName;
    layout.appDirectory = layout.managedDirectory / "App";
    layout.runtimeDirectory = layout.appDirectory / "Runtime";
    layout.resourcesDirectory = layout.appDirectory / "Resources";
    layout.toolsDirectory = layout.appDirectory / "Tools";
    layout.legalDirectory = layout.appDirectory / "Legal";

    if (installMode == InstallMode::Installed) {
        const char* dataDirectoryName = localTestBuild
            ? kLocalTestDataDirectoryName
            : kAppDirectoryName;
        const std::filesystem::path roamingRoot = roamingAppDataRoot.empty()
            ? layout.managedDirectory
            : roamingAppDataRoot / dataDirectoryName;
        const std::filesystem::path localRoot = localAppDataRoot.empty()
            ? layout.managedDirectory
            : localAppDataRoot / dataDirectoryName;
        layout.roamingDataDirectory = roamingRoot;
        layout.localDataDirectory = localRoot;
        layout.userDirectory = roamingRoot / "User";
        layout.tempDirectory = localRoot / "Temp";
    } else {
        layout.roamingDataDirectory = layout.managedDirectory;
        layout.localDataDirectory = layout.managedDirectory;
        layout.userDirectory = layout.managedDirectory / "User";
        layout.tempDirectory = layout.managedDirectory / "Temp";
    }

    layout.settingsDirectory = layout.userDirectory / "Settings";
    layout.settingsFilePath = layout.settingsDirectory / kSettingsFileName;
    layout.backgroundMediaDirectory = layout.userDirectory / "Media" / "Backgrounds";
    layout.libraryDirectory = layout.userDirectory / "Library";
    // Supported Stack projects live with the application, not in the settings
    // tree. This keeps the Resolve-style project library portable and makes the
    // project store easy to find without mixing it with preferences/cache data.
    // Legacy project locations are intentionally not migrated or deleted.
    layout.projectsDirectory = layout.executableDirectory / "Stack Projects";
    layout.presetsDirectory = layout.userDirectory / "Presets";
    layout.cacheDirectory = layout.tempDirectory / "Cache";
    layout.updateCacheDirectory = layout.tempDirectory / "Updates";
    layout.logsDirectory = layout.tempDirectory / "Logs";
    layout.startupLogPath = layout.logsDirectory / "StackStartup.log";
    layout.imguiIniPath = layout.settingsDirectory / "imgui.ini";
    layout.migrationMarkerPath = layout.settingsDirectory / kMigrationMarkerFileName;
    layout.migrationLogPath = layout.logsDirectory / kMigrationLogFileName;
    return layout;
}

MigrationReport MigrateLegacyData(const RuntimeLayout& layout) {
    MigrationContext context(layout);
    context.report.logPath = layout.migrationLogPath;
    std::error_code ec;
    if (std::filesystem::exists(layout.migrationMarkerPath, ec) && !ec) {
        context.report.completed = true;
        context.report.alreadyCompleted = true;
        return context.report;
    }

    if (layout.installMode == InstallMode::Installed) {
        MigrateLegacyRoot(context, layout.roamingDataDirectory, layout.localDataDirectory);
        MigrateLegacyRoot(context, layout.executableDirectory, layout.executableDirectory);
    } else {
        MigrateLegacyRoot(context, layout.executableDirectory, layout.executableDirectory);
    }

    WriteMigrationLog(context);
    if (context.report.errors == 0) {
        EnsureDirectory(layout.settingsDirectory);
        std::ofstream marker(layout.migrationMarkerPath, std::ios::trunc);
        if (marker) {
            marker << "Stack runtime layout migration v2 complete\n";
            marker << "moved=" << context.report.movedFiles << '\n';
            marker << "duplicates=" << context.report.removedDuplicateFiles << '\n';
            marker << "conflicts=" << context.report.conflicts << '\n';
            context.report.completed = true;
        } else {
            ++context.report.errors;
        }
    }
    return context.report;
}

const RuntimeLayout& GetRuntimeLayout() { return MutableRuntimeLayout(); }
InstallMode GetInstallMode() { return GetRuntimeLayout().installMode; }
bool IsInstalledBuild() { return GetInstallMode() == InstallMode::Installed; }
bool IsLocalTestBuild() {
    std::error_code ec;
    return std::filesystem::exists(GetAppDirectory() / kLocalTestMarkerFileName, ec) && !ec;
}
const std::filesystem::path& GetExecutablePath() { return GetRuntimeLayout().executablePath; }
const std::filesystem::path& GetExecutableDirectory() { return GetRuntimeLayout().executableDirectory; }
const std::filesystem::path& GetManagedDirectory() { return GetRuntimeLayout().managedDirectory; }
const std::filesystem::path& GetAppDirectory() { return GetRuntimeLayout().appDirectory; }
const std::filesystem::path& GetRuntimeDirectory() { return GetRuntimeLayout().runtimeDirectory; }
const std::filesystem::path& GetResourcesDirectory() { return GetRuntimeLayout().resourcesDirectory; }
const std::filesystem::path& GetToolsDirectory() { return GetRuntimeLayout().toolsDirectory; }
const std::filesystem::path& GetLegalDirectory() { return GetRuntimeLayout().legalDirectory; }
const std::filesystem::path& GetUserDirectory() { return GetRuntimeLayout().userDirectory; }
const std::filesystem::path& GetSettingsDirectory() { return GetRuntimeLayout().settingsDirectory; }
const std::filesystem::path& GetSettingsFilePath() { return GetRuntimeLayout().settingsFilePath; }
const std::filesystem::path& GetBackgroundMediaDirectory() { return GetRuntimeLayout().backgroundMediaDirectory; }
const std::filesystem::path& GetLibraryDirectory() { return GetRuntimeLayout().libraryDirectory; }
const std::filesystem::path& GetProjectsDirectory() { return GetRuntimeLayout().projectsDirectory; }
const std::filesystem::path& GetPresetsDirectory() { return GetRuntimeLayout().presetsDirectory; }
const std::filesystem::path& GetTempDirectory() { return GetRuntimeLayout().tempDirectory; }
const std::filesystem::path& GetCacheDirectory() { return GetRuntimeLayout().cacheDirectory; }
const std::filesystem::path& GetUpdateCacheDirectory() { return GetRuntimeLayout().updateCacheDirectory; }
const std::filesystem::path& GetLogsDirectory() { return GetRuntimeLayout().logsDirectory; }
const std::filesystem::path& GetStartupLogPath() { return GetRuntimeLayout().startupLogPath; }
const std::filesystem::path& GetImGuiIniPath() { return GetRuntimeLayout().imguiIniPath; }
const std::filesystem::path& GetMigrationLogPath() { return GetRuntimeLayout().migrationLogPath; }

void EnsureRuntimeDirectories() {
    const RuntimeLayout& layout = GetRuntimeLayout();
    EnsureDirectory(layout.settingsDirectory);
    EnsureDirectory(layout.backgroundMediaDirectory);
    EnsureDirectory(layout.libraryDirectory);
    EnsureDirectory(layout.projectsDirectory);
    EnsureDirectory(layout.presetsDirectory);
    EnsureDirectory(layout.cacheDirectory);
    EnsureDirectory(layout.updateCacheDirectory);
    EnsureDirectory(layout.logsDirectory);
}

DirectoryWriteProbe ProbeProjectsDirectoryWritable() {
    DirectoryWriteProbe probe;
    probe.directory = GetProjectsDirectory();
    std::error_code ec;
    std::filesystem::create_directories(probe.directory, ec);
    if (ec) {
        probe.message = "Stack cannot create the Projects directory at " +
            probe.directory.u8string() + ": " + ec.message();
        return probe;
    }

    const std::filesystem::path temporary = probe.directory /
        (".stack-write-probe-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            probe.message = "Stack Projects is read-only at " +
                probe.directory.u8string() +
                ". Grant this user Modify permission or use Save As to a writable location.";
            return probe;
        }
        output << "Stack project storage write probe\n";
        output.flush();
        if (!output.good()) {
            probe.message = "Stack could not complete a test write in " +
                probe.directory.u8string() + ".";
        }
    }
    ec.clear();
    std::filesystem::remove(temporary, ec);
    if (!probe.message.empty()) {
        return probe;
    }
    if (ec) {
        probe.message = "Stack can write project data but could not clean up its test file in " +
            probe.directory.u8string() + ": " + ec.message();
        return probe;
    }
    probe.writable = true;
    return probe;
}

void MigrateLegacyPortableDataIfNeeded() {
    static std::once_flag once;
    std::call_once(once, []() {
        MigrateLegacyData(GetRuntimeLayout());
    });
}

std::string GetInstallModeLabel() {
    return IsInstalledBuild() ? "Installed" : "Portable";
}

} // namespace AppPaths
