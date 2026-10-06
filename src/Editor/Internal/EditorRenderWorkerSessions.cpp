#include "Editor/EditorRenderWorker.h"

#include <algorithm>

bool EditorRenderWorker::IsOwnerSnapshotInvalidLocked(
    std::uint64_t ownerId, std::uint64_t generation,
    std::uint64_t serial, RawRenderPurpose purpose) const {
    if (generation < m_InvalidBeforeGeneration || serial < m_InvalidBeforeSchedulingSerial)
        return true;
    const auto found = m_OwnerStates.find(ownerId);
    if (found == m_OwnerStates.end()) return false;
    const auto& owner = found->second;
    const auto canceled = owner.canceledPurposeBeforeSerial.find(purpose);
    return owner.released || generation < owner.invalidBeforeGeneration ||
        serial < owner.invalidBeforeSerial ||
        (canceled != owner.canceledPurposeBeforeSerial.end() && serial < canceled->second);
}

bool EditorRenderWorker::IsOwnerResultStaleLocked(const Result& result) const {
    if (IsOwnerSnapshotInvalidLocked(result.ownerId, result.generation,
            result.schedulingSerial, result.rawRenderPurpose)) return true;
    const auto found = m_OwnerStates.find(result.ownerId);
    return found != m_OwnerStates.end() && result.schedulingSerial < found->second.latestSerial &&
        !Stack::GraphRendering::MayFinishActive(result.graphRequest, found->second.latestGraphRequest);
}

bool EditorRenderWorker::HasDenoiseWorkLocked() const {
    if (!m_DrainingDenoise.empty()) return true;
    for (const auto& [id, owner] : m_OwnerStates) {
        (void)id;
        if (owner.continuation || owner.denoiseInFlight) return true;
    }
    return false;
}

bool EditorRenderWorker::HasPendingForOwner(std::uint64_t ownerId) const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if ((m_Rendering && m_RenderingOwner == ownerId) ||
        (m_HasPending && m_Pending.ownerId == ownerId)) return true;
    const auto found = m_OwnerStates.find(ownerId);
    if (found != m_OwnerStates.end() &&
        (found->second.continuation || found->second.denoiseInFlight)) return true;
    return std::any_of(m_DrainingDenoise.begin(), m_DrainingDenoise.end(),
        [ownerId](const DrainingDenoiseState& value) { return value.ownerId == ownerId; });
}

void EditorRenderWorker::CancelOwnerSnapshots(
    std::uint64_t ownerId, std::optional<RawRenderPurpose> purpose) {
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto& owner = m_OwnerStates[ownerId];
        if (purpose) owner.canceledPurposeBeforeSerial[*purpose] = m_NextSchedulingSerial;
        else owner.invalidBeforeSerial = m_NextSchedulingSerial;
        if (m_HasPending && m_Pending.ownerId == ownerId &&
            (!purpose || m_Pending.rawRenderPurpose == *purpose)) {
            m_Pending = {};
            m_HasPending = false;
        }
        if (!purpose || owner.denoisePurpose == *purpose) {
            owner.continuation.reset();
            owner.continuationReady = false;
            owner.resetDenoiseRequested = true;
        }
    }
    m_Cv.notify_one();
}

void EditorRenderWorker::InvalidateOwnerSnapshotsBefore(
    std::uint64_t ownerId, std::uint64_t generation) {
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto& owner = m_OwnerStates[ownerId];
        owner.invalidBeforeGeneration = std::max(owner.invalidBeforeGeneration, generation);
        if (m_HasPending && m_Pending.ownerId == ownerId && m_Pending.generation < generation) {
            m_Pending = {};
            m_HasPending = false;
        }
        if (owner.latestGeneration < generation ||
            (owner.continuation && owner.continuation->generation < generation)) {
            owner.continuation.reset();
            owner.continuationReady = false;
            owner.resetDenoiseRequested = true;
        }
    }
    m_Cv.notify_one();
}

void EditorRenderWorker::ReleaseOwner(std::uint64_t ownerId) {
    CancelOwnerSnapshots(ownerId);
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_OwnerStates[ownerId].released = true;
    }
    m_Cv.notify_one();
}

void EditorRenderWorker::BindDenoiseState(const Snapshot& snapshot, RenderPipeline& pipeline) {
    std::shared_ptr<RawRgbDenoiseState> state;
    bool allowStart = false;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto& owner = m_OwnerStates[snapshot.ownerId];
        if (!owner.denoise) owner.denoise = std::make_shared<RawRgbDenoiseState>();
        if (!owner.denoise->pending && !owner.denoise->deferred)
            owner.denoisePurpose = snapshot.rawRenderPurpose;
        state = owner.denoise;
        allowStart = m_DrainingDenoise.empty();
        for (const auto& [id, other] : m_OwnerStates) {
            if (id != snapshot.ownerId && other.denoise && other.denoise->pending) {
                allowStart = false;
                break;
            }
        }
    }
    // Only the admitted job may retain source/proxy/model image buffers.
    // Waiting owners retain a snapshot, whose RAW payload is shared.
    pipeline.SetRawRgbDenoiseState(std::move(state), allowStart);
}

void EditorRenderWorker::PollDenoiseContinuationsLocked() {
    for (auto it = m_OwnerStates.begin(); it != m_OwnerStates.end();) {
        auto& owner = it->second;
        if (owner.resetDenoiseRequested) {
            if (owner.denoise && owner.denoise->pending) {
                owner.denoise->RequestCancellation();
                m_DrainingDenoise.push_back({it->first, std::move(owner.denoise)});
            } else {
                owner.denoise.reset();
            }
            owner.denoiseInFlight = false;
            owner.resetDenoiseRequested = false;
        }
        if (owner.released) it = m_OwnerStates.erase(it);
        else ++it;
    }
    for (auto it = m_DrainingDenoise.begin(); it != m_DrainingDenoise.end();) {
        if (!it->state->pending || !it->state->future.valid() || it->state->IsCompletionReady()) {
            it->state->CancelAndWait();
            it = m_DrainingDenoise.erase(it);
        } else ++it;
    }
    bool inferencePending = !m_DrainingDenoise.empty();
    for (const auto& [id, owner] : m_OwnerStates) {
        (void)id;
        inferencePending |= owner.denoise && owner.denoise->pending;
    }
    for (auto& [id, owner] : m_OwnerStates) {
        if (!owner.continuation) continue;
        if (owner.resumeOriginalPurpose) {
            const auto purpose = owner.continuation->rawRenderPurpose;
            owner.continuationReady =
                (purpose != RawRenderPurpose::ExplicitExport && purpose != RawRenderPurpose::ExplicitInspection) ||
                CanStartAuthoritativeRenderLocked(id);
            continue;
        }
        if (!owner.denoise) continue;
        owner.continuationReady = owner.denoise->pending
            ? !owner.denoise->future.valid() || owner.denoise->IsCompletionReady()
            : owner.denoise->deferred && !inferencePending;
    }
}

bool EditorRenderWorker::CanStartAuthoritativeRenderLocked(std::uint64_t ownerId) const {
    if (!m_DrainingDenoise.empty()) return false;
    for (const auto& [id, owner] : m_OwnerStates) {
        if (!owner.denoise || !owner.denoise->pending) continue;
        // The requested owner can consume its own ready result before the
        // synchronous render. Another owner's result must be published first.
        if (id != ownerId || (owner.denoise->future.valid() && !owner.denoise->IsCompletionReady())) return false;
    }
    return true;
}

bool EditorRenderWorker::DeferAuthoritativeRenderLocked(Snapshot& snapshot) {
    if (snapshot.rawRenderPurpose != RawRenderPurpose::ExplicitExport &&
        snapshot.rawRenderPurpose != RawRenderPurpose::ExplicitInspection) return false;
    if (CanStartAuthoritativeRenderLocked(snapshot.ownerId)) return false;
    auto& owner = m_OwnerStates[snapshot.ownerId];
    owner.denoisePurpose = snapshot.rawRenderPurpose;
    owner.continuation = std::move(snapshot);
    owner.continuationReady = false;
    owner.resumeOriginalPurpose = true;
    return true;
}

bool EditorRenderWorker::TryConsumeReadyDenoiseSnapshot(Snapshot& snapshot) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    for (auto& [id, owner] : m_OwnerStates) {
        // Dedicated workers replay owner zero themselves. Shared clients
        // return through the service so priorities and active owner agree.
        if (id == 0 || !owner.continuationReady || !owner.continuation) continue;
        snapshot = std::move(*owner.continuation);
        owner.continuation.reset();
        owner.continuationReady = false;
        if (!owner.resumeOriginalPurpose) snapshot.rawRenderPurpose = RawRenderPurpose::DenoiseCompletion;
        owner.resumeOriginalPurpose = false;
        owner.denoisePurpose = snapshot.rawRenderPurpose;
        return true;
    }
    return false;
}

void EditorRenderWorker::RetainDenoiseContinuationLocked(Snapshot& snapshot) {
    auto& owner = m_OwnerStates[snapshot.ownerId];
    owner.denoiseInFlight = owner.denoise && owner.denoise->pending;
    const bool current = !IsOwnerSnapshotInvalidLocked(snapshot.ownerId, snapshot.generation,
        snapshot.schedulingSerial, snapshot.rawRenderPurpose) && owner.latestSerial == snapshot.schedulingSerial;
    if (!current) return;
    const bool continuationAllowed = snapshot.rawRenderPurpose != RawRenderPurpose::ExplicitExport &&
        snapshot.rawRenderPurpose != RawRenderPurpose::ExplicitInspection &&
        snapshot.rawRenderPurpose != RawRenderPurpose::ViewportCalibration &&
        !(snapshot.rawRenderPurpose == RawRenderPurpose::ViewportOverview && snapshot.rawWorkspace.viewportDependencyEdge >= 0);
    if (continuationAllowed && owner.denoise && (owner.denoise->pending || owner.denoise->deferred)) {
        owner.denoisePurpose = snapshot.rawRenderPurpose;
        owner.continuation = std::move(snapshot);
        owner.continuationReady = false;
        owner.resumeOriginalPurpose = false;
    } else {
        owner.continuation.reset();
        owner.continuationReady = false;
        // A settled export has no interactive follow-up to publish.
        if (!continuationAllowed && owner.denoiseInFlight) owner.resetDenoiseRequested = true;
    }
}

void EditorRenderWorker::DrainDenoiseForShutdown() noexcept {
    decltype(m_OwnerStates) owners;
    decltype(m_DrainingDenoise) draining;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        owners.swap(m_OwnerStates);
        draining.swap(m_DrainingDenoise);
        m_Rendering = false;
    }
    for (auto& [id, owner] : owners) {
        (void)id;
        if (owner.denoise) owner.denoise->RequestCancellation();
    }
    for (auto& value : draining) value.state->RequestCancellation();
    for (auto& [id, owner] : owners) {
        (void)id;
        if (owner.denoise) owner.denoise->CancelAndWait();
    }
    for (auto& value : draining) value.state->CancelAndWait();
}
