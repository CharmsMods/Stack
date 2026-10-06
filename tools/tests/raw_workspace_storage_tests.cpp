#include "Raw/RawWorkspace.h"
#include "Raw/RawGalleryFileActions.h"
#include "Persistence/ProjectStore.h"
#include "Persistence/ProjectIndex.h"
#include "ThirdParty/stb_image_write.h"
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <unordered_map>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <sddl.h>
#endif

namespace {
void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void Write(const std::filesystem::path& path, const std::string& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << data;
    Check(stream.good(), "Could not write storage fixture");
}
nlohmann::json Read(const std::filesystem::path& path) {
    std::ifstream stream(path);
    return nlohmann::json::parse(stream);
}
void MakeThumbnail(const Stack::RawWorkspace::ManagedLayout& layout,
                   const Stack::RawWorkspace::SourceRecord& source) {
    namespace RW = Stack::RawWorkspace;
    auto info = RW::BuildThumbnailInfo(layout, source);
    std::filesystem::create_directories(info.absolutePath.parent_path());
    const unsigned char pixels[] = {20, 30, 40, 255};
    std::string png;
    const auto append = [](void* context, void* data, int length) {
        static_cast<std::string*>(context)->append(static_cast<char*>(data), static_cast<size_t>(length));
    };
    Check(stbi_write_png_to_func(append, &png, 1, 1, 4, pixels, 4) != 0,
        "Could not write fixture preview");
    Write(info.absolutePath, png);
    const auto signature = RW::BuildThumbnailSignature(source);
    Write(info.signaturePath, nlohmann::json({
        {"schemaVersion", signature.schemaVersion},
        {"sourceRelativePath", signature.sourceRelativePath},
        {"sourceFileSizeBytes", signature.sourceFileSizeBytes},
        {"sourceModifiedTimeTicks", signature.sourceModifiedTimeTicks},
        {"sourceFingerprint", signature.sourceFingerprint},
        {"rawLoaderAlgorithmVersion", signature.rawLoaderAlgorithmVersion},
        {"neutralPreviewSettingsVersion", signature.neutralPreviewSettingsVersion},
        {"thumbnailVersion", signature.thumbnailVersion},
        {"maxDimension", signature.maxDimension},
        {"thumbnailWidth", 1}, {"thumbnailHeight", 1}
    }).dump());
}
}

static void RunClosetWorkspaceStorage() {
    namespace RW = Stack::RawWorkspace;
    namespace Project = Stack::Project;
    const auto sandbox = std::filesystem::temp_directory_path() /
        ("stack-closet-" + Project::GenerateStableUuid());
    std::filesystem::create_directories(sandbox);
    const auto root = sandbox / L"Photo collection \u65c5";
    Write(root / L"first \u65c5.dng", "first original bytes");
    Write(root / "Subfolder" / "Closet" / "second.dng", "second original bytes");
    int progressivelyPublished = 0;
    std::uint64_t verifiedBytes = 0;
    auto scan = RW::ScanWorkspace(
        root,
        {},
        {},
        {},
        [&](const RW::ManagedLayout& callbackLayout,
            RW::SourceRecord& source,
            RW::ScanProgress& progress) {
            Check(!source.fingerprint.empty(),
                "Progressive publication requires completed source identity");
            Check(source.absolutePath.parent_path() !=
                    callbackLayout.dataDirectory,
                "Progressive publication must exclude managed data");
            ++progressivelyPublished;
            verifiedBytes = progress.verifiedRawBytes;
        });
    if (!scan.success) throw std::runtime_error("Workspace scan: " + scan.errorMessage);
    Check(scan.success && scan.sources.size() == 2,
        "Closet scan must retain ordinary nested folders called Closet");
    const auto nowUnixSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    Check(std::all_of(scan.sources.begin(), scan.sources.end(),
            [nowUnixSeconds](const RW::SourceRecord& source) {
                return source.modifiedUnixSeconds > nowUnixSeconds - 60 &&
                    source.modifiedUnixSeconds <= nowUnixSeconds + 60;
            }),
        "Scanned source file dates must use the Unix clock for display");
    Check(progressivelyPublished == 2 &&
            verifiedBytes == scan.progress.verifiedRawBytes &&
            verifiedBytes > 0,
        "Scan must publish each verified source with live byte progress");
    Check(std::filesystem::is_regular_file(scan.layout.manifestPath),
        "Closet must have a manifest");
    for (auto& source : scan.sources) MakeThumbnail(scan.layout, source);
    Check(RW::ClassifyThumbnails(scan.layout, scan.sources), "Classify fixture thumbnails");
    RW::SourceRecord lazyValidationProbe = scan.sources.front();
    Write(lazyValidationProbe.thumbnail.absolutePath, "not a png");
    Check(RW::ClassifyThumbnailMetadata(
              scan.layout, lazyValidationProbe) == RW::ThumbnailStatus::Valid,
        "Folder loading should trust matching thumbnail metadata without decoding every PNG");
    Check(RW::ClassifyThumbnail(
              scan.layout, lazyValidationProbe) == RW::ThumbnailStatus::Stale,
        "Deferred strict thumbnail validation should still detect decode failure");
    MakeThumbnail(scan.layout, scan.sources.front());
    Check(RW::ClassifyThumbnail(scan.layout, scan.sources.front()) ==
            RW::ThumbnailStatus::Valid,
        "Restore lazy-validation fixture thumbnail");
    RW::RawGalleryManualGrouping grouping;
    grouping.stacks = {{scan.sources[0].relativePathKey, scan.sources[1].relativePathKey}};
    grouping.manualSourceOrder = {scan.sources[1].relativePathKey, scan.sources[0].relativePathKey};
    grouping.sortMode = RW::RawGalleryFilmstripSortMode::Manual;
    std::string error;
    Check(RW::SaveGalleryState(scan.layout, grouping, &error), "Save folder-local gallery");
    Write(scan.layout.ratingsPath, R"({"schema":"stack.rawWorkspace.ratings","schemaVersion":1,"ratings":{"first.dng":5}})");

    // Independently owned assets, including a project with several inputs.
    for (int count = 1; count <= 2; ++count) {
        Project::RawProjectSnapshot bootstrap;
        bootstrap.projectId = "closet-project-" + std::to_string(count);
        bootstrap.projectName = bootstrap.projectId;
        bootstrap.projectKindHint = StackBinaryFormat::kRawProjectKind;
        auto store = Project::CreateProjectStore(
            scan.layout.projectsDirectory / "Collection" / bootstrap.projectName,
            Project::ProjectStorageKind::DirectoryBundle, bootstrap);
        Check(bool(store), "Create nested project bundle");
        const auto transaction = store.store->BeginTransaction(store.snapshot.persistedStorageRevision);
        auto snapshot = store.snapshot;
        for (int index = 0; index < count; ++index) {
            Project::EmbeddedAssetRecord asset;
            Check(store.store->StageAssetFile(transaction, scan.sources[index].absolutePath,
                Project::MultiFrameInputFamily::Raw, nlohmann::json::object(), asset, &error),
                "Copy original into project");
            Check(asset.workspaceRelativeSourcePath == scan.sources[index].relativePathKey,
                "Managed asset must retain relative workspace provenance");
            snapshot.embeddedAssets.push_back(asset);
        }
        Check(bool(store.store->Commit(transaction, snapshot)), "Commit project-owned RAW copies");
    }
    Check(RW::DiscoverProjects(scan.layout, scan.sources), "Discover nested project bundles");
    std::vector<RW::SourceSetProjectCatalogEntry> projects;
    Check(RW::DiscoverSourceSetProjects(scan.layout, scan.sources, projects) && projects.size() == 2,
        "Projects must be discovered recursively");
    scan.sources.front().captureTimestamp = 1760000000;
    scan.sources.front().captureMetadataChecked = true;
    Check(RW::WriteCatalogSkeleton(scan.layout, scan.sources, "", &error), "Save relative catalog");
    auto catalog = Read(scan.layout.catalogPath);
    Check(!catalog.contains("workspaceRoot"), "Catalog must not own an absolute root");
    for (const auto& source : catalog["sources"]) {
        Check(!source.contains("absolutePath"), "Catalog sources must be relative");
        Check(!source.contains("fileName") && !source.contains("stem") &&
                !source.contains("extension") &&
                !source.contains("parentFolder"),
            "Catalog should derive redundant path fields at load time");
        Check(!source["thumbnail"].contains("relativePath") &&
                !source["thumbnail"].contains("signatureRelativePath"),
            "Catalog should derive deterministic thumbnail paths");
        for (const auto& membership : source["sourceSetProjectMemberships"])
            Check(!std::filesystem::path(membership["projectPath"].get<std::string>()).is_absolute(),
                "Cached project memberships must be relative");
    }
    std::vector<RW::SourceRecord> warmSources;
    Check(RW::LoadCatalogSnapshot(scan.layout, warmSources),
        "Load repeat-open identity cache");
    Check(warmSources.front().captureTimestamp == 1760000000 &&
            warmSources.front().captureMetadataChecked &&
            warmSources.front().modifiedUnixSeconds > 0,
        "Warm catalog lost capture or file date metadata");
    Check(std::all_of(
              warmSources.begin(),
              warmSources.end(),
              [](const RW::SourceRecord& source) {
                  return source.thumbnail.status ==
                          RW::ThumbnailStatus::Valid &&
                      !source.thumbnail.absolutePath.empty();
              }),
        "Compact warm catalog must preserve immediately displayable thumbnails");
    std::unordered_map<std::string, RW::SourceRecord> warmByKey;
    for (const auto& source : warmSources) {
        warmByKey.emplace(source.relativePathKey, source);
    }
    auto repeatOpen = RW::ScanWorkspace(
        root,
        {},
        {},
        {},
        {},
        [&](RW::SourceRecord& source) {
            const auto cached = warmByKey.find(source.relativePathKey);
            if (cached == warmByKey.end() ||
                cached->second.fileSizeBytes != source.fileSizeBytes ||
                cached->second.modifiedTimeTicks != source.modifiedTimeTicks ||
                cached->second.sourceIdentityAlgorithmVersion !=
                    RW::kSourceIdentityAlgorithmVersion) {
                return false;
            }
            source.fingerprint = cached->second.fingerprint;
            source.sourceIdentityAlgorithmVersion =
                cached->second.sourceIdentityAlgorithmVersion;
            source.captureTimestamp = cached->second.captureTimestamp;
            source.captureMetadataChecked = cached->second.captureMetadataChecked;
            return true;
        });
    Check(repeatOpen.success && repeatOpen.progress.hashedRawCount == 0 &&
            repeatOpen.progress.reusedSourceIdentityCount == 2 &&
            repeatOpen.progress.verifiedRawBytes == 0,
        "Repeat open must not reread unchanged RAW contents");
    Check(repeatOpen.sources.front().captureTimestamp == 1760000000 &&
            repeatOpen.sources.front().captureMetadataChecked,
        "Repeat scan lost cached capture metadata");
    const auto changedSourcePath = root / L"first \u65c5.dng";
    const auto unchangedModifiedTime =
        std::filesystem::last_write_time(changedSourcePath);
    Write(changedSourcePath, "changed source bytes require verification");
    auto changedOpen = RW::ScanWorkspace(
        root,
        {},
        {},
        {},
        {},
        [&](RW::SourceRecord& source) {
            const auto cached = warmByKey.find(source.relativePathKey);
            if (cached == warmByKey.end() ||
                cached->second.fileSizeBytes != source.fileSizeBytes ||
                cached->second.modifiedTimeTicks != source.modifiedTimeTicks ||
                cached->second.sourceIdentityAlgorithmVersion !=
                    RW::kSourceIdentityAlgorithmVersion) {
                return false;
            }
            source.fingerprint = cached->second.fingerprint;
            source.sourceIdentityAlgorithmVersion =
                cached->second.sourceIdentityAlgorithmVersion;
            return true;
        });
    Check(changedOpen.success && changedOpen.progress.hashedRawCount == 1 &&
            changedOpen.progress.reusedSourceIdentityCount == 1 &&
            changedOpen.progress.verifiedRawBytes ==
                std::filesystem::file_size(changedSourcePath),
        "Changed RAW metadata must trigger full hashing only for that source");
    Write(changedSourcePath, "first original bytes");
    std::filesystem::last_write_time(
        changedSourcePath, unchangedModifiedTime);
    const auto renamed = sandbox / "Renamed photos";
    std::filesystem::rename(root, renamed);
    auto layout = RW::BuildManagedLayout(renamed);
    std::filesystem::rename(layout.projectsDirectory / "Collection" / "closet-project-1",
        layout.projectsDirectory / "Collection" / "Renamed project");
    auto moved = RW::ScanWorkspace(renamed);
    Check(moved.success && moved.sources.size() == 2, "Renamed workspace scan excludes project RAW copies");
    Check(RW::ClassifyThumbnails(moved.layout, moved.sources), "Validate relocated thumbnails");
    for (const auto& source : moved.sources)
        Check(source.thumbnail.status == RW::ThumbnailStatus::Valid, "Rename must preserve previews");
    RW::RawGalleryManualGrouping restored;
    Check(RW::LoadGalleryState(layout, restored, &error) &&
        restored.stacks == grouping.stacks && restored.manualSourceOrder == grouping.manualSourceOrder &&
        restored.sortMode == grouping.sortMode, "Rename must preserve gallery organization");
    Check(Read(layout.ratingsPath)["ratings"]["first.dng"] == 5, "Rename must preserve ratings");
    Check(RW::DiscoverProjects(layout, moved.sources) &&
        RW::DiscoverSourceSetProjects(layout, moved.sources, projects) && projects.size() == 2,
        "Renamed project and workspace must remain discoverable");
    for (const auto& project : projects) {
        const auto opened = Project::OpenProjectStore(project.absolutePath);
        Check(bool(opened) && !opened.snapshot.embeddedAssets.empty(),
            "Renamed project must open from its own assets");
    }
    Check(!moved.sources[0].sourceSetProjectMemberships.empty() &&
        !moved.sources[1].sourceSetProjectMemberships.empty(),
        "Source associations survive both renames");

    const auto copied = sandbox / "Copied photos";
    std::filesystem::copy(renamed, copied, std::filesystem::copy_options::recursive);
    const auto copiedSource = copied / moved.sources[0].relativePath;
    std::filesystem::last_write_time(copiedSource,
        std::filesystem::last_write_time(copiedSource) + std::chrono::seconds(5));
    auto transferred = RW::ScanWorkspace(copied);
    Check(transferred.success && RW::ClassifyThumbnails(transferred.layout, transferred.sources),
        "Copied workspace should load");
    for (const auto& source : transferred.sources)
        Check(source.thumbnail.status == RW::ThumbnailStatus::Valid,
            "Timestamp changes alone must preserve matching content previews");
    const auto originalTime = std::filesystem::last_write_time(copiedSource);
    const auto originalSize = std::filesystem::file_size(copiedSource);
    Write(copiedSource, std::string(static_cast<size_t>(originalSize), 'x'));
    std::filesystem::last_write_time(copiedSource, originalTime);
    transferred = RW::ScanWorkspace(copied);
    Check(RW::ClassifyThumbnails(transferred.layout, transferred.sources), "Classify replaced RAW");
    for (const auto& source : transferred.sources)
        Check(source.thumbnail.status == (source.absolutePath == copiedSource
                ? RW::ThumbnailStatus::Stale : RW::ThumbnailStatus::Valid),
            "Same-name, same-size, same-time replacement must invalidate only its preview");

    const auto conflict = sandbox / "Conflict";
    Write(conflict / "Closet" / "user-file.txt", "keep me");
    Check(!RW::EnsureManagedFolders(conflict, &error) &&
        std::filesystem::exists(conflict / "Closet" / "user-file.txt") &&
        !std::filesystem::exists(conflict / "Closet" / "workspace.json"),
        "Unowned Closet must be preserved and rejected");
    Write(conflict / "Closet" / "workspace.json", R"({"schema":3,"schemaVersion":"wrong"})");
    Check(!RW::EnsureManagedFolders(conflict, &error), "Malformed manifest must fail without throwing");
    const auto blocked = sandbox / "Not a directory";
    Write(blocked, "file");
    Check(!RW::EnsureManagedFolders(blocked, &error), "Unavailable source must fail");
    Check(!RW::EnsureManagedFolders(sandbox / "Missing", &error), "Missing source must not be created");

#if defined(_WIN32)
    const auto readOnly = sandbox / "Read only";
    std::filesystem::create_directories(readOnly);
    DWORD size = 0;
    GetFileSecurityW(readOnly.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &size);
    std::vector<unsigned char> originalSecurity(size);
    Check(GetFileSecurityW(readOnly.c_str(), DACL_SECURITY_INFORMATION,
        originalSecurity.data(), size, &size) != FALSE, "Read fixture security descriptor");
    PSECURITY_DESCRIPTOR denied = nullptr;
    Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:(D;;0x00000006;;;WD)(A;;FA;;;WD)", SDDL_REVISION_1, &denied, nullptr) != FALSE,
        "Create fixture write-denial descriptor");
    const bool secured = SetFileSecurityW(readOnly.c_str(), DACL_SECURITY_INFORMATION, denied) != FALSE;
    LocalFree(denied);
    Check(secured, "Apply fixture write-denial descriptor");
    const bool rejected = !RW::EnsureManagedFolders(readOnly, &error);
    const bool restoredAcl = SetFileSecurityW(readOnly.c_str(), DACL_SECURITY_INFORMATION,
        originalSecurity.data()) != FALSE;
    Check(restoredAcl, "Restore fixture permissions");
    Check(rejected && error.find("writable") != std::string::npos &&
        std::filesystem::is_empty(readOnly), "Read-only source must fail without leaving a partial Closet");
#endif

    // This unique temporary test root contains only fixtures made above.
    std::filesystem::remove_all(sandbox);
}

void TestPersistenceBinaryFormat();

void TestClosetWorkspaceStorage() {
    try {
        TestPersistenceBinaryFormat();
        RunClosetWorkspaceStorage();
    } catch (const std::exception& error) {
        std::cerr << "Closet storage validation failed: " << error.what() << std::endl;
        std::exit(1);
    }
}
