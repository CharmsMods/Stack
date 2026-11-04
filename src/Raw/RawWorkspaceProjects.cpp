#include "Raw/Bracketing/SourceSetIdentity.h"
#include <set>
#include "Raw/RawGraphOperation.h"
#include "Raw/RawRecipeCompatibility.h"
#include "Raw/RawWorkspace.h"
#include "Raw/RawSourceFolders.h"
#include "Raw/RawGalleryCoverCache.h"
#include "Raw/RawWorkspaceManagedGraph.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Raw/MultiFrameDenoise/SharedBurst.h"
#include "Persistence/ProjectIndex.h"
#include "Persistence/BracketSourceReferences.h"
#include "Persistence/AutoBracketProvenance.h"
#include "Utils/AtomicExportFile.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace Stack::RawWorkspace {
namespace {

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::filesystem::path NormalizePath(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal();
}

std::int64_t FileTimeTicks(const std::filesystem::file_time_type& time) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        time.time_since_epoch()).count();
}

std::int64_t ProjectFileTimeTicks(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(path, ec);
    return ec ? 0 : FileTimeTicks(writeTime);
}

std::string GenericPathKey(const std::filesystem::path& path) {
    return path.generic_u8string();
}

std::string LowerGenericPathKey(const std::filesystem::path& path) {
    return ToLowerAscii(GenericPathKey(path));
}

std::string JsonStringOrDefault(
    const nlohmann::json& object,
    const char* key,
    const std::string& fallback = {}) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
}

std::uintmax_t JsonUintMaxOrDefault(
    const nlohmann::json& object,
    const char* key,
    std::uintmax_t fallback = 0) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return fallback;
    }
    if (it->is_number_unsigned()) {
        return it->get<std::uintmax_t>();
    }
    if (it->is_number_integer()) {
        const auto signedValue = it->get<std::int64_t>();
        return signedValue >= 0 ? static_cast<std::uintmax_t>(signedValue) : fallback;
    }
    return fallback;
}

std::int64_t JsonInt64OrDefault(
    const nlohmann::json& object,
    const char* key,
    std::int64_t fallback = 0) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return fallback;
    }
    if (it->is_number_integer()) {
        return it->get<std::int64_t>();
    }
    if (it->is_number_unsigned()) {
        const auto unsignedValue = it->get<unsigned long long>();
        if (unsignedValue <= static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max())) {
            return static_cast<std::int64_t>(unsignedValue);
        }
    }
    return fallback;
}

std::string SafeProjectStem(const SourceRecord& source) {
    std::string stem = source.stem.empty() ? source.fileName : source.stem;
    for (char& ch : stem) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (!(std::isalnum(uch) || ch == '_' || ch == '-' || ch == '.')) {
            ch = '_';
        }
    }
    return stem.empty() ? std::string("raw_project") : stem;
}

ProjectStatus StatusForProjectInfo(const ProjectInfo& info) {
    if (info.embeddedRaw) {
        return ProjectStatus::Embedded;
    }
    return ProjectStatus::Existing;
}

ProjectInfo ProjectInfoFromIndex(
    const Stack::Project::ProjectRecord& record) {
    ProjectInfo info;
    info.mode = RawProjectModeFromString(record.rawWorkspaceMode);
    info.sourceRelativePathKey = record.rawSourceRelativePathKey;
    info.sourceFingerprint = record.rawSourceFingerprint;
    info.sourceFileSizeBytes = record.rawSourceFileSizeBytes;
    info.sourceModifiedTimeTicks = record.rawSourceModifiedTimeTicks;
    info.linkedRaw = record.rawSourceLinked;
    info.embeddedRaw = record.rawSourceEmbedded;
    if (info.embeddedRaw) info.linkedRaw = false;
    info.readOnlyReason = record.rawWorkspaceReadOnlyReason;
    if (info.mode == RawProjectMode::Unknown) {
        info.status = ProjectStatus::Invalid;
        info.errorMessage = "Project has a missing or unsupported RAW Workspace mode.";
    } else {
        info.status = StatusForProjectInfo(info);
    }
    return info;
}

} // namespace

std::filesystem::path WriteProjectCoverCache(
    const std::filesystem::path& directory,
    const std::string& projectId,
    const std::vector<unsigned char>& bytes) {
    if (projectId.empty() || bytes.empty()) return {};

    std::string safeId = projectId;
    for (char& character : safeId) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (!(std::isalnum(value) || character == '-' || character == '_')) {
            character = '_';
        }
    }
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char value : bytes) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    std::ostringstream fileName;
    fileName << safeId << '-' << std::hex << hash << ".png";

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return {};
    const std::filesystem::path path = directory / fileName.str();
    error.clear();
    const auto matches = [&] {
        std::error_code readError;
        if (std::filesystem::file_size(path,readError)!=bytes.size() || readError) return false;
        std::ifstream input(path,std::ios::binary);
        const std::vector<unsigned char> cached{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
        return cached==bytes;
    };
    if (matches()) return path;
    // Covers are rebuildable content-addressed files. Publish complete bytes
    // so a concurrent thumbnail reader cannot latch a half-written PNG.
    Stack::FileSave::ExportFileTarget target;
    std::string failure;
    if (Stack::FileSave::PrepareExportFileTarget(path,{}, {".png"},target,failure) &&
        Stack::FileSave::WriteExportFileAtomically(target,bytes,failure)) return path;
    return matches() ? path : std::filesystem::path{};

}

namespace {
std::shared_ptr<const Stack::Project::ProjectRecord> GalleryProjectMetadata(
    const Stack::Project::ProjectRecord& indexed,
    const std::filesystem::path& sourceRoot) {
    auto metadata = std::make_shared<Stack::Project::ProjectRecord>(indexed);
    // Accepted cards already own their cover/cache and source-set summary.
    // Retain no second encoded image or potentially large authored settings.
    std::vector<unsigned char>().swap(metadata->coverThumbnailBytes);
    std::vector<Stack::Project::MultiFrameSourceSet>().swap(metadata->sourceSets);
    for (auto& source : metadata->sources) {
        if (!source.workspaceRelativePath.empty())
            source.originalPath = NormalizePath(sourceRoot / source.workspaceRelativePath);
    }
    return metadata;
}

void AttachProjectToSource(
    SourceRecord& source,
    ProjectInfo info,
    std::filesystem::path actualAbsolutePath,
    std::filesystem::path actualRelativePath,
    std::string associationReason) {
    info.absolutePath = NormalizePath(actualAbsolutePath);
    info.relativePath = actualRelativePath.lexically_normal();
    info.projectModifiedTimeTicks = ProjectFileTimeTicks(info.absolutePath);
    if (info.status == ProjectStatus::Unknown ||
        info.status == ProjectStatus::NoProject ||
        info.status == ProjectStatus::Existing ||
        info.status == ProjectStatus::Embedded) {
        info.status = StatusForProjectInfo(info);
    }
    info.associationReason = std::move(associationReason);
    // Choose consistently when several versions use this source. The newest
    // committed project wins, with a stable path tie-breaker.
    if ((source.project.status == ProjectStatus::Existing || source.project.status == ProjectStatus::Embedded) &&
        (source.project.projectModifiedTimeTicks > info.projectModifiedTimeTicks ||
         (source.project.projectModifiedTimeTicks == info.projectModifiedTimeTicks &&
          LowerGenericPathKey(source.project.absolutePath) < LowerGenericPathKey(info.absolutePath)))) return;
    source.project = std::move(info);
}

std::vector<Stack::Project::ProjectRecord> ScanWorkspaceProjects(const ManagedLayout& layout) {
    auto roots = Stack::Project::ProjectIndex::DefaultRoots();
    roots.insert(roots.begin(),layout.projectsDirectory);
    auto records = Stack::Project::ProjectIndex::Get().Rebuild(roots, true);
    const auto inside = [](const auto& path, const auto& root) {
        if (root.empty()) return false;
        auto prefix = LowerGenericPathKey(NormalizePath(root));
        if (prefix.back() != '/') prefix += '/';
        return LowerGenericPathKey(NormalizePath(path)).rfind(prefix, 0) == 0;
    };
    records.erase(std::remove_if(records.begin(), records.end(), [&](const auto& record) {
        return !inside(record.absolutePath,layout.projectsDirectory);
    }), records.end());
    return records;
}
} // namespace

std::filesystem::path BuildProjectRelativePathForSource(const SourceRecord& source) {
    // Working files share the root Closet's Projects directory;
    // the viewed source hierarchy is provenance, not storage architecture.
    return std::filesystem::path(SafeProjectStem(source) + ".stack");
}

ProjectInfo BuildExpectedProjectInfo(const ManagedLayout& layout, const SourceRecord& source) {
    ProjectInfo info;
    info.relativePath = BuildProjectRelativePathForSource(source);
    info.absolutePath = NormalizePath(layout.projectsDirectory / info.relativePath);
    info.status = ProjectStatus::NoProject;
    info.mode = RawProjectMode::UnifiedLayers;
    info.sourceRelativePathKey = source.relativePathKey;
    info.sourceFingerprint = source.fingerprint;
    info.sourceFileSizeBytes = source.fileSizeBytes;
    info.sourceModifiedTimeTicks = source.modifiedTimeTicks;
    info.linkedRaw = true;
    info.embeddedRaw = false;
    return info;
}

nlohmann::json BuildRawSourceRefJson(const SourceRecord& source, bool linkedRaw) {
    return {
        { "sourcePath", source.absolutePath.u8string() },
        { "relativePathKey", source.relativePathKey },
        { "fingerprint", source.fingerprint.empty() ? nlohmann::json() : nlohmann::json(source.fingerprint) },
        { "fileSizeBytes", source.fileSizeBytes },
        { "modifiedTimeTicks", source.modifiedTimeTicks },
        { "displayName", source.fileName },
        { "linked", linkedRaw }
    };
}

nlohmann::json BuildRawProjectData(
    const SourceRecord& source,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const nlohmann::json& downstreamGraph,
    RawProjectMode mode,
    bool linkedRaw) {
    nlohmann::json value = nlohmann::json::object();
    value["schema"] = "stack.rawWorkspace.project";
    value["schemaVersion"] =
        Stack::Project::kRawWorkspaceProjectSchemaVersion;
    value["rawProjectModel"] =
        Stack::Project::kRawProjectModelSourceSets;
    value["rawWorkspaceMode"] = RawProjectModeToString(mode);
    value["rawSourceRef"] = BuildRawSourceRefJson(source, linkedRaw);
    value["rawRecipe"] = Stack::RawRecipe::SerializeWorkspaceSourceRecipe(recipe);
    (void)downstreamGraph;
    return value;
}

bool ApplyRawWorkspaceDataToProjectDocument(
    const SourceRecord& source,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const nlohmann::json& downstreamGraph,
    StackBinaryFormat::ProjectDocument& document,
    RawProjectMode mode,
    bool linkedRaw) {
    document.metadata.projectKind = StackBinaryFormat::kRawProjectKind;
    document.pipelineData = downstreamGraph;
    const nlohmann::json existing = document.rawWorkspaceData.is_object()
        ? document.rawWorkspaceData
        : nlohmann::json::object();
    nlohmann::json value = BuildRawProjectData(
        source, recipe, downstreamGraph, mode, linkedRaw);
    static constexpr const char* kCurrentPreservedFields[] = {
        "projectId",
        "activeSourceSetId",
        "activeFrameId",
        "managedAssetId",
        "originalSourcePath",
        "originalFileFingerprint",
        "repairRequired",
        "repairedCopy"
    };
    for (const char* field : kCurrentPreservedFields) {
        const auto stored = existing.find(field);
        if (stored != existing.end()) {
            value[field] = *stored;
        }
    }
    document.rawWorkspaceData = std::move(value);
    return document.rawWorkspaceData.is_object();
}

bool ReadProjectInfoFromDocument(
    const StackBinaryFormat::ProjectDocument& document,
    ProjectInfo& outInfo,
    Stack::RawRecipe::RawDevelopmentRecipe* outRecipe) {
    outInfo = {};
    const nlohmann::json& raw = document.rawWorkspaceData;
    if (!raw.is_object()) {
        outInfo.status = ProjectStatus::Invalid;
        outInfo.errorMessage = "Project does not contain RAW Workspace metadata.";
        return false;
    }
    if (raw.value("schema", std::string()) !=
            "stack.rawWorkspace.project" ||
        raw.value("schemaVersion", 0) !=
            Stack::Project::kRawWorkspaceProjectSchemaVersion ||
        raw.value("rawProjectModel", std::string()) !=
            Stack::Project::kRawProjectModelSourceSets) {
        outInfo.status = ProjectStatus::Invalid;
        outInfo.errorMessage =
            "Project does not use the current RAW Workspace schema.";
        return false;
    }

    const auto modeIt = raw.find("rawWorkspaceMode");
    if (modeIt == raw.end() || !modeIt->is_string()) {
        outInfo.mode = RawProjectMode::Unknown;
        outInfo.status = ProjectStatus::Invalid;
        outInfo.errorMessage = "Project has a missing RAW Workspace mode.";
    } else {
        outInfo.mode = RawProjectModeFromString(modeIt->get<std::string>());
        if (outInfo.mode == RawProjectMode::Unknown) {
            outInfo.status = ProjectStatus::Invalid;
            outInfo.errorMessage = "Project has an unsupported RAW Workspace mode.";
        }
    }
    outInfo.readOnlyReason = raw.contains("readOnlyReason") && raw["readOnlyReason"].is_string()
        ? raw["readOnlyReason"].get<std::string>()
        : std::string();

    const nlohmann::json sourceRef = raw.value("rawSourceRef", nlohmann::json::object());
    if (sourceRef.is_object()) {
        outInfo.sourceRelativePathKey = sourceRef.value(
            "relativePathKey", std::string());
        outInfo.sourceFingerprint = JsonStringOrDefault(sourceRef, "fingerprint");
        outInfo.sourceFileSizeBytes = JsonUintMaxOrDefault(sourceRef, "fileSizeBytes");
        outInfo.sourceModifiedTimeTicks = JsonInt64OrDefault(sourceRef, "modifiedTimeTicks");
        outInfo.linkedRaw = sourceRef.value("linked", true);
    }
    const auto assetId=raw.value("managedAssetId",std::string{});
    if (!assetId.empty() && document.rawProjectSnapshot) {
        const auto* asset=Stack::Project::FindEmbeddedAsset(*document.rawProjectSnapshot,assetId);
        const bool portable=document.projectStore &&
            document.projectStore->StorageKind()==Stack::Project::ProjectStorageKind::PortableFile;
        outInfo.embeddedRaw=asset && (portable || !asset->linkedSource);
        outInfo.linkedRaw=asset && !outInfo.embeddedRaw;
    }

    if (outRecipe != nullptr) {
        const nlohmann::json recipe = raw.value(
            "rawRecipe", nlohmann::json::object());
        if (!Stack::RawRecipe::IsCanonicalWorkspaceSourceRecipeDocument(recipe)) {
            outInfo.status = ProjectStatus::Invalid;
            outInfo.errorMessage =
                "Project does not use the current RAW recipe schema.";
            return false;
        }
        *outRecipe = Stack::RawRecipe::DeserializeRecipe(recipe);
    }

    if (outInfo.status != ProjectStatus::Invalid) {
        outInfo.status = StatusForProjectInfo(outInfo);
    }
    return true;
}

namespace {

bool AssociateWorkspaceProjects(
    const ManagedLayout& layout,
    std::vector<SourceRecord>& sources,
    const std::vector<Stack::Project::ProjectRecord>& indexedProjects,
    CancellationPredicate shouldCancel) {
    std::unordered_map<std::string, std::vector<std::size_t>> fingerprintToSource;
    std::unordered_map<std::string, std::size_t> absolutePathToSource;

    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (shouldCancel && shouldCancel()) {
            return false;
        }
        SourceRecord& source = sources[index];
        source.project = BuildExpectedProjectInfo(layout, source);
        if (!source.fingerprint.empty()) {
            fingerprintToSource[source.fingerprint].push_back(index);
        }
        absolutePathToSource[LowerGenericPathKey(
            NormalizePath(source.absolutePath))] = index;
    }

    for (const Stack::Project::ProjectRecord& record : indexedProjects) {
        if (shouldCancel && shouldCancel()) {
            return false;
        }
        if (record.needsAttention ||
            record.projectKind != StackBinaryFormat::kRawProjectKind) {
            continue;
        }
        const std::filesystem::path absolutePath =
            NormalizePath(record.absolutePath);

        std::error_code relativeError;
        std::filesystem::path relativePath = std::filesystem::relative(
            absolutePath,
            layout.projectsDirectory,
            relativeError);
        if (relativeError) {
            relativePath = absolutePath.filename();
        }
        relativePath = relativePath.lexically_normal();

        if (!record.hasRawWorkspaceData) {
            continue;
        }
        ProjectInfo info = ProjectInfoFromIndex(record);
        const auto managedOwner = ResolveManagedProjectSourceFolder(absolutePath);
        const auto& sourceRoot = managedOwner.empty() ? layout.workspaceRoot : managedOwner;

        auto exactSource = absolutePathToSource.end();
        if (!info.sourceRelativePathKey.empty() && !info.sourceFingerprint.empty()) {
            auto expected = absolutePathToSource.find(LowerGenericPathKey(NormalizePath(
                sourceRoot / std::filesystem::u8path(info.sourceRelativePathKey))));
            if (expected != absolutePathToSource.end() &&
                sources[expected->second].fingerprint == info.sourceFingerprint &&
                sources[expected->second].fileSizeBytes == info.sourceFileSizeBytes) {
                exactSource = expected;
            }
        }
        for (const Stack::Project::IndexedProjectSource& indexedSource : record.sources) {
            if (exactSource != absolutePathToSource.end()) break;
            if (!indexedSource.workspaceRelativePath.empty()) {
                exactSource = absolutePathToSource.find(LowerGenericPathKey(
                    NormalizePath(sourceRoot / indexedSource.workspaceRelativePath)));
                if (exactSource != absolutePathToSource.end() &&
                    sources[exactSource->second].fingerprint == indexedSource.contentSha256) break;
                exactSource = absolutePathToSource.end();
            }
            if (!indexedSource.originalPath.empty()) {
                exactSource = absolutePathToSource.find(
                    LowerGenericPathKey(NormalizePath(
                        indexedSource.originalPath)));
                if (exactSource != absolutePathToSource.end() &&
                    sources[exactSource->second].fingerprint == indexedSource.contentSha256) break;
                exactSource = absolutePathToSource.end();
            }
        }
        if (exactSource != absolutePathToSource.end()) {
            AttachProjectToSource(
                sources[exactSource->second],
                std::move(info),
                absolutePath,
                relativePath,
                "original-source-path");
            continue;
        }

        auto fingerprintIt = info.sourceFingerprint.empty()
            ? fingerprintToSource.end()
            : fingerprintToSource.find(info.sourceFingerprint);
        if (fingerprintIt != fingerprintToSource.end()) {
            for (const auto sourceIndex : fingerprintIt->second)
                AttachProjectToSource(sources[sourceIndex], info, absolutePath,
                    relativePath, "source-fingerprint");
        }
    }
    return true;
}

bool AssociateWorkspaceSourceSetProjects(
    const ManagedLayout& layout,
    std::vector<SourceRecord>& sources,
    std::vector<SourceSetProjectCatalogEntry>& projects,
    const std::vector<Stack::Project::ProjectRecord>& indexedProjects,
    CancellationPredicate shouldCancel) {
    projects.clear();
    std::unordered_map<std::string, std::size_t> originPathToSource;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> byteLengthToSources;
    std::unordered_map<std::size_t, Stack::RawEvidence::SourceIdentity> sourceIdentities;
    for (std::size_t index = 0; index < sources.size(); ++index) {
        SourceRecord& source = sources[index];
        source.sourceSetProjectMemberships.clear();
        originPathToSource[LowerGenericPathKey(NormalizePath(source.absolutePath))] = index;
        byteLengthToSources[static_cast<std::uint64_t>(source.fileSizeBytes)].push_back(index);
    }

    const auto inspectProject = [&](const Stack::Project::ProjectRecord& indexed) {
        const std::filesystem::path candidate = indexed.absolutePath;
        // Projects is scoped by storage location, not by which editing tools
        // the document uses. Graph-only and portable projects belong here too.

        SourceSetProjectCatalogEntry entry;
        entry.projectId = indexed.projectId;
        entry.projectName = indexed.displayName;
        entry.absolutePath = NormalizePath(candidate);
        std::error_code relativeError;
        entry.relativePath = std::filesystem::relative(
            entry.absolutePath, layout.projectsDirectory, relativeError);
        if (relativeError) entry.relativePath = candidate.filename();
        entry.storageKind = indexed.storageKind;
        entry.status = indexed.readOnlyRecovery
            ? ProjectStatus::Conflict
            : ProjectStatus::Existing;
        entry.readOnlyRecovery = indexed.readOnlyRecovery;
        entry.conflict = indexed.readOnlyRecovery;
        entry.dirty = indexed.editRevision != indexed.storageRevision;
        entry.persistedStorageRevision = indexed.storageRevision;
        entry.errorMessage = indexed.errorMessage;
        entry.multiFrameProject = indexed.multiFrameProject;
        entry.bracketingProject = std::any_of(indexed.sourceSets.begin(),indexed.sourceSets.end(),[](const auto& set){return set.settings.contains("bracketing");});
        entry.hasSavedBracketingResult = indexed.hasSavedBracketingResult;
        entry.untouchedAutomaticBracket = std::any_of(indexed.sourceSets.begin(), indexed.sourceSets.end(),
            [&](const auto& set) {
                return Stack::Project::MatchesAutomaticBracketRevision(set, indexed.projectId,
                    indexed.editRevision, indexed.storageRevision) &&
                    std::find(indexed.savedBracketingSourceSetIds.begin(), indexed.savedBracketingSourceSetIds.end(),
                        set.sourceSetId) != indexed.savedBracketingSourceSetIds.end();
            });
        entry.sourceSetCount = static_cast<std::uint64_t>(indexed.sourceSets.size());
        const bool invalidBurst = std::any_of(
            indexed.sourceSets.begin(),
            indexed.sourceSets.end(),
            [](const Stack::Project::MultiFrameSourceSet& sourceSet) {
                return sourceSet.operationIntent ==
                        Stack::Project::MultiFrameOperationIntent::RawBurstDenoise &&
                    (sourceSet.operationSchemaVersion !=
                         Stack::Project::kMfdOperationSchemaVersion ||
                     sourceSet.settings.value("algorithmId", std::string()) !=
                         Raw::Mfd::kSharedBurstAlgorithmId);
            });
        if (!invalidBurst) {
            entry.coverThumbnailBytes = indexed.coverThumbnailBytes;
        }
        entry.sourceWorkspaceRoot = ResolveManagedProjectSourceFolder(entry.absolutePath);
        const auto projectLayout = entry.sourceWorkspaceRoot.empty()
            ? layout : BuildManagedLayout(entry.sourceWorkspaceRoot);
        entry.projectMetadata = GalleryProjectMetadata(indexed, projectLayout.workspaceRoot);
        entry.coverThumbnailCachePath = WriteProjectCoverCache(
            projectLayout.projectCoversDirectory, entry.projectId, entry.coverThumbnailBytes);

        std::unordered_map<std::string, std::vector<std::size_t>> assetSources;
        for (const Stack::Project::IndexedProjectSource& asset : indexed.sources) {
            if (asset.assetId.empty()) continue;
            const bool primarySource = !indexed.rawSourceRelativePathKey.empty() &&
                indexed.rawSourceFingerprint == asset.contentSha256 &&
                indexed.rawSourceFileSizeBytes == asset.byteLength;
            const std::filesystem::path originPath = primarySource
                ? projectLayout.workspaceRoot / std::filesystem::u8path(indexed.rawSourceRelativePathKey)
                : (asset.workspaceRelativePath.empty()
                    ? asset.originalPath : projectLayout.workspaceRoot / asset.workspaceRelativePath);
            if (!originPath.empty()) {
                const auto source = originPathToSource.find(
                    LowerGenericPathKey(NormalizePath(originPath)));
                if (source != originPathToSource.end() &&
                    sources[source->second].fingerprint == asset.contentSha256) {
                    assetSources[asset.assetId].push_back(source->second);
                }
            }
            if (!assetSources[asset.assetId].empty()) continue;

            const auto sameSize = byteLengthToSources.find(asset.byteLength);
            if (sameSize == byteLengthToSources.end()) continue;
            for (std::size_t sourceIndex : sameSize->second) {
                if (!sources[sourceIndex].fingerprint.empty()) {
                    if (sources[sourceIndex].fingerprint == asset.contentSha256)
                        assetSources[asset.assetId].push_back(sourceIndex);
                    continue;
                }
                auto identity = sourceIdentities.find(sourceIndex);
                if (identity == sourceIdentities.end()) {
                    identity = sourceIdentities.emplace(
                        sourceIndex,
                        Stack::RawEvidence::ComputeSourceIdentity(
                            sources[sourceIndex].absolutePath)).first;
                }
                if (identity->second.valid &&
                    identity->second.sha256 == asset.contentSha256) {
                    assetSources[asset.assetId].push_back(sourceIndex);
                }
            }
        }

        if (indexed.storageKind == Stack::Project::ProjectStorageKind::WorkingFile) {
            std::string referenceId;
            std::set<std::string> required;
            for (const auto& set : indexed.sourceSets) {
                const auto submitted=set.settings.find("bracketing");
                if (submitted!=set.settings.end()) {
                    const auto active=Stack::Project::BracketCaptureIds(*submitted);
                    required.insert(active.begin(),active.end());
                } else for (const auto& frame:set.frames) if (frame.enabled) required.insert(frame.frameId);
                for (const auto& frame:set.frames)
                    if (frame.frameId==set.referenceFrameId && referenceId.empty()) referenceId=frame.assetId;
            }
            for (const auto& asset:indexed.sources) {
                if (!asset.linked || !assetSources[asset.assetId].empty()) continue;
                const bool reference=indexed.sourceSets.empty() ? indexed.sources.size()==1 : asset.assetId==referenceId;
                const bool needed=indexed.sourceSets.empty() || std::any_of(indexed.sourceSets.begin(),indexed.sourceSets.end(),
                    [&](const auto& set){return std::any_of(set.frames.begin(),set.frames.end(),[&](const auto& frame){
                        return frame.assetId==asset.assetId && required.count(frame.frameId);});});
                if (!reference && (!needed || entry.hasSavedBracketingResult)) continue;
                auto path=asset.workspaceRelativePath.empty() ? asset.originalPath : projectLayout.workspaceRoot/asset.workspaceRelativePath;
                std::error_code ec;
                const bool existing=std::filesystem::file_size(path,ec)==asset.byteLength && !ec &&
                    Stack::RawEvidence::ComputeSourceIdentity(path,shouldCancel).sha256==asset.contentSha256;
                if (!existing) {
                    // The store also consults the master catalog for renamed bracket-backed originals.
                    auto opened=Stack::Project::OpenProjectStore(indexed.absolutePath);
                    path=opened ? opened.store->ResolveAssetFile(asset.assetId) : std::filesystem::path{};
                }
                if (path.empty()) {
                    if (needed && !entry.hasSavedBracketingResult) entry.unlinked=true;
                } else if (reference) {
                    auto source=std::make_shared<SourceRecord>();
                    source->absolutePath=path; source->sourceWorkspaceRoot=projectLayout.workspaceRoot;
                    source->relativePath=asset.workspaceRelativePath.empty() ? path.filename() : asset.workspaceRelativePath;
                    source->relativePathKey=source->relativePath.generic_u8string();
                    source->fileName=path.filename().u8string(); source->stem=path.stem().u8string();
                    source->extension=path.extension().u8string(); source->fileSizeBytes=asset.byteLength;
                    source->fingerprint=asset.contentSha256;
                    source->sourceIdentityAlgorithmVersion=kSourceIdentityAlgorithmVersion;
                    source->browsePreview = true;
                    entry.linkedOriginal=std::move(source);
                }
            }
        }

        std::vector<std::string> sourceIdentities;
        for (const Stack::Project::MultiFrameSourceSet& sourceSet : indexed.sourceSets) {
            if (sourceSet.inputFamily == Stack::Project::MultiFrameInputFamily::Raw) {
                ++entry.rawSetCount;
            } else {
                ++entry.rasterSetCount;
            }
            std::unordered_set<std::size_t> attachedSources;
            const auto selectedFrame = [&](const auto& frame) {
                const auto selected = sourceSet.settings.find("bracketingSelection");
                return selected == sourceSet.settings.end() || (selected->is_array() &&
                    std::find(selected->begin(), selected->end(), frame.frameId) != selected->end());
            };
            const auto sourceSetFrameCount = static_cast<std::uint64_t>(std::count_if(
                sourceSet.frames.begin(), sourceSet.frames.end(), selectedFrame));
            for (const Stack::Project::SourceSetFrame& frame : sourceSet.frames) {
                if (!selectedFrame(frame)) continue;
                ++entry.totalFrameCount;
                const auto asset = std::find_if(indexed.sources.begin(), indexed.sources.end(),
                    [&](const auto& candidate) { return candidate.assetId == frame.assetId; });
                if (asset != indexed.sources.end()) sourceIdentities.push_back(asset->contentSha256);
                const auto matches = assetSources.find(frame.assetId);
                if (matches == assetSources.end()) continue;
                for (std::size_t sourceIndex : matches->second) {
                    if (entry.referenceSourceKey.empty() &&
                        frame.frameId == sourceSet.referenceFrameId) {
                        entry.referenceSourceKey = sources[sourceIndex].relativePathKey;
                    }
                    if (!attachedSources.insert(sourceIndex).second) continue;
                    entry.sourceKeys.push_back(sources[sourceIndex].relativePathKey);
                    SourceSetProjectMembership membership;
                    membership.projectId = entry.projectId;
                    membership.projectName = entry.projectName;
                    membership.projectPath = entry.absolutePath;
                    membership.sourceSetId = sourceSet.sourceSetId;
                    membership.sourceSetName = sourceSet.name;
                    membership.projectIsMultiFrame =
                        entry.multiFrameProject;
                    membership.projectCoverThumbnailCachePath =
                        entry.coverThumbnailCachePath;
                    membership.hasSavedBracketingResult = std::find(
                        indexed.savedBracketingSourceSetIds.begin(), indexed.savedBracketingSourceSetIds.end(),
                        sourceSet.sourceSetId) != indexed.savedBracketingSourceSetIds.end();
                    membership.sourceSetFrameCount = sourceSetFrameCount;
                    sources[sourceIndex].sourceSetProjectMemberships.push_back(
                        std::move(membership));
                }
            }
        }
        // Single-RAW projects have managed source identity but no source-set
        // node. Attach them through the same shared index relationship so the
        // Gallery presents the project card and suppresses the unedited card.
        std::unordered_set<std::size_t> directlyAttached;
        for (const Stack::Project::IndexedProjectSource& indexedSource :
             indexed.sources) {
            // Source sets already define their selected frame relationships.
            // The direct RAW fallback must not reattach excluded captures.
            if (!indexed.sourceSets.empty()) break;
            if (!indexedSource.originalPath.empty())
                sourceIdentities.push_back(indexedSource.contentSha256);
            for (std::size_t sourceIndex : assetSources[indexedSource.assetId]) {
                if (!directlyAttached.insert(sourceIndex).second) continue;
                entry.sourceKeys.push_back(sources[sourceIndex].relativePathKey);
                const bool alreadyAttached = std::any_of(
                    sources[sourceIndex].sourceSetProjectMemberships.begin(),
                    sources[sourceIndex].sourceSetProjectMemberships.end(),
                    [&](const SourceSetProjectMembership& membership) {
                        return membership.projectId == entry.projectId;
                    });
                if (!alreadyAttached) {
                    SourceSetProjectMembership membership;
                    membership.projectId = entry.projectId;
                    membership.projectName = entry.projectName;
                    membership.projectPath = entry.absolutePath;
                    membership.sourceSetName = "RAW source";
                    membership.projectIsMultiFrame =
                        entry.multiFrameProject;
                    membership.projectCoverThumbnailCachePath =
                        entry.coverThumbnailCachePath;
                    sources[sourceIndex].sourceSetProjectMemberships.push_back(
                        std::move(membership));
                }
                if (entry.referenceSourceKey.empty()) {
                    entry.referenceSourceKey =
                        sources[sourceIndex].relativePathKey;
                }
            }
        }
        if (entry.totalFrameCount == 0 && !directlyAttached.empty()) {
            entry.totalFrameCount =
                static_cast<std::uint64_t>(directlyAttached.size());
            entry.rawSetCount = 1;
        }
        std::sort(entry.sourceKeys.begin(), entry.sourceKeys.end());
        entry.sourceKeys.erase(std::unique(entry.sourceKeys.begin(), entry.sourceKeys.end()), entry.sourceKeys.end());
        if (sourceIdentities.empty() && !indexed.rawSourceFingerprint.empty())
            sourceIdentities.push_back(indexed.rawSourceFingerprint);
        entry.sourceIdentityKey = BuildGallerySourceIdentityKey(std::move(sourceIdentities));
        projects.push_back(std::move(entry));
    };

    for (const Stack::Project::ProjectRecord& record : indexedProjects) {
        if (shouldCancel && shouldCancel()) return false;
        if (record.needsAttention) {
            SourceSetProjectCatalogEntry invalid;
            invalid.projectId = record.projectId;
            invalid.projectName = record.displayName;
            invalid.absolutePath = record.absolutePath;
            invalid.relativePath = record.absolutePath.lexically_relative(layout.projectsDirectory);
            invalid.status = ProjectStatus::Invalid;
            invalid.errorMessage = record.errorMessage;
            invalid.sourceWorkspaceRoot = ResolveManagedProjectSourceFolder(record.absolutePath);
            invalid.projectMetadata = GalleryProjectMetadata(record,
                invalid.sourceWorkspaceRoot.empty() ? layout.workspaceRoot : invalid.sourceWorkspaceRoot);
            projects.push_back(std::move(invalid));
            continue;
        }
        inspectProject(record);
    }
    std::sort(projects.begin(), projects.end(), [](const auto& lhs, const auto& rhs) {
        return ToLowerAscii(lhs.projectName) < ToLowerAscii(rhs.projectName);
    });
    return true;
}

} // namespace

bool DiscoverProjects(
    const ManagedLayout& layout,
    std::vector<SourceRecord>& sources,
    CancellationPredicate shouldCancel) {
    if (shouldCancel && shouldCancel()) return false;
    return AssociateWorkspaceProjects(
        layout, sources, ScanWorkspaceProjects(layout), std::move(shouldCancel));
}

bool DiscoverSourceSetProjects(
    const ManagedLayout& layout,
    std::vector<SourceRecord>& sources,
    std::vector<SourceSetProjectCatalogEntry>& projects,
    CancellationPredicate shouldCancel) {
    if (shouldCancel && shouldCancel()) return false;
    return AssociateWorkspaceSourceSetProjects(
        layout, sources, projects, ScanWorkspaceProjects(layout), std::move(shouldCancel));
}

bool DiscoverWorkspaceProjects(
    const ManagedLayout& layout,
    std::vector<SourceRecord>& sources,
    std::vector<SourceSetProjectCatalogEntry>& projects,
    CancellationPredicate shouldCancel,
    const nlohmann::json* scannedMasters) {
    if (shouldCancel && shouldCancel()) return false;
    // Keep discovery scoped to this operation. Both associations consume the
    // same records; another workspace cannot replace them via the global index.
    std::error_code ec;
    const auto indexedProjects = std::filesystem::is_directory(layout.projectsDirectory, ec) &&
        IsMarkedCloset(layout.dataDirectory) ? ScanWorkspaceProjects(layout) : std::vector<Stack::Project::ProjectRecord>{};
    if (shouldCancel && shouldCancel()) return false;
    if (!AssociateWorkspaceProjects(layout, sources, indexedProjects, shouldCancel) ||
        !AssociateWorkspaceSourceSetProjects(layout, sources, projects, indexedProjects, shouldCancel)) return false;
    std::set<std::string> represented;
    for (const auto& doc : indexedProjects) for (const auto& set : doc.sourceSets) {
        if (std::find(doc.savedBracketingSourceSetIds.begin(), doc.savedBracketingSourceSetIds.end(), set.sourceSetId) ==
            doc.savedBracketingSourceSetIds.end()) continue;
        auto saved=set.settings.find("bracketingResult");
        if (saved!=set.settings.end() && saved->is_object()) represented.insert(saved->value("identity",std::string{}));
    }
    nlohmann::json catalog;
    if (!scannedMasters) {
        std::ifstream stream(layout.catalogDirectory / "masters.json", std::ios::binary);
        if (!stream) return true;
        catalog=nlohmann::json::parse(stream,nullptr,false);
        if (!catalog.is_object() || !catalog.contains("sources")) return true;
        scannedMasters = &catalog["sources"];
    }
    if (!scannedMasters->is_array()) return true;
    for (const auto& master : *scannedMasters) {
        if (!master.is_object() || !master.contains("resultId") || !master["resultId"].is_string() ||
            !master.contains("relativePath") || !master["relativePath"].is_string()) continue;
        const auto id=master["resultId"].get<std::string>();
        if (!represented.insert(id).second) continue;
        const auto relative=std::filesystem::u8path(master["relativePath"].get<std::string>());
        if (relative.empty() || relative.has_root_path() || std::find(relative.begin(),relative.end(),"..")!=relative.end()) continue;
        SourceSetProjectCatalogEntry card;
        card.projectId="master:"+id;card.projectName=relative.stem().u8string();
        card.relativePath=relative;card.absolutePath=layout.workspaceRoot/relative;
        card.sourceWorkspaceRoot=layout.workspaceRoot;card.status=ProjectStatus::Existing;
        card.bracketingProject=true;card.hasSavedBracketingResult=true;
        card.standaloneMaster = true;
        const auto captures = master.find("captures");
        if (captures != master.end() && captures->is_array()) {
            std::vector<Raw::Bracketing::CaptureContentIdentity> identities;
            for (const auto& capture : *captures) {
                if (!capture.is_object() || !capture.contains("sha256") || !capture["sha256"].is_string() ||
                    !capture.contains("byteLength") || !capture["byteLength"].is_number_unsigned()) {
                    identities.clear();
                    break;
                }
                identities.push_back({capture["sha256"].get<std::string>(), capture["byteLength"].get<std::uint64_t>()});
            }
            card.masterCaptureSetIdentity = Raw::Bracketing::SourceSetIdentity(identities);
        }
        auto source = std::make_shared<SourceRecord>();
        source->sourceWorkspaceRoot = layout.workspaceRoot;
        source->absolutePath = card.absolutePath;
        source->relativePath = relative;
        source->relativePathKey = relative.generic_u8string();
        source->fileName = relative.filename().u8string();
        source->stem = relative.stem().u8string();
        source->extension = relative.extension().u8string();
        source->parentFolderKey = relative.parent_path().generic_u8string();
        source->fingerprint = master.value("fingerprint", std::string{});
        source->fileSizeBytes = master.value("fileSizeBytes", std::uintmax_t{});
        source->modifiedTimeTicks = master.value("modifiedTimeTicks", std::int64_t{});
        source->sourceIdentityAlgorithmVersion = kSourceIdentityAlgorithmVersion;
        source->project.status = ProjectStatus::NoProject;
        card.referenceSourceKey = source->relativePathKey;
        card.masterSource = std::move(source);
        projects.push_back(std::move(card));
    }
    return true;
}

} // namespace Stack::RawWorkspace
