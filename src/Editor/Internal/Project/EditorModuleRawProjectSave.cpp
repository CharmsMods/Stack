#include "Raw/RawGraphOperation.h"
#include "Editor/EditorModule.h"
#include "Editor/Internal/Project/ProjectLifecycleSupport.h"

#include "App/AppPaths.h"
#include "Async/TaskSystem.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Library/LibraryManager.h"
#include "Persistence/ProjectIndex.h"
#include "Raw/RawLoader.h"
#include "Raw/RawTechnicalEvidence.h"

#include <imgui.h>

#include <algorithm>
#include <exception>
#include <filesystem>

#include <memory>
#include <string>
#include <utility>
#include <vector>

using Stack::Editor::ProjectInternal::ApplyRawWorkspaceProjectInfoToSource;
using Stack::Editor::ProjectInternal::EnsureMinimalProjectDocument;
using Stack::Editor::ProjectInternal::OverlayOwnedJsonFields;
using Stack::Editor::ProjectInternal::RawWorkspaceProjectFileTimeTicks;

bool EditorModule::SaveActiveRawWorkspaceProject(
    bool explicitSave,
    bool synchronousAutosave) {
    if (!m_DocumentPersistenceEnabled) {
        return false;
    }
    if (m_Project->rawInteractionDraft.active &&
        !ResolveRawWorkspaceInteractionDraft(false)) {
        return false;
    }
    if (!IsRawWorkspaceProjectActive()) {
        return false;
    }
    if (!IsUnifiedProjectStoreActive() ||
        !m_Project->snapshot ||
        !m_Project->store) {
        PostNotification(
            UiNotificationSeverity::Error,
            "The current project store is unavailable.",
            "raw-workspace-project-save");
        return false;
    }

    if (!synchronousAutosave) {
        if (!explicitSave) {
            // Ordinary edits are already dirty in memory. The shared
            // debounce owns when their autosave is enqueued.
            return true;
        }
        return EnqueueCurrentProjectSave(
            Stack::Project::ProjectSaveReason::Explicit,
            m_Project->name);
    }
    if (m_Project->saves.IsBusy()) {
        return false;
    }

    std::string error;
    Stack::Project::RawProjectSnapshot snapshot =
        *m_Project->snapshot;
    if (!IsMultiFrameRawProjectActive()) {
        if (!snapshot.rawWorkspaceData.is_object()) {
            snapshot.rawWorkspaceData = nlohmann::json::object();
        }
        snapshot.rawWorkspaceData["rawRecipe"] =
            Stack::RawRecipe::SerializeWorkspaceSourceRecipe(
                m_Project->rawRecipe);
        snapshot.rawWorkspaceData["rawWorkspaceMode"] =
            Stack::RawWorkspace::RawProjectModeToString(
                m_Project->rawMode);
        StackBinaryFormat::ProjectDocument modeDocument;
        modeDocument.rawWorkspaceData = snapshot.rawWorkspaceData;
        ApplyActiveRawWorkspaceModeDataToDocument(modeDocument);
        snapshot.rawWorkspaceData =
            std::move(modeDocument.rawWorkspaceData);
    }
    const Stack::Project::ProjectStoreTransaction transaction =
        m_Project->store->BeginTransaction(
            snapshot.persistedStorageRevision);
    const bool saved = transaction && CommitActiveMultiFrameMutation(
        std::move(snapshot),
        m_Project->graph,
        transaction,
        false,
        &error,
        false);
    if (saved) {
        if (Stack::RawWorkspace::SourceRecord* source =
                FindRawWorkspaceSourceByKey(
                    m_Project->rawSourceKey)) {
            source->project.autosaved = !explicitSave;
            source->project.dirty = false;
        }
        PersistRawWorkspaceCatalog();
        if (explicitSave) {
            RescanRawWorkspace();
            LibraryManager::Get().RequestRefreshLibraryAsync();
            PostNotification(UiNotificationSeverity::Success,
                "Project saved.", "raw-workspace-project-save");
        }
        m_Project->lastAutosaveTime = ImGui::GetCurrentContext()
            ? ImGui::GetTime()
            : 0.0;
    } else {
        PostNotification(
            UiNotificationSeverity::Error,
            error.empty()
                ? "Failed to save the RAW project."
                : error,
            "raw-workspace-project-save");
    }
    return saved;
}

bool EditorModule::SaveActiveRawWorkspaceProjectIfDirty() {
    if (!IsRawWorkspaceProjectActive() || !IsDirty()) {
        return true;
    }
    return SaveActiveRawWorkspaceProject(false);
}

bool EditorModule::FlushActiveRawWorkspaceProjectIfDirty() {
    if (!IsRawWorkspaceProjectActive() || !IsDirty()) {
        return true;
    }
    return SaveActiveRawWorkspaceProject(false, true);
}

std::string EditorModule::EnsureProjectDocumentId() {
    return m_Project->EnsureDocumentId();
}
