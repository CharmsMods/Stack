#pragma once

#include <cstdint>
#include <string>

namespace Stack::Project {

enum class ProjectLifecyclePhase {
    Empty,
    Loading,
    ReadyClean,
    ReadyDirty,
    Importing,
    Saving,
    SaveFailed,
    Conflict,
    ReadOnlyRecovery
};

struct ProjectReplacementToken {
    std::uint64_t generation = 0;
    explicit operator bool() const { return generation != 0; }
};

struct ProjectSaveToken {
    std::string projectId;
    std::uint64_t generation = 0;
    std::uint64_t snapshotDirtyRevision = 0;
    std::uint64_t expectedStorageRevision = 0;
    explicit operator bool() const { return generation != 0 && !projectId.empty(); }
};

class ProjectSessionController {
public:
    ProjectLifecyclePhase Phase() const { return m_Phase; }
    const std::string& ProjectId() const { return m_ProjectId; }
    std::uint64_t DirtyRevision() const { return m_DirtyRevision; }
    std::uint64_t PersistedDirtyRevision() const { return m_PersistedDirtyRevision; }
    std::uint64_t StorageRevision() const { return m_StorageRevision; }
    bool IsDirty() const { return m_DirtyRevision != m_PersistedDirtyRevision; }

    ProjectReplacementToken BeginReplacement();
    bool CompleteReplacement(
        const ProjectReplacementToken& token,
        std::string projectId,
        std::uint64_t dirtyRevision,
        std::uint64_t storageRevision,
        bool readOnlyRecovery = false);
    void FailReplacement(const ProjectReplacementToken& token);
    void Clear();

    std::uint64_t NoteEdit();
    bool BeginImport();
    void CompleteImport(bool published);

    ProjectSaveToken BeginSave();
    bool CompleteSave(
        const ProjectSaveToken& token,
        bool success,
        std::uint64_t committedStorageRevision,
        bool conflict = false);
    void CancelSave(const ProjectSaveToken& token);
    void MarkConflict();
    void MarkReadOnlyRecovery();

private:
    bool IsCurrentSave(const ProjectSaveToken& token) const;
    void UpdateReadyPhase();

    ProjectLifecyclePhase m_Phase = ProjectLifecyclePhase::Empty;
    std::string m_ProjectId;
    std::uint64_t m_DirtyRevision = 0;
    std::uint64_t m_PersistedDirtyRevision = 0;
    std::uint64_t m_StorageRevision = 0;
    std::uint64_t m_ReplacementGeneration = 0;
    std::uint64_t m_SaveGeneration = 0;
    std::uint64_t m_ActiveSaveGeneration = 0;
    std::uint64_t m_ImportStartingDirtyRevision = 0;
    ProjectLifecyclePhase m_PhaseBeforeReplacement = ProjectLifecyclePhase::Empty;
};

} // namespace Stack::Project
