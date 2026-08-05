#pragma once

#include "Renderer/Internal/RenderPipelineGraphSchedule.h"
#include "Renderer/RenderTiling.h"
#include "Utils/SharedPixelBuffer.h"

#include <cstddef>
#include <vector>

namespace Stack::EditorRenderWorkerTiles {

SharedPixelBuffer CropSharedPixelBuffer(
    const SharedPixelBuffer& source,
    int sourceWidth,
    int sourceHeight,
    int channels,
    int x,
    int y,
    int width,
    int height) noexcept;

class TileGraphBatch {
public:
    TileGraphBatch(
        const RenderGraphSnapshot& graph,
        int fullWidth,
        int fullHeight);

    TileGraphBatch(const TileGraphBatch&) = delete;
    TileGraphBatch& operator=(const TileGraphBatch&) = delete;
    TileGraphBatch(TileGraphBatch&&) = delete;
    TileGraphBatch& operator=(TileGraphBatch&&) = delete;

    bool Prepare(const RenderTileRect& tile) noexcept;

    const RenderGraphSnapshot& Graph() const noexcept {
        return m_Graph;
    }

    const Renderer::GraphExecution::GraphTopologyIndex&
    Topology() const noexcept {
        return m_Topology;
    }

private:
    struct FullFrameImageSource {
        std::size_t nodeIndex = 0;
        SharedPixelBuffer pixels;
        int channels = 0;
    };

    RenderGraphSnapshot m_Graph;
    int m_FullWidth = 0;
    int m_FullHeight = 0;
    std::vector<FullFrameImageSource> m_ImageSources;
    Renderer::GraphExecution::GraphTopologyIndex m_Topology;
};

} // namespace Stack::EditorRenderWorkerTiles
