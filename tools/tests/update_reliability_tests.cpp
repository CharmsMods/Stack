#include "App/UpdateDownloadStorage.h"
#include "App/UpdateJson.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct TemporaryTree {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("stack-update-tests-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryTree() { std::filesystem::create_directories(root); }
    ~TemporaryTree() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

void Write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    output.close();
    Require(!output.fail(), "test fixture write failed");
}

std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void TestDownloadCommit(const std::filesystem::path& root) {
    const auto final = root / std::filesystem::u8path(u8"Stack-\u65e5\u672c.exe");
    auto partial = final;
    partial += ".partial";
    std::string error;
    Write(final, "previous-installer");
    Require(!AppUpdate::CommitDownloadedInstaller(partial, final, error),
        "missing download was accepted");
    Require(!error.empty() && Read(final) == "previous-installer",
        "failed commit destroyed the previous installer");

    Write(partial, "complete-installer");
#if defined(_WIN32)
    const HANDLE locked = CreateFileW(final.c_str(), GENERIC_READ, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(locked != INVALID_HANDLE_VALUE, "could not lock destination fixture");
    const bool lockedCommit = AppUpdate::CommitDownloadedInstaller(partial, final, error);
    CloseHandle(locked);
    Require(!lockedCommit && !error.empty(), "locked destination was reported as saved");
    Require(Read(final) == "previous-installer" && Read(partial) == "complete-installer",
        "failed replacement lost installer data");
#endif
    Require(AppUpdate::CommitDownloadedInstaller(partial, final, error),
        "completed Unicode download was not committed");
    Require(error.empty() && Read(final) == "complete-installer" &&
        !std::filesystem::exists(partial), "successful commit left incorrect contents");

    const auto directory = root / "directory.exe";
    std::filesystem::create_directory(directory);
    Write(partial, "retry-installer");
    Require(!AppUpdate::CommitDownloadedInstaller(partial, directory, error),
        "directory collision was reported as saved");
    Require(std::filesystem::is_directory(directory) && Read(partial) == "retry-installer",
        "failed directory replacement lost data");
}

void TestOptionalJsonFields() {
    using nlohmann::json;
    const json release = json::parse(R"({"body":null,"digest":null,"name":"Stack","draft":false})");
    Require(AppUpdate::ReadUpdateField(release, "body", std::string()).empty(),
        "null release notes were not accepted");
    Require(AppUpdate::ReadUpdateField(release, "digest", std::string()).empty(),
        "null release digest was not accepted");
    Require(AppUpdate::ReadUpdateField(release, "name", std::string()) == "Stack",
        "valid release name changed");
    const json cache = json::parse(R"({"automaticStartupCheck":[],"lastCheckUnixSeconds":-1,"downloadReady":"yes"})");
    Require(AppUpdate::ReadUpdateField(cache, "automaticStartupCheck", true),
        "invalid preference did not default");
    Require(!AppUpdate::ReadUpdateField(cache, "downloadReady", false),
        "invalid cached readiness was accepted");
    Require(AppUpdate::ReadUpdateField(cache, "lastCheckUnixSeconds", std::uint64_t{0}) == 0,
        "negative cached timestamp wrapped");
    Require(AppUpdate::ReadUpdateField(json::array(), "name", std::string()).empty(),
        "invalid release object was accepted");
}

} // namespace

int main() {
    try {
        TemporaryTree tree;
        TestDownloadCommit(tree.root);
        TestOptionalJsonFields();
        std::cout << "Update reliability tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Update reliability test failed: " << error.what() << '\n';
        return 1;
    }
}
