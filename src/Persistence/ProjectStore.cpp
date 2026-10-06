#include "Persistence/ProjectStore.h"
#include "Persistence/ProjectCatalogChanges.h"

#include "Persistence/RawProjectEditPipeline.h"
#include "Raw/RawTechnicalEvidence.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Stack::Project {
namespace {

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

constexpr const char* kWorkingProjectManifestName = "project.stack";
constexpr const char* kWorkingProjectPreviousManifestName = "project.stack.previous";
constexpr const char* kWorkingProjectAssetsDirectoryName = "assets";
constexpr const char* kWorkingProjectPreviewName = "preview.png";
constexpr const char* kBundleStagingDirectoryName = ".staging";
constexpr std::array<char, 8> kPortableHeader = { 'S', 'T', 'K', 'R', 'A', 'W', '3', '\0' };
constexpr std::array<char, 4> kRecordMagic = { 'S', 'R', '3', 'R' };
constexpr std::array<char, 4> kFooterMagic = { 'S', 'R', '3', 'F' };
constexpr std::uint32_t kAssetRecordType = 1;
constexpr std::uint32_t kManifestRecordType = 2;
constexpr std::uint32_t kCoverRecordType = 3;
constexpr std::uint64_t kRecordFixedHeaderSize = 4u + 4u + 8u + 4u + 64u;
constexpr std::uint64_t kFooterSize = 4u + 8u + 8u;
constexpr std::uint64_t kMaximumManifestBytes = 256u * 1024u * 1024u;

struct AssetLocation {
    std::uint64_t payloadOffset = 0;
    std::uint64_t payloadLength = 0;
    std::string checksum;
    std::filesystem::path relativePath;
};

struct StagedAsset {
    EmbeddedAssetRecord record;
    std::filesystem::path stagingPath;
};

struct TransactionState {
    std::uint64_t expectedStorageRevision = 0;
    std::filesystem::path stagingDirectory;
    std::unordered_map<std::string, StagedAsset> assets;
};

std::string FileNameForAsset(const EmbeddedAssetRecord& asset) {
    return asset.sha256 + "-" + std::to_string(asset.byteLength) + ".original";
}

std::string SanitizeManagedFileName(std::string name) {
    if (name.empty()) name = "asset";
    for (char& character : name) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (value < 32u || character == '<' || character == '>' ||
            character == ':' || character == '"' || character == '/' ||
            character == '\\' || character == '|' || character == '?' ||
            character == '*') {
            character = '_';
        }
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) {
        name.pop_back();
    }
    return name.empty() ? std::string("asset") : name;
}

bool IsSafeManagedAssetPath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) return false;
    auto component = path.begin();
    if (component == path.end() ||
        LowerAscii(component->string()) != kWorkingProjectAssetsDirectoryName) {
        return false;
    }
    for (; component != path.end(); ++component) {
        if (*component == "." || *component == "..") return false;
    }
    return true;
}

std::filesystem::path NormalizeDirectoryStorePath(
    const std::filesystem::path& requestedPath) {
    if (LowerAscii(requestedPath.filename().u8string()) ==
        kWorkingProjectManifestName) {
        return requestedPath.parent_path().lexically_normal();
    }
    return requestedPath.lexically_normal();
}

struct StorePathLess {
    bool operator()(const std::filesystem::path& a, const std::filesystem::path& b) const {
#if defined(_WIN32)
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
#else
        return a < b;
#endif
    }
};

std::shared_ptr<std::mutex> CommitMutexForPath(const std::filesystem::path& path) {
    std::error_code error;
    auto key = std::filesystem::weakly_canonical(path, error);
    if (error) {
        error.clear();
        key = std::filesystem::absolute(path, error);
        if (error) key = path;
    }
    key = key.lexically_normal();
    static std::mutex registryMutex;
    static std::map<std::filesystem::path, std::weak_ptr<std::mutex>, StorePathLess> registry;
    std::lock_guard<std::mutex> lock(registryMutex);
    for (auto entry = registry.begin(); entry != registry.end();) {
        if (entry->second.expired()) entry = registry.erase(entry);
        else ++entry;
    }
    auto& existing = registry[key];
    auto mutex = existing.lock();
    if (!mutex) {
        mutex = std::make_shared<std::mutex>();
        existing = mutex;
    }
    return mutex;
}

std::string FileNameForStagedAsset() {
    // Transaction directories already isolate staged files. A generated short
    // name avoids pushing otherwise valid project locations over the Windows
    // path limit during Save As and portable compaction.
    return GenerateStableUuid() + ".stage";
}

std::filesystem::path PortableStagingRoot(const std::filesystem::path& path) {
    std::filesystem::path staging = path;
    staging += ".staging";
    return staging;
}

bool ReplaceFileAtomically(
    const std::filesystem::path& temporaryPath,
    const std::filesystem::path& destinationPath) {
#if defined(_WIN32)
    return MoveFileExW(
               temporaryPath.c_str(), destinationPath.c_str(),
               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(temporaryPath, destinationPath, error);
    return !error;
#endif
}

bool MoveFileWithoutOverwrite(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
#if defined(_WIN32)
    return MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::create_hard_link(source, destination, error);
    if (error) return false;
    std::filesystem::remove(source, error);
    return true;
#endif
}

bool EnsureParentDirectory(const std::filesystem::path& path, std::string& error) {
    const std::filesystem::path parent = path.parent_path();
    if (parent.empty()) return true;
    std::error_code filesystemError;
    std::filesystem::create_directories(parent, filesystemError);
    if (filesystemError) {
        error = "Could not create project storage directory: " + filesystemError.message();
        return false;
    }
    return true;
}

bool CopyStream(
    std::istream& input,
    std::ostream& output,
    std::uint64_t* copiedBytes = nullptr,
    std::uint64_t maximumBytes = std::numeric_limits<std::uint64_t>::max()) {
    // Asset ingestion can run on ordinary UI/worker threads whose Windows
    // stack is commonly only 1 MiB. Keep the streaming buffer on the heap so
    // a large-copy path cannot overflow the thread stack before I/O begins.
    std::vector<char> buffer(1024u * 1024u);
    std::uint64_t copied = 0;
    while (input.good() && copied < maximumBytes) {
        const std::uint64_t remaining = maximumBytes - copied;
        const std::streamsize request = static_cast<std::streamsize>(
            std::min<std::uint64_t>(remaining, buffer.size()));
        input.read(buffer.data(), request);
        const std::streamsize count = input.gcount();
        if (count > 0) {
            output.write(buffer.data(), count);
            if (!output.good()) return false;
            copied += static_cast<std::uint64_t>(count);
        }
    }
    if (copiedBytes) *copiedBytes = copied;
    return copied == maximumBytes || input.eof();
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    return output.good();
}

bool ReadJsonFile(const std::filesystem::path& path, json& value, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Could not open project manifest.";
        return false;
    }
    try {
        input >> value;
    } catch (const std::exception& exception) {
        error = std::string("Could not parse project manifest: ") + exception.what();
        return false;
    }
    return true;
}

bool VerifyFileIdentity(
    const std::filesystem::path& path,
    const EmbeddedAssetRecord& expected,
    std::string& error) {
    const RawEvidence::SourceIdentity identity = RawEvidence::ComputeSourceIdentity(path);
    if (!identity.valid) {
        error = "Could not checksum staged source asset.";
        return false;
    }
    if (identity.sha256 != expected.sha256 || identity.byteSize != expected.byteLength) {
        error = "Staged source asset does not match its expected SHA-256 and byte length.";
        return false;
    }
    return true;
}

bool WriteUnsigned32(std::ostream& output, std::uint32_t value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return output.good();
}

bool WriteUnsigned64(std::ostream& output, std::uint64_t value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return output.good();
}

bool ReadUnsigned32(std::istream& input, std::uint32_t& value) {
    input.read(reinterpret_cast<char*>(&value), sizeof(value));
    return input.good();
}

bool ReadUnsigned64(std::istream& input, std::uint64_t& value) {
    input.read(reinterpret_cast<char*>(&value), sizeof(value));
    return input.good();
}

class BoundedFileStreamBuffer final : public std::streambuf {
public:
    BoundedFileStreamBuffer(
        const std::filesystem::path& path,
        std::uint64_t offset,
        std::uint64_t length)
        : m_File(path, std::ios::binary), m_Remaining(length) {
        if (m_File) {
            m_File.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        }
        setg(m_Buffer.data(), m_Buffer.data(), m_Buffer.data());
    }

    bool Good() const { return m_File.good(); }

protected:
    int_type underflow() override {
        if (!m_File || m_Remaining == 0u) return traits_type::eof();
        const std::streamsize request = static_cast<std::streamsize>(
            std::min<std::uint64_t>(m_Remaining, m_Buffer.size()));
        m_File.read(m_Buffer.data(), request);
        const std::streamsize count = m_File.gcount();
        if (count <= 0) return traits_type::eof();
        m_Remaining -= static_cast<std::uint64_t>(count);
        setg(m_Buffer.data(), m_Buffer.data(), m_Buffer.data() + count);
        return traits_type::to_int_type(*gptr());
    }

private:
    std::ifstream m_File;
    std::uint64_t m_Remaining = 0;
    std::array<char, 64 * 1024> m_Buffer {};
};

class BoundedFileInputStream final : public std::istream {
public:
    BoundedFileInputStream(
        const std::filesystem::path& path,
        std::uint64_t offset,
        std::uint64_t length)
        : std::istream(nullptr), m_Buffer(path, offset, length) {
        rdbuf(&m_Buffer);
        if (!m_Buffer.Good()) setstate(std::ios::badbit);
    }

private:
    BoundedFileStreamBuffer m_Buffer;
};

json SerializeStoreEnvelope(
    const RawProjectSnapshot& snapshot,
    ProjectStorageKind kind,
    std::uint64_t generation,
    const std::unordered_map<std::string, AssetLocation>& locations,
    const AssetLocation* coverLocation) {
    json value = SerializeRawProjectSnapshot(snapshot);
    json storage = {
        { "kind", ProjectStorageKindName(kind) },
        { "generation", generation },
        { "assets", json::object() }
    };
    for (const auto& entry : locations) {
        json location = {
            { "payloadOffset", entry.second.payloadOffset },
            { "payloadLength", entry.second.payloadLength },
            { "checksum", entry.second.checksum }
        };
        if (!entry.second.relativePath.empty()) {
            location["relativePath"] = entry.second.relativePath.generic_u8string();
        }
        storage["assets"][entry.first] = std::move(location);
    }
    if (coverLocation) {
        storage["coverThumbnail"] = {
            { "payloadOffset", coverLocation->payloadOffset },
            { "payloadLength", coverLocation->payloadLength },
            { "checksum", coverLocation->checksum }
        };
        if (!coverLocation->relativePath.empty()) {
            storage["coverThumbnail"]["relativePath"] =
                coverLocation->relativePath.generic_u8string();
        }
    }
    value["_store"] = std::move(storage);
    value["persistedStorageRevision"] = generation;
    return value;
}

bool SameLogicalProjectSnapshot(
    const RawProjectSnapshot& left,
    const RawProjectSnapshot& right) {
    if (left.coverThumbnailBytes != right.coverThumbnailBytes) return false;
    json leftValue = SerializeRawProjectSnapshot(left);
    json rightValue = SerializeRawProjectSnapshot(right);
    // The storage generation is the result of a commit, not authored project
    // content. An otherwise identical save must not manufacture a new
    // generation and make another in-session snapshot appear externally stale.
    leftValue["persistedStorageRevision"] = 0u;
    rightValue["persistedStorageRevision"] = 0u;
    return leftValue == rightValue;
}

bool DeserializeStoreEnvelope(
    const json& value,
    ProjectStorageKind expectedKind,
    RawProjectSnapshot& snapshot,
    std::uint64_t& generation,
    std::unordered_map<std::string, AssetLocation>& locations,
    AssetLocation& coverLocation,
    bool& hasCover,
    std::string& error) try {
    const json storage = value.value("_store", json::object());
    ProjectStorageKind kind;
    if (!ParseProjectStorageKind(storage.value("kind", std::string()), kind) ||
        kind != expectedKind) {
        error = "Project manifest storage kind does not match its container.";
        return false;
    }
    const auto generationValue = storage.find("generation");
    if (generationValue == storage.end() || !generationValue->is_number_unsigned()) {
        error = "Project manifest storage generation is missing or invalid.";
        return false;
    }
    generation = generationValue->get<std::uint64_t>();
    if (!DeserializeRawProjectSnapshot(value, snapshot, &error)) return false;
    snapshot.persistedStorageRevision = generation;

    locations.clear();
    const json assets = storage.value("assets", json::object());
    if (!assets.is_object()) {
        error = "Project storage asset map is invalid.";
        return false;
    }
    for (auto iterator = assets.begin(); iterator != assets.end(); ++iterator) {
        if (!iterator.value().is_object()) {
            error = "Project storage asset location is invalid.";
            return false;
        }
        AssetLocation location;
        location.payloadOffset = iterator.value().value("payloadOffset", 0ull);
        location.payloadLength = iterator.value().value("payloadLength", 0ull);
        location.checksum = iterator.value().value("checksum", std::string());
        location.relativePath = std::filesystem::u8path(iterator.value().value("relativePath", std::string()));
        locations.emplace(iterator.key(), std::move(location));
    }
    hasCover = false;
    const auto cover = storage.find("coverThumbnail");
    if (cover != storage.end() && cover->is_object()) {
        coverLocation.payloadOffset = cover->value("payloadOffset", 0ull);
        coverLocation.payloadLength = cover->value("payloadLength", 0ull);
        coverLocation.checksum = cover->value("checksum", std::string());
        coverLocation.relativePath = std::filesystem::u8path(cover->value("relativePath", std::string()));
        hasCover = true;
    }
    return true;
} catch (const std::exception& exception) {
    // A malformed current manifest must still allow Load to try the previous
    // committed generation. Do not let typed JSON access bypass recovery.
    error = std::string("Project manifest storage data is invalid: ") + exception.what();
    return false;
}

struct RecordHeader {
    std::uint32_t type = 0;
    std::uint64_t payloadLength = 0;
    std::string key;
    std::string checksum;
    std::uint64_t payloadOffset = 0;
};

bool ReadRecordHeaderAt(
    std::ifstream& input,
    std::uint64_t recordOffset,
    RecordHeader& header) {
    input.clear();
    input.seekg(static_cast<std::streamoff>(recordOffset), std::ios::beg);
    std::array<char, 4> magic {};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    std::uint32_t keyLength = 0;
    std::array<char, 64> checksum {};
    if (!input.good() || magic != kRecordMagic ||
        !ReadUnsigned32(input, header.type) ||
        !ReadUnsigned64(input, header.payloadLength) ||
        !ReadUnsigned32(input, keyLength)) {
        return false;
    }
    input.read(checksum.data(), static_cast<std::streamsize>(checksum.size()));
    if (!input.good() || keyLength > 1024u * 1024u) return false;
    header.key.resize(keyLength);
    if (keyLength > 0u) {
        input.read(&header.key[0], static_cast<std::streamsize>(keyLength));
        if (!input.good()) return false;
    }
    header.checksum.assign(checksum.data(), checksum.size());
    header.payloadOffset = recordOffset + kRecordFixedHeaderSize + keyLength;
    return true;
}

bool ReadManifestRecord(
    const std::filesystem::path& path,
    std::uint64_t recordOffset,
    json& manifest,
    std::string& error) {
    std::ifstream input(path, std::ios::binary);
    RecordHeader header;
    if (!input || !ReadRecordHeaderAt(input, recordOffset, header) ||
        header.type != kManifestRecordType || header.payloadLength > kMaximumManifestBytes) {
        error = "Portable project manifest record is invalid.";
        return false;
    }
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(header.payloadLength));
    if (!payload.empty()) {
        input.read(reinterpret_cast<char*>(payload.data()),
                   static_cast<std::streamsize>(payload.size()));
        if (!input.good()) {
            error = "Portable project manifest record is truncated.";
            return false;
        }
    }
    const RawEvidence::SourceIdentity identity = RawEvidence::ComputeSourceIdentity(payload);
    if (!identity.valid || identity.sha256 != header.checksum) {
        error = "Portable project manifest checksum failed.";
        return false;
    }
    try {
        manifest = json::parse(payload.begin(), payload.end());
    } catch (const std::exception& exception) {
        error = std::string("Portable project manifest JSON is invalid: ") + exception.what();
        return false;
    }
    return true;
}

bool FindLastPortableManifest(
    const std::filesystem::path& path,
    json& manifest,
    std::uint64_t& footerGeneration,
    std::uint64_t* footerOffset,
    std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Could not open portable project.";
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff signedSize = input.tellg();
    if (signedSize < static_cast<std::streamoff>(kPortableHeader.size() + kFooterSize)) {
        error = "Portable project is too small.";
        return false;
    }
    const std::uint64_t fileSize = static_cast<std::uint64_t>(signedSize);
    input.seekg(0, std::ios::beg);
    std::array<char, 8> header {};
    input.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (!input.good() || header != kPortableHeader) {
        error = "Portable project header is invalid.";
        return false;
    }

    constexpr std::uint64_t chunkSize = 64u * 1024u;
    std::uint64_t scanEnd = fileSize;
    std::vector<char> chunk;
    while (scanEnd > kPortableHeader.size()) {
        const std::uint64_t scanStart = scanEnd > chunkSize
            ? scanEnd - chunkSize
            : static_cast<std::uint64_t>(kPortableHeader.size());
        const std::uint64_t length = scanEnd - scanStart;
        chunk.resize(static_cast<std::size_t>(length));
        input.clear();
        input.seekg(static_cast<std::streamoff>(scanStart), std::ios::beg);
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        if (input.gcount() != static_cast<std::streamsize>(chunk.size())) break;
        for (std::int64_t index = static_cast<std::int64_t>(chunk.size()) - 4;
             index >= 0; --index) {
            if (!std::equal(kFooterMagic.begin(), kFooterMagic.end(), chunk.begin() + index)) {
                continue;
            }
            const std::uint64_t candidateOffset = scanStart + static_cast<std::uint64_t>(index);
            if (candidateOffset + kFooterSize > fileSize) continue;
            input.clear();
            input.seekg(static_cast<std::streamoff>(candidateOffset + 4u), std::ios::beg);
            std::uint64_t manifestOffset = 0;
            std::uint64_t generation = 0;
            if (!ReadUnsigned64(input, manifestOffset) || !ReadUnsigned64(input, generation) ||
                manifestOffset < kPortableHeader.size() || manifestOffset >= candidateOffset) {
                continue;
            }
            json candidateManifest;
            std::string candidateError;
            if (!ReadManifestRecord(path, manifestOffset, candidateManifest, candidateError)) continue;
            const std::uint64_t manifestGeneration = candidateManifest
                .value("_store", json::object()).value("generation", 0ull);
            if (manifestGeneration != generation) continue;
            manifest = std::move(candidateManifest);
            footerGeneration = generation;
            if (footerOffset) *footerOffset = candidateOffset;
            return true;
        }
        if (scanStart == kPortableHeader.size()) break;
        scanEnd = scanStart + 3u;
    }
    error = "Portable project has no valid committed manifest generation.";
    return false;
}

bool AppendRecordHeader(
    std::ostream& output,
    std::uint32_t type,
    const std::string& key,
    std::uint64_t payloadLength,
    const std::string& checksum) {
    if (checksum.size() != 64u || key.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    output.write(kRecordMagic.data(), static_cast<std::streamsize>(kRecordMagic.size()));
    if (!WriteUnsigned32(output, type) || !WriteUnsigned64(output, payloadLength) ||
        !WriteUnsigned32(output, static_cast<std::uint32_t>(key.size()))) {
        return false;
    }
    output.write(checksum.data(), static_cast<std::streamsize>(checksum.size()));
    output.write(key.data(), static_cast<std::streamsize>(key.size()));
    return output.good();
}

class TransactionalProjectStore final : public ProjectStore {
public:
    TransactionalProjectStore(std::filesystem::path path, ProjectStorageKind kind)
        : m_Path(kind == ProjectStorageKind::DirectoryBundle
              ? NormalizeDirectoryStorePath(path)
              : std::move(path)),
          m_Kind(kind), m_CommitMutex(CommitMutexForPath(m_Path)) {}

    ProjectStorageKind StorageKind() const override { return m_Kind; }
    const std::filesystem::path& StoragePath() const override { return m_Path; }
    std::uint64_t StorageRevision() const override { return m_StorageRevision; }
    bool ReadStorageRevision(std::uint64_t& revision, std::string& error) const override {
        std::lock_guard<std::mutex> lock(m_Mutex);
        try {
            if (ReadCurrentRevision(revision, error)) return true;
            if (error.empty()) error = "The project has no valid storage generation.";
        } catch (const std::exception& exception) {
            error = exception.what();
        }
        return false;
    }
    bool IsReadOnlyRecovery() const override { return m_ReadOnlyRecovery; }

    bool Load(RawProjectSnapshot& snapshot, bool& recoveredPrevious, std::string& error) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto applyManifest = [&](const json& manifest, std::string& loadError) {
            bool hasCover = false;
            if (!DeserializeStoreEnvelope(
                    manifest, m_Kind, snapshot, m_StorageRevision, m_AssetLocations,
                    m_CoverLocation, hasCover, loadError)) {
                return false;
            }
            // Opening validates only the cheap structural asset facts. Full
            // decoding and checksums remain lazy, but a manifest cannot become
            // an active session when a required asset is absent or truncated.
            std::error_code sizeError;
            const std::uint64_t portableSize =
                m_Kind == ProjectStorageKind::PortableFile
                ? static_cast<std::uint64_t>(
                    std::filesystem::file_size(m_Path, sizeError))
                : 0u;
            if (m_Kind == ProjectStorageKind::PortableFile && sizeError) {
                loadError = "The portable project size could not be read.";
                return false;
            }
            for (const EmbeddedAssetRecord& asset : snapshot.embeddedAssets) {
                // Bracket pixels are a derived measurement. The saved originals
                // remain usable if this optional result must be reconstructed.
                if (asset.managedRole == "bracketing-result") continue;
                const auto location = m_AssetLocations.find(asset.assetId);
                if (location == m_AssetLocations.end()) {
                    loadError = "A required managed asset is missing: " +
                        asset.assetId;
                    return false;
                }
                if (m_Kind == ProjectStorageKind::DirectoryBundle) {
                    sizeError.clear();
                    const std::uint64_t actualSize =
                        static_cast<std::uint64_t>(std::filesystem::file_size(
                            m_Path / location->second.relativePath,
                            sizeError));
                    if (sizeError || actualSize != asset.byteLength) {
                        loadError = "A required managed asset is missing or has the wrong size: " +
                            asset.assetId;
                        return false;
                    }
                } else if (
                    location->second.payloadOffset > portableSize ||
                    location->second.payloadLength != asset.byteLength ||
                    location->second.payloadLength >
                        portableSize - location->second.payloadOffset) {
                    loadError = "A portable managed asset is missing or truncated: " +
                        asset.assetId;
                    return false;
                }
            }
            m_HasCover = hasCover;
            if (hasCover) {
                std::string coverError;
                if (!LoadCoverThumbnail(
                        snapshot.coverThumbnailBytes, coverError)) {
                    // preview.png is a rebuildable Library/UI artifact. A
                    // missing or stale preview must never reject an otherwise
                    // valid authoritative project generation.
                    snapshot.coverThumbnailBytes.clear();
                    m_HasCover = false;
                    loadError = "Project loaded without its rebuildable preview (" +
                        coverError + ").";
                }
            }
            return true;
        };

        if (m_Kind == ProjectStorageKind::DirectoryBundle) {
            json currentManifest;
            std::string currentError;
            if (ReadJsonFile(CurrentManifestPath(), currentManifest, currentError) &&
                applyManifest(currentManifest, currentError)) {
                m_Snapshot = snapshot;
                error = currentError;
                return true;
            }

            json previousManifest;
            std::string previousError;
            if (!ReadJsonFile(
                    PreviousManifestPath(),
                    previousManifest,
                    previousError) ||
                !applyManifest(previousManifest, previousError)) {
                error = "Current bundle manifest is invalid (" + currentError +
                    "); previous manifest recovery also failed (" + previousError + ").";
                return false;
            }
            recoveredPrevious = true;
            m_ReadOnlyRecovery = true;
            error = "Recovered the previous project generation because the current manifest is invalid: " +
                currentError;
            if (!previousError.empty()) {
                error += " " + previousError;
            }
        } else {
            json manifest;
            std::uint64_t generation = 0;
            if (!FindLastPortableManifest(m_Path, manifest, generation, nullptr, error)) return false;
            if (!applyManifest(manifest, error)) return false;
        }
        m_Snapshot = snapshot;
        return true;
    }

    ProjectStoreTransaction BeginTransaction(
        std::uint64_t expectedStorageRevision) override {
        std::lock_guard<std::mutex> lock(m_Mutex);
        ProjectStoreTransaction transaction;
        transaction.transactionId = GenerateStableUuid();
        transaction.expectedStorageRevision = expectedStorageRevision;
        TransactionState state;
        state.expectedStorageRevision = expectedStorageRevision;
        state.stagingDirectory = StagingRoot() / transaction.transactionId;
        std::error_code error;
        std::filesystem::create_directories(state.stagingDirectory, error);
        if (error) return {};
        m_Transactions.emplace(transaction.transactionId, std::move(state));
        return transaction;
    }

    bool StageAssetFile(
        const ProjectStoreTransaction& transaction,
        const std::filesystem::path& sourcePath,
        MultiFrameInputFamily inputFamily,
        const json& captureMetadataSummary,
        EmbeddedAssetRecord& record,
        std::string* errorMessage) override {
        std::string error;
        const RawEvidence::SourceIdentity identity = RawEvidence::ComputeSourceIdentity(sourcePath);
        if (!identity.valid) {
            error = "Could not read the selected source asset.";
            return Finish(errorMessage, error, false);
        }
        record.assetId = MakeAssetId(identity.sha256, identity.byteSize);
        record.sha256 = identity.sha256;
        record.byteLength = identity.byteSize;
        record.originalFilename = sourcePath.filename().u8string();
        record.displayName = record.originalFilename;
        record.originalSourcePath = sourcePath.u8string();
        // Provenance is relative to the enclosing photo workspace, when this
        // store belongs to one. Asset playback always uses projectAssetPath.
        for (auto parent = m_Path.parent_path(); !parent.empty();) {
            std::error_code provenanceError;
            if (LowerAscii(parent.filename().u8string()) == "closet" &&
                std::filesystem::is_regular_file(parent / "workspace.json", provenanceError)) {
                const auto relative = sourcePath.lexically_normal().lexically_relative(parent.parent_path());
                if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..")
                    record.workspaceRelativeSourcePath = relative.generic_u8string();
                break;
            }
            const auto next = parent.parent_path();
            if (next == parent) break;
            parent = next;
        }
        record.originalFileFingerprint = identity.sha256;
        record.inputFamily = inputFamily;
        record.captureMetadataSummary = captureMetadataSummary;

        std::lock_guard<std::mutex> lock(m_Mutex);
        TransactionState* state = FindTransaction(transaction, error);
        if (!state) return Finish(errorMessage, error, false);
        if (m_AssetLocations.find(record.assetId) != m_AssetLocations.end() ||
            state->assets.find(record.assetId) != state->assets.end()) {
            return Finish(errorMessage, std::string(), true);
        }
        std::string managedName = SanitizeManagedFileName(record.originalFilename);
        std::filesystem::path managedPath =
            std::filesystem::path(kWorkingProjectAssetsDirectoryName) /
            std::filesystem::u8path(managedName);
        const auto pathAlreadyUsed = [&](const std::filesystem::path& candidate) {
            const std::string key = LowerAscii(candidate.generic_u8string());
            for (const auto& [assetId, location] : m_AssetLocations) {
                (void)assetId;
                if (LowerAscii(location.relativePath.generic_u8string()) == key) {
                    return true;
                }
            }
            for (const auto& [assetId, staged] : state->assets) {
                (void)assetId;
                if (LowerAscii(staged.record.projectAssetPath) == key) {
                    return true;
                }
            }
            return false;
        };
        if (pathAlreadyUsed(managedPath)) {
            const auto namePath = std::filesystem::u8path(managedName);
            const std::string identitySuffix = identity.sha256.substr(0u, 8u);
            managedName = namePath.stem().u8string() + "-" + identitySuffix +
                namePath.extension().u8string();
            managedPath = std::filesystem::path(kWorkingProjectAssetsDirectoryName) /
                std::filesystem::u8path(managedName);
        }
        record.projectAssetPath = managedPath.generic_u8string();
        const std::filesystem::path stagedPath =
            state->stagingDirectory / FileNameForStagedAsset();
        std::ifstream input(sourcePath, std::ios::binary);
        std::ofstream output(stagedPath, std::ios::binary | std::ios::trunc);
        std::uint64_t copied = 0;
        if (!input) {
            error = "Could not open the selected source asset for staging.";
            return Finish(errorMessage, error, false);
        }
        if (!output) {
            error = "Could not create the project staging file.";
            return Finish(errorMessage, error, false);
        }
        if (!CopyStream(input, output, &copied) || copied != record.byteLength) {
            error = "Could not copy the selected source asset into project staging.";
            return Finish(errorMessage, error, false);
        }
        output.close();
        if (!output.good() || !VerifyFileIdentity(stagedPath, record, error)) {
            return Finish(errorMessage, error, false);
        }
        state->assets.emplace(record.assetId, StagedAsset { record, stagedPath });
        return Finish(errorMessage, std::string(), true);
    }

    bool StageAssetStream(
        const ProjectStoreTransaction& transaction,
        std::istream& source,
        const EmbeddedAssetRecord& expectedRecord,
        std::string* errorMessage) override {
        std::string error;
        std::lock_guard<std::mutex> lock(m_Mutex);
        TransactionState* state = FindTransaction(transaction, error);
        if (!state) return Finish(errorMessage, error, false);
        if (m_AssetLocations.find(expectedRecord.assetId) != m_AssetLocations.end() ||
            state->assets.find(expectedRecord.assetId) != state->assets.end()) {
            return Finish(errorMessage, std::string(), true);
        }
        EmbeddedAssetRecord stagedRecord = expectedRecord;
        if (stagedRecord.displayName.empty()) {
            stagedRecord.displayName = stagedRecord.originalFilename;
        }
        if (stagedRecord.originalFileFingerprint.empty()) {
            stagedRecord.originalFileFingerprint = stagedRecord.sha256;
        }
        if (!IsSafeManagedAssetPath(stagedRecord.projectAssetPath)) {
            std::string managedName = SanitizeManagedFileName(
                stagedRecord.originalFilename.empty()
                    ? stagedRecord.assetId.substr(0u, 16u)
                    : stagedRecord.originalFilename);
            stagedRecord.projectAssetPath = (
                std::filesystem::path(kWorkingProjectAssetsDirectoryName) /
                std::filesystem::u8path(managedName)).generic_u8string();
        }
        const auto pathAlreadyUsedByAnotherAsset = [&]() {
            const std::string key = LowerAscii(stagedRecord.projectAssetPath);
            for (const auto& [assetId, location] : m_AssetLocations) {
                if (assetId != stagedRecord.assetId &&
                    LowerAscii(location.relativePath.generic_u8string()) == key) {
                    return true;
                }
            }
            for (const auto& [assetId, staged] : state->assets) {
                if (assetId != stagedRecord.assetId &&
                    LowerAscii(staged.record.projectAssetPath) == key) {
                    return true;
                }
            }
            return false;
        };
        if (pathAlreadyUsedByAnotherAsset()) {
            const auto authoredPath = std::filesystem::u8path(stagedRecord.projectAssetPath);
            const std::string identitySuffix =
                stagedRecord.sha256.substr(0u, 8u);
            stagedRecord.projectAssetPath = (
                authoredPath.parent_path() /
                (authoredPath.stem().u8string() + "-" + identitySuffix +
                 authoredPath.extension().u8string())).generic_u8string();
        }
        const std::filesystem::path stagedPath =
            state->stagingDirectory / FileNameForStagedAsset();
        std::ofstream output(stagedPath, std::ios::binary | std::ios::trunc);
        std::uint64_t copied = 0;
        if (!output) {
            error = "Could not create the project staging file for " +
                expectedRecord.assetId + ".";
            return Finish(errorMessage, error, false);
        }
        if (!CopyStream(source, output, &copied, stagedRecord.byteLength) ||
            copied != stagedRecord.byteLength) {
            error = "Could not stream the source asset into project staging (" +
                stagedRecord.assetId + "; expected " +
                std::to_string(stagedRecord.byteLength) + " bytes, copied " +
                std::to_string(copied) + ").";
            return Finish(errorMessage, error, false);
        }
        output.close();
        if (!output.good() || !VerifyFileIdentity(stagedPath, stagedRecord, error)) {
            return Finish(errorMessage, error, false);
        }
        state->assets.emplace(
            stagedRecord.assetId, StagedAsset { stagedRecord, stagedPath });
        return Finish(errorMessage, std::string(), true);
    }

    ProjectStoreCommitResult Commit(
        const ProjectStoreTransaction& transaction,
        const RawProjectSnapshot& requestedSnapshot) override {
        return CommitAtRevision(transaction, requestedSnapshot, std::nullopt);
    }

    void Abort(const ProjectStoreTransaction& transaction) override {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto found = m_Transactions.find(transaction.transactionId);
        if (found == m_Transactions.end()) return;
        const std::filesystem::path stagingDirectory = found->second.stagingDirectory;
        m_Transactions.erase(found);
        RemoveStagingDirectory(stagingDirectory);
    }

    ProjectAssetStream OpenAssetStream(
        const std::string& assetId,
        std::string* errorMessage) const override {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto found = m_AssetLocations.find(assetId);
        if (found == m_AssetLocations.end()) {
            Finish(errorMessage, "The project does not contain that embedded asset.", false);
            return {};
        }
        ProjectAssetStream result;
        if (m_Kind == ProjectStorageKind::DirectoryBundle) {
            const std::filesystem::path assetPath = m_Path / found->second.relativePath;
            auto stream = std::make_unique<std::ifstream>(assetPath, std::ios::binary);
            if (!*stream) {
                Finish(errorMessage, "Could not open embedded project asset.", false);
                return {};
            }
            result.stream = std::move(stream);
        } else {
            std::error_code sizeError;
            const std::uint64_t fileSize = static_cast<std::uint64_t>(
                std::filesystem::file_size(m_Path, sizeError));
            if (sizeError || found->second.payloadOffset > fileSize ||
                found->second.payloadLength > fileSize - found->second.payloadOffset) {
                Finish(
                    errorMessage,
                    "Portable embedded asset location is outside the project file (" +
                        assetId + "; offset " +
                        std::to_string(found->second.payloadOffset) + ", length " +
                        std::to_string(found->second.payloadLength) + ", file " +
                        std::to_string(fileSize) + ").",
                    false);
                return {};
            }
            auto stream = std::make_unique<BoundedFileInputStream>(
                m_Path, found->second.payloadOffset, found->second.payloadLength);
            if (!*stream) {
                Finish(errorMessage, "Could not open portable embedded project asset.", false);
                return {};
            }
            result.stream = std::move(stream);
        }
        result.byteLength = found->second.payloadLength;
        Finish(errorMessage, std::string(), true);
        return result;
    }

    bool CopyAssetToFile(
        const std::string& assetId,
        const std::filesystem::path& destinationPath,
        std::string* errorMessage) const override {
        std::string error;
        ProjectAssetStream source = OpenAssetStream(assetId, &error);
        if (!source) return Finish(errorMessage, error, false);
        if (!EnsureParentDirectory(destinationPath, error)) {
            return Finish(errorMessage, error, false);
        }
        // Stage beside the destination so publication remains an atomic rename,
        // but do not append to the already content-addressed destination name.
        // MultiFrame cache paths contain several identity segments; appending
        // even a shortened UUID to that full name can push an otherwise valid
        // Windows path past MAX_PATH before the processor starts.
        const std::string temporaryId = GenerateStableUuid();
        const std::filesystem::path temporary =
            destinationPath.parent_path() /
            (".asset-copy-" + temporaryId.substr(0u, 16u) + ".tmp");
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "Could not create the temporary project-asset copy.";
            return Finish(errorMessage, error, false);
        }
        std::uint64_t copied = 0;
        if (!CopyStream(*source.stream, output, &copied, source.byteLength) ||
            copied != source.byteLength) {
            error = "Could not read the complete embedded project asset (expected " +
                std::to_string(source.byteLength) + " bytes, copied " +
                std::to_string(copied) + ").";
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            return Finish(errorMessage, error, false);
        }
        output.close();
        if (!output.good() || !ReplaceFileAtomically(temporary, destinationPath)) {
            error = "Could not publish copied project asset.";
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            return Finish(errorMessage, error, false);
        }
        return Finish(errorMessage, std::string(), true);
    }

    bool Verify(
        const RawProjectSnapshot& snapshot,
        std::vector<std::string>* errors) const override {
        const ModelValidationResult validation = ValidateRawProjectSnapshot(snapshot);
        std::vector<std::string> localErrors = validation.errors;
        for (const EmbeddedAssetRecord& asset : snapshot.embeddedAssets) {
            std::filesystem::path temporary;
            if (m_Kind == ProjectStorageKind::DirectoryBundle) {
                std::lock_guard<std::mutex> lock(m_Mutex);
                const auto found = m_AssetLocations.find(asset.assetId);
                if (found == m_AssetLocations.end()) {
                    localErrors.push_back("Missing stored asset: " + asset.assetId);
                    continue;
                }
                temporary = m_Path / found->second.relativePath;
                std::string identityError;
                if (!VerifyFileIdentity(temporary, asset, identityError)) {
                    localErrors.push_back(identityError + " " + asset.assetId);
                }
            } else {
                std::string streamError;
                ProjectAssetStream stream = OpenAssetStream(asset.assetId, &streamError);
                if (!stream) {
                    localErrors.push_back(streamError);
                    continue;
                }
                const std::filesystem::path verificationPath =
                    PortableStagingRoot(m_Path) /
                    ("verify-" + GenerateStableUuid() + ".tmp");
                std::error_code directoryError;
                std::filesystem::create_directories(verificationPath.parent_path(), directoryError);
                if (directoryError) {
                    localErrors.push_back("Could not create verification staging file.");
                    continue;
                }
                std::ofstream output(verificationPath, std::ios::binary | std::ios::trunc);
                std::uint64_t copied = 0;
                if (!output || !CopyStream(*stream.stream, output, &copied, asset.byteLength) ||
                    copied != asset.byteLength) {
                    localErrors.push_back("Could not stream asset for verification: " + asset.assetId);
                }
                output.close();
                std::string identityError;
                if (output.good() && !VerifyFileIdentity(verificationPath, asset, identityError)) {
                    localErrors.push_back(identityError + " " + asset.assetId);
                }
                std::error_code cleanupError;
                std::filesystem::remove(verificationPath, cleanupError);
            }
        }
        if (errors) *errors = std::move(localErrors);
        return errors ? errors->empty() : localErrors.empty();
    }

    bool Optimize(std::string* errorMessage) override {
        if (m_Kind == ProjectStorageKind::DirectoryBundle) {
            return Finish(errorMessage, std::string(), true);
        }
        const std::filesystem::path temporary =
            m_Path.parent_path() /
            (".stack-optimize-" + GenerateStableUuid() + ".tmp");
        const auto cleanupTemporary = [&]() {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            cleanupError.clear();
            std::filesystem::remove_all(PortableStagingRoot(temporary), cleanupError);
        };
        std::uint64_t expectedSourceRevision = 0;
        RawProjectSnapshot sourceSnapshot;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            expectedSourceRevision = m_StorageRevision;
            sourceSnapshot = m_Snapshot;
        }
        if (expectedSourceRevision == std::numeric_limits<std::uint64_t>::max()) {
            return Finish(errorMessage, "The project storage revision is exhausted.", false);
        }
        RawProjectSnapshot bootstrap = sourceSnapshot;
        bootstrap.embeddedAssets.clear();
        bootstrap.sourceSets.clear();
        bootstrap.multiFrameGraph = {};
        bootstrap.pipelineData = json::object();
        bootstrap.rawWorkspaceData = json::object();
        bootstrap.nodeBrowserThumbnails = json::array();
        bootstrap.coverThumbnailBytes.clear();
        bootstrap.sourceAssetId.clear();
        bootstrap.lifecycle.initialAssetIds.clear();
        bootstrap.activeSourceSetId.clear();
        bootstrap.activeFrameId.clear();
        bootstrap.mfdInputRevision = 0;
        bootstrap.hdrInputRevision = 0;
        bootstrap.postRecipeRevision = 0;
        bootstrap.dirtyRevision = 0;
        bootstrap.persistedStorageRevision = 0;
        ProjectStoreOpenResult target = CreateProjectStore(
            temporary, ProjectStorageKind::PortableFile, bootstrap);
        if (!target) {
            cleanupTemporary();
            return Finish(errorMessage, target.message, false);
        }
        const ProjectStoreTransaction transaction =
            target.store->BeginTransaction(target.snapshot.persistedStorageRevision);
        if (!transaction) {
            target.store.reset();
            cleanupTemporary();
            return Finish(errorMessage, "Could not begin compact project transaction.", false);
        }
        for (const EmbeddedAssetRecord& asset : sourceSnapshot.embeddedAssets) {
            std::string streamError;
            ProjectAssetStream stream = OpenAssetStream(asset.assetId, &streamError);
            if (!stream || !target.store->StageAssetStream(
                    transaction, *stream.stream, asset, &streamError)) {
                target.store->Abort(transaction);
                target.store.reset();
                cleanupTemporary();
                return Finish(errorMessage, streamError, false);
            }
        }
        RawProjectSnapshot compactSnapshot = std::move(sourceSnapshot);
        compactSnapshot.persistedStorageRevision = target.snapshot.persistedStorageRevision;
        const auto targetImplementation =
            std::dynamic_pointer_cast<TransactionalProjectStore>(target.store);
        if (!targetImplementation) {
            target.store->Abort(transaction);
            target.store.reset();
            cleanupTemporary();
            return Finish(errorMessage, "The compact project store is unavailable.", false);
        }
        const ProjectStoreCommitResult commit = targetImplementation->CommitAtRevision(
            transaction, compactSnapshot, expectedSourceRevision + 1u);
        if (!commit) {
            target.store.reset();
            cleanupTemporary();
            return Finish(errorMessage, commit.message, false);
        }
        target.store.reset();
        ProjectStoreOpenResult compactCheck = OpenProjectStore(temporary);
        std::vector<std::string> compactErrors;
        if (!compactCheck ||
            compactCheck.store->StorageRevision() != expectedSourceRevision + 1u ||
            !compactCheck.store->Verify(compactCheck.snapshot, &compactErrors)) {
            const std::string compactError = !compactCheck
                ? compactCheck.message
                : (compactErrors.empty()
                    ? "The compact project revision could not be verified."
                    : compactErrors.front());
            compactCheck.store.reset();
            cleanupTemporary();
            return Finish(errorMessage, compactError, false);
        }
        compactCheck.store.reset();
        // Another handle may have committed while the compact copy was built.
        // Hold the same path lock as Commit through revision check and replace.
        std::lock_guard<std::mutex> commitLock(*m_CommitMutex);
        std::uint64_t currentSourceRevision = 0;
        std::string revisionError;
        if (!ReadCurrentRevision(currentSourceRevision, revisionError) ||
            currentSourceRevision != expectedSourceRevision) {
            cleanupTemporary();
            return Finish(
                errorMessage,
                revisionError.empty()
                    ? "The project changed while it was being optimized. Reload or Save Copy."
                    : revisionError,
                false);
        }
        std::error_code stagingCleanupError;
        std::filesystem::remove_all(
            PortableStagingRoot(temporary), stagingCleanupError);
        if (!ReplaceFileAtomically(temporary, m_Path)) {
            cleanupTemporary();
            return Finish(errorMessage, "Could not atomically replace the optimized project.", false);
        }
        bool recovered = false;
        std::string loadError;
        RawProjectSnapshot reloaded;
        if (!Load(reloaded, recovered, loadError)) {
            return Finish(errorMessage, loadError, false);
        }
        return Finish(errorMessage, std::string(), true);
    }

private:
    static bool Finish(std::string* output, const std::string& message, bool result) {
        if (output) *output = message;
        return result;
    }

    ProjectStoreCommitResult CommitAtRevision(
        const ProjectStoreTransaction& transaction,
        const RawProjectSnapshot& requestedSnapshot,
        std::optional<std::uint64_t> requestedStorageRevision) {
        // The storage generation check and publication are one operation for
        // every handle to this path, including projects open in separate tabs.
        std::lock_guard<std::mutex> commitLock(*m_CommitMutex);
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_ReadOnlyRecovery) {
            return { ProjectStoreCommitStatus::ReadOnlyRecovery, m_StorageRevision,
                     "A recovered previous manifest must be saved as a repaired copy." };
        }
        std::string error;
        TransactionState* state = FindTransaction(transaction, error);
        if (!state) {
            return { ProjectStoreCommitStatus::IoFailure, m_StorageRevision, error };
        }
        const ModelValidationResult validation = ValidateRawProjectSnapshot(requestedSnapshot);
        if (!validation.valid) {
            return { ProjectStoreCommitStatus::InvalidSnapshot, m_StorageRevision,
                     validation.errors.empty() ? "Project snapshot is invalid."
                                               : validation.errors.front() };
        }
        std::uint64_t diskRevision = 0;
        if (!ReadCurrentRevision(diskRevision, error)) {
            return { ProjectStoreCommitStatus::IoFailure, m_StorageRevision, error };
        }
        if (diskRevision != state->expectedStorageRevision) {
            return { ProjectStoreCommitStatus::Conflict, diskRevision,
                     "The project changed outside this session. Reload, Save Copy, or Cancel." };
        }
        for (const EmbeddedAssetRecord& asset : requestedSnapshot.embeddedAssets) {
            if (m_AssetLocations.find(asset.assetId) == m_AssetLocations.end() &&
                state->assets.find(asset.assetId) == state->assets.end()) {
                return { ProjectStoreCommitStatus::InvalidSnapshot, m_StorageRevision,
                         "Project snapshot publishes an asset that was not staged." };
            }
        }
        if (!requestedStorageRevision &&
            state->assets.empty() &&
            SameLogicalProjectSnapshot(requestedSnapshot, m_Snapshot)) {
            const std::filesystem::path stagingDirectory = state->stagingDirectory;
            m_Transactions.erase(transaction.transactionId);
            RemoveStagingDirectory(stagingDirectory);
            return { ProjectStoreCommitStatus::Committed, diskRevision, std::string() };
        }
        if (!requestedStorageRevision &&
            diskRevision == std::numeric_limits<std::uint64_t>::max()) {
            return { ProjectStoreCommitStatus::IoFailure, m_StorageRevision,
                     "The project storage revision is exhausted." };
        }
        const std::uint64_t newRevision = requestedStorageRevision.value_or(diskRevision + 1u);
        if (newRevision <= diskRevision) {
            return { ProjectStoreCommitStatus::InvalidSnapshot, m_StorageRevision,
                     "A project commit must advance the storage revision." };
        }
        RawProjectSnapshot snapshot = requestedSnapshot;
        snapshot.persistedStorageRevision = newRevision;
        const bool committed = m_Kind == ProjectStorageKind::DirectoryBundle
            ? CommitDirectory(*state, snapshot, newRevision, error)
            : CommitPortable(*state, snapshot, newRevision, error);
        if (!committed) {
            return { ProjectStoreCommitStatus::IoFailure, m_StorageRevision, error };
        }
        m_StorageRevision = newRevision;
        m_Snapshot = std::move(snapshot);
        const std::filesystem::path stagingDirectory = state->stagingDirectory;
        m_Transactions.erase(transaction.transactionId);
        RemoveStagingDirectory(stagingDirectory);
        NotifyProjectCatalogChanged();
        return { ProjectStoreCommitStatus::Committed, newRevision, std::string() };
    }

    std::filesystem::path StagingRoot() const {
        return m_Kind == ProjectStorageKind::DirectoryBundle
            ? m_Path / kBundleStagingDirectoryName
            : PortableStagingRoot(m_Path);
    }

    std::filesystem::path CurrentManifestPath() const {
        return m_Path / kWorkingProjectManifestName;
    }

    std::filesystem::path PreviousManifestPath() const {
        return m_Path / kWorkingProjectPreviousManifestName;
    }

    void RemoveStagingDirectory(const std::filesystem::path& path) const {
        const std::filesystem::path root = StagingRoot().lexically_normal();
        const std::filesystem::path target = path.lexically_normal();
        if (target.empty() || target == root || target.parent_path() != root) return;
        std::error_code error;
        std::filesystem::remove_all(target, error);
    }

    TransactionState* FindTransaction(
        const ProjectStoreTransaction& transaction,
        std::string& error) {
        const auto found = m_Transactions.find(transaction.transactionId);
        if (found == m_Transactions.end() ||
            found->second.expectedStorageRevision != transaction.expectedStorageRevision) {
            error = "Project store transaction is no longer active.";
            return nullptr;
        }
        return &found->second;
    }

    bool ReadCurrentRevision(std::uint64_t& revision, std::string& error) const {
        json manifest;
        if (m_Kind == ProjectStorageKind::DirectoryBundle) {
            if (!ReadJsonFile(CurrentManifestPath(), manifest, error)) return false;
            revision = manifest.value("_store", json::object()).value("generation", 0ull);
            return revision > 0u;
        }
        return FindLastPortableManifest(m_Path, manifest, revision, nullptr, error);
    }

    bool LoadCoverThumbnail(std::vector<unsigned char>& bytes, std::string& error) const {
        bytes.clear();
        if (!m_HasCover || m_CoverLocation.payloadLength == 0u) return true;
        std::unique_ptr<std::istream> input;
        if (m_Kind == ProjectStorageKind::DirectoryBundle) {
            auto file = std::make_unique<std::ifstream>(
                m_Path / m_CoverLocation.relativePath, std::ios::binary);
            if (!*file) {
                error = "Could not read project cover thumbnail.";
                return false;
            }
            input = std::move(file);
        } else {
            auto file = std::make_unique<BoundedFileInputStream>(
                m_Path, m_CoverLocation.payloadOffset, m_CoverLocation.payloadLength);
            if (!*file) {
                error = "Could not read portable project cover thumbnail.";
                return false;
            }
            input = std::move(file);
        }
        if (m_CoverLocation.payloadLength > std::numeric_limits<std::size_t>::max()) {
            error = "Project cover thumbnail is too large.";
            return false;
        }
        bytes.resize(static_cast<std::size_t>(m_CoverLocation.payloadLength));
        if (!bytes.empty()) {
            input->read(reinterpret_cast<char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
            if (input->gcount() != static_cast<std::streamsize>(bytes.size())) {
                error = "Project cover thumbnail is truncated.";
                return false;
            }
        }
        const RawEvidence::SourceIdentity identity = RawEvidence::ComputeSourceIdentity(
            std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
        if (!m_CoverLocation.checksum.empty() &&
            identity.sha256 != m_CoverLocation.checksum) {
            error = "Project cover thumbnail checksum failed.";
            return false;
        }
        return true;
    }

    bool CommitDirectory(
        TransactionState& state,
        const RawProjectSnapshot& snapshot,
        std::uint64_t revision,
        std::string& error) {
        std::error_code filesystemError;
        const std::filesystem::path mediaRoot =
            m_Path / kWorkingProjectAssetsDirectoryName;
        std::filesystem::create_directories(mediaRoot, filesystemError);
        if (filesystemError) {
            error = "Could not create bundle media directory.";
            return false;
        }
        auto newLocations = m_AssetLocations;
        for (const auto& entry : state.assets) {
            const EmbeddedAssetRecord& asset = entry.second.record;
            const auto authoredPath = std::filesystem::u8path(asset.projectAssetPath);
            const std::filesystem::path relative = IsSafeManagedAssetPath(authoredPath)
                ? authoredPath
                : std::filesystem::path(kWorkingProjectAssetsDirectoryName) /
                    FileNameForAsset(asset);
            const std::filesystem::path destination = m_Path / relative;
            std::filesystem::create_directories(
                destination.parent_path(), filesystemError);
            if (filesystemError) {
                error = "Could not create the managed asset directory.";
                return false;
            }
            if (std::filesystem::exists(destination, filesystemError)) {
                if (!VerifyFileIdentity(destination, asset, error)) return false;
                std::filesystem::remove(entry.second.stagingPath, filesystemError);
            } else if (!MoveFileWithoutOverwrite(entry.second.stagingPath, destination)) {
                // A file published by another process may have won the claim.
                // Reuse it only when it has exactly the staged content.
                if (!VerifyFileIdentity(destination, asset, error)) return false;
                std::filesystem::remove(entry.second.stagingPath, filesystemError);
            }
            newLocations[asset.assetId] = AssetLocation {
                0u, asset.byteLength, asset.sha256, relative
            };
        }

        AssetLocation coverLocation;
        bool hasCover = false;
        if (!snapshot.coverThumbnailBytes.empty()) {
            const RawEvidence::SourceIdentity identity = RawEvidence::ComputeSourceIdentity(
                std::vector<std::uint8_t>(snapshot.coverThumbnailBytes.begin(),
                                          snapshot.coverThumbnailBytes.end()));
            const std::filesystem::path relative =
                std::filesystem::path(kWorkingProjectPreviewName);
            const std::filesystem::path coverPath = m_Path / relative;
            bool coverPublished = true;
            filesystemError.clear();
            std::filesystem::create_directories(coverPath.parent_path(), filesystemError);
            if (filesystemError) {
                coverPublished = false;
            }
            std::filesystem::path temporary = coverPath;
            temporary += ".tmp-" + state.stagingDirectory.filename().u8string();
            if (coverPublished) {
                std::ofstream output(
                    temporary, std::ios::binary | std::ios::trunc);
                if (!output) {
                    coverPublished = false;
                } else {
                    output.write(
                        reinterpret_cast<const char*>(
                            snapshot.coverThumbnailBytes.data()),
                        static_cast<std::streamsize>(
                            snapshot.coverThumbnailBytes.size()));
                    output.close();
                    coverPublished = output.good() &&
                        ReplaceFileAtomically(temporary, coverPath);
                }
            }
            if (!coverPublished) {
                std::error_code cleanupError;
                std::filesystem::remove(temporary, cleanupError);
                if (std::any_of(snapshot.sourceSets.begin(),snapshot.sourceSets.end(),[](const auto& set) {
                    return set.settings.contains("bracketingResult");
                })) {
                    error = "Could not publish the saved bracket cover.";
                    return false;
                }
            } else {
                coverLocation = AssetLocation {
                    0u,
                    static_cast<std::uint64_t>(
                        snapshot.coverThumbnailBytes.size()),
                    identity.sha256,
                    relative
                };
                hasCover = true;
            }
        }

        const json manifest = SerializeStoreEnvelope(
            snapshot, m_Kind, revision, newLocations,
            hasCover ? &coverLocation : nullptr);
        const std::filesystem::path manifestPath = CurrentManifestPath();
        std::filesystem::path temporary = manifestPath;
        temporary += ".tmp-" + state.stagingDirectory.filename().u8string();
        if (!WriteTextFile(temporary, manifest.dump(2))) {
            error = "Could not write staged bundle manifest.";
            return false;
        }
        filesystemError.clear();
        if (std::filesystem::exists(manifestPath, filesystemError)) {
            const std::filesystem::path previous = PreviousManifestPath();
            std::filesystem::path previousTemporary = previous;
            previousTemporary += ".tmp-" + state.stagingDirectory.filename().u8string();
            std::filesystem::copy_file(
                manifestPath, previousTemporary,
                std::filesystem::copy_options::overwrite_existing, filesystemError);
            if (filesystemError || !ReplaceFileAtomically(previousTemporary, previous)) {
                std::filesystem::remove(temporary, filesystemError);
                error = "Could not retain the previous bundle manifest.";
                return false;
            }
        }
        if (!ReplaceFileAtomically(temporary, manifestPath)) {
            std::filesystem::remove(temporary, filesystemError);
            error = "Could not atomically publish the bundle manifest.";
            return false;
        }
        m_AssetLocations = std::move(newLocations);
        m_CoverLocation = coverLocation;
        m_HasCover = hasCover;
        return true;
    }

    bool CommitPortable(
        TransactionState& state,
        const RawProjectSnapshot& snapshot,
        std::uint64_t revision,
        std::string& error) {
        std::ofstream output(m_Path, std::ios::binary | std::ios::app);
        if (!output) {
            error = "Could not append to portable project.";
            return false;
        }
        output.seekp(0, std::ios::end);
        if (!output.good() || output.tellp() < 0) {
            error = "Could not locate the portable append position.";
            return false;
        }
        std::error_code sizeError;
        std::uint64_t appendOffset = static_cast<std::uint64_t>(
            std::filesystem::file_size(m_Path, sizeError));
        if (sizeError) {
            error = "Could not measure the portable append position.";
            return false;
        }
        auto newLocations = m_AssetLocations;
        for (const auto& entry : state.assets) {
            const EmbeddedAssetRecord& asset = entry.second.record;
            const std::uint64_t recordOffset = appendOffset;
            if (!AppendRecordHeader(
                    output, kAssetRecordType, asset.assetId, asset.byteLength, asset.sha256)) {
                error = "Could not append portable asset record header.";
                return false;
            }
            const std::uint64_t payloadOffset =
                recordOffset + kRecordFixedHeaderSize + asset.assetId.size();
            std::ifstream input(entry.second.stagingPath, std::ios::binary);
            std::uint64_t copied = 0;
            if (!input || !CopyStream(input, output, &copied, asset.byteLength) ||
                copied != asset.byteLength) {
                error = "Could not append portable asset bytes.";
                return false;
            }
            newLocations[asset.assetId] = AssetLocation {
                payloadOffset, asset.byteLength, asset.sha256, {}
            };
            appendOffset += kRecordFixedHeaderSize +
                static_cast<std::uint64_t>(asset.assetId.size()) +
                asset.byteLength;
        }

        AssetLocation coverLocation;
        bool hasCover = !snapshot.coverThumbnailBytes.empty();
        if (hasCover) {
            const std::vector<std::uint8_t> bytes(
                snapshot.coverThumbnailBytes.begin(), snapshot.coverThumbnailBytes.end());
            const RawEvidence::SourceIdentity identity = RawEvidence::ComputeSourceIdentity(bytes);
            const std::uint64_t recordOffset = appendOffset;
            if (!AppendRecordHeader(
                    output, kCoverRecordType, "cover", identity.byteSize, identity.sha256)) {
                error = "Could not append portable cover record header.";
                return false;
            }
            const std::uint64_t payloadOffset = recordOffset + kRecordFixedHeaderSize + 5u;
            output.write(
                reinterpret_cast<const char*>(snapshot.coverThumbnailBytes.data()),
                static_cast<std::streamsize>(snapshot.coverThumbnailBytes.size()));
            if (!output.good()) {
                error = "Could not append portable cover bytes.";
                return false;
            }
            coverLocation = AssetLocation {
                payloadOffset, identity.byteSize, identity.sha256, {}
            };
            appendOffset += kRecordFixedHeaderSize + 5u + identity.byteSize;
        }

        const json manifest = SerializeStoreEnvelope(
            snapshot, m_Kind, revision, newLocations,
            hasCover ? &coverLocation : nullptr);
        const std::string manifestText = manifest.dump();
        const std::vector<std::uint8_t> manifestBytes(
            manifestText.begin(), manifestText.end());
        const RawEvidence::SourceIdentity manifestIdentity =
            RawEvidence::ComputeSourceIdentity(manifestBytes);
        const std::uint64_t manifestOffset = appendOffset;
        if (!AppendRecordHeader(
                output, kManifestRecordType, "manifest", manifestIdentity.byteSize,
                manifestIdentity.sha256)) {
            error = "Could not append portable manifest record header.";
            return false;
        }
        output.write(manifestText.data(), static_cast<std::streamsize>(manifestText.size()));
        output.write(kFooterMagic.data(), static_cast<std::streamsize>(kFooterMagic.size()));
        if (!WriteUnsigned64(output, manifestOffset) || !WriteUnsigned64(output, revision)) {
            error = "Could not append portable manifest footer.";
            return false;
        }
        output.flush();
        output.close();
        if (!output.good()) {
            error = "Could not flush portable project commit.";
            return false;
        }
        m_AssetLocations = std::move(newLocations);
        m_CoverLocation = coverLocation;
        m_HasCover = hasCover;
        return true;
    }

    std::filesystem::path m_Path;
    ProjectStorageKind m_Kind;
    std::uint64_t m_StorageRevision = 0;
    bool m_ReadOnlyRecovery = false;
    RawProjectSnapshot m_Snapshot;
    std::unordered_map<std::string, AssetLocation> m_AssetLocations;
    AssetLocation m_CoverLocation;
    bool m_HasCover = false;
    std::unordered_map<std::string, TransactionState> m_Transactions;
    mutable std::mutex m_Mutex;
    std::shared_ptr<std::mutex> m_CommitMutex;
};

bool InitializeStorage(
    const std::filesystem::path& path,
    ProjectStorageKind kind,
    std::string& error) {
    const std::filesystem::path storagePath = kind == ProjectStorageKind::DirectoryBundle
        ? NormalizeDirectoryStorePath(path)
        : path;
    std::error_code filesystemError;
    if (kind == ProjectStorageKind::DirectoryBundle) {
        if (!EnsureParentDirectory(storagePath, error)) return false;
        // Claim the root in one filesystem operation. Checking existence and
        // then creating nested directories lets two creators share a root,
        // overwrite each other's manifest, or delete each other's assets.
        if (!std::filesystem::create_directory(storagePath, filesystemError)) {
            error = filesystemError
                ? "Could not create project bundle: " + filesystemError.message()
                : "A project or directory already exists at the requested bundle path.";
            return false;
        }
        std::filesystem::create_directories(
            storagePath / kWorkingProjectAssetsDirectoryName,
            filesystemError);
        if (filesystemError) {
            std::error_code cleanupError;
            std::filesystem::remove_all(storagePath, cleanupError);
            error = "Could not create project bundle.";
            return false;
        }
        std::filesystem::create_directories(
            storagePath / kBundleStagingDirectoryName,
            filesystemError);
        if (filesystemError) {
            std::error_code cleanupError;
            std::filesystem::remove_all(storagePath, cleanupError);
            error = "Could not create project bundle staging directory.";
            return false;
        }
        return true;
    }

    if (!EnsureParentDirectory(path, error)) return false;
#if defined(_WIN32)
    const HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = "Could not claim a new portable project file.";
        return false;
    }
    DWORD written = 0;
    const bool headerWritten = WriteFile(output, kPortableHeader.data(),
        static_cast<DWORD>(kPortableHeader.size()), &written, nullptr) &&
        written == kPortableHeader.size() && FlushFileBuffers(output);
    const bool closed = CloseHandle(output) != FALSE;
#else
    const int output = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (output < 0) {
        error = "Could not claim a new portable project file.";
        return false;
    }
    const bool headerWritten = write(output, kPortableHeader.data(), kPortableHeader.size()) ==
        static_cast<ssize_t>(kPortableHeader.size()) && fsync(output) == 0;
    const bool closed = close(output) == 0;
#endif
    if (!headerWritten || !closed) {
        std::filesystem::remove(path, filesystemError);
        error = "Could not write portable project header.";
        return false;
    }
    std::filesystem::create_directories(PortableStagingRoot(path), filesystemError);
    if (filesystemError) {
        std::error_code cleanupError;
        std::filesystem::remove(path, cleanupError);
        cleanupError.clear();
        std::filesystem::remove_all(PortableStagingRoot(path), cleanupError);
        error = "Could not create portable project staging directory.";
        return false;
    }
    return true;
}

} // namespace

std::filesystem::path ResolveProjectStoreRoot(const std::filesystem::path& path) {
    return NormalizeDirectoryStorePath(path);
}

std::filesystem::path WorkingProjectDocumentPath(
    const std::filesystem::path& projectRoot) {
    return NormalizeDirectoryStorePath(projectRoot) /
        kWorkingProjectManifestName;
}

bool IsDirectoryProjectBundle(const std::filesystem::path& path) {
    const std::filesystem::path root = NormalizeDirectoryStorePath(path);
    std::error_code error;
    if (!std::filesystem::is_directory(root, error) || error) {
        return false;
    }
    error.clear();
    if (std::filesystem::is_regular_file(
            root / kWorkingProjectManifestName, error) && !error) {
        return true;
    }
    error.clear();
    if (std::filesystem::is_regular_file(
            root / kWorkingProjectPreviousManifestName, error) && !error) {
        return true;
    }
    return false;
}

bool IsPortableV3Project(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::array<char, 8> header {};
    if (!input) return false;
    input.read(header.data(), static_cast<std::streamsize>(header.size()));
    return input.good() && header == kPortableHeader;
}

ProjectStoreOpenResult CreateProjectStore(
    const std::filesystem::path& path,
    ProjectStorageKind storageKind,
    const RawProjectSnapshot& initialSnapshot) {
    ProjectStoreOpenResult result;
    std::string error;
    if (!initialSnapshot.embeddedAssets.empty()) {
        result.message = "Create an empty store, stage originals, then commit the source-set snapshot.";
        return result;
    }
    const ModelValidationResult validation = ValidateRawProjectSnapshot(initialSnapshot);
    if (!validation.valid) {
        result.message = validation.errors.empty()
            ? "Initial RAW project snapshot is invalid."
            : validation.errors.front();
        return result;
    }
    const std::filesystem::path storagePath = storageKind == ProjectStorageKind::DirectoryBundle
        ? NormalizeDirectoryStorePath(path)
        : path;
    if (!InitializeStorage(storagePath, storageKind, error)) {
        result.message = error;
        return result;
    }
    const auto cleanupInitializedStorage = [&]() {
        std::error_code cleanupError;
        if (storageKind == ProjectStorageKind::DirectoryBundle) {
            std::filesystem::remove_all(storagePath, cleanupError);
        } else {
            std::filesystem::remove(storagePath, cleanupError);
            cleanupError.clear();
            std::filesystem::remove_all(PortableStagingRoot(storagePath), cleanupError);
        }
    };
    auto store = std::make_shared<TransactionalProjectStore>(storagePath, storageKind);
    RawProjectSnapshot bootstrap = initialSnapshot;
    bootstrap.persistedStorageRevision = 0;
    const ProjectStoreTransaction transaction = store->BeginTransaction(0);
    if (!transaction) {
        store.reset();
        cleanupInitializedStorage();
        result.message = "Could not begin initial project store transaction.";
        return result;
    }

    // A new store has no committed generation yet. Commit bypasses the ordinary
    // disk-revision reader by publishing the first generation directly.
    std::string commitError;
    bool initialized = false;
    if (storageKind == ProjectStorageKind::DirectoryBundle) {
        json manifest = SerializeStoreEnvelope(
            bootstrap, storageKind, 1u, {}, nullptr);
        const std::filesystem::path manifestPath =
            storagePath / kWorkingProjectManifestName;
        std::filesystem::path temporary = manifestPath;
        temporary += ".tmp-initial";
        initialized = WriteTextFile(temporary, manifest.dump(2)) &&
            ReplaceFileAtomically(temporary, manifestPath);
    } else {
        std::ofstream output(storagePath, std::ios::binary | std::ios::app);
        std::error_code sizeError;
        const std::uint64_t manifestOffset = static_cast<std::uint64_t>(
            std::filesystem::file_size(storagePath, sizeError));
        json manifest = SerializeStoreEnvelope(
            bootstrap, storageKind, 1u, {}, nullptr);
        const std::string manifestText = manifest.dump();
        const std::vector<std::uint8_t> bytes(manifestText.begin(), manifestText.end());
        const RawEvidence::SourceIdentity identity = RawEvidence::ComputeSourceIdentity(bytes);
        initialized = output && !sizeError && AppendRecordHeader(
            output, kManifestRecordType, "manifest", identity.byteSize, identity.sha256);
        if (initialized) {
            output.write(manifestText.data(), static_cast<std::streamsize>(manifestText.size()));
            output.write(kFooterMagic.data(), static_cast<std::streamsize>(kFooterMagic.size()));
            initialized = WriteUnsigned64(output, manifestOffset) && WriteUnsigned64(output, 1u);
            output.flush();
            output.close();
            initialized = initialized && output.good();
        }
    }
    store->Abort(transaction);
    if (!initialized) {
        store.reset();
        cleanupInitializedStorage();
        result.message = "Could not publish the initial project generation.";
        return result;
    }
    bool recovered = false;
    if (!store->Load(result.snapshot, recovered, error)) {
        store.reset();
        cleanupInitializedStorage();
        result.message = error;
        return result;
    }
    result.store = std::move(store);
    NotifyProjectCatalogChanged();
    return result;
}

ProjectStoreOpenResult OpenProjectStore(const std::filesystem::path& path) {
    ProjectStoreOpenResult result;
    ProjectStorageKind kind;
    if (IsDirectoryProjectBundle(path)) {
        kind = ProjectStorageKind::DirectoryBundle;
    } else if (IsPortableV3Project(path)) {
        kind = ProjectStorageKind::PortableFile;
    } else {
        result.message =
            "This is not a current Stack working project or packed project.";
        return result;
    }
    const std::filesystem::path storagePath = kind == ProjectStorageKind::DirectoryBundle
        ? NormalizeDirectoryStorePath(path)
        : path;
    auto store = std::make_shared<TransactionalProjectStore>(storagePath, kind);
    if (!store->Load(
            result.snapshot, result.recoveredPreviousManifest, result.message)) {
        return result;
    }
    result.store = std::move(store);
    return result;
}

ProjectStoreOpenResult ConvertProjectStore(
    const ProjectStoreHandle& sourceStore,
    const RawProjectSnapshot& snapshot,
    const std::filesystem::path& destinationPath,
    ProjectStorageKind destinationKind,
    const std::function<bool(const ProjectStoreHandle&, const ProjectStoreTransaction&,
        RawProjectSnapshot&, std::string&)>& prepareCopy) {
    ProjectStoreOpenResult result;
    if (!sourceStore) {
        result.message = "Source project store is unavailable.";
        return result;
    }
    RawProjectSnapshot bootstrap = snapshot;
    bootstrap.embeddedAssets.clear();
    bootstrap.sourceSets.clear();
    bootstrap.multiFrameGraph = {};
    bootstrap.pipelineData = json::object();
    bootstrap.rawWorkspaceData = json::object();
    bootstrap.nodeBrowserThumbnails = json::array();
    bootstrap.coverThumbnailBytes.clear();
    bootstrap.sourceAssetId.clear();
    bootstrap.lifecycle.initialAssetIds.clear();
    bootstrap.activeSourceSetId.clear();
    bootstrap.activeFrameId.clear();
    bootstrap.mfdInputRevision = 0;
    bootstrap.hdrInputRevision = 0;
    bootstrap.postRecipeRevision = 0;
    bootstrap.dirtyRevision = 0;
    bootstrap.persistedStorageRevision = 0;
    const std::filesystem::path normalizedDestination =
        destinationKind == ProjectStorageKind::DirectoryBundle
        ? NormalizeDirectoryStorePath(destinationPath)
        : destinationPath;
    ProjectStoreOpenResult destination = CreateProjectStore(
        normalizedDestination, destinationKind, bootstrap);
    if (!destination) return destination;
    const auto cleanupDestination = [&]() {
        destination.store.reset();
        std::error_code cleanupError;
        if (destinationKind == ProjectStorageKind::DirectoryBundle) {
            std::filesystem::remove_all(normalizedDestination, cleanupError);
        } else {
            std::filesystem::remove(normalizedDestination, cleanupError);
            cleanupError.clear();
            std::filesystem::remove_all(
                PortableStagingRoot(normalizedDestination), cleanupError);
        }
    };
    const ProjectStoreTransaction transaction = destination.store->BeginTransaction(
        destination.snapshot.persistedStorageRevision);
    if (!transaction) {
        destination.message = "Could not begin Save As conversion transaction.";
        cleanupDestination();
        return destination;
    }
    RawProjectSnapshot converted = snapshot;
    if (destinationKind == ProjectStorageKind::DirectoryBundle) {
        for (EmbeddedAssetRecord& asset : converted.embeddedAssets) {
            if (asset.displayName.empty()) {
                asset.displayName = asset.originalFilename;
            }
            if (asset.originalFileFingerprint.empty()) {
                asset.originalFileFingerprint = asset.sha256;
            }
            if (!IsSafeManagedAssetPath(asset.projectAssetPath)) {
                const std::string managedName = SanitizeManagedFileName(
                    asset.originalFilename.empty()
                        ? asset.assetId.substr(0u, 16u)
                        : asset.originalFilename);
                asset.projectAssetPath = (
                    std::filesystem::path(kWorkingProjectAssetsDirectoryName) /
                    std::filesystem::u8path(managedName)).generic_u8string();
            }
        }
        std::string rebaseError;
        if (!RebaseSingleRawRecipeToManagedAsset(
                converted, normalizedDestination, &rebaseError)) {
            destination.store->Abort(transaction);
            destination.message = rebaseError.empty()
                ? "The copied RAW project could not bind its managed asset."
                : std::move(rebaseError);
            cleanupDestination();
            return destination;
        }
    }
    std::vector<std::string> unavailableResults;
    for (const EmbeddedAssetRecord& asset : converted.embeddedAssets) {
        std::string error;
        ProjectAssetStream stream = sourceStore->OpenAssetStream(asset.assetId, &error);
        if (!stream || !destination.store->StageAssetStream(
                transaction, *stream.stream, asset, &error)) {
            if (asset.managedRole == "bracketing-result") {
                unavailableResults.push_back(asset.assetId);
                continue;
            }
            destination.store->Abort(transaction);
            destination.message = error;
            cleanupDestination();
            return destination;
        }
    }
    for (const auto& id : unavailableResults) {
        converted.embeddedAssets.erase(std::remove_if(converted.embeddedAssets.begin(),converted.embeddedAssets.end(),
            [&](const auto& asset){return asset.assetId==id;}),converted.embeddedAssets.end());
        for(auto& set:converted.sourceSets) {
            const auto result=set.settings.find("bracketingResult");
            if(result!=set.settings.end()&&result->value("assetId",std::string())==id)set.settings.erase(result);
        }
    }
    // Stage session-owned additions in the destination transaction. Save As
    // must also work when the original store is read-only or conflicted.
    std::string preparationError;
    bool prepared = true;
    try {
        if (prepareCopy) prepared = prepareCopy(destination.store, transaction, converted, preparationError);
    } catch (const std::exception& e) {
        prepared = false;
        preparationError = e.what();
    }
    if (!prepared) {
        destination.store->Abort(transaction);
        destination.message = preparationError;
        cleanupDestination();
        return destination;
    }
    converted.persistedStorageRevision = destination.snapshot.persistedStorageRevision;
    const ProjectStoreCommitResult commit = destination.store->Commit(transaction, converted);
    if (!commit) {
        destination.store->Abort(transaction);
        destination.message = commit.message;
        cleanupDestination();
        return destination;
    }
    return OpenProjectStore(normalizedDestination);
}

} // namespace Stack::Project
