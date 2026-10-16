#include "Editor/EditorModule.h"
#include "Persistence/ProjectIndex.h"
#include "Persistence/RawProjectEditPipeline.h"
#include "Persistence/ManagedAssetPaths.h"
#include "Raw/RawLoader.h"

#include <stdexcept>
#include <iostream>

// First persistence is a snapshot of the existing document. In particular it
// must not run the legacy recipe materializer, which edits the live graph and
// changes document identity while importing the source on the UI thread.
void EditorModule::StartInitialRawProjectSaveAsync(
    std::uint64_t capturedRevision,
    Stack::Project::ProjectSaveCoordinator::Completion completion) {
    namespace Project = Stack::Project;
    const auto* selected = FindRawWorkspaceSourceByKey(m_Project->rawSourceKey);
    const auto owner = m_Project;
    const auto projectId = EnsureProjectDocumentId();
    if (!selected || selected->absolutePath.empty() || m_RawWorkspace.workspaceRoot.empty()) {
        Project::ProjectSaveResult result;
        result.projectId = projectId;
        result.message = "The RAW project's source or Gallery folder is unavailable.";
        completion(std::move(result));
        return;
    }
    const auto source = *selected;
    const auto destination = Project::ProjectIndex::BuildUniqueProjectPath(
        Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot).projectsDirectory,
        owner->name.empty() ? source.stem : owner->name, projectId);
    Project::RawProjectSnapshot captured;
    captured.projectId = projectId;
    captured.projectName = owner->name.empty() ? source.stem : owner->name;
    captured.projectKindHint = StackBinaryFormat::kRawProjectKind;
    captured.dirtyRevision = capturedRevision;
    captured.lifecycle.creationOrigin = Project::ProjectCreationOrigin::AutoFromViewer;
    captured.pipelineData = SerializePipeline();
    StackBinaryFormat::ProjectDocument rawDocument;
    Stack::RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(source, owner->rawRecipe,
        captured.pipelineData, rawDocument, owner->rawMode, false);
    ApplyActiveRawWorkspaceModeDataToDocument(rawDocument);
    captured.rawWorkspaceData = std::move(rawDocument.rawWorkspaceData);

    auto work = [this, owner, projectId, source, destination, capturedRevision,
        captured = std::move(captured), completion]() mutable {
        Project::ProjectStoreOpenResult saved;
        Project::ProjectStoreTransaction transaction;
        const auto stagingParent = destination.parent_path() / ".staging";
        const auto stagingRoot = stagingParent / Project::GenerateStableUuid();
        bool createdStaging = false;
        std::string error;
        bool success = false;
        try {
            if (Project::ClosetAssetRoot(destination).empty())
                throw std::runtime_error("The Gallery folder moved or is no longer available.");
            Project::RawProjectSnapshot bootstrap;
            bootstrap.projectId = projectId;
            bootstrap.projectName = captured.projectName;
            bootstrap.projectKindHint = captured.projectKindHint;
            saved = Project::CreateProjectStore(stagingRoot,
                Project::ProjectStorageKind::DirectoryBundle, bootstrap);
            if (!saved) throw std::runtime_error(saved.message);
            createdStaging = true;
            transaction = saved.store->BeginTransaction(saved.snapshot.persistedStorageRevision);
            if (!transaction) throw std::runtime_error("Could not begin the first RAW save.");
            Raw::RawMetadata metadata;
            nlohmann::json summary = nlohmann::json::object();
            if (Raw::RawLoader::LoadMetadata(source.absolutePath.string(), metadata)) {
                captured.sourceWidth = std::max(1, metadata.visibleWidth);
                captured.sourceHeight = std::max(1, metadata.visibleHeight);
                summary = Project::SerializeRawCaptureCompatibilitySummary(
                    Project::BuildRawCaptureCompatibilitySummary(metadata));
            }
            Project::EmbeddedAssetRecord asset;
            if (!saved.store->StageAssetFile(transaction, source.absolutePath,
                    Project::MultiFrameInputFamily::Raw, summary, asset, &error))
                throw std::runtime_error(error);
            if (!source.fingerprint.empty() && source.fingerprint != asset.sha256)
                throw std::runtime_error("The original RAW changed since it was opened. The current edits have been kept.");
            captured.embeddedAssets.push_back(asset);
            captured.lifecycle.initialAssetIds.push_back(asset.assetId);
            captured.rawWorkspaceData["managedAssetId"] = asset.assetId;
            captured.rawWorkspaceData["originalSourcePath"] = asset.originalSourcePath;
            captured.rawWorkspaceData["originalFileFingerprint"] = asset.sha256;
            captured.persistedStorageRevision = saved.snapshot.persistedStorageRevision;
            if (!Project::RebaseSingleRawRecipeToManagedAsset(captured, destination, &error))
                throw std::runtime_error(error);
            if (!StackBinaryFormat::ExternalizeManagedProjectAssets(saved.store, transaction, captured))
                throw std::runtime_error("Could not store the project's authored assets.");
            const auto commit = saved.store->Commit(transaction, captured);
            if (!commit) throw std::runtime_error(commit.message);
            captured.persistedStorageRevision = commit.committedStorageRevision;
            saved.store.reset();
            // Publish the first complete document as one folder move. Gallery
            // never discovers a bootstrap document while source copying runs.
            std::filesystem::rename(stagingRoot, destination);
            createdStaging = false;
            saved = Project::OpenProjectStore(destination);
            if (!saved) throw std::runtime_error(saved.message);
            success = true;
        } catch (const std::exception& exception) {
            error = exception.what();
            std::cerr << "[Project Save] First RAW save failed: " << error << '\n';
            if (saved.store && transaction) saved.store->Abort(transaction);
            saved.store.reset();
            if (createdStaging && stagingRoot.parent_path() == stagingParent &&
                stagingRoot.filename() != "." && stagingRoot.filename() != "..") {
                std::error_code cleanupError;
                std::filesystem::remove_all(stagingRoot, cleanupError);
            }
        }
        owner->tasks.PostToMain([this, owner, projectId, destination, capturedRevision,
            captured = std::move(captured), saved = std::move(saved), success,
            error = std::move(error), completion = std::move(completion)]() mutable {
            Project::ProjectSaveResult result;
            result.projectId = projectId;
            result.path = destination.string();
            result.message = error;
            if (m_Project != owner || owner->documentId != projectId || owner->store) {
                result.status = Project::ProjectSaveStatus::Canceled;
                result.message = "The project changed before its first save completed.";
            } else if (success) {
                owner->store = std::move(saved.store);
                owner->storePath = destination;
                owner->snapshot = std::make_shared<Project::RawProjectSnapshot>(std::move(captured));
                owner->lifecycle.AdoptSavedBaseline(projectId, capturedRevision,
                    owner->editRevision, owner->snapshot->persistedStorageRevision);
                owner->snapshot->dirtyRevision = owner->lifecycle.DirtyRevision();
                const auto assetId = owner->snapshot->rawWorkspaceData.value("managedAssetId", std::string());
                const auto managedPath = owner->store->ResolveAssetFile(assetId).string();
                const auto originalPath = owner->rawRecipe.source.sourcePath;
                owner->rawRecipe.source.sourcePath = managedPath;
                for (auto& node : owner->graph.GetNodes()) {
                    if (node.kind == EditorNodeGraph::NodeKind::RawDevelopment &&
                        node.rawDevelopment.recipe.source.sourcePath == originalPath)
                        node.rawDevelopment.recipe.source.sourcePath = managedPath;
                }
                SetCurrentProjectFileName(destination.string());
                ClearDirtyIfRevision(capturedRevision);
                result.status = Project::ProjectSaveStatus::Saved;
                result.persistedEditRevision = capturedRevision;
                result.storageRevision = owner->snapshot->persistedStorageRevision;
                RefreshUnifiedProjectViewsAfterSave(true);
            } else {
                result.status = Project::ProjectSaveStatus::Failed;
            }
            completion(std::move(result));
        });
    };
    if (!ProjectTasks().Submit("Saving RAW project", std::move(work))) {
        Project::ProjectSaveResult result;
        result.projectId = projectId;
        result.message = "The first RAW save could not be queued.";
        completion(std::move(result));
    }
}
