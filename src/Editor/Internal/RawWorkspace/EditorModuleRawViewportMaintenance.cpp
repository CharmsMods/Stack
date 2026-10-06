#include "Editor/EditorModule.h"
#include "Editor/Internal/RawWorkspace/RawViewportAuxiliarySnapshot.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"
#include "Editor/RawRenderPlanning.h"
#include <imgui.h>

bool EditorModule::TrySubmitRawViewportMaintenance(double now) {
    if (m_RawViewportMaintenanceGeneration || m_RenderDirty || m_RenderPending || IsAnyRenderBackendBusy() ||
        !m_RawWorkspaceRootTabActive || !IsRawWorkspaceProjectActive() || IsRawWorkspaceUiInteractionActive() ||
        ImGui::IsAnyItemActive() || now - m_LastRenderDirtyTime < 0.25 ||
        m_RawWorkspaceFullResolutionPreviewPending || m_RawWorkspaceFullResolutionPreviewRequested ||
        m_RawWorkspaceAnalysisRequested || IsExportBusy() || IsRawWorkspaceProjectLoadBusy()) return false;
    const auto& recipe = RawViewportRecipe();
    const bool reconstruction = Stack::RawRecipe::IsRgbDenoiseActive(recipe.rgbDenoise) &&
        recipe.rgbDenoise.method != Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1;
    if (recipe.finishTone.layerJson.is_object() && recipe.finishTone.layerJson.value("autoCalibratePending", false)) return false;
    const auto content = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe, 0).postOutputCrop;
    const bool overview = m_RawViewportPresentedRegion.Valid() && m_RawViewportOverviewContent != content && m_RawViewportOverviewFailedContent != content;
    if (m_RawViewportWarmupEdge <= GetCalibratedRawViewportEdge()) m_RawViewportWarmupEdge = 0;
    if (!overview && (reconstruction || m_RawViewportWarmupEdge <= 0)) return false;
    const int edge = overview ? std::min(512, std::max(m_RawRenderSessionFullFrameWidth, m_RawRenderSessionFullFrameHeight))
        : m_RawViewportWarmupEdge;
    const auto generation = Stack::EditorRenderScheduling::NextGlobalGeneration();
    EditorRenderWorker::Snapshot snapshot;
    if (!TryBuildRenderSnapshot(generation, snapshot)) return false;
    Raw::ConfigureViewportAuxiliarySnapshot(snapshot, recipe,
        overview ? RawRenderPurpose::ViewportOverview : RawRenderPurpose::CachePrewarm, edge);
    if (reconstruction) {
        const auto native = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe,0).neutralPlacement;
        const auto cached = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe,m_RawViewportCachedRequestEdge).neutralPlacement;
        if (m_RawViewportNativeCachedStages[1] == native) snapshot.rawWorkspace.viewportDependencyEdge = 0;
        else if (m_RawViewportCachedStages[1] == cached) snapshot.rawWorkspace.viewportDependencyEdge = m_RawViewportCachedRequestEdge;
        else return false;
    }
    if (!overview) {
        int first = static_cast<int>(m_RawViewportEditStage);
        if (m_RawViewportRequest.visible.Partial()) first = std::min(first,static_cast<int>(Raw::ViewportFirstRegionalStage(recipe)));
        if (first <= 0) { m_RawViewportWarmupEdge = 0; return false; }
        const auto inputStage = static_cast<Raw::ViewportStage>(first-1);
        const int native = std::max(snapshot.width, snapshot.height);
        const auto decision = Raw::ResolveRawFullFramePreviewDecision(snapshot.width, snapshot.height,
            m_RawWorkspaceGlMaxTextureSize, m_RawWorkspaceVramWorkingBudgetBytes, m_RawWorkspaceMinimumMemoryTiling,16);
        if (edge >= native && !decision.allowed) { m_RawViewportWarmupEdge = 0; return false; }
        snapshot.rawWorkspace.cachePrewarmStage = inputStage;
        snapshot.rawWorkspace.cachePrewarmFingerprint = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint(
            recipe, edge >= native ? 0 : edge, inputStage);
        snapshot.rawWorkspace.cachePrewarmByteBudget = m_RawWorkspaceVramWorkingBudgetBytes / 4;
    }
    if (!Stack::EditorRendering::RawRenderService::Get().Submit(m_RawRenderClientId, std::move(snapshot))) return false;
    m_RawViewportMaintenanceGeneration = generation;
    m_RawViewportMaintenanceIsOverview = overview;
    if (!overview) m_RawViewportWarmupEdge = 0;
    m_RenderPending = true;
    return true;
}

void EditorModule::AdoptRawViewportMaintenance(EditorRenderWorker::Result& result) {
    if (result.generation != m_RawViewportMaintenanceGeneration) return;
    m_RawViewportMaintenanceGeneration = 0;
    if (!result.success && !result.telemetry.superseded && result.rawRenderPurpose == RawRenderPurpose::ViewportOverview)
        m_RawViewportOverviewFailedContent = result.rawWorkspace.presentationFingerprint;
    if (!result.success || result.telemetry.superseded ||
        result.rawWorkspace.sourceKey != GetActiveRawWorkspacePreviewIdentity()) return;
    if (result.rawRenderPurpose == RawRenderPurpose::ViewportOverview) {
        const auto content = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(RawViewportRecipe(), 0).postOutputCrop;
        if (content != result.rawWorkspace.presentationFingerprint || !result.outputTexture.texture) return;
        QueueViewportOutputTextureRelease(m_RawViewportOverviewTexture);
        m_RawViewportOverviewTexture = std::move(result.outputTexture);
        m_RawViewportOverviewContent = content;
    } else if (!IsRawWorkspaceUiInteractionActive()) {
        const int edge = GetCalibratedRawViewportEdge();
        if (edge > 0) m_RawWorkspaceAdaptivePreviewScale = static_cast<float>(edge) /
            std::max(1, m_RawWorkspacePhysicalViewportMaxDimension);
    }
}
