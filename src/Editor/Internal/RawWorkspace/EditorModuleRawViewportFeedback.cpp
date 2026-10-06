#include "Editor/EditorModule.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"

void EditorModule::ObserveRawViewportFeedback(const EditorRenderWorker::Result& result) {
    if (!result.timingOnly || result.rawRenderPurpose != RawRenderPurpose::InteractivePresentation ||
        !result.telemetry.interactionActive || !m_RawWorkspaceAdaptiveGestureActive ||
        result.telemetry.timingWorkload != m_RawViewportAdaptiveWorkload ||
        result.telemetry.gestureId != m_RawViewportGestureId) return;
    m_RawViewportController.SetContext(m_RawViewportAdaptiveWorkload);
    m_RawViewportController.Observe(result.telemetry.timingContext,result.telemetry.predictedMs,
        result.telemetry.completedServiceMs);
    m_RawWorkspaceAdaptiveFrameTimeMs = result.telemetry.completedServiceMs;
    const int edge=GetCalibratedRawViewportEdge();
    if (edge<=0) return;
    const float desired=float(edge)/std::max(1,m_RawWorkspacePhysicalViewportMaxDimension);
    m_RawWorkspaceAdaptivePreviewScale=Stack::EditorRenderScheduling::StabilizeRawPreviewScale(
        m_RawWorkspaceAdaptivePreviewScale,desired,m_RawWorkspacePreviewHealthyStreak,
        m_RawWorkspacePreviewSlowSamples,m_RawWorkspacePreviewScaleCooldown,true);
}
