#include "Persistence/ProjectStore.h"

#include "Raw/RawTechnicalEvidence.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Stack::Project {
namespace {

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

constexpr const char* kBundleManifestName = "project.stackmanifest";
constexpr const char* kBundlePreviousManifestName = "project.stackmanifest.previous";
constexpr const char* kBundleMediaDirectoryName = "media";
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
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    return !error;
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
            location["relativePath"] = entry.second.relativePath.generic_string();
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
                coverLocation->relativePath.generic_string();
        }
    }
    value["_store"] = std::move(storage);
    value["persistedStorageRevision"] = generation;
    return value;
}

bool SameLogicalProjectSnapshot(
    const RawProjectSnapshot& left,
    const RawProjectSnapshot& right) {
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
    std::string& error) {
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
        location.relativePath = iterator.value().value("relativePath", std::string());
        locations.emplace(iterator.key(), std::move(location));
    }
    hasCover = false;
    const auto cover = storage.find("coverThumbnail");
    if (cover != storage.end() && cover->is_object()) {
        coverLocation.payloadOffset = cover->value("payloadOffset", 0ull);
        coverLocation.payloadLength = cover->value("payloadLength", 0ull);
        coverLocation.checksum = cover->value("checksum", std::string());
        coverLocation.relativePath = cover->value("relativePath", std::string());
        hasCover = true;
    }
    return true;
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
        : m_Path(std::move(path)), m_Kind(kind) {}

    ProjectStorageKind StorageKind() const override { return m_Kind; }
    const std::filesystem::path& StoragePath() const override { return m_Path; }
    std::uint64_t StorageRevision() const override { return m_StorageRevision; }
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
            m_HasCover = hasCover;
            if (hasCover && !LoadCoverThumbnail(snapshot.coverThumbnailBytes, loadError)) {
                return false;
            }
            return true;
        };

        if (m_Kind == ProjectStorageKind::DirectoryBundle) {
            json currentManifest;
            std::string currentError;
            if (ReadJsonFile(m_Path / kBundleManifestName, currentManifest, currentError) &&
                applyManifest(currentManifest, currentError)) {
                m_Snapshot = snapshot;
                return true;
            }

            json previousManifest;
            std::string previousError;
            if (!ReadJsonFile(
                    m_Path / kBundlePreviousManifestName,
                    previousManifest,
                    previousError) ||
                !applyManifest(previousManifest, previousError)) {
                error = "Current bundle manifest is invalid (" + currentError +
                    "); previous manifest recovery also failed (" + previousError + ").";
                return false;
            }
            recoveredPrevious = true;
            m_ReadOnlyRecovery = true;
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
        record.originalFileName = sourcePath.filename().string();
        record.originalExtension = sourcePath.extension().string();
        record.inputFamily = inputFamily;
        record.captureMetadataSummary = captureMetadataSummary;
        record.informationalOriginPath = sourcePath.string();

        std::lock_guard<std::mutex> lock(m_Mutex);
        TransactionState* state = FindTransaction(transaction, error);
        if (!state) return Finish(errorMessage, error, false);
        if (m_AssetLocations.find(record.assetId) != m_AssetLocations.end() ||
            state->assets.find(record.assetId) != state->assets.end()) {
            return Finish(errorMessage, std::string(), true);
        }
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
        const std::filesystem::path stagedPath =
            state->stagingDirectory / FileNameForStagedAsset();
        std::ofstream output(stagedPath, std::ios::binary | std::ios::trunc);
        std::uint64_t copied = 0;
        if (!output) {
            error = "Could not create the project staging file for " +
                expectedRecord.assetId + ".";
            return Finish(errorMessage, error, false);
        }
        if (!CopyStream(source, output, &copied, expectedRecord.byteLength) ||
            copied != expectedRecord.byteLength) {
            error = "Could not stream the source asset into project staging (" +
                expectedRecord.assetId + "; expected " +
                std::to_string(expectedRecord.byteLength) + " bytes, copied " +
                std::to_string(copied) + ").";
            return Finish(errorMessage, error, false);
        }
        output.close();
        if (!output.good() || !VerifyFileIdentity(stagedPath, expectedRecord, error)) {
            return Finish(errorMessage, error, false);
        }
        state->assets.emplace(
            expectedRecord.assetId, StagedAsset { expectedRecord, stagedPath });
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
        std::filesystem::path temporary = destinationPath;
        temporary += ".tmp-" + GenerateStableUuid();
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        std::uint64_t copied = 0;
        if (!output || !CopyStream(*source.stream, output, &copied, source.byteLength) ||
            copied != source.byteLength) {
            error = "Could not copy embedded project asset.";
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
        const std::uint64_t expectedSourceRevision = m_StorageRevision;
        if (expectedSourceRevision == std::numeric_limits<std::uint64_t>::max()) {
            return Finish(errorMessage, "The project storage revision is exhausted.", false);
        }
        RawProjectSnapshot bootstrap = m_Snapshot;
        bootstrap.embeddedAssets.clear();
        bootstrap.sourceSets.clear();
        bootstrap.activeSourceSetId.clear();
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
        for (const EmbeddedAssetRecord& asset : m_Snapshot.embeddedAssets) {
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
        RawProjectSnapshot compactSnapshot = m_Snapshot;
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
        m_AssetLocations.clear();
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
        return { ProjectStoreCommitStatus::Committed, newRevision, std::string() };
    }

    std::filesystem::path StagingRoot() const {
        return m_Kind == ProjectStorageKind::DirectoryBundle
            ? m_Path / kBundleStagingDirectoryName
            : PortableStagingRoot(m_Path);
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
            if (!ReadJsonFile(m_Path / kBundleManifestName, manifest, error)) return false;
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
        const std::filesystem::path mediaRoot = m_Path / kBundleMediaDirectoryName;
        std::filesystem::create_directories(mediaRoot, filesystemError);
        if (filesystemError) {
            error = "Could not create bundle media directory.";
            return false;
        }
        auto newLocations = m_AssetLocations;
        for (const auto& entry : state.assets) {
            const EmbeddedAssetRecord& asset = entry.second.record;
            const std::filesystem::path relative =
                std::filesystem::path(kBundleMediaDirectoryName) / FileNameForAsset(asset);
            const std::filesystem::path destination = m_Path / relative;
            if (std::filesystem::exists(destination, filesystemError)) {
                std::filesystem::remove(entry.second.stagingPath, filesystemError);
            } else if (!MoveFileWithoutOverwrite(entry.second.stagingPath, destination)) {
                error = "Could not publish staged asset into the project bundle.";
                return false;
            }
            newLocations[asset.assetId] = AssetLocation {
                0u, asset.byteLength, asset.sha256, relative
            };
        }

        AssetLocation coverLocation;
        bool hasCover = !snapshot.coverThumbnailBytes.empty();
        if (hasCover) {
            const RawEvidence::SourceIdentity identity = RawEvidence::ComputeSourceIdentity(
                std::vector<std::uint8_t>(snapshot.coverThumbnailBytes.begin(),
                                          snapshot.coverThumbnailBytes.end()));
            const std::filesystem::path relative =
                std::filesystem::path("cover") / (identity.sha256 + ".thumbnail");
            const std::filesystem::path coverPath = m_Path / relative;
            std::filesystem::create_directories(coverPath.parent_path(), filesystemError);
            if (filesystemError) {
                error = "Could not create bundle cover directory.";
                return false;
            }
            if (!std::filesystem::exists(coverPath, filesystemError)) {
                std::filesystem::path temporary = coverPath;
                temporary += ".tmp-" + state.stagingDirectory.filename().string();
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!output) {
                    error = "Could not stage project cover thumbnail.";
                    return false;
                }
                output.write(
                    reinterpret_cast<const char*>(snapshot.coverThumbnailBytes.data()),
                    static_cast<std::streamsize>(snapshot.coverThumbnailBytes.size()));
                output.close();
                if (!output.good() || !MoveFileWithoutOverwrite(temporary, coverPath)) {
                    error = "Could not publish project cover thumbnail.";
                    return false;
                }
            }
            coverLocation = AssetLocation {
                0u, static_cast<std::uint64_t>(snapshot.coverThumbnailBytes.size()),
                identity.sha256, relative
            };
        }

        const json manifest = SerializeStoreEnvelope(
            snapshot, m_Kind, revision, newLocations,
            hasCover ? &coverLocation : nullptr);
        const std::filesystem::path manifestPath = m_Path / kBundleManifestName;
        std::filesystem::path temporary = manifestPath;
        temporary += ".tmp-" + state.stagingDirectory.filename().string();
        if (!WriteTextFile(temporary, manifest.dump(2))) {
            error = "Could not write staged bundle manifest.";
            return false;
        }
        if (std::filesystem::exists(manifestPath, filesystemError)) {
            const std::filesystem::path previous = m_Path / kBundlePreviousManifestName;
            std::filesystem::path previousTemporary = previous;
            previousTemporary += ".tmp-" + state.stagingDirectory.filename().string();
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
};

bool InitializeStorage(
    const std::filesystem::path& path,
    ProjectStorageKind kind,
    std::string& error) {
    std::error_code filesystemError;
    if (kind == ProjectStorageKind::DirectoryBundle) {
        if (std::filesystem::exists(path, filesystemError)) {
            error = "A project or directory already exists at the requested bundle path.";
            return false;
        }
        std::filesystem::create_directories(path / kBundleMediaDirectoryName, filesystemError);
        if (filesystemError) {
            error = "Could not create project bundle.";
            return false;
        }
        std::filesystem::create_directories(path / kBundleStagingDirectoryName, filesystemError);
        if (filesystemError) {
            std::error_code cleanupError;
            std::filesystem::remove_all(path, cleanupError);
            error = "Could not create project bundle staging directory.";
            return false;
        }
        return true;
    }

    if (!EnsureParentDirectory(path, error)) return false;
    if (std::filesystem::exists(path, filesystemError)) {
        error = "A project already exists at the requested portable path.";
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Could not create portable project.";
        return false;
    }
    output.write(kPortableHeader.data(), static_cast<std::streamsize>(kPortableHeader.size()));
    output.close();
    if (!output.good()) {
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

bool IsDirectoryProjectBundle(const std::filesystem::path& path) {
    std::error_code error;
    if (LowerAscii(path.extension().string()) != ".stackbundle" ||
        !std::filesystem::is_directory(path, error) || error) {
        return false;
    }
    error.clear();
    if (std::filesystem::is_regular_file(path / kBundleManifestName, error) && !error) {
        return true;
    }
    error.clear();
    return std::filesystem::is_regular_file(
        path / kBundlePreviousManifestName, error) && !error;
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
    if (!InitializeStorage(path, storageKind, error)) {
        result.message = error;
        return result;
    }
    const auto cleanupInitializedStorage = [&]() {
        std::error_code cleanupError;
        if (storageKind == ProjectStorageKind::DirectoryBundle) {
            std::filesystem::remove_all(path, cleanupError);
        } else {
            std::filesystem::remove(path, cleanupError);
            cleanupError.clear();
            std::filesystem::remove_all(PortableStagingRoot(path), cleanupError);
        }
    };
    auto store = std::make_shared<TransactionalProjectStore>(path, storageKind);
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
        const std::filesystem::path manifestPath = path / kBundleManifestName;
        std::filesystem::path temporary = manifestPath;
        temporary += ".tmp-initial";
        initialized = WriteTextFile(temporary, manifest.dump(2)) &&
            ReplaceFileAtomically(temporary, manifestPath);
    } else {
        std::ofstream output(path, std::ios::binary | std::ios::app);
        std::error_code sizeError;
        const std::uint64_t manifestOffset = static_cast<std::uint64_t>(
            std::filesystem::file_size(path, sizeError));
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
        result.message = "The path is not a RAW Workspace v3 project store.";
        return result;
    }
    auto store = std::make_shared<TransactionalProjectStore>(path, kind);
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
    ProjectStorageKind destinationKind) {
    ProjectStoreOpenResult result;
    if (!sourceStore) {
        result.message = "Source project store is unavailable.";
        return result;
    }
    RawProjectSnapshot bootstrap = snapshot;
    bootstrap.embeddedAssets.clear();
    bootstrap.sourceSets.clear();
    bootstrap.activeSourceSetId.clear();
    bootstrap.dirtyRevision = 0;
    bootstrap.persistedStorageRevision = 0;
    ProjectStoreOpenResult destination = CreateProjectStore(
        destinationPath, destinationKind, bootstrap);
    if (!destination) return destination;
    const auto cleanupDestination = [&]() {
        destination.store.reset();
        std::error_code cleanupError;
        if (destinationKind == ProjectStorageKind::DirectoryBundle) {
            std::filesystem::remove_all(destinationPath, cleanupError);
        } else {
            std::filesystem::remove(destinationPath, cleanupError);
            cleanupError.clear();
            std::filesystem::remove_all(
                PortableStagingRoot(destinationPath), cleanupError);
        }
    };
    const ProjectStoreTransaction transaction = destination.store->BeginTransaction(
        destination.snapshot.persistedStorageRevision);
    if (!transaction) {
        destination.message = "Could not begin Save As conversion transaction.";
        cleanupDestination();
        return destination;
    }
    for (const EmbeddedAssetRecord& asset : snapshot.embeddedAssets) {
        std::string error;
        ProjectAssetStream stream = sourceStore->OpenAssetStream(asset.assetId, &error);
        if (!stream || !destination.store->StageAssetStream(
                transaction, *stream.stream, asset, &error)) {
            destination.store->Abort(transaction);
            destination.message = error;
            cleanupDestination();
            return destination;
        }
    }
    RawProjectSnapshot converted = snapshot;
    converted.persistedStorageRevision = destination.snapshot.persistedStorageRevision;
    const ProjectStoreCommitResult commit = destination.store->Commit(transaction, converted);
    if (!commit) {
        destination.store->Abort(transaction);
        destination.message = commit.message;
        cleanupDestination();
        return destination;
    }
    return OpenProjectStore(destinationPath);
}

} // namespace Stack::Project
