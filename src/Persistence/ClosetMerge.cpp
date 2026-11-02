#include "Persistence/ClosetMerge.h"
#include "Persistence/ManagedAssetPaths.h"
#include "Persistence/ProjectStore.h"
#include "Raw/RawTechnicalEvidence.h"

#include <algorithm>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include <set>
#include <map>
#include <unordered_map>

namespace Stack::Project {
bool InspectClosetMerge(const std::filesystem::path& parentRoot,
    const std::filesystem::path& nestedCloset, ClosetMergePlan& plan, std::string& error) try {
    plan = {};
    const auto parent = std::filesystem::canonical(parentRoot);
    const auto nested = std::filesystem::canonical(nestedCloset);
    const auto relative = nested.lexically_relative(parent);
    if (relative.empty() || *relative.begin() == ".." || relative.is_absolute() ||
        nested.parent_path() == parent || nested.filename() != "Closet" ||
        !IsPhotoCloset(nested) || !IsPhotoCloset(parent / "Closet")) {
        error = "Choose a nested Stack Closet inside this photo root.";
        return false;
    }
    std::ifstream marker(nested/"workspace.json",std::ios::binary);
    const auto manifest=nlohmann::json::parse(marker,nullptr,false);
    if (!manifest.is_object() || manifest.value("workingProjectFormat",0)!=1) {
        error="Only a new-format Closet can be merged.";return false;
    }
    plan.parentRoot = parent;
    plan.nestedCloset = nested;
    std::map<std::string, std::string> payloads;
    for (const auto& entry : std::filesystem::directory_iterator(nested / "Projects")) {
        if (entry.path().filename() == ".staging" && entry.is_directory() &&
            std::filesystem::is_empty(entry.path())) continue;
        if (!entry.is_regular_file() || !IsWorkingProjectFile(entry.path()) ||
            std::filesystem::canonical(entry.path()) != entry.path()) {
            error = "The nested Projects folder contains an unsupported file, redirected path, or unfinished save.";
            return false;
        }
        auto opened = OpenProjectStore(entry.path());
        if (!opened) { error = opened.message; return false; }
        plan.projects.push_back(entry.path());
        for (const auto& asset : opened.snapshot.embeddedAssets)
            if (!asset.linkedSource) payloads.emplace(asset.assetId, asset.displayName);
    }
    std::sort(plan.projects.begin(), plan.projects.end());
    for (const auto& file : plan.projects) {
        const auto identity = RawEvidence::ComputeSourceIdentity(file);
        if (!identity.valid) { error = "A nested project is unavailable."; return false; }
        plan.revisions.push_back(identity.sha256);
    }
    for (const auto& payload : payloads) plan.authoredAssets.push_back(payload.second);
    if (plan.projects.empty()) {
        std::ifstream input(nested / "workspace.json", std::ios::binary);
        const auto marker = json::parse(input, nullptr, false);
        if (!marker.is_object() || marker.value("workingProjectFormat", 0) != 1) {
            error = "This Closet has no new-format working projects to merge.";
            return false;
        }
    }
    return true;
} catch (const std::exception& exception) { error = exception.what(); return false; }

bool MergeCloset(const ClosetMergePlan& requested, std::string& error) try {
    // Serialize import and retirement across editors sharing the same parent.
#ifdef _WIN32
    const auto lockPath = requested.parentRoot / "Closet" / "Catalog" / ".merge.lock";
    HANDLE handle = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (handle == INVALID_HANDLE_VALUE) { error = "Another merge is active or this Closet is unavailable."; return false; }
    struct Lock { HANDLE handle; ~Lock() { CloseHandle(handle); } } lock{handle};
#endif
    ClosetMergePlan plan;
    if (!InspectClosetMerge(requested.parentRoot, requested.nestedCloset, plan, error)) return false;
    if (requested.projects != plan.projects || requested.revisions != plan.revisions || requested.authoredAssets != plan.authoredAssets) {
        error = "The nested Closet changed. Review the merge again.";
        return false;
    }
    const auto parentProjects = plan.parentRoot / "Closet" / "Projects";
    const auto staging = parentProjects / ".staging" / GenerateStableUuid();
    std::filesystem::create_directories(staging);
    struct StagingCleanup {
        std::filesystem::path directory;
        ~StagingCleanup() { std::error_code ec; std::filesystem::remove_all(directory, ec); }
    } cleanup{staging};
    // IDs identify logical edits. Receipts identify the exact nested revision
    // already imported if publication succeeded but retirement was interrupted.
    std::set<std::string> ids;
    std::unordered_map<std::string, std::filesystem::path> receipts;
    for (const auto& file : std::filesystem::directory_iterator(parentProjects)) {
        if (!file.is_regular_file() || !IsWorkingProjectFile(file.path())) continue;
        auto opened = OpenProjectStore(file.path());
        if (!opened) { error = opened.message; return false; }
        ids.insert(opened.snapshot.projectId);
        receipts.emplace(opened.snapshot.adoptedFrom, file.path());
    }
    for (std::size_t i = 0; i < plan.projects.size(); ++i) {
        const auto& file = plan.projects[i];
        auto source = OpenProjectStore(file);
        if (!source) { error = source.message; return false; }
        const auto receipt = "closet-import:" + plan.revisions[i];
        if (receipts.count(receipt)) continue;
        auto snapshot = source.snapshot;
        if (ids.count(snapshot.projectId)) {
            snapshot.projectId = GenerateStableUuid();
            if (snapshot.rawWorkspaceData.is_object())
                snapshot.rawWorkspaceData["projectId"] = snapshot.projectId;
        }
        ids.insert(snapshot.projectId);
        snapshot.adoptedFrom = receipt;
        const auto temporary = staging / (GenerateStableUuid() + ".stack");
        auto copy = ConvertProjectStore(source.store, snapshot, temporary, ProjectStorageKind::WorkingFile);
        if (!copy || copy.snapshot.projectId != snapshot.projectId ||
            copy.snapshot.embeddedAssets.size() != snapshot.embeddedAssets.size()) {
            error = copy ? "The imported project could not be verified." : copy.message;
            return false;
        }
        for (const auto& asset : copy.snapshot.embeddedAssets) {
            if (asset.linkedSource) continue;
            const auto stored = RawEvidence::ComputeSourceIdentity(copy.store->ResolveAssetFile(asset.assetId));
            if (!stored.valid || stored.sha256 != asset.sha256 || stored.byteSize != asset.byteLength) {
                error = "An imported project payload failed verification.";
                return false;
            }
        }
        const auto destination = UniqueWorkingProjectPath(parentProjects, file.stem().u8string());
        copy.store.reset();
        std::filesystem::rename(temporary, destination);
        receipts.emplace(receipt, destination);
    }
    ClosetMergePlan finalInventory;
    if (!InspectClosetMerge(plan.parentRoot, plan.nestedCloset, finalInventory, error) ||
        finalInventory.projects != plan.projects || finalInventory.revisions != plan.revisions) { error = "The nested Closet changed during the merge."; return false; }
    // Keep the retired Closet intact, including its Trash. Photos outside it
    // never move. Discovery recognizes a retired marked Closet by this prefix.
    std::filesystem::rename(plan.nestedCloset, plan.nestedCloset.parent_path() /
        (".stack-merged-" + GenerateStableUuid()));
    return true;
} catch (const std::exception& exception) { error = exception.what(); return false; }
}
