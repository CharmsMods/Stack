#include "Editor/EditorRenderWorker.h"
#include "Renderer/ViewportTextureCopy.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kViewportOverviewEdge = 512;

} // namespace

void EditorRenderWorker::PrepareViewportOverview(const Snapshot& snapshot, Result& result) {
    if (!snapshot.prepareViewportOverview || !result.success ||
        !result.viewportPreview.enabled || result.viewportPreview.empty ||
        !result.viewportPreviewGeometry.Valid() || !m_PersistentPipeline ||
        result.outputTexture.texture == 0 || ShouldAbortStaleSnapshot(snapshot)) return;

    // Optional coverage must not turn a completed main result into a failure.
    // Its texture owns a separate immutable lease and readiness fence.
    try {
        // A whole-view result already contains the current edits. Preserve a
        // small independent lease before later regional results replace it.
        if (!result.viewportPreviewGeometry.resident.Partial()) {
            const auto& source = result.outputTexture;
            const double scale = std::min(1.0,double(kViewportOverviewEdge)/std::max(source.width,source.height));
            SharedTextureResult overview;
            overview.width=std::max(1,int(std::lround(source.width*scale)));
            overview.height=std::max(1,int(std::lround(source.height*scale)));
            overview.texture=Stack::Renderer::CopyViewportTexture(source.texture,source.width,source.height,overview.width,overview.height);
            if (source.hdr) {
                const auto texture=Stack::Renderer::CopyViewportTexture(source.hdr.Texture(),source.width,source.height,overview.width,overview.height);
                overview.hdr=Raw::RawGpuImageLease::AdoptOwned(texture,overview.width,overview.height,Raw::RawGpuImageFamily::Presentation,16);
            }
            if (!overview.EnsureLease() || (source.hdr && !overview.hdr)) return;
            overview.readyFence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
            if (!overview.readyFence) return;
            glFlush(); result.viewportOverview=std::move(overview);
            return;
        }
        // Regional foreground work is published immediately. The editor's
        // NavigationCoverage step schedules a bounded whole-image render on a
        // later worker turn; optional coverage cannot hold this frame hostage.
    } catch (...) {
        // The detail result remains adoptable. The caller may request its
        // missing coverage again after a later completed foreground render.
    }
}
