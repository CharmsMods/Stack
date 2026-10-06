#pragma once

#include "Persistence/RawProjectModel.h"

#include <filesystem>
#include <functional>
#include <istream>
#include <memory>
#include <string>
#include <vector>

namespace Stack::Project {

enum class ProjectStoreCommitStatus {
    Committed,
    Conflict,
    InvalidSnapshot,
    IoFailure,
    ReadOnlyRecovery
};

struct ProjectStoreTransaction {
    std::string transactionId;
    std::uint64_t expectedStorageRevision = 0;

    explicit operator bool() const { return !transactionId.empty(); }
};

struct ProjectStoreCommitResult {
    ProjectStoreCommitStatus status = ProjectStoreCommitStatus::IoFailure;
    std::uint64_t committedStorageRevision = 0;
    std::string message;

    explicit operator bool() const {
        return status == ProjectStoreCommitStatus::Committed;
    }
};

struct ProjectAssetStream {
    std::unique_ptr<std::istream> stream;
    std::uint64_t byteLength = 0;

    explicit operator bool() const { return stream != nullptr && stream->good(); }
};

struct ProjectStoreOpenResult {
    std::shared_ptr<class ProjectStore> store;
    RawProjectSnapshot snapshot;
    bool recoveredPreviousManifest = false;
    std::string message;

    explicit operator bool() const { return store != nullptr; }
};

class ProjectStore {
public:
    virtual ~ProjectStore() = default;

    virtual ProjectStorageKind StorageKind() const = 0;
    virtual const std::filesystem::path& StoragePath() const = 0;
    virtual std::uint64_t StorageRevision() const = 0;
    // Read the committed generation from storage without loading image assets.
    virtual bool ReadStorageRevision(std::uint64_t& revision, std::string& error) const = 0;
    virtual bool IsReadOnlyRecovery() const = 0;

    virtual ProjectStoreTransaction BeginTransaction(
        std::uint64_t expectedStorageRevision) = 0;

    virtual bool StageAssetFile(
        const ProjectStoreTransaction& transaction,
        const std::filesystem::path& sourcePath,
        MultiFrameInputFamily inputFamily,
        const json& captureMetadataSummary,
        EmbeddedAssetRecord& record,
        std::string* errorMessage = nullptr) = 0;

    virtual bool StageAssetStream(
        const ProjectStoreTransaction& transaction,
        std::istream& source,
        const EmbeddedAssetRecord& expectedRecord,
        std::string* errorMessage = nullptr) = 0;

    virtual ProjectStoreCommitResult Commit(
        const ProjectStoreTransaction& transaction,
        const RawProjectSnapshot& snapshot) = 0;

    virtual void Abort(const ProjectStoreTransaction& transaction) = 0;

    virtual ProjectAssetStream OpenAssetStream(
        const std::string& assetId,
        std::string* errorMessage = nullptr) const = 0;

    virtual bool CopyAssetToFile(
        const std::string& assetId,
        const std::filesystem::path& destinationPath,
        std::string* errorMessage = nullptr) const = 0;

    virtual bool Verify(
        const RawProjectSnapshot& snapshot,
        std::vector<std::string>* errors = nullptr) const = 0;

    virtual bool Optimize(std::string* errorMessage = nullptr) = 0;
};

using ProjectStoreHandle = std::shared_ptr<ProjectStore>;

bool IsDirectoryProjectBundle(const std::filesystem::path& path);
bool IsPortableV3Project(const std::filesystem::path& path);
std::filesystem::path ResolveProjectStoreRoot(const std::filesystem::path& path);
std::filesystem::path WorkingProjectDocumentPath(const std::filesystem::path& projectRoot);

ProjectStoreOpenResult CreateProjectStore(
    const std::filesystem::path& path,
    ProjectStorageKind storageKind,
    const RawProjectSnapshot& initialSnapshot);

ProjectStoreOpenResult OpenProjectStore(const std::filesystem::path& path);

ProjectStoreOpenResult ConvertProjectStore(
    const ProjectStoreHandle& sourceStore,
    const RawProjectSnapshot& snapshot,
    const std::filesystem::path& destinationPath,
    ProjectStorageKind destinationKind,
    const std::function<bool(const ProjectStoreHandle&, const ProjectStoreTransaction&,
        RawProjectSnapshot&, std::string&)>& prepareCopy = {});

} // namespace Stack::Project
