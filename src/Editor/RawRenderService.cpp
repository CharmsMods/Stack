#include "Editor/RawRenderService.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Stack::EditorRendering {

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

void RawRenderService::ReleaseResultResources(
    EditorRenderWorker::Result& result) {
    auto releaseTexture = [](EditorRenderWorker::SharedTextureResult& texture) {
        texture.Reset();
    };
    releaseTexture(result.outputTexture);
    releaseTexture(result.rawWorkspace.localRangeOverlayTexture);
    if (result.outputTiles.readyFence != nullptr) {
        glDeleteSync(result.outputTiles.readyFence);
        result.outputTiles.readyFence = nullptr;
    }
    for (EditorRenderWorker::SharedTextureTile& tile :
         result.outputTiles.tiles) {
        if (tile.texture != 0) glDeleteTextures(1, &tile.texture);
        tile.texture = 0;
    }
    result.outputTiles.tiles.clear();
}

void RawRenderService::Release(ClientId clientId) {
    if (clientId == 0) return;
    std::shared_lock<std::shared_mutex> lifetime(m_WorkerLifetimeMutex);
    bool shutdown = false;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Clients.erase(clientId) == 0) return;
        m_Sessions.erase(clientId);
        m_Pending.erase(clientId);
        const auto completed = m_Completed.find(clientId);
        if (completed != m_Completed.end()) {
            for (EditorRenderWorker::Result& result : completed->second) {
                ReleaseResultResources(result);
            }
            m_Completed.erase(completed);
        }
        m_Timings.erase(clientId);
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
    std::lock_guard<std::mutex> lock(m_Mutex);
    for (auto& [owner, results] : m_Completed) {
        (void)owner;
        for (EditorRenderWorker::Result& result : results) {
            ReleaseResultResources(result);
        }
    }
    m_Completed.clear();
    m_Timings.clear();
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
    std::vector<EditorRenderWorker::Result> completed;
    EditorRenderWorker::Result result;
    while (m_Worker.TryConsumeCompleted(result)) {
        completed.push_back(std::move(result));
    }
    while (m_Worker.TryConsumeViewportTiming(result)) completed.push_back(std::move(result));
    std::vector<EditorRenderWorker::Snapshot> readyDenoise;
    EditorRenderWorker::Snapshot continuation;
    while (m_Worker.TryConsumeReadyDenoiseSnapshot(continuation)) {
        readyDenoise.push_back(std::move(continuation));
    }

    EditorRenderWorker::Snapshot next;
    ClientId nextOwner = 0;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        for (EditorRenderWorker::Result& value : completed) {
            if (m_Clients.find(value.ownerId) == m_Clients.end()) {
                ReleaseResultResources(value);
                continue;
            }
            if (value.timingOnly) {
                auto& timings=m_Timings[value.ownerId];
                if (timings.size()>=32) timings.pop_front();
                timings.push_back(std::move(value));
                continue;
            }
            auto& queue = m_Completed[value.ownerId];
            if (value.graphRequest.enabled) {
                for (auto it = queue.begin(); it != queue.end();) {
                    if (!it->graphRequest.enabled) { ++it; continue; }
                    ReleaseResultResources(*it);
                    it = queue.erase(it);
                }
            }
            queue.push_back(std::move(value));
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
        if (!m_Worker.HasPendingOrBusyForShutdown() ||
            m_Worker.IsWaitingForRawDenoiseCompletion()) {
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
        QueueSyntheticSuperseded(m_ActiveSnapshot, false);
        m_ActiveOwner = 0;
        m_ActiveSnapshot = {};
    }
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
    QueueSyntheticSuperseded(m_ActiveSnapshot, false);
    m_ActiveOwner = 0;
    m_ActiveSnapshot = {};
    return false;
}

bool RawRenderService::TryConsumeCompleted(
    ClientId clientId,
    EditorRenderWorker::Result& result) {
    Pump();
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found = m_Completed.find(clientId);
    if (found == m_Completed.end() || found->second.empty()) return false;
    result = std::move(found->second.front());
    found->second.pop_front();
    if (found->second.empty()) m_Completed.erase(found);
    return true;
}

bool RawRenderService::TryConsumeViewportTiming(ClientId clientId, EditorRenderWorker::Result& result) {
    Pump();
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found=m_Timings.find(clientId);
    if (found==m_Timings.end() || found->second.empty()) return false;
    result=std::move(found->second.front()); found->second.pop_front();
    if (found->second.empty()) m_Timings.erase(found);
    return true;
}

bool RawRenderService::IsBusyFor(ClientId clientId) const {
    if (clientId == 0) return false;
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_ActiveOwner == clientId ||
        m_Pending.find(clientId) != m_Pending.end() || m_Worker.HasPendingForOwner(clientId);
}

bool RawRenderService::HasPendingOrBusyForShutdown(
    ClientId clientId) const {
    return IsBusyFor(clientId);
}

EditorRenderWorker::RenderProgress RawRenderService::GetProgressFor(
    ClientId clientId) const {
    std::lock_guard<std::mutex> lock(m_Mutex);
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
    std::string& error) {
    std::shared_lock<std::shared_mutex> lifetime(m_WorkerLifetimeMutex);
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_Initialized) {
            error = "The RAW render owner is unavailable.";
            return false;
        }
    }
    return m_Worker.ExecuteOpenGlTaskBlocking(std::move(task), error);
}

} // namespace Stack::EditorRendering
