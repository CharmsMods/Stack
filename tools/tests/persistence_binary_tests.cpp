#include "Persistence/StackBinaryFormat.h"
#include "Raw/RawTechnicalEvidence.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <future>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<unsigned char>;
namespace Format = StackBinaryFormat;
namespace Project = Stack::Project;

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename T>
void Append(Bytes& bytes, T value) {
    const auto* first = reinterpret_cast<const unsigned char*>(&value);
    bytes.insert(bytes.end(), first, first + sizeof(value));
}

void AppendString(Bytes& bytes, const std::string& text) {
    Append(bytes, static_cast<std::uint32_t>(text.size()));
    bytes.insert(bytes.end(), text.begin(), text.end());
}

Bytes PresetMetadata(bool validId = true) {
    Bytes bytes { 9u }; // Object with the required preset ID.
    Append(bytes, std::uint32_t { 1u });
    AppendString(bytes, "id");
    Append(bytes, static_cast<std::uint8_t>(validId ? 6u : 4u));
    if (validId) AppendString(bytes, "fixture-preset");
    else Append(bytes, std::uint64_t { 17u });
    return bytes;
}

std::uint32_t Crc32(const Bytes& bytes) {
    std::uint32_t crc = 0xffffffffu;
    for (unsigned char byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xedb88320u : 0u);
        }
    }
    return ~crc;
}

void WriteBytes(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    stream.close();
    Check(stream.good(), "Could not write binary format fixture");
}

void WriteWithChecksum(const std::filesystem::path& path, Bytes bytes) {
    const auto checksum = Crc32(bytes);
    for (char byte : std::string("CKSM")) bytes.push_back(byte);
    Append(bytes, checksum);
    WriteBytes(path, bytes);
}

struct Section {
    std::string id;
    Bytes bytes;
    std::uint8_t compression = 0;
    std::uint64_t uncompressedSize = 0;
};

Bytes Container(Format::FileKind kind, const std::vector<Section>& sections) {
    Bytes bytes { 'M', 'S', 'T', 'K' };
    Append(bytes, std::uint16_t { 2u });
    Append(bytes, static_cast<std::uint16_t>(kind));
    Append(bytes, static_cast<std::uint32_t>(sections.size()));
    std::uint64_t offset = 12u + sections.size() * 29u;
    for (const auto& section : sections) {
        bytes.insert(bytes.end(), section.id.begin(), section.id.end());
        Append(bytes, offset);
        Append(bytes, static_cast<std::uint64_t>(section.bytes.size()));
        Append(bytes, section.compression ? section.uncompressedSize
            : static_cast<std::uint64_t>(section.bytes.size()));
        Append(bytes, section.compression);
        offset += section.bytes.size();
    }
    for (const auto& section : sections) {
        bytes.insert(bytes.end(), section.bytes.begin(), section.bytes.end());
    }
    return bytes;
}

template <typename T>
void Patch(Bytes& bytes, std::size_t offset, T value) {
    Check(offset + sizeof(value) <= bytes.size(), "Invalid fixture patch");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void TestPresetInputValidation(const std::filesystem::path& sandbox) {
    const auto path = sandbox / "fixture.stackpreset";
    Format::NodePresetDocument original;
    original.metadata.id = "valid-preset";
    original.graphPayload = {{ "repeated", std::string(32768u, 'x') }};
    original.thumbnailBytes = { 1u, 2u, 3u };
    original.boundarySockets.push_back({ "Input", "Image", "input", "image" });
    Check(Format::WriteNodePresetFile(path, original), "Write compressed preset fixture");
    Format::NodePresetDocument loaded;
    Check(Format::ReadNodePresetFile(path, loaded) &&
            loaded.graphPayload == original.graphPayload &&
            loaded.thumbnailBytes == original.thumbnailBytes &&
            loaded.boundarySockets.size() == 1u,
        "Compressed preset must round trip");

    Format::NodePresetDocument tooDeep;
    tooDeep.metadata.id = "too-deep";
    for (std::size_t level = 0; level < 160u; ++level) {
        auto parent = Format::json::array();
        parent.push_back(std::move(tooDeep.graphPayload));
        tooDeep.graphPayload = std::move(parent);
    }
    Check(!Format::WriteNodePresetFile(path, tooDeep),
        "Writer must reject nesting that the reader cannot reopen");
    Check(Format::ReadNodePresetFile(path, loaded) && loaded.graphPayload == original.graphPayload,
        "Rejected preset writes must preserve the previous file");

    std::ifstream stream(path, std::ios::binary);
    Bytes compressed { std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>() };
    stream.close();
    constexpr std::size_t graphEntry = 12u + 2u * 29u;
    Check(compressed[graphEntry + 28u] == 1u, "Fixture graph must use deflate");
    compressed.resize(compressed.size() - 8u);
    Patch(compressed, graphEntry + 20u, std::uint64_t { 1u });
    WriteWithChecksum(path, compressed);
    Check(!Format::ReadNodePresetFile(path, loaded),
        "Compressed payload exceeding its declared output size must fail");
    Check(loaded.metadata.id == original.metadata.id &&
            loaded.graphPayload == original.graphPayload,
        "Failed preset reads must preserve the caller's document");

    const auto reject = [&](const Bytes& bytes, const char* message) {
        WriteWithChecksum(path, bytes);
        Check(!Format::ReadNodePresetFile(path, loaded), message);
    };
    const auto preset = Format::FileKind::NodePreset;
    reject(Container(preset, {{ "META", PresetMetadata(false) }}),
        "Wrong metadata types must fail without throwing");
    reject(Container(preset, {{ "META", PresetMetadata() },
        { "PIPE", { 0u, 0u } }}), "Trailing binary JSON bytes must fail");
    reject(Container(preset, {{ "META", PresetMetadata() },
        { "PIPE", { 1u, 2u, 3u }, 1u, 30u }}),
        "Invalid compressed graph must not become an empty successful preset");
    reject(Container(preset, {{ "META", PresetMetadata() },
        { "THMB", { 1u }, 1u, 30u }}), "Invalid thumbnail payload must fail");
    reject(Container(preset, {{ "META", PresetMetadata() },
        { "BNDY", { 0u } }}), "Boundary sockets must be an array");
    reject(Container(preset, {{ "META", PresetMetadata() },
        { "PIPE", {}, 9u, 0u }}), "Unknown compression must fail");
    reject(Container(preset, {{ "META", PresetMetadata() },
        { "META", PresetMetadata() }}), "Duplicate section IDs must fail");

    Bytes deepJson;
    for (std::size_t level = 0; level < 2048u; ++level) {
        Append(deepJson, std::uint8_t { 8u });
        Append(deepJson, std::uint32_t { 1u });
    }
    deepJson.push_back(0u);
    reject(Container(preset, {{ "META", PresetMetadata() },
        { "PIPE", std::move(deepJson) }}), "Deeply nested JSON must fail without overflowing the stack");

    Bytes overflow = Container(preset, {{ "META", PresetMetadata() }});
    Patch(overflow, 16u, std::numeric_limits<std::uint64_t>::max() - 4u);
    reject(overflow, "Wrapped section ranges must fail");
    overflow = Container(preset, {{ "META", PresetMetadata() }});
    Patch(overflow, 16u, std::uint64_t { 0u });
    reject(overflow, "Sections must not overlap the header");
    overflow = Container(preset, {{ "META", PresetMetadata() }});
    Patch(overflow, 8u, std::numeric_limits<std::uint32_t>::max());
    reject(overflow, "Section counts larger than the table must fail");
}

void TestLibraryInputValidation(const std::filesystem::path& sandbox) {
    const auto path = sandbox / "fixture.stacklibrary";
    Format::LibraryBundleDocument original;
    original.bundleName = "Fixture Library";
    original.projects.push_back({ "example", {} });
    Format::AssetDocument asset;
    asset.fileName = "example.png";
    original.assets.push_back(std::move(asset));
    Check(Format::WriteLibraryBundle(path, original), "Write library fixture");
    Format::LibraryBundleDocument loaded;
    Check(Format::ReadLibraryBundle(path, loaded) &&
            loaded.bundleName == original.bundleName && loaded.projects.size() == 1u &&
            loaded.assets.size() == 1u, "Library bundle must round trip");
    Bytes emptyObject { 9u };
    Append(emptyObject, std::uint32_t { 0u });
    WriteWithChecksum(path, Container(Format::FileKind::LibraryBundle,
        {{ "META", emptyObject }, { "PROJ", { 1u }, 1u, 50u }}));
    Check(!Format::ReadLibraryBundle(path, loaded) && loaded.projects.size() == 1u &&
            loaded.bundleName == original.bundleName,
        "Corrupt library projects must fail and preserve the caller's document");
    WriteWithChecksum(path, Container(Format::FileKind::LibraryBundle,
        {{ "META", emptyObject }}));
    Check(Format::ReadLibraryBundle(path, loaded) && loaded.projects.empty() &&
            loaded.assets.empty(), "Absent optional sections must not retain a previous library's contents");
}

void TestUnicodeProjectPaths(const std::filesystem::path& sandbox) {
    const auto path = sandbox / L"Project \u65c5.stack";
    Format::ProjectDocument document;
    document.metadata.projectName.clear();
    document.metadata.sourceWidth = 1;
    document.metadata.sourceHeight = 1;
    document.adoptedFrom = sandbox / L"Original \u65c5.stack";
    const auto originalImage = sandbox / L"Image \u65c5.png";
    document.pipelineData = {{ "nodeGraph", {{ "nodes", Format::json::array({{
        { "kind", "Image" }, { "id", 1 }, { "instanceUuid", "fixture-node" },
        { "sourcePath", originalImage.u8string() },
        { "pngBytes", Format::json::binary(Bytes { 1u, 2u, 3u }) }
    }}) }} }};
    Check(Format::WriteProjectFile(path, document), "Save a project in a Unicode folder");
    const auto root = path.parent_path() / path.stem();
    Format::ProjectDocument loaded;
    Check(Format::ReadProjectFile(root, loaded), "Open a project from a Unicode folder");
    Check(loaded.metadata.projectName == path.stem().u8string() &&
            loaded.adoptedFrom == document.adoptedFrom,
        "Project names and adoption paths must retain Unicode characters");
    Check(loaded.rawProjectSnapshot && loaded.rawProjectSnapshot->embeddedAssets.size() == 1u,
        "Unicode project fixture must have a managed image");
    const auto& asset = loaded.rawProjectSnapshot->embeddedAssets.front();
    Check(asset.originalFilename == originalImage.filename().u8string(),
        "Managed image filenames must retain Unicode characters");
    loaded.rawWorkspaceData = {{ "managedAssetId", asset.assetId },
        { "rawRecipe", {{ "sourceRef", {{ "sourcePath", "old-path" }} }} }};
    Check(Format::WriteProjectFile(root, loaded), "Resave a loaded Unicode project");
    Format::ProjectDocument rewritten;
    Check(Format::ReadProjectFile(root, rewritten), "Read rewritten managed source path");
    const auto expected = (root / std::filesystem::u8path(asset.projectAssetPath)).lexically_normal();
    Check(rewritten.rawWorkspaceData["rawRecipe"]["sourceRef"]["sourcePath"] == expected.u8string() &&
            rewritten.adoptedFrom == document.adoptedFrom,
        "Managed raw paths and resaved adoption paths must retain Unicode characters");
}

void TestMalformedManifestRecovery(const std::filesystem::path& sandbox) {
    const auto root = sandbox / "recoverable-project";
    Project::RawProjectSnapshot snapshot;
    snapshot.projectId = Project::GenerateStableUuid();
    snapshot.projectName = "Previous generation";
    auto created = Project::CreateProjectStore(root, Project::ProjectStorageKind::DirectoryBundle, snapshot);
    Check(static_cast<bool>(created), "Create recovery fixture");
    auto transaction = created.store->BeginTransaction(created.snapshot.persistedStorageRevision);
    snapshot = created.snapshot;
    snapshot.projectName = "Current generation";
    Check(static_cast<bool>(created.store->Commit(transaction, snapshot)), "Commit recovery fixture");
    const auto manifestPath = Project::WorkingProjectDocumentPath(root);
    Format::json manifest;
    {
        std::ifstream stream(manifestPath);
        stream >> manifest;
    }
    manifest["_store"]["kind"] = 17u;
    const auto text = manifest.dump();
    WriteBytes(manifestPath, Bytes(text.begin(), text.end()));
    const auto recovered = Project::OpenProjectStore(root);
    Check(static_cast<bool>(recovered) && recovered.store->IsReadOnlyRecovery() &&
            recovered.snapshot.projectName == "Previous generation",
        "Wrong storage field types must recover the previous committed manifest");
}

void TestCreateOnlyProjectSave(const std::filesystem::path& sandbox) {
    const auto root = sandbox / "create-only-project";
    Format::ProjectDocument document;
    document.metadata.projectName = "Original project";
    document.metadata.sourceWidth = 1;
    document.metadata.sourceHeight = 1;
    Check(Format::WriteProjectFile(root, document, true),
        "Create-only save must create a missing backing store");
    const auto original = Project::OpenProjectStore(root);
    Check(static_cast<bool>(original), "Create-only project must be readable");
    const Bytes sentinel { 11u, 22u, 33u };
    const auto sentinelPath = root / "keep.bin";
    WriteBytes(sentinelPath, sentinel);
    document.metadata.projectName = "Replacement project";
    Check(!Format::WriteProjectFile(root, document, true),
        "Create-only save must refuse an existing project");
    auto unchanged = Project::OpenProjectStore(root);
    Check(static_cast<bool>(unchanged) &&
            unchanged.snapshot.projectName == original.snapshot.projectName &&
            unchanged.snapshot.projectId == original.snapshot.projectId &&
            unchanged.snapshot.persistedStorageRevision == original.snapshot.persistedStorageRevision,
        "Rejected create-only save must preserve the original project and revision");
    {
        std::ifstream stream(sentinelPath, std::ios::binary);
        const Bytes retained { std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>() };
        Check(retained == sentinel, "Rejected create-only save must preserve unrelated data");
    }
    Format::ProjectDocument loaded;
    Check(Format::ReadProjectFile(root, loaded), "Read bound project fixture");
    Check(!Format::WriteProjectFile(sandbox / "bound-copy", loaded, true) &&
            !std::filesystem::exists(sandbox / "bound-copy"),
        "Create-only save must reject bound stores without committing or copying them");

    const auto competingRoot = sandbox / "competing-project";
    std::promise<void> release;
    const std::shared_future<void> ready = release.get_future().share();
    std::vector<std::future<Project::ProjectStoreOpenResult>> attempts;
    attempts.reserve(4u);
    try {
        for (int index = 0; index < 4; ++index) {
            attempts.push_back(std::async(std::launch::async, [ready, competingRoot, index]() {
                Project::RawProjectSnapshot snapshot;
                snapshot.projectId = Project::GenerateStableUuid();
                snapshot.projectName = "Creator " + std::to_string(index);
                ready.wait();
                return Project::CreateProjectStore(
                    competingRoot, Project::ProjectStorageKind::DirectoryBundle, snapshot);
            }));
        }
    } catch (...) {
        release.set_value();
        throw;
    }
    release.set_value();
    std::size_t successes = 0;
    std::string winningId;
    for (auto& attempt : attempts) {
        auto result = attempt.get();
        if (result) {
            ++successes;
            winningId = result.snapshot.projectId;
        }
    }
    const auto winner = Project::OpenProjectStore(competingRoot);
    Check(successes == 1u && static_cast<bool>(winner) && winner.snapshot.projectId == winningId,
        "Competing creators must leave exactly one readable project without sharing its root");
}

void TestConcurrentProjectStoreHandles(const std::filesystem::path& sandbox) {
    for (auto kind : { Project::ProjectStorageKind::DirectoryBundle,
                       Project::ProjectStorageKind::PortableFile }) {
        const auto root = sandbox / (kind == Project::ProjectStorageKind::DirectoryBundle
            ? "concurrent-bundle" : "concurrent-portable.stack");
        Project::RawProjectSnapshot initial;
        initial.projectId = Project::GenerateStableUuid();
        initial.projectName = "Initial";
        auto first = Project::CreateProjectStore(root, kind, initial);
        auto second = Project::OpenProjectStore(root.parent_path() / "." / root.filename());
        Check(first && second, "Open two handles to the same project path");
        std::promise<void> release;
        auto ready = release.get_future().share();
        const auto start = [&](Project::ProjectStoreOpenResult opened, const char* name) {
            opened.snapshot.projectName = name;
            const auto transaction = opened.store->BeginTransaction(opened.snapshot.persistedStorageRevision);
            Check(static_cast<bool>(transaction), "Begin concurrent project transaction");
            return std::async(std::launch::async, [opened, transaction, ready]() {
                ready.wait();
                auto result = opened.store->Commit(transaction, opened.snapshot);
                if (!result) opened.store->Abort(transaction);
                return result;
            });
        };
        auto writeA = start(first, "Writer A");
        std::future<Project::ProjectStoreCommitResult> writeB;
        try { writeB = start(second, "Writer B"); }
        catch (...) { release.set_value(); throw; }
        release.set_value();
        const auto resultA = writeA.get();
        const auto resultB = writeB.get();
        Check(static_cast<bool>(resultA) != static_cast<bool>(resultB) &&
            (resultA ? resultB.status : resultA.status) == Project::ProjectStoreCommitStatus::Conflict,
            "Exactly one same-baseline writer must commit; the other must report a conflict");
        const auto reopened = Project::OpenProjectStore(root);
        Check(reopened && reopened.snapshot.projectName == (resultA ? "Writer A" : "Writer B") &&
            reopened.snapshot.persistedStorageRevision == first.snapshot.persistedStorageRevision + 1,
            "Concurrent commits must retain the winning snapshot and advance once");
    }
}

void TestProjectAssetCollision(const std::filesystem::path& sandbox) {
    const auto root = sandbox / "asset-collision";
    Project::RawProjectSnapshot initial;
    initial.projectId = Project::GenerateStableUuid();
    initial.projectName = "Collision check";
    auto created = Project::CreateProjectStore(root, Project::ProjectStorageKind::DirectoryBundle, initial);
    Check(static_cast<bool>(created), "Create asset collision project");
    const auto assetPath = root / "assets" / "collision.bin";
    const Bytes original { 1u, 2u, 3u };
    const Bytes incoming { 4u, 5u, 6u };
    WriteBytes(assetPath, original);
    const auto identity = Stack::RawEvidence::ComputeSourceIdentity(incoming);
    Project::EmbeddedAssetRecord record;
    record.assetId = Project::MakeAssetId(identity.sha256, identity.byteSize);
    record.sha256 = identity.sha256;
    record.byteLength = identity.byteSize;
    record.projectAssetPath = "assets/collision.bin";
    record.displayName = "Collision asset";
    record.originalFilename = "collision.bin";
    record.originalFileFingerprint = identity.sha256;
    record.inputFamily = Project::MultiFrameInputFamily::Raster;
    record.captureMetadataSummary = Format::json::object();
    const auto transaction = created.store->BeginTransaction(created.snapshot.persistedStorageRevision);
    std::istringstream stream(std::string(incoming.begin(), incoming.end()), std::ios::in | std::ios::binary);
    Check(created.store->StageAssetStream(transaction, stream, record), "Stage colliding asset bytes");
    auto snapshot = created.snapshot;
    snapshot.embeddedAssets.push_back(record);
    const auto committed = created.store->Commit(transaction, snapshot);
    Check(!committed && committed.status == Project::ProjectStoreCommitStatus::IoFailure,
        "A different asset at the claimed path must prevent publication");
    created.store->Abort(transaction);
    std::ifstream retainedStream(assetPath, std::ios::binary);
    const Bytes retained { std::istreambuf_iterator<char>(retainedStream), std::istreambuf_iterator<char>() };
    const auto reopened = Project::OpenProjectStore(root);
    Check(retained == original && reopened && reopened.snapshot.embeddedAssets.empty() &&
        reopened.snapshot.persistedStorageRevision == created.snapshot.persistedStorageRevision,
        "An asset collision must preserve existing bytes and the previous manifest");
}

void TestPortableCreateExclusivity(const std::filesystem::path& sandbox) {
    const auto path = sandbox / "competing-portable.stack";
    std::promise<void> release;
    const auto ready = release.get_future().share();
    const auto create = [path, ready](const char* name) {
        Project::RawProjectSnapshot snapshot;
        snapshot.projectId = Project::GenerateStableUuid();
        snapshot.projectName = name;
        ready.wait();
        return Project::CreateProjectStore(path, Project::ProjectStorageKind::PortableFile, snapshot);
    };
    auto first = std::async(std::launch::async, create, "Creator A");
    std::future<Project::ProjectStoreOpenResult> second;
    try { second = std::async(std::launch::async, create, "Creator B"); }
    catch (...) { release.set_value(); throw; }
    release.set_value();
    const auto a = first.get();
    const auto b = second.get();
    const auto reopened = Project::OpenProjectStore(path);
    Check(static_cast<bool>(a) != static_cast<bool>(b) && reopened &&
        reopened.snapshot.projectId == (a ? a.snapshot.projectId : b.snapshot.projectId),
        "Concurrent portable creators must retain exactly one intact project");
}

void TestCapturedProjectWriteBaseline(const std::filesystem::path& sandbox) {
    const auto path = sandbox / "captured-save";
    Format::ProjectDocument document;
    document.projectId = Project::GenerateStableUuid();
    document.metadata.projectName = "Captured project";
    document.metadata.sourceWidth = 1;
    document.metadata.sourceHeight = 1;
    document.pipelineData = {{ "layers", Format::json::array() }};
    const auto saved = Format::WriteProjectFileWithResult(path, document, false, 0);
    if (!saved) throw std::runtime_error("Captured project save failed: " + saved.commit.message);
    Check(saved && saved.snapshot.projectId == document.projectId &&
        saved.snapshot.persistedStorageRevision == saved.commit.committedStorageRevision,
        "A captured save must return its exact store and committed baseline");
    auto other = Project::OpenProjectStore(path);
    auto transaction = other.store->BeginTransaction(other.snapshot.persistedStorageRevision);
    other.snapshot.projectName = "Outside edit";
    Check(static_cast<bool>(other.store->Commit(transaction, other.snapshot)), "Commit an outside edit");
    const auto stale = Format::WriteProjectFileWithResult(path, document, false,
        saved.snapshot.persistedStorageRevision);
    const auto reopened = Project::OpenProjectStore(path);
    Check(!stale && stale.commit.status == Project::ProjectStoreCommitStatus::Conflict && reopened &&
        reopened.snapshot.projectName == "Outside edit",
        "A stale captured baseline must preserve the outside edit");
}

void TestProjectStoreOwnershipCases(const std::filesystem::path& sandbox) {
    TestMalformedManifestRecovery(sandbox);
    TestCreateOnlyProjectSave(sandbox);
    TestConcurrentProjectStoreHandles(sandbox);
    TestProjectAssetCollision(sandbox);
    TestPortableCreateExclusivity(sandbox);
    TestCapturedProjectWriteBaseline(sandbox);
}

} // namespace

void TestProjectStoreOwnership() {
    const auto sandbox = std::filesystem::temp_directory_path() /
        ("stack-project-ownership-" + Project::GenerateStableUuid());
    std::filesystem::create_directories(sandbox);
    TestProjectStoreOwnershipCases(sandbox);
    std::filesystem::remove_all(sandbox);
}

void TestPersistenceBinaryFormat() {
    const auto sandbox = std::filesystem::temp_directory_path() /
        ("stack-persistence-" + Project::GenerateStableUuid());
    std::filesystem::create_directories(sandbox);
    TestPresetInputValidation(sandbox);
    TestLibraryInputValidation(sandbox);
    TestUnicodeProjectPaths(sandbox);
    TestProjectStoreOwnershipCases(sandbox);
    // This unique temporary directory contains only the fixtures above.
    std::filesystem::remove_all(sandbox);
}
