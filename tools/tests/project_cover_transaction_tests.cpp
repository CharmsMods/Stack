#include "Persistence/ProjectStore.h"
#include "Raw/RawTechnicalEvidence.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__linux__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
// Interpose only this test executable's final-manifest rename. Production
// code has no failure-injection hook and all fixture files are private.
namespace { const char* blockedRename = nullptr; unsigned blockedRenameCount = 0; }
extern "C" int rename(const char* from, const char* to) noexcept {
    if (blockedRename && std::strcmp(to, blockedRename) == 0) {
        ++blockedRenameCount;
        errno = EACCES;
        return -1;
    }
    return static_cast<int>(syscall(SYS_renameat, AT_FDCWD, from, AT_FDCWD, to));
}
#endif

namespace {
namespace fs = std::filesystem;
namespace Project = Stack::Project;
using Bytes = std::vector<unsigned char>;
const Bytes oldCover{ 'o', 'l', 'd' };
const Bytes newCover{ 'n', 'e', 'w' };
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string Read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    Require(bool(input), "read fixture");
    return {std::istreambuf_iterator<char>(input), {}};
}
void Put(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << bytes;
    output.close();
    Require(output.good(), "write fixture");
}
struct Sandbox {
    fs::path root = fs::temp_directory_path() / ("stack-cover-tests-" + Project::GenerateStableUuid());
    Sandbox() { Require(fs::create_directory(root), "create private sandbox"); }
    ~Sandbox() { std::error_code error; fs::remove_all(root, error); }
};
struct Fixture {
    fs::path root;
    Project::ProjectStoreOpenResult opened;
    Project::RawProjectSnapshot snapshot;
    Fixture(const fs::path& path) : root(path) {
        snapshot.projectId = Project::GenerateStableUuid();
        snapshot.projectName = "Original";
        opened = Project::CreateProjectStore(root, Project::ProjectStorageKind::DirectoryBundle, snapshot);
        Require(bool(opened), "create real project store");
        Require(bool(Commit(oldCover, "Original")), "publish first cover");
    }
    fs::path Manifest() const { return Project::WorkingProjectDocumentPath(root); }
    fs::path Previous() const { return Manifest().string() + ".previous"; }
    fs::path Cover() const {
        const auto manifest = nlohmann::json::parse(Read(Manifest()));
        return root / fs::u8path(manifest.at("_store").at("coverThumbnail")
            .at("relativePath").get<std::string>());
    }
    fs::path ExpectedCover(const Bytes& bytes) const {
        const auto identity = Stack::RawEvidence::ComputeSourceIdentity(bytes);
        return root / "previews" / (identity.sha256 + "-" + std::to_string(identity.byteSize) + ".png");
    }
    Project::ProjectStoreCommitResult Commit(const Bytes& cover, const std::string& name) {
        snapshot.coverThumbnailBytes = cover;
        snapshot.projectName = name;
        auto transaction = opened.store->BeginTransaction(opened.store->StorageRevision());
        Require(bool(transaction), "begin actual transaction");
        auto result = opened.store->Commit(transaction, snapshot);
        if (!result) opened.store->Abort(transaction);
        return result;
    }
    void RequireOldReopens() const {
        const auto reloaded = Project::OpenProjectStore(root);
        Require(bool(reloaded) && !reloaded.recoveredPreviousManifest &&
            reloaded.snapshot.projectName == "Original" &&
            reloaded.snapshot.coverThumbnailBytes == oldCover, "prior project and cover reopen intact");
    }
    void RequireOldRecovery() const {
        Put(Manifest(), "invalid current manifest");
        const auto reloaded = Project::OpenProjectStore(root);
        Require(bool(reloaded) && reloaded.recoveredPreviousManifest &&
            reloaded.snapshot.projectName == "Original" &&
            reloaded.snapshot.coverThumbnailBytes == oldCover, "previous manifest restores its own cover");
    }
};
}

int main() {
    Sandbox box;
    {
        Fixture fixture(box.root / "backup-failure");
        const auto manifest = Read(fixture.Manifest());
        const auto cover = fixture.Cover();
        Require(fs::remove(fixture.Previous()), "remove private previous fixture");
        Require(fs::create_directory(fixture.Previous()), "block backup publication");
        Put(fixture.Previous() / "blocker", "x");
        const auto result = fixture.Commit(newCover, "Changed");
        Require(!result && result.message == "Could not retain the previous bundle manifest.", "backup failure reached");
        Require(Read(fixture.Manifest()) == manifest && Read(cover) == "old", "backup failure preserves manifest and cover");
        fixture.RequireOldReopens();
    }
#if defined(_WIN32) || defined(__linux__)
    {
        Fixture fixture(box.root / "final-failure");
        const auto manifest = Read(fixture.Manifest());
        const auto cover = fixture.Cover();
#if defined(_WIN32)
        const HANDLE guard = CreateFileW(fixture.Manifest().c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(guard != INVALID_HANDLE_VALUE, "hold manifest without delete sharing");
#else
        const auto target = fixture.Manifest().string();
        blockedRename = target.c_str();
        blockedRenameCount = 0;
#endif
        const auto result = fixture.Commit(newCover, "Changed");
#if defined(_WIN32)
        CloseHandle(guard);
#else
        blockedRename = nullptr;
        Require(blockedRenameCount == 1, "interposed exactly the final publication");
#endif
        Require(!result && result.message == "Could not atomically publish the bundle manifest.", "final publication failure reached");
        Require(Read(fixture.Manifest()) == manifest && Read(fixture.Previous()) == manifest &&
            Read(cover) == "old", "final publication failure preserves both manifests and prior cover");
        fixture.RequireOldReopens();
    }
#endif
    {
        Fixture fixture(box.root / "recovery");
        const auto oldPath = fixture.Cover();
        Require(bool(fixture.Commit(newCover, "Changed")), "publish changed cover");
        Require(fixture.Cover() != oldPath && Read(oldPath) == "old", "success retains prior immutable cover");
        fixture.RequireOldRecovery();
    }
    {
        Fixture fixture(box.root / "legacy-recovery");
        auto manifest = nlohmann::json::parse(Read(fixture.Manifest()));
        fs::rename(fixture.Cover(), fixture.root / "preview.png");
        manifest["_store"]["coverThumbnail"]["relativePath"] = "preview.png";
        Put(fixture.Manifest(), manifest.dump(2));
        fixture.opened = Project::OpenProjectStore(fixture.root);
        Require(bool(fixture.opened), "load legacy fixed-cover manifest");
        Require(bool(fixture.Commit(newCover, "Changed")), "save legacy manifest to immutable cover");
        Require(Read(fixture.root / "preview.png") == "old", "legacy cover untouched");
        fixture.RequireOldRecovery();
    }
    {
        Fixture fixture(box.root / "reuse");
        const auto cover = fixture.Cover();
        const auto marker = fs::file_time_type::clock::now() - std::chrono::hours(24);
        fs::last_write_time(cover, marker);
        const auto storedMarker = fs::last_write_time(cover);
        Require(bool(fixture.Commit(oldCover, "Renamed")), "save metadata with identical cover");
        Require(fixture.Cover() == cover && fs::last_write_time(cover) == storedMarker, "same cover verified and reused without replacement");
        Require(std::distance(fs::directory_iterator(cover.parent_path()), fs::directory_iterator()) == 1,
            "same cover does not create duplicate immutable files");
    }
    {
        Fixture fixture(box.root / "occupied-corrupt");
        const auto cover = fixture.Cover();
        const auto occupied = fixture.ExpectedCover(newCover);
        Put(occupied, "bad");
        Require(bool(fixture.Commit(newCover, "Changed")), "optional corrupt cover publication remains nonfatal");
        const auto reloaded = Project::OpenProjectStore(fixture.root);
        Require(bool(reloaded) && reloaded.snapshot.projectName == "Changed" &&
            reloaded.snapshot.coverThumbnailBytes.empty(), "mismatched occupied cover is not referenced");
        Require(Read(occupied) == "bad" && Read(cover) == "old", "neither occupied nor previous cover overwritten");
        fixture.RequireOldRecovery();
    }
    {
        Fixture fixture(box.root / "bracket-required");
        const auto manifest = Read(fixture.Manifest());
        const auto cover = fixture.Cover();
        const auto occupied = fixture.ExpectedCover(newCover);
        Put(occupied, "wrong-length");
        Project::MultiFrameSourceSet sourceSet;
        sourceSet.sourceSetId = Project::GenerateStableUuid();
        // Exercise the store's required-cover marker independently of image
        // processing. This empty source set is a valid authored draft.
        sourceSet.operationIntent = Project::MultiFrameOperationIntent::Mfsr;
        sourceSet.settings["bracketingResult"] = nlohmann::json::object();
        fixture.snapshot.sourceSets.push_back(sourceSet);
        const auto validation = Project::ValidateRawProjectSnapshot(fixture.snapshot);
        for (const auto& error : validation.errors) std::cerr << error << '\n';
        Require(validation.valid, "required-cover fixture is valid");
        const auto result = fixture.Commit(newCover, "Changed");
        Require(!result && result.message == "Could not publish the saved bracket cover.", "required bracket cover still fails save");
        Require(Read(fixture.Manifest()) == manifest && Read(occupied) == "wrong-length" && Read(cover) == "old",
            "required-cover failure preserves prior state and occupied bytes");
        fixture.RequireOldReopens();
    }
    {
        Fixture fixture(box.root / "optional-active-corruption");
        Put(fixture.Cover(), "bad");
        const auto reloaded = Project::OpenProjectStore(fixture.root);
        Require(bool(reloaded) && !reloaded.recoveredPreviousManifest &&
            reloaded.snapshot.coverThumbnailBytes.empty() && !reloaded.message.empty(),
            "corrupt active preview does not reject or roll back authoritative project");
    }
    std::cout << "Project cover transaction tests passed\n";
}
