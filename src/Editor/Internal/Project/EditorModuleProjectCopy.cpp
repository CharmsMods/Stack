#include "Editor/EditorModule.h"
#include "Editor/Bracketing/BracketingSession.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"
#include "App/AppPaths.h"
#include "Persistence/ProjectIndex.h"
#include "Persistence/ProjectSnapshotCapture.h"
#include "Persistence/BracketingResultStore.h"
#include "Project/BracketingCopy.h"
#include "Persistence/ManagedAssetPaths.h"

bool EditorModule::RequestProjectCopy(const std::filesystem::path& requestedDestination,
    bool portable, bool activateCopy, std::function<void(bool)> onComplete) {
    namespace Project = Stack::Project;
    if (requestedDestination.empty() || m_ProjectCopyPending || HasPendingGraphImageImports()) {
        if (onComplete) onComplete(false);
        return false;
    }
    if (!IsUnifiedProjectStoreActive() || m_Project->saves.IsBusy()) {
        const auto owner = m_Project;
        return RequestSaveCurrentProject({}, [this, owner, requestedDestination, portable, activateCopy,
            onComplete = std::move(onComplete)](bool success) mutable {
            owner->tasks.PostToMain([this, owner, requestedDestination, portable, activateCopy, success,
                onComplete = std::move(onComplete)]() mutable {
                if (success && m_Project == owner)
                    RequestProjectCopy(requestedDestination, portable, activateCopy, std::move(onComplete));
                else if (onComplete) onComplete(false);
            });
        });
    }
    if (!FinishWorkspaceInteraction()) { if (onComplete) onComplete(false); return false; }
    const auto owner = m_Project;
    const auto originalId = owner->documentId;
    const auto revision = owner->editRevision;
    const auto sourceStore = owner->store;
    auto snapshot = Project::CopyProjectSnapshotWithoutPipeline(*owner->snapshot);
    snapshot.projectName = owner->name;
    snapshot.pipelineData = SerializePipeline();
    if (IsRawWorkspaceProjectActive()) {
        if (!IsMultiFrameRawProjectActive()) {
            snapshot.rawWorkspaceData["rawRecipe"] = Stack::RawRecipe::SerializeWorkspaceSourceRecipe(owner->rawRecipe);
            StackBinaryFormat::ProjectDocument modeDocument;
            modeDocument.rawWorkspaceData = snapshot.rawWorkspaceData;
            ApplyActiveRawWorkspaceModeDataToDocument(modeDocument);
            snapshot.rawWorkspaceData = std::move(modeDocument.rawWorkspaceData);
        }
        Project::UpdateWorkspaceManifestFields(snapshot);
    }
    if (!portable) {
        snapshot.projectId = Project::GenerateStableUuid();
        snapshot.lifecycle.creationOrigin = Project::ProjectCreationOrigin::Manual;
        snapshot.lifecycle.cleanupWhenUntouched = false;
        snapshot.lifecycle.explicitlyRetained = true;
        snapshot.adoptedFrom.clear();
        snapshot.rawWorkspaceData["projectId"] = snapshot.projectId;
        if (!activateCopy) snapshot.projectName += " version";
    }
    snapshot.rawWorkspaceData["repairRequired"] = false;
    auto destination = requestedDestination.lexically_normal();
    if (portable) {
        if (destination.extension() != ".stack") destination += ".stack";
    } else if (destination.filename() == "project.stack") destination = destination.parent_path();
    else if (destination.extension() == ".stack" || destination.extension() == ".stackbundle")
        destination = destination.parent_path() / destination.stem();

    std::optional<Project::BracketingCopy> draft;
    std::shared_ptr<const Raw::Bracketing::BracketingResult> result;
    std::string resultSet;
    if (m_Bracketing && !m_Bracketing->newProject && m_Bracketing->projectId == owner->snapshot->projectId) {
        if (m_Bracketing->processRequired) {
            draft.emplace();
            draft->sources = m_Bracketing->sources;
            draft->recipe = m_Bracketing->recipe;
            draft->setId = m_Bracketing->setId;
            draft->projectName = snapshot.projectName;
            draft->metadataPending = static_cast<bool>(m_Bracketing->metadataJob);
        }
        if (m_Bracketing->result && m_Bracketing->completedRevision == snapshot.hdrInputRevision) {
            const auto* set = Project::FindSourceSet(snapshot, m_Bracketing->setId);
            if (set && set->settings.value("bracketing", nlohmann::json()).dump() == m_Bracketing->completedRecipe) {
                result = m_Bracketing->result;
                resultSet = m_Bracketing->setId;
            }
        }
    }
    m_ProjectCopyPending = true;
    const bool submitted = ProjectTasks().Submit("Saving project copy",
        [this, owner, originalId, revision, sourceStore, destination, portable, activateCopy,
         snapshot = std::move(snapshot), draft = std::move(draft), result, resultSet, onComplete]() mutable {
        Project::ProjectStoreOpenResult copy;
        try {
            copy = Project::ConvertProjectStore(sourceStore, snapshot, destination,
                portable ? Project::ProjectStorageKind::PortableFile : Project::ProjectStorageKind::DirectoryBundle,
                [&](const auto& store, const auto& transaction, auto& captured, auto& error) {
                    if (draft && !Project::StageBracketingCopy(*draft, store, transaction, captured, error)) return false;
                    if (!StackBinaryFormat::ExternalizeManagedProjectAssets(store, transaction, captured)) {
                        error = "Could not collect all of the project's authored assets.";
                        return false;
                    }
                    return !result || Project::StageBracketingResult(store, transaction,
                        captured, resultSet, *result, {}, error);
                });
        } catch (const std::exception& error) { copy.message = error.what(); }
        owner->tasks.PostToMain([this, owner, originalId, revision, sourceStore, destination,
            portable, activateCopy, copy = std::move(copy), onComplete = std::move(onComplete)]() mutable {
            m_ProjectCopyPending = false;
            const bool success = static_cast<bool>(copy);
            const bool newerEdits = owner->editRevision != revision;
            // Imports may have added assets to the original while this copy
            // was writing. Keep that newer document and its store together;
            // never attach it to a copy that lacks those dependencies.
            if (success && activateCopy && !newerEdits && m_Project == owner && owner->documentId == originalId && owner->store == sourceStore) {
                CancelMfdExperimentalProcessing({}, false);
                CancelHdrProcessing();
                CancelMultiFrameGraphProcessing();
                InvalidateRenderSnapshotsBefore(m_RenderGeneration + 1);
                ClearRawRenderSession();
                owner->store = copy.store;
                owner->snapshot = std::make_shared<Project::RawProjectSnapshot>(std::move(copy.snapshot));
                owner->storePath = destination;
                owner->documentId = owner->snapshot->projectId;
                owner->lifecycle.Clear();
                owner->lifecycle.AdoptSavedBaseline(owner->documentId, revision,
                    owner->editRevision, owner->snapshot->persistedStorageRevision);
                owner->saves.Reset(owner->documentId, revision);
                owner->snapshot->dirtyRevision = owner->lifecycle.DirtyRevision();
                SetCurrentProjectFileName(destination.string());
                ClearDirtyIfRevision(revision);
                if (m_HdrAdoptedRawResult) m_HdrAdoptedRawResult->projectId = owner->documentId;
                if (m_MfdAdoptedRawResult) m_MfdAdoptedRawResult->projectId = owner->documentId;
                if (m_Bracketing && !m_Bracketing->newProject) {
                    m_Bracketing->projectId = owner->documentId;
                    m_Bracketing->job.reset();
                    m_Bracketing->storedRecipe.clear();
                    m_Bracketing->selectionRestored = false;
                }
                MarkRenderRefreshDirty();
            }
            if (success) RefreshUnifiedProjectViewsAfterSave(!portable);
            PostNotification(success ? UiNotificationSeverity::Success : UiNotificationSeverity::Error,
                success ? (portable ? "Exported a complete .stack project." :
                    (activateCopy && newerEdits ? "Copy saved. Newer edits remain open in the original project." : "Project copy saved.")) :
                    (copy.message.empty() ? "The project copy could not be saved." : copy.message), "project-copy");
            if (onComplete) onComplete(success);
        });
    });
    if (!submitted) { m_ProjectCopyPending = false; if (onComplete) onComplete(false); }
    return submitted;
}

void EditorModule::RequestCurrentProjectVersion() {
    const auto sharedAssets = m_Project->store
        ? Stack::Project::ClosetAssetRoot(m_Project->store->StoragePath()) : std::filesystem::path{};
    const auto root = !sharedAssets.empty() ? sharedAssets.parent_path() / "Projects" : !m_RawWorkspace.workspaceRoot.empty()
        ? Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot).projectsDirectory
        : IsRawWorkspaceProjectActive() && m_Project->store
        ? Stack::RawWorkspace::BuildManagedLayout(m_Project->store->StoragePath().parent_path()).projectsDirectory
        : AppPaths::GetProjectsDirectory();
    const auto destination = Stack::Project::ProjectIndex::BuildUniqueProjectPath(root,
        m_Project->name + " version", Stack::Project::GenerateStableUuid());
    RequestProjectCopy(destination, false, false);
}
