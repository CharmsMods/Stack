#include "Selection.h"
#include "Raw/Bracketing/GroupSuggestion.h"
#include "Raw/Bracketing/SourceSetIdentity.h"
#include "Raw/RawCaptureCompatibility.h"
#include "Raw/RawGalleryInputPolicy.h"
#include "Raw/RawGallerySimilarity.h"
#include "Raw/RawGalleryThumbnailPixels.h"
#include "Raw/RawLoader.h"
#include "Raw/RawTechnicalEvidence.h"
#include <algorithm>
#include <map>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Stack::BurstEngine {
namespace {
struct SelectedPath {
    std::filesystem::path path;
    std::string key, folderKey, orderKey, displayKey;
};
struct Capture {
    Raw::Bracketing::Source source;
    Project::RawCaptureCompatibilitySummary metadata;
    RawWorkspace::RawGallerySimilarityInput similarity;
};

std::string FolderKey(const std::filesystem::path& path) {
#ifdef _WIN32
    // Selection paths may spell the same Windows parent with different case.
    // Use invariant Unicode folding for identity, retaining original paths for
    // opening files and user-facing diagnostics.
    const auto native = path.generic_wstring();
    const auto count = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
        native.data(), static_cast<int>(native.size()), nullptr, 0, nullptr, nullptr, 0);
    if (count > 0) {
        std::wstring folded(static_cast<std::size_t>(count), L'\0');
        if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, native.data(),
            static_cast<int>(native.size()), folded.data(), count, nullptr, nullptr, 0))
            return std::filesystem::path(folded).generic_u8string();
    }
#endif
    return path.generic_u8string();
}

std::string BurstIdentity(const std::vector<Raw::Bracketing::Source>& sources) {
    std::vector<Raw::Bracketing::CaptureContentIdentity> identities;
    identities.reserve(sources.size());
    for (const auto& source : sources)
        identities.push_back({source.sha256, source.bytes});
    return Raw::Bracketing::SourceSetIdentity(identities);
}
} // namespace

SelectionResult AnalyzeSelection(const SelectionRequest& request) {
    SelectionResult result;
    const auto canceled = [&] { return request.shouldCancel && request.shouldCancel(); };
    const auto report = [&](std::size_t completed, const std::string& message) {
        if (request.reportProgress) request.reportProgress(completed, request.paths.size(), message);
    };
    const auto skip = [&](const std::filesystem::path& path, SelectionIssue issue,
        const std::string& message, const std::filesystem::path& related = {}) {
        result.skipped.push_back({path, issue, message, related});
    };
    try {
        std::vector<SelectedPath> paths;
        paths.reserve(request.paths.size());
        for (const auto& path : request.paths) {
            if (canceled()) break;
            std::error_code error;
            if (path.empty() || !std::filesystem::is_regular_file(path, error)) {
                skip(path, SelectionIssue::Unreadable, "The selected item is not an accessible image file.");
                continue;
            }
            const auto absolute = std::filesystem::absolute(path, error).lexically_normal();
            if (error) {
                skip(path, SelectionIssue::Unreadable, "Could not resolve the selected file: " + error.message());
                continue;
            }
            // Match Gallery's lexical absolute paths: a selected link keeps
            // its selected parent for grouping and output placement.
            paths.push_back({absolute, FolderKey(absolute), FolderKey(absolute.parent_path()),
                RawWorkspace::RawGalleryPathOrderKey(absolute.filename().u8string()), absolute.generic_u8string()});
        }
        // Gallery assigns catalog indices in case-folded relative-path order.
        // Apply that filename order within each explicitly selected parent.
        std::stable_sort(paths.begin(), paths.end(), [](const auto& left, const auto& right) {
            if (left.folderKey != right.folderKey) return left.folderKey < right.folderKey;
            return left.orderKey != right.orderKey ? left.orderKey < right.orderKey : left.displayKey < right.displayKey;
        });
        std::map<std::string, std::filesystem::path> seenPaths;
        std::map<std::pair<std::string, std::string>, std::filesystem::path> seenContent;
        std::vector<Capture> captures;
        std::size_t completed = result.skipped.size();
        for (const auto& selected : paths) {
            if (canceled()) break;
            const auto& path = selected.path;
            report(completed, "Reading " + path.filename().u8string());
            ++completed;
            const auto previous = seenPaths.find(selected.key);
            if (previous != seenPaths.end()) {
                skip(path, SelectionIssue::Duplicate, "This file was selected more than once.", previous->second);
                continue;
            }
            seenPaths.emplace(selected.key, path);
            if (!RawWorkspace::IsGalleryRawPath(path)) {
                skip(path, SelectionIssue::Unsupported, "This file type is not supported by the RAW burst workflow.");
                continue;
            }
            Raw::RawMetadata metadata;
            if (!Raw::RawLoader::LoadMetadata(path.u8string(), metadata)) {
                skip(path, SelectionIssue::Unreadable, metadata.error.empty() ? "Could not read RAW metadata." : metadata.error);
                continue;
            }
            const auto compatibility = Project::BuildRawCaptureCompatibilitySummary(metadata);
            if (!compatibility.supported) {
                skip(path, SelectionIssue::Unsupported, compatibility.rejectionReason);
                continue;
            }
            const auto identity = RawEvidence::ComputeSourceIdentity(path, request.shouldCancel);
            if (canceled()) break;
            if (!identity.valid) {
                skip(path, SelectionIssue::Unreadable, "Could not identify this capture: " + identity.reason);
                continue;
            }
            const auto& folder = selected.folderKey;
            const auto contentKey = std::make_pair(folder, identity.sha256);
            const auto duplicate = seenContent.find(contentKey);
            if (duplicate != seenContent.end()) {
                skip(path, SelectionIssue::Duplicate,
                    "This is an identical copy of another selected capture in this folder.", duplicate->second);
                continue;
            }
            Raw::RawImageData raw;
            if (!Raw::RawLoader::LoadFile(path.u8string(), raw, request.shouldCancel)) {
                if (canceled()) break;
                skip(path, SelectionIssue::Unreadable, raw.metadata.error.empty() ? "Could not decode this RAW capture." : raw.metadata.error);
                continue;
            }
            if (raw.metadata.sourceContentSha256 != identity.sha256 || raw.metadata.sourceByteSize != identity.byteSize) {
                skip(path, SelectionIssue::Unreadable, "The capture changed while it was being analyzed. Select it again after writing finishes.");
                continue;
            }
            std::vector<unsigned char> pixels;
            int width = 0, height = 0;
            if (!RawWorkspace::BuildRawGalleryThumbnailPixels(raw,
                RawWorkspace::kNeutralThumbnailMaxDimension, pixels, width, height, false, request.shouldCancel)) {
                if (canceled()) break;
                skip(path, SelectionIssue::Unreadable, "Could not prepare this capture for automatic grouping.");
                continue;
            }
            Capture capture;
            capture.source = {identity.sha256, identity.sha256, identity.byteSize, path, path.filename().u8string()};
            capture.metadata = Project::BuildRawCaptureCompatibilitySummary(raw.metadata);
            if (!capture.metadata.supported) {
                skip(path, SelectionIssue::Unsupported, capture.metadata.rejectionReason);
                continue;
            }
            capture.similarity = {selected.displayKey, folder, captures.size(),
                RawWorkspace::BuildRawGallerySimilarityDescriptor(pixels.data(), width, height)};
            captures.push_back(std::move(capture));
            seenContent.emplace(contentKey, path);
        }
        if (canceled()) { result.canceled = true; return result; }
        report(request.paths.size(), "Grouping selected captures");
        std::vector<RawWorkspace::RawGallerySimilarityInput> inputs;
        std::map<std::string, const Capture*> byKey;
        for (const auto& capture : captures) {
            inputs.push_back(capture.similarity);
            byKey.emplace(capture.similarity.sourceKey, &capture);
        }
        const auto stacks = RawWorkspace::BuildRawGallerySimilarityStacks(inputs, request.shouldCancel);
        for (const auto& stack : stacks) {
            if (canceled()) break;
            if (stack.sourceKeys.size() < 2) {
                const auto& capture = *byKey.at(stack.sourceKeys.front());
                skip(capture.source.path, SelectionIssue::Singleton,
                    "No matching independent capture was selected in this folder. Select the rest of its burst.");
                continue;
            }
            const auto& reference = *byKey.at(stack.sourceKeys.front());
            std::string incompatibility;
            const Capture* incompatible = nullptr;
            for (const auto& key : stack.sourceKeys) {
                const auto& candidate = *byKey.at(key);
                if (!Project::AreHdrCapturesStructurallyCompatible(reference.metadata, candidate.metadata, &incompatibility)) {
                    incompatible = &candidate;
                    break;
                }
            }
            if (incompatible) {
                for (const auto& key : stack.sourceKeys)
                    skip(byKey.at(key)->source.path, SelectionIssue::Incompatible,
                        "This automatically grouped set cannot be combined: " + incompatibility, incompatible->source.path);
                continue;
            }
            SelectedBurst burst;
            burst.name = reference.source.path.stem().u8string() + " Burst";
            burst.parentDirectory = reference.source.path.parent_path();
            std::vector<Raw::Bracketing::CaptureExposure> exposures;
            for (const auto& key : stack.sourceKeys) {
                const auto& capture = *byKey.at(key);
                burst.sources.push_back(capture.source);
                exposures.push_back({capture.source.frameId, capture.metadata.exposureTimeSeconds,
                    capture.metadata.apertureFNumber, capture.metadata.isoSpeed});
            }
            burst.recipe = Raw::Bracketing::SuggestGroups(exposures);
            burst.identity = BurstIdentity(burst.sources);
            result.bursts.push_back(std::move(burst));
        }
        if (canceled()) { result.canceled = true; result.bursts.clear(); }
        else report(request.paths.size(), "Selected captures are ready");
    } catch (const std::exception& error) {
        result.bursts.clear();
        result.canceled = canceled();
        if (!result.canceled) result.error = error.what();
    }
    return result;
}
} // namespace Stack::BurstEngine
