#pragma once

#include "Editor/EditorRenderWorker.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

struct GLFWwindow;

namespace Stack::Validation {
bool ValidateRawViewportInteraction(GLFWwindow* sharedWindow);
}

namespace Stack::EditorRendering {

struct RawRenderSessionConfig {
    std::string sourceIdentity;
    std::uint64_t sourceHash = 0;
    std::uint64_t graphStructureRevision = 0;
    std::uint64_t processingContractRevision = 0;
    std::shared_ptr<const EditorRenderWorker::Snapshot> snapshotTemplate;
};

struct RawRenderCommand {
    std::uint64_t generation = 0;
    std::uint64_t lastAcceptedGeneration = 0;
    std::uint64_t cancellationToken = 0;
    RawRenderPurpose purpose = RawRenderPurpose::InteractivePresentation;
    int priority = RawRenderPurposePriority(
        RawRenderPurpose::InteractivePresentation);
    int targetEdge = 0;
    EditorRenderWorker::RawRenderTelemetry telemetry;
    EditorRenderWorker::RawWorkspaceSnapshot rawWorkspace;
    ViewportTilingSettings viewportTiling;
    std::vector<EditorRenderWorker::PreviewRequest> previews;
};

struct RawPresentationResult {
    std::string sessionSourceIdentity;
    std::uint64_t sourceHash = 0;
    std::size_t recipeFingerprint = 0;
    std::uint64_t acceptedGeneration = 0;
    int resolutionTierEdge = 0;
    EditorRenderWorker::SharedTextureResult texture;
    EditorRenderWorker::RawRenderTelemetry telemetry;
};

// One process-wide GL owner for every live RAW graph. Editor sessions retain
// independent documents and revisions, while this service serializes their
// GPU evaluation and routes fenced results back to the submitting session.
class RawRenderService {
public:
    using ClientId = std::uint64_t;

    static RawRenderService& Get();

    ClientId Acquire(GLFWwindow* sharedWindow);
    void Release(ClientId clientId);

    bool Submit(ClientId clientId, EditorRenderWorker::Snapshot snapshot);
    bool ConfigureSession(ClientId clientId, RawRenderSessionConfig config);
    void ClearSession(ClientId clientId);
    bool SubmitCommand(ClientId clientId, RawRenderCommand command);
    void CancelPurpose(ClientId clientId, RawRenderPurpose purpose);
    void CancelCachePrewarm(ClientId clientId) {
        CancelPurpose(clientId, RawRenderPurpose::CachePrewarm);
    }
    bool TryConsumeCompleted(
        ClientId clientId,
        EditorRenderWorker::Result& result);
    bool TryConsumeViewportTiming(ClientId clientId, EditorRenderWorker::Result& result);
    bool IsBusyFor(ClientId clientId) const;
    bool HasPendingOrBusyForShutdown(ClientId clientId) const;
    EditorRenderWorker::RenderProgress GetProgressFor(
        ClientId clientId) const;
    void InvalidateSnapshotsBefore(
        ClientId clientId,
        std::uint64_t generation);
    bool ExecuteOpenGlTaskBlocking(
        EditorRenderWorker::OpenGlTask task,
        std::string& error);

private:
    friend bool Stack::Validation::ValidateRawViewportInteraction(GLFWwindow* sharedWindow);
    RawRenderService() = default;
    ~RawRenderService() = default;
    RawRenderService(const RawRenderService&) = delete;
    RawRenderService& operator=(const RawRenderService&) = delete;

    void Pump();
    void QueueSyntheticSuperseded(
        const EditorRenderWorker::Snapshot& snapshot,
        bool workerStarted, bool overload = false, RawPreviewSkipReason reason = RawPreviewSkipReason::None);
    static void ReleaseResultResources(EditorRenderWorker::Result& result);

    mutable std::mutex m_Mutex;
    // Protect worker initialization/shutdown without locking session edits for
    // the duration of a blocking compute dispatch.
    mutable std::shared_mutex m_WorkerLifetimeMutex;
    EditorRenderWorker m_Worker;
    std::unordered_set<ClientId> m_Clients;
    std::unordered_map<ClientId, EditorRenderWorker::Snapshot> m_Pending;
    std::unordered_map<ClientId, RawRenderSessionConfig> m_Sessions;
    std::unordered_map<ClientId, std::deque<EditorRenderWorker::Result>>
        m_Completed;
    std::unordered_map<ClientId, std::deque<EditorRenderWorker::Result>> m_Timings;
    ClientId m_ActiveOwner = 0;
    EditorRenderWorker::Snapshot m_ActiveSnapshot;
    ClientId m_NextClientId = 1;
    bool m_Initialized = false;
};

} // namespace Stack::EditorRendering
