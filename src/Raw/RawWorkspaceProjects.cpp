#include "Raw/RawWorkspace.h"
#include "Raw/RawTechnicalEvidence.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
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
    return path.generic_string();
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

std::vector<unsigned char> ReadBinaryFile(const std::filesystem::path& path, std::string* outError) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        if (outError) {
            *outError = "Failed to open " + path.string() + " for reading.";
        }
        return {};
    }
    std::vector<unsigned char> bytes;
    bytes.assign(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
    if (!in.good() && !in.eof()) {
        if (outError) {
            *outError = "Failed to read " + path.string() + ".";
        }
        bytes.clear();
    }
    return bytes;
}

bool ReadProjectDocumentForDiscovery(
    const std::filesystem::path& path,
    StackBinaryFormat::ProjectDocument& outDocument) {
    StackBinaryFormat::ProjectLoadOptions options;
    options.includeThumbnail = false;
    options.includeSourceImage = false;
    options.includePipelineData = false;
    options.includeNodeBrowserThumbnails = false;
    options.includeRawWorkspaceData = true;
    return StackBinaryFormat::ReadProjectFile(path, outDocument, options);
}

ProjectStatus StatusForProjectInfo(const ProjectInfo& info) {
    if (info.embeddedRaw) {
        return ProjectStatus::Embedded;
    }
    return ProjectStatus::Existing;
}

nlohmann::json BuildEmbeddedRawPlaceholder() {
    return {
        { "present", false },
        { "fileName", nullptr },
        { "bytes", nullptr }
    };
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

} // namespace

std::filesystem::path BuildProjectRelativePathForSource(const SourceRecord& source) {
    std::filesystem::path relative;
    if (!source.parentFolderKey.empty()) {
        relative = std::filesystem::path(source.parentFolderKey);
    }
    relative /= SafeProjectStem(source) + ".stack";
    return relative.lexically_normal();
}

ProjectInfo BuildExpectedProjectInfo(const ManagedLayout& layout, const SourceRecord& source) {
    ProjectInfo info;
    info.relativePath = BuildProjectRelativePathForSource(source);
    info.absolutePath = NormalizePath(layout.projectsDirectory / info.relativePath);
    info.status = ProjectStatus::NoProject;
    info.mode = RawProjectMode::RecipeBacked;
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
        { "sourcePath", source.absolutePath.string() },
        { "workspaceRelativePath", source.relativePathKey },
        { "relativePathKey", source.relativePathKey },
        { "fingerprint", source.fingerprint.empty() ? nlohmann::json() : nlohmann::json(source.fingerprint) },
        { "fileSizeBytes", source.fileSizeBytes },
        { "modifiedTimeTicks", source.modifiedTimeTicks },
        { "displayName", source.fileName },
        { "linked", linkedRaw },
        { "embedded", !linkedRaw }
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
    value["rawWorkspaceSchemaVersion"] = 2;
    value["rawWorkspaceMode"] = RawProjectModeToString(mode);
    value["rawSourceRef"] = BuildRawSourceRefJson(source, linkedRaw);
    value["rawRecipe"] = Stack::RawRecipe::SerializeRecipe(recipe);
    (void)downstreamGraph;
    value["managedRawSection"] = nullptr;
    value["customRawSection"] = nullptr;
    value["readOnlyReason"] = nullptr;
    value["embeddedRaw"] = BuildEmbeddedRawPlaceholder();
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
    nlohmann::json value = document.rawWorkspaceData.is_object()
        ? document.rawWorkspaceData
        : nlohmann::json::object();
    value["schema"] = "stack.rawWorkspace.project";
    value["rawWorkspaceSchemaVersion"] = 2;
    value["rawWorkspaceMode"] = RawProjectModeToString(mode);
    value["rawSourceRef"] = BuildRawSourceRefJson(source, linkedRaw);
    value["rawRecipe"] = Stack::RawRecipe::SerializeRecipe(recipe);
    value.erase("downstreamGraph");
    if (!value.contains("managedRawSection")) {
        value["managedRawSection"] = nullptr;
    }
    if (!value.contains("customRawSection")) {
        value["customRawSection"] = nullptr;
    }
    if (!value.contains("readOnlyReason")) {
        value["readOnlyReason"] = nullptr;
    }
    if (linkedRaw || !value.contains("embeddedRaw")) {
        value["embeddedRaw"] = BuildEmbeddedRawPlaceholder();
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
            "relativePathKey",
            sourceRef.value("workspaceRelativePath", std::string()));
        outInfo.sourceFingerprint = JsonStringOrDefault(sourceRef, "fingerprint");
        outInfo.sourceFileSizeBytes = JsonUintMaxOrDefault(sourceRef, "fileSizeBytes");
        outInfo.sourceModifiedTimeTicks = JsonInt64OrDefault(sourceRef, "modifiedTimeTicks");
        outInfo.linkedRaw = sourceRef.value("linked", true);
        outInfo.embeddedRaw = sourceRef.value("embedded", false);
    }

    const nlohmann::json embeddedRaw = raw.value("embeddedRaw", nlohmann::json::object());
    if (embeddedRaw.is_object() && embeddedRaw.value("present", false)) {
        outInfo.embeddedRaw = true;
        outInfo.linkedRaw = false;
    }

    if (outRecipe != nullptr) {
        *outRecipe = Stack::RawRecipe::DeserializeRecipe(raw.value("rawRecipe", nlohmann::json::object()));
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
    std::unordered_map<std::string, std::size_t> expectedPathToSource;
    std::unordered_map<std::string, std::size_t> relativeKeyToSource;
    std::unordered_map<std::string, std::size_t> fingerprintToSource;

    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (shouldCancel && shouldCancel()) {
            return false;
        }
        SourceRecord& source = sources[index];
        source.project = BuildExpectedProjectInfo(layout, source);
        expectedPathToSource[LowerGenericPathKey(source.project.relativePath)] = index;
        relativeKeyToSource[ToLowerAscii(source.relativePathKey)] = index;
        if (!source.fingerprint.empty()) {
            fingerprintToSource[source.fingerprint] = index;
        }
    }

    std::error_code ec;
    if (!std::filesystem::exists(layout.projectsDirectory, ec) || ec) {
        return true;
    }

    std::filesystem::recursive_directory_iterator it(layout.projectsDirectory, ec);
    const std::filesystem::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (shouldCancel && shouldCancel()) {
            return false;
        }
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        const std::filesystem::path absolutePath = NormalizePath(it->path());
        if (ToLowerAscii(absolutePath.extension().string()) != ".stack") {
            continue;
        }

        std::filesystem::path relativePath = std::filesystem::relative(absolutePath, layout.projectsDirectory, ec);
        if (ec) {
            ec.clear();
            relativePath = absolutePath.filename();
        }
        relativePath = relativePath.lexically_normal();

        StackBinaryFormat::ProjectDocument document;
        ProjectInfo info;
        const bool loaded = ReadProjectDocumentForDiscovery(absolutePath, document) &&
            ReadProjectInfoFromDocument(document, info, nullptr);

        const std::string expectedKey = LowerGenericPathKey(relativePath);
        auto expectedIt = expectedPathToSource.find(expectedKey);
        if (expectedIt != expectedPathToSource.end()) {
            if (loaded) {
                AttachProjectToSource(
                    sources[expectedIt->second],
                    std::move(info),
                    absolutePath,
                    relativePath,
                    "expected-project-path");
            } else {
                ProjectInfo invalid = BuildExpectedProjectInfo(layout, sources[expectedIt->second]);
                invalid.absolutePath = absolutePath;
                invalid.relativePath = relativePath;
                invalid.status = ProjectStatus::Invalid;
                invalid.errorMessage = "Project file could not be read as a RAW Workspace project.";
                sources[expectedIt->second].project = std::move(invalid);
            }
            continue;
        }

        if (!loaded) {
            continue;
        }

        auto sourceIt = info.sourceRelativePathKey.empty()
            ? relativeKeyToSource.end()
            : relativeKeyToSource.find(ToLowerAscii(info.sourceRelativePathKey));
        if (sourceIt != relativeKeyToSource.end()) {
            AttachProjectToSource(
                sources[sourceIt->second],
                std::move(info),
                absolutePath,
                relativePath,
                "stored-source-reference");
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

    std::error_code iteratorError;
    if (!std::filesystem::exists(layout.projectsDirectory, iteratorError) || iteratorError) {
        return true;
    }

    const auto inspectProject = [&](const std::filesystem::path& candidate) {
        Stack::Project::ProjectStoreOpenResult opened =
            Stack::Project::OpenProjectStore(candidate);
        if (!opened) {
            SourceSetProjectCatalogEntry invalid;
            invalid.absolutePath = NormalizePath(candidate);
            invalid.relativePath = candidate.filename();
            invalid.status = ProjectStatus::Invalid;
            invalid.errorMessage = opened.message;
            projects.push_back(std::move(invalid));
            return;
        }

        SourceSetProjectCatalogEntry entry;
        entry.projectId = opened.snapshot.projectId;
        entry.projectName = opened.snapshot.projectName;
        entry.absolutePath = NormalizePath(candidate);
        std::error_code relativeError;
        entry.relativePath = std::filesystem::relative(
            entry.absolutePath, layout.projectsDirectory, relativeError);
        if (relativeError) entry.relativePath = candidate.filename();
        entry.storageKind = opened.store->StorageKind();
        entry.status = opened.store->IsReadOnlyRecovery()
            ? ProjectStatus::Conflict
            : ProjectStatus::Existing;
        entry.readOnlyRecovery = opened.store->IsReadOnlyRecovery();
        entry.sourceSetCount = static_cast<std::uint64_t>(opened.snapshot.sourceSets.size());

        std::unordered_map<std::string, std::vector<std::size_t>> assetSources;
        for (const Stack::Project::EmbeddedAssetRecord& asset : opened.snapshot.embeddedAssets) {
            if (!asset.informationalOriginPath.empty()) {
                const auto source = originPathToSource.find(
                    LowerGenericPathKey(NormalizePath(asset.informationalOriginPath)));
                if (source != originPathToSource.end()) {
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
                if (identity->second.valid && identity->second.sha256 == asset.sha256) {
                    assetSources[asset.assetId].push_back(sourceIndex);
                }
            }
        }

        for (const Stack::Project::MultiFrameSourceSet& sourceSet : opened.snapshot.sourceSets) {
            entry.totalFrameCount += static_cast<std::uint64_t>(sourceSet.frames.size());
            if (sourceSet.inputFamily == Stack::Project::MultiFrameInputFamily::Raw) {
                ++entry.rawSetCount;
            } else {
                ++entry.rasterSetCount;
            }
            std::unordered_set<std::size_t> attachedSources;
            for (const Stack::Project::SourceSetFrame& frame : sourceSet.frames) {
                const auto matches = assetSources.find(frame.assetId);
                if (matches == assetSources.end()) continue;
                for (std::size_t sourceIndex : matches->second) {
                    if (!attachedSources.insert(sourceIndex).second) continue;
                    SourceSetProjectMembership membership;
                    membership.projectId = entry.projectId;
                    membership.projectName = entry.projectName;
                    membership.projectPath = entry.absolutePath;
                    membership.sourceSetId = sourceSet.sourceSetId;
                    membership.sourceSetName = sourceSet.name;
                    sources[sourceIndex].sourceSetProjectMemberships.push_back(
                        std::move(membership));
                }
            }
        }
        projects.push_back(std::move(entry));
    };

    std::filesystem::recursive_directory_iterator iterator(
        layout.projectsDirectory,
        std::filesystem::directory_options::skip_permission_denied,
        iteratorError);
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(iteratorError)) {
        if (shouldCancel && shouldCancel()) return false;
        if (iteratorError) {
            iteratorError.clear();
            continue;
        }
        const std::filesystem::path candidate = iterator->path();
        std::error_code kindError;
        if (iterator->is_directory(kindError) && !kindError &&
            ToLowerAscii(candidate.extension().string()) == ".stackbundle") {
            iterator.disable_recursion_pending();
            inspectProject(candidate);
            continue;
        }
        kindError.clear();
        if (iterator->is_regular_file(kindError) && !kindError &&
            ToLowerAscii(candidate.extension().string()) == ".stack" &&
            Stack::Project::IsPortableV3Project(candidate)) {
            inspectProject(candidate);
        }
    }
    std::sort(projects.begin(), projects.end(), [](const auto& lhs, const auto& rhs) {
        return ToLowerAscii(lhs.projectName) < ToLowerAscii(rhs.projectName);
    });
    return true;
}

bool RelinkProjectDocumentToSource(
    const SourceRecord& source,
    StackBinaryFormat::ProjectDocument& document,
    std::string* outError) {
    if (!document.rawWorkspaceData.is_object()) {
        if (outError) {
            *outError = "Project does not contain RAW Workspace metadata.";
        }
        return false;
    }

    document.rawWorkspaceData["rawSourceRef"] = BuildRawSourceRefJson(source, true);
    document.rawWorkspaceData["readOnlyReason"] = nullptr;

    nlohmann::json recipeJson = document.rawWorkspaceData.value("rawRecipe", nlohmann::json::object());
    Stack::RawRecipe::RawDevelopmentRecipe recipe = Stack::RawRecipe::DeserializeRecipe(recipeJson);
    recipe.source.sourcePath = source.absolutePath.string();
    recipe.source.relativePathKey = source.relativePathKey;
    recipe.source.fingerprint = source.fingerprint;
    recipe.source.fileSizeBytes = static_cast<std::uint64_t>(source.fileSizeBytes);
    recipe.source.modifiedTimeTicks = source.modifiedTimeTicks;
    recipe.source.displayName = source.fileName;
    document.rawWorkspaceData["rawRecipe"] = Stack::RawRecipe::SerializeRecipe(recipe);
    return true;
}

bool EmbedRawSourceInProjectDocument(
    const SourceRecord& source,
    StackBinaryFormat::ProjectDocument& document,
    std::string* outError) {
    if (!document.rawWorkspaceData.is_object()) {
        if (outError) {
            *outError = "Project does not contain RAW Workspace metadata.";
        }
        return false;
    }

    std::vector<unsigned char> bytes = ReadBinaryFile(source.absolutePath, outError);
    if (bytes.empty()) {
        if (outError && outError->empty()) {
            *outError = "RAW source is empty or could not be read.";
        }
        return false;
    }

    nlohmann::json::binary_t::container_type binary(bytes.begin(), bytes.end());
    document.rawWorkspaceData["rawSourceRef"] = BuildRawSourceRefJson(source, false);
    document.rawWorkspaceData["embeddedRaw"] = {
        { "present", true },
        { "fileName", source.fileName },
        { "sourceRelativePath", source.relativePathKey },
        { "bytes", nlohmann::json::binary(std::move(binary)) }
    };
    return true;
}

} // namespace Stack::RawWorkspace
