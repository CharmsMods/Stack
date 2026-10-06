#include "Persistence/ProjectSessionController.h"

#include <utility>

namespace Stack::Project {

ProjectReplacementToken ProjectSessionController::BeginReplacement() {
    if (m_Phase != ProjectLifecyclePhase::Loading) {
        m_PhaseBeforeReplacement = m_Phase;
    }
    m_Phase = ProjectLifecyclePhase::Loading;
    return { ++m_ReplacementGeneration };
}

bool ProjectSessionController::CompleteReplacement(
    const ProjectReplacementToken& token,
    std::string projectId,
    std::uint64_t dirtyRevision,
    std::uint64_t storageRevision,
    bool readOnlyRecovery) {
    if (!token || token.generation != m_ReplacementGeneration || projectId.empty()) {
        return false;
    }
    m_ProjectId = std::move(projectId);
    m_DirtyRevision = dirtyRevision;
    m_PersistedDirtyRevision = dirtyRevision;
    m_StorageRevision = storageRevision;
    m_ActiveSaveGeneration = 0;
    m_Phase = readOnlyRecovery
        ? ProjectLifecyclePhase::ReadOnlyRecovery
        : ProjectLifecyclePhase::ReadyClean;
    return true;
}

void ProjectSessionController::FailReplacement(const ProjectReplacementToken& token) {
    if (!token || token.generation != m_ReplacementGeneration) return;
    m_Phase = m_PhaseBeforeReplacement;
}

void ProjectSessionController::Clear() {
    ++m_ReplacementGeneration;
    ++m_SaveGeneration;
    m_ActiveSaveGeneration = 0;
    m_ProjectId.clear();
    m_DirtyRevision = 0;
    m_PersistedDirtyRevision = 0;
    m_StorageRevision = 0;
    m_PhaseBeforeReplacement = ProjectLifecyclePhase::Empty;
    m_Phase = ProjectLifecyclePhase::Empty;
}

bool ProjectSessionController::AdoptSavedBaseline(
    std::string projectId,
    std::uint64_t persistedDirtyRevision,
    std::uint64_t currentDirtyRevision,
    std::uint64_t storageRevision) {
    if (projectId.empty() || storageRevision == 0 ||
        currentDirtyRevision < persistedDirtyRevision ||
        (!m_ProjectId.empty() && m_ProjectId != projectId)) return false;
    ++m_ReplacementGeneration;
    ++m_SaveGeneration;
    m_ActiveSaveGeneration = 0;
    m_ProjectId = std::move(projectId);
    m_PersistedDirtyRevision = persistedDirtyRevision;
    m_DirtyRevision = currentDirtyRevision;
    m_StorageRevision = storageRevision;
    UpdateReadyPhase();
    return true;
}

std::uint64_t ProjectSessionController::NoteEdit() {
    if (m_ProjectId.empty()) return m_DirtyRevision;
    ++m_DirtyRevision;
    if (m_Phase == ProjectLifecyclePhase::ReadyClean ||
        m_Phase == ProjectLifecyclePhase::ReadyDirty ||
        m_Phase == ProjectLifecyclePhase::SaveFailed) {
        m_Phase = ProjectLifecyclePhase::ReadyDirty;
    }
    return m_DirtyRevision;
}

bool ProjectSessionController::BeginImport() {
    if (m_ProjectId.empty() ||
        m_Phase == ProjectLifecyclePhase::Loading ||
        m_Phase == ProjectLifecyclePhase::Saving ||
        m_Phase == ProjectLifecyclePhase::Conflict ||
        m_Phase == ProjectLifecyclePhase::ReadOnlyRecovery) {
        return false;
    }
    m_ImportStartingDirtyRevision = m_DirtyRevision;
    m_Phase = ProjectLifecyclePhase::Importing;
    return true;
}

void ProjectSessionController::CompleteImport(bool published) {
    if (m_Phase != ProjectLifecyclePhase::Importing) return;
    if (!published) m_DirtyRevision = m_ImportStartingDirtyRevision;
    UpdateReadyPhase();
}

ProjectSaveToken ProjectSessionController::BeginSave() {
    if (m_ProjectId.empty() ||
        m_Phase == ProjectLifecyclePhase::Loading ||
        m_Phase == ProjectLifecyclePhase::Importing ||
        m_Phase == ProjectLifecyclePhase::Conflict ||
        m_Phase == ProjectLifecyclePhase::ReadOnlyRecovery) {
        return {};
    }
    ProjectSaveToken token {
        m_ProjectId,
        ++m_SaveGeneration,
        m_DirtyRevision,
        m_StorageRevision
    };
    m_ActiveSaveGeneration = token.generation;
    m_Phase = ProjectLifecyclePhase::Saving;
    return token;
}

bool ProjectSessionController::CompleteSave(
    const ProjectSaveToken& token,
    bool success,
    std::uint64_t committedStorageRevision,
    bool conflict) {
    if (!IsCurrentSave(token)) return false;
    m_ActiveSaveGeneration = 0;
    if (conflict) {
        m_Phase = ProjectLifecyclePhase::Conflict;
        return true;
    }
    if (!success) {
        m_Phase = ProjectLifecyclePhase::SaveFailed;
        return true;
    }
    m_StorageRevision = committedStorageRevision;
    // A save may finish after newer edits. It persists only the dirty revision
    // captured by its token and therefore cannot incorrectly clear them.
    if (token.snapshotDirtyRevision > m_PersistedDirtyRevision) {
        m_PersistedDirtyRevision = token.snapshotDirtyRevision;
    }
    UpdateReadyPhase();
    return true;
}

void ProjectSessionController::CancelSave(const ProjectSaveToken& token) {
    if (!IsCurrentSave(token)) return;
    m_ActiveSaveGeneration = 0;
    UpdateReadyPhase();
}

bool ProjectSessionController::RecoverOrphanedSave() {
    if (m_Phase != ProjectLifecyclePhase::Saving) {
        return false;
    }
    ++m_SaveGeneration;
    m_ActiveSaveGeneration = 0;
    UpdateReadyPhase();
    return true;
}

void ProjectSessionController::MarkConflict() {
    if (!m_ProjectId.empty()) m_Phase = ProjectLifecyclePhase::Conflict;
}

void ProjectSessionController::MarkReadOnlyRecovery() {
    if (!m_ProjectId.empty()) m_Phase = ProjectLifecyclePhase::ReadOnlyRecovery;
}

bool ProjectSessionController::IsCurrentSave(const ProjectSaveToken& token) const {
    return token && token.projectId == m_ProjectId &&
        token.generation == m_ActiveSaveGeneration;
}

void ProjectSessionController::UpdateReadyPhase() {
    if (m_ProjectId.empty()) {
        m_Phase = ProjectLifecyclePhase::Empty;
    } else {
        m_Phase = IsDirty()
            ? ProjectLifecyclePhase::ReadyDirty
            : ProjectLifecyclePhase::ReadyClean;
    }
}

} // namespace Stack::Project
