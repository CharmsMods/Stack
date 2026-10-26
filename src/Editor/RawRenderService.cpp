#include "Editor/RawRenderService.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"
#include "Editor/Internal/EditorRenderResultRetirement.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Stack::EditorRendering {

using Internal::RenderResultRetirement;
using Internal::RenderResultResourceGuard;

RawRenderService& RawRenderService::Get() {
    static RawRenderService service;
    return service;
}

RawRenderService::ClientId RawRenderService::Acquire(
    GLFWwindow* sharedWindow) {
    {
        // Adding a client to the live worker must not wait for another
        // project's queued or running compute task.
        std::shared_lock<std::shared_mutex> lifetime(m_WorkerLifetimeMutex);
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Initialized) {
            const ClientId clientId = m_NextClientId++;
            m_Clients.insert(clientId);
            return clientId;
        }
    }
    std::unique_lock<std::shared_mutex> lifetime(m_WorkerLifetimeMutex);
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_Initialized) {
        if (sharedWindow == nullptr || !m_Worker.Initialize(sharedWindow)) {
            return 0;
        }
        m_Initialized = true;
    }
    const ClientId clientId = m_NextClientId++;
    m_Clients.insert(clientId);
    return clientId;
}

void RawRenderService::Release(ClientId clientId) {
    if (clientId == 0) return;
    std::shared_lock<std::shared_mutex> lifetime(m_WorkerLifetimeMutex);
    bool shutdown = false;
    {
        RenderResultRetirement retired;
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Clients.erase(clientId) == 0) return;
        m_Sessions.erase(clientId);
        m_Pending.erase(clientId);
        const auto completed = m_Completed.find(clientId);
        if (completed != m_Completed.end()) {
            retired.results.splice(retired.results.end(), completed->second);
            m_Completed.erase(completed);
        }
        const auto timings = m_Timings.find(clientId);
        if (timings != m_Timings.end()) {
            retired.results.splice(retired.results.end(), timings->second);
            m_Timings.erase(timings);
        }
        if (m_ActiveOwner == clientId) {
            m_ActiveOwner = 0;
            m_ActiveSnapshot = {};
        }
        shutdown = m_Clients.empty() && m_Initialized;
    }
    m_Worker.ReleaseOwner(clientId);
    if (!shutdown) return;

    lifetime.unlock();
    std::unique_lock<std::shared_mutex> shutdownLifetime(m_WorkerLifetimeMutex);
    {
        // A new client may have arrived while the lifetime lock changed.
        // Only the final client may stop the shared worker.
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_Clients.empty() || !m_Initialized) return;
        m_Initialized = false;
    }
    m_Worker.RequestStopForShutdown();
    m_Worker.Shutdown();
    RenderResultRetirement retired;
    std::lock_guard<std::mutex> lock(m_Mutex);
    for (auto* completed : {&m_Completed, &m_Timings}) {
        for (auto& [owner, results] : *completed) {
            (void)owner;
            retired.results.splice(retired.results.end(), results);
        }
        completed->clear();
    }
    m_Pending.clear();
    m_Sessions.clear();
    m_ActiveOwner = 0;
    m_ActiveSnapshot = {};
}

bool RawRenderService::ConfigureSession(
    ClientId clientId,
    RawRenderSessionConfig config) {
    if (clientId == 0 || !config.snapshotTemplate ||
        config.sourceIdentity.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_Initialized || m_Clients.find(clientId) == m_Clients.end()) {
        return false;
    }
    m_Sessions[clientId] = std::move(config);
    return true;
}

void RawRenderService::ClearSession(ClientId clientId) {
    if (clientId == 0) return;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Sessions.erase(clientId);
        m_Pending.erase(clientId);
        if (m_ActiveOwner == clientId) {
            m_ActiveOwner = 0;
            m_ActiveSnapshot = {};
        }
    }
    m_Worker.CancelOwnerSnapshots(clientId);
}

void RawRenderService::CancelPurpose(ClientId clientId, RawRenderPurpose purpose) {
    if (clientId == 0) return;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto pending = m_Pending.find(clientId);
        if (pending != m_Pending.end() &&
            pending->second.rawRenderPurpose ==
                purpose) {
            QueueSyntheticSuperseded(pending->second, false, false, RawPreviewSkipReason::Canceled);
            m_Pending.erase(pending);
        }
        if (m_ActiveOwner == clientId &&
            m_ActiveSnapshot.rawRenderPurpose ==
                purpose) {
            QueueSyntheticSuperseded(m_ActiveSnapshot, true, false, RawPreviewSkipReason::Canceled);
            m_ActiveOwner = 0;
            m_ActiveSnapshot = {};
        }
    }
    m_Worker.CancelOwnerSnapshots(clientId, purpose);
}

bool RawRenderService::SubmitCommand(
    ClientId clientId,
    RawRenderCommand command) {
    EditorRenderWorker::Snapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto found = m_Sessions.find(clientId);
        if (!m_Initialized || found == m_Sessions.end() ||
            !found->second.snapshotTemplate ||
            found->second.sourceIdentity != command.rawWorkspace.sourceKey ||
            found->second.sourceHash != command.rawWorkspace.sourceHash) {
            return false;
        }
        snapshot.rawSessionTemplate = found->second.snapshotTemplate;
        snapshot.rawSessionContractRevision =
            found->second.processingContractRevision;
    }
    snapshot.generation = command.generation;
    snapshot.schedulingSerial = command.cancellationToken;
    snapshot.lastAcceptedGeneration = command.lastAcceptedGeneration;
    snapshot.rawRenderPurpose = command.purpose;
    snapshot.previewMaxDimension = command.targetEdge;
    snapshot.telemetry = command.telemetry;
    snapshot.rawWorkspace = std::move(command.rawWorkspace);
    snapshot.viewportPreview = command.viewportPreview;
    snapshot.viewportViewId = command.viewportViewId;
    snapshot.viewportExecutionIdentity = command.viewportExecutionIdentity;
    snapshot.viewportContentIdentity = command.viewportContentIdentity;
    snapshot.prepareViewportOverview = command.prepareViewportOverview;
    snapshot.viewportTiling = command.viewportTiling;
    snapshot.previews = std::move(command.previews);
    snapshot.rawSessionCommand = true;
    return Submit(clientId, std::move(snapshot));
}

void RawRenderService::QueueSyntheticSuperseded(
    const EditorRenderWorker::Snapshot& snapshot,
    bool workerStarted, bool overload, RawPreviewSkipReason reason) {
    if (snapshot.graphRequest.enabled || snapshot.ownerId == 0 ||
        m_Clients.find(snapshot.ownerId) == m_Clients.end()) {
        return;
    }
    EditorRenderWorker::Result result;
    result.ownerId = snapshot.ownerId;
    result.generation = snapshot.generation;
    result.graphRequest = snapshot.graphRequest;
    result.lastAcceptedGeneration = snapshot.lastAcceptedGeneration;
    result.rawRenderPurpose = snapshot.rawRenderPurpose;
    result.telemetry = snapshot.telemetry;
    result.telemetry.superseded = true;
    result.telemetry.workerStarted = workerStarted;
    result.telemetry.overloadSkipped = overload;
    result.telemetry.skipReason = reason != RawPreviewSkipReason::None ? reason : overload ? RawPreviewSkipReason::OverloadDeadline :
        workerStarted ? RawPreviewSkipReason::PriorityChange : RawPreviewSkipReason::InputCoalesced;
    result.previewMaxDimension = snapshot.previewMaxDimension;
    result.viewportPreview = snapshot.viewportPreview;
    result.viewportViewId = snapshot.viewportViewId;
    result.viewportExecutionIdentity = snapshot.viewportExecutionIdentity;
    result.viewportContentIdentity = snapshot.viewportContentIdentity;
    result.rawWorkspace.sourceKey = snapshot.rawWorkspace.sourceKey;
    result.rawWorkspace.sourceHash = snapshot.rawWorkspace.sourceHash;
    result.rawWorkspace.recipeRevision =
        snapshot.rawWorkspace.recipeRevision;
    result.rawWorkspace.cachePrewarmStage =
        snapshot.rawWorkspace.cachePrewarmStage;
    result.rawWorkspace.cachePrewarmFingerprint =
        snapshot.rawWorkspace.cachePrewarmFingerprint;
    result.error = "Render superseded by a newer snapshot.";
    m_Completed[snapshot.ownerId].push_back(std::move(result));
}

void RawRenderService::Pump() {
    RenderResultRetirement completed, retired;
    EditorRenderWorker::Result result;
    RenderResultResourceGuard resultResources(result);
    while (m_Worker.TryConsumeCompleted(result)) {
        completed.results.push_back(std::move(result));
        result.outputTiles = {};
    }
    while (m_Worker.TryConsumeViewportTiming(result)) {
        completed.results.push_back(std::move(result));
        result.outputTiles = {};
    }
    std::vector<EditorRenderWorker::Snapshot> readyDenoise;
    EditorRenderWorker::Snapshot continuation;
    while (m_Worker.TryConsumeReadyDenoiseSnapshot(continuation)) {
        readyDenoise.push_back(std::move(continuation));
    }

    EditorRenderWorker::Snapshot next;
    ClientId nextOwner = 0;
    std::string workerError;
    const bool workerFailed = m_Worker.GetThreadFailure(&workerError);
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        for (auto it = completed.results.begin(); it != completed.results.end();) {
            const auto current = it++;
            auto& value = *current;
            if (m_Clients.find(value.ownerId) == m_Clients.end()) continue;
            if (value.timingOnly) {
                auto& timings = m_Timings[value.ownerId];
                if (timings.size() >= 32) retired.Take(timings, timings.begin());
                timings.splice(timings.end(), completed.results, current);
                continue;
            }
            auto& queue = m_Completed[value.ownerId];
            if (value.graphRequest.enabled && !workerFailed) {
                for (auto stale = queue.begin(); stale != queue.end();) {
                    if (!stale->graphRequest.enabled) { ++stale; continue; }
                    retired.Take(queue, stale++);
                }
            }
            queue.splice(queue.end(), completed.results, current);
        }
        for (auto& ready : readyDenoise) {
            if (m_Clients.find(ready.ownerId) == m_Clients.end()) continue;
            if (ready.rawSessionCommand) {
                const auto session = m_Sessions.find(ready.ownerId);
                if (session == m_Sessions.end() ||
                    session->second.sourceIdentity != ready.rawWorkspace.sourceKey ||
                    session->second.sourceHash != ready.rawWorkspace.sourceHash ||
                    session->second.processingContractRevision != ready.rawSessionContractRevision) {
                    m_Worker.InvalidateOwnerSnapshotsBefore(ready.ownerId, ready.generation + 1);
                    continue;
                }
            }
            const auto pending = m_Pending.find(ready.ownerId);
            // A newer edit will consume the same owner's completed job. Its
            // latest recipe takes precedence over the parked snapshot.
            if (pending != m_Pending.end() && pending->second.generation >= ready.generation) continue;
            ready.telemetry.queuedAt = std::chrono::steady_clock::now();
            m_Pending[ready.ownerId] = std::move(ready);
        }
        if (workerFailed) {
            // The worker reports accepted active/parked requests itself. The
            // service owns requests that never reached it, and must finish
            // those for their original project rather than call them canceled.
            for (const auto& [owner, pending] : m_Pending)
                QueueWorkerFailure(pending, workerError);
            m_Pending.clear();
            m_ActiveOwner = 0;
            m_ActiveSnapshot = {};
            return;
        }
        // A compute batch owns the GL context, not the next image slot.
        // Admit pending foreground images now so the worker selects them at
        // its next safe boundary rather than waiting for all burst batches.
        if (!m_Worker.HasPendingSnapshotOrRender()) {
            m_ActiveOwner = 0;
            m_ActiveSnapshot = {};
            auto selected = m_Pending.end();
            for (auto it = m_Pending.begin(); it != m_Pending.end(); ++it) {
                const int candidatePriority =
                    RawRenderPurposePriority(it->second.rawRenderPurpose);
                const int selectedPriority = selected == m_Pending.end()
                    ? std::numeric_limits<int>::max()
                    : RawRenderPurposePriority(
                        selected->second.rawRenderPurpose);
                if (selected == m_Pending.end() ||
                    candidatePriority < selectedPriority ||
                    (candidatePriority == selectedPriority &&
                     it->second.generation > selected->second.generation)) {
                    selected = it;
                }
            }
            if (selected != m_Pending.end()) {
                nextOwner = selected->first;
                next = std::move(selected->second);
                m_Pending.erase(selected);
                m_ActiveOwner = nextOwner;
                m_ActiveSnapshot = next;
            }
        }
    }
    if (nextOwner != 0 && !m_Worker.Submit(std::move(next))) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Worker.GetThreadFailure(&workerError))
            QueueWorkerFailure(m_ActiveSnapshot, workerError);
        else
            QueueSyntheticSuperseded(m_ActiveSnapshot, false);
        m_ActiveOwner = 0;
        m_ActiveSnapshot = {};
    }
}

void RawRenderService::QueueWorkerFailure(
    const EditorRenderWorker::Snapshot& snapshot, const std::string& error) {
    if (!snapshot.ownerId || m_Clients.find(snapshot.ownerId) == m_Clients.end()) return;
    m_Completed[snapshot.ownerId].push_back(
        EditorRenderWorker::MakeFailedResult(snapshot, error));
}

bool RawRenderService::Submit(
    ClientId clientId,
    EditorRenderWorker::Snapshot snapshot) {
    if (clientId == 0) return false;
    Pump();
    snapshot.ownerId = clientId;
    snapshot.telemetry.queuedAt = std::chrono::steady_clock::now();
    if (snapshot.telemetry.snapshotReadyAt.time_since_epoch().count() != 0) {
        snapshot.telemetry.commandEnqueueMs =
            std::chrono::duration<double, std::milli>(
                snapshot.telemetry.queuedAt -
                snapshot.telemetry.snapshotReadyAt).count();
    }

    bool submitNow = false;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_Initialized ||
            m_Clients.find(clientId) == m_Clients.end()) {
            return false;
        }
        std::string workerError;
        if (m_Worker.GetThreadFailure(&workerError)) {
            QueueWorkerFailure(snapshot, workerError);
            // Accept a terminal failure, not new GPU work. The caller commits
            // this request's identity and consumes its ordinary failed result.
            return true;
        }
        if (m_ActiveOwner == 0) {
            // A compute task may own the worker without owning a RAW session.
            // Queue the foreground snapshot now so it runs between batches.
            m_ActiveOwner = clientId;
            m_ActiveSnapshot = snapshot;
            submitNow = true;
        } else if (
            m_ActiveOwner == clientId &&
            snapshot.rawRenderPurpose ==
                RawRenderPurpose::InteractivePresentation &&
            m_ActiveSnapshot.rawRenderPurpose == RawRenderPurpose::InteractivePresentation &&
            !m_Worker.IsWaitingForRawDenoiseCompletion() &&
            m_ActiveSnapshot.rawWorkspace.sourceKey == snapshot.rawWorkspace.sourceKey &&
            m_ActiveSnapshot.rawWorkspace.sourceHash == snapshot.rawWorkspace.sourceHash &&
            m_ActiveSnapshot.telemetry.gestureId == snapshot.telemetry.gestureId &&
            ((!snapshot.graphRequest.enabled && !m_ActiveSnapshot.graphRequest.enabled) ||
             Stack::GraphRendering::MayFinishActive(
                 m_ActiveSnapshot.graphRequest, snapshot.graphRequest))) {
            // Finish one frame and retain only the latest pending value. Age
            // includes queue waits and cold resource preparation; canceling
            // on that age can starve a continuous drag of every visible frame.
            // Background refinement remains interruptible at stage boundaries.
            const auto pending = m_Pending.find(clientId);
            if (pending != m_Pending.end()) {
                QueueSyntheticSuperseded(pending->second, false);
            }
            m_Pending[clientId] = std::move(snapshot);
            return true;
        } else if (
            m_ActiveOwner == clientId &&
            RawRenderPurposePriority(snapshot.rawRenderPurpose) <=
                RawRenderPurposePriority(
                    m_ActiveSnapshot.rawRenderPurpose)) {
            const auto pending = m_Pending.find(clientId);
            if (pending != m_Pending.end()) {
                QueueSyntheticSuperseded(pending->second, false);
                m_Pending.erase(pending);
            }
            QueueSyntheticSuperseded(m_ActiveSnapshot, true);
            m_ActiveSnapshot = snapshot;
            submitNow = true;
        } else if (
            m_ActiveOwner != clientId &&
            RawRenderPurposePriority(snapshot.rawRenderPurpose) <
                RawRenderPurposePriority(
                    m_ActiveSnapshot.rawRenderPurpose)) {
            // The persistent worker accepts a newer scheduling serial and
            // cancels the lower-priority stage at its next safe boundary.
            QueueSyntheticSuperseded(m_ActiveSnapshot, true);
            const auto pending = m_Pending.find(clientId);
            if (pending != m_Pending.end()) {
                QueueSyntheticSuperseded(pending->second, false);
                m_Pending.erase(pending);
            }
            m_ActiveOwner = clientId;
            m_ActiveSnapshot = snapshot;
            submitNow = true;
        } else {
            const auto existing = m_Pending.find(clientId);
            if (existing != m_Pending.end()) {
                QueueSyntheticSuperseded(existing->second, false);
            }
            m_Pending[clientId] = std::move(snapshot);
            return true;
        }
    }
    if (submitNow && m_Worker.Submit(std::move(snapshot))) return true;

    std::lock_guard<std::mutex> lock(m_Mutex);
    std::string workerError;
    const bool workerFailed = m_Worker.GetThreadFailure(&workerError);
    if (workerFailed)
        QueueWorkerFailure(m_ActiveSnapshot, workerError);
    else
        QueueSyntheticSuperseded(m_ActiveSnapshot, false);
    m_ActiveOwner = 0;
    m_ActiveSnapshot = {};
    return workerFailed;
}

bool RawRenderService::TryConsumeCompleted(
    ClientId clientId,
    EditorRenderWorker::Result& result) {
    Pump();
    EditorRenderWorker::Result completed;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto found = m_Completed.find(clientId);
        if (found == m_Completed.end() || found->second.empty()) return false;
        completed = std::move(found->second.front());
        found->second.pop_front();
        if (found->second.empty()) m_Completed.erase(found);
    }
    result = std::move(completed);
    return true;
}

bool RawRenderService::TryConsumeViewportTiming(ClientId clientId, EditorRenderWorker::Result& result) {
    Pump();
    EditorRenderWorker::Result completed;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto found = m_Timings.find(clientId);
        if (found == m_Timings.end() || found->second.empty()) return false;
        completed = std::move(found->second.front());
        found->second.pop_front();
        if (found->second.empty()) m_Timings.erase(found);
    }
    result = std::move(completed);
    return true;
}

bool RawRenderService::IsBusyFor(ClientId clientId) const {
    if (clientId == 0) return false;
    std::lock_guard<std::mutex> lock(m_Mutex);
    // Fatal cleanup can still be draining canceled denoise resources, but no
    // request can finish there. A terminal result must not re-latch UI busy.
    if (m_Worker.GetThreadFailure()) return false;
    return m_ActiveOwner == clientId ||
        m_Pending.find(clientId) != m_Pending.end() || m_Worker.HasPendingForOwner(clientId);
}

bool RawRenderService::HasWorkerFailure(ClientId clientId) const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return clientId != 0 && m_Initialized &&
        m_Clients.find(clientId) != m_Clients.end() &&
        m_Worker.GetThreadFailure();
}

bool RawRenderService::HasSnapshotWorkFor(ClientId clientId) const {
    if (clientId == 0) return false;
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Worker.GetThreadFailure()) return false;
    return m_Pending.find(clientId) != m_Pending.end() ||
        m_Worker.HasSnapshotWorkFor(clientId);
}

bool RawRenderService::HasPendingOrBusyForShutdown(
    ClientId clientId) const {
    return IsBusyFor(clientId);
}

EditorRenderWorker::RenderProgress RawRenderService::GetProgressFor(
    ClientId clientId) const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Worker.GetThreadFailure()) return {};
    if (m_ActiveOwner == clientId) return m_Worker.GetProgress();
    EditorRenderWorker::RenderProgress progress;
    if (m_Pending.find(clientId) != m_Pending.end() || m_Worker.HasPendingForOwner(clientId)) {
        progress.busy = true;
        progress.totalSteps = 1;
        progress.label = "Waiting for RAW processing...";
    }
    return progress;
}

void RawRenderService::InvalidateSnapshotsBefore(
    ClientId clientId,
    std::uint64_t generation) {
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto pending = m_Pending.find(clientId);
        if (pending != m_Pending.end() &&
            pending->second.generation < generation) {
            QueueSyntheticSuperseded(pending->second, false);
            m_Pending.erase(pending);
        }
        if (m_ActiveOwner == clientId &&
            m_ActiveSnapshot.generation < generation) {
            QueueSyntheticSuperseded(m_ActiveSnapshot, true);
            m_ActiveOwner = 0;
            m_ActiveSnapshot = {};
        }
    }
    m_Worker.InvalidateOwnerSnapshotsBefore(clientId, generation);
}

bool RawRenderService::ExecuteOpenGlTaskBlocking(
    EditorRenderWorker::OpenGlTask task,
    std::string& error,
    Raw::OpenGlTaskPriority priority) {
    std::shared_lock<std::shared_mutex> lifetime(m_WorkerLifetimeMutex);
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_Initialized) {
            error = "The RAW render owner is unavailable.";
            return false;
        }
    }
    return m_Worker.ExecuteOpenGlTaskBlocking(std::move(task), error, priority);
}

} // namespace Stack::EditorRendering
