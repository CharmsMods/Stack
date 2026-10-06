#include "Editor/RawAttributeTargetProject.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Persistence/ProjectStore.h"
#include "Raw/RawLoader.h"

#include <algorithm>
#include <system_error>

namespace Stack::EditorRawAttributes {
namespace {

bool BuildCompactRawGraph(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    nlohmann::json& pipeline,
    std::string& error) {
    EditorNodeGraph::Graph graph;
    EditorNodeGraph::RawDevelopmentPayload payload;
    payload.recipe = recipe;
    payload.projectStatus = "Edited";
    payload.edited = true;
    payload.autosaved = false;
    EditorNodeGraph::Node* raw = graph.AddRawDevelopmentNode(
        std::move(payload), EditorNodeGraph::Vec2{ 20.0f, 120.0f });
    if (!raw) {
        error = "Failed to create the target RAW Development node.";
        return false;
    }
    EditorNodeGraph::Node* output = graph.AddOutputNode(
        EditorNodeGraph::Vec2{ 320.0f, 120.0f }, true);
    if (!output) {
        error = "Failed to create the target Output node.";
        return false;
    }
    if (!graph.TryConnectSockets(
            raw->id,
            EditorNodeGraph::kImageOutputSocketId,
            output->id,
            EditorNodeGraph::kImageInputSocketId,
            &error)) {
        if (error.empty()) {
            error = "Failed to connect the target RAW project graph.";
        }
        return false;
    }
    graph.SetOutputNodeId(output->id);
    graph.SelectNode(raw->id);
    pipeline = EditorNodeGraph::SerializeGraphPayload(
        nlohmann::json::array(), graph);
    return true;
}

std::filesystem::path BuildUniqueEditedCopyPath(
    const std::filesystem::path& sourcePath,
    Stack::Project::ProjectStorageKind storageKind) {
    std::filesystem::path normalized = sourcePath.lexically_normal();
    if (storageKind == Stack::Project::ProjectStorageKind::DirectoryBundle &&
        normalized.filename() == "project.stack") {
        normalized = normalized.parent_path();
    }
    const std::filesystem::path parent = normalized.parent_path();
    const std::string sourceName = storageKind ==
            Stack::Project::ProjectStorageKind::PortableFile
        ? normalized.stem().string()
        : normalized.filename().string();
    const std::string extension = storageKind ==
            Stack::Project::ProjectStorageKind::PortableFile
        ? normalized.extension().string()
        : std::string();
    const std::string baseName = (sourceName.empty()
        ? std::string("RAW Project")
        : sourceName) + " - Pasted Edits";
    for (unsigned int suffix = 1u; suffix < 10000u; ++suffix) {
        const std::string candidateName = baseName +
            (suffix == 1u ? std::string()
                          : " " + std::to_string(suffix));
        const std::filesystem::path candidate =
            parent / (candidateName + extension);
        std::error_code existsError;
        if (!std::filesystem::exists(candidate, existsError) &&
            !existsError) {
            return candidate;
        }
    }
    return {};
}

} // namespace

CreatedTargetProjectResult CreateProjectAndPasteAttributes(
    const std::filesystem::path& workspaceRoot,
    const Stack::RawWorkspace::SourceRecord& source,
    const Stack::RawRecipe::RawEditAttributeBundle& bundle,
    const std::vector<std::string>& selectedKeys,
    const std::filesystem::path& projectsDirectoryOverride) {
    namespace Project = Stack::Project;
    CreatedTargetProjectResult result;
    if (workspaceRoot.empty() || source.absolutePath.empty()) {
        result.transfer.errorMessage =
            "The selected RAW source is not available in a local workspace.";
        return result;
    }
    Stack::RawRecipe::RawEditAttributeTargetCompatibility compatibility;
    const Stack::RawRecipe::RawEditAttributeSelectionPlan plan =
        Stack::RawRecipe::PlanRawEditAttributeSelectionForTarget(
            bundle, selectedKeys, compatibility);
    result.transfer.warnings = plan.warnings;
    if (plan.applicableKeys.empty()) {
        result.transfer.success = true;
        result.transfer.skipped = true;
        return result;
    }

    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe(
            source.absolutePath.string(), source.fileName);
    recipe.source.relativePathKey = source.relativePathKey;
    recipe.source.fingerprint = source.fingerprint;
    recipe.source.fileSizeBytes =
        static_cast<std::uint64_t>(source.fileSizeBytes);
    recipe.source.modifiedTimeTicks = source.modifiedTimeTicks;
    recipe.source.displayName = source.fileName;
    std::string targetPlacement = "internal";
    const Stack::RawRecipe::RawEditAttributeApplyResult applied =
        Stack::RawRecipe::ApplyRawEditAttributeBundle(
            bundle, plan.applicableKeys, recipe, &targetPlacement);
    result.transfer.appliedKeys = applied.appliedKeys;
    result.transfer.warnings.insert(
        result.transfer.warnings.end(),
        applied.warnings.begin(),
        applied.warnings.end());
    if (!applied.success) {
        result.transfer.errorMessage = applied.errorMessage;
        return result;
    }
    // New projects always start with the compact internal transform. The look
    // is copied, while graph placement remains a separate structural action.
    recipe.viewTransform.layerJson["enabled"] = true;

    Stack::RawWorkspace::ManagedLayout layout =
        Stack::RawWorkspace::BuildManagedLayout(workspaceRoot);
    if (!projectsDirectoryOverride.empty()) {
        layout.projectsDirectory = projectsDirectoryOverride.lexically_normal();
    }
    std::filesystem::path projectRoot =
        layout.projectsDirectory /
        Stack::RawWorkspace::BuildProjectRelativePathForSource(source);
    std::error_code existsError;
    if (std::filesystem::exists(projectRoot, existsError) && !existsError) {
        const std::filesystem::path base = projectRoot;
        bool found = false;
        for (unsigned int suffix = 2u; suffix < 10000u; ++suffix) {
            std::filesystem::path candidate = base;
            candidate += "-" + std::to_string(suffix);
            existsError.clear();
            if (!std::filesystem::exists(candidate, existsError) &&
                !existsError) {
                projectRoot = std::move(candidate);
                found = true;
                break;
            }
        }
        if (!found) {
            result.transfer.errorMessage =
                "Stack could not choose a unique project folder for this image.";
            return result;
        }
    }
    result.projectPath = projectRoot;

    const std::string projectName = source.stem.empty()
        ? source.fileName
        : source.stem;
    Project::RawProjectSnapshot bootstrap;
    bootstrap.projectId = Project::GenerateStableUuid();
    bootstrap.projectName = projectName.empty()
        ? "Untitled RAW Project"
        : projectName;
    bootstrap.projectKindHint = StackBinaryFormat::kRawProjectKind;
    bootstrap.lifecycle.creationOrigin =
        Project::ProjectCreationOrigin::MultiSelection;
    bootstrap.lifecycle.cleanupWhenUntouched = false;
    bootstrap.lifecycle.explicitlyRetained = true;

    Project::ProjectStoreOpenResult created = Project::CreateProjectStore(
        projectRoot,
        Project::ProjectStorageKind::DirectoryBundle,
        bootstrap);
    if (!created) {
        result.transfer.errorMessage = created.message.empty()
            ? "Stack could not create the target project folder."
            : created.message;
        return result;
    }
    const auto removeFailedNewProject = [&]() {
        created.store.reset();
        std::error_code cleanup;
        const std::filesystem::path normalizedRoot =
            projectRoot.lexically_normal();
        const std::filesystem::path normalizedProjects =
            layout.projectsDirectory.lexically_normal();
        const auto relative = normalizedRoot.lexically_relative(
            normalizedProjects);
        const bool safelyContained =
            !normalizedRoot.empty() &&
            !normalizedProjects.empty() &&
            normalizedRoot != normalizedProjects &&
            !relative.empty() &&
            !relative.is_absolute() &&
            *relative.begin() != std::filesystem::path("..");
        if (safelyContained) {
            std::filesystem::remove_all(normalizedRoot, cleanup);
        }
    };

    const Project::ProjectStoreTransaction transaction =
        created.store->BeginTransaction(
            created.snapshot.persistedStorageRevision);
    if (!transaction) {
        removeFailedNewProject();
        result.transfer.errorMessage =
            "Stack could not begin the target project import.";
        return result;
    }

    nlohmann::json captureSummary = nlohmann::json::object();
    Raw::RawMetadata metadata;
    if (Raw::RawLoader::LoadMetadata(source.absolutePath.string(), metadata)) {
        captureSummary = Project::SerializeRawCaptureCompatibilitySummary(
            Project::BuildRawCaptureCompatibilitySummary(metadata));
    }
    Project::EmbeddedAssetRecord asset;
    std::string stageError;
    if (!created.store->StageAssetFile(
            transaction,
            source.absolutePath,
            Project::MultiFrameInputFamily::Raw,
            captureSummary,
            asset,
            &stageError)) {
        created.store->Abort(transaction);
        removeFailedNewProject();
        result.transfer.errorMessage = stageError.empty()
            ? "Stack could not copy the RAW source into its target project."
            : stageError;
        return result;
    }

    recipe.source.sourcePath =
        (projectRoot / asset.projectAssetPath).lexically_normal().string();
    recipe.source.fingerprint = asset.originalFileFingerprint;
    recipe.source.fileSizeBytes = asset.byteLength;
    nlohmann::json pipeline;
    std::string graphError;
    if (!BuildCompactRawGraph(recipe, pipeline, graphError)) {
        created.store->Abort(transaction);
        removeFailedNewProject();
        result.transfer.errorMessage = std::move(graphError);
        return result;
    }

    Project::RawProjectSnapshot snapshot = created.snapshot;
    snapshot.embeddedAssets.push_back(asset);
    snapshot.lifecycle.initialAssetIds.push_back(asset.assetId);
    snapshot.dirtyRevision = 1u;
    snapshot.postRecipeRevision = 1u;
    snapshot.pipelineData = std::move(pipeline);
    StackBinaryFormat::ProjectDocument rawDocument;
    Stack::RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
        source,
        recipe,
        snapshot.pipelineData,
        rawDocument,
        Stack::RawWorkspace::RawProjectMode::UnifiedLayers,
        false);
    snapshot.rawWorkspaceData = std::move(rawDocument.rawWorkspaceData);
    snapshot.rawWorkspaceData["managedAssetId"] = asset.assetId;
    snapshot.rawWorkspaceData["originalSourcePath"] =
        asset.originalSourcePath;
    snapshot.rawWorkspaceData["originalFileFingerprint"] =
        asset.originalFileFingerprint;

    const Project::ProjectStoreCommitResult committed =
        created.store->Commit(transaction, snapshot);
    if (!committed) {
        created.store->Abort(transaction);
        removeFailedNewProject();
        result.transfer.errorMessage = committed.message.empty()
            ? "Stack could not save the new target project."
            : committed.message;
        return result;
    }
    result.transfer.success = true;
    result.transfer.changed = true;
    result.transfer.committedStorageRevision =
        committed.committedStorageRevision;
    return result;
}

CreatedTargetProjectResult DuplicateProjectAndPasteAttributes(
    const std::filesystem::path& sourceProjectPath,
    const std::string& sourceSetId,
    const Stack::RawRecipe::RawEditAttributeBundle& bundle,
    const std::vector<std::string>& selectedKeys,
    const Stack::Project::ProjectStoreHandle& sourceStoreOverride,
    const Stack::Project::RawProjectSnapshot* sourceSnapshotOverride) {
    namespace Project = Stack::Project;
    CreatedTargetProjectResult result;

    Project::ProjectStoreHandle sourceStore = sourceStoreOverride;
    Project::RawProjectSnapshot sourceSnapshot;
    if (sourceStore && sourceSnapshotOverride != nullptr) {
        sourceSnapshot = *sourceSnapshotOverride;
    } else {
        const Project::ProjectStoreOpenResult opened =
            Project::OpenProjectStore(sourceProjectPath);
        if (!opened) {
            result.transfer.errorMessage = opened.message.empty()
                ? "The target RAW project could not be opened for copying."
                : opened.message;
            return result;
        }
        sourceStore = opened.store;
        sourceSnapshot = opened.snapshot;
    }
    if (!sourceStore) {
        result.transfer.errorMessage =
            "The target RAW project store is unavailable.";
        return result;
    }

    const Project::ProjectStorageKind storageKind =
        sourceStore->StorageKind();
    const std::filesystem::path destination = BuildUniqueEditedCopyPath(
        sourceProjectPath.empty()
            ? sourceStore->StoragePath()
            : sourceProjectPath,
        storageKind);
    if (destination.empty()) {
        result.transfer.errorMessage =
            "Stack could not choose a unique name for the pasted-edit copy.";
        return result;
    }

    sourceSnapshot.projectId = Project::GenerateStableUuid();
    sourceSnapshot.projectName =
        (sourceSnapshot.projectName.empty()
            ? std::string("RAW Project")
            : sourceSnapshot.projectName) + " - Pasted Edits";
    sourceSnapshot.lifecycle.creationOrigin =
        Project::ProjectCreationOrigin::MultiSelection;
    sourceSnapshot.lifecycle.cleanupWhenUntouched = false;
    sourceSnapshot.lifecycle.explicitlyRetained = true;

    Project::ProjectStoreOpenResult copied = Project::ConvertProjectStore(
        sourceStore,
        sourceSnapshot,
        destination,
        storageKind);
    if (!copied) {
        result.transfer.errorMessage = copied.message.empty()
            ? "Stack could not create the independent target project copy."
            : copied.message;
        return result;
    }
    copied.store.reset();
    result.projectPath = destination;
    result.transfer = Project::PasteRawEditAttributesIntoProject(
        destination,
        sourceSetId,
        bundle,
        selectedKeys);
    if (!result.transfer.success || result.transfer.skipped) {
        std::error_code cleanupError;
        if (storageKind == Project::ProjectStorageKind::DirectoryBundle) {
            std::filesystem::remove_all(destination, cleanupError);
        } else {
            std::filesystem::remove(destination, cleanupError);
            std::filesystem::path staging = destination;
            staging += ".staging";
            cleanupError.clear();
            std::filesystem::remove_all(staging, cleanupError);
        }
        result.projectPath.clear();
    }
    if (!result.transfer.success) {
        result.transfer.errorMessage = result.transfer.errorMessage.empty()
            ? "The independent project copy could not apply the selected RAW attributes."
            : result.transfer.errorMessage;
    }
    return result;
}

} // namespace Stack::EditorRawAttributes
