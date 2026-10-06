#include "Editor/EditorRenderWorker.h"

bool EditorRenderWorker::TryConsumeViewportTiming(Result& result) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_CompletedViewportTimings.empty()) return false;
    result=std::move(m_CompletedViewportTimings.front());
    m_CompletedViewportTimings.pop();
    return true;
}

void EditorRenderWorker::CaptureViewportTiming(const Snapshot& snapshot, Result& result) {
    if (!m_PersistentPipeline || snapshot.rawRenderPurpose == RawRenderPurpose::ViewportCalibration ||
        snapshot.rawWorkspace.sourceKey.empty()) return;
    auto batch=m_PersistentPipeline->m_RawViewportGpuTiming.Capture();
    if (!result.success || result.telemetry.superseded || snapshot.graphRequest.enabled ||
        (snapshot.rawRenderPurpose != RawRenderPurpose::InteractivePresentation &&
         snapshot.rawRenderPurpose != RawRenderPurpose::ViewportRefinement) ||
        snapshot.rawWorkspace.analysisRequested || result.rawWorkspace.viewportDiagnostic ||
        snapshot.rawWorkspace.graphScopeStage != RawDevelopmentGraphScopeStage::None ||
        snapshot.previews.size() || snapshot.developCandidateRenders.size()) {
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
    evidence.schedulingSerial=result.schedulingSerial;
    evidence.rawRenderPurpose=result.rawRenderPurpose;
    evidence.telemetry=result.telemetry;
    evidence.telemetry.cpuDispatchMs=result.telemetry.workerTotalMs;
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
    evidence.timingNativeWidth=snapshot.rawWorkspace.fullFrameWidth;
    evidence.timingNativeHeight=snapshot.rawWorkspace.fullFrameHeight;
    evidence.timingOutputWidth=result.outputTexture.width;
    evidence.timingOutputHeight=result.outputTexture.height;
    evidence.timingRepresentation=result.timingRepresentation;
    m_PendingViewportTimings.push_back(std::move(pending));
}
void EditorRenderWorker::PollViewportTimings() {
    for (auto it=m_PendingViewportTimings.begin();it!=m_PendingViewportTimings.end();) {
        Raw::ViewportGpuTiming::Measurement measurement;
        if (!Raw::ViewportGpuTiming::Poll(it->batch,measurement)) { ++it; continue; }
        auto& result=it->evidence;
        result.cpuStageCosts=measurement.cpu; result.gpuStageCosts=measurement.gpu;
        result.measuredStages=measurement.executed;
        result.telemetry.gpuServiceMs=measurement.gpuServiceMs;
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
