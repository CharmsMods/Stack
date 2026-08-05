#include "Editor/Internal/EditorRenderWorkerTileGraph.h"

#include "Utils/PixelBufferUtils.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>
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
    int height) noexcept {
    if (source.empty() ||
        sourceWidth <= 0 ||
        sourceHeight <= 0 ||
        width <= 0 ||
        height <= 0 ||
        !PixelBuffer::IsSupportedInterleavedChannelCount(
            channels) ||
        !PixelBuffer::HasCompletePixelBuffer(
            source.size(),
            sourceWidth,
            sourceHeight,
            channels)) {
        return {};
    }

    x = std::clamp(x, 0, sourceWidth - 1);
    y = std::clamp(y, 0, sourceHeight - 1);
    width = std::clamp(width, 1, sourceWidth - x);
    height = std::clamp(height, 1, sourceHeight - y);

    std::size_t croppedBytes = 0;
    std::size_t sourceRowBytes = 0;
    std::size_t croppedRowBytes = 0;
    if (!PixelBuffer::TryComputePixelByteCount(
            width,
            height,
            channels,
            croppedBytes) ||
        !PixelBuffer::TryComputePixelByteCount(
            sourceWidth,
            1,
            channels,
            sourceRowBytes) ||
        !PixelBuffer::TryComputePixelByteCount(
            width,
            1,
            channels,
            croppedRowBytes)) {
        return {};
    }

    try {
        std::vector<unsigned char> cropped(croppedBytes);
        const unsigned char* sourceData = source.data();
        const std::size_t sourceXOffset =
            static_cast<std::size_t>(x) *
            static_cast<std::size_t>(channels);
        for (int row = 0; row < height; ++row) {
            const std::size_t sourceOffset =
                static_cast<std::size_t>(y + row) *
                    sourceRowBytes +
                sourceXOffset;
            const std::size_t destinationOffset =
                static_cast<std::size_t>(row) *
                croppedRowBytes;
            std::memcpy(
                cropped.data() + destinationOffset,
                sourceData + sourceOffset,
                croppedRowBytes);
        }
        return MakeSharedPixelBufferOwned(
            std::move(cropped));
    } catch (const std::bad_alloc&) {
        return {};
    } catch (const std::length_error&) {
        return {};
    }
}

TileGraphBatch::TileGraphBatch(
    const RenderGraphSnapshot& graph,
    int fullWidth,
    int fullHeight)
    : m_Graph(graph),
      m_FullWidth(fullWidth),
      m_FullHeight(fullHeight) {
    m_ImageSources.reserve(m_Graph.nodes.size());
    for (std::size_t nodeIndex = 0;
         nodeIndex < m_Graph.nodes.size();
         ++nodeIndex) {
        const RenderGraphNode& node =
            m_Graph.nodes[nodeIndex];
        if (node.kind != RenderGraphNodeKind::Image ||
            node.image.pixels.empty() ||
            node.image.width != m_FullWidth ||
            node.image.height != m_FullHeight) {
            continue;
        }
        m_ImageSources.push_back(FullFrameImageSource{
            nodeIndex,
            node.image.pixels,
            node.image.channels
        });
    }
    m_Topology =
        Renderer::GraphExecution::BuildGraphTopologyIndex(
            m_Graph);
}

bool TileGraphBatch::Prepare(
    const RenderTileRect& tile) noexcept {
    for (const FullFrameImageSource& source :
         m_ImageSources) {
        SharedPixelBuffer cropped =
            CropSharedPixelBuffer(
                source.pixels,
                m_FullWidth,
                m_FullHeight,
                source.channels,
                tile.haloX,
                tile.haloY,
                tile.haloWidth,
                tile.haloHeight);
        if (cropped.empty()) {
            return false;
        }
        RenderGraphImagePayload& image =
            m_Graph.nodes[source.nodeIndex].image;
        image.pixels = std::move(cropped);
        image.width = tile.haloWidth;
        image.height = tile.haloHeight;
    }
    return m_Topology.IsBoundTo(m_Graph);
}

} // namespace Stack::EditorRenderWorkerTiles
