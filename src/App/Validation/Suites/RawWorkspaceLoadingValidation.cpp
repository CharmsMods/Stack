#include "App/Validation/ValidationSuites.h"

#include "Raw/RawLoader.h"
#include "Raw/RawWorkspace.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

namespace Stack::Validation {
namespace {

using Clock = std::chrono::steady_clock;

double ElapsedMilliseconds(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

bool PathLooksLikeOption(const char* text) {
    return text != nullptr && text[0] == '-' && text[1] == '-';
}

} // namespace

bool ValidateRawWorkspaceLoadingSmoke(int rawArgCount, char** rawArgs) {
    if (rawArgCount <= 0 || rawArgs == nullptr || rawArgs[0] == nullptr || PathLooksLikeOption(rawArgs[0])) {
        std::cerr << "RAW Workspace loading smoke usage: --validate-raw-workspace-loading-smoke <workspace-folder> [--expect-min-sources N]\n";
        return false;
    }

    std::filesystem::path workspaceRoot = rawArgs[0];
    int expectMinSources = 1;
    for (int i = 1; i < rawArgCount; ++i) {
        const std::string option = rawArgs[i] ? rawArgs[i] : "";
        if (option == "--expect-min-sources") {
            if (i + 1 >= rawArgCount) {
                std::cerr << "RAW Workspace loading smoke failed: --expect-min-sources requires a value.\n";
                return false;
            }
            try {
                expectMinSources = std::max(0, std::stoi(rawArgs[++i]));
            } catch (...) {
                std::cerr << "RAW Workspace loading smoke failed: invalid --expect-min-sources value.\n";
                return false;
            }
        } else {
            std::cerr << "RAW Workspace loading smoke failed: unknown argument " << option << "\n";
            return false;
        }
    }

    std::error_code ec;
    workspaceRoot = std::filesystem::absolute(workspaceRoot, ec).lexically_normal();
    if (ec || !std::filesystem::exists(workspaceRoot, ec) || ec || !std::filesystem::is_directory(workspaceRoot, ec) || ec) {
        std::cerr << "RAW Workspace loading smoke failed: workspace folder does not exist: "
                  << workspaceRoot.string() << "\n";
        return false;
    }

    auto isRawPath = [](const std::filesystem::path& path) {
        return Raw::RawLoader::IsRawPath(path.string()) ||
            Stack::RawWorkspace::DefaultRawPathPredicate(path);
    };

    Stack::RawWorkspace::ScanProgress lastProgress;
    const Clock::time_point scanStart = Clock::now();
    Stack::RawWorkspace::ScanResult scan = Stack::RawWorkspace::ScanWorkspace(
        workspaceRoot,
        isRawPath,
        [&](const Stack::RawWorkspace::ScanProgress& progress) {
            lastProgress = progress;
        });
    const Clock::time_point scanEnd = Clock::now();
    if (!scan.success) {
        std::cerr << "RAW Workspace loading smoke failed: scan failed: "
                  << scan.errorMessage << "\n";
        return false;
    }
    if (static_cast<int>(scan.sources.size()) < expectMinSources) {
        std::cerr << "RAW Workspace loading smoke failed: expected at least "
                  << expectMinSources << " RAW sources, found "
                  << scan.sources.size() << "\n";
        return false;
    }
    if (scan.progress.discoveredRawCount != static_cast<int>(scan.sources.size()) ||
        lastProgress.discoveredRawCount != static_cast<int>(scan.sources.size())) {
        std::cerr << "RAW Workspace loading smoke failed: scan progress did not match source count.\n";
        return false;
    }

    const Clock::time_point classifyStart = Clock::now();
    if (!Stack::RawWorkspace::ClassifyThumbnails(scan.layout, scan.sources)) {
        std::cerr << "RAW Workspace loading smoke failed: thumbnail classification was canceled unexpectedly.\n";
        return false;
    }
    const Clock::time_point classifyEnd = Clock::now();

    const Stack::RawWorkspace::ThumbnailProgress thumbnailProgress =
        Stack::RawWorkspace::BuildThumbnailProgress(scan.sources);
    if (thumbnailProgress.total != static_cast<int>(scan.sources.size())) {
        std::cerr << "RAW Workspace loading smoke failed: thumbnail progress total did not match source count.\n";
        return false;
    }

    const Clock::time_point discoverStart = Clock::now();
    if (!Stack::RawWorkspace::DiscoverProjects(scan.layout, scan.sources)) {
        std::cerr << "RAW Workspace loading smoke failed: project discovery was canceled unexpectedly.\n";
        return false;
    }
    const Clock::time_point discoverEnd = Clock::now();

    Stack::RawWorkspace::WorkspaceState state;
    state.workspaceRoot = scan.layout.workspaceRoot;
    state.sources = scan.sources;
    if (!state.sources.empty()) {
        const std::string selectedKey = state.sources[state.sources.size() / 2].relativePathKey;
        if (!Stack::RawWorkspace::SelectSourceByKey(state, selectedKey) ||
            state.selectedSourceKey != selectedKey) {
            std::cerr << "RAW Workspace loading smoke failed: source selection failed.\n";
            return false;
        }
    }

    const Stack::RawWorkspace::GalleryPresentation presentation =
        Stack::RawWorkspace::BuildGalleryPresentation(state);
    if (presentation.totalSources != static_cast<int>(state.sources.size())) {
        std::cerr << "RAW Workspace loading smoke failed: gallery presentation source count mismatch.\n";
        return false;
    }
    if (!state.sources.empty() && !presentation.hasSelection) {
        std::cerr << "RAW Workspace loading smoke failed: gallery presentation did not preserve selection.\n";
        return false;
    }

    bool cancelScan = false;
    Stack::RawWorkspace::ScanResult canceledScan = Stack::RawWorkspace::ScanWorkspace(
        workspaceRoot,
        isRawPath,
        [&](const Stack::RawWorkspace::ScanProgress& progress) {
            if (progress.filesVisited > 0 || progress.discoveredRawCount > 0) {
                cancelScan = true;
            }
        },
        [&]() {
            return cancelScan;
        });
    if (canceledScan.success ||
        canceledScan.errorMessage.find("canceled") == std::string::npos) {
        std::cerr << "RAW Workspace loading smoke failed: scan cancellation did not stop the scan.\n";
        return false;
    }

    std::vector<Stack::RawWorkspace::SourceRecord> cancellationSources = scan.sources;
    if (Stack::RawWorkspace::ClassifyThumbnails(
            scan.layout,
            cancellationSources,
            Stack::RawWorkspace::kNeutralThumbnailMaxDimension,
            []() {
                return true;
            })) {
        std::cerr << "RAW Workspace loading smoke failed: classify cancellation did not stop work.\n";
        return false;
    }
    if (Stack::RawWorkspace::DiscoverProjects(
            scan.layout,
            cancellationSources,
            []() {
                return true;
            })) {
        std::cerr << "RAW Workspace loading smoke failed: project discovery cancellation did not stop work.\n";
        return false;
    }

    std::cout << "RAW Workspace loading smoke passed: root=\"" << workspaceRoot.string()
              << "\" sources=" << scan.sources.size()
              << " groups=" << presentation.groups.size()
              << " thumbnails(valid=" << thumbnailProgress.valid
              << ", queued=" << thumbnailProgress.queued
              << ", failed=" << thumbnailProgress.failed
              << ") projects(existing="
              << std::count_if(scan.sources.begin(), scan.sources.end(), [](const Stack::RawWorkspace::SourceRecord& source) {
                     return source.project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
                         source.project.status == Stack::RawWorkspace::ProjectStatus::Embedded;
                 })
              << ") timings(ms scan=" << ElapsedMilliseconds(scanStart, scanEnd)
              << ", classify=" << ElapsedMilliseconds(classifyStart, classifyEnd)
              << ", discover=" << ElapsedMilliseconds(discoverStart, discoverEnd)
              << ")\n";
    return true;
}

} // namespace Stack::Validation
