#include "Editor/EditorModule.h"
#include "Raw/RawViewportDetail.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

void EditorModule::UpdateRawViewportCoverage(ImVec2 imageMin, ImVec2 imageMax,
    ImVec2 viewMin, ImVec2 viewMax, bool currentPreview) {
    ImRect coverage(viewMin, viewMax);
    if (currentPreview && !m_PermanentGalleryWorkspace && !IsBracketingToolActive() &&
        ImGui::GetWindowViewport() == ImGui::GetMainViewport()) {
        // The photo beneath controls is part of the visible render demand.
        // Interaction and fitting still belong to the right-hand editing area.
        const auto* viewport = ImGui::GetMainViewport();
        coverage = ImRect(viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x,
            viewport->Pos.y + viewport->Size.y));
    }
    coverage.ClipWithFull(ImRect(imageMin, imageMax));
    // The generated surround can be navigated with no real photo on-screen.
    // Keep its accepted source instead of requesting a fabricated one-pixel
    // native crop or changing render demand to an empty intersection.
    if (coverage.GetWidth() <= 0.f || coverage.GetHeight() <= 0.f) return;
    UpdateRawWorkspaceInteractivePreviewDimension(ImVec2(
        std::max(1.f, coverage.GetWidth()), std::max(1.f, coverage.GetHeight())));
    if (!currentPreview || m_ViewportOutputExpectedNativeWidth <= 0 ||
        m_ViewportOutputExpectedNativeHeight <= 0 || imageMax.x <= imageMin.x || imageMax.y <= imageMin.y) return;

    const int width = m_ViewportOutputExpectedNativeWidth;
    const int height = m_ViewportOutputExpectedNativeHeight;
    const int left = std::clamp(int(std::floor((coverage.Min.x - imageMin.x) / (imageMax.x - imageMin.x) * width)), 0, width - 1);
    const int top = std::clamp(int(std::floor((coverage.Min.y - imageMin.y) / (imageMax.y - imageMin.y) * height)), 0, height - 1);
    const int right = std::clamp(int(std::ceil((coverage.Max.x - imageMin.x) / (imageMax.x - imageMin.x) * width)), left + 1, width);
    const int bottom = std::clamp(int(std::ceil((coverage.Max.y - imageMin.y) / (imageMax.y - imageMin.y) * height)), top + 1, height);
    const Raw::ViewportRegion next{width, height, left, top, right - left, bottom - top};
    if (m_RawViewportRequest.visible == next) return;

    const auto previous = m_RawViewportRequest.visible;
    m_RawViewportRequest.visible = next;
    ++m_RawViewportRequest.generation;
    ClearRawViewportTransition();
    UpdateRawViewportEditTiming(RawViewportRecipe());
    if (!m_RawViewportLastPresentation.diagnostic &&
        HasRawWorkspaceFullResolutionPreviewForSource(GetActiveRawWorkspacePreviewIdentity())) {
        m_RawViewportPresentedGeneration = m_RawViewportRequest.generation;
        m_RawViewportLastPresentation.view = m_RawViewportRequest.generation;
        return;
    }
    if (!m_RawViewportLastPresentation.diagnostic &&
        HasRawWorkspaceCurrentPresentationForSource(GetActiveRawWorkspacePreviewIdentity())) {
        const auto covers = [&](const auto& texture, const Raw::ViewportRegion& region) {
            return IsViewportTextureSafeForDrawing(texture.texture) &&
                Raw::ViewportDetailCoversDisplayRegion(region, texture.width, texture.height, next,
                    m_RawViewportPhysicalWidth, m_RawViewportPhysicalHeight);
        };
        const bool retained = m_RawViewportDetailContent &&
            m_RawViewportDetailContent == m_RawViewportPresentationContent &&
            covers(m_RawViewportDetailTexture, m_RawViewportDetailRegion);
        if (covers(m_RawWorkspacePresentationTexture, m_RawViewportPresentedRegion) || retained) {
            m_RawViewportPresentedGeneration = m_RawViewportRequest.generation;
            m_RawViewportLastPresentation.view = m_RawViewportRequest.generation;
            return;
        }
    }
    m_RawWorkspaceFullResolutionPreviewPending = true;
    m_RawWorkspaceFullResolutionPreviewRequested = false;
    if ((previous.width != next.width || previous.height != next.height) &&
        BuildRawViewportDecisionInput().regionalAllowed) {
        // A smaller visible area may fit a budget that rejected the old view.
        // Full-raster graphs have unchanged allocation requirements when the
        // view moves. Only a regional executor can retry on a smaller crop.
        m_RawWorkspaceFullResolutionPreviewDeferredByBudget = false;
        m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes = 0;
    }
    // Navigation changes coverage, not pixels. Keep the accepted frame and its
    // detail while the existing settled-refinement path fills missing coverage.
    // Actual edits already invalidate rendering through their own action path.
    // Diagnostic views retain their ordinary view-dependent refresh behavior.
    if (m_RawViewportLastPresentation.diagnostic) MarkRenderRefreshDirty();
}
