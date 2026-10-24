#include "Editor/EditorModule.h"

bool EditorModule::IsGraphContinuationViewCurrent(const EditorRenderWorker::Result& result) const {
    if (!result.viewportViewId) return IsViewportPreviewResultCurrent(result);
    if (!UsesGraphViewportPreview() ||
        result.viewportViewId != (m_RawWorkspaceRootTabActive ? 1 : 2)) return false;
    const auto& view = ActiveViewportPreviewState();
    if (view.request.empty || result.viewportContentIdentity != view.contentIdentity) return false;
    // Successful regional replacements need real geometry and the ordinary
    // coverage/anti-downgrade checks. A failure has no replacement coverage.
    return result.success
        ? result.viewportPreview.enabled && IsViewportPreviewResultCurrent(result)
        : result.viewportPreview.generation == view.request.generation;
}

bool EditorModule::IsCurrentGraphDenoiseContinuation(
    const EditorRenderWorker::Result& result) const {
    const auto& presented = m_RenderPresentation;
    if (!result.graphRequest.enabled ||
        result.rawRenderPurpose != RawRenderPurpose::DenoiseCompletion ||
        result.telemetry.superseded ||
        result.error == "Render superseded by a newer snapshot." ||
        !presented.denoisePending || presented.failed ||
        result.ownerId != presented.ownerId ||
        result.ownerId != (result.rawWorkspace.sourceKey.empty() ? 0 : m_RawRenderClientId) ||
        result.generation != presented.generation ||
        result.generation != m_RenderGeneration ||
        result.generation != m_GraphAcceptedResultGeneration ||
        result.generation != m_ViewportOutputRenderGeneration ||
        result.schedulingSerial <= presented.schedulingSerial ||
        result.rawWorkspace.sourceKey != presented.sourceKey ||
        result.rawWorkspace.sourceHash != presented.sourceHash ||
        result.rawWorkspace.recipeRevision != presented.recipeRevision) return false;
    const auto current = CurrentGraphRenderTag();
    return Stack::GraphRendering::SameContext(result.graphRequest, presented.graphRequest) &&
        result.graphRequest.revision == presented.graphRequest.revision &&
        Stack::GraphRendering::SameContext(result.graphRequest, current) &&
        result.graphRequest.revision == current.revision;
}

bool EditorModule::IsPresentedRenderComplete() const {
    const auto& presented = m_RenderPresentation;
    if (!presented.generation || presented.generation != m_ViewportOutputRenderGeneration ||
        presented.denoisePending || presented.failed) return false;
    // RAW identity can change after a catalog rescan without an authored
    // recipe edit. This proof is independent of the emitted Graph mode.
    if (!presented.sourceKey.empty() &&
        (presented.sourceKey != GetActiveRawWorkspacePreviewIdentity() ||
         presented.sourceHash == 0 || presented.sourceHash != RawViewportSourceHash())) return false;
    if (presented.hasExplicitDocumentContext) {
        const auto current = CurrentGraphRenderTag();
        const auto& accepted = presented.explicitDocumentContext;
        const bool sameContext = accepted.enabled
            ? Stack::GraphRendering::SameContext(accepted, current) : !current.enabled;
        if (!sameContext || accepted.revision != current.revision) return false;
    }
    if (presented.graphRequest.enabled) {
        const auto current = CurrentGraphRenderTag();
        return Stack::GraphRendering::SameContext(presented.graphRequest, current) &&
            presented.graphRequest.revision == current.revision;
    }
    return presented.sourceKey.empty()
        ? presented.generation >= m_RenderGeneration
        : presented.generation >= m_LatestRawPresentationGeneration &&
          presented.recipeRevision == m_LatestRawPresentationRecipeRevision;
}

void EditorModule::AdoptRenderPresentationState(const EditorRenderWorker::Result& result) {
    Stack::EditorRendering::RenderPresentationState accepted;
    accepted.ownerId = result.ownerId;
    accepted.generation = result.generation;
    accepted.schedulingSerial = result.schedulingSerial;
    accepted.graphRequest = result.graphRequest;
    const auto& request = m_FullQualityRenderRequest;
    if (!result.graphRequest.enabled &&
        result.rawRenderPurpose == RawRenderPurpose::ExplicitInspection &&
        request.phase == Stack::EditorRendering::FullQualityRenderRequestState::Phase::Submitted &&
        result.ownerId == request.ownerId && result.generation == request.generation) {
        accepted.hasExplicitDocumentContext = true;
        accepted.explicitDocumentContext = request.graphRequest;
    }
    accepted.sourceKey = result.rawWorkspace.sourceKey;
    accepted.sourceHash = result.rawWorkspace.sourceHash;
    accepted.recipeRevision = result.rawWorkspace.recipeRevision;
    accepted.denoisePending = result.denoisePending;
    accepted.failed = !result.success || !result.error.empty();
    accepted.error = result.error;
    // Build allocating identity first. If it fails after texture adoption, the
    // old generation (or still-pending same generation) cannot claim readiness.
    m_RenderPresentation = std::move(accepted);
}

void EditorModule::ObserveRenderPresentationFailure(const EditorRenderWorker::Result& result) {
    if ((result.success && result.error.empty()) ||
        !IsGraphContinuationViewCurrent(result) ||
        !IsCurrentGraphDenoiseContinuation(result)) return;
    // Retain the useful before-state, but it can never be exported as the
    // completed authored image. A fresh explicit request can retry normally.
    m_RenderPresentation.failed = true;
    m_RenderPresentation.error = result.error.empty()
        ? "AI denoise completion failed." : result.error;
}

void EditorModule::TrackFullQualityRenderSnapshot(const EditorRenderWorker::Snapshot& snapshot) {
    auto& request = m_FullQualityRenderRequest;
    if (!request.Pending() || !snapshot.outputConnected ||
        snapshot.rawRenderPurpose != RawRenderPurpose::ExplicitInspection) return;
    using Phase = Stack::EditorRendering::FullQualityRenderRequestState::Phase;
    const auto current = CurrentGraphRenderTag();
    const auto ownerId = snapshot.rawWorkspace.sourceKey.empty() ? 0 : m_RawRenderClientId;
    if (request.phase == Phase::Submitted) {
        const bool sameContext = request.graphRequest.enabled
            ? Stack::GraphRendering::SameContext(request.graphRequest, current) : !current.enabled;
        if (!sameContext || request.graphRequest.revision != current.revision ||
            request.ownerId != ownerId || request.generation != snapshot.generation ||
            request.emittedGraphRequest.enabled != snapshot.graphRequest.enabled ||
            (snapshot.graphRequest.enabled &&
             (!Stack::GraphRendering::SameContext(request.emittedGraphRequest, snapshot.graphRequest) ||
              request.emittedGraphRequest.revision != snapshot.graphRequest.revision)) ||
            request.sourceKey != snapshot.rawWorkspace.sourceKey ||
            request.sourceHash != snapshot.rawWorkspace.sourceHash ||
            request.recipeRevision != snapshot.rawWorkspace.recipeRevision)
            CancelFullQualityRenderRequest("Full-quality rendering was superseded before completion.");
        return; // A submitted attempt must never be rebound to newer work.
    }
    Stack::EditorRendering::FullQualityRenderRequestState submitted;
    submitted.phase = Phase::Submitted;
    submitted.ownerId = ownerId;
    submitted.generation = snapshot.generation;
    submitted.graphRequest = current;
    submitted.emittedGraphRequest = snapshot.graphRequest;
    submitted.sourceKey = snapshot.rawWorkspace.sourceKey;
    submitted.sourceHash = snapshot.rawWorkspace.sourceHash;
    submitted.recipeRevision = snapshot.rawWorkspace.recipeRevision;
    request = std::move(submitted);
}

void EditorModule::FailFullQualityRenderRequest(const std::string& reason, bool canceled) {
    auto& request = m_FullQualityRenderRequest;
    if (!request.Pending()) return;
    using Phase = Stack::EditorRendering::FullQualityRenderRequestState::Phase;
    request.phase = canceled ? Phase::Canceled : Phase::Failed;
    m_RawWorkspaceExplicitFullQualityRenderRequested = false;
    m_RawWorkspaceFullResolutionPreviewPending = false;
    m_RawWorkspaceFullResolutionPreviewRequested = false;
    m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
    request.error = reason.empty() ? "Full-quality rendering failed." : reason;
}

void EditorModule::CancelFullQualityRenderRequest(const std::string& reason) {
    FailFullQualityRenderRequest(reason, true);
}

void EditorModule::CompleteFullQualityRenderRequest(
    const EditorRenderWorker::Result& result, bool adopted) {
    auto& request = m_FullQualityRenderRequest;
    using Phase = Stack::EditorRendering::FullQualityRenderRequestState::Phase;
    if (request.phase != Phase::Submitted ||
        result.rawRenderPurpose != RawRenderPurpose::ExplicitInspection ||
        result.ownerId != request.ownerId || result.generation != request.generation) return;
    const auto reject = [&](const std::string& reason, bool canceled = false) {
        // A malformed successful packet can be rejected after the ordinary
        // presentation consumer ran. Its pixels must not claim final quality.
        if (adopted && m_RenderPresentation.ownerId == result.ownerId &&
            m_RenderPresentation.generation == result.generation)
            m_RenderPresentation.failed = true;
        FailFullQualityRenderRequest(reason, canceled);
    };
    // Once owner and generation identify this submitted attempt, an invalid
    // immutable identity is a terminal rejection, not an unrelated packet to
    // ignore. Otherwise Queue would wait for a second completion that cannot
    // arrive. Older generations and other owners remain harmlessly ignored.
    if (result.rawWorkspace.sourceKey != request.sourceKey ||
        result.rawWorkspace.sourceHash != request.sourceHash ||
        result.rawWorkspace.recipeRevision != request.recipeRevision) {
        reject("The full-quality render returned a different source or recipe.", true);
        return;
    }
    const auto current = CurrentGraphRenderTag();
    const bool sameContext = request.graphRequest.enabled
        ? Stack::GraphRendering::SameContext(request.graphRequest, current)
        : !current.enabled;
    if (result.telemetry.superseded || result.error == "Render superseded by a newer snapshot." ||
        result.graphRequest.enabled != request.emittedGraphRequest.enabled ||
        (result.graphRequest.enabled &&
         (!Stack::GraphRendering::SameContext(result.graphRequest, request.emittedGraphRequest) ||
          result.graphRequest.revision != request.emittedGraphRequest.revision)) ||
        !sameContext || request.graphRequest.revision != current.revision) {
        reject("Full-quality rendering was superseded by a newer request.", true);
        return;
    }
    if (!result.success || !result.error.empty() || !adopted ||
        !IsPresentedRenderComplete() || !HasCompleteNativeViewportOutput()) {
        reject(result.error.empty()
            ? "The full-quality render did not produce a complete final image." : result.error);
        return;
    }
    request.phase = Phase::Ready;
    request.error.clear();
    m_RawWorkspaceExplicitFullQualityRenderRequested = false;
}

bool EditorModule::DidFullQualityRenderFail(std::string* errorMessage) const {
    if (errorMessage) errorMessage->clear();
    using Phase = Stack::EditorRendering::FullQualityRenderRequestState::Phase;
    if (m_FullQualityRenderRequest.phase == Phase::Submitted ||
        m_FullQualityRenderRequest.phase == Phase::Ready) {
        const auto& tracked = m_FullQualityRenderRequest.graphRequest;
        const auto current = CurrentGraphRenderTag();
        const bool sameContext = tracked.enabled
            ? Stack::GraphRendering::SameContext(tracked, current) : !current.enabled;
        const bool sourceChanged = !m_FullQualityRenderRequest.sourceKey.empty() &&
            (m_FullQualityRenderRequest.sourceKey != GetActiveRawWorkspacePreviewIdentity() ||
             m_FullQualityRenderRequest.sourceHash != RawViewportSourceHash());
        if (!sameContext || tracked.revision != current.revision || sourceChanged) {
            if (errorMessage) *errorMessage = "Full-quality rendering was superseded by a document change.";
            return true;
        }
    }
    if (m_FullQualityRenderRequest.phase != Phase::Failed &&
        m_FullQualityRenderRequest.phase != Phase::Canceled) return false;
    if (errorMessage) *errorMessage = m_FullQualityRenderRequest.error;
    return true;
}
