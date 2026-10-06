#include "Persistence/ProjectIndex.h"

#include "App/AppPaths.h"
#include "Persistence/ProjectStore.h"
#include "Persistence/StackBinaryFormat.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <functional>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace Stack::Project {
namespace {

std::filesystem::path NormalizePath(const std::filesystem::path& path) {
    if (path.empty()) return {};
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal();
}

std::string LowerPathKey(const std::filesystem::path& path) {
    if (path.empty()) return {};
    std::string key = NormalizePath(path).generic_u8string();
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return key;
}

std::string SanitizeStem(std::string value) {
    for (char& character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (!(std::isalnum(byte) || character == '-' || character == '_' ||
              character == '.' || character == ' ')) {
            character = '_';
        }
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '.')) {
        value.pop_back();
    }
    return value.empty() ? std::string("Project") : value;
}

std::string ShortId(const std::string& projectId) {
    std::string compact;
    for (char value : projectId) {
        if (std::isalnum(static_cast<unsigned char>(value))) {
            compact.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(value))));
        }
        if (compact.size() == 8u) break;
    }
    if (compact.size() < 8u) {
        const std::size_t hash = std::hash<std::string>{}(projectId);
        std::ostringstream stream;
        stream << std::hex << std::setw(8) << std::setfill('0') <<
            static_cast<std::uint32_t>(hash);
        compact = stream.str();
    }
    return compact;
}

std::string StableInvalidIdForPath(const std::filesystem::path& path) {
    const std::size_t hash = std::hash<std::string>{}(LowerPathKey(path));
    std::ostringstream stream;
    stream << "invalid-" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return stream.str();
}

std::string JsonString(const nlohmann::json& object, const char* key) {
    if (!object.is_object()) return {};
    const auto value = object.find(key);
    return value != object.end() && value->is_string()
        ? value->get<std::string>()
        : std::string();
}

std::uint64_t JsonUnsigned(const nlohmann::json& object, const char* key) {
    if (!object.is_object()) return 0;
    const auto value = object.find(key);
    if (value == object.end()) return 0;
    if (value->is_number_unsigned()) return value->get<std::uint64_t>();
    if (value->is_number_integer()) {
        const std::int64_t signedValue = value->get<std::int64_t>();
        return signedValue > 0 ? static_cast<std::uint64_t>(signedValue) : 0;
    }
    return 0;
}

std::int64_t JsonSigned(const nlohmann::json& object, const char* key) {
    if (!object.is_object()) return 0;
    const auto value = object.find(key);
    if (value == object.end()) return 0;
    if (value->is_number_integer()) return value->get<std::int64_t>();
    if (value->is_number_unsigned()) {
        return static_cast<std::int64_t>(std::min<std::uint64_t>(
            value->get<std::uint64_t>(),
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())));
    }
    return 0;
}

void AppendRawWorkspaceMetadata(
    const nlohmann::json& rawWorkspaceData,
    ProjectRecord& record) {
    if (!rawWorkspaceData.is_object()) return;
    record.hasRawWorkspaceData = true;
    record.rawWorkspaceMode = JsonString(
        rawWorkspaceData, "rawWorkspaceMode");
    record.rawWorkspaceReadOnlyReason = JsonString(
        rawWorkspaceData, "readOnlyReason");
    const auto recipe = rawWorkspaceData.find("rawRecipe");
    record.hasRawWorkspaceRecipe = recipe != rawWorkspaceData.end() &&
        recipe->is_object() && recipe->contains("rawRecipeVersion");

    const nlohmann::json ref = rawWorkspaceData.value(
        "rawSourceRef", nlohmann::json::object());
    if (ref.is_object()) {
        record.rawSourceRelativePathKey = JsonString(
            ref, "relativePathKey");
        record.rawSourceFingerprint = JsonString(ref, "fingerprint");
        record.rawSourceFileSizeBytes = JsonUnsigned(ref, "fileSizeBytes");
        record.rawSourceModifiedTimeTicks = JsonSigned(
            ref, "modifiedTimeTicks");
        const auto linked = ref.find("linked");
        if (linked != ref.end() && linked->is_boolean()) {
            record.rawSourceLinked = linked->get<bool>();
        }
    }
}

void AppendSnapshotSources(
    const RawProjectSnapshot& snapshot,
    std::vector<IndexedProjectSource>& sources) {
    std::unordered_set<std::string> seen;
    for (const EmbeddedAssetRecord& asset : snapshot.embeddedAssets) {
        IndexedProjectSource source;
        source.originalPath = std::filesystem::u8path(asset.originalSourcePath);
        source.workspaceRelativePath = std::filesystem::u8path(asset.workspaceRelativeSourcePath);
        source.fingerprint = asset.originalFileFingerprint;
        source.contentSha256 = asset.sha256;
        source.byteLength = asset.byteLength;
        source.assetId = asset.assetId;
        const std::string key = source.assetId + "|" +
            LowerPathKey(source.originalPath);
        if (seen.insert(key).second) {
            sources.push_back(std::move(source));
        }
    }

}

ProjectRecord RecordFromStore(
    const std::filesystem::path& path,
    ProjectStoreOpenResult opened) {
    ProjectRecord record;
    record.absolutePath = NormalizePath(path);
    record.projectId = opened.snapshot.projectId;
    record.displayName = opened.snapshot.projectName.empty()
        ? record.absolutePath.stem().u8string()
        : opened.snapshot.projectName;
    record.projectKind = opened.snapshot.projectKindHint.empty()
        ? StackBinaryFormat::kEditorProjectKind
        : opened.snapshot.projectKindHint;
    record.timestamp = opened.snapshot.timestamp;
    if (record.timestamp.empty()) record.timestamp = "Unknown";
    record.sourceWidth = opened.snapshot.sourceWidth;
    record.sourceHeight = opened.snapshot.sourceHeight;
    record.storageKind = opened.store->StorageKind();
    record.format = record.storageKind == ProjectStorageKind::PortableFile
        ? IndexedProjectFormat::PackedPortable
        : IndexedProjectFormat::CurrentBundle;
    record.coverThumbnailBytes = opened.snapshot.coverThumbnailBytes;
    record.coverState = record.coverThumbnailBytes.empty()
        ? IndexedCoverState::Missing
        : IndexedCoverState::Ready;
    record.editRevision = opened.snapshot.dirtyRevision;
    record.storageRevision = opened.snapshot.persistedStorageRevision;
    record.readOnlyRecovery = opened.store->IsReadOnlyRecovery();
    record.needsAttention = record.readOnlyRecovery;
    record.errorMessage = record.readOnlyRecovery
        ? (opened.message.empty()
            ? "The previous valid project generation was recovered read-only."
            : opened.message)
        : std::string();
    record.sourceSets = opened.snapshot.sourceSets;
    record.multiFrameProject = IsMultiFrameProjectDocument(opened.snapshot);
    AppendRawWorkspaceMetadata(opened.snapshot.rawWorkspaceData, record);
    AppendSnapshotSources(opened.snapshot, record.sources);
    if (record.hasRawWorkspaceData && !record.sources.empty()) {
        record.rawSourceEmbedded = true;
        record.rawSourceLinked = false;
    }
    return record;
}

bool HasStackExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".stack";
}

std::pair<std::uint64_t, std::int64_t> ProjectIndexSignature(
    const std::filesystem::path& candidate,
    bool directory) {
    const std::filesystem::path signaturePath = directory
        ? candidate / "project.stack"
        : candidate;
    std::error_code error;
    const std::uint64_t size = static_cast<std::uint64_t>(
        std::filesystem::file_size(signaturePath, error));
    if (error) return {};
    error.clear();
    const auto modified = std::filesystem::last_write_time(
        signaturePath, error);
    if (error) return {};
    return {
        size,
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            modified.time_since_epoch()).count()
    };
}

} // namespace

ProjectIndex& ProjectIndex::Get() {
    static ProjectIndex index;
    return index;
}

std::vector<std::filesystem::path> ProjectIndex::DefaultRoots() {
    const std::filesystem::path executable = AppPaths::GetExecutableDirectory();
    std::vector<std::filesystem::path> roots {
        AppPaths::GetProjectsDirectory(),
        AppPaths::GetLibraryDirectory(),
        executable / "Stack" / "User" / "Library",
        executable / "User" / "Library"
    };
    std::set<std::string> seen;
    std::vector<std::filesystem::path> unique;
    for (const std::filesystem::path& root : roots) {
        const std::string key = LowerPathKey(root);
        if (!key.empty() && seen.insert(key).second) {
            unique.push_back(NormalizePath(root));
        }
    }
    return unique;
}

void ProjectIndex::RebuildDefaultRoots() {
    Rebuild(DefaultRoots());
}

std::vector<ProjectRecord> ProjectIndex::Rebuild(const std::vector<std::filesystem::path>& roots,
    bool recursive) {
    std::lock_guard<std::mutex> rebuildLock(m_RebuildMutex);
    const std::vector<ProjectRecord> previousRecords = Snapshot();
    std::unordered_map<std::string, ProjectRecord> previousByPath;
    previousByPath.reserve(previousRecords.size());
    for (const ProjectRecord& record : previousRecords) {
        previousByPath.emplace(LowerPathKey(record.absolutePath), record);
    }

    std::vector<ProjectRecord> records;
    std::unordered_set<std::string> seenPaths;

    for (const std::filesystem::path& root : roots) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec) || ec) continue;
        auto parentName = root.parent_path().filename().u8string();
        std::transform(parentName.begin(), parentName.end(), parentName.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        const bool recursiveWorkspace = recursive || parentName == "closet";
        std::filesystem::recursive_directory_iterator iterator(
            root,
            std::filesystem::directory_options::skip_permission_denied,
            ec);
        const std::filesystem::recursive_directory_iterator end;
        for (; iterator != end; iterator.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            const std::filesystem::path candidate = iterator->path();
            const std::string pathKey = LowerPathKey(candidate);
            if (!seenPaths.insert(pathKey).second) {
                if (iterator->is_directory(ec) && !ec) iterator.disable_recursion_pending();
                ec.clear();
                continue;
            }

            const bool directory = iterator->is_directory(ec) && !ec;
            ec.clear();
            if (directory) {
                if (!recursiveWorkspace) iterator.disable_recursion_pending();
                auto name = candidate.filename().u8string();
                std::transform(name.begin(), name.end(), name.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (name == "trash" || name == ".staging" || name == "assets") {
                    iterator.disable_recursion_pending();
                    continue;
                }
                if (IsDirectoryProjectBundle(candidate)) iterator.disable_recursion_pending();
            }
            const bool stackFile = iterator->is_regular_file(ec) && !ec &&
                HasStackExtension(candidate);
            ec.clear();
            if (!directory && !stackFile) continue;
            const auto [signatureSize, signatureModifiedTimeTicks] =
                ProjectIndexSignature(candidate, directory);
            const auto previous = previousByPath.find(pathKey);
            if (previous != previousByPath.end() &&
                signatureSize != 0 &&
                previous->second.indexSignatureSize == signatureSize &&
                previous->second.indexSignatureModifiedTimeTicks ==
                    signatureModifiedTimeTicks) {
                records.push_back(previous->second);
                continue;
            }
            if (directory && !IsDirectoryProjectBundle(candidate)) {
                std::error_code partialError;
                const bool looksLikePartialBundle =
                    std::filesystem::exists(candidate / "assets", partialError) ||
                    std::filesystem::exists(candidate / ".staging", partialError) ||
                    std::filesystem::exists(
                        candidate / "project.stack.previous",
                        partialError);
                if (looksLikePartialBundle) {
                    iterator.disable_recursion_pending();
                    ProjectRecord invalid;
                    invalid.projectId = StableInvalidIdForPath(candidate);
                    invalid.absolutePath = NormalizePath(candidate);
                    invalid.displayName = candidate.filename().u8string();
                    invalid.format = IndexedProjectFormat::NeedsAttention;
                    invalid.coverState = IndexedCoverState::Invalid;
                    invalid.needsAttention = true;
                    invalid.indexSignatureSize = signatureSize;
                    invalid.indexSignatureModifiedTimeTicks =
                        signatureModifiedTimeTicks;
                    invalid.errorMessage =
                        "The project bundle is incomplete or has no readable manifest.";
                    records.push_back(std::move(invalid));
                }
                continue;
            }

            ProjectStoreOpenResult opened = OpenProjectStore(candidate);
            if (opened) {
                ProjectRecord record = RecordFromStore(
                    candidate, std::move(opened));
                record.indexSignatureSize = signatureSize;
                record.indexSignatureModifiedTimeTicks =
                    signatureModifiedTimeTicks;
                records.push_back(std::move(record));
                continue;
            }

            if (directory) {
                ProjectRecord invalid;
                invalid.projectId = StableInvalidIdForPath(candidate);
                invalid.absolutePath = NormalizePath(candidate);
                invalid.displayName = candidate.filename().u8string();
                invalid.format = IndexedProjectFormat::NeedsAttention;
                invalid.coverState = IndexedCoverState::Invalid;
                invalid.needsAttention = true;
                invalid.indexSignatureSize = signatureSize;
                invalid.indexSignatureModifiedTimeTicks =
                    signatureModifiedTimeTicks;
                invalid.errorMessage = opened.message.empty()
                    ? "The project bundle manifest could not be read."
                    : opened.message;
                records.push_back(std::move(invalid));
                continue;
            }

            if (stackFile) {
                ProjectRecord invalid;
                invalid.projectId = StableInvalidIdForPath(candidate);
                invalid.absolutePath = NormalizePath(candidate);
                invalid.displayName = candidate.stem().u8string();
                invalid.format = IndexedProjectFormat::NeedsAttention;
                invalid.coverState = IndexedCoverState::Invalid;
                invalid.needsAttention = true;
                invalid.indexSignatureSize = signatureSize;
                invalid.indexSignatureModifiedTimeTicks =
                    signatureModifiedTimeTicks;
                invalid.errorMessage = "The project container could not be read.";
                records.push_back(std::move(invalid));
            }
        }
    }

    std::sort(records.begin(), records.end(), [](const ProjectRecord& left, const ProjectRecord& right) {
        if (left.needsAttention != right.needsAttention) {
            return !left.needsAttention;
        }
        if (left.timestamp != right.timestamp) {
            return left.timestamp > right.timestamp;
        }
        return left.displayName < right.displayName;
    });

    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Records = records;
    return records;
}

std::vector<ProjectRecord> ProjectIndex::Snapshot() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Records;
}

std::vector<ProjectRecord> ProjectIndex::SnapshotForRoot(
    const std::filesystem::path& root) const {
    const std::string rootKey = LowerPathKey(root);
    if (rootKey.empty()) return {};

    std::string rootPrefix = rootKey;
    if (rootPrefix.back() != '/') rootPrefix.push_back('/');

    std::vector<ProjectRecord> records;
    std::lock_guard<std::mutex> lock(m_Mutex);
    for (const ProjectRecord& record : m_Records) {
        const std::string projectKey = LowerPathKey(record.absolutePath);
        if (projectKey.rfind(rootPrefix, 0) == 0) {
            records.push_back(record);
        }
    }
    return records;
}

bool ProjectIndex::FindByPath(
    const std::filesystem::path& path,
    ProjectRecord& outRecord) const {
    const std::string key = LowerPathKey(path);
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found = std::find_if(
        m_Records.begin(), m_Records.end(), [&](const ProjectRecord& record) {
            return LowerPathKey(record.absolutePath) == key;
        });
    if (found == m_Records.end()) return false;
    outRecord = *found;
    return true;
}

std::filesystem::path ProjectIndex::BuildUniqueProjectPath(
    const std::filesystem::path& root,
    const std::string& displayName,
    const std::string& projectId) {
    const std::string stem = SanitizeStem(displayName) + "--" + ShortId(projectId);
    std::filesystem::path candidate = root / stem;
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec) && !ec) return candidate;
    for (unsigned int suffix = 2; suffix < 10000; ++suffix) {
        candidate = root / (stem + "-" + std::to_string(suffix));
        ec.clear();
        if (!std::filesystem::exists(candidate, ec) && !ec) return candidate;
    }
    return root / (stem + "-" + ShortId(GenerateStableUuid()));
}

bool ProjectIndex::CloneVersion(
    const std::filesystem::path& sourceProject,
    const std::filesystem::path& destinationRoot,
    std::filesystem::path& outProjectPath,
    std::string* errorMessage) {
    ProjectStoreOpenResult opened = OpenProjectStore(sourceProject);
    if (!opened) {
        if (errorMessage) {
            *errorMessage = opened.message.empty()
                ? "Only current managed projects can be versioned."
                : opened.message;
        }
        return false;
    }

    std::string baseName = opened.snapshot.projectName.empty()
        ? sourceProject.stem().u8string()
        : opened.snapshot.projectName;
    const std::size_t versionMarker = baseName.rfind(" Version ");
    if (versionMarker != std::string::npos) {
        const std::string tail = baseName.substr(versionMarker + 9u);
        if (!tail.empty() && std::all_of(tail.begin(), tail.end(), [](unsigned char value) {
                return std::isdigit(value) != 0;
            })) {
            baseName.erase(versionMarker);
        }
    }

    unsigned int nextVersion = 2;
    // Version numbering is local to the destination root. A project in a
    // different workspace must not consume a version number here.
    const std::vector<ProjectRecord> existing = SnapshotForRoot(destinationRoot);
    for (const ProjectRecord& record : existing) {
        const std::string prefix = baseName + " Version ";
        if (record.displayName.rfind(prefix, 0) != 0) continue;
        try {
            nextVersion = std::max(
                nextVersion,
                static_cast<unsigned int>(std::stoul(
                    record.displayName.substr(prefix.size()))) + 1u);
        } catch (...) {
        }
    }

    RawProjectSnapshot clone = opened.snapshot;
    clone.projectId = GenerateStableUuid();
    clone.projectName = baseName + " Version " + std::to_string(nextVersion);
    clone.lifecycle.creationOrigin = ProjectCreationOrigin::Manual;
    clone.lifecycle.cleanupWhenUntouched = false;
    clone.lifecycle.explicitlyRetained = true;
    clone.lifecycle.autoCreatedAtDirtyRevision = 0;
    clone.adoptedFrom.clear();
    clone.dirtyRevision = 0;
    clone.persistedStorageRevision = 0;

    outProjectPath = BuildUniqueProjectPath(
        destinationRoot, clone.projectName, clone.projectId);
    ProjectStoreOpenResult converted = ConvertProjectStore(
        opened.store,
        clone,
        outProjectPath,
        ProjectStorageKind::DirectoryBundle);
    if (!converted) {
        if (errorMessage) {
            *errorMessage = converted.message.empty()
                ? "The new project version could not be committed."
                : converted.message;
        }
        outProjectPath.clear();
        return false;
    }

    if (errorMessage) errorMessage->clear();
    return true;
}

} // namespace Stack::Project
