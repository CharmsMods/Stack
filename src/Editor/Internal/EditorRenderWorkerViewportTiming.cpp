#include "Editor/EditorRenderWorker.h"

bool EditorRenderWorker::WaitForSubmittedGpuWork() {
    // GL dispatch returning only means the commands were queued. Keep one
    // worker batch in flight, otherwise a CPU-fast shader path can build a GPU
    // backlog that delays the UI context and every newer image edit.
    GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!fence) {
        // Preserve the bound even if the driver cannot allocate another sync.
        // This fallback runs exclusively on the worker, never on the UI thread.
        glFinish();
        return true;
    }
    glFlush();
    bool complete = false;
    for (;;) {
        const GLenum status = glClientWaitSync(fence, 0, 0);
        if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) {
            complete = true;
            break;
        }
        if (status == GL_WAIT_FAILED) {
            glFinish();
            complete = true;
            break;
        }
        std::unique_lock<std::mutex> lock(m_Mutex);
        // Pending edits replace the one queued snapshot while this wait lets
        // the UI run. Shutdown can wake it immediately; do not spin merely
        // because another snapshot is pending.
        if (m_Cv.wait_for(lock, std::chrono::milliseconds(2), [this] { return m_StopRequested; })) break;
    }
    glDeleteSync(fence);
    return complete;
}

bool EditorRenderWorker::TryConsumeViewportTiming(Result& result) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_CompletedViewportTimings.empty()) return false;
    result=std::move(m_CompletedViewportTimings.front());
    m_CompletedViewportTimings.pop();
    return true;
}

void EditorRenderWorker::CaptureViewportTiming(const Snapshot& snapshot, Result& result) {
    if (!m_PersistentPipeline || snapshot.rawRenderPurpose == RawRenderPurpose::ViewportCalibration ||
        (snapshot.rawWorkspace.sourceKey.empty() && !result.viewportPreview.enabled)) return;
    auto batch=m_PersistentPipeline->m_RawViewportGpuTiming.Capture();
    if (!batch.serviceStart) { Raw::ViewportGpuTiming::Release(batch); return; }
    if (!result.success || result.telemetry.superseded ||
        (result.graphRequest.enabled && !result.viewportPreview.enabled) ||
        (snapshot.rawRenderPurpose != RawRenderPurpose::InteractivePresentation &&
         snapshot.rawRenderPurpose != RawRenderPurpose::ViewportRefinement) ||
        (snapshot.rawWorkspace.hasRecipe && snapshot.rawWorkspace.analysisRequested) || result.rawWorkspace.viewportDiagnostic ||
        snapshot.rawWorkspace.graphScopeStage != RawDevelopmentGraphScopeStage::None ||
        snapshot.rawWorkspace.gradingScopeSource != RawDevelopmentGradingScopeSource::None ||
        snapshot.rawWorkspace.localRangeTargetSampleRequested || snapshot.rawWorkspace.localRangeTargetPreview.enabled ||
        snapshot.compositeOutputs.size() ||
        (!result.viewportPreview.enabled && (snapshot.previews.size() || snapshot.developCandidateRenders.size()))) {
        Raw::ViewportGpuTiming::Release(batch); return;
    }
    if (m_PendingViewportTimings.size()>=16) {
        Raw::ViewportGpuTiming::Release(m_PendingViewportTimings.front().batch);
        m_PendingViewportTimings.pop_front();
    }
    PendingViewportTiming pending;
    pending.batch=std::move(batch);
    auto& evidence=pending.evidence;
    evidence.timingOnly=true; evidence.success=true;
    evidence.ownerId=result.ownerId; evidence.generation=result.generation;
    evidence.graphRequest=result.graphRequest;
    evidence.viewportPreview=result.viewportPreview;
    evidence.viewportViewId=result.viewportViewId;
    evidence.viewportPreviewGeometry=result.viewportPreviewGeometry;
    evidence.viewportExecutionIdentity=result.viewportExecutionIdentity;
    evidence.viewportContentIdentity=result.viewportContentIdentity;
    evidence.viewportCold=result.viewportCold;
    evidence.schedulingSerial=result.schedulingSerial;
    evidence.rawRenderPurpose=result.rawRenderPurpose;
    evidence.telemetry=result.telemetry;
    evidence.telemetry.cpuDispatchMs=result.telemetry.cpuDispatchMs > 0.0 ?
        result.telemetry.cpuDispatchMs : result.telemetry.workerTotalMs;
    evidence.previewMaxDimension=result.previewMaxDimension;
    evidence.rawWorkspace.sourceKey=result.rawWorkspace.sourceKey;
    evidence.rawWorkspace.sourceHash=result.rawWorkspace.sourceHash;
    evidence.rawWorkspace.recipeRevision=result.rawWorkspace.recipeRevision;
    evidence.rawWorkspace.viewportGeneration=result.rawWorkspace.viewportGeneration;
    evidence.rawWorkspace.viewportRegion=result.rawWorkspace.viewportRegion;
    evidence.rawWorkspace.expectedNativeOutputWidth=result.rawWorkspace.expectedNativeOutputWidth;
    evidence.rawWorkspace.expectedNativeOutputHeight=result.rawWorkspace.expectedNativeOutputHeight;
    evidence.firstMeasuredStage=result.firstMeasuredStage;
    evidence.cacheEdge=result.cacheEdge;
    evidence.workloadKeys=result.workloadKeys;
    evidence.editStage=result.editStage;
    evidence.mainGraphStats=result.mainGraphStats;
    evidence.timingNativeWidth=result.viewportPreviewGeometry.Valid() ? result.viewportPreviewGeometry.logicalWidth : snapshot.rawWorkspace.fullFrameWidth;
    evidence.timingNativeHeight=result.viewportPreviewGeometry.Valid() ? result.viewportPreviewGeometry.logicalHeight : snapshot.rawWorkspace.fullFrameHeight;
    evidence.timingOutputWidth=result.outputTexture.width;
    evidence.timingOutputHeight=result.outputTexture.height;
    evidence.timingRepresentation=result.timingRepresentation;
    m_PendingViewportTimings.push_back(std::move(pending));
}

void EditorRenderWorker::CompleteViewportTiming(const Result& result) {
    if (!result.viewportPreview.enabled) return;
    for (auto it = m_PendingViewportTimings.rbegin(); it != m_PendingViewportTimings.rend(); ++it) {
        auto& evidence = it->evidence;
        if (evidence.ownerId != result.ownerId || evidence.generation != result.generation ||
            evidence.schedulingSerial != result.schedulingSerial || evidence.viewportViewId != result.viewportViewId ||
            evidence.viewportExecutionIdentity != result.viewportExecutionIdentity ||
            evidence.viewportContentIdentity != result.viewportContentIdentity) continue;
        // The stage spans describe the isolated main render. The request's
        // service interval also includes publication and required coverage.
        // Separate auxiliary passes between them count conservatively; the
        // caller supplies their workload in the compatible execution identity.
        unsigned int end = 0;
        glGenQueries(1, &end);
        if (end) {
            glQueryCounter(end, GL_TIMESTAMP);
            if (it->batch.serviceEnd) glDeleteQueries(1, &it->batch.serviceEnd);
            it->batch.serviceEnd = end;
            glFlush();
        }
        evidence.telemetry.cpuDispatchMs = std::max(evidence.telemetry.cpuDispatchMs, result.telemetry.workerTotalMs);
        evidence.telemetry.workerTotalMs = result.telemetry.workerTotalMs;
        return;
    }
}

void EditorRenderWorker::FinishViewportTiming(const Result& result) {
    if (!result.viewportPreview.enabled) return;
    for (auto it = m_PendingViewportTimings.rbegin(); it != m_PendingViewportTimings.rend(); ++it) {
        auto& evidence = it->evidence;
        if (evidence.ownerId != result.ownerId || evidence.generation != result.generation ||
            evidence.schedulingSerial != result.schedulingSerial || evidence.viewportViewId != result.viewportViewId)
            continue;
        // Measure the completed worker interval directly. max(CPU submit, GPU
        // timestamps) misses the GPU tail after CPU preparation. Queue delay
        // and UI presentation cadence remain separate from this service cost.
        evidence.telemetry.completedServiceMs = result.telemetry.workerTotalMs;
        evidence.telemetry.workerTotalMs = result.telemetry.workerTotalMs;
        evidence.telemetry.queueWaitMs = result.telemetry.queueWaitMs;
        evidence.telemetry.superseded = result.telemetry.superseded;
        return;
    }
}

void EditorRenderWorker::PollViewportTimings() {
    for (auto it=m_PendingViewportTimings.begin();it!=m_PendingViewportTimings.end();) {
        Raw::ViewportGpuTiming::Measurement measurement;
        if (!Raw::ViewportGpuTiming::Poll(it->batch,measurement)) { ++it; continue; }
        auto& result=it->evidence;
        result.cpuStageCosts=measurement.cpu; result.gpuStageCosts=measurement.gpu;
        result.measuredStages=measurement.executed;
        result.telemetry.gpuServiceMs=measurement.gpuServiceMs;
        if (result.telemetry.completedServiceMs <= 0.0)
            result.telemetry.completedServiceMs=std::max(result.telemetry.cpuDispatchMs,measurement.gpuServiceMs);
        for (std::size_t i=0;i<result.stageCosts.size();++i)
            result.stageCosts[i]=std::max(measurement.cpu[i],measurement.gpu[i]);
        // Calibrate stage attribution against completed service time rather
        // than adding overlapping CPU and GPU timelines together.
        const double attributed=Raw::ViewportMeasuredWorkMs(result.stageCosts);
        if (attributed>result.telemetry.completedServiceMs && attributed>0)
            for (auto& cost : result.stageCosts) cost*=result.telemetry.completedServiceMs/attributed;
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!IsOwnerSnapshotInvalidLocked(result.ownerId,result.generation,result.schedulingSerial,result.rawRenderPurpose)) {
            if (m_CompletedViewportTimings.size()<64) m_CompletedViewportTimings.push(std::move(result));
        }
        it=m_PendingViewportTimings.erase(it);
    }
}
