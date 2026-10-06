#include "Editor/EditorModule.h"
#include <imgui.h>

void EditorModule::ClearRawViewportTransition() {
    QueueViewportOutputTextureRelease(m_RawViewportPreviousTexture);
    m_RawViewportFadeStarted = -1.0;
    m_RawViewportFadeDuration = 0.0;
}

void EditorModule::PrepareRawViewportTransition(const EditorRenderWorker::Result& result) {
    const double now = ImGui::GetTime();
    Raw::ViewportPresentation next;
    next.source = result.rawWorkspace.sourceKey;
    next.gesture = result.telemetry.gestureId;
    next.view = result.rawWorkspace.viewportGeneration;
    next.region = result.rawWorkspace.viewportRegion;
    next.stage = result.editStage;
    next.width = result.outputTexture.width;
    next.height = result.outputTexture.height;
    next.interactive = result.telemetry.interactionActive && m_RawWorkspaceAdaptiveGestureActive &&
        result.rawRenderPurpose == RawRenderPurpose::InteractivePresentation;
    next.encodedSrgb = result.rawWorkspace.viewportEncodedSrgb;
    next.diagnostic = result.rawRenderPurpose == RawRenderPurpose::ExplicitInspection ||
        result.rawWorkspace.viewportDiagnostic || m_RawWorkspaceLabUi.colorWarpInspectionActive ||
        m_RawWorkspaceLabUi.colorWarpDiagnosticLocked || m_RawWorkspaceLabUi.colorWarpLightnessRangePreviewActive ||
        (m_RawWorkspaceLabUi.activeTool == RawLabTool::Color && (ImGui::IsKeyDown(ImGuiKey_H) ||
            GetRawWorkspaceColorWarpViewMode(RawViewportRecipe()) == 2));
    const double sampleInterval = std::min((now-m_RawViewportPresentationTime)*1000.0,
        result.telemetry.queueWaitMs+result.telemetry.workerTotalMs);
    if (m_RawViewportLastPresentation.source != next.source || m_RawViewportLastPresentation.view != next.view)
        m_RawViewportCadenceMs = 0;
    if (std::isfinite(sampleInterval) && sampleInterval > 0)
        m_RawViewportCadenceMs = m_RawViewportCadenceMs > 0 ?
            0.65*m_RawViewportCadenceMs+0.35*sampleInterval : sampleInterval;
    const double duration = Raw::ViewportFadeDuration(m_RawViewportLastPresentation,next,
        (now-m_RawViewportPresentationTime)*1000.0,
        result.telemetry.queueWaitMs+result.telemetry.workerTotalMs,GetRawViewportFadeBelowFps(),
        m_RawViewportCadenceMs,ImGui::GetIO().DeltaTime*1000.0);
    const bool active = m_RawViewportPreviousTexture.texture && m_RawViewportFadeDuration > 0;
    const double progress = active && m_RawViewportFadeStarted >= 0 ?
        std::clamp((now-m_RawViewportFadeStarted)/m_RawViewportFadeDuration,0.0,1.0) : 0;
    const bool keepBase = active && progress < 1 && m_RawViewportFadeBase.source == next.source &&
        m_RawViewportFadeBase.view == next.view && Raw::MapViewportFade(m_RawViewportFadeBase.region,next.region).valid;
    const auto& retained = keepBase ? m_RawViewportPreviousTexture : m_RawWorkspacePresentationTexture;
    const auto bytes = std::uint64_t(std::max(0,retained.width))*std::max(0,retained.height)*8u;
    const bool room = bytes > 0 && bytes <= m_RawWorkspaceVramWorkingBudgetBytes/8;
    if (GetSmoothRawViewportUpdates() && room && duration > 0) {
        if (!keepBase) {
            ClearRawViewportTransition();
            m_RawViewportPreviousTexture = std::move(m_RawWorkspacePresentationTexture);
            m_RawViewportFadeBase = m_RawViewportLastPresentation;
        }
        m_RawViewportFadeDuration = duration;
        // Pending fades start on their first actual viewport draw. Retargeting
        // keeps the blend's progress and base instead of flashing its old target.
        m_RawViewportFadeStarted = keepBase && m_RawViewportFadeStarted >= 0 ? now-progress*duration : -1.0;
        m_RawViewportFadeDecision = keepBase ? "retargeted" : "pending-first-draw";
    } else {
        ClearRawViewportTransition();
        m_RawViewportFadeDecision = !GetSmoothRawViewportUpdates() ? "disabled" :
            !bytes ? "no-previous-texture" : !room ? "memory-budget" : "context-or-cadence";
    }
    m_RawViewportLastPresentation = std::move(next);
    m_RawViewportPresentationTime = now;
}

bool EditorModule::DrawRawViewportTransition(ImDrawList* list, ImVec2 minimum, ImVec2 maximum) {
    if (m_RawViewportFadeDuration <= 0) return false;
    if (m_RawViewportFadeStarted < 0) m_RawViewportFadeStarted = ImGui::GetTime();
    m_RawViewportFadeDecision = "drawing";
    const double elapsed = ImGui::GetTime() - m_RawViewportFadeStarted;
    const bool diagnostic = m_RawWorkspaceLabUi.colorWarpInspectionActive ||
        m_RawWorkspaceLabUi.colorWarpDiagnosticLocked || m_RawWorkspaceLabUi.colorWarpLightnessRangePreviewActive ||
        (m_RawWorkspaceLabUi.activeTool == RawLabTool::Color && (ImGui::IsKeyDown(ImGuiKey_H) ||
            GetRawWorkspaceColorWarpViewMode(RawViewportRecipe()) == 2));
    if (!GetSmoothRawViewportUpdates() || diagnostic || m_RawViewportFadeDuration <= 0 ||
        elapsed >= m_RawViewportFadeDuration || m_RawWorkspaceLabUi.previewPanning ||
        m_RawWorkspaceLabUi.previewZoomAnimating ||
        !IsViewportTextureSafeForDrawing(m_RawViewportPreviousTexture.texture)) {
        ClearRawViewportTransition();
        return false;
    }
    const auto map = Raw::MapViewportFade(m_RawViewportFadeBase.region,m_RawViewportLastPresentation.region);
    const bool drawn = m_RawViewportFadeRenderer.Draw(list, m_RawViewportPreviousTexture.texture,
        m_RawWorkspacePresentationTexture.texture, minimum, maximum,
        static_cast<float>(std::clamp(elapsed / m_RawViewportFadeDuration, 0.0, 1.0)),
        m_RawViewportLastPresentation.encodedSrgb,ImVec2(0,1),ImVec2(1,0),IM_COL32_WHITE,0,
        ImVec2(float(map.x),float(map.y)),ImVec2(float(map.width),float(map.height)));
    // The whole-window photo may borrow this fade immediately afterward.
    // If the viewport fell back to an ordinary image, it must do the same.
    if (!drawn) ClearRawViewportTransition();
    return drawn;
}

bool EditorModule::AdoptRawViewportFrame(EditorRenderWorker::Result& result) {
    // Check the replacement before transferring the accepted frame into a fade.
    auto& texture = result.outputTexture;
    if (!IsViewportTextureSafeForDrawing(texture.texture) || texture.width <= 0 || texture.height <= 0 ||
        !texture.EnsureLease(Raw::RawGpuImageFamily::Presentation)) return false;
    RetainRawViewportDetail(result);
    PrepareRawViewportTransition(result);
    return AdoptRawWorkspacePresentationTexture(texture,result.rawWorkspace.viewportRegion);
}
