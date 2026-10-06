#include "Persistence/ProjectOpenCoordinator.h"

#include "Persistence/ProjectStore.h"
#include "Persistence/StackBinaryFormat.h"
#include "App/AppPaths.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Raw/RawLoader.h"
#include "Raw/RawWorkspace.h"
#include "ThirdParty/stb_image.h"
#include "Utils/PixelBufferUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <system_error>

namespace Stack::Project {
namespace {

std::filesystem::path NormalizeProjectPath(const std::filesystem::path& path) {
    if (path.empty()) return {};
    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::absolute(path, ec);
    if (ec) normalized = path;
    normalized = normalized.lexically_normal();
    if (normalized.filename() == "project.stack") {
        normalized = normalized.parent_path();
    }
    return normalized;
}

bool IsRawProjectDocument(const StackBinaryFormat::ProjectDocument& document) {
    return document.metadata.projectKind == StackBinaryFormat::kRawProjectKind ||
        (document.rawWorkspaceData.is_object() &&
         document.rawWorkspaceData.value("schema", std::string()) ==
            "stack.rawWorkspace.project");
}

bool DecodeImageBytes(
    const std::vector<unsigned char>& encoded,
    std::vector<unsigned char>& pixels,
    int& width,
    int& height,
    int& channels) {
    pixels.clear();
    width = 0;
    height = 0;
    channels = 0;
    if (encoded.empty() ||
        encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    int sourceChannels = 0;
    stbi_set_flip_vertically_on_load_thread(1);
    unsigned char* decoded = stbi_load_from_memory(
        encoded.data(),
        static_cast<int>(encoded.size()),
        &width,
        &height,
        &sourceChannels,
        4);
    if (decoded == nullptr) return false;

    channels = 4;
    const bool copied = Stack::PixelBuffer::CopyInterleavedPixels(
        decoded, width, height, channels, pixels);
    stbi_image_free(decoded);
    if (!copied) {
        width = 0;
        height = 0;
        channels = 0;
    }
    return copied;
}

std::shared_ptr<const Raw::RawImageData> DecodeSingleRawProjectSource(
    StackBinaryFormat::ProjectDocument& document,
    std::string& error) {
    error.clear();
    if (document.rawProjectSnapshot &&
        IsMultiFrameProjectDocument(*document.rawProjectSnapshot)) {
        return {};
    }

    Stack::RawWorkspace::ProjectInfo projectInfo;
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    if (!Stack::RawWorkspace::ReadProjectInfoFromDocument(
            document,
            projectInfo,
            &recipe)) {
        error = "The single-RAW project has no resolvable source reference.";
        return {};
    }

    const auto* asset = document.rawProjectSnapshot
        ? FindEmbeddedAsset(*document.rawProjectSnapshot,
            document.rawWorkspaceData.value("managedAssetId", std::string())) : nullptr;
    if (!asset && document.rawProjectSnapshot && document.rawProjectSnapshot->embeddedAssets.size() == 1)
        asset = &document.rawProjectSnapshot->embeddedAssets.front();
    if (!asset || !document.projectStore) {
        error = "The single-RAW project has no managed source asset.";
        return {};
    }
    std::filesystem::path sourcePath;
    if (document.projectStore->StorageKind() == ProjectStorageKind::PortableFile) {
        sourcePath = AppPaths::GetCacheDirectory() / "ProjectRawSources" /
            (asset->sha256 + "-" + std::to_string(asset->byteLength) +
             std::filesystem::u8path(asset->originalFilename).extension().u8string());
        // Always copy from this store. A cache file or an old absolute source
        // path is not evidence that it contains the saved original.
        if (!document.projectStore->CopyAssetToFile(asset->assetId, sourcePath, &error)) return {};
    } else {
        sourcePath = document.projectStore->StoragePath() / std::filesystem::u8path(asset->projectAssetPath);
    }
    auto decoded = std::make_shared<Raw::RawImageData>();
    if (!Raw::RawLoader::LoadFile(sourcePath.u8string(), *decoded) ||
        !decoded->metadata.error.empty()) {
        error = decoded->metadata.error.empty()
            ? "The single-RAW source could not be decoded."
            : decoded->metadata.error;
        return {};
    }
    if (decoded->metadata.sourceContentSha256 != asset->sha256 ||
        decoded->metadata.sourceByteSize != asset->byteLength) {
        error = "The single-RAW source no longer matches its saved content identity.";
        return {};
    }
    const auto path = sourcePath.lexically_normal().u8string();
    document.rawWorkspaceData["rawRecipe"]["sourceRef"]["sourcePath"] = path;
    if (document.pipelineData.contains("nodeGraph") &&
        document.pipelineData["nodeGraph"].contains("nodes")) {
        for (auto& node : document.pipelineData["nodeGraph"]["nodes"]) {
            const auto kind = node.value("kind", std::string());
            if (kind == "RawDevelopment") {
                const auto stored = node.value("rawRecipe", nlohmann::json::object());
                const auto ref = stored.value("sourceRef", nlohmann::json::object());
                if (ref.value("sourcePath", std::string()) == recipe.source.sourcePath ||
                    (!recipe.source.fingerprint.empty() &&
                     ref.value("fingerprint", std::string()) == recipe.source.fingerprint))
                    node["rawRecipe"]["sourceRef"]["sourcePath"] = path;
            } else if (kind == "RawSource" &&
                node.value("sourcePath", std::string()) == recipe.source.sourcePath) {
                node["sourcePath"] = path;
                node["rawMetadata"]["sourcePath"] = path;
            }
        }
    }
    return decoded;
}

void PopulateProbeFromDocument(
    const StackBinaryFormat::ProjectDocument& document,
    ProjectFormatProbe& probe) {
    probe.projectKind = document.metadata.projectKind.empty()
        ? StackBinaryFormat::kEditorProjectKind
        : document.metadata.projectKind;
    probe.rawProject = IsRawProjectDocument(document);
    probe.multiFrameProject = document.rawProjectSnapshot &&
        IsMultiFrameProjectDocument(*document.rawProjectSnapshot);
}

} // namespace

ProjectFormatProbe ProjectOpenCoordinator::Probe(
    const std::filesystem::path& path) {
    ProjectFormatProbe probe;
    probe.normalizedPath = NormalizeProjectPath(path);
    if (probe.normalizedPath.empty()) {
        probe.error = "No project path was provided.";
        return probe;
    }

    std::error_code ec;
    const bool directory = std::filesystem::is_directory(
        probe.normalizedPath, ec) && !ec;
    ec.clear();
    const bool regularFile = std::filesystem::is_regular_file(
        probe.normalizedPath, ec) && !ec;
    if (!directory && !regularFile) {
        probe.error = "The selected project no longer exists at " +
            probe.normalizedPath.string() + ".";
        return probe;
    }

    if (directory || IsPortableV3Project(probe.normalizedPath)) {
        ProjectStoreOpenResult opened = OpenProjectStore(probe.normalizedPath);
        if (!opened) {
            probe.error = opened.message.empty()
                ? "The project store could not be opened."
                : opened.message;
            return probe;
        }
        if (!EditorNodeGraph::IsCurrentGraphPayload(
                opened.snapshot.pipelineData)) {
            probe.error =
                "The project uses an obsolete editor graph schema.";
            return probe;
        }
        probe.format = directory
            ? ProjectOpenFormat::CurrentBundle
            : ProjectOpenFormat::PackedPortable;
        probe.supported = true;
        probe.projectKind = opened.snapshot.projectKindHint.empty()
            ? StackBinaryFormat::kEditorProjectKind
            : opened.snapshot.projectKindHint;
        probe.multiFrameProject = IsMultiFrameProjectDocument(opened.snapshot);
        probe.rawProject =
            probe.projectKind == StackBinaryFormat::kRawProjectKind ||
            probe.multiFrameProject ||
            (opened.snapshot.rawWorkspaceData.is_object() &&
             opened.snapshot.rawWorkspaceData.value(
                 "schema", std::string()) ==
                "stack.rawWorkspace.project");
        return probe;
    }

    probe.error = regularFile
        ? "The selected file is not a current packed Stack project."
        : "The selected path is not a current Stack project.";
    return probe;
}

ProjectOpenResult ProjectOpenCoordinator::Load(
    const std::filesystem::path& path) {
    ProjectOpenResult result;
    result.probe = Probe(path);
    if (!result.probe.supported) {
        result.error = result.probe.error;
        return result;
    }

    StackBinaryFormat::ProjectLoadOptions options;
    options.includeThumbnail = false;
    options.includeSourceImage = true;
    options.includePipelineData = true;
    options.includeNodeBrowserThumbnails = true;
    options.includeRawWorkspaceData = true;
    options.verifyChecksum = false;

    StackBinaryFormat::ProjectDocument document;
    if (!StackBinaryFormat::ReadProjectFile(
            result.probe.normalizedPath, document, options)) {
        result.error =
            "The project manifest opened, but its editor state or a required asset could not be restored.";
        return result;
    }

    PopulateProbeFromDocument(document, result.probe);
    if (result.probe.projectKind == StackBinaryFormat::kRenderProjectKind ||
        result.probe.projectKind == StackBinaryFormat::kCompositeProjectKind) {
        result.error = "This project type cannot be opened in the editing workspace.";
        return result;
    }

    auto candidate = std::make_shared<LoadedProjectData>();
    if (result.probe.rawProject) {
        candidate->sourceState = ProjectSourceState::LazyManagedAsset;
        if (!result.probe.multiFrameProject) {
            std::string rawError;
            candidate->decodedRawSource = DecodeSingleRawProjectSource(
                document, rawError);
            if (!candidate->decodedRawSource) {
                candidate->sourceState = ProjectSourceState::Unavailable;
                result.error = rawError.empty()
                    ? "The single-RAW source is unavailable."
                    : rawError;
                return result;
            }
        }
    } else {
        if (!DecodeImageBytes(
                document.sourceImageBytes,
                candidate->sourcePixels,
                candidate->width,
                candidate->height,
                candidate->channels)) {
            candidate->sourceState = ProjectSourceState::Unavailable;
            result.error = "The project source image could not be decoded.";
            return result;
        }
        candidate->sourceState = ProjectSourceState::DecodedPixels;
    }

    candidate->pipelineData = document.pipelineData.is_null()
        ? StackBinaryFormat::json::array() : document.pipelineData;
    candidate->projectId = document.projectId;
    candidate->projectName = document.metadata.projectName;
    candidate->projectFileName = result.probe.normalizedPath.string();
    candidate->projectKind = result.probe.rawProject
        ? StackBinaryFormat::kRawProjectKind
        : result.probe.projectKind;
    candidate->adoptedFrom = document.adoptedFrom;
    candidate->rawWorkspaceData = std::move(document.rawWorkspaceData);
    candidate->projectStore = document.projectStore;
    candidate->rawProjectSnapshot = document.rawProjectSnapshot;
    candidate->nodeBrowserThumbnailEntries =
        std::move(document.nodeBrowserThumbnailEntries);
    if (candidate->adoptedFrom.empty() &&
        result.probe.format == ProjectOpenFormat::PackedPortable) {
        candidate->adoptedFrom = result.probe.normalizedPath;
    }

    if (candidate->projectStore && candidate->projectStore->IsReadOnlyRecovery()) {
        result.warning =
            "The current project generation was damaged, so Stack opened the previous generation read-only. Use Save As to create a repaired project.";
    }
    result.candidate = std::move(candidate);
    return result;
}

} // namespace Stack::Project
