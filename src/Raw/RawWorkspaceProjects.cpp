#include "Raw/RawGraphOperation.h"
#include "Raw/RawRecipeCompatibility.h"
#include "Raw/RawWorkspace.h"
#include "Raw/RawWorkspaceManagedGraph.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Raw/MultiFrameDenoise/SharedBurst.h"
#include "Persistence/ProjectIndex.h"

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

std::filesystem::path WriteProjectCoverCache(
    const ManagedLayout& layout,
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
    const std::filesystem::path directory =
        layout.projectCoversDirectory;
    std::filesystem::create_directories(directory, error);
    if (error) return {};
    const std::filesystem::path path = directory / fileName.str();
    error.clear();
    if (std::filesystem::exists(path, error) && !error &&
        std::filesystem::file_size(path, error) == bytes.size() && !error) {
        return path;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return {};
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return output.good() ? path : std::filesystem::path();
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
    source.project = std::move(info);
}

std::vector<Stack::Project::ProjectRecord> ScanWorkspaceProjects(const ManagedLayout& layout) {
    auto roots = Stack::Project::ProjectIndex::DefaultRoots();
    roots.insert(roots.begin(), {layout.workspaceRoot, layout.projectsDirectory});
    auto records = Stack::Project::ProjectIndex::Get().Rebuild(roots, true);
    const auto inside = [](const auto& path, const auto& root) {
        if (root.empty()) return false;
        auto prefix = LowerGenericPathKey(NormalizePath(root));
        if (prefix.back() != '/') prefix += '/';
        return LowerGenericPathKey(NormalizePath(path)).rfind(prefix, 0) == 0;
    };
    records.erase(std::remove_if(records.begin(), records.end(), [&](const auto& record) {
        return !inside(record.absolutePath, layout.workspaceRoot) &&
            !inside(record.absolutePath, layout.projectsDirectory);
    }), records.end());
    return records;
}
} // namespace

std::filesystem::path BuildProjectRelativePathForSource(const SourceRecord& source) {
    // Working projects are folders in Stack's configured Projects directory;
    // the viewed source hierarchy is provenance, not storage architecture.
    return std::filesystem::path(SafeProjectStem(source)).lexically_normal();
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
    outInfo.embeddedRaw = raw.contains("managedAssetId") &&
        raw["managedAssetId"].is_string() &&
        !raw["managedAssetId"].get<std::string>().empty();
    if (outInfo.embeddedRaw) outInfo.linkedRaw = false;

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

bool DiscoverProjects(
    const ManagedLayout& layout,
    std::vector<SourceRecord>& sources,
    CancellationPredicate shouldCancel) {
    std::unordered_map<std::string, std::size_t> fingerprintToSource;
    std::unordered_map<std::string, std::size_t> absolutePathToSource;

    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (shouldCancel && shouldCancel()) {
            return false;
        }
        SourceRecord& source = sources[index];
        source.project = BuildExpectedProjectInfo(layout, source);
        if (!source.fingerprint.empty()) {
            fingerprintToSource[source.fingerprint] = index;
        }
        absolutePathToSource[LowerGenericPathKey(
            NormalizePath(source.absolutePath))] = index;
    }

    // The shared ProjectIndex is rebuilt for the rest of the application as
    // well, but a RAW Workspace owns its project population. Do not use the
    // global snapshot here. Include projects directly in the browsed folder
    // as well as its managed Projects folder, so external Save As copies are
    // discoverable only after the user browses their location.
    const auto indexedProjects = ScanWorkspaceProjects(layout);
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

        auto exactSource = absolutePathToSource.end();
        for (const Stack::Project::IndexedProjectSource& indexedSource :
             record.sources) {
            if (!indexedSource.workspaceRelativePath.empty()) {
                exactSource = absolutePathToSource.find(LowerGenericPathKey(
                    NormalizePath(layout.workspaceRoot / indexedSource.workspaceRelativePath)));
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
            AttachProjectToSource(
                sources[fingerprintIt->second],
                std::move(info),
                absolutePath,
                relativePath,
                "source-fingerprint");
        }
    }
    return true;
}

bool DiscoverSourceSetProjects(
    const ManagedLayout& layout,
    std::vector<SourceRecord>& sources,
    std::vector<SourceSetProjectCatalogEntry>& projects,
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

    // The shared ProjectIndex also contains ordinary Library projects. RAW
    // Workspace cards include the browsed folder and its managed Projects
    // folder, including multiple independent projects for one source.
    const auto indexedProjects = ScanWorkspaceProjects(layout);

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
        entry.errorMessage = indexed.errorMessage;
        entry.multiFrameProject = indexed.multiFrameProject;
        entry.bracketingProject = std::any_of(indexed.sourceSets.begin(),indexed.sourceSets.end(),[](const auto& set){return set.settings.contains("bracketing");});
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
        entry.coverThumbnailCachePath = WriteProjectCoverCache(
            layout, entry.projectId, entry.coverThumbnailBytes);

        std::unordered_map<std::string, std::vector<std::size_t>> assetSources;
        for (const Stack::Project::IndexedProjectSource& asset : indexed.sources) {
            if (asset.assetId.empty()) continue;
            const std::filesystem::path originPath = asset.workspaceRelativePath.empty()
                ? asset.originalPath : layout.workspaceRoot / asset.workspaceRelativePath;
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

        for (const Stack::Project::MultiFrameSourceSet& sourceSet : indexed.sourceSets) {
            entry.totalFrameCount += static_cast<std::uint64_t>(sourceSet.frames.size());
            if (sourceSet.inputFamily == Stack::Project::MultiFrameInputFamily::Raw) {
                ++entry.rawSetCount;
            } else {
                ++entry.rasterSetCount;
            }
            std::unordered_set<std::size_t> attachedSources;
            for (const Stack::Project::SourceSetFrame& frame : sourceSet.frames) {
                if(sourceSet.settings.contains("bracketingSelection")) {
                    const auto& selected=sourceSet.settings["bracketingSelection"];
                    if(std::find(selected.begin(),selected.end(),frame.frameId)==selected.end())continue;
                }
                const auto matches = assetSources.find(frame.assetId);
                if (matches == assetSources.end()) continue;
                for (std::size_t sourceIndex : matches->second) {
                    if (entry.referenceSourceKey.empty() &&
                        frame.frameId == sourceSet.referenceFrameId) {
                        entry.referenceSourceKey = sources[sourceIndex].relativePathKey;
                    }
                    if (!attachedSources.insert(sourceIndex).second) continue;
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
            std::vector<std::size_t> matches;
            const auto origin = indexedSource.workspaceRelativePath.empty()
                ? indexedSource.originalPath : layout.workspaceRoot / indexedSource.workspaceRelativePath;
            if (!origin.empty()) {
                const auto match = originPathToSource.find(
                    LowerGenericPathKey(NormalizePath(
                        origin)));
                if (match != originPathToSource.end() &&
                    sources[match->second].fingerprint == indexedSource.contentSha256) {
                    matches.push_back(match->second);
                }
            }
            if (matches.empty() && !indexedSource.fingerprint.empty()) {
                for (std::size_t sourceIndex = 0;
                     sourceIndex < sources.size();
                     ++sourceIndex) {
                    if (sources[sourceIndex].fingerprint ==
                        indexedSource.fingerprint) {
                        matches.push_back(sourceIndex);
                    }
                }
            }
            for (std::size_t sourceIndex : matches) {
                if (!directlyAttached.insert(sourceIndex).second) continue;
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

} // namespace Stack::RawWorkspace
