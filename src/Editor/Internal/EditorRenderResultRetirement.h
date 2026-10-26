#pragma once

#include "Editor/EditorRenderWorker.h"

#include <list>
#include <optional>
#include <type_traits>
#include <utility>

namespace Stack::EditorRendering::Internal {

inline void ReleaseRenderResultResources(EditorRenderWorker::Result& result) noexcept {
    result.outputTexture.Reset();
    result.viewportOverview.Reset();
    result.rawWorkspace.localRangeOverlayTexture.Reset();
    if (result.outputTiles.readyFence) {
        glDeleteSync(result.outputTiles.readyFence);
        result.outputTiles.readyFence = nullptr;
    }
    for (auto& tile : result.outputTiles.tiles) {
        if (tile.texture != 0) {
            glDeleteTextures(1, &tile.texture);
            tile.texture = 0;
        }
    }
    result.outputTiles.tiles.clear();
}

class RenderResultResourceGuard {
public:
    explicit RenderResultResourceGuard(EditorRenderWorker::Result& result) : m_Result(result) {}
    ~RenderResultResourceGuard() { ReleaseRenderResultResources(m_Result); }
    RenderResultResourceGuard(const RenderResultResourceGuard&) = delete;
    RenderResultResourceGuard& operator=(const RenderResultResourceGuard&) = delete;
private:
    EditorRenderWorker::Result& m_Result;
};

// Detach under the scheduling lock, then retire on the same GL context after
// unlocking. Splicing owns the existing nodes without allocation or a backlog.
// Keep this scope inside the lifetime of the current GL context.
class RenderResultRetirement {
public:
    using Result = EditorRenderWorker::Result;
    using Results = std::list<Result>;

    RenderResultRetirement() = default;
    RenderResultRetirement(const RenderResultRetirement&) = delete;
    RenderResultRetirement& operator=(const RenderResultRetirement&) = delete;

    ~RenderResultRetirement() {
        for (auto& result : results) ReleaseRenderResultResources(result);
        if (fallback) ReleaseRenderResultResources(*fallback);
        if (unpublished) ReleaseRenderResultResources(*unpublished);
    }

    void Take(Results& source, Results::iterator result) noexcept {
        results.splice(results.end(), source, result);
    }

    void TakeFallback(std::optional<Result>& source) noexcept {
        if (!source) return;
        MoveInto(fallback, *source);
        source.reset();
    }

    void TakeUnpublished(Result& source) noexcept {
        MoveInto(unpublished, source);
    }

    Results results;

private:
    static void MoveInto(std::optional<Result>& destination, Result& source) noexcept {
        static_assert(std::is_nothrow_move_constructible_v<Result>);
        static_assert(std::is_nothrow_move_assignable_v<Result>);
        destination.emplace(std::move(source));
        // Tile sets carry raw names rather than a destructor. Their ordinary
        // move transfers the vector but copies this fence handle.
        source.outputTiles = {};
    }

    std::optional<Result> fallback, unpublished;
};

} // namespace Stack::EditorRendering::Internal
