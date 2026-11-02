#include "Persistence/ProjectStore.h"
#include "Persistence/ClosetMerge.h"
#include "Raw/RawTechnicalEvidence.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <sstream>

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
    Fixture(const fs::path& path) : root(path / "Photo.stack") {
        snapshot.projectId = Project::GenerateStableUuid();
        snapshot.projectName = "Original";
        opened = Project::CreateProjectStore(root, Project::ProjectStorageKind::WorkingFile, snapshot);
        Require(bool(opened), "create real project store");
        Require(bool(Commit(oldCover, "Original")), "publish first cover");
    }
    fs::path Manifest() const { return Project::WorkingProjectDocumentPath(root); }
    fs::path Cover() const {
        const auto contents = Read(Manifest());
        const auto manifest = nlohmann::json::parse(contents.substr(contents.find('\n') + 1));
        return root.parent_path() / fs::u8path(manifest.at("_store").at("coverThumbnail")
            .at("relativePath").get<std::string>());
    }
    fs::path ExpectedCover(const Bytes& bytes) const {
        const auto identity = Stack::RawEvidence::ComputeSourceIdentity(bytes);
        return root.parent_path() / "Thumbnails" / "Project Covers" / (identity.sha256 + "-" + std::to_string(identity.byteSize) + ".png");
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
};
}

int main() try {
    Sandbox box;
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
        Require(Read(fixture.Manifest()) == manifest &&
            Read(cover) == "old", "final publication failure preserves both manifests and prior cover");
        fixture.RequireOldReopens();
    }
#endif
    {
        const auto legacy = box.root / "legacy";
        fs::create_directory(legacy);
        Put(legacy / "project.stack", "{}");
        Require(!Project::OpenProjectStore(legacy) && !Project::OpenProjectStore(legacy / "project.stack"),
            "obsolete project layout is not accepted");
        Require(Read(legacy / "project.stack") == "{}", "obsolete files are untouched");
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
    {
        // Real encoded 1x1 24-bit BMPs. Each capture has different pixels.
        const auto root = box.root / "Trip";
        const auto projects = root / "Closet" / "Projects";
        fs::create_directories(projects);
        fs::create_directory(root / "Day 1");
        Put(root / "Closet" / "workspace.json", R"({"schema":"stack.rawWorkspace.closet","schemaVersion":1,"workingProjectFormat":1})");
        std::string bmp(58, '\0');
        bmp[0]='B'; bmp[1]='M'; bmp[2]=58; bmp[10]=54; bmp[14]=40;
        bmp[18]=1; bmp[22]=1; bmp[26]=1; bmp[28]=24; bmp[34]=4;
        for (int i=0; i<2000; ++i) {
            bmp[54]=static_cast<char>(i); bmp[55]=static_cast<char>(i>>8);
            Put(root / "Day 1" / (std::to_string(i) + ".bmp"), bmp);
        }
        Project::RawProjectSnapshot bootstrap;
        bootstrap.projectId=Project::GenerateStableUuid(); bootstrap.projectName="Portrait";
        auto first=Project::CreateProjectStore(projects/"Portrait.stack", Project::ProjectStorageKind::WorkingFile, bootstrap);
        Require(bool(first), "create working file in photo Closet");
        auto tx=first.store->BeginTransaction(first.store->StorageRevision());
        std::string error;
        for (int i=0; i<2; ++i) {
            Project::EmbeddedAssetRecord asset;
            Require(first.store->StageAssetFile(tx,root/"Day 1"/(std::to_string(i)+".bmp"),
                Project::MultiFrameInputFamily::Raster,nlohmann::json::object(),asset,&error),"link capture");
            first.snapshot.embeddedAssets.push_back(asset);
        }
        Project::EmbeddedAssetRecord mask;
        const Bytes paint{1, 42, 99};
        auto identity=Stack::RawEvidence::ComputeSourceIdentity(paint);
        mask.sha256=identity.sha256; mask.byteLength=paint.size();
        mask.assetId=Project::MakeAssetId(mask.sha256,mask.byteLength);
        mask.originalFileFingerprint = mask.sha256;
        mask.displayName = mask.originalFilename = "painted.mask"; mask.managedRole="graph-payload";
        mask.projectAssetPath="assets/painted.mask";
        std::istringstream input(std::string(paint.begin(),paint.end()));
        Require(first.store->StageAssetStream(tx,input,mask,&error),"stage authored samples");
        first.snapshot.embeddedAssets.push_back(mask);
        const auto committed = first.store->Commit(tx, first.snapshot);
        Require(bool(committed), committed.message.c_str());
        first=Project::OpenProjectStore(first.store->StoragePath());
        for (int i=1; i<10; ++i) {
            auto edit=first.snapshot; edit.projectId=Project::GenerateStableUuid();
            edit.projectName="Edit "+std::to_string(i);
            Require(bool(Project::ConvertProjectStore(first.store,edit,projects/(edit.projectName+".stack"),
                Project::ProjectStorageKind::WorkingFile)),"save linked version");
        }
        std::uintmax_t projectBytes=0, payloadBytes=0; unsigned projectCount=0, payloadCount=0;
        for (const auto& entry:fs::recursive_directory_iterator(root/"Closet")) {
            if (!entry.is_regular_file() || entry.path().filename()=="workspace.json") continue;
            if (entry.path().parent_path()==projects && entry.path().extension()==".stack") {
                ++projectCount; projectBytes+=entry.file_size();
            } else { ++payloadCount; payloadBytes+=entry.file_size(); }
        }
        Require(projectCount==10 && payloadCount==1 && payloadBytes==paint.size(),"working saves duplicated source or mask bytes");
        std::cout<<"Disk inventory: 2000 originals, "<<projectCount<<" projects ("<<projectBytes
            <<" bytes), "<<payloadCount<<" authored payload ("<<payloadBytes<<" bytes), 0 copied originals\n";
        const auto nested = root / "Day 1" / "Closet";
        fs::create_directories(nested / "Projects");
        fs::create_directories(root / "Closet" / "Catalog");
        Put(nested / "workspace.json", R"({"schema":"stack.rawWorkspace.closet","schemaVersion":1,"workingProjectFormat":1})");
        auto distinct = first.snapshot;
        distinct.projectName = "Nested conflicting edit";
        const auto inside = nested / "Projects" / "Portrait.stack";
        Require(bool(Project::ConvertProjectStore(first.store, distinct, inside,
            Project::ProjectStorageKind::WorkingFile)), "stage nested conflicting project");
        Project::ClosetMergePlan merge;
        Require(Project::InspectClosetMerge(root, nested, merge, error) &&
            merge.projects.size() == 1 && merge.authoredAssets.size() == 1, "inspect merge dependencies");
        Require(Project::MergeCloset(merge, error), error.c_str());
        auto imported = Project::OpenProjectStore(projects / "Portrait-2.stack");
        Require(imported && imported.snapshot.projectId != first.snapshot.projectId &&
            imported.snapshot.projectName == distinct.projectName && imported.store->Verify(imported.snapshot),
            "merge preserved distinct edit and rebased sources");
        Require(!fs::exists(nested) && fs::exists(root / "Day 1" / "0.bmp"), "retire only nested Closet");
        for (const auto& entry : fs::directory_iterator(root / "Day 1")) {
            if (entry.is_directory() && entry.path().filename().u8string().rfind(".stack-merged-",0) == 0) {
                fs::rename(entry.path(), nested);
                break;
            }
        }
        Require(Project::MergeCloset(merge, error) && !fs::exists(projects / "Portrait-3.stack"),
            "retry after interrupted retirement must not import a duplicate");
        const auto packed=box.root/"Portable.stack";
        Require(bool(Project::ConvertProjectStore(first.store,first.snapshot,packed,Project::ProjectStorageKind::PortableFile)),
            "pack dependencies");
        fs::rename(root,box.root/"Moved");
        const auto moved=box.root/"Moved";
        const auto project=moved/"Closet"/"Projects"/"Portrait.stack";
        auto reopened=Project::OpenProjectStore(project);
        Require(reopened && reopened.store->Verify(reopened.snapshot),"move entire root");
        const auto relocated=box.root/"Renamed";
        fs::create_directory(relocated);
        for (int i=0; i<2; ++i) fs::rename(moved/"Day 1"/(std::to_string(i)+".bmp"),relocated/(std::to_string(i+87)+".bmp"));
        Put(moved/"Day 1"/"0.bmp",bmp);
        reopened=Project::OpenProjectStore(project);
        Require(reopened && !reopened.store->OpenAssetStream(reopened.snapshot.embeddedAssets.front().assetId),
            "reject same-name same-length source with wrong SHA");
        std::size_t matched=0;
        Require(Project::RelinkProjectSources(project,relocated,matched,error) && matched==2,"relink all exact matches in one folder");
        reopened=Project::OpenProjectStore(project);
        Require(reopened && reopened.store->Verify(reopened.snapshot),"relinked project verifies");
        auto other=Project::OpenProjectStore(project.parent_path()/"Edit 1.stack");
        Require(other && other.store->Verify(other.snapshot),"Locate folder reconnects another edit sharing the same original");
        fs::remove_all(relocated);
        auto portable=Project::OpenProjectStore(packed);
        Require(portable && portable.store->Verify(portable.snapshot),"portable owns every original and authored payload");
    }
    std::cout << "Project cover transaction tests passed\n";
} catch (const std::exception& exception) {
    std::cerr << exception.what() << "\n";
    return 1;
}
