#include "Utils/UiBusyState.h"
#include "Editor/EditorModule.h"
#include "Editor/Internal/Project/ProjectLifecycleSupport.h"

#include "Async/TaskSystem.h"
#include "App/AppPaths.h"
#include "Library/LibraryManager.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"
#include "Persistence/ProjectIndex.h"
#include "Raw/RawLoader.h"
#include "Raw/RawTechnicalEvidence.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using Stack::Editor::ProjectInternal::ApplyRawWorkspaceProjectInfoToSource;
using Stack::Editor::ProjectInternal::EnsureMinimalProjectDocument;
using Stack::Editor::ProjectInternal::OverlayOwnedJsonFields;
using Stack::Editor::ProjectInternal::RawWorkspaceProjectFileTimeTicks;

namespace {

struct RawWorkspaceProjectLoadResult {
    Stack::RawWorkspace::SourceRecord source;
    EditorLoadedProjectData loadedProject;
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    Stack::RawWorkspace::RawProjectMode mode = Stack::RawWorkspace::RawProjectMode::UnifiedLayers;
    Stack::RawWorkspace::ManagedRawSection managedSection;
    bool success = false;
    bool hasRawWorkspaceInfo = false;
    std::string errorMessage;
};

} // namespace

bool EditorModule::HasProjectContent() const {
    return !m_Project->name.empty() ||
        !m_Project->fileName.empty() ||
        !m_Project->layers.empty() ||
        !m_Project->graph.GetNodes().empty() ||
        !m_Project->graph.GetLinks().empty() ||
        m_Pipeline.HasSourceImage();
}

bool EditorModule::HasOpenProjectSession() const {
    return HasProjectContent() ||
        !m_Project->documentId.empty() ||
        !m_Project->lifecycle.ProjectId().empty() ||
        m_Project->store != nullptr ||
        m_Project->snapshot != nullptr ||
        !m_Project->storePath.empty();
}

EditorModule::ProjectSessionKind EditorModule::GetProjectSessionKind() const {
    if (IsRawWorkspaceProjectActive()) {
        const Stack::RawWorkspace::SourceRecord* source =
            FindRawWorkspaceSourceByKey(m_Project->rawSourceKey);
        if (source != nullptr &&
            source->project.status == Stack::RawWorkspace::ProjectStatus::NoProject &&
            !IsDirty()) {
            return ProjectSessionKind::RawPreview;
        }
        return ProjectSessionKind::RawProject;
    }
    return HasOpenProjectSession()
        ? ProjectSessionKind::EditorProject
        : ProjectSessionKind::Empty;
}

EditorModule::ProjectFileCommandContext
EditorModule::GetProjectFileCommandContext() const {
    ProjectFileCommandContext context;
    if (IsAutoBracketWorkspace()) {
        context.canClose = true;
        context.canOpen = false;
        context.canCreateEditorProject = false;
        context.busyReason = "Use a project tab to open or edit a project.";
        return context;
    }
    context.sessionKind = GetProjectSessionKind();
    context.dirty = IsDirty();
    if (IsUnifiedProjectStoreActive()) {
        context.projectPath = m_Project->store->StoragePath();
    } else {
        context.projectPath = m_Project->storePath.empty()
            ? std::filesystem::path(m_Project->fileName)
            : m_Project->storePath;
    }

    if (IsUnifiedProjectStoreActive()) {
        context.lifecyclePhase = m_Project->lifecycle.Phase();
        context.storageKind = m_Project->store->StorageKind();
    } else {
        context.lifecyclePhase = context.sessionKind == ProjectSessionKind::Empty
            ? Stack::Project::ProjectLifecyclePhase::Empty
            : (context.dirty
                ? Stack::Project::ProjectLifecyclePhase::ReadyDirty
                : Stack::Project::ProjectLifecyclePhase::ReadyClean);
    }

    context.conflict = context.lifecyclePhase ==
        Stack::Project::ProjectLifecyclePhase::Conflict;
    context.readOnlyRecovery = context.lifecyclePhase ==
        Stack::Project::ProjectLifecyclePhase::ReadOnlyRecovery;

    const bool projectLoadBusy =
        IsDeferredLoadedProjectApplyActive() ||
        IsRawWorkspaceProjectLoadBusy() ||
        Async::IsBusy(GetProjectLoadTaskState());
    const bool projectSaveBusy =
        IsProjectFileSaveBusy() ||
        IsRawWorkspaceProjectSaveBusy();
    const bool importBusy =
        IsSourceLoadBusy() ||
        IsGraphDropImportBusy();
    const bool lifecycleLoadBusy = context.lifecyclePhase ==
        Stack::Project::ProjectLifecyclePhase::Loading;
    const bool lifecycleSaveBusy = context.lifecyclePhase ==
        Stack::Project::ProjectLifecyclePhase::Saving;
    const bool lifecycleImportBusy = context.lifecyclePhase ==
        Stack::Project::ProjectLifecyclePhase::Importing;
    const bool mfdProcessingBusy =
        IsMultiFrameRawProjectActive() && IsMfdExperimentalProcessingBusy();
    context.busy = projectLoadBusy || projectSaveBusy || importBusy ||
        lifecycleLoadBusy || lifecycleSaveBusy || lifecycleImportBusy ||
        mfdProcessingBusy;
    if (projectLoadBusy) {
        context.busyReason = "A project is currently opening.";
    } else if (projectSaveBusy) {
        context.busyReason = "A project save is currently in progress.";
    } else if (importBusy) {
        context.busyReason = "Imported project content is still being prepared.";
    } else if (lifecycleLoadBusy) {
        context.busyReason = "The project store is currently opening.";
    } else if (lifecycleSaveBusy) {
        context.busyReason = "The project store is currently committing a save.";
    } else if (lifecycleImportBusy) {
        context.busyReason = "Source frames are currently being imported.";
    } else if (mfdProcessingBusy) {
        context.busyReason = "Multi-frame processing is currently running.";
    } else if (context.conflict) {
        context.busyReason =
            "The open project has a storage conflict. Save a copy to continue safely.";
    } else if (context.readOnlyRecovery) {
        context.busyReason =
            "The open project is in read-only recovery. Save a copy to continue editing.";
    } else if (!m_DocumentPersistenceEnabled) {
        context.busyReason = "Project saving is disabled in this session.";
    }

    const bool hasSession = context.sessionKind != ProjectSessionKind::Empty;
    // Save is the one command that remains available while a save is active:
    // Ctrl+S joins the single-writer queue and completes only after its
    // requested revision is durable. Loading/importing still cannot be saved.
    context.canSave = (hasSession || HasUnsavedBracketingDraft()) &&
        m_DocumentPersistenceEnabled &&
        !projectLoadBusy && !importBusy && !lifecycleLoadBusy &&
        !lifecycleImportBusy && !mfdProcessingBusy &&
        !context.conflict && !context.readOnlyRecovery;
    context.canSaveAs = hasSession && !context.busy;
    context.canClose = hasSession && !context.busy;
    context.canOpen = !context.busy;
    context.canCreateEditorProject = !context.busy;
    return context;
}

bool EditorModule::CloseCurrentProject(bool discardUnsavedChanges) {
    if(RequestAutoBracketForeground("close this project",[this,discardUnsavedChanges]{CloseCurrentProject(discardUnsavedChanges);}))return true;
    const ProjectSessionKind kind = GetProjectSessionKind();
    if (kind == ProjectSessionKind::Empty) {
        return false;
    }
    if (kind == ProjectSessionKind::RawPreview ||
        kind == ProjectSessionKind::RawProject) {
        return CloseActiveRawWorkspaceProject(discardUnsavedChanges);
    }
    if (IsDirty() && !discardUnsavedChanges) {
        return false;
    }
    ResetToBlankProject();
    return true;
}

bool EditorModule::CloseEditorProjectAndActivateRawWorkspace() {
    if(RequestAutoBracketForeground("change projects",[this]{CloseEditorProjectAndActivateRawWorkspace();}))return true;
    if (IsRawWorkspaceProjectActive()) {
        m_RawWorkspaceLockedByEditorProject = false;
        m_RawWorkspaceRootTabActive = true;
        return true;
    }

    ResetToBlankProject();
    m_RawWorkspaceLockedByEditorProject = false;
    m_RawWorkspaceRootTabActive = true;
    if (!m_PendingRawWorkspaceExplicitOpenSourceKey.empty()) {
        const std::string sourceKey =
            std::move(m_PendingRawWorkspaceExplicitOpenSourceKey);
        m_PendingRawWorkspaceExplicitOpenSourceKey.clear();
        SelectRawWorkspaceSource(sourceKey);
    }
    return true;
}

bool EditorModule::CloseActiveRawWorkspaceProject(bool discardUnsavedChanges) {
    if(RequestAutoBracketForeground("close this project",[this,discardUnsavedChanges]{CloseActiveRawWorkspaceProject(discardUnsavedChanges);}))return true;
    if (!IsRawWorkspaceProjectActive()) {
        return false;
    }
    if (IsDirty() && !discardUnsavedChanges) {
        return false;
    }
    if (IsRawWorkspaceProjectLoadBusy() || IsRawWorkspaceProjectSaveBusy()) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "Wait for the current RAW project operation to finish before closing it.",
            "raw-workspace-project-close-busy");
        return false;
    }

    const std::string selectedSourceKey = m_PinnedRawWorkspaceSource.has_value()
        ? m_RawWorkspaceSelectedSourceBeforePinnedProject
        : m_RawWorkspace.selectedSourceKey;
    const std::vector<std::string> selectedSourceKeys =
        m_RawWorkspace.selectedSourceKeys;
    m_RawWorkspaceProjectLoadGeneration.fetch_add(1, std::memory_order_relaxed);
    m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Idle;
    m_RawWorkspaceProjectLoadSourceKey.clear();
    m_RawWorkspaceProjectLoadStatusText.clear();
    ResetDeferredLoadedProjectApplyState();
    m_PendingRawWorkspaceDeferredProjectFinalize = false;
    m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
    m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
    m_PendingRawWorkspaceOpenGraphSourceKey.clear();
    m_PendingRawWorkspaceExplicitOpenSourceKey.clear();
    m_RawWorkspacePreviewStageQueued = false;
    m_RawWorkspacePreviewStageSourceKey.clear();
    m_RawWorkspacePreviewStageQueuedFrame = -1;
    ClearRawWorkspaceLivePreviewState();
    ResetToBlankProject();

    const auto sourceExists = [&](const std::string& key) {
        return !key.empty() && std::any_of(
            m_RawWorkspace.sources.begin(),
            m_RawWorkspace.sources.end(),
            [&](const Stack::RawWorkspace::SourceRecord& source) {
                return source.relativePathKey == key;
            });
    };
    m_RawWorkspace.selectedSourceKey = sourceExists(selectedSourceKey)
        ? selectedSourceKey
        : std::string();
    m_RawWorkspace.selectedSourceKeys.clear();
    for (const std::string& key : selectedSourceKeys) {
        if (sourceExists(key)) {
            m_RawWorkspace.selectedSourceKeys.push_back(key);
        }
    }
    if (m_RawWorkspace.selectedSourceKey.empty() &&
        !m_RawWorkspace.selectedSourceKeys.empty()) {
        m_RawWorkspace.selectedSourceKey =
            m_RawWorkspace.selectedSourceKeys.front();
    }
    m_RawWorkspaceRootTabActive = true;
    m_RawWorkspaceLockedByEditorProject = false;
    InvalidateRawWorkspaceGalleryPresentation();
    PersistRawWorkspaceCatalog();
    SaveRawWorkspaceAppState();
    PostNotification(
        UiNotificationSeverity::Success,
        "Project closed. The RAW gallery remains available.",
        "raw-workspace-project-closed");
    return true;
}

void EditorModule::SetNotificationScope(Stack::Notifications::Notifier notifier) {
    m_Notifier = std::move(notifier);
    ProjectTasks().SetActivityOwner(m_Notifier.GetOwner().id, m_Notifier.GetOwner().generation);
}

void EditorModule::ShowUiNotification(UiNotificationSeverity severity, std::string message, std::string dedupeKey) {
    PostNotification(severity, std::move(message), std::move(dedupeKey));
}

void EditorModule::PostNotification(UiNotificationSeverity severity, std::string message, std::string dedupeKey) {
    PostUiNotification(m_Notifier, severity, std::move(message), std::move(dedupeKey));
}

void EditorModule::ResetToBlankProject() {
    auto canceledLoadCompletion = std::move(m_DeferredLoadedProjectApply.completion);
    ResetDeferredLoadedProjectApplyState();
    m_Project->files->load.Invalidate();
    m_Project->files->save.Invalidate();
    ResetProjectInteractionState();
    ResetBracketingForProjectLoad({});
    m_Project->saves.Reset();
    ++m_ProjectFileSaveGeneration;
    m_ProjectFileSaveTaskState = Async::TaskState::Idle;
    m_ProjectFileSaveStatusText.clear();
    CancelMfdExperimentalProcessing({}, true);
    CancelHdrProcessing();
    CancelMultiFrameGraphProcessing();
    m_HdrAdoptedRawResult.reset();
    m_MultiFrameProcessingCache.reset();
    m_MultiFrameFusionResults.clear();
    m_MultiFrameCacheProjectId.clear();
    m_FusionWorkspaceUi.reset();
    m_HdrProcessingReport.reset();
    m_MultiFrameProjectCoverRefreshPending = false;
    CancelCanvasTool();
    CancelGraphAutoFocusTracking();
    m_GraphDropImportTaskState = Async::TaskState::Idle;
    m_GraphDropImportStatusText.clear();
    m_PendingGraphDropImports.clear();
    m_SourceLoadTaskState = Async::TaskState::Idle;
    m_SourceLoadStatusText.clear();
    m_Project->layers.clear();
    m_SelectedLayerIndex = -1;
    m_Project->graph.ResetFromLayers(0, false);
    ClearCompositeRuntimeState();
    m_NodeDirtyGenerations.clear();
    m_DevelopAutoSolveTriggerHashes.clear();
    m_DevelopAutoRawSolveTriggerHashes.clear();
    m_DevelopAutoRawCalibrationHashes.clear();
    m_DevelopAutoGuidanceDrafts.clear();
    m_RawDevelopExposureDrafts.clear();
    m_LastRawDevelopInteractionTime = -1.0;
    m_RawDevelopInteractionSerialCounter = 1;
    m_RawDevelopInteractionTimes.clear();
    m_RawDevelopInteractionSerials.clear();
    m_DeferredDevelopCandidateFeedbackTimes.clear();
    m_PreviewDisplayedRevisions.clear();
    m_PreviewPixelCache.clear();
    m_PreviewRequestedGenerations.clear();
    m_PreviewCompletedGenerations.clear();
    m_HdrMergeRequestedGenerations.clear();
    m_HdrMergeCompletedGenerations.clear();
    m_HdrMergeFailureMessages.clear();
    m_HdrMergeRenderingNodeIds.clear();
    m_HdrMergeSubmittedNodesByGeneration.clear();
    m_ScopeDisplayedRevisions.clear();
    ResetNodeBrowserThumbnailState();
    ResetRenderSubmissionState();
    ClearViewportOutputTiles();
    m_Pipeline.Clear();
    m_CompositePreviewPipeline.Clear();
    m_Project->rawSourceKey.clear();
    m_PinnedRawWorkspaceSource.reset();
    m_RawWorkspaceSelectedSourceBeforePinnedProject.clear();
    m_Project->rawPipelineActive = false;
    m_RawWorkspaceStaleRenderStatusText.clear();
    m_Project->storePath.clear();
    m_Project->store.reset();
    m_Project->snapshot.reset();
    m_Project->singleRawSource.reset();
    m_Project->lifecycle.Clear();
    m_Project->rawRecipe = {};
    m_Project->rawMode = Stack::RawWorkspace::RawProjectMode::UnifiedLayers;
    m_Project->managedRaw = {};
    SetCurrentProjectName("");
    SetCurrentProjectFileName("");
    m_Project->documentId.clear();
    m_Project->adoptionSourcePath.clear();
    m_ProjectNamingPromptRequested = false;
    m_ProjectNamingPromptShown = false;
    m_Project->editRevision = 0;
    MarkRenderRefreshDirty();
    ClearDirty();
    m_Project->lastEditTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    m_Project->lastAutosaveTime = -1.0;
    m_Project->lastAutosaveAttemptTime = -1.0;
    if (canceledLoadCompletion) {
        try {
            canceledLoadCompletion(false, "The project closed before loading completed.");
        } catch (...) {
        }
    }
}

void EditorModule::ResetRenderSubmissionState() {
    m_RenderGeneration =
        Stack::EditorRenderScheduling::NextGlobalGeneration();
    InvalidateRenderSnapshotsBefore(m_RenderGeneration);
    m_RenderPending = false;
    m_RenderDirty = true;
    m_LastCompletedRenderGeneration = 0;
    m_LastSubmittedRenderRevision = 0;
    m_HdrMergeRequestedGenerations.clear();
    m_HdrMergeCompletedGenerations.clear();
    m_HdrMergeFailureMessages.clear();
    m_HdrMergeRenderingNodeIds.clear();
    m_HdrMergeSubmittedNodesByGeneration.clear();
    m_Viewport.ResetSinglePreviewState();
    ClearViewportOutputTiles();
    m_HoverFade = 0.0f;
}

const Stack::RawWorkspace::SourceRecord* EditorModule::FindRawWorkspaceSourceByKey(const std::string& sourceKey) const {
    if (m_PinnedRawWorkspaceSource.has_value() &&
        m_PinnedRawWorkspaceSource->relativePathKey == sourceKey) {
        return &(*m_PinnedRawWorkspaceSource);
    }
    const auto it = std::find_if(
        m_RawWorkspace.sources.begin(),
        m_RawWorkspace.sources.end(),
        [&](const Stack::RawWorkspace::SourceRecord& source) {
            return source.relativePathKey == sourceKey;
        });
    return it == m_RawWorkspace.sources.end() ? nullptr : &(*it);
}

Stack::RawWorkspace::SourceRecord* EditorModule::FindRawWorkspaceSourceByKey(const std::string& sourceKey) {
    if (m_PinnedRawWorkspaceSource.has_value() &&
        m_PinnedRawWorkspaceSource->relativePathKey == sourceKey) {
        return &(*m_PinnedRawWorkspaceSource);
    }
    auto it = std::find_if(
        m_RawWorkspace.sources.begin(),
        m_RawWorkspace.sources.end(),
        [&](const Stack::RawWorkspace::SourceRecord& source) {
            return source.relativePathKey == sourceKey;
        });
    return it == m_RawWorkspace.sources.end() ? nullptr : &(*it);
}

void EditorModule::PreserveActiveRawProjectSourceForLibraryNavigation() {
    if (!IsRawWorkspaceProjectActive() ||
        m_Project->rawSourceKey.empty() ||
        (m_PinnedRawWorkspaceSource.has_value() &&
         m_PinnedRawWorkspaceSource->relativePathKey ==
             m_Project->rawSourceKey)) {
        return;
    }
    const auto source = std::find_if(
        m_RawWorkspace.sources.begin(),
        m_RawWorkspace.sources.end(),
        [&](const Stack::RawWorkspace::SourceRecord& candidate) {
            return candidate.relativePathKey ==
                m_Project->rawSourceKey;
        });
    if (source == m_RawWorkspace.sources.end()) {
        return;
    }
    m_RawWorkspaceSelectedSourceBeforePinnedProject =
        m_RawWorkspace.selectedSourceKey;
    m_PinnedRawWorkspaceSource = *source;
    if (!m_Project->storePath.empty()) {
        m_PinnedRawWorkspaceSource->project.absolutePath =
            m_Project->storePath;
        m_PinnedRawWorkspaceSource->project.relativePath =
            m_Project->storePath.filename();
    }
}

bool EditorModule::ApplyLoadedRawProjectSessionMetadata(
    const LoadedProjectData& projectData,
    std::string* outError) {
    const bool isRawProject =
        projectData.projectKind == StackBinaryFormat::kRawProjectKind;
    m_Project->singleRawSource = isRawProject
        ? projectData.decodedRawSource
        : std::shared_ptr<const Raw::RawImageData>();
    if (!isRawProject) {
        if (m_PinnedRawWorkspaceSource.has_value() &&
            m_RawWorkspace.selectedSourceKey ==
                m_PinnedRawWorkspaceSource->relativePathKey) {
            m_RawWorkspace.selectedSourceKey =
                m_RawWorkspaceSelectedSourceBeforePinnedProject;
        }
        m_Project->rawSourceKey.clear();
        m_PinnedRawWorkspaceSource.reset();
        m_RawWorkspaceSelectedSourceBeforePinnedProject.clear();
        m_Project->rawPipelineActive = false;
        m_RawWorkspaceStaleRenderStatusText.clear();
        m_Project->storePath.clear();
        m_Project->store = projectData.projectStore;
        m_Project->snapshot = projectData.rawProjectSnapshot;
        if (m_Project->store && m_Project->snapshot) {
            const Stack::Project::ProjectReplacementToken replacement =
                m_Project->lifecycle.BeginReplacement();
            if (!m_Project->lifecycle.CompleteReplacement(
                    replacement,
                    m_Project->snapshot->projectId,
                    m_Project->snapshot->dirtyRevision,
                    m_Project->snapshot->persistedStorageRevision,
                    m_Project->store->IsReadOnlyRecovery())) {
                if (outError) {
                    *outError = "The unified project session could not be activated.";
                }
                return false;
            }
        } else {
            m_Project->lifecycle.Clear();
        }
        m_Project->rawRecipe = {};
        m_Project->rawMode = Stack::RawWorkspace::RawProjectMode::UnifiedLayers;
        m_Project->managedRaw = {};
        return true;
    }

    if (projectData.transientRawPreview) {
        if (projectData.rawProjectSnapshot || projectData.projectStore) {
            if (outError) {
                *outError = "The temporary RAW preview has conflicting project storage.";
            }
            return false;
        }

        StackBinaryFormat::ProjectDocument document;
        document.rawWorkspaceData = projectData.rawWorkspaceData;
        Stack::RawWorkspace::ProjectInfo projectInfo;
        Stack::RawRecipe::RawDevelopmentRecipe recipe;
        if (!Stack::RawWorkspace::ReadProjectInfoFromDocument(
                document, projectInfo, &recipe)) {
            if (outError) {
                *outError = projectInfo.errorMessage.empty()
                    ? "The temporary RAW preview metadata is invalid."
                    : projectInfo.errorMessage;
            }
            return false;
        }
        if (projectInfo.mode !=
            Stack::RawWorkspace::RawProjectMode::UnifiedLayers) {
            if (outError) {
                *outError = "The temporary RAW preview is not recipe-backed.";
            }
            return false;
        }

        const std::string sourceKey =
            !projectInfo.sourceRelativePathKey.empty()
                ? projectInfo.sourceRelativePathKey
                : recipe.source.relativePathKey;
        const Stack::RawWorkspace::SourceRecord* source =
            FindRawWorkspaceSourceByKey(sourceKey);
        if (!source) {
            if (outError) {
                *outError = "The Gallery image for the temporary RAW preview is unavailable.";
            }
            return false;
        }

        m_Project->store.reset();
        m_Project->snapshot.reset();
        m_Project->lifecycle.Clear();
        m_PinnedRawWorkspaceSource.reset();
        m_RawWorkspaceSelectedSourceBeforePinnedProject.clear();
        m_Project->rawSourceKey = source->relativePathKey;
        m_RawWorkspace.selectedSourceKey = source->relativePathKey;
        m_Project->storePath =
            std::filesystem::path(projectData.projectFileName).lexically_normal();
        m_Project->rawRecipe = std::move(recipe);
        m_Project->rawMode = projectInfo.mode;
        m_Project->managedRaw = {};
        m_Project->rawPipelineActive = true;
        m_RawWorkspaceLockedByEditorProject = false;
        m_RawWorkspaceStaleRenderStatusText.clear();
        return true;
    }

    if (projectData.rawProjectSnapshot && projectData.projectStore) {
        const Stack::Project::ModelValidationResult validation =
            Stack::Project::ValidateRawProjectSnapshot(
                *projectData.rawProjectSnapshot);
        if (!validation.valid) {
            if (outError) {
                *outError = validation.errors.empty()
                    ? "The multi-frame RAW project manifest is invalid."
                    : validation.errors.front();
            }
            return false;
        }
        m_Project->storePath =
            std::filesystem::path(projectData.projectFileName).lexically_normal();
        m_Project->store = projectData.projectStore;
        m_Project->snapshot = projectData.rawProjectSnapshot;
        if (m_Project->snapshot->coverThumbnailBytes.empty()) {
            m_MultiFrameProjectCoverRefreshPending = true;
        }
        m_Project->rawPipelineActive = true;
        m_RawWorkspaceLockedByEditorProject = false;
        const bool multiFrameProject = IsMultiFrameRawProjectActive();
        if (multiFrameProject) {
            m_Project->rawSourceKey.clear();
            m_PinnedRawWorkspaceSource.reset();
            m_RawWorkspaceSelectedSourceBeforePinnedProject.clear();
            m_Project->rawRecipe = {};
            m_Project->rawMode =
                Stack::RawWorkspace::RawProjectMode::UnifiedLayers;
            m_Project->managedRaw = {};
            m_RawWorkspaceStaleRenderStatusText =
                "Process the burst to create its developed RAW result.";
        } else {
            StackBinaryFormat::ProjectDocument document;
            document.rawWorkspaceData = projectData.rawWorkspaceData;
            Stack::RawWorkspace::ProjectInfo projectInfo;
            Stack::RawRecipe::RawDevelopmentRecipe recipe;
            if (!Stack::RawWorkspace::ReadProjectInfoFromDocument(
                    document, projectInfo, &recipe)) {
                if (outError) {
                    *outError = projectInfo.errorMessage.empty()
                        ? "The single-image RAW project metadata is invalid."
                        : projectInfo.errorMessage;
                }
                return false;
            }

            Stack::RawWorkspace::SourceRecord pinnedSource;
            pinnedSource.absolutePath = recipe.source.sourcePath;
            pinnedSource.relativePathKey = !projectInfo.sourceRelativePathKey.empty()
                ? projectInfo.sourceRelativePathKey
                : recipe.source.relativePathKey;
            if (pinnedSource.relativePathKey.empty()) {
                pinnedSource.relativePathKey =
                    "project-asset:" + m_Project->snapshot->projectId;
            }
            pinnedSource.relativePath = pinnedSource.relativePathKey;
            pinnedSource.fileName = !recipe.source.displayName.empty()
                ? recipe.source.displayName
                : pinnedSource.absolutePath.filename().string();
            pinnedSource.stem =
                std::filesystem::path(pinnedSource.fileName).stem().string();
            pinnedSource.extension =
                std::filesystem::path(pinnedSource.fileName).extension().string();
            pinnedSource.fileSizeBytes = projectInfo.sourceFileSizeBytes;
            pinnedSource.modifiedTimeTicks = projectInfo.sourceModifiedTimeTicks;
            pinnedSource.fingerprint = projectInfo.sourceFingerprint;
            pinnedSource.project = projectInfo;
            pinnedSource.project.absolutePath = m_Project->storePath;
            pinnedSource.project.relativePath =
                m_Project->storePath.filename();
            pinnedSource.project.status =
                Stack::RawWorkspace::ProjectStatus::Existing;
            pinnedSource.project.linkedRaw = false;
            pinnedSource.project.embeddedRaw = false;
            pinnedSource.project.associationReason =
                "Project-owned RAW asset";

            auto catalogSource = std::find_if(
                m_RawWorkspace.sources.begin(),
                m_RawWorkspace.sources.end(),
                [&](const Stack::RawWorkspace::SourceRecord& source) {
                    return (!pinnedSource.fingerprint.empty() &&
                            source.fingerprint == pinnedSource.fingerprint) ||
                        source.relativePathKey ==
                            pinnedSource.relativePathKey;
                });
            if (catalogSource != m_RawWorkspace.sources.end()) {
                catalogSource->project = pinnedSource.project;
                m_PinnedRawWorkspaceSource.reset();
                m_RawWorkspaceSelectedSourceBeforePinnedProject.clear();
                m_Project->rawSourceKey =
                    catalogSource->relativePathKey;
            } else {
                m_RawWorkspaceSelectedSourceBeforePinnedProject =
                    m_RawWorkspace.selectedSourceKey;
                m_PinnedRawWorkspaceSource = std::move(pinnedSource);
                m_Project->rawSourceKey =
                    m_PinnedRawWorkspaceSource->relativePathKey;
            }
            // A unified single-RAW bundle is already the active editing
            // session. Select its resolved catalog or pinned source so RAW Lab
            // does not render its empty "Select an image" state.
            m_RawWorkspace.selectedSourceKey =
                m_Project->rawSourceKey;
            m_Project->rawRecipe = std::move(recipe);
            m_Project->rawMode = projectInfo.mode;
            m_Project->managedRaw = projectData.rawWorkspaceData.is_object()
                ? Stack::RawWorkspace::DeserializeManagedRawSection(
                    projectData.rawWorkspaceData.value(
                        "managedRawSection", nlohmann::json::object()))
                : Stack::RawWorkspace::ManagedRawSection{};
            m_RawWorkspaceStaleRenderStatusText.clear();
        }
        const Stack::Project::ProjectReplacementToken replacement =
            m_Project->lifecycle.BeginReplacement();
        if (!m_Project->lifecycle.CompleteReplacement(
                replacement,
                m_Project->snapshot->projectId,
                m_Project->snapshot->dirtyRevision,
                m_Project->snapshot->persistedStorageRevision,
                m_Project->store->IsReadOnlyRecovery())) {
            if (outError) *outError = "The project session could not be activated.";
            return false;
        }
        if (multiFrameProject) {
            RestoreHdrResultCacheAfterProjectLoad();
        }
        return true;
    }

    if (outError) {
        *outError = "The RAW project does not contain the current project store and snapshot.";
    }
    return false;
}

Stack::RawRecipe::RawDevelopmentRecipe EditorModule::BuildRawWorkspaceDefaultRecipe(
    const Stack::RawWorkspace::SourceRecord& source) const {
    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe(source.absolutePath.string(), source.fileName);
    recipe.source.relativePathKey = source.relativePathKey;
    recipe.source.fingerprint = source.fingerprint;
    recipe.source.fileSizeBytes = static_cast<std::uint64_t>(source.fileSizeBytes);
    recipe.source.modifiedTimeTicks = source.modifiedTimeTicks;
    recipe.source.displayName = source.fileName;
    return recipe;
}

bool EditorModule::BuildRawWorkspaceProjectGraph(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    bool markEdited,
    std::string* outError) {
    if (outError) {
        outError->clear();
    }
    ResetForPipelineDeserialization();

    EditorNodeGraph::RawDevelopmentPayload payload;
    payload.recipe = recipe;
    payload.projectStatus = markEdited ? "Edited" : "Not Edited";
    payload.edited = markEdited;
    payload.autosaved = false;

    EditorNodeGraph::Node* rawNode =
        m_Project->graph.AddRawDevelopmentNode(std::move(payload), EditorNodeGraph::Vec2{ 20.0f, 120.0f });
    if (!rawNode) {
        if (outError) {
            *outError = "Failed to create the RAW Development node.";
        }
        return false;
    }
    const int rawNodeId = rawNode->id;

    EditorNodeGraph::Node* outputNode =
        m_Project->graph.AddOutputNode(EditorNodeGraph::Vec2{ 320.0f, 120.0f }, true);
    if (!outputNode) {
        if (outError) {
            *outError = "Failed to create the Output node.";
        }
        return false;
    }
    const int outputNodeId = outputNode->id;

    std::string connectError;
    if (!m_Project->graph.TryConnectSockets(
            rawNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            outputNodeId,
            EditorNodeGraph::kImageInputSocketId,
            &connectError)) {
        if (outError) {
            *outError = connectError.empty()
                ? "Failed to connect the RAW Development node to the Output node."
                : connectError;
        }
        return false;
    }

    m_Project->graph.SetOutputNodeId(outputNodeId);
    SelectGraphNode(rawNodeId);
    if (markEdited) {
        MarkRenderDirty(rawNodeId);
    } else {
        MarkRenderRefreshDirty();
    }
    return true;
}

bool EditorModule::RequestLoadRawWorkspaceProjectForSource(
    const Stack::RawWorkspace::SourceRecord& source,
    bool includeNodeBrowserThumbnails) {
    if (!m_RawWorkspaceRootTabActive || source.project.absolutePath.empty()) {
        return false;
    }

    if (Async::IsBusy(m_RawWorkspaceProjectLoadTaskState) &&
        m_RawWorkspaceProjectLoadSourceKey == source.relativePathKey) {
        return true;
    }

    const std::uint64_t generation =
        m_RawWorkspaceProjectLoadGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Queued;
    m_RawWorkspaceProjectLoadSourceKey = source.relativePathKey;
    m_RawWorkspaceProjectLoadStatusText = "Loading RAW project...";

    bool submitted = false;
    try {
        submitted = ProjectTasks().Submit("Loading project",
            [this, generation, source, includeNodeBrowserThumbnails]() mutable {
                auto isLoadCanceled = [this, generation]() {
                    return generation !=
                        m_RawWorkspaceProjectLoadGeneration.load(std::memory_order_relaxed);
                };
                if (isLoadCanceled()) {
                    return;
                }

                RawWorkspaceProjectLoadResult result;
                result.source = source;

                try {
                    StackBinaryFormat::ProjectDocument document;
                    StackBinaryFormat::ProjectLoadOptions options;
                    options.includeThumbnail = false;
                    options.includeSourceImage = false;
                    options.includePipelineData = true;
                    options.includeNodeBrowserThumbnails = includeNodeBrowserThumbnails;
                    options.includeRawWorkspaceData = true;
                    if (!StackBinaryFormat::ReadProjectFile(
                            source.project.absolutePath,
                            document,
                            options)) {
                        if (isLoadCanceled()) {
                            return;
                        }
                        result.errorMessage = "Failed to load the RAW project.";
                    } else {
                        if (isLoadCanceled()) {
                            return;
                        }
                        result.loadedProject.sourceState =
                            ProjectSourceState::LazyAsset;
                        result.loadedProject.sourcePixels.clear();
                        result.loadedProject.width = 0;
                        result.loadedProject.height = 0;
                        result.loadedProject.channels = 4;
                        result.loadedProject.pipelineData = document.pipelineData.is_null()
                            ? nlohmann::json::array()
                            : document.pipelineData;
                        result.loadedProject.rawWorkspaceData = document.rawWorkspaceData;
                        result.loadedProject.projectStore = document.projectStore;
                        result.loadedProject.rawProjectSnapshot =
                            document.rawProjectSnapshot;
                        result.loadedProject.projectId = document.projectId;
                        result.loadedProject.adoptedFrom = document.adoptedFrom;
                        result.loadedProject.projectKind = StackBinaryFormat::kRawProjectKind;
                        result.loadedProject.projectName = document.metadata.projectName.empty()
                            ? (source.stem.empty() ? source.fileName : source.stem)
                            : document.metadata.projectName;
                        result.loadedProject.projectFileName =
                            source.project.absolutePath.string();
                        result.loadedProject.nodeBrowserThumbnailEntries =
                            document.nodeBrowserThumbnailEntries;

                        Stack::RawWorkspace::ProjectInfo projectInfo;
                        if (Stack::RawWorkspace::ReadProjectInfoFromDocument(
                                document,
                                projectInfo,
                                &result.recipe)) {
                            result.mode = projectInfo.mode;
                            result.managedSection = document.rawWorkspaceData.is_object()
                                ? Stack::RawWorkspace::DeserializeManagedRawSection(
                                    document.rawWorkspaceData.value(
                                        "managedRawSection",
                                        nlohmann::json::object()))
                                : Stack::RawWorkspace::ManagedRawSection{};
                            result.hasRawWorkspaceInfo = true;
                        }
                        result.success = result.loadedProject.projectStore &&
                            result.loadedProject.rawProjectSnapshot &&
                            result.hasRawWorkspaceInfo;
                        if (!result.success) {
                            result.errorMessage =
                                "The RAW project does not contain a complete current project store.";
                        }
                    }
                } catch (const std::exception& error) {
                    result.success = false;
                    result.errorMessage =
                        std::string("Failed to load the RAW project: ") + error.what();
                } catch (...) {
                    result.success = false;
                    result.errorMessage = "Failed to load the RAW project.";
                }
                if (isLoadCanceled()) {
                    return;
                }

                ProjectTasks().PostToMain(
                    [this, generation, result = std::move(result)]() mutable {
                        // This editor owns the load even when Graph is visible.
                        // Dropping an owned completion on a view change leaves
                        // the RAW loading state queued indefinitely.
                        if (generation != m_RawWorkspaceProjectLoadGeneration.load(
                                std::memory_order_relaxed)) {
                            return;
                        }

                        if (!result.success) {
                            m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Failed;
                            m_RawWorkspaceProjectLoadStatusText = result.errorMessage.empty()
                                ? "Failed to load the RAW project."
                                : result.errorMessage;
                            if (m_PendingRawWorkspaceOpenGraphSourceKey ==
                                result.source.relativePathKey) {
                                m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
                                m_PendingRawWorkspaceOpenGraphSourceKey.clear();
                            }
                            PostNotification(
                                UiNotificationSeverity::Error,
                                m_RawWorkspaceProjectLoadStatusText,
                                "raw-workspace-project-load");
                            if (IsRawWorkspaceProjectActive()) {
                                m_RawWorkspace.selectedSourceKey =
                                    m_Project->rawSourceKey;
                            }
                            return;
                        }

                        auto loadedProject = std::make_shared<EditorLoadedProjectData>(
                            std::move(result.loadedProject));
                        if (!BeginDeferredLoadedProjectApply(loadedProject)) {
                            m_PendingRawWorkspaceDeferredProjectFinalize = false;
                            m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
                            if (m_PendingRawWorkspaceOpenGraphSourceKey ==
                                result.source.relativePathKey) {
                                m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
                                m_PendingRawWorkspaceOpenGraphSourceKey.clear();
                            }
                            m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Failed;
                            m_RawWorkspaceProjectLoadStatusText =
                                "Failed to apply the RAW project.";
                            PostNotification(
                                UiNotificationSeverity::Error,
                                m_RawWorkspaceProjectLoadStatusText,
                                "raw-workspace-project-load");
                            if (IsRawWorkspaceProjectActive()) {
                                m_RawWorkspace.selectedSourceKey =
                                    m_Project->rawSourceKey;
                            }
                            return;
                        }

                        m_Project->rawRecipe = std::move(result.recipe);
                        m_Project->rawMode = result.mode;
                        m_Project->managedRaw = result.managedSection;
                        m_Project->rawSourceKey = result.source.relativePathKey;
                        m_RawWorkspaceStaleRenderStatusText.clear();
                        m_Project->storePath =
                            result.source.project.absolutePath;
                        m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Applying;
                        m_RawWorkspaceProjectLoadStatusText = "Applying RAW project...";
                        m_PendingRawWorkspaceDeferredProjectFinalize = true;
                        m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey =
                            result.source.relativePathKey;
                    });
            });
    } catch (...) {
        submitted = false;
    }

    if (submitted) {
        return true;
    }
    if (generation ==
        m_RawWorkspaceProjectLoadGeneration.load(std::memory_order_relaxed)) {
        m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Failed;
        m_RawWorkspaceProjectLoadStatusText = "Could not queue the RAW project load.";
        if (m_PendingRawWorkspaceOpenGraphSourceKey == source.relativePathKey) {
            m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
            m_PendingRawWorkspaceOpenGraphSourceKey.clear();
        }
        PostNotification(
            UiNotificationSeverity::Error,
            m_RawWorkspaceProjectLoadStatusText,
            "raw-workspace-project-load");
    }
    return false;
}

void EditorModule::FinalizeDeferredRawWorkspaceProjectLoadIfNeeded() {
    if (!m_PendingRawWorkspaceDeferredProjectFinalize) {
        return;
    }
    if (m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey != m_Project->rawSourceKey) {
        const std::string deferredSourceKey = m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey;
        m_PendingRawWorkspaceDeferredProjectFinalize = false;
        m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
        if (m_PendingRawWorkspaceOpenGraphSourceKey == deferredSourceKey) {
            m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
            m_PendingRawWorkspaceOpenGraphSourceKey.clear();
        }
        if (m_RawWorkspaceProjectLoadSourceKey == deferredSourceKey) {
            m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Idle;
            m_RawWorkspaceProjectLoadSourceKey.clear();
            m_RawWorkspaceProjectLoadStatusText.clear();
        }
        return;
    }

    m_Project->rawPipelineActive = true;
    m_RawWorkspaceLockedByEditorProject = false;
    ClearDirty();

    m_PendingRawWorkspaceDeferredProjectFinalize = false;
    m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
    m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Idle;
    const Stack::RawWorkspace::SourceRecord* activeSource =
        FindRawWorkspaceSourceByKey(m_Project->rawSourceKey);
    const bool activeSourceHasStoredProject =
        activeSource &&
        (activeSource->project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
         activeSource->project.status == Stack::RawWorkspace::ProjectStatus::Embedded);
    m_RawWorkspaceProjectLoadStatusText = activeSourceHasStoredProject
        ? "RAW project loaded."
        : "RAW preview ready.";

    if (m_PendingRawWorkspaceOpenGraphAfterProjectLoad &&
        m_PendingRawWorkspaceOpenGraphSourceKey == m_Project->rawSourceKey) {
        m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
        m_PendingRawWorkspaceOpenGraphSourceKey.clear();
        FocusRawWorkspaceDevelopmentNode();
        RequestOpenEditorTab();
    }
}

bool EditorModule::ResolveRawWorkspaceRecipeForSource(
    const Stack::RawWorkspace::SourceRecord& source,
    Stack::RawRecipe::RawDevelopmentRecipe& outRecipe,
    Stack::RawWorkspace::RawProjectMode* outMode,
    std::string* outError) const {
    if (IsRawWorkspaceProjectActive() &&
        m_Project->rawSourceKey == source.relativePathKey) {
        outRecipe = ReadRawControlRecipe();
        if (outMode) {
            *outMode = m_Project->rawMode;
        }
        return true;
    }

    if (source.project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
        source.project.status == Stack::RawWorkspace::ProjectStatus::Embedded) {
        const auto cacheIt = m_RawWorkspaceRecipePreviewCache.find(source.relativePathKey);
        if (cacheIt != m_RawWorkspaceRecipePreviewCache.end() &&
            cacheIt->second.projectPath == source.project.absolutePath &&
            cacheIt->second.projectModifiedTimeTicks == source.project.projectModifiedTimeTicks) {
            outRecipe = cacheIt->second.recipe;
            if (outMode) {
                *outMode = cacheIt->second.mode;
            }
            if (outError) {
                *outError = cacheIt->second.errorMessage;
            }
            return cacheIt->second.success;
        }

        RawWorkspaceRecipePreviewCacheEntry cacheEntry;
        cacheEntry.projectPath = source.project.absolutePath;
        cacheEntry.projectModifiedTimeTicks = source.project.projectModifiedTimeTicks;
        cacheEntry.mode = source.project.mode;

        StackBinaryFormat::ProjectDocument document;
        StackBinaryFormat::ProjectLoadOptions options;
        options.includeThumbnail = false;
        options.includeSourceImage = false;
        options.includePipelineData = false;
        options.includeNodeBrowserThumbnails = false;
        options.includeRawWorkspaceData = true;
        if (!StackBinaryFormat::ReadProjectFile(source.project.absolutePath, document, options)) {
            outRecipe = BuildRawWorkspaceDefaultRecipe(source);
            cacheEntry.recipe = outRecipe;
            cacheEntry.success = false;
            cacheEntry.errorMessage = "The selected RAW project could not be read.";
            m_RawWorkspaceRecipePreviewCache[source.relativePathKey] = cacheEntry;
            if (outError) {
                *outError = cacheEntry.errorMessage;
            }
            if (outMode) {
                *outMode = source.project.mode;
            }
            return false;
        }

        Stack::RawWorkspace::ProjectInfo info;
        if (!Stack::RawWorkspace::ReadProjectInfoFromDocument(document, info, &outRecipe)) {
            outRecipe = BuildRawWorkspaceDefaultRecipe(source);
            cacheEntry.recipe = outRecipe;
            cacheEntry.success = false;
            cacheEntry.errorMessage = info.errorMessage.empty()
                ? "The selected project does not contain RAW Workspace metadata."
                : info.errorMessage;
            m_RawWorkspaceRecipePreviewCache[source.relativePathKey] = cacheEntry;
            if (outError) {
                *outError = cacheEntry.errorMessage;
            }
            if (outMode) {
                *outMode = source.project.mode;
            }
            return false;
        }
        if (info.status == Stack::RawWorkspace::ProjectStatus::Invalid) {
            outRecipe = BuildRawWorkspaceDefaultRecipe(source);
            cacheEntry.recipe = outRecipe;
            cacheEntry.mode = info.mode;
            cacheEntry.success = false;
            cacheEntry.errorMessage = info.errorMessage.empty()
                ? "The selected project contains invalid RAW Workspace metadata."
                : info.errorMessage;
            m_RawWorkspaceRecipePreviewCache[source.relativePathKey] = cacheEntry;
            if (outError) {
                *outError = cacheEntry.errorMessage;
            }
            if (outMode) {
                *outMode = info.mode;
            }
            return false;
        }

        cacheEntry.recipe = outRecipe;
        cacheEntry.mode = info.mode;
        cacheEntry.success = true;
        cacheEntry.errorMessage.clear();
        m_RawWorkspaceRecipePreviewCache[source.relativePathKey] = cacheEntry;
        if (outMode) {
            *outMode = info.mode;
        }
        return true;
    }

    outRecipe = BuildRawWorkspaceDefaultRecipe(source);
    if (outMode) {
        *outMode = Stack::RawWorkspace::RawProjectMode::UnifiedLayers;
    }
    return true;
}

bool EditorModule::FocusRawWorkspaceDevelopmentNode() {
    SwitchToSubWindow(EditorSubWindow::NodeGraph);
    if (m_Project->rawMode == Stack::RawWorkspace::RawProjectMode::ManagedDecomposed &&
        m_Project->managedRaw.rawDecodeNodeId > 0 &&
        m_Project->graph.FindNode(m_Project->managedRaw.rawDecodeNodeId)) {
        SelectGraphNode(m_Project->managedRaw.rawDecodeNodeId);
        return true;
    }
    if (m_Project->rawMode == Stack::RawWorkspace::RawProjectMode::CustomGraph &&
        m_Project->managedRaw.rawSourceNodeId > 0 &&
        m_Project->graph.FindNode(m_Project->managedRaw.rawSourceNodeId)) {
        SelectGraphNode(m_Project->managedRaw.rawSourceNodeId);
        return true;
    }
    for (const EditorNodeGraph::Node& node : m_Project->graph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::RawDevelopment) {
            continue;
        }
        const std::string& key = node.rawDevelopment.recipe.source.relativePathKey;
        if (m_Project->rawSourceKey.empty() || key.empty() || key == m_Project->rawSourceKey) {
            SelectGraphNode(node.id);
            return true;
        }
    }
    return false;
}

bool EditorModule::OpenRawWorkspaceProjectInGraph(const Stack::RawWorkspace::SourceRecord& source) {
    if (source.project.status != Stack::RawWorkspace::ProjectStatus::Existing &&
        source.project.status != Stack::RawWorkspace::ProjectStatus::Embedded) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "Make an edit to create this RAW project first.",
            "raw-workspace-open-graph-preview-only");
        return false;
    }

    if (!IsRawWorkspaceProjectActive() || m_Project->rawSourceKey != source.relativePathKey) {
        if (!FlushActiveRawWorkspaceProjectIfDirty()) {
            return false;
        }
        m_PendingRawWorkspaceOpenGraphAfterProjectLoad = true;
        m_PendingRawWorkspaceOpenGraphSourceKey = source.relativePathKey;
        if (!RequestLoadRawWorkspaceProjectForSource(source, true)) {
            m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
            m_PendingRawWorkspaceOpenGraphSourceKey.clear();
            return false;
        }
        return true;
    }

    FocusRawWorkspaceDevelopmentNode();
    RequestOpenEditorTab();
    return true;
}

bool EditorModule::StageRawWorkspaceProjectForSourcePreview(
    const Stack::RawWorkspace::SourceRecord& source,
    bool createNewProject) {
    if (!createNewProject &&
        (source.project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
         source.project.status == Stack::RawWorkspace::ProjectStatus::Embedded)) {
        return RequestLoadRawWorkspaceProjectForSource(source);
    }

    Stack::RawRecipe::RawDevelopmentRecipe recipe = BuildRawWorkspaceDefaultRecipe(source);
    EditorNodeGraph::Graph previewGraph;
    EditorNodeGraph::RawDevelopmentPayload payload;
    payload.recipe = recipe;
    payload.projectStatus = "Not Edited";
    payload.edited = false;
    payload.autosaved = false;

    std::string graphError;
    EditorNodeGraph::Node* rawNode =
        previewGraph.AddRawDevelopmentNode(std::move(payload), EditorNodeGraph::Vec2{ 20.0f, 120.0f });
    if (!rawNode) {
        graphError = "Failed to create the RAW Development node.";
    }
    const int rawNodeId = rawNode ? rawNode->id : -1;

    EditorNodeGraph::Node* outputNode = graphError.empty()
        ? previewGraph.AddOutputNode(EditorNodeGraph::Vec2{ 320.0f, 120.0f }, true)
        : nullptr;
    if (graphError.empty() && !outputNode) {
        graphError = "Failed to create the Output node.";
    }
    const int outputNodeId = outputNode ? outputNode->id : -1;

    if (graphError.empty() &&
        !previewGraph.TryConnectSockets(
            rawNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            outputNodeId,
            EditorNodeGraph::kImageInputSocketId,
            &graphError)) {
        if (graphError.empty()) {
            graphError = "Failed to connect the RAW Development node to the Output node.";
        }
    }

    if (!graphError.empty()) {
        std::cerr
            << "[RAW Workspace] Failed to create preview project graph"
            << " sourceKey=" << source.relativePathKey
            << " projectPath=" << Stack::RawWorkspace::BuildExpectedProjectInfo(
                Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot),
                source).absolutePath.string()
            << " error=" << graphError
            << "\n";
        PostNotification(
            UiNotificationSeverity::Error,
            "Failed to create the RAW project graph: " + graphError,
            "raw-workspace-project-preview-graph-create");
        return false;
    }
    previewGraph.SetOutputNodeId(outputNodeId);
    previewGraph.SelectNode(rawNodeId);

    const Stack::RawWorkspace::ManagedLayout layout =
        Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot);
    const Stack::RawWorkspace::ProjectInfo expectedProject =
        Stack::RawWorkspace::BuildExpectedProjectInfo(layout, source);
    const std::string projectName = source.stem.empty() ? source.fileName : source.stem;

    auto loadedProject = std::make_shared<EditorLoadedProjectData>();
    // The RAW Development node owns lazy decode from the source reference.
    // Never invent a 1x1 base image whose dimensions disagree with the RAW.
    loadedProject->sourceState = ProjectSourceState::LazyAsset;
    loadedProject->sourcePixels.clear();
    loadedProject->width = 0;
    loadedProject->height = 0;
    loadedProject->channels = 4;
    loadedProject->pipelineData =
        EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(), previewGraph);
    loadedProject->pipelineData["rawLayerStack"] = Stack::Project::SerializeRawLayerStack(Stack::Project::RawLayerStackState{});
    loadedProject->pipelineData["rawLayerSourceNodeUuid"] = previewGraph.FindNode(rawNodeId)->instanceUuid;
    StackBinaryFormat::ProjectDocument previewDocument;
    Stack::RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
        source,
        recipe,
        nlohmann::json::object(),
        previewDocument,
        Stack::RawWorkspace::RawProjectMode::UnifiedLayers,
        true);
    loadedProject->rawWorkspaceData = std::move(previewDocument.rawWorkspaceData);
    loadedProject->projectKind = StackBinaryFormat::kRawProjectKind;
    loadedProject->transientRawPreview = true;
    loadedProject->projectName = projectName;
    loadedProject->projectFileName = expectedProject.absolutePath.string();

    if (!BeginDeferredLoadedProjectApply(loadedProject)) {
        m_PendingRawWorkspaceDeferredProjectFinalize = false;
        m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
        m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Failed;
        m_RawWorkspaceProjectLoadStatusText = "Failed to apply the RAW preview.";
        if (IsRawWorkspaceProjectActive()) {
            m_RawWorkspace.selectedSourceKey = m_Project->rawSourceKey;
        }
        return false;
    }

    m_Project->rawSourceKey = source.relativePathKey;
    m_RawWorkspaceStaleRenderStatusText.clear();
    m_Project->storePath = expectedProject.absolutePath;
    m_Project->rawRecipe = std::move(recipe);
    m_Project->rawMode = Stack::RawWorkspace::RawProjectMode::UnifiedLayers;
    m_Project->managedRaw = {};
    // BeginDeferredLoadedProjectApply intentionally deactivates the previous
    // session before deserialization. This newly constructed preview is now
    // the active RAW session and must be identifiable as such before its first
    // render is submitted; otherwise the loader can wait on a presentation
    // whose snapshot was emitted as an untagged generic graph result.
    m_Project->rawPipelineActive = true;
    m_RawWorkspaceProjectLoadSourceKey = source.relativePathKey;
    m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Applying;
    m_RawWorkspaceProjectLoadStatusText = "Applying RAW preview...";
    m_PendingRawWorkspaceDeferredProjectFinalize = true;
    m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey = source.relativePathKey;
    return true;
}

bool EditorModule::LoadActiveRawWorkspaceProjectInGraph() {
    if (!IsRawWorkspaceProjectActive()) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "Open a RAW project before loading it into the graph.",
            "raw-workspace-load-current-no-project");
        return false;
    }
    const bool focused = FocusRawWorkspaceDevelopmentNode();
    if (focused) {
        RequestOpenEditorTab();
    }
    return focused;
}

bool EditorModule::MaterializeSelectedRawPreviewProject(
    const Stack::RawWorkspace::SourceRecord& source,
    Stack::RawRecipe::RawDevelopmentRecipe& managedRecipe,
    std::string* outError) {
    namespace Project = Stack::Project;
    if (source.absolutePath.empty()) {
        if (outError) *outError = "The viewed RAW source path is unavailable.";
        return false;
    }

    const Stack::RawWorkspace::ManagedLayout layout =
        Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot);
    std::filesystem::path projectRoot =
        layout.projectsDirectory /
        Stack::RawWorkspace::BuildProjectRelativePathForSource(source);
    std::error_code existsError;
    if (std::filesystem::exists(projectRoot, existsError) && !existsError) {
        const std::filesystem::path base = projectRoot;
        for (unsigned int suffix = 2u; suffix < 10000u; ++suffix) {
            std::filesystem::path candidate = base;
            candidate += "-" + std::to_string(suffix);
            existsError.clear();
            if (!std::filesystem::exists(candidate, existsError) && !existsError) {
                projectRoot = std::move(candidate);
                break;
            }
        }
    }

    const std::string projectName = source.stem.empty()
        ? source.fileName
        : source.stem;
    Project::RawProjectSnapshot bootstrap;
    bootstrap.projectId = Project::GenerateStableUuid();
    bootstrap.projectName = projectName.empty() ? "Untitled RAW Project" : projectName;
    bootstrap.projectKindHint = StackBinaryFormat::kRawProjectKind;
    bootstrap.lifecycle.creationOrigin =
        Project::ProjectCreationOrigin::AutoFromViewer;
    bootstrap.lifecycle.cleanupWhenUntouched = true;
    bootstrap.lifecycle.autoCreatedAtDirtyRevision = 1u;

    const nlohmann::json untouchedRecipe =
        Stack::RawRecipe::SerializeRecipe(m_Project->rawRecipe);
    const std::string untouchedText = untouchedRecipe.dump();
    const std::vector<std::uint8_t> untouchedBytes(
        untouchedText.begin(), untouchedText.end());
    const Stack::RawEvidence::SourceIdentity untouchedIdentity =
        Stack::RawEvidence::ComputeSourceIdentity(untouchedBytes);
    if (untouchedIdentity.valid) {
        bootstrap.lifecycle.untouchedStateFingerprint =
            untouchedIdentity.sha256;
    }

    Project::ProjectStoreOpenResult created = Project::CreateProjectStore(
        projectRoot,
        Project::ProjectStorageKind::DirectoryBundle,
        bootstrap);
    if (!created) {
        if (outError) *outError = created.message;
        return false;
    }

    const Project::ProjectStoreTransaction transaction =
        created.store->BeginTransaction(
            created.snapshot.persistedStorageRevision);
    if (!transaction) {
        created.store.reset();
        std::error_code cleanupError;
        std::filesystem::remove_all(projectRoot, cleanupError);
        if (outError) *outError = "Could not begin the first project asset import.";
        return false;
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
        std::error_code cleanupError;
        std::filesystem::remove_all(projectRoot, cleanupError);
        if (outError) {
            *outError = stageError.empty()
                ? "Could not copy the viewed RAW into the project."
                : stageError;
        }
        return false;
    }

    managedRecipe.source.sourcePath =
        (projectRoot / asset.projectAssetPath).lexically_normal().string();
    managedRecipe.source.relativePathKey = source.relativePathKey;
    managedRecipe.source.fingerprint = asset.originalFileFingerprint;
    managedRecipe.source.fileSizeBytes = asset.byteLength;
    managedRecipe.source.modifiedTimeTicks = source.modifiedTimeTicks;
    managedRecipe.source.displayName = source.fileName;

    for (EditorNodeGraph::Node& node : m_Project->graph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::RawDevelopment) {
            node.rawDevelopment.recipe = managedRecipe;
        }
    }

    Project::RawProjectSnapshot snapshot = created.snapshot;
    asset.workspaceRelativeSourcePath = source.relativePath.generic_string();
    snapshot.embeddedAssets.push_back(asset);
    snapshot.lifecycle.initialAssetIds.push_back(asset.assetId);
    snapshot.dirtyRevision = 1u;
    snapshot.pipelineData = SerializePipeline();
    StackBinaryFormat::ProjectDocument rawDocument;
    Stack::RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
        source,
        managedRecipe,
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
        std::error_code cleanupError;
        std::filesystem::remove_all(projectRoot, cleanupError);
        if (outError) *outError = committed.message;
        return false;
    }

    Project::ProjectStoreOpenResult opened = Project::OpenProjectStore(projectRoot);
    if (!opened) {
        if (outError) *outError = opened.message;
        return false;
    }
    m_Project->store = std::move(opened.store);
    m_Project->snapshot =
        std::make_shared<Project::RawProjectSnapshot>(
            std::move(opened.snapshot));
    if (m_Project->snapshot->coverThumbnailBytes.empty()) {
        m_MultiFrameProjectCoverRefreshPending = true;
    }
    m_Project->storePath = projectRoot.lexically_normal();
    const Project::ProjectReplacementToken replacement =
        m_Project->lifecycle.BeginReplacement();
    if (!m_Project->lifecycle.CompleteReplacement(
            replacement,
            m_Project->snapshot->projectId,
            m_Project->snapshot->dirtyRevision,
            m_Project->snapshot->persistedStorageRevision,
            false)) {
        if (outError) *outError = "Could not activate the newly created project.";
        return false;
    }
    if (outError) outError->clear();
    return true;
}

bool EditorModule::EnsureRawWorkspaceProjectForSelectedRecipeEdit(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    const Stack::RawWorkspace::SourceRecord* selectedSource =
        FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
    if (!selectedSource) {
        std::cerr
            << "[RAW Project Edit] selected source is unavailable"
            << " selectedKey=" << m_RawWorkspace.selectedSourceKey
            << " activeKey=" << m_Project->rawSourceKey
            << " catalogSources=" << m_RawWorkspace.sources.size()
            << " pinned=" << (m_PinnedRawWorkspaceSource.has_value() ? "yes" : "no")
            << "\n";
        PostNotification(
            UiNotificationSeverity::Error,
            "Select a RAW source before editing.",
            "raw-workspace-no-selection");
        return false;
    }

    const bool alreadyActive =
        IsRawWorkspaceProjectActive() &&
        m_Project->rawSourceKey == selectedSource->relativePathKey;
    if (!alreadyActive &&
        (m_RawWorkspacePreviewStageQueued &&
         m_RawWorkspacePreviewStageSourceKey == selectedSource->relativePathKey)) {
        std::cerr << "[RAW Project Edit] preview staging is still queued\n";
        return false;
    }
    if (Async::IsBusy(m_RawWorkspaceProjectLoadTaskState) &&
        m_RawWorkspaceProjectLoadSourceKey == selectedSource->relativePathKey) {
        std::cerr << "[RAW Project Edit] project loading is still active\n";
        return false;
    }
    if (m_PendingRawWorkspaceDeferredProjectFinalize &&
        m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey == selectedSource->relativePathKey &&
        IsDeferredLoadedProjectApplyActive()) {
        std::cerr << "[RAW Project Edit] deferred project apply is still active\n";
        return false;
    }
    bool needsDefaultGraph = !alreadyActive;

    if (m_Project->rawSourceKey != selectedSource->relativePathKey) {
        if (!FlushActiveRawWorkspaceProjectIfDirty()) {
            return false;
        }
        ClearRawWorkspaceLivePreviewState();
        if (selectedSource->project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
            selectedSource->project.status == Stack::RawWorkspace::ProjectStatus::Embedded) {
            if (!RequestLoadRawWorkspaceProjectForSource(*selectedSource)) {
                std::cerr << "[RAW Project Edit] existing project could not be queued for loading\n";
                return false;
            }
            PostNotification(
                UiNotificationSeverity::Info,
                "Loading this RAW project before applying edits.",
                "raw-workspace-project-load-before-edit");
            return false;
        }
    }

    Stack::RawRecipe::RawDevelopmentRecipe resolvedRecipe = recipe;
    const bool keepProjectOwnedSource =
        alreadyActive &&
        IsUnifiedProjectStoreActive() &&
        !m_Project->rawRecipe.source.sourcePath.empty();
    if (keepProjectOwnedSource) {
        // Once the first edit has copied the RAW into the project, every later
        // edit must stay bound to that managed asset. Rebinding to the browser
        // original here made otherwise healthy projects fail after the
        // original was moved or deleted.
        resolvedRecipe.source = m_Project->rawRecipe.source;
    } else {
        resolvedRecipe.source.sourcePath = selectedSource->absolutePath.string();
        resolvedRecipe.source.relativePathKey = selectedSource->relativePathKey;
        resolvedRecipe.source.fingerprint = selectedSource->fingerprint;
        resolvedRecipe.source.fileSizeBytes = static_cast<std::uint64_t>(selectedSource->fileSizeBytes);
        resolvedRecipe.source.modifiedTimeTicks = selectedSource->modifiedTimeTicks;
        resolvedRecipe.source.displayName = selectedSource->fileName;
    }

    const Stack::RawWorkspace::ManagedLayout layout =
        Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot);
    const Stack::RawWorkspace::ProjectInfo expectedProject =
        Stack::RawWorkspace::BuildExpectedProjectInfo(layout, *selectedSource);

    const bool selectedHasNoProject =
        selectedSource->project.status == Stack::RawWorkspace::ProjectStatus::NoProject ||
        selectedSource->project.absolutePath.empty();
    std::filesystem::path targetProjectPath = selectedHasNoProject
            ? expectedProject.absolutePath
            : selectedSource->project.absolutePath;
    const Stack::RawWorkspace::RawProjectMode targetMode = selectedHasNoProject
        ? Stack::RawWorkspace::RawProjectMode::UnifiedLayers
        : m_Project->rawMode;

    if (needsDefaultGraph) {
        const nlohmann::json previousPipeline = SerializePipeline();
        std::string graphError;
        if (!BuildRawWorkspaceProjectGraph(resolvedRecipe, true, &graphError)) {
            DeserializePipeline(previousPipeline);
            std::cerr
                << "[RAW Workspace] Failed to create edit project graph"
                << " selectedSourceKey=" << selectedSource->relativePathKey
                << " activeSourceKey=" << m_Project->rawSourceKey
                << " targetProjectPath=" << targetProjectPath.string()
                << " error=" << (graphError.empty() ? std::string("unknown") : graphError)
                << "\n";
            PostNotification(
                UiNotificationSeverity::Error,
                graphError.empty()
                    ? "Failed to create the RAW project graph."
                    : "Failed to create the RAW project graph: " + graphError,
                "raw-workspace-project-graph-create");
            return false;
        }
    }
    bool materializedFromViewing = false;
    if (selectedHasNoProject && !IsUnifiedProjectStoreActive()) {
        std::string materializeError;
        if (!MaterializeSelectedRawPreviewProject(
                *selectedSource,
                resolvedRecipe,
                &materializeError)) {
            std::cerr << "[RAW Project Edit] Could not materialize the project: " << materializeError << '\n';
            PostNotification(
                UiNotificationSeverity::Error,
                materializeError.empty()
                    ? "Could not create the project for this first edit."
                    : materializeError,
                "raw-workspace-project-materialize");
            return false;
        }
        targetProjectPath = m_Project->storePath;
        materializedFromViewing = true;
    }
    m_Project->rawSourceKey = selectedSource->relativePathKey;
    m_Project->rawPipelineActive = true;
    m_RawWorkspaceStaleRenderStatusText.clear();
    m_Project->storePath = targetProjectPath;
    m_Project->rawRecipe = Stack::RawRecipe::BuildWorkspaceSourceRecipe(resolvedRecipe);
    m_Project->rawMode = targetMode;
    if (m_Project->rawMode == Stack::RawWorkspace::RawProjectMode::UnifiedLayers) {
        m_Project->managedRaw = {};
    }
    SetCurrentProjectName(selectedSource->stem.empty() ? selectedSource->fileName : selectedSource->stem);
    SetCurrentProjectFileName(m_Project->storePath.string());
    if (Stack::RawWorkspace::SourceRecord* mutableSource =
            FindRawWorkspaceSourceByKey(selectedSource->relativePathKey)) {
        mutableSource->project.absolutePath = m_Project->storePath;
        mutableSource->project.relativePath =
            m_Project->storePath.filename();
        mutableSource->project.status =
            Stack::RawWorkspace::ProjectStatus::Existing;
        mutableSource->project.linkedRaw = false;
        mutableSource->project.embeddedRaw = false;
        mutableSource->project.autosaved = materializedFromViewing;
        mutableSource->project.dirty = !materializedFromViewing;
    }
    if (materializedFromViewing) {
        ClearDirty();
        PersistRawWorkspaceCatalog();
        RefreshUnifiedProjectViewsAfterSave(true);
    }
    // The caller owns the one render/document invalidation for this edit.
    // Marking here as well advanced every compact RAW adjustment twice.
    return true;
}

bool EditorModule::UpdateRawWorkspaceInteractionDraft(
    const Stack::RawRecipe::RawDevelopmentRecipe& persistedRecipe,
    const Stack::RawRecipe::RawDevelopmentRecipe& draftRecipe) {
    if (m_Project->rawInteractionDraft.active &&
        m_Project->rawInteractionDraft.sourceKey !=
            m_RawWorkspace.selectedSourceKey) {
        if (!ResolveRawWorkspaceInteractionDraft(false)) {
            return false;
        }
    }
    if (!m_Project->rawInteractionDraft.active) {
        // Materialize a first-edit project using the untouched frame-start
        // recipe. The visible gesture is then an overlay and does not dirty or
        // revise the document until release.
        if (!EnsureRawWorkspaceProjectForSelectedRecipeEdit(
                persistedRecipe)) {
            return false;
        }
        m_Project->rawInteractionDraft.active = true;
        m_Project->rawInteractionDraft.sourceKey =
            m_Project->rawSourceKey;
        m_Project->rawInteractionDraft.source =
            m_Project->rawRecipe.source;
    }

    m_Project->rawInteractionDraft.recipe = draftRecipe;
    m_Project->rawInteractionDraft.recipe.source =
        m_Project->rawInteractionDraft.source;

    // The render command owns the transient recipe overlay. The authored
    // graph remains unchanged until release, so one pointer sample now causes
    // one render invalidation and no graph/document invalidation.
    MarkRenderRefreshDirty();
    NoteRawWorkspaceRecipePreviewEdit(true);
    return true;
}

bool EditorModule::ResolveRawWorkspaceInteractionDraft(bool cancel) {
    if (!m_Project->rawInteractionDraft.active) {
        return true;
    }
    RawInteractionDraft resolved = std::move(m_Project->rawInteractionDraft);
    m_Project->rawInteractionDraft = {};
    if (!cancel) {
        return ApplyRawWorkspaceRecipeEditForSelectedSource(
            resolved.recipe,
            false);
    }

    // Cancel only discards the transient command overlay. No graph state was
    // mutated during the gesture, so there is nothing to restore.
    MarkRenderRefreshDirty();
    NoteRawWorkspaceRecipePreviewEdit(false);
    return true;
}

bool EditorModule::ApplyRawWorkspaceRecipeEditForSelectedSource(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    bool interactionActive) {
    if (m_RawWorkspaceAutoBaseUi.preciseAppliedDisplayFitAwaitingRender &&
        m_RawWorkspaceAutoBaseUi.preciseStartingPoint.state !=
            Stack::PreciseIntegration::LifecycleState::Applying) {
        m_RawWorkspaceAutoBaseUi.preciseAppliedDisplayFitAwaitingRender = false;
        m_RawWorkspaceAutoBaseUi.preciseAppliedRecipeIdentity.clear();
        m_RawWorkspaceAutoBaseUi.startingPointDisplayFitPending = false;
    }
    if (m_RawWorkspaceAutoBaseUi.preciseStartingPoint.active &&
        Stack::PreciseIntegration::IsRunning(
            m_RawWorkspaceAutoBaseUi.preciseStartingPoint.state) &&
        m_RawWorkspaceAutoBaseUi.preciseStartingPoint.state !=
            Stack::PreciseIntegration::LifecycleState::Applying) {
        CancelRawWorkspacePreciseStartingPoint(
            "The visible RAW recipe changed before precise apply.");
    }
    Stack::Project::RawLayerStackState operationCandidate;
    bool operationsChanged = false;
    if (!PrepareRawControlRecipeEdit(recipe, operationCandidate, operationsChanged, m_RawLayerStatus)) return false;
    if (!EnsureRawWorkspaceProjectForSelectedRecipeEdit(ReadRawControlRecipe())) return false;
    const int boundSource = ResolveRawWorkspaceStageOutputNodeId();
    auto* sourceNode = m_Project->graph.FindNode(boundSource);
    if (!sourceNode || sourceNode->kind != EditorNodeGraph::NodeKind::RawDevelopment) {
        m_RawLayerStatus = "The project's source-development binding is unavailable.";
        return false;
    }
    if (!m_Project->rawLayers.ApplySourceEdit(std::move(operationCandidate), m_Project->rawRecipe,
            recipe, m_RawLayerStatus, &m_Project->graph, boundSource)) return false;
    sourceNode->rawDevelopment.recipe = m_Project->rawRecipe;
    sourceNode->rawDevelopment.projectStatus = "Edited";
    sourceNode->rawDevelopment.edited = true;
    sourceNode->rawDevelopment.autosaved = false;

    if (Stack::RawWorkspace::SourceRecord* source =
            FindRawWorkspaceSourceByKey(m_Project->rawSourceKey)) {
        bool presentationChanged = false;
        if (source->project.absolutePath.empty()) {
            const Stack::RawWorkspace::ManagedLayout layout =
                Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot);
            const Stack::RawWorkspace::ProjectInfo expectedProject =
                Stack::RawWorkspace::BuildExpectedProjectInfo(layout, *source);
            source->project.absolutePath = expectedProject.absolutePath;
            source->project.relativePath = expectedProject.relativePath;
            presentationChanged = true;
        }
        if (source->project.status == Stack::RawWorkspace::ProjectStatus::Unknown ||
            source->project.status == Stack::RawWorkspace::ProjectStatus::NoProject) {
            source->project.status = Stack::RawWorkspace::ProjectStatus::Existing;
            presentationChanged = true;
        }
        presentationChanged = presentationChanged ||
            source->project.mode != m_Project->rawMode ||
            source->project.autosaved ||
            !source->project.dirty;
        source->project.mode = m_Project->rawMode;
        // Apply... is always an authored edit, including the first edit whose
        // managed asset was just materialized by Ensure.... Keep the browser
        // card in sync, but invalidate its presentation only on the clean to
        // dirty transition instead of once per slider frame.
        source->project.autosaved = false;
        source->project.dirty = true;
        if (presentationChanged) {
            InvalidateRawWorkspaceGalleryPresentation();
        }
    }

    MarkRenderDirty(boundSource);
    NoteRawWorkspaceRecipePreviewEdit(interactionActive);
    return true;
}

void EditorModule::RefreshUnifiedProjectViewsAfterSave(
    bool projectMembershipChanged,
    bool persistCatalog) {
    RefreshBracketingProjectCard();
    if (!m_RawWorkspaceAppStateLoaded) {
        if (projectMembershipChanged) {
            LibraryManager::Get().RequestRefreshLibraryAsync();
        }
        return;
    }

    // An ordinary save changes revisions, not membership. Update the active
    // browser record in place; rescanning the whole RAW folder after every
    // autosave wastes I/O and can contend with rendering and continued edits.
    if (Stack::RawWorkspace::SourceRecord* source =
            FindRawWorkspaceSourceByKey(m_Project->rawSourceKey)) {
        source->project.status = Stack::RawWorkspace::ProjectStatus::Existing;
        source->project.absolutePath = m_Project->storePath;
        source->project.relativePath =
            m_Project->storePath.filename();
        source->project.autosaved = true;
        source->project.dirty = false;
        InvalidateRawWorkspaceGalleryPresentation();
        if (persistCatalog) {
            PersistRawWorkspaceCatalog();
        }
    }

    if (projectMembershipChanged) {
        LibraryManager::Get().RequestRefreshLibraryAsync();
        // First publication, adoption, and version creation genuinely change
        // the shared project population and still require rediscovery.
        PreserveActiveRawProjectSourceForLibraryNavigation();
        RescanRawWorkspace();
    }
}

void EditorModule::RequestCreateProjectVersion(
    const std::filesystem::path& sourceProject) {
    if (sourceProject.empty()) return;
    std::filesystem::path destinationRoot = AppPaths::GetProjectsDirectory();
    if (!m_RawWorkspace.workspaceRoot.empty()) {
        const std::filesystem::path workspaceProjects =
            Stack::RawWorkspace::BuildManagedLayout(
                m_RawWorkspace.workspaceRoot).projectsDirectory;
        std::error_code relativeError;
        const std::filesystem::path relative = std::filesystem::absolute(
            sourceProject, relativeError).lexically_normal().lexically_relative(
                workspaceProjects.lexically_normal());
        if (!relativeError && !relative.empty() && !relative.is_absolute() &&
            (relative.begin() == relative.end() ||
             *relative.begin() != std::filesystem::path(".."))) {
            destinationRoot = workspaceProjects;
        }
    }
    std::error_code writableError;
    std::filesystem::create_directories(destinationRoot, writableError);
    if (writableError) {
        PostNotification(
            UiNotificationSeverity::Error,
            "The project version folder is not writable: " +
                destinationRoot.string() + ": " + writableError.message(),
            "raw-gallery-version-project-root");
        return;
    }
    PostNotification(
        UiNotificationSeverity::Info,
        "Creating an independent project version...",
        "raw-gallery-version-create");
    const bool submitted = ProjectTasks().SubmitHighPriority("Saving version",[
        this,
        sourceProject,
        destinationRoot
    ]() {
        std::filesystem::path createdPath;
        std::string error;
        const bool success = Stack::Project::ProjectIndex::Get().CloneVersion(
            sourceProject,
            destinationRoot,
            createdPath,
            &error);
        ProjectTasks().PostToMain([
            this,
            success,
            createdPath,
            error = std::move(error)
        ]() mutable {
            if (success) {
                RefreshUnifiedProjectViewsAfterSave(true);
                PostNotification(
                    UiNotificationSeverity::Success,
                    "Created " + createdPath.filename().string() + ".",
                    "raw-gallery-version-create");
            } else {
                PostNotification(
                    UiNotificationSeverity::Error,
                    error.empty()
                        ? "The independent project version could not be created."
                        : error,
                    "raw-gallery-version-create");
            }
        });
    });
    if (!submitted) {
        PostNotification(
            UiNotificationSeverity::Error,
            "The project version operation could not be queued.",
            "raw-gallery-version-create");
    }
}

bool EditorModule::EnqueueCurrentProjectSave(
    Stack::Project::ProjectSaveReason reason,
    const std::string& fallbackName,
    std::function<void(bool)> onComplete) {
    if (!m_DocumentPersistenceEnabled) {
        if (onComplete) onComplete(false);
        return false;
    }
    if (GetProjectSessionKind() == ProjectSessionKind::Empty) {
        if (onComplete) onComplete(false);
        return false;
    }
    if (HasPendingGraphImageImports()) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "Finishing imported slices before saving the project.",
            "editor-graph-image-save-wait");
        if (onComplete) onComplete(false);
        return false;
    }

    const auto firstNameCharacter = fallbackName.find_first_not_of(" \t\r\n");
    const std::string requestedName = firstNameCharacter == std::string::npos
        ? std::string{}
        : fallbackName.substr(firstNameCharacter,
            fallbackName.find_last_not_of(" \t\r\n") - firstNameCharacter + 1);
    if (!requestedName.empty() && requestedName != m_Project->name) {
        m_Project->name = requestedName;
        MarkDirty();
    }
    const std::string projectId = EnsureProjectDocumentId();
    const std::uint64_t requestedRevision = m_Project->editRevision;
    const std::string projectName = !requestedName.empty()
        ? requestedName
        : (m_Project->name.empty() ? "Untitled Project" : m_Project->name);

    Stack::Project::ProjectSaveCoordinator::Request request;
    request.projectId = projectId;
    request.editRevision = requestedRevision;
    request.reason = reason;
    auto write = [this, projectId, projectName, reason](
        Stack::Project::ProjectSaveCoordinator::Completion completion,
        bool requireNewStore) mutable {
        if (projectId != m_Project->documentId) {
            Stack::Project::ProjectSaveResult result;
            result.status = Stack::Project::ProjectSaveStatus::Canceled;
            result.projectId = projectId;
            result.message = "The active project changed before the save started.";
            completion(std::move(result));
            return;
        }

        const std::uint64_t capturedRevision = m_Project->editRevision;
        if (IsUnifiedProjectStoreActive() && !requireNewStore) {
            StartManagedProjectSaveAsync(
                capturedRevision,
                reason,
                std::move(completion));
            return;
        }
        if (requireNewStore && m_Project->snapshot &&
            (std::any_of(m_Project->snapshot->embeddedAssets.begin(),m_Project->snapshot->embeddedAssets.end(),
                [&](const auto& asset) { return asset.assetId != m_Project->snapshot->sourceAssetId; }) ||
             m_Project->snapshot->sourceWidth != 1 || m_Project->snapshot->sourceHeight != 1 ||
             std::any_of(m_Project->graph.GetNodes().begin(),m_Project->graph.GetNodes().end(),
                [](const auto& node) { return node.kind == EditorNodeGraph::NodeKind::Image; }))) {
            Stack::Project::ProjectSaveResult result;
            result.status = Stack::Project::ProjectSaveStatus::Failed;
            result.projectId = projectId;
            result.message = "The project store is missing. Its embedded sources must be recovered before saving.";
            completion(std::move(result));
            return;
        }

        const bool adopting = !m_Project->adoptionSourcePath.empty();
        if (IsRawWorkspaceProjectActive()) {
            Stack::Project::ProjectSaveResult result;
            result.status = Stack::Project::ProjectSaveStatus::Failed;
            result.projectId = projectId;
            result.message = "The current RAW project store is unavailable.";
            completion(std::move(result));
            return;
        }

        std::filesystem::path destination;
        if (adopting) {
            const std::filesystem::path destinationRoot =
                IsRawWorkspaceProjectActive() &&
                        !m_RawWorkspace.workspaceRoot.empty()
                    ? Stack::RawWorkspace::BuildManagedLayout(
                          m_RawWorkspace.workspaceRoot).projectsDirectory
                    : AppPaths::GetProjectsDirectory();
            destination = Stack::Project::ProjectIndex::BuildUniqueProjectPath(
                destinationRoot,
                projectName,
                projectId);
        } else if (!m_Project->fileName.empty()) {
            const std::filesystem::path current(m_Project->fileName);
            if (current.is_absolute()) destination = current.lexically_normal();
        } else if (!m_RawWorkspace.workspaceRoot.empty()) {
            // An unsaved Graph document belongs to the loaded workspace just
            // like a RAW or bracket document. Keep its first save discoverable
            // in that folder's Projects section.
            destination = Stack::Project::ProjectIndex::BuildUniqueProjectPath(
                Stack::RawWorkspace::BuildManagedLayout(
                    m_RawWorkspace.workspaceRoot).projectsDirectory,
                projectName, projectId);
        }

        auto finish = [
            this,
            projectId,
            capturedRevision,
            adopting,
            completion = std::move(completion)
        ](bool success) mutable {
            Stack::Project::ProjectSaveResult result;
            result.status = success
                ? Stack::Project::ProjectSaveStatus::Saved
                : Stack::Project::ProjectSaveStatus::Failed;
            result.projectId = projectId;
            result.persistedEditRevision = success ? capturedRevision : 0;
            result.path = m_Project->fileName;
            result.message = success ? std::string() : "Failed to save the project.";
            if (success) {
                if (adopting) m_Project->adoptionSourcePath.clear();
                RefreshUnifiedProjectViewsAfterSave(adopting);
            }
            completion(std::move(result));
        };

        if (!destination.empty()) {
            LibraryManager::Get().RequestSaveProjectToPath(
                projectName,
                this,
                destination,
                std::move(finish),
                requireNewStore);
        } else {
            LibraryManager::Get().RequestSaveProject(
                projectName,
                this,
                m_Project->fileName,
                std::move(finish),
                requireNewStore);
        }
    };
    request.start = [this, reason, write = std::move(write)](
        Stack::Project::ProjectSaveCoordinator::Completion completion) mutable {
        if (reason == Stack::Project::ProjectSaveReason::Explicit &&
            !IsDirty() && m_Project->adoptionSourcePath.empty() &&
            !m_Project->rawInteractionDraft.active &&
            (IsUnifiedProjectStoreActive() || m_Project->savedFileStamp.has_value())) {
            CheckCurrentProjectSaveAsync(std::move(completion), write);
        } else {
            write(std::move(completion), false);
        }
    };
    const auto saveNotifier = GetNotifier();
    const auto saveOperation = reason == Stack::Project::ProjectSaveReason::Explicit
        ? saveNotifier.NewOperation() : 0;
    const std::weak_ptr<Stack::Project::ProjectSession> saveOwner = m_Project;
    request.completion = [saveNotifier, saveOperation, saveOwner, projectName,
        onComplete = std::move(onComplete)](Stack::Project::ProjectSaveResult result) mutable {
        if (saveOperation != 0) {
            Stack::Notifications::NoticeSpec notice;
            notice.title = "Save";
            notice.context = projectName;
            notice.operationId = saveOperation;
            const auto execution = Async::TaskSystem::CurrentActivity();
            if (execution.ownerId == saveNotifier.GetOwner().id &&
                execution.ownerGeneration == saveNotifier.GetOwner().generation && execution.operationId != 0)
                notice.operationId = execution.operationId;
            notice.dedupeKey = "explicit-project-save";
            notice.details = result.path;
            if (result.status == Stack::Project::ProjectSaveStatus::Saved) {
                const auto owner = saveOwner.lock();
                const bool newerEdits = owner && owner->documentId == result.projectId &&
                    owner->editRevision > result.persistedEditRevision;
                notice.severity = UiNotificationSeverity::Success;
                notice.outcome = Stack::Notifications::Outcome::Success;
                notice.message = newerEdits
                    ? "Project snapshot saved. Newer edits are still pending."
                    : "Project saved.";
            } else if (result.status == Stack::Project::ProjectSaveStatus::Canceled) {
                notice.outcome = Stack::Notifications::Outcome::Cancelled;
                notice.message = result.message.empty() ? "Save cancelled." : result.message;
                notice.preview = false;
            } else {
                notice.severity = UiNotificationSeverity::Error;
                notice.outcome = Stack::Notifications::Outcome::Failure;
                notice.message = result.message.empty() ? "Could not save the project." : result.message;
            }
            saveNotifier.Post(std::move(notice));
        }
        if (onComplete) onComplete(static_cast<bool>(result));
    };
    return m_Project->saves.Enqueue(std::move(request));
}

bool EditorModule::RequestSaveCurrentProject(
    const std::string& fallbackName,
    std::function<void(bool)> onComplete) {
    if (m_Project->rawInteractionDraft.active && !ResolveRawWorkspaceInteractionDraft(false)) {
        if (onComplete) onComplete(false);
        return false;
    }
    return EnqueueCurrentProjectSave(
        Stack::Project::ProjectSaveReason::Explicit,
        fallbackName,
        std::move(onComplete));
}

bool EditorModule::RequestSaveProjectAs(
    const std::filesystem::path& requestedDestination,
    std::function<void(bool)> onComplete) {
    if (requestedDestination.empty()) {
        PostNotification(
            UiNotificationSeverity::Error,
            "Save As did not receive a destination.",
            "project-file-save-as-missing-destination");
        if (onComplete) {
            onComplete(false);
        }
        return false;
    }
    if (GetProjectSessionKind() == ProjectSessionKind::Empty) {
        PostNotification(
            UiNotificationSeverity::Error,
            "There is no open project to save.",
            "project-file-save-as-empty-session");
        if (onComplete) {
            onComplete(false);
        }
        return false;
    }
    const ProjectFileCommandContext saveAsContext =
        GetProjectFileCommandContext();
    const bool recoverySaveAs =
        saveAsContext.conflict || saveAsContext.readOnlyRecovery;
    if (saveAsContext.busy && !recoverySaveAs) {
        PostNotification(
            UiNotificationSeverity::Warning,
            saveAsContext.busyReason.empty()
                ? "Finish the current project operation before using Save As."
                : saveAsContext.busyReason,
            "project-file-save-as-busy");
        if (onComplete) {
            onComplete(false);
        }
        return false;
    }

    if (IsUnifiedProjectStoreActive()) {
        std::string error;
        const bool success = SaveActiveMultiFrameRawProjectAs(
            requestedDestination,
            Stack::Project::ProjectStorageKind::DirectoryBundle,
            &error);
        if (!success) {
            PostNotification(
                UiNotificationSeverity::Error,
                error.empty() ? "Failed to save the project copy." : error,
                "project-file-save-as");
        } else {
            PostNotification(
                UiNotificationSeverity::Success,
                "Project saved to the new location.",
                "project-file-save-as");
        }
        if (onComplete) {
            onComplete(success);
        }
        return success;
    }

    if (HasPendingGraphImageImports()) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "Finishing imported slices before saving the project.",
            "project-file-save-as-wait");
        if (onComplete) {
            onComplete(false);
        }
        return false;
    }

    std::filesystem::path destination = requestedDestination.lexically_normal();
    std::string destinationExtension = destination.extension().string();
    std::transform(
        destinationExtension.begin(),
        destinationExtension.end(),
        destinationExtension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    std::string destinationName = destination.filename().string();
    std::transform(
        destinationName.begin(),
        destinationName.end(),
        destinationName.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (destinationName == "project.stack") {
        destination = destination.parent_path();
    } else if (destinationExtension == ".stack" ||
               destinationExtension == ".stackbundle") {
        destination = destination.parent_path() / destination.stem();
    }
    std::error_code absoluteError;
    destination = std::filesystem::absolute(destination, absoluteError).lexically_normal();
    if (absoluteError) {
        PostNotification(
            UiNotificationSeverity::Error,
            "The selected project destination is invalid.",
            "project-file-save-as");
        if (onComplete) {
            onComplete(false);
        }
        return false;
    }

    std::filesystem::path currentPath = m_Project->storePath;
    if (currentPath.empty() && !m_Project->fileName.empty()) {
        currentPath = std::filesystem::path(m_Project->fileName);
        if (currentPath.is_relative()) {
            currentPath = LibraryManager::Get().GetLibraryPath() / currentPath;
        }
        currentPath = currentPath.lexically_normal();
    }
    if (!currentPath.empty() && currentPath == destination) {
        return RequestSaveCurrentProject({}, std::move(onComplete));
    }

    const std::string projectName = m_Project->name.empty()
        ? (destination.filename().string().empty()
            ? std::string("Untitled Project")
            : destination.filename().string())
        : m_Project->name;

    auto document = std::make_shared<StackBinaryFormat::ProjectDocument>();
    if (!BuildProjectDocumentForSave(projectName, *document)) {
        PostNotification(
            UiNotificationSeverity::Error,
            "Failed to capture the current project for Save As.",
            "project-file-save-as");
        if (onComplete) {
            onComplete(false);
        }
        return false;
    }

    document->projectId = Stack::Project::GenerateStableUuid();
    if (document->rawProjectSnapshot) {
        document->rawProjectSnapshot = std::make_shared<Stack::Project::RawProjectSnapshot>(*document->rawProjectSnapshot);
        document->rawProjectSnapshot->projectId = document->projectId;
    }
    const std::uint64_t capturedEditRevision = GetProjectEditRevision();
    const auto savedStore = std::make_shared<Stack::Project::ProjectStoreOpenResult>();
    ++m_ProjectFileSaveGeneration;
    const std::uint64_t generation = m_ProjectFileSaveGeneration;
    m_ProjectFileSaveTaskState = Async::TaskState::Running;
    m_ProjectFileSaveStatusText = "Writing the project to its new location...";

    bool submitted = false;
    try {
        submitted = ProjectTasks().Submit("Saving",[
            this,
            generation,
            destination,
            projectName,
            capturedEditRevision,
            document,
            savedStore,
            onComplete = std::move(onComplete)
        ]() mutable {
            bool success = false;
            try {
                std::error_code directoryError;
                if (destination.has_parent_path()) {
                    std::filesystem::create_directories(
                        destination.parent_path(),
                        directoryError);
                }
                success = !directoryError &&
                    StackBinaryFormat::WriteProjectFile(destination, *document);
                if (success) {
                    *savedStore = Stack::Project::OpenProjectStore(destination);
                    success = static_cast<bool>(*savedStore);
                }
            } catch (...) {
                success = false;
            }

            ProjectTasks().PostToMain([
                this,
                generation,
                destination,
                projectName,
                capturedEditRevision,
                document,
                savedStore,
                success,
                onComplete = std::move(onComplete)
            ]() mutable {
                if (generation != m_ProjectFileSaveGeneration) {
                    if (onComplete) {
                        onComplete(false);
                    }
                    return;
                }

                bool completionSuccess = false;
                try {
                if (!success) {
                    m_ProjectFileSaveTaskState = Async::TaskState::Failed;
                    m_ProjectFileSaveStatusText =
                        "Failed to save the project to the new location.";
                    MarkDirty();
                    PostNotification(
                        UiNotificationSeverity::Error,
                        m_ProjectFileSaveStatusText,
                        "project-file-save-as");
                } else {
                    if (GetProjectEditRevision() == capturedEditRevision)
                        SetCurrentProjectName(projectName);
                    SetCurrentProjectFileName(destination.string());
                    m_Project->documentId = document->projectId;
                    if (!AdoptSavedProjectStore(savedStore->store,
                            std::move(savedStore->snapshot), document->projectId, capturedEditRevision))
                        throw std::runtime_error("The saved project store could not be adopted.");
                    m_Project->adoptionSourcePath.clear();
                    m_Project->saves.Reset(m_Project->documentId, capturedEditRevision);
                    const bool clearedCurrentSnapshot =
                        ClearDirtyIfRevision(capturedEditRevision);
                    m_ProjectFileSaveTaskState = Async::TaskState::Idle;
                    m_ProjectFileSaveStatusText = clearedCurrentSnapshot
                        ? "Project saved to the new location."
                        : "Project copy saved; newer edits are still unsaved.";
                    PostNotification(
                        UiNotificationSeverity::Success,
                        m_ProjectFileSaveStatusText,
                        clearedCurrentSnapshot
                            ? "project-file-save-as"
                            : "project-file-save-as-newer-edits");
                    completionSuccess = clearedCurrentSnapshot;
                }
                } catch (...) {
                    m_ProjectFileSaveTaskState = Async::TaskState::Failed;
                    m_ProjectFileSaveStatusText =
                        "The project was written, but Save As finalization failed.";
                    MarkDirty();
                }
                if (onComplete) {
                    onComplete(completionSuccess);
                }
            });
        });
    } catch (...) {
        submitted = false;
    }

    if (submitted) {
        return true;
    }

    if (generation == m_ProjectFileSaveGeneration) {
        m_ProjectFileSaveTaskState = Async::TaskState::Failed;
        m_ProjectFileSaveStatusText = "The Save As task could not be queued.";
        MarkDirty();
        PostNotification(
            UiNotificationSeverity::Error,
            m_ProjectFileSaveStatusText,
            "project-file-save-as");
    }
    if (onComplete) {
        onComplete(false);
    }
    return false;
}

bool EditorModule::PackCurrentProject(
    const std::filesystem::path& requestedDestination,
    std::string* errorMessage) {
    const auto finish = [&](bool success, std::string message = {}) {
        if (errorMessage) {
            *errorMessage = std::move(message);
        }
        return success;
    };
    if (requestedDestination.empty() ||
        GetProjectSessionKind() == ProjectSessionKind::Empty) {
        return finish(false, "No project is available to pack.");
    }
    if (IsUnifiedProjectStoreActive()) {
        return SaveActiveMultiFrameRawProjectAs(
            requestedDestination,
            Stack::Project::ProjectStorageKind::PortableFile,
            errorMessage);
    }

    std::filesystem::path workingPath(m_Project->fileName);
    if (workingPath.empty()) {
        return finish(false, "Save the working project before packing it.");
    }
    if (workingPath.is_relative()) {
        workingPath = LibraryManager::Get().GetLibraryPath() / workingPath;
    }
    workingPath = workingPath.lexically_normal();

    if (IsDirty()) {
        StackBinaryFormat::ProjectDocument document;
        const std::string name = m_Project->name.empty()
            ? workingPath.filename().string()
            : m_Project->name;
        if (!BuildProjectDocumentForSave(name, document) ||
            !StackBinaryFormat::WriteProjectFile(workingPath, document)) {
            return finish(false, "The latest project state could not be saved before packing.");
        }
        ClearDirty();
    }

    Stack::Project::ProjectStoreOpenResult opened =
        Stack::Project::OpenProjectStore(workingPath);
    if (!opened) {
        return finish(
            false,
            opened.message.empty()
                ? "The current working project could not be opened for packing."
                : opened.message);
    }
    std::filesystem::path destination = requestedDestination.lexically_normal();
    std::string extension = destination.extension().string();
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
    if (extension != ".stack") {
        destination += ".stack";
    }
    Stack::Project::ProjectStoreOpenResult packed =
        Stack::Project::ConvertProjectStore(
            opened.store,
            opened.snapshot,
            destination,
            Stack::Project::ProjectStorageKind::PortableFile);
    return packed
        ? finish(true)
        : finish(false, packed.message.empty()
            ? "The project could not be packed."
            : packed.message);
}

void EditorModule::ClearPendingRawWorkspaceProjectReplacement() {
    m_PendingRawWorkspaceProjectReplacement = {};
    m_RawWorkspaceReplacementActionLabel.clear();
    m_RawWorkspaceReplacementTargetLabel.clear();
    m_RawWorkspaceReplacementDiscardOpenSourceKey.clear();
    m_RawWorkspaceReplacementAuthorized = false;
    m_RawWorkspaceReplacementSavePending = false;
}

void EditorModule::QueueRawWorkspaceProjectReplacement(
    std::string actionLabel,
    std::string targetLabel,
    std::function<bool(std::string*)> action,
    std::string discardOpenSourceKey) {
    if (IsWorkspaceTransitionPending() || !FinishWorkspaceInteraction()) return;
    m_RawWorkspaceReplacementActionLabel = std::move(actionLabel);
    m_RawWorkspaceReplacementTargetLabel = std::move(targetLabel);
    m_RawWorkspaceReplacementDiscardOpenSourceKey =
        std::move(discardOpenSourceKey);
    m_PendingRawWorkspaceProjectReplacement = std::move(action);
    m_RawWorkspaceReplacementSavePending = true;
    m_RawWorkspaceReplacementExecuteAfterSave = false;
    m_ShowRawWorkspaceReplaceProjectPopup = false;
    RequestSaveWorkspaceBeforeClose([this](bool success) {
        m_RawWorkspaceReplacementSavePending = false;
        if (success) m_RawWorkspaceReplacementExecuteAfterSave = true;
        else {
            ClearPendingRawWorkspaceProjectReplacement();
            PostNotification(UiNotificationSeverity::Error,
                "Could not save the current project. It will stay open.", "raw-workspace-transition-save-failed");
        }
    });
}

bool EditorModule::ExecutePendingRawWorkspaceProjectReplacement(
    bool discardCurrent) {
    if (!m_PendingRawWorkspaceProjectReplacement) {
        return false;
    }

    if (discardCurrent &&
        !m_RawWorkspaceReplacementDiscardOpenSourceKey.empty()) {
        // The selected source is staged on the next UI tick. Keep this token
        // until staging starts so the normal autosave guard does not turn an
        // explicit Discard choice into an implicit save.
        m_RawWorkspaceReplacementSkipSaveSourceKey =
            m_RawWorkspaceReplacementDiscardOpenSourceKey;
    }

    m_RawWorkspaceReplacementAuthorized = discardCurrent;
    std::string error;
    const auto action = m_PendingRawWorkspaceProjectReplacement;
    const bool success = action(&error);
    m_RawWorkspaceReplacementAuthorized = false;
    if (!success) {
        m_RawWorkspaceReplacementSkipSaveSourceKey.clear();
        PostNotification(
            UiNotificationSeverity::Error,
            error.empty() ? "The requested project could not be opened." : error,
            "raw-workspace-project-replacement");
        return false;
    }

    ClearPendingRawWorkspaceProjectReplacement();
    return true;
}

bool EditorModule::RequestOpenRawWorkspaceProject(
    const std::filesystem::path& projectPath) {
    return RequestOpenProjectFromPath(projectPath, false);
}

bool EditorModule::RequestOpenProjectFromPath(
    const std::filesystem::path& projectPath,
    bool currentDispositionApproved) {
    if (projectPath.empty()) {
        return false;
    }
    if (m_PermanentGalleryWorkspace)
        return RequestOpenRawWorkspaceProjectFromGallery(projectPath);
    (void)currentDispositionApproved;
    if (IsWorkspaceTransitionPending() || !FinishWorkspaceInteraction()) return false;
    if (IsDeferredLoadedProjectApplyActive() ||
        IsRawWorkspaceProjectLoadBusy()) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "Finish opening the current selection before opening another project.",
            "raw-workspace-project-replacement-busy");
        return false;
    }

    const std::filesystem::path normalized = projectPath.lexically_normal();
    auto openAction = [this, normalized](std::string* error) {
        std::error_code filesystemError;
        if (!std::filesystem::exists(normalized, filesystemError) ||
            filesystemError) {
            if (error) {
                *error = "The selected project no longer exists.";
            }
            return false;
        }
        LibraryManager::Get().RequestLoadProjectFromPath(normalized, this);
        return true;
    };

    if (NeedsWorkspaceSaveBeforeTransition() || IsProjectFileSaveBusy() || IsRawWorkspaceProjectSaveBusy()) {
        QueueRawWorkspaceProjectReplacement(
            "open project",
            normalized.filename().string(),
            std::move(openAction));
        return true;
    }
    std::string error;
    const bool opened = openAction(&error);
    if (!opened && !error.empty()) {
        PostNotification(
            UiNotificationSeverity::Error,
            error,
            "raw-workspace-project-open");
    }
    return opened;
}

void EditorModule::RenderRawWorkspaceLifecyclePopups() {
    if (m_RawWorkspaceReplacementExecuteAfterSave) {
        m_RawWorkspaceReplacementExecuteAfterSave = false;
        if (!ExecutePendingRawWorkspaceProjectReplacement(false))
            ClearPendingRawWorkspaceProjectReplacement();
    }
    m_ShowRawWorkspaceReplaceProjectPopup = false;
    if (m_ShowRawWorkspaceCloseProjectPopup) {
        m_ShowRawWorkspaceCloseProjectPopup = false;
        if (!m_RawWorkspaceReplacementSavePending && FinishWorkspaceInteraction()) {
            m_RawWorkspaceReplacementSavePending = true;
            RequestSaveWorkspaceBeforeClose([this](bool success) {
                m_RawWorkspaceReplacementSavePending = false;
                if (success) m_RawWorkspaceCloseAfterSave = true;
                else PostNotification(UiNotificationSeverity::Error,
                    "Could not save the current project. It will stay open.", "raw-workspace-close-save-failed");
            });
        }
    }
    if (m_RawWorkspaceCloseAfterSave) {
        m_RawWorkspaceCloseAfterSave = false;
        CloseActiveRawWorkspaceProject(false);
    }
}
