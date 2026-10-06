#include "Persistence/ProjectSaveCoordinator.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Stack::Project {

bool IsProjectAutosaveDue(
    bool dirty,
    bool hasSaveTarget,
    bool saveBusy,
    double nowSeconds,
    double lastEditSeconds,
    double lastAutosaveSeconds,
    double quietSeconds,
    double minimumIntervalSeconds) {
    const bool cadenceElapsed = lastAutosaveSeconds < 0.0 ||
        (nowSeconds >= lastAutosaveSeconds &&
         nowSeconds - lastAutosaveSeconds >= minimumIntervalSeconds);
    return dirty &&
        hasSaveTarget &&
        !saveBusy &&
        std::isfinite(nowSeconds) &&
        std::isfinite(lastEditSeconds) &&
        std::isfinite(lastAutosaveSeconds) &&
        quietSeconds >= 0.0 &&
        minimumIntervalSeconds >= 0.0 &&
        nowSeconds >= lastEditSeconds &&
        nowSeconds - lastEditSeconds >= quietSeconds &&
        cadenceElapsed &&
        lastAutosaveSeconds < lastEditSeconds;
}

bool ShouldDeferProjectAutosaveForForegroundInput(
    bool localTargetDragActive,
    bool anyItemActive,
    bool anyPointerButtonDown,
    float mouseWheel,
    float mouseWheelHorizontal) {
    return localTargetDragActive ||
        anyItemActive ||
        anyPointerButtonDown ||
        std::abs(mouseWheel) > 0.0001f ||
        std::abs(mouseWheelHorizontal) > 0.0001f;
}

bool ProjectSaveCoordinator::Enqueue(Request request) {
    if (request.projectId.empty() || !request.start) {
        if (request.completion) {
            ProjectSaveResult result;
            result.status = ProjectSaveStatus::Failed;
            result.projectId = request.projectId;
            result.message = "The project save request was incomplete.";
            request.completion(std::move(result));
        }
        return false;
    }

    if (m_ProjectId.empty()) {
        m_ProjectId = request.projectId;
    } else if (m_ProjectId != request.projectId) {
        if (request.completion) {
            ProjectSaveResult result;
            result.status = ProjectSaveStatus::Canceled;
            result.projectId = request.projectId;
            result.message = "The active project changed before the save could start.";
            request.completion(std::move(result));
        }
        return false;
    }

    if (request.completion) {
        m_Waiters.push_back({ request.editRevision, request.completion });
        request.completion = {};
    }

    if (!m_Active &&
        request.editRevision <= m_PersistedEditRevision &&
        request.reason == ProjectSaveReason::Autosave) {
        ProjectSaveResult result;
        result.status = ProjectSaveStatus::Saved;
        result.projectId = m_ProjectId;
        result.persistedEditRevision = m_PersistedEditRevision;
        CompleteSatisfiedWaiters(result);
        return true;
    }

    if (m_Active) {
        const bool replacePending = !m_Pending ||
            request.editRevision >= m_Pending->editRevision ||
            request.reason != ProjectSaveReason::Autosave;
        if (replacePending) {
            m_Pending = std::move(request);
        }
        return true;
    }

    StartRequest(std::move(request));
    return true;
}

void ProjectSaveCoordinator::Reset(
    std::string projectId,
    std::uint64_t persistedEditRevision) {
    Cancel("The active project changed.");
    m_ProjectId = std::move(projectId);
    m_PersistedEditRevision = persistedEditRevision;
}

void ProjectSaveCoordinator::Cancel(const std::string& message) {
    ++m_Generation;
    ProjectSaveResult result;
    result.status = ProjectSaveStatus::Canceled;
    result.projectId = m_ProjectId;
    result.persistedEditRevision = m_PersistedEditRevision;
    result.message = message;
    std::vector<Waiter> waiters = std::move(m_Waiters);
    m_Waiters.clear();
    m_Active.reset();
    m_Pending.reset();
    for (Waiter& waiter : waiters) {
        if (waiter.completion) {
            waiter.completion(result);
        }
    }
}

void ProjectSaveCoordinator::StartRequest(Request request) {
    m_Active = std::move(request);
    const std::uint64_t generation = ++m_Generation;
    try {
        Start starter = m_Active->start;
        starter([this, generation](ProjectSaveResult result) {
            if (generation != m_Generation) {
                return;
            }
            CompleteActive(std::move(result));
        });
    } catch (...) {
        ProjectSaveResult result;
        result.status = ProjectSaveStatus::Failed;
        result.projectId = m_ProjectId;
        result.message = "The project save failed before its background work started.";
        CompleteActive(std::move(result));
    }
}

void ProjectSaveCoordinator::CompleteActive(ProjectSaveResult result) {
    if (!m_Active) {
        return;
    }
    const std::uint64_t activeRevision = m_Active->editRevision;
    if (result.projectId.empty()) {
        result.projectId = m_ProjectId;
    }
    if (result.persistedEditRevision == 0 && result) {
        result.persistedEditRevision = activeRevision;
    }
    m_Active.reset();

    if (result) {
        m_PersistedEditRevision = std::max(
            m_PersistedEditRevision,
            result.persistedEditRevision);
        CompleteSatisfiedWaiters(result);
    } else {
        CompleteFailedWaiters(activeRevision, result);
    }

    if (m_Pending) {
        Request pending = std::move(*m_Pending);
        m_Pending.reset();
        if (result &&
            pending.editRevision <= m_PersistedEditRevision) {
            CompleteSatisfiedWaiters(result);
        } else {
            StartRequest(std::move(pending));
        }
    }
}

void ProjectSaveCoordinator::CompleteSatisfiedWaiters(
    const ProjectSaveResult& result) {
    std::vector<Completion> completions;
    std::vector<Waiter> remaining;
    remaining.reserve(m_Waiters.size());
    for (Waiter& waiter : m_Waiters) {
        if (waiter.editRevision <= m_PersistedEditRevision) {
            if (waiter.completion) {
                completions.push_back(std::move(waiter.completion));
            }
        } else {
            remaining.push_back(std::move(waiter));
        }
    }
    m_Waiters = std::move(remaining);
    for (Completion& completion : completions) {
        ProjectSaveResult delivered = result;
        delivered.status = ProjectSaveStatus::Saved;
        delivered.persistedEditRevision = m_PersistedEditRevision;
        completion(std::move(delivered));
    }
}

void ProjectSaveCoordinator::CompleteFailedWaiters(
    std::uint64_t throughRevision,
    const ProjectSaveResult& result) {
    std::vector<Completion> completions;
    std::vector<Waiter> remaining;
    remaining.reserve(m_Waiters.size());
    for (Waiter& waiter : m_Waiters) {
        if (waiter.editRevision <= throughRevision) {
            if (waiter.completion) {
                completions.push_back(std::move(waiter.completion));
            }
        } else {
            remaining.push_back(std::move(waiter));
        }
    }
    m_Waiters = std::move(remaining);
    for (Completion& completion : completions) {
        completion(result);
    }
}

} // namespace Stack::Project
