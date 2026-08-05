#include "Editor/EditorModule.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

std::vector<unsigned char> MinimalTransparentPngBytes() {
    static constexpr std::array<unsigned char, 70> kPng = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
        0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x01, 0x63, 0x60, 0x60, 0x60, 0x00,
        0x00, 0x00, 0x04, 0x00, 0x01, 0x0b, 0x0e, 0x0c,
        0x1a, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
        0x44, 0xae, 0x42, 0x60, 0x82
    };
    return std::vector<unsigned char>(kPng.begin(), kPng.end());
}

std::int64_t RawWorkspaceProjectFileTimeTicks(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(path, ec);
    return ec
        ? 0
        : std::chrono::duration_cast<std::chrono::microseconds>(
            writeTime.time_since_epoch()).count();
}

std::string RawWorkspaceProjectSaveRevisionKey(
    const std::filesystem::path& workspaceRoot,
    const std::string& sourceKey) {
    return workspaceRoot.lexically_normal().generic_string() + "\n" + sourceKey;
}

void EnsureMinimalProjectDocument(StackBinaryFormat::ProjectDocument& document, const std::string& name) {
    if (document.metadata.projectKind.empty()) {
        document.metadata.projectKind = StackBinaryFormat::kRawProjectKind;
    }
    if (document.metadata.projectName.empty()) {
        document.metadata.projectName = name.empty() ? "Untitled RAW Project" : name;
    }
    if (document.metadata.sourceWidth <= 0) {
        document.metadata.sourceWidth = 1;
    }
    if (document.metadata.sourceHeight <= 0) {
        document.metadata.sourceHeight = 1;
    }
    if (document.thumbnailBytes.empty()) {
        document.thumbnailBytes = MinimalTransparentPngBytes();
    }
    if (document.sourceImageBytes.empty()) {
        document.sourceImageBytes = MinimalTransparentPngBytes();
    }
}

void OverlayOwnedJsonFields(
    nlohmann::json& current,
    const nlohmann::json& owned) {
    if (!current.is_object() || !owned.is_object()) {
        current = owned;
        return;
    }

    for (auto it = owned.begin(); it != owned.end(); ++it) {
        auto currentIt = current.find(it.key());
        if (currentIt != current.end() &&
            currentIt->is_object() &&
            it->is_object()) {
            OverlayOwnedJsonFields(*currentIt, *it);
        } else {
            current[it.key()] = *it;
        }
    }
}

struct RawWorkspaceProjectLoadResult {
    Stack::RawWorkspace::SourceRecord source;
    EditorLoadedProjectData loadedProject;
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    Stack::RawWorkspace::RawProjectMode mode = Stack::RawWorkspace::RawProjectMode::RecipeBacked;
    Stack::RawWorkspace::ManagedRawSection managedSection;
    bool success = false;
    bool hasRawWorkspaceInfo = false;
    std::string errorMessage;
};

void ApplyRawWorkspaceProjectInfoToSource(
    Stack::RawWorkspace::SourceRecord& source,
    const StackBinaryFormat::ProjectDocument& document,
    const std::filesystem::path& projectPath,
    const std::filesystem::path& projectRelativePath,
    bool autosaved,
    bool dirty,
    std::string associationReason) {
    Stack::RawWorkspace::ProjectInfo info;
    if (!Stack::RawWorkspace::ReadProjectInfoFromDocument(document, info, nullptr)) {
        info = source.project;
        info.status = Stack::RawWorkspace::ProjectStatus::Invalid;
        if (info.errorMessage.empty()) {
            info.errorMessage = "Project does not contain RAW Workspace metadata.";
        }
    }

    info.absolutePath = projectPath.lexically_normal();
    info.relativePath = projectRelativePath.lexically_normal();
    info.projectModifiedTimeTicks = RawWorkspaceProjectFileTimeTicks(info.absolutePath);
    if (info.status != Stack::RawWorkspace::ProjectStatus::Invalid) {
        info.status = info.embeddedRaw
            ? Stack::RawWorkspace::ProjectStatus::Embedded
            : Stack::RawWorkspace::ProjectStatus::Existing;
    }
    info.autosaved = autosaved;
    info.dirty = dirty;
    info.associationReason = std::move(associationReason);
    source.project = std::move(info);
}

} // namespace

bool EditorModule::HasProjectContent() const {
    return !m_CurrentProjectName.empty() ||
        !m_CurrentProjectFileName.empty() ||
        !m_Layers.empty() ||
        !m_NodeGraph.GetNodes().empty() ||
        !m_NodeGraph.GetLinks().empty() ||
        m_Pipeline.HasSourceImage();
}

EditorModule::ProjectSessionKind EditorModule::GetProjectSessionKind() const {
    if (IsRawWorkspaceProjectActive()) {
        const Stack::RawWorkspace::SourceRecord* source =
            FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey);
        if (source != nullptr &&
            source->project.status == Stack::RawWorkspace::ProjectStatus::NoProject &&
            !m_Dirty) {
            return ProjectSessionKind::RawPreview;
        }
        return ProjectSessionKind::RawProject;
    }
    return HasProjectContent()
        ? ProjectSessionKind::EditorProject
        : ProjectSessionKind::Empty;
}

EditorModule::ProjectFileCommandContext
EditorModule::GetProjectFileCommandContext() const {
    ProjectFileCommandContext context;
    context.sessionKind = GetProjectSessionKind();
    context.dirty = m_Dirty;
    context.projectPath = m_ActiveRawWorkspaceProjectPath.empty()
        ? std::filesystem::path(m_CurrentProjectFileName)
        : m_ActiveRawWorkspaceProjectPath;

    if (IsMultiFrameRawProjectActive()) {
        context.lifecyclePhase = m_ProjectSessionController.Phase();
        context.storageKind = m_ActiveRawProjectStore->StorageKind();
    } else {
        context.lifecyclePhase = context.sessionKind == ProjectSessionKind::Empty
            ? Stack::Project::ProjectLifecyclePhase::Empty
            : (m_Dirty
                ? Stack::Project::ProjectLifecyclePhase::ReadyDirty
                : Stack::Project::ProjectLifecyclePhase::ReadyClean);
    }

    context.conflict = context.lifecyclePhase ==
        Stack::Project::ProjectLifecyclePhase::Conflict;
    context.readOnlyRecovery = context.lifecyclePhase ==
        Stack::Project::ProjectLifecyclePhase::ReadOnlyRecovery;

    const LibraryManager& library = LibraryManager::Get();
    const bool projectLoadBusy =
        IsDeferredLoadedProjectApplyActive() ||
        IsRawWorkspaceProjectLoadBusy() ||
        Async::IsBusy(library.GetProjectLoadTaskState());
    const bool projectSaveBusy =
        IsProjectFileSaveBusy() ||
        IsRawWorkspaceProjectSaveBusy() ||
        Async::IsBusy(library.GetSaveTaskState());
    const bool importBusy =
        IsSourceLoadBusy() ||
        IsGraphDropImportBusy() ||
        Async::IsBusy(library.GetImportTaskState());
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
    }

    const bool hasSession = context.sessionKind != ProjectSessionKind::Empty;
    context.canSave = hasSession && !context.busy &&
        !context.conflict && !context.readOnlyRecovery;
    context.canSaveAs = hasSession && !context.busy;
    context.canClose = hasSession && !context.busy;
    context.canOpen = !context.busy;
    context.canCreateEditorProject = !context.busy;
    return context;
}

bool EditorModule::CloseCurrentProject(bool discardUnsavedChanges) {
    const ProjectSessionKind kind = GetProjectSessionKind();
    if (kind == ProjectSessionKind::Empty) {
        return false;
    }
    if (kind == ProjectSessionKind::RawPreview ||
        kind == ProjectSessionKind::RawProject) {
        return CloseActiveRawWorkspaceProject(discardUnsavedChanges);
    }
    if (m_Dirty && !discardUnsavedChanges) {
        return false;
    }
    ResetToBlankProject();
    return true;
}

bool EditorModule::CloseEditorProjectAndActivateRawWorkspace() {
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
    if (!IsRawWorkspaceProjectActive()) {
        return false;
    }
    if (m_Dirty && !discardUnsavedChanges) {
        return false;
    }
    if (IsRawWorkspaceProjectLoadBusy() || IsRawWorkspaceProjectSaveBusy()) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Wait for the current RAW project operation to finish before closing it.",
            "raw-workspace-project-close-busy");
        return false;
    }

    const std::string selectedSourceKey = m_PinnedRawWorkspaceSource.has_value()
        ? m_RawWorkspaceSelectedSourceBeforePinnedProject
        : m_RawWorkspace.selectedSourceKey;
    const std::vector<std::string> selectedSourceKeys =
        m_RawWorkspace.selectedSourceKeys;
    const std::string activeSourceKey = m_ActiveRawWorkspaceSourceKey;
    if (!activeSourceKey.empty()) {
        // Any delayed save completion for this session is stale after close.
        BumpRawWorkspaceProjectSaveRevision(
            m_RawWorkspace.workspaceRoot,
            activeSourceKey);
    }

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
    QueueUiNotification(
        UiNotificationSeverity::Success,
        "Project closed. The RAW gallery remains available.",
        "raw-workspace-project-closed");
    return true;
}

bool EditorModule::ConsumeUiNotification(UiNotificationEvent& outEvent) {
    if (m_UiNotifications.empty()) {
        return false;
    }
    outEvent = std::move(m_UiNotifications.front());
    m_UiNotifications.pop_front();
    return true;
}

void EditorModule::ShowUiNotification(UiNotificationSeverity severity, std::string message, std::string dedupeKey) {
    QueueUiNotification(severity, std::move(message), std::move(dedupeKey));
}

void EditorModule::QueueUiNotification(UiNotificationSeverity severity, std::string message, std::string dedupeKey) {
    if (message.empty()) {
        return;
    }
    if (!dedupeKey.empty()) {
        for (UiNotificationEvent& event : m_UiNotifications) {
            if (event.dedupeKey == dedupeKey) {
                event.severity = severity;
                event.message = std::move(message);
                return;
            }
        }
    }
    m_UiNotifications.push_back(UiNotificationEvent{
        severity,
        std::move(message),
        std::move(dedupeKey)
    });
}

void EditorModule::ResetToBlankProject() {
    ++m_ProjectFileSaveGeneration;
    m_ProjectFileSaveTaskState = Async::TaskState::Idle;
    m_ProjectFileSaveStatusText.clear();
    CancelMfdExperimentalProcessing({}, true);
    CancelCanvasTool();
    CancelGraphAutoFocusTracking();
    m_GraphDropImportTaskState = Async::TaskState::Idle;
    m_GraphDropImportStatusText.clear();
    m_PendingGraphDropImports.clear();
    m_SourceLoadTaskState = Async::TaskState::Idle;
    m_SourceLoadStatusText.clear();
    m_Layers.clear();
    m_SelectedLayerIndex = -1;
    m_NodeGraph.ResetFromLayers(0, false);
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
    m_ActiveRawWorkspaceSourceKey.clear();
    m_PinnedRawWorkspaceSource.reset();
    m_RawWorkspaceSelectedSourceBeforePinnedProject.clear();
    m_RawWorkspacePipelineActive = false;
    m_RawWorkspaceStaleRenderStatusText.clear();
    m_ActiveRawWorkspaceProjectPath.clear();
    m_ActiveRawProjectStore.reset();
    m_ActiveRawProjectSnapshot.reset();
    m_ProjectSessionController.Clear();
    m_ActiveRawWorkspaceRecipe = {};
    m_ActiveRawWorkspaceMode = Stack::RawWorkspace::RawProjectMode::RecipeBacked;
    m_ActiveManagedRawSection = {};
    m_ShowRawWorkspaceRelinkPopup = false;
    m_ShowRawWorkspaceEmbedPopup = false;
    SetCurrentProjectName("");
    SetCurrentProjectFileName("");
    MarkRenderDirty();
    ClearDirty();
    m_LastUserActionTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    m_LastAutoSaveTime = -1.0;
}

void EditorModule::ResetRenderSubmissionState() {
    ++m_RenderGeneration;
    if (m_RenderWorkerAvailable) {
        m_RenderWorker.InvalidateSnapshotsBefore(m_RenderGeneration);
    }
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

bool EditorModule::ApplyLoadedRawProjectSessionMetadata(
    const LoadedProjectData& projectData,
    std::string* outError) {
    const bool isRawProject =
        projectData.projectKind == StackBinaryFormat::kRawProjectKind ||
        (projectData.rawWorkspaceData.is_object() &&
         projectData.rawWorkspaceData.value("schema", std::string()) ==
             "stack.rawWorkspace.project");
    if (!isRawProject) {
        if (m_PinnedRawWorkspaceSource.has_value() &&
            m_RawWorkspace.selectedSourceKey ==
                m_PinnedRawWorkspaceSource->relativePathKey) {
            m_RawWorkspace.selectedSourceKey =
                m_RawWorkspaceSelectedSourceBeforePinnedProject;
        }
        m_ActiveRawWorkspaceSourceKey.clear();
        m_PinnedRawWorkspaceSource.reset();
        m_RawWorkspaceSelectedSourceBeforePinnedProject.clear();
        m_RawWorkspacePipelineActive = false;
        m_RawWorkspaceStaleRenderStatusText.clear();
        m_ActiveRawWorkspaceProjectPath.clear();
        m_ActiveRawProjectStore.reset();
        m_ActiveRawProjectSnapshot.reset();
        m_ProjectSessionController.Clear();
        m_ActiveRawWorkspaceRecipe = {};
        m_ActiveRawWorkspaceMode = Stack::RawWorkspace::RawProjectMode::RecipeBacked;
        m_ActiveManagedRawSection = {};
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
        m_ActiveRawWorkspaceSourceKey.clear();
        m_PinnedRawWorkspaceSource.reset();
        m_RawWorkspaceSelectedSourceBeforePinnedProject.clear();
        m_ActiveRawWorkspaceProjectPath =
            std::filesystem::path(projectData.projectFileName).lexically_normal();
        m_ActiveRawWorkspaceRecipe = {};
        m_ActiveRawWorkspaceMode = Stack::RawWorkspace::RawProjectMode::RecipeBacked;
        m_ActiveManagedRawSection = {};
        m_ActiveRawProjectStore = projectData.projectStore;
        m_ActiveRawProjectSnapshot = projectData.rawProjectSnapshot;
        m_RawWorkspacePipelineActive = true;
        m_RawWorkspaceLockedByEditorProject = false;
        m_RawWorkspaceStaleRenderStatusText =
            "Process the burst to create its developed RAW result.";
        const Stack::Project::ProjectReplacementToken replacement =
            m_ProjectSessionController.BeginReplacement();
        if (!m_ProjectSessionController.CompleteReplacement(
                replacement,
                m_ActiveRawProjectSnapshot->projectId,
                m_ActiveRawProjectSnapshot->dirtyRevision,
                m_ActiveRawProjectSnapshot->persistedStorageRevision,
                m_ActiveRawProjectStore->IsReadOnlyRecovery())) {
            if (outError) *outError = "The multi-frame project session could not be activated.";
            return false;
        }
        return true;
    }

    m_ActiveRawProjectStore.reset();
    m_ActiveRawProjectSnapshot.reset();
    m_ProjectSessionController.Clear();

    StackBinaryFormat::ProjectDocument document;
    document.rawWorkspaceData = projectData.rawWorkspaceData;
    Stack::RawWorkspace::ProjectInfo projectInfo;
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    if (!Stack::RawWorkspace::ReadProjectInfoFromDocument(
            document,
            projectInfo,
            &recipe)) {
        if (outError) {
            *outError = projectInfo.errorMessage.empty()
                ? "The project does not contain valid RAW Workspace metadata."
                : projectInfo.errorMessage;
        }
        return false;
    }
    if (projectInfo.mode == Stack::RawWorkspace::RawProjectMode::CustomGraph ||
        projectInfo.mode == Stack::RawWorkspace::RawProjectMode::Unknown) {
        if (outError) {
            *outError = projectInfo.mode == Stack::RawWorkspace::RawProjectMode::CustomGraph
                ? "This legacy custom RAW graph cannot be converted safely and was not opened."
                : "This project uses an unsupported RAW Workspace mode and was not opened.";
        }
        return false;
    }

    Stack::RawWorkspace::SourceRecord pinnedSource;
    pinnedSource.absolutePath = recipe.source.sourcePath;
    pinnedSource.relativePathKey = !projectInfo.sourceRelativePathKey.empty()
        ? projectInfo.sourceRelativePathKey
        : recipe.source.relativePathKey;
    if (pinnedSource.relativePathKey.empty()) {
        pinnedSource.relativePathKey = !recipe.source.fingerprint.empty()
            ? recipe.source.fingerprint
            : std::string("external-project:") + projectData.projectFileName;
    }
    pinnedSource.relativePath = pinnedSource.relativePathKey;
    pinnedSource.fileName = !recipe.source.displayName.empty()
        ? recipe.source.displayName
        : pinnedSource.absolutePath.filename().string();
    if (pinnedSource.fileName.empty()) {
        pinnedSource.fileName = projectData.projectName.empty()
            ? std::filesystem::path(projectData.projectFileName).stem().string()
            : projectData.projectName;
    }
    pinnedSource.stem = std::filesystem::path(pinnedSource.fileName).stem().string();
    pinnedSource.extension = std::filesystem::path(pinnedSource.fileName).extension().string();
    pinnedSource.fileSizeBytes = projectInfo.sourceFileSizeBytes;
    pinnedSource.modifiedTimeTicks = projectInfo.sourceModifiedTimeTicks;
    pinnedSource.fingerprint = projectInfo.sourceFingerprint;
    pinnedSource.project = projectInfo;
    pinnedSource.project.absolutePath =
        std::filesystem::path(projectData.projectFileName).lexically_normal();
    pinnedSource.project.relativePath = pinnedSource.project.absolutePath.filename();
    std::error_code projectExistsError;
    const bool projectFileExists =
        std::filesystem::exists(pinnedSource.project.absolutePath, projectExistsError) &&
        !projectExistsError;
    pinnedSource.project.status = projectInfo.embeddedRaw
        ? Stack::RawWorkspace::ProjectStatus::Embedded
        : (projectFileExists
            ? Stack::RawWorkspace::ProjectStatus::Existing
            : Stack::RawWorkspace::ProjectStatus::NoProject);
    pinnedSource.project.associationReason = "Pinned project loaded outside the active RAW folder.";

    auto catalogSourceIt = std::find_if(
        m_RawWorkspace.sources.begin(),
        m_RawWorkspace.sources.end(),
        [&](const Stack::RawWorkspace::SourceRecord& source) {
            return source.relativePathKey == pinnedSource.relativePathKey;
        });
    if (catalogSourceIt != m_RawWorkspace.sources.end()) {
        catalogSourceIt->project = pinnedSource.project;
        m_PinnedRawWorkspaceSource.reset();
        m_RawWorkspaceSelectedSourceBeforePinnedProject.clear();
        m_ActiveRawWorkspaceSourceKey = catalogSourceIt->relativePathKey;
    } else {
        if (!m_PinnedRawWorkspaceSource.has_value()) {
            m_RawWorkspaceSelectedSourceBeforePinnedProject =
                m_RawWorkspace.selectedSourceKey;
        }
        m_PinnedRawWorkspaceSource = std::move(pinnedSource);
        m_ActiveRawWorkspaceSourceKey = m_PinnedRawWorkspaceSource->relativePathKey;
    }
    m_RawWorkspace.selectedSourceKey = m_ActiveRawWorkspaceSourceKey;
    m_ActiveRawWorkspaceProjectPath =
        std::filesystem::path(projectData.projectFileName).lexically_normal();
    m_ActiveRawWorkspaceRecipe = std::move(recipe);
    m_ActiveRawWorkspaceMode = projectInfo.mode;
    m_ActiveManagedRawSection = projectData.rawWorkspaceData.is_object()
        ? Stack::RawWorkspace::DeserializeManagedRawSection(
            projectData.rawWorkspaceData.value(
                "managedRawSection",
                nlohmann::json::object()))
        : Stack::RawWorkspace::ManagedRawSection{};
    m_RawWorkspacePipelineActive = true;
    m_RawWorkspaceLockedByEditorProject = false;
    m_RawWorkspaceStaleRenderStatusText.clear();
    return true;
}

std::uint64_t EditorModule::BumpRawWorkspaceProjectSaveRevision(
    const std::filesystem::path& workspaceRoot,
    const std::string& sourceKey) {
    if (workspaceRoot.empty() || sourceKey.empty()) {
        return 0;
    }

    const std::string key = RawWorkspaceProjectSaveRevisionKey(workspaceRoot, sourceKey);
    std::lock_guard<std::mutex> lock(m_RawWorkspaceProjectSaveRevisionMutex);
    return ++m_RawWorkspaceProjectSaveRevisions[key];
}

std::uint64_t EditorModule::GetRawWorkspaceProjectSaveRevision(
    const std::filesystem::path& workspaceRoot,
    const std::string& sourceKey) const {
    if (workspaceRoot.empty() || sourceKey.empty()) {
        return 0;
    }

    const std::string key = RawWorkspaceProjectSaveRevisionKey(workspaceRoot, sourceKey);
    std::lock_guard<std::mutex> lock(m_RawWorkspaceProjectSaveRevisionMutex);
    const auto it = m_RawWorkspaceProjectSaveRevisions.find(key);
    return it == m_RawWorkspaceProjectSaveRevisions.end() ? 0 : it->second;
}

bool EditorModule::IsRawWorkspaceProjectSaveJobCurrent(const RawWorkspaceProjectSaveJob& job) const {
    return job.sourceRevision != 0 &&
        GetRawWorkspaceProjectSaveRevision(job.workspaceRoot, job.sourceKey) == job.sourceRevision;
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
        m_NodeGraph.AddRawDevelopmentNode(std::move(payload), EditorNodeGraph::Vec2{ 20.0f, 120.0f });
    if (!rawNode) {
        if (outError) {
            *outError = "Failed to create the RAW Development node.";
        }
        return false;
    }
    const int rawNodeId = rawNode->id;

    EditorNodeGraph::Node* outputNode =
        m_NodeGraph.AddOutputNode(EditorNodeGraph::Vec2{ 320.0f, 120.0f }, true);
    if (!outputNode) {
        if (outError) {
            *outError = "Failed to create the Output node.";
        }
        return false;
    }
    const int outputNodeId = outputNode->id;

    std::string connectError;
    if (!m_NodeGraph.TryConnectSockets(
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

    m_NodeGraph.SetOutputNodeId(outputNodeId);
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
        submitted = Async::TaskSystem::Get().Submit(
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
                        result.loadedProject.sourcePixels.assign(4, 0);
                        result.loadedProject.width = 1;
                        result.loadedProject.height = 1;
                        result.loadedProject.channels = 4;
                        result.loadedProject.pipelineData = document.pipelineData.is_null()
                            ? nlohmann::json::array()
                            : document.pipelineData;
                        if (result.loadedProject.pipelineData.empty() &&
                            document.rawWorkspaceData.is_object() &&
                            document.rawWorkspaceData.contains("downstreamGraph")) {
                            result.loadedProject.pipelineData =
                                document.rawWorkspaceData["downstreamGraph"];
                        }
                        result.loadedProject.rawWorkspaceData = document.rawWorkspaceData;
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
                        result.success = true;
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

                Async::TaskSystem::Get().PostToMain(
                    [this, generation, result = std::move(result)]() mutable {
                        if (generation != m_RawWorkspaceProjectLoadGeneration.load(
                                std::memory_order_relaxed) ||
                            !m_RawWorkspaceRootTabActive) {
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
                            QueueUiNotification(
                                UiNotificationSeverity::Error,
                                m_RawWorkspaceProjectLoadStatusText,
                                "raw-workspace-project-load");
                            if (IsRawWorkspaceProjectActive()) {
                                m_RawWorkspace.selectedSourceKey =
                                    m_ActiveRawWorkspaceSourceKey;
                            }
                            return;
                        }

                        if (!result.hasRawWorkspaceInfo) {
                            result.recipe = BuildRawWorkspaceDefaultRecipe(result.source);
                            result.mode = Stack::RawWorkspace::RawProjectMode::RecipeBacked;
                            result.managedSection = {};
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
                            QueueUiNotification(
                                UiNotificationSeverity::Error,
                                m_RawWorkspaceProjectLoadStatusText,
                                "raw-workspace-project-load");
                            if (IsRawWorkspaceProjectActive()) {
                                m_RawWorkspace.selectedSourceKey =
                                    m_ActiveRawWorkspaceSourceKey;
                            }
                            return;
                        }

                        m_ActiveRawWorkspaceRecipe = std::move(result.recipe);
                        m_ActiveRawWorkspaceMode = result.mode;
                        m_ActiveManagedRawSection = result.managedSection;
                        m_ActiveRawWorkspaceSourceKey = result.source.relativePathKey;
                        m_RawWorkspaceStaleRenderStatusText.clear();
                        m_ActiveRawWorkspaceProjectPath =
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
        QueueUiNotification(
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
    if (!m_RawWorkspaceRootTabActive) {
        m_PendingRawWorkspaceDeferredProjectFinalize = false;
        m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
        m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
        m_PendingRawWorkspaceOpenGraphSourceKey.clear();
        m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Idle;
        m_RawWorkspaceProjectLoadSourceKey.clear();
        m_RawWorkspaceProjectLoadStatusText.clear();
        ResetDeferredLoadedProjectApplyState();
        m_RawWorkspacePipelineActive = false;
        return;
    }
    if (m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey != m_ActiveRawWorkspaceSourceKey) {
        const std::string deferredSourceKey = m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey;
        m_PendingRawWorkspaceDeferredProjectFinalize = false;
        m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
        if (m_PendingRawWorkspaceOpenGraphSourceKey == deferredSourceKey) {
            m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
            m_PendingRawWorkspaceOpenGraphSourceKey.clear();
        }
        return;
    }

    m_RawWorkspacePipelineActive = true;
    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::ManagedDecomposed &&
        !ValidateActiveRawWorkspaceManagedGraph(false)) {
        MarkActiveRawWorkspaceProjectAsCustomGraph(Stack::RawWorkspace::kCustomGraphReadOnlyReason);
        QueueUiNotification(
            UiNotificationSeverity::Info,
            Stack::RawWorkspace::kCustomGraphReadOnlyReason,
            "raw-workspace-managed-load-invalid");
    }

    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::CustomGraph) {
        MarkDirty();
    } else {
        ClearDirty();
    }

    m_PendingRawWorkspaceDeferredProjectFinalize = false;
    m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
    m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Idle;
    const Stack::RawWorkspace::SourceRecord* activeSource =
        FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey);
    const bool activeSourceHasStoredProject =
        activeSource &&
        (activeSource->project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
         activeSource->project.status == Stack::RawWorkspace::ProjectStatus::Embedded);
    m_RawWorkspaceProjectLoadStatusText = activeSourceHasStoredProject
        ? "RAW project loaded."
        : "RAW preview ready.";

    if (m_PendingRawWorkspaceOpenGraphAfterProjectLoad &&
        m_PendingRawWorkspaceOpenGraphSourceKey == m_ActiveRawWorkspaceSourceKey) {
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
        m_ActiveRawWorkspaceSourceKey == source.relativePathKey) {
        outRecipe = m_ActiveRawWorkspaceRecipe;
        if (outMode) {
            *outMode = m_ActiveRawWorkspaceMode;
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
        *outMode = Stack::RawWorkspace::RawProjectMode::RecipeBacked;
    }
    return true;
}

bool EditorModule::FocusRawWorkspaceDevelopmentNode() {
    SwitchToSubWindow(EditorSubWindow::NodeGraph);
    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::ManagedDecomposed &&
        m_ActiveManagedRawSection.rawDecodeNodeId > 0 &&
        m_NodeGraph.FindNode(m_ActiveManagedRawSection.rawDecodeNodeId)) {
        SelectGraphNode(m_ActiveManagedRawSection.rawDecodeNodeId);
        return true;
    }
    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::CustomGraph &&
        m_ActiveManagedRawSection.rawSourceNodeId > 0 &&
        m_NodeGraph.FindNode(m_ActiveManagedRawSection.rawSourceNodeId)) {
        SelectGraphNode(m_ActiveManagedRawSection.rawSourceNodeId);
        return true;
    }
    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::RawDevelopment) {
            continue;
        }
        const std::string& key = node.rawDevelopment.recipe.source.relativePathKey;
        if (m_ActiveRawWorkspaceSourceKey.empty() || key.empty() || key == m_ActiveRawWorkspaceSourceKey) {
            SelectGraphNode(node.id);
            return true;
        }
    }
    return false;
}

bool EditorModule::OpenRawWorkspaceProjectInGraph(const Stack::RawWorkspace::SourceRecord& source) {
    if (source.project.status != Stack::RawWorkspace::ProjectStatus::Existing &&
        source.project.status != Stack::RawWorkspace::ProjectStatus::Embedded) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Make an edit to create this RAW project first.",
            "raw-workspace-open-graph-preview-only");
        return false;
    }

    if (!IsRawWorkspaceProjectActive() || m_ActiveRawWorkspaceSourceKey != source.relativePathKey) {
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
    const Stack::RawWorkspace::SourceRecord& source) {
    if (source.project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
        source.project.status == Stack::RawWorkspace::ProjectStatus::Embedded) {
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
        QueueUiNotification(
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
    loadedProject->sourcePixels.assign(4, 0);
    loadedProject->width = 1;
    loadedProject->height = 1;
    loadedProject->channels = 4;
    loadedProject->pipelineData =
        EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(), previewGraph);
    StackBinaryFormat::ProjectDocument previewDocument;
    Stack::RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
        source,
        recipe,
        nlohmann::json::object(),
        previewDocument,
        Stack::RawWorkspace::RawProjectMode::RecipeBacked,
        true);
    loadedProject->rawWorkspaceData = std::move(previewDocument.rawWorkspaceData);
    loadedProject->projectKind = StackBinaryFormat::kRawProjectKind;
    loadedProject->projectName = projectName;
    loadedProject->projectFileName = expectedProject.absolutePath.string();

    if (!BeginDeferredLoadedProjectApply(loadedProject)) {
        m_PendingRawWorkspaceDeferredProjectFinalize = false;
        m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
        m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Failed;
        m_RawWorkspaceProjectLoadStatusText = "Failed to apply the RAW preview.";
        if (IsRawWorkspaceProjectActive()) {
            m_RawWorkspace.selectedSourceKey = m_ActiveRawWorkspaceSourceKey;
        }
        return false;
    }

    m_ActiveRawWorkspaceSourceKey = source.relativePathKey;
    m_RawWorkspaceStaleRenderStatusText.clear();
    m_ActiveRawWorkspaceProjectPath = expectedProject.absolutePath;
    m_ActiveRawWorkspaceRecipe = std::move(recipe);
    m_ActiveRawWorkspaceMode = Stack::RawWorkspace::RawProjectMode::RecipeBacked;
    m_ActiveManagedRawSection = {};
    m_RawWorkspaceProjectLoadSourceKey = source.relativePathKey;
    m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Applying;
    m_RawWorkspaceProjectLoadStatusText = "Applying RAW preview...";
    m_PendingRawWorkspaceDeferredProjectFinalize = true;
    m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey = source.relativePathKey;
    return true;
}

bool EditorModule::LoadActiveRawWorkspaceProjectInGraph() {
    if (!IsRawWorkspaceProjectActive()) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
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

bool EditorModule::EnsureRawWorkspaceProjectForSelectedRecipeEdit(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    const Stack::RawWorkspace::SourceRecord* selectedSource =
        FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
    if (!selectedSource) {
        QueueUiNotification(
            UiNotificationSeverity::Error,
            "Select a RAW source before editing.",
            "raw-workspace-no-selection");
        return false;
    }

    const bool alreadyActive =
        IsRawWorkspaceProjectActive() &&
        m_ActiveRawWorkspaceSourceKey == selectedSource->relativePathKey;
    if (!alreadyActive &&
        (m_RawWorkspacePreviewStageQueued &&
         m_RawWorkspacePreviewStageSourceKey == selectedSource->relativePathKey)) {
        return false;
    }
    if (Async::IsBusy(m_RawWorkspaceProjectLoadTaskState) &&
        m_RawWorkspaceProjectLoadSourceKey == selectedSource->relativePathKey) {
        return false;
    }
    if (m_PendingRawWorkspaceDeferredProjectFinalize &&
        m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey == selectedSource->relativePathKey &&
        IsDeferredLoadedProjectApplyActive()) {
        return false;
    }
    bool needsDefaultGraph = !alreadyActive;

    if (m_ActiveRawWorkspaceSourceKey != selectedSource->relativePathKey) {
        if (!FlushActiveRawWorkspaceProjectIfDirty()) {
            return false;
        }
        ClearRawWorkspaceLivePreviewState();
        if (selectedSource->project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
            selectedSource->project.status == Stack::RawWorkspace::ProjectStatus::Embedded) {
            if (!RequestLoadRawWorkspaceProjectForSource(*selectedSource)) {
                return false;
            }
            QueueUiNotification(
                UiNotificationSeverity::Info,
                "Loading this RAW project before applying edits.",
                "raw-workspace-project-load-before-edit");
            return false;
        }
    }

    Stack::RawRecipe::RawDevelopmentRecipe resolvedRecipe = recipe;
    resolvedRecipe.source.sourcePath = selectedSource->absolutePath.string();
    resolvedRecipe.source.relativePathKey = selectedSource->relativePathKey;
    resolvedRecipe.source.fingerprint = selectedSource->fingerprint;
    resolvedRecipe.source.fileSizeBytes = static_cast<std::uint64_t>(selectedSource->fileSizeBytes);
    resolvedRecipe.source.modifiedTimeTicks = selectedSource->modifiedTimeTicks;
    resolvedRecipe.source.displayName = selectedSource->fileName;

    const Stack::RawWorkspace::ManagedLayout layout =
        Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot);
    const Stack::RawWorkspace::ProjectInfo expectedProject =
        Stack::RawWorkspace::BuildExpectedProjectInfo(layout, *selectedSource);

    const bool selectedHasNoProject =
        selectedSource->project.status == Stack::RawWorkspace::ProjectStatus::NoProject ||
        selectedSource->project.absolutePath.empty();
    const std::filesystem::path targetProjectPath = selectedHasNoProject
            ? expectedProject.absolutePath
            : selectedSource->project.absolutePath;
    const Stack::RawWorkspace::RawProjectMode targetMode = selectedHasNoProject
        ? Stack::RawWorkspace::RawProjectMode::RecipeBacked
        : m_ActiveRawWorkspaceMode;

    if (needsDefaultGraph) {
        const nlohmann::json previousPipeline = SerializePipeline();
        std::string graphError;
        if (!BuildRawWorkspaceProjectGraph(resolvedRecipe, true, &graphError)) {
            DeserializePipeline(previousPipeline);
            std::cerr
                << "[RAW Workspace] Failed to create edit project graph"
                << " selectedSourceKey=" << selectedSource->relativePathKey
                << " activeSourceKey=" << m_ActiveRawWorkspaceSourceKey
                << " targetProjectPath=" << targetProjectPath.string()
                << " error=" << (graphError.empty() ? std::string("unknown") : graphError)
                << "\n";
            QueueUiNotification(
                UiNotificationSeverity::Error,
                graphError.empty()
                    ? "Failed to create the RAW project graph."
                    : "Failed to create the RAW project graph: " + graphError,
                "raw-workspace-project-graph-create");
            return false;
        }
    }
    m_ActiveRawWorkspaceSourceKey = selectedSource->relativePathKey;
    m_RawWorkspacePipelineActive = true;
    m_RawWorkspaceStaleRenderStatusText.clear();
    m_ActiveRawWorkspaceProjectPath = targetProjectPath;
    m_ActiveRawWorkspaceRecipe = resolvedRecipe;
    m_ActiveRawWorkspaceMode = targetMode;
    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::RecipeBacked) {
        m_ActiveManagedRawSection = {};
    }
    SetCurrentProjectName(selectedSource->stem.empty() ? selectedSource->fileName : selectedSource->stem);
    SetCurrentProjectFileName(m_ActiveRawWorkspaceProjectPath.string());
    MarkDirty();
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
    if (IsRawWorkspaceProjectActive() &&
        m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::ManagedDecomposed &&
        m_RawWorkspace.selectedSourceKey == m_ActiveRawWorkspaceSourceKey) {
        std::string reason;
        if (!Stack::RawWorkspace::IsRecipeRepresentableAsManagedGraph(recipe, &reason)) {
            QueueUiNotification(
                UiNotificationSeverity::Info,
                reason.empty() ? "This RAW edit cannot be represented by the managed graph." : reason,
                "raw-workspace-managed-recipe-blocked");
            return false;
        }
    }

    if (!EnsureRawWorkspaceProjectForSelectedRecipeEdit(recipe)) {
        return false;
    }

    if (Stack::RawWorkspace::SourceRecord* source =
            FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey)) {
        if (source->project.absolutePath.empty()) {
            const Stack::RawWorkspace::ManagedLayout layout =
                Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot);
            const Stack::RawWorkspace::ProjectInfo expectedProject =
                Stack::RawWorkspace::BuildExpectedProjectInfo(layout, *source);
            source->project.absolutePath = expectedProject.absolutePath;
            source->project.relativePath = expectedProject.relativePath;
        }
        if (source->project.status == Stack::RawWorkspace::ProjectStatus::Unknown ||
            source->project.status == Stack::RawWorkspace::ProjectStatus::NoProject) {
            source->project.status = Stack::RawWorkspace::ProjectStatus::Existing;
        }
        source->project.mode = m_ActiveRawWorkspaceMode;
        source->project.autosaved = false;
        source->project.dirty = true;
        InvalidateRawWorkspaceGalleryPresentation();
    }

    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::ManagedDecomposed) {
        if (!ApplyActiveRawWorkspaceRecipeToManagedGraph()) {
            return false;
        }
    } else {
        for (EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
            if (node.kind != EditorNodeGraph::NodeKind::RawDevelopment) {
                continue;
            }
            const std::string key = node.rawDevelopment.recipe.source.relativePathKey;
            if (key.empty() || key == m_ActiveRawWorkspaceSourceKey) {
                node.rawDevelopment.recipe = m_ActiveRawWorkspaceRecipe;
                node.rawDevelopment.projectStatus = "Edited";
                node.rawDevelopment.edited = true;
                node.rawDevelopment.autosaved = false;
                MarkRenderDirty(node.id);
            }
        }
    }
    MarkDirty();
    NoteRawWorkspaceRecipePreviewEdit(interactionActive);
    return true;
}

void EditorModule::EnsureRawWorkspaceProjectSaveWorker() {
    if (m_RawWorkspaceProjectSaveWorker.joinable()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceProjectSaveMutex);
        m_RawWorkspaceProjectSaveWorkerStopRequested = false;
        m_RawWorkspaceProjectSaveWorkerBusy = false;
    }
    m_RawWorkspaceProjectSaveWorker = std::thread([this]() {
        RawWorkspaceProjectSaveWorkerLoop();
    });
}

void EditorModule::EnqueueRawWorkspaceProjectSave(RawWorkspaceProjectSaveJob job) {
    EnsureRawWorkspaceProjectSaveWorker();
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceProjectSaveMutex);
        m_RawWorkspaceProjectSaveQueue.push_back(std::move(job));
    }
    ++m_RawWorkspaceProjectSaveInFlightCount;
    m_RawWorkspaceProjectSaveTaskState = Async::TaskState::Queued;
    m_RawWorkspaceProjectSaveStatusText = "Saving RAW project...";
    m_RawWorkspaceProjectSaveCv.notify_one();
}

void EditorModule::RequestRawWorkspaceProjectSaveWorkerDrain() {
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceProjectSaveMutex);
        m_RawWorkspaceProjectSaveWorkerStopRequested = true;
    }
    m_RawWorkspaceProjectSaveCv.notify_all();
}

bool EditorModule::IsRawWorkspaceProjectSaveWorkerIdle() const {
    std::lock_guard<std::mutex> lock(m_RawWorkspaceProjectSaveMutex);
    return m_RawWorkspaceProjectSaveQueue.empty() && !m_RawWorkspaceProjectSaveWorkerBusy;
}

void EditorModule::ShutdownRawWorkspaceProjectSaveWorker() {
    RequestRawWorkspaceProjectSaveWorkerDrain();
    if (m_RawWorkspaceProjectSaveWorker.joinable()) {
        m_RawWorkspaceProjectSaveWorker.join();
    }
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceProjectSaveMutex);
        m_RawWorkspaceProjectSaveQueue.clear();
        m_RawWorkspaceProjectSaveWorkerStopRequested = false;
        m_RawWorkspaceProjectSaveWorkerBusy = false;
    }
    m_RawWorkspaceProjectSaveInFlightCount = 0;
    m_RawWorkspaceProjectSaveTaskState = Async::TaskState::Idle;
    m_RawWorkspaceProjectSaveStatusText.clear();
}

bool EditorModule::WriteRawWorkspaceProjectSaveJob(
    RawWorkspaceProjectSaveJob& job,
    std::string& error,
    bool& skippedStale) const {
    try {
        std::lock_guard<std::mutex> fileLock(m_RawWorkspaceProjectFileWriteMutex);
        // The revision must be checked while the file lock is held. A newer
        // synchronous transition save may otherwise finish after the worker's
        // first check but before this older job writes the file.
        if (!IsRawWorkspaceProjectSaveJobCurrent(job)) {
            skippedStale = true;
            return true;
        }
        StackBinaryFormat::ProjectDocument existingDocumentForWorker;
        StackBinaryFormat::ProjectLoadOptions existingOptions;
        existingOptions.includeThumbnail = false;
        existingOptions.includeSourceImage = false;
        existingOptions.includePipelineData = false;
        existingOptions.includeNodeBrowserThumbnails = false;
        existingOptions.includeRawWorkspaceData = true;
        const bool loadedExistingRawWorkspaceData =
            std::filesystem::exists(job.projectPath) &&
            StackBinaryFormat::ReadProjectFile(job.projectPath, existingDocumentForWorker, existingOptions);
        nlohmann::json workerEmbeddedRaw = nlohmann::json::object();
        bool preserveWorkerEmbeddedRaw = false;
        if (loadedExistingRawWorkspaceData &&
            existingDocumentForWorker.rawWorkspaceData.is_object()) {
            auto embeddedIt =
                existingDocumentForWorker.rawWorkspaceData.find("embeddedRaw");
            if (embeddedIt != existingDocumentForWorker.rawWorkspaceData.end() &&
                embeddedIt->is_object() &&
                embeddedIt->value("present", false)) {
                workerEmbeddedRaw = std::move(*embeddedIt);
                existingDocumentForWorker.rawWorkspaceData.erase(embeddedIt);
                preserveWorkerEmbeddedRaw = true;
            }
            nlohmann::json mergedRawWorkspaceData =
                std::move(existingDocumentForWorker.rawWorkspaceData);
            OverlayOwnedJsonFields(
                mergedRawWorkspaceData,
                job.document.rawWorkspaceData);
            job.document.rawWorkspaceData =
                std::move(mergedRawWorkspaceData);
        }
        if (preserveWorkerEmbeddedRaw) {
            job.document.rawWorkspaceData["embeddedRaw"] =
                std::move(workerEmbeddedRaw);
            job.document.rawWorkspaceData["rawSourceRef"]["linked"] = false;
            job.document.rawWorkspaceData["rawSourceRef"]["embedded"] = true;
        } else if (job.projectStatus == Stack::RawWorkspace::ProjectStatus::Embedded) {
            error = "Failed to preserve the embedded RAW source while saving.";
            return false;
        }

        if (job.projectPath.has_parent_path()) {
            std::filesystem::create_directories(job.projectPath.parent_path());
        }
        if (!StackBinaryFormat::WriteProjectFile(job.projectPath, job.document)) {
            error = "Failed to save the RAW project.";
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    } catch (...) {
        error = "Failed to save the RAW project.";
        return false;
    }
}

void EditorModule::CompleteRawWorkspaceProjectSave(
    RawWorkspaceProjectSaveJob job,
    bool success,
    bool skippedStale,
    std::string errorMessage) {
    if (m_RawWorkspaceProjectSaveInFlightCount > 0) {
        --m_RawWorkspaceProjectSaveInFlightCount;
    }
    m_RawWorkspaceProjectSaveTaskState = m_RawWorkspaceProjectSaveInFlightCount > 0
        ? Async::TaskState::Running
        : Async::TaskState::Idle;
    m_RawWorkspaceProjectSaveStatusText = m_RawWorkspaceProjectSaveInFlightCount > 0
        ? "Saving RAW project..."
        : std::string();

    if (skippedStale) {
        return;
    }

    const bool jobWorkspaceCurrent =
        job.workspaceRoot.lexically_normal() == m_RawWorkspace.workspaceRoot.lexically_normal();
    const bool jobRevisionCurrent = IsRawWorkspaceProjectSaveJobCurrent(job);
    if (!jobRevisionCurrent) {
        // The write may have completed just before a newer synchronous save
        // acquired the file lock. In that case the newer save is already the
        // authoritative catalog/dirty state, so this delayed completion must
        // not roll any of it back.
        return;
    }
    Stack::RawWorkspace::SourceRecord* savedSource = jobWorkspaceCurrent
        ? FindRawWorkspaceSourceByKey(job.sourceKey)
        : nullptr;
    if (success) {
        if (savedSource) {
            savedSource->project.status = job.projectStatus;
            savedSource->project.absolutePath = job.projectPath;
            savedSource->project.relativePath = job.projectRelativePath;
            savedSource->project.mode = job.mode;
            savedSource->project.projectModifiedTimeTicks = RawWorkspaceProjectFileTimeTicks(job.projectPath);
            if (jobRevisionCurrent) {
                savedSource->project.autosaved = true;
                savedSource->project.dirty = false;
            }
            InvalidateRawWorkspaceGalleryPresentation();
        }
        if (jobWorkspaceCurrent &&
            jobRevisionCurrent &&
            m_ActiveRawWorkspaceSourceKey == job.sourceKey) {
            if (IsRawWorkspaceProjectActive()) {
                SetCurrentProjectName(job.projectName);
                SetCurrentProjectFileName(job.projectPath.string());
                ClearDirty();
            }
        }
        if (jobWorkspaceCurrent) {
            PersistRawWorkspaceCatalog();
        }
        if (jobRevisionCurrent && jobWorkspaceCurrent) {
            QueueUiNotification(
                UiNotificationSeverity::Success,
                "RAW project autosaved.",
                "raw-workspace-project-autosave");
        }
    } else {
        if (savedSource) {
            if (jobRevisionCurrent) {
                savedSource->project.absolutePath = job.previousAbsolutePath;
                savedSource->project.relativePath = job.previousRelativePath;
                savedSource->project.status = job.previousProjectStatus;
                savedSource->project.mode = job.previousMode;
                savedSource->project.autosaved = job.previousAutosaved;
            }
            savedSource->project.dirty = true;
            InvalidateRawWorkspaceGalleryPresentation();
        }
        if (jobWorkspaceCurrent && m_ActiveRawWorkspaceSourceKey == job.sourceKey) {
            if (IsRawWorkspaceProjectActive()) {
                MarkDirty();
            }
        }
        if (jobWorkspaceCurrent) {
            PersistRawWorkspaceCatalog();
        }
        QueueUiNotification(
            UiNotificationSeverity::Error,
            errorMessage.empty() ? "Failed to save the RAW project." : errorMessage,
            "raw-workspace-project-save");
    }
}

void EditorModule::RawWorkspaceProjectSaveWorkerLoop() {
    while (true) {
        RawWorkspaceProjectSaveJob job;
        {
            std::unique_lock<std::mutex> lock(m_RawWorkspaceProjectSaveMutex);
            m_RawWorkspaceProjectSaveCv.wait(lock, [this]() {
                return m_RawWorkspaceProjectSaveWorkerStopRequested ||
                    !m_RawWorkspaceProjectSaveQueue.empty();
            });
            if (m_RawWorkspaceProjectSaveQueue.empty()) {
                if (m_RawWorkspaceProjectSaveWorkerStopRequested) {
                    break;
                }
                continue;
            }
            job = std::move(m_RawWorkspaceProjectSaveQueue.front());
            m_RawWorkspaceProjectSaveQueue.pop_front();
            m_RawWorkspaceProjectSaveWorkerBusy = true;
        }

        std::string error;
        bool skippedStale = false;
        bool success = true;
        if (!IsRawWorkspaceProjectSaveJobCurrent(job)) {
            skippedStale = true;
        } else {
            success = WriteRawWorkspaceProjectSaveJob(
                job,
                error,
                skippedStale);
        }

        bool postCompletion = true;
        {
            std::lock_guard<std::mutex> lock(m_RawWorkspaceProjectSaveMutex);
            m_RawWorkspaceProjectSaveWorkerBusy = false;
            postCompletion = !m_RawWorkspaceProjectSaveWorkerStopRequested;
        }
        m_RawWorkspaceProjectSaveCv.notify_all();

        if (postCompletion) {
            Async::TaskSystem::Get().PostToMain([
                this,
                job = std::move(job),
                success,
                skippedStale,
                error = std::move(error)
            ]() mutable {
                CompleteRawWorkspaceProjectSave(
                    std::move(job),
                    success,
                    skippedStale,
                    std::move(error));
            });
        }
    }
}

bool EditorModule::SaveActiveRawWorkspaceProject(
    bool explicitSave,
    bool synchronousAutosave) {
    if (!IsRawWorkspaceProjectActive()) {
        return false;
    }
    if (IsMultiFrameRawProjectActive()) {
        (void)explicitSave;
        (void)synchronousAutosave;
        std::string error;
        const bool saved = SaveActiveMultiFrameRawProject(&error);
        if (!saved) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                error.empty() ? "Failed to save the multi-frame RAW project." : error,
                "multi-frame-raw-project-save");
        }
        return saved;
    }

    Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey);
    if (!source) {
        QueueUiNotification(
            UiNotificationSeverity::Error,
            "The active RAW source is no longer in the Workspace scan.",
            "raw-workspace-save-missing-source");
        return false;
    }

    const std::filesystem::path workspaceRoot = m_RawWorkspace.workspaceRoot;
    const std::filesystem::path projectPath = m_ActiveRawWorkspaceProjectPath;
    const std::string sourceKey = m_ActiveRawWorkspaceSourceKey;
    const bool pinnedExternalProject =
        m_PinnedRawWorkspaceSource.has_value() &&
        m_PinnedRawWorkspaceSource->relativePathKey == sourceKey;
    const bool requireSynchronousSave =
        synchronousAutosave || workspaceRoot.empty() || pinnedExternalProject;
    const std::uint64_t saveRevision =
        BumpRawWorkspaceProjectSaveRevision(workspaceRoot, sourceKey);
    StackBinaryFormat::ProjectDocument document;
    StackBinaryFormat::ProjectDocument existingDocument;
    nlohmann::json existingRawWorkspaceData = nlohmann::json::object();
    nlohmann::json existingEmbeddedRaw = nlohmann::json::object();
    bool preserveEmbeddedRaw = false;
    bool loadedExistingRawWorkspaceData = false;
    StackBinaryFormat::ProjectLoadOptions existingOptions;
    existingOptions.includeThumbnail = false;
    existingOptions.includeSourceImage = false;
    existingOptions.includePipelineData = false;
    existingOptions.includeNodeBrowserThumbnails = false;
    existingOptions.includeRawWorkspaceData = true;
    {
        std::lock_guard<std::mutex> fileLock(m_RawWorkspaceProjectFileWriteMutex);
        loadedExistingRawWorkspaceData =
            std::filesystem::exists(projectPath) &&
            StackBinaryFormat::ReadProjectFile(
                projectPath,
                existingDocument,
                existingOptions);
    }
    if (loadedExistingRawWorkspaceData &&
        existingDocument.rawWorkspaceData.is_object()) {
        existingRawWorkspaceData =
            std::move(existingDocument.rawWorkspaceData);
        auto embeddedIt = existingRawWorkspaceData.find("embeddedRaw");
        if (embeddedIt != existingRawWorkspaceData.end() &&
            embeddedIt->is_object() &&
            embeddedIt->value("present", false)) {
            existingEmbeddedRaw = std::move(*embeddedIt);
            existingRawWorkspaceData.erase(embeddedIt);
            preserveEmbeddedRaw = true;
        }
    }
    if (source->project.status == Stack::RawWorkspace::ProjectStatus::Embedded &&
        !preserveEmbeddedRaw) {
        MarkDirty();
        QueueUiNotification(
            UiNotificationSeverity::Error,
            "Failed to preserve the embedded RAW source while saving.",
            "raw-workspace-project-save");
        return false;
    }

    const std::string projectName = m_CurrentProjectName.empty()
        ? (source->stem.empty() ? source->fileName : source->stem)
        : m_CurrentProjectName;
    const nlohmann::json pipeline = SerializePipeline();
    if (!explicitSave) {
        document = {};
        document.metadata.projectKind = StackBinaryFormat::kRawProjectKind;
        document.metadata.projectName = projectName;
        document.pipelineData = pipeline;
        EnsureMinimalProjectDocument(document, projectName);
    } else if (!BuildProjectDocumentForSave(projectName, document)) {
        document = {};
        document.metadata.projectKind = StackBinaryFormat::kRawProjectKind;
        document.metadata.projectName = projectName;
        document.pipelineData = pipeline;
        document.nodeBrowserThumbnailEntries = GetPersistedNodeBrowserThumbnails();
        EnsureMinimalProjectDocument(document, projectName);
    }
    Stack::RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
        *source,
        m_ActiveRawWorkspaceRecipe,
        pipeline,
        document,
        m_ActiveRawWorkspaceMode,
        !preserveEmbeddedRaw);
    ApplyActiveRawWorkspaceModeDataToDocument(document);
    if (loadedExistingRawWorkspaceData &&
        existingRawWorkspaceData.is_object()) {
        nlohmann::json mergedRawWorkspaceData =
            std::move(existingRawWorkspaceData);
        OverlayOwnedJsonFields(
            mergedRawWorkspaceData,
            document.rawWorkspaceData);
        document.rawWorkspaceData = std::move(mergedRawWorkspaceData);
    }
    if (preserveEmbeddedRaw) {
        document.rawWorkspaceData["embeddedRaw"] =
            std::move(existingEmbeddedRaw);
        document.rawWorkspaceData["rawSourceRef"]["linked"] = false;
        document.rawWorkspaceData["rawSourceRef"]["embedded"] = true;
    }

    const Stack::RawWorkspace::RawProjectMode savedMode = m_ActiveRawWorkspaceMode;
    const Stack::RawWorkspace::ProjectStatus savedProjectStatus =
        source->project.status == Stack::RawWorkspace::ProjectStatus::Embedded
            ? Stack::RawWorkspace::ProjectStatus::Embedded
            : Stack::RawWorkspace::ProjectStatus::Existing;
    const std::filesystem::path projectRelativePath = source->project.relativePath.empty()
        ? Stack::RawWorkspace::BuildProjectRelativePathForSource(*source)
        : source->project.relativePath;

    if (!explicitSave && !requireSynchronousSave) {
        RawWorkspaceProjectSaveJob job;
        job.workspaceRoot = workspaceRoot;
        job.projectPath = projectPath;
        job.projectRelativePath = projectRelativePath;
        job.sourceKey = sourceKey;
        job.projectName = projectName;
        job.sourceRevision = saveRevision;
        job.mode = savedMode;
        job.projectStatus = savedProjectStatus;
        job.previousAbsolutePath = source->project.absolutePath;
        job.previousRelativePath = source->project.relativePath;
        job.previousProjectStatus = source->project.status;
        job.previousMode = source->project.mode;
        job.previousAutosaved = source->project.autosaved;
        job.previousDirty = source->project.dirty;
        job.document = std::move(document);
        EnqueueRawWorkspaceProjectSave(std::move(job));

        if (source->project.absolutePath.empty()) {
            source->project.absolutePath = projectPath;
            source->project.relativePath = projectRelativePath;
        }
        if (source->project.status == Stack::RawWorkspace::ProjectStatus::Unknown ||
            source->project.status == Stack::RawWorkspace::ProjectStatus::NoProject) {
            source->project.status = Stack::RawWorkspace::ProjectStatus::Existing;
        }
        source->project.mode = savedMode;
        // A queued snapshot is not durable yet. Keep the source and current
        // revision dirty until the matching worker completion succeeds.
        source->project.autosaved = false;
        source->project.dirty = true;
        InvalidateRawWorkspaceGalleryPresentation();
        SetCurrentProjectName(projectName);
        SetCurrentProjectFileName(projectPath.string());
        m_LastAutoSaveTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
        PersistRawWorkspaceCatalog();
        return true;
    }

    std::error_code ec;
    bool wroteProject = false;
    {
        std::lock_guard<std::mutex> fileLock(m_RawWorkspaceProjectFileWriteMutex);
        std::filesystem::create_directories(projectPath.parent_path(), ec);
        wroteProject = !ec && StackBinaryFormat::WriteProjectFile(projectPath, document);
    }
    if (!wroteProject) {
        MarkDirty();
        QueueUiNotification(
            UiNotificationSeverity::Error,
            "Failed to save the RAW project.",
            "raw-workspace-project-save");
        return false;
    }

    ApplyRawWorkspaceProjectInfoToSource(
        *source,
        document,
        projectPath,
        projectRelativePath,
        !explicitSave,
        false,
        explicitSave ? "explicit-save" : "autosave");
    InvalidateRawWorkspaceGalleryPresentation();
    for (EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::RawDevelopment &&
            node.rawDevelopment.recipe.source.relativePathKey == m_ActiveRawWorkspaceSourceKey) {
            node.rawDevelopment.projectStatus = "Edited";
            node.rawDevelopment.edited = true;
            node.rawDevelopment.autosaved = !explicitSave;
        }
    }

    PersistRawWorkspaceCatalog();
    SetCurrentProjectName(projectName);
    SetCurrentProjectFileName(projectPath.string());
    ClearDirty();
    m_LastAutoSaveTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    QueueUiNotification(
        UiNotificationSeverity::Success,
        explicitSave ? "RAW project saved." : "RAW project autosaved.",
        explicitSave ? "raw-workspace-project-save" : "raw-workspace-project-autosave");
    return true;
}

bool EditorModule::SaveActiveRawWorkspaceProjectIfDirty() {
    if (!IsRawWorkspaceProjectActive() || !m_Dirty) {
        return true;
    }
    if (!IsMultiFrameRawProjectActive() &&
        m_RawWorkspaceProjectSaveInFlightCount > 0) {
        return true;
    }
    return SaveActiveRawWorkspaceProject(false);
}

bool EditorModule::FlushActiveRawWorkspaceProjectIfDirty() {
    if (!IsRawWorkspaceProjectActive()) {
        return true;
    }
    if (!m_Dirty &&
        (IsMultiFrameRawProjectActive() ||
         m_RawWorkspaceProjectSaveInFlightCount == 0)) {
        return true;
    }
    // Project replacement and shutdown require a durable write before the
    // active session can be discarded. This lightweight autosave avoids
    // full-resolution output/source readbacks and PNG copies on the UI thread.
    return SaveActiveRawWorkspaceProject(false, true);
}

bool EditorModule::RequestSaveCurrentProject(
    const std::string& fallbackName,
    std::function<void(bool)> onComplete) {
    if (IsRawWorkspaceProjectActive()) {
        const bool success = SaveActiveRawWorkspaceProject(true);
        if (onComplete) {
            onComplete(success);
        }
        return success;
    }

    if (HasPendingGraphImageImports()) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Finishing imported slices before saving the project.",
            "editor-graph-image-save-wait");
        if (onComplete) {
            onComplete(false);
        }
        return false;
    }

    const std::string projectName = !fallbackName.empty()
        ? fallbackName
        : (m_CurrentProjectName.empty() ? "Untitled Project" : m_CurrentProjectName);
    const std::filesystem::path currentPath(m_CurrentProjectFileName);
    if (!currentPath.empty() && currentPath.is_absolute()) {
        LibraryManager::Get().RequestSaveProjectToPath(
            projectName,
            this,
            currentPath.lexically_normal(),
            std::move(onComplete));
        return true;
    }
    LibraryManager::Get().RequestSaveProject(projectName, this, m_CurrentProjectFileName, std::move(onComplete));
    return true;
}

bool EditorModule::RequestSaveProjectAs(
    const std::filesystem::path& requestedDestination,
    std::function<void(bool)> onComplete) {
    if (requestedDestination.empty() ||
        GetProjectSessionKind() == ProjectSessionKind::Empty ||
        IsProjectFileSaveBusy()) {
        if (onComplete) {
            onComplete(false);
        }
        return false;
    }

    if (IsMultiFrameRawProjectActive()) {
        const Stack::Project::ProjectStorageKind storageKind =
            m_ActiveRawProjectStore->StorageKind();
        std::string error;
        const bool success = SaveActiveMultiFrameRawProjectAs(
            requestedDestination,
            storageKind,
            &error);
        if (!success) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                error.empty() ? "Failed to save the project copy." : error,
                "project-file-save-as");
        } else {
            QueueUiNotification(
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
        QueueUiNotification(
            UiNotificationSeverity::Info,
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
    if (destinationExtension != ".stack") {
        destination += ".stack";
    }
    std::error_code absoluteError;
    destination = std::filesystem::absolute(destination, absoluteError).lexically_normal();
    if (absoluteError) {
        QueueUiNotification(
            UiNotificationSeverity::Error,
            "The selected project destination is invalid.",
            "project-file-save-as");
        if (onComplete) {
            onComplete(false);
        }
        return false;
    }

    std::filesystem::path currentPath = m_ActiveRawWorkspaceProjectPath;
    if (currentPath.empty() && !m_CurrentProjectFileName.empty()) {
        currentPath = std::filesystem::path(m_CurrentProjectFileName);
        if (currentPath.is_relative()) {
            currentPath = LibraryManager::Get().GetLibraryPath() / currentPath;
        }
        currentPath = currentPath.lexically_normal();
    }
    if (!currentPath.empty() && currentPath == destination) {
        return RequestSaveCurrentProject({}, std::move(onComplete));
    }

    const bool rawSession = IsRawWorkspaceProjectActive();
    const std::string sourceKey = rawSession
        ? m_ActiveRawWorkspaceSourceKey
        : std::string();
    const std::string projectName = m_CurrentProjectName.empty()
        ? (destination.stem().string().empty()
            ? std::string("Untitled Project")
            : destination.stem().string())
        : m_CurrentProjectName;

    auto document = std::make_shared<StackBinaryFormat::ProjectDocument>();
    if (!BuildProjectDocumentForSave(projectName, *document)) {
        if (!rawSession) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "Failed to capture the current project for Save As.",
                "project-file-save-as");
            if (onComplete) {
                onComplete(false);
            }
            return false;
        }

        const Stack::RawWorkspace::SourceRecord* source =
            FindRawWorkspaceSourceByKey(sourceKey);
        if (!source) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "The active RAW source is no longer available.",
                "project-file-save-as");
            if (onComplete) {
                onComplete(false);
            }
            return false;
        }
        document->metadata.projectKind = StackBinaryFormat::kRawProjectKind;
        document->metadata.projectName = projectName;
        document->pipelineData = SerializePipeline();
        document->nodeBrowserThumbnailEntries = GetPersistedNodeBrowserThumbnails();
        EnsureMinimalProjectDocument(*document, projectName);
        Stack::RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
            *source,
            m_ActiveRawWorkspaceRecipe,
            document->pipelineData,
            *document,
            m_ActiveRawWorkspaceMode,
            true);
        ApplyActiveRawWorkspaceModeDataToDocument(*document);
    }

    if (rawSession) {
        const Stack::RawWorkspace::SourceRecord* source =
            FindRawWorkspaceSourceByKey(sourceKey);
        std::string embedError;
        if (!source ||
            !Stack::RawWorkspace::EmbedRawSourceInProjectDocument(
                *source,
                *document,
                &embedError)) {
            m_ShowRawWorkspaceRelinkPopup = true;
            QueueUiNotification(
                UiNotificationSeverity::Error,
                embedError.empty()
                    ? "The linked RAW original must be relinked before Save As."
                    : embedError + " Relink the RAW original before trying Save As again.",
                "project-file-save-as");
            if (onComplete) {
                onComplete(false);
            }
            return false;
        }
    }

    ++m_ProjectFileSaveGeneration;
    const std::uint64_t generation = m_ProjectFileSaveGeneration;
    m_ProjectFileSaveTaskState = Async::TaskState::Running;
    m_ProjectFileSaveStatusText = "Writing the project to its new location...";

    bool submitted = false;
    try {
        submitted = Async::TaskSystem::Get().Submit([
            this,
            generation,
            destination,
            projectName,
            rawSession,
            sourceKey,
            document,
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
                    StackBinaryFormat::ProjectDocument verification;
                    StackBinaryFormat::ProjectLoadOptions options;
                    options.includeThumbnail = false;
                    options.includeSourceImage = false;
                    options.includePipelineData = false;
                    options.includeNodeBrowserThumbnails = false;
                    options.includeRawWorkspaceData = rawSession;
                    success = StackBinaryFormat::ReadProjectFile(
                        destination,
                        verification,
                        options);
                    if (success && rawSession) {
                        success = verification.rawWorkspaceData.is_object() &&
                            verification.rawWorkspaceData.value(
                                "embeddedRaw",
                                nlohmann::json::object())
                                .value("present", false);
                    }
                }
            } catch (...) {
                success = false;
            }

            Async::TaskSystem::Get().PostToMain([
                this,
                generation,
                destination,
                projectName,
                rawSession,
                sourceKey,
                document,
                success,
                onComplete = std::move(onComplete)
            ]() mutable {
                if (generation != m_ProjectFileSaveGeneration) {
                    if (onComplete) {
                        onComplete(false);
                    }
                    return;
                }

                if (!success) {
                    m_ProjectFileSaveTaskState = Async::TaskState::Failed;
                    m_ProjectFileSaveStatusText =
                        "Failed to save the project to the new location.";
                    MarkDirty();
                    QueueUiNotification(
                        UiNotificationSeverity::Error,
                        m_ProjectFileSaveStatusText,
                        "project-file-save-as");
                    if (onComplete) {
                        onComplete(false);
                    }
                    return;
                }

                if (rawSession) {
                    if (Stack::RawWorkspace::SourceRecord* source =
                            FindRawWorkspaceSourceByKey(sourceKey)) {
                        ApplyRawWorkspaceProjectInfoToSource(
                            *source,
                            *document,
                            destination,
                            {},
                            false,
                            false,
                            "save-as");
                    }
                    m_ActiveRawWorkspaceProjectPath = destination;
                    InvalidateRawWorkspaceGalleryPresentation();
                    PersistRawWorkspaceCatalog();
                }
                SetCurrentProjectName(projectName);
                SetCurrentProjectFileName(destination.string());
                ClearDirty();
                m_ProjectFileSaveTaskState = Async::TaskState::Idle;
                m_ProjectFileSaveStatusText =
                    "Project saved to the new location.";
                QueueUiNotification(
                    UiNotificationSeverity::Success,
                    m_ProjectFileSaveStatusText,
                    "project-file-save-as");
                if (onComplete) {
                    onComplete(true);
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
        QueueUiNotification(
            UiNotificationSeverity::Error,
            m_ProjectFileSaveStatusText,
            "project-file-save-as");
    }
    if (onComplete) {
        onComplete(false);
    }
    return false;
}

bool EditorModule::RelinkActiveRawWorkspaceProjectToSelectedSource() {
    Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
    if (!source || source->project.absolutePath.empty()) {
        return false;
    }

    BumpRawWorkspaceProjectSaveRevision(m_RawWorkspace.workspaceRoot, source->relativePathKey);
    StackBinaryFormat::ProjectDocument document;
    {
        std::lock_guard<std::mutex> fileLock(m_RawWorkspaceProjectFileWriteMutex);
        if (!StackBinaryFormat::ReadProjectFile(source->project.absolutePath, document)) {
            return false;
        }
        std::string error;
        if (!Stack::RawWorkspace::RelinkProjectDocumentToSource(*source, document, &error)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                error.empty() ? "Failed to relink RAW project." : error,
                "raw-workspace-project-relink");
            return false;
        }
        if (!StackBinaryFormat::WriteProjectFile(source->project.absolutePath, document)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "Failed to write the relinked RAW project.",
                "raw-workspace-project-relink");
            return false;
        }
    }

    ApplyRawWorkspaceProjectInfoToSource(
        *source,
        document,
        source->project.absolutePath,
        source->project.relativePath.empty()
            ? Stack::RawWorkspace::BuildProjectRelativePathForSource(*source)
            : source->project.relativePath,
        false,
        false,
        "relinked-selected-source");
    PersistRawWorkspaceCatalog();
    QueueUiNotification(
        UiNotificationSeverity::Success,
        "RAW project relinked.",
        "raw-workspace-project-relink");
    return true;
}

bool EditorModule::EmbedActiveRawWorkspaceProject() {
    Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
    if (!source || source->project.absolutePath.empty()) {
        return false;
    }

    BumpRawWorkspaceProjectSaveRevision(m_RawWorkspace.workspaceRoot, source->relativePathKey);
    StackBinaryFormat::ProjectDocument document;
    {
        std::lock_guard<std::mutex> fileLock(m_RawWorkspaceProjectFileWriteMutex);
        if (!StackBinaryFormat::ReadProjectFile(source->project.absolutePath, document)) {
            return false;
        }
        std::string error;
        if (!Stack::RawWorkspace::EmbedRawSourceInProjectDocument(*source, document, &error)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                error.empty() ? "Failed to embed RAW source." : error,
                "raw-workspace-project-embed");
            return false;
        }
        if (!StackBinaryFormat::WriteProjectFile(source->project.absolutePath, document)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "Failed to write the embedded RAW project.",
                "raw-workspace-project-embed");
            return false;
        }
    }

    ApplyRawWorkspaceProjectInfoToSource(
        *source,
        document,
        source->project.absolutePath,
        source->project.relativePath.empty()
            ? Stack::RawWorkspace::BuildProjectRelativePathForSource(*source)
            : source->project.relativePath,
        false,
        false,
        "embedded-selected-source");
    PersistRawWorkspaceCatalog();
    QueueUiNotification(
        UiNotificationSeverity::Success,
        "RAW source embedded in the selected project.",
        "raw-workspace-project-embed");
    return true;
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
    m_RawWorkspaceReplacementActionLabel = std::move(actionLabel);
    m_RawWorkspaceReplacementTargetLabel = std::move(targetLabel);
    m_RawWorkspaceReplacementDiscardOpenSourceKey =
        std::move(discardOpenSourceKey);
    m_PendingRawWorkspaceProjectReplacement = std::move(action);
    m_RawWorkspaceReplacementSavePending = false;
    m_RawWorkspaceReplacementExecuteAfterSave = false;
    m_ShowRawWorkspaceReplaceProjectPopup = true;
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
    const bool success =
        m_PendingRawWorkspaceProjectReplacement(&error);
    m_RawWorkspaceReplacementAuthorized = false;
    if (!success) {
        m_RawWorkspaceReplacementSkipSaveSourceKey.clear();
        QueueUiNotification(
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
    if (IsDeferredLoadedProjectApplyActive() ||
        IsRawWorkspaceProjectLoadBusy()) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
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

    if (!currentDispositionApproved && HasProjectContent() && m_Dirty) {
        QueueRawWorkspaceProjectReplacement(
            "open project",
            normalized.filename().string(),
            std::move(openAction));
        return true;
    }
    std::string error;
    const bool opened = openAction(&error);
    if (!opened && !error.empty()) {
        QueueUiNotification(
            UiNotificationSeverity::Error,
            error,
            "raw-workspace-project-open");
    }
    return opened;
}

void EditorModule::RenderRawWorkspaceLifecyclePopups() {
    if (m_RawWorkspaceReplacementExecuteAfterSave) {
        m_RawWorkspaceReplacementExecuteAfterSave = false;
        ExecutePendingRawWorkspaceProjectReplacement(false);
    }
    if (m_ShowRawWorkspaceCloseProjectPopup) {
        ImGui::OpenPopup("Close RAW Project##RawWorkspace");
        m_ShowRawWorkspaceCloseProjectPopup = false;
    }
    if (m_ShowRawWorkspaceRelinkPopup) {
        ImGui::OpenPopup("Relink RAW Project##RawWorkspace");
        m_ShowRawWorkspaceRelinkPopup = false;
    }
    if (m_ShowRawWorkspaceEmbedPopup) {
        ImGui::OpenPopup("Bake / Embed RAW##RawWorkspace");
        m_ShowRawWorkspaceEmbedPopup = false;
    }
    if (m_ShowRawWorkspaceReplaceProjectPopup) {
        ImGui::OpenPopup("Switch Editing Project##RawWorkspace");
        m_ShowRawWorkspaceReplaceProjectPopup = false;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(
            "Close RAW Project##RawWorkspace",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool busy =
            IsRawWorkspaceProjectLoadBusy() || IsRawWorkspaceProjectSaveBusy();
        ImGui::TextWrapped(
            "Close the current project and return to the RAW gallery? The folder, "
            "selection, and saved project on disk will remain available.");
        ImGui::Spacing();
        ImGui::BeginDisabled(busy);
        if (m_Dirty) {
            if (ImGui::Button("Save & Close", ImVec2(130.0f, 0.0f))) {
                if (SaveActiveRawWorkspaceProject(true) &&
                    CloseActiveRawWorkspaceProject(false)) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard & Close", ImVec2(140.0f, 0.0f))) {
                if (CloseActiveRawWorkspaceProject(true)) {
                    ImGui::CloseCurrentPopup();
                }
            }
        } else if (ImGui::Button("Close Project", ImVec2(130.0f, 0.0f))) {
            if (CloseActiveRawWorkspaceProject(false)) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
        }
        if (busy) {
            ImGui::TextDisabled(
                "Wait for the current project operation to finish before closing.");
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(
        viewport->GetCenter(),
        ImGuiCond_Appearing,
        ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(
            "Switch Editing Project##RawWorkspace",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!m_PendingRawWorkspaceProjectReplacement) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        } else {
            const std::string action =
                m_RawWorkspaceReplacementActionLabel.empty()
                ? "switch projects"
                : m_RawWorkspaceReplacementActionLabel;
            ImGui::TextWrapped(
                "The current project has unsaved changes. Save it before you %s, "
                "discard those changes, or cancel and keep working?",
                action.c_str());
            if (!m_RawWorkspaceReplacementTargetLabel.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled(
                    "Next: %s",
                    m_RawWorkspaceReplacementTargetLabel.c_str());
            }

            const bool busy =
                m_RawWorkspaceReplacementSavePending ||
                IsRawWorkspaceProjectSaveBusy() ||
                IsRawWorkspaceProjectLoadBusy();
            ImGui::Spacing();
            ImGui::BeginDisabled(busy);
            if (ImGui::Button("Save & Continue", ImVec2(140.0f, 0.0f))) {
                if (IsRawWorkspaceProjectActive()) {
                    if (SaveActiveRawWorkspaceProject(true) &&
                        ExecutePendingRawWorkspaceProjectReplacement(false)) {
                        ImGui::CloseCurrentPopup();
                    }
                } else {
                    m_RawWorkspaceReplacementSavePending = true;
                    RequestSaveCurrentProject(
                        {},
                        [this](bool success) {
                            m_RawWorkspaceReplacementSavePending = false;
                            if (success) {
                                m_RawWorkspaceReplacementExecuteAfterSave = true;
                            }
                        });
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard & Continue", ImVec2(150.0f, 0.0f))) {
                if (ExecutePendingRawWorkspaceProjectReplacement(true)) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) ||
                ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                m_RawWorkspaceReplacementSkipSaveSourceKey.clear();
                ClearPendingRawWorkspaceProjectReplacement();
                ImGui::CloseCurrentPopup();
            }
            if (busy) {
                ImGui::TextDisabled(
                    m_RawWorkspaceReplacementSavePending
                    ? "Saving the current project..."
                    : "Waiting for the current project operation...");
            }
            ImGui::EndPopup();
        }
    }

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Relink RAW Project##RawWorkspace", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const Stack::RawWorkspace::SourceRecord* source =
            FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
        ImGui::TextWrapped(
            "Relink this project to the selected RAW file. Stack will update the linked source metadata in the project file.");
        if (source != nullptr) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", source->relativePathKey.c_str());
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(source == nullptr || source->project.absolutePath.empty());
        if (ImGui::Button("Relink", ImVec2(120.0f, 0.0f))) {
            RelinkActiveRawWorkspaceProjectToSelectedSource();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Bake / Embed RAW##RawWorkspace", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const Stack::RawWorkspace::SourceRecord* source =
            FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
        ImGui::TextWrapped(
            "Linked projects stay smaller and depend on the original RAW file. Embedded projects are larger but keep a copy of this RAW inside the selected project.");
        if (source != nullptr) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", source->relativePathKey.c_str());
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(source == nullptr || source->project.absolutePath.empty());
        if (ImGui::Button("Embed In This Project", ImVec2(180.0f, 0.0f))) {
            EmbedActiveRawWorkspaceProject();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

