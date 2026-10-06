#include "Utils/SavePathConfirmation.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct TemporaryDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("stack-save-target-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() { std::filesystem::create_directories(path); }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void TestNormalizedTargetDecisions() {
    using Stack::FileSave::NormalizedTargetAction;
    using Stack::FileSave::ReviewNormalizedTarget;
    TemporaryDirectory directory;
    const auto selected = directory.path / "graph.bmp";
    const auto target = directory.path / "graph.png";
    std::error_code error;
    Require(ReviewNormalizedTarget(selected, target, error) == NormalizedTargetAction::Ready,
        "A new normalized target must not ask to overwrite a nonexistent image");
    {
        std::ofstream output(target, std::ios::binary);
        output << "Existing graph image";
        Require(output.good(), "Could not create the existing image fixture");
    }
    Require(ReviewNormalizedTarget(selected, target, error) ==
            NormalizedTargetAction::ConfirmReplacement && !error,
        "Changing a typed BMP extension to an existing PNG must require confirmation");
    Require(ReviewNormalizedTarget(target, target, error) == NormalizedTargetAction::Ready,
        "The path already confirmed by the native dialog must not ask twice");
    Require(ReviewNormalizedTarget(directory.path / "." / "graph.png", target, error) ==
            NormalizedTargetAction::Ready,
        "Equivalent lexical paths must not ask twice");

    const auto alias = directory.path / "graph-link.bmp";
    std::filesystem::create_hard_link(target, alias, error);
    if (!error) {
        Require(ReviewNormalizedTarget(alias, target, error) == NormalizedTargetAction::Ready,
            "Two names for the same file must retain the native overwrite confirmation");
    }
#if defined(_WIN32)
    const auto uppercase = directory.path / "GRAPH.PNG";
    error.clear();
    if (std::filesystem::exists(uppercase, error) && !error) {
        Require(ReviewNormalizedTarget(uppercase, target, error) == NormalizedTargetAction::Ready,
            "Windows case aliases must not ask twice");
    }
#endif
    Require(ReviewNormalizedTarget({}, target, error) == NormalizedTargetAction::Unavailable && error,
        "Invalid target review must reject the save");
    Require(std::filesystem::file_size(target) == 20,
        "Reviewing a target must leave the existing image unchanged");
}

} // namespace

int main() {
    try {
        TestNormalizedTargetDecisions();
        std::cout << "Normalized save target confirmation tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
