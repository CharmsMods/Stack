#include "App/AppPaths.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void WriteFile(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << contents;
    Require(output.good(), "test file write failed");
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

struct TemporaryTree {
    TemporaryTree() {
        const auto serial = std::chrono::high_resolution_clock::now()
            .time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
            ("stack-app-path-tests-" + std::to_string(serial));
        std::filesystem::create_directories(root);
    }

    ~TemporaryTree() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    std::filesystem::path root;
};

void TestPortableLayout(const std::filesystem::path& root) {
    const std::filesystem::path executable = root / "portable" / "Stack.exe";
    const AppPaths::RuntimeLayout layout = AppPaths::BuildRuntimeLayoutForExecutable(
        executable,
        AppPaths::InstallMode::Portable);
    const std::filesystem::path managed = executable.parent_path() / "Stack";
    Require(layout.managedDirectory == managed, "portable managed root mismatch");
    Require(layout.appDirectory == managed / "App", "portable app path mismatch");
    Require(layout.runtimeDirectory == managed / "App" / "Runtime", "portable runtime path mismatch");
    Require(layout.resourcesDirectory == managed / "App" / "Resources", "portable resources path mismatch");
    Require(layout.toolsDirectory == managed / "App" / "Tools", "portable tools path mismatch");
    Require(layout.legalDirectory == managed / "App" / "Legal", "portable legal path mismatch");
    Require(layout.userDirectory == managed / "User", "portable user path mismatch");
    Require(layout.settingsDirectory == managed / "User" / "Settings", "portable settings path mismatch");
    Require(layout.backgroundMediaDirectory == managed / "User" / "Media" / "Backgrounds", "portable background path mismatch");
    Require(layout.libraryDirectory == managed / "User" / "Library", "portable library path mismatch");
    Require(layout.projectsDirectory == executable.parent_path() / "Stack Projects", "portable projects path mismatch");
    Require(layout.presetsDirectory == managed / "User" / "Presets", "portable presets path mismatch");
    Require(layout.tempDirectory == managed / "Temp", "portable temp path mismatch");
    Require(layout.cacheDirectory == managed / "Temp" / "Cache", "portable cache path mismatch");
    Require(layout.logsDirectory == managed / "Temp" / "Logs", "portable logs path mismatch");
    Require(layout.updateCacheDirectory == managed / "Temp" / "Updates", "portable updates path mismatch");
    Require(layout.imguiIniPath == managed / "User" / "Settings" / "imgui.ini", "portable ImGui path mismatch");
}

void TestInstalledLayoutAndMarker(const std::filesystem::path& root) {
    const std::filesystem::path appRoot = root / "installed";
    const std::filesystem::path executable = appRoot / "Stack.exe";
    WriteFile(appRoot / "Stack" / "App" / "StackInstalledBuild.marker", "installed\n");
    Require(
        AppPaths::DetectInstallMode(appRoot) == AppPaths::InstallMode::Installed,
        "managed installed marker was not detected");

    const std::filesystem::path roaming = root / "roaming";
    const std::filesystem::path local = root / "local";
    const AppPaths::RuntimeLayout layout = AppPaths::BuildRuntimeLayoutForExecutable(
        executable,
        AppPaths::InstallMode::Installed,
        roaming,
        local);
    Require(layout.appDirectory == appRoot / "Stack" / "App", "installed app path mismatch");
    Require(layout.userDirectory == roaming / "Stack" / "User", "installed user path mismatch");
    Require(layout.projectsDirectory == appRoot / "Stack Projects", "installed projects path mismatch");
    Require(layout.tempDirectory == local / "Stack" / "Temp", "installed temp path mismatch");
}

void TestLocalTestInstalledLayout(const std::filesystem::path& root) {
    const std::filesystem::path appRoot = root / "installed-local-test";
    const std::filesystem::path roaming = root / "local-test-roaming";
    const std::filesystem::path local = root / "local-test-local";
    const AppPaths::RuntimeLayout layout = AppPaths::BuildRuntimeLayoutForExecutable(
        appRoot / "Stack.exe",
        AppPaths::InstallMode::Installed,
        roaming,
        local,
        true);
    Require(
        layout.userDirectory == roaming / "Stack Local Test" / "User",
        "local-test installed user path must not share production data");
    Require(
        layout.tempDirectory == local / "Stack Local Test" / "Temp",
        "local-test installed temp path must not share production data");
    Require(
        layout.projectsDirectory == appRoot / "Stack Projects",
        "local-test installed projects must remain beside the local-test executable");
}

void TestPortableMigration(const std::filesystem::path& root) {
    const std::filesystem::path appRoot = root / "migration-portable";
    const std::filesystem::path executable = appRoot / "Stack.exe";
    std::filesystem::create_directories(appRoot);
    const AppPaths::RuntimeLayout layout = AppPaths::BuildRuntimeLayoutForExecutable(
        executable,
        AppPaths::InstallMode::Portable);

    WriteFile(
        appRoot / "StackSettings.json",
        R"({"appearance":{"backgroundImagePath":"wall.png","backgroundImages":[{"backgroundImagePath":"second.jpg"}]}})");
    WriteFile(appRoot / "RawWorkspaceState.json", "raw-state");
    WriteFile(appRoot / "LibraryViewState.json", "library-view");
    WriteFile(appRoot / "StackUpdateState.json", "update-state");
    WriteFile(appRoot / "imgui.ini", "imgui-state");
    WriteFile(appRoot / "wall.png", "background-one");
    WriteFile(appRoot / "second.jpg", "background-two");
    WriteFile(appRoot / "StackBackgroundImage-old.png", "background-three");
    WriteFile(appRoot / "Library" / "ProjectA" / "project.stack", "project");
    WriteFile(appRoot / "Library" / "same.bin", "same");
    WriteFile(appRoot / "Library" / "conflict.bin", "legacy");
    WriteFile(appRoot / "Presets" / "default.json", "preset");
    WriteFile(appRoot / "Cache" / "cache.bin", "cache");
    WriteFile(appRoot / "Logs" / "legacy.log", "log");
    WriteFile(appRoot / "UpdateCache" / "update.bin", "update");

    WriteFile(layout.libraryDirectory / "same.bin", "same");
    WriteFile(layout.libraryDirectory / "conflict.bin", "destination");
    WriteFile(
        layout.settingsDirectory / "RawWorkspaceState.json.stack-migration-v2.tmp",
        "interrupted-copy");

    const AppPaths::MigrationReport report = AppPaths::MigrateLegacyData(layout);
    Require(report.completed, "portable migration did not complete");
    Require(report.errors == 0, "portable migration reported errors");
    Require(report.conflicts == 1, "portable migration conflict was not reported");
    Require(report.removedDuplicateFiles == 1, "portable duplicate was not removed");
    Require(!std::filesystem::exists(appRoot / "StackSettings.json"), "legacy settings remained");
    Require(std::filesystem::exists(layout.settingsFilePath), "settings were not migrated");
    Require(ReadFile(layout.settingsDirectory / "RawWorkspaceState.json") == "raw-state", "interrupted migration was not repaired");
    Require(std::filesystem::exists(layout.backgroundMediaDirectory / "wall.png"), "active background was not migrated");
    Require(std::filesystem::exists(layout.backgroundMediaDirectory / "second.jpg"), "library background was not migrated");
    Require(std::filesystem::exists(layout.backgroundMediaDirectory / "StackBackgroundImage-old.png"), "managed legacy background was not migrated");
    Require(std::filesystem::exists(layout.libraryDirectory / "ProjectA" / "project.stack"), "Library project was not migrated");
    Require(ReadFile(layout.libraryDirectory / "conflict.bin") == "destination", "destination did not win conflict");
    Require(std::filesystem::exists(appRoot / "Library" / "conflict.bin"), "conflicting legacy copy was removed");
    Require(std::filesystem::exists(layout.presetsDirectory / "default.json"), "preset was not migrated");
    Require(std::filesystem::exists(layout.cacheDirectory / "cache.bin"), "cache was not migrated");
    Require(std::filesystem::exists(layout.logsDirectory / "legacy.log"), "log was not migrated");
    Require(std::filesystem::exists(layout.updateCacheDirectory / "update.bin"), "update was not migrated");
    Require(std::filesystem::exists(layout.migrationMarkerPath), "migration marker was not written");
    Require(std::filesystem::exists(layout.migrationLogPath), "migration log was not written");

    const AppPaths::MigrationReport second = AppPaths::MigrateLegacyData(layout);
    Require(second.completed && second.alreadyCompleted, "migration relaunch was not idempotent");
}

void TestInstalledMigration(const std::filesystem::path& root) {
    const std::filesystem::path appRoot = root / "migration-installed";
    const std::filesystem::path roaming = root / "migration-roaming";
    const std::filesystem::path local = root / "migration-local";
    const AppPaths::RuntimeLayout layout = AppPaths::BuildRuntimeLayoutForExecutable(
        appRoot / "Stack.exe",
        AppPaths::InstallMode::Installed,
        roaming,
        local);
    WriteFile(roaming / "Stack" / "StackSettings.json", R"({"appearance":{}})");
    WriteFile(roaming / "Stack" / "Library" / "Installed" / "project.stack", "installed-project");
    WriteFile(roaming / "Stack" / "Presets" / "installed.json", "installed-preset");
    WriteFile(local / "Stack" / "Cache" / "installed.bin", "installed-cache");
    WriteFile(local / "Stack" / "Logs" / "installed.log", "installed-log");
    WriteFile(local / "Stack" / "Updates" / "installed.update", "installed-update");

    const AppPaths::MigrationReport report = AppPaths::MigrateLegacyData(layout);
    Require(report.completed && report.errors == 0, "installed migration failed");
    Require(std::filesystem::exists(layout.settingsFilePath), "installed settings were not migrated");
    Require(std::filesystem::exists(layout.libraryDirectory / "Installed" / "project.stack"), "installed Library was not migrated");
    Require(std::filesystem::exists(layout.presetsDirectory / "installed.json"), "installed presets were not migrated");
    Require(std::filesystem::exists(layout.cacheDirectory / "installed.bin"), "installed cache was not migrated");
    Require(std::filesystem::exists(layout.logsDirectory / "installed.log"), "installed logs were not migrated");
    Require(std::filesystem::exists(layout.updateCacheDirectory / "installed.update"), "installed updates were not migrated");
}

void TestUnicodeMigration(const std::filesystem::path& root) {
    const auto appRoot = root / std::filesystem::u8path(u8"portable-\u65e5\u672c-\U0001f4f7");
    Require(AppPaths::DetectInstallMode(appRoot) == AppPaths::InstallMode::Portable,
        "Unicode portable path was not detected");
    const auto layout = AppPaths::BuildRuntimeLayoutForExecutable(
        appRoot / "Stack.exe", AppPaths::InstallMode::Portable);
    const auto relativeBackground = std::filesystem::u8path(u8"photos/\u80cc\u666f.png");
    const auto relativeLibraryFile = std::filesystem::u8path(u8"\u5199\u771f/\u753b\u50cf.bin");
    WriteFile(appRoot / "StackSettings.json",
        u8R"({"appearance":{"backgroundImagePath":"photos/\u80cc\u666f.png"}})");
    WriteFile(appRoot / relativeBackground, "unicode-background");
    WriteFile(appRoot / "Library" / relativeLibraryFile, "unicode-library");
    const auto report = AppPaths::MigrateLegacyData(layout);
    Require(report.completed && report.errors == 0, "Unicode migration failed");
    Require(ReadFile(layout.backgroundMediaDirectory / relativeBackground.filename()) ==
        "unicode-background", "Unicode background was not migrated");
    Require(ReadFile(layout.libraryDirectory / relativeLibraryFile) == "unicode-library",
        "Unicode library file was not migrated");
    Require(ReadFile(report.logPath).find(appRoot.u8string()) != std::string::npos,
        "migration log did not preserve the Unicode path");
}

void TestMalformedMigrationSettings(const std::filesystem::path& root) {
    const char* cases[] = {
        R"({"appearance":null})",
        R"({"appearance":[]})",
        R"({"appearance":{"backgroundImagePath":12,"backgroundImages":[{"backgroundImagePath":false},null]}})",
        R"({"appearance":{"backgroundImages":false}})"
    };
    int index = 0;
    for (const char* settings : cases) {
        const auto appRoot = root / ("malformed-settings-" + std::to_string(index++));
        const auto layout = AppPaths::BuildRuntimeLayoutForExecutable(
            appRoot / "Stack.exe", AppPaths::InstallMode::Portable);
        WriteFile(appRoot / "StackSettings.json", settings);
        WriteFile(appRoot / "Presets" / "saved.json", "preset-data");
        const auto report = AppPaths::MigrateLegacyData(layout);
        Require(report.completed && report.errors == 0,
            "malformed settings interrupted migration");
        Require(ReadFile(layout.settingsFilePath) == settings,
            "migration changed malformed settings");
        Require(ReadFile(layout.presetsDirectory / "saved.json") == "preset-data",
            "malformed settings prevented unrelated files from migrating");
    }
}

void TestMigrationPathContainment(const std::filesystem::path& root) {
    const auto appRoot = root / "containment";
    const auto sibling = root / "containment-sibling" / "wall.png";
    const auto layout = AppPaths::BuildRuntimeLayoutForExecutable(
        appRoot / "Stack.exe", AppPaths::InstallMode::Portable);
    WriteFile(sibling, "outside-background");
    WriteFile(appRoot / "StackSettings.json",
        R"({"appearance":{"backgroundImagePath":"../containment-sibling/wall.png"}})");
    const auto report = AppPaths::MigrateLegacyData(layout);
    Require(report.completed && report.errors == 0, "containment migration failed");
    Require(ReadFile(sibling) == "outside-background" &&
        !std::filesystem::exists(layout.backgroundMediaDirectory / "wall.png"),
        "migration followed a sibling-prefix path outside the legacy root");
}

} // namespace

int main() {
    try {
        TemporaryTree tree;
        TestPortableLayout(tree.root);
        TestInstalledLayoutAndMarker(tree.root);
        TestLocalTestInstalledLayout(tree.root);
        TestPortableMigration(tree.root);
        TestInstalledMigration(tree.root);
        TestUnicodeMigration(tree.root);
        TestMalformedMigrationSettings(tree.root);
        TestMigrationPathContainment(tree.root);
        std::cout << "Stack AppPaths and migration tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Stack AppPaths test failure: " << error.what() << '\n';
        return 1;
    }
}
