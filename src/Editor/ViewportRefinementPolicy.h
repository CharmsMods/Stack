#pragma once

#include "Renderer/ViewportPreviewPolicy.h"

namespace Stack::EditorRendering {

// Match the current display before spending time on source pixels that are
// smaller than a display pixel. Include one physical pixel for raster rounding.
inline int ViewportScreenEdge(const Renderer::ImageRegion& visible,
    int physicalWidth, int physicalHeight) {
    if (!visible.Valid() || physicalWidth <= 0 || physicalHeight <= 0) return 0;
    const int nativeEdge = std::max(visible.fullWidth, visible.fullHeight);
    const double scale = std::min(1.0, std::max(
        (double(physicalWidth) + 1.0) / visible.width,
        (double(physicalHeight) + 1.0) / visible.height));
    return std::clamp(int(std::ceil(nativeEdge * scale)), 1, nativeEdge);
}

// Offscreen coverage cannot grow into another unbounded native render at high
// zoom. Spend at most four visible rasters and eight megapixels; the worker's
// ordinary dependency/memory admission remains authoritative.
inline int ViewportNavigationEdge(const Renderer::ImageRegion& visible,
    int physicalWidth, int physicalHeight) {
    const int screenEdge = ViewportScreenEdge(visible, physicalWidth, physicalHeight);
    if (!screenEdge) return 0;
    const int nativeEdge = std::max(visible.fullWidth, visible.fullHeight);
    const double scale = double(screenEdge) / nativeEdge;
    const double maximumPixels = std::min(8.0 * 1024 * 1024,
        4.0 * visible.width * visible.height * scale * scale);
    const double fullPixels = double(visible.fullWidth) * visible.fullHeight;
    return std::max(1, std::min(screenEdge,
        int(std::floor(nativeEdge * std::sqrt(std::min(1.0, maximumPixels / fullPixels))))));
}

inline bool ViewportScreenCovered(const Renderer::ViewportPreviewGeometry& geometry,
    const Renderer::ImageRegion& visible, int physicalWidth, int physicalHeight,
    bool navigating, double minimumDetail = 0.0) {
    // Small zoom changes can reuse useful pixels while the camera is moving.
    // The user's detail floor is still a floor; stopping requires full display
    // density again. Coverage itself is never relaxed.
    const double detail = navigating ? std::clamp(std::max(.8, minimumDetail), .8, 1.0) : 1.0;
    return Renderer::ViewportPreviewCoversDisplay(geometry, visible,
        std::max(1, int(std::ceil(physicalWidth * detail))),
        std::max(1, int(std::ceil(physicalHeight * detail))));
}

inline bool ViewportWholeCoverageReady(const Renderer::ViewportPreviewGeometry& geometry, int edge) {
    return edge > 0 && geometry.Valid() && !geometry.resident.Partial() &&
        std::max(geometry.resident.fullWidth, geometry.resident.fullHeight) >= edge;
}

struct ViewportRefinementDemand {
    bool screenReady = false;
    bool navigationCoverageReady = false;
    bool navigating = false;
    bool cameraQuiet = false;
};

inline Renderer::ViewportRefinementStep SelectViewportRefinementStep(const ViewportRefinementDemand& demand) {
    using Step = Renderer::ViewportRefinementStep;
    if (!demand.screenReady) return Step::VisibleScreen;
    if (demand.navigating || !demand.cameraQuiet) return Step::None;
    if (!demand.navigationCoverageReady) return Step::NavigationCoverage;
    return Step::NativeSection;
}

// Only a new visible-screen request interrupts useful background refinement.
// Moving the camera does not repeatedly cancel an already-running screen job.
inline bool SupersedesViewportRefinement(const Renderer::ViewportPreviewRequest& active,
    const Renderer::ViewportPreviewRequest& pending) {
    using Step = Renderer::ViewportRefinementStep;
    return active.enabled && pending.enabled && !pending.empty &&
        pending.refinement == Step::VisibleScreen &&
        (active.refinement == Step::NativeSection || active.refinement == Step::NavigationCoverage);
}

} // namespace Stack::EditorRendering
