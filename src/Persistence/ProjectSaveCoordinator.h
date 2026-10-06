#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Stack::Project {

enum class ProjectSaveReason {
    Autosave,
    Explicit,
    SaveAs,
    SwitchProject,
    CloseProject,
    FirstEdit,
    Adoption
};

enum class ProjectSaveStatus {
    Saved,
    Conflict,
    Failed,
    Canceled
};

struct ProjectSaveResult {
    ProjectSaveStatus status = ProjectSaveStatus::Failed;
    std::string projectId;
    std::uint64_t persistedEditRevision = 0;
    std::uint64_t storageRevision = 0;
    std::string path;
    std::string message;

    explicit operator bool() const {
        return status == ProjectSaveStatus::Saved;
    }
};

// Pure timing policy used by the editor frame loop and regression tests. A
// periodic save must satisfy both the post-edit quiet window and the minimum
// interval since the prior save. A negative lastAutosaveSeconds value means
// this project has not been autosaved yet. The coordinator itself remains
// concerned only with revision ordering/writes.
bool IsProjectAutosaveDue(
    bool dirty,
    bool hasSaveTarget,
    bool saveBusy,
    double nowSeconds,
    double lastEditSeconds,
    double lastAutosaveSeconds,
    double quietSeconds = 10.0,
    double minimumIntervalSeconds = 60.0);

// A save that becomes due while the user is beginning another interaction
// should wait for the next genuinely idle frame. ImGui does not mark a newly
// clicked control active until that control has been submitted, while the
// shared persistence pump runs before the editing surfaces. Pointer-down and
// wheel input therefore need to participate independently from item activity.
bool ShouldDeferProjectAutosaveForForegroundInput(
    bool localTargetDragActive,
    bool anyItemActive,
    bool anyPointerButtonDown,
    float mouseWheel,
    float mouseWheelHorizontal);

// Serializes every save request for the active document. The coordinator does
// not own a thread pool: each request supplies an asynchronous starter so the
// Editor can use the appropriate immutable snapshot/write implementation.
// Requests received while a write is active are coalesced to the newest edit
// revision, while callbacks for explicit flushes are retained.
class ProjectSaveCoordinator {
public:
    using Completion = std::function<void(ProjectSaveResult)>;
    using Start = std::function<void(Completion)>;

    struct Request {
        std::string projectId;
        std::uint64_t editRevision = 0;
        ProjectSaveReason reason = ProjectSaveReason::Autosave;
        Start start;
        Completion completion;
    };

    bool Enqueue(Request request);
    void Reset(std::string projectId = {}, std::uint64_t persistedEditRevision = 0);
    void Cancel(const std::string& message = "Project save canceled.");

    bool IsBusy() const { return m_Active.has_value(); }
    bool HasPendingSave() const { return m_Pending.has_value(); }
    const std::string& ProjectId() const { return m_ProjectId; }
    std::uint64_t PersistedEditRevision() const {
        return m_PersistedEditRevision;
    }

private:
    struct Waiter {
        std::uint64_t editRevision = 0;
        Completion completion;
    };

    void StartRequest(Request request);
    void CompleteActive(ProjectSaveResult result);
    void CompleteSatisfiedWaiters(const ProjectSaveResult& result);
    void CompleteFailedWaiters(
        std::uint64_t throughRevision,
        const ProjectSaveResult& result);

    std::string m_ProjectId;
    std::uint64_t m_PersistedEditRevision = 0;
    std::uint64_t m_Generation = 0;
    std::optional<Request> m_Active;
    std::optional<Request> m_Pending;
    std::vector<Waiter> m_Waiters;
};

} // namespace Stack::Project
