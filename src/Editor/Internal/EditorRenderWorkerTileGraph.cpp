#include "Editor/Internal/EditorRenderWorkerTileGraph.h"

#include "Utils/PixelBufferUtils.h"
#include "Utils/HashUtils.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Stack::EditorRenderWorkerTiles {
namespace {
// The linear master is always packed RGBA, independently of the byte layout.
std::shared_ptr<const std::vector<float>> CropFloatMaster(
    const std::vector<float>& source, int sourceWidth, int sourceHeight,
    const RenderTileRect& tile) noexcept {
    std::size_t sourceElements=0, tileElements=0;
    if (!PixelBuffer::TryComputePixelElementCount(sourceWidth,sourceHeight,4,sourceElements) ||
        source.size()<sourceElements ||
        !PixelBuffer::TryComputePixelElementCount(tile.haloWidth,tile.haloHeight,4,tileElements) ||
        tileElements>std::numeric_limits<std::size_t>::max()/sizeof(float)) return {};
    try {
        auto cropped=std::make_shared<std::vector<float>>(tileElements);
        const auto sourceStride=static_cast<std::size_t>(sourceWidth)*4;
        const auto tileStride=static_cast<std::size_t>(tile.haloWidth)*4;
        for(int row=0;row<tile.haloHeight;++row) {
            const auto sourceOffset=static_cast<std::size_t>(tile.haloY+row)*sourceStride+
                static_cast<std::size_t>(tile.haloX)*4;
            std::copy_n(source.data()+sourceOffset,tileStride,cropped->data()+static_cast<std::size_t>(row)*tileStride);
        }
        return cropped;
    } catch(const std::bad_alloc&) { return {}; }
      catch(const std::length_error&) { return {}; }
}
}


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
            node.image.channels,
            node.image.hdrPixels
        });
    }
    m_Topology =
        Renderer::GraphExecution::BuildGraphTopologyIndex(
            m_Graph);
}

bool TileGraphBatch::Prepare(
    const RenderTileRect& tile) noexcept {
    // Unlike the standalone byte helper, a graph tile cannot silently clamp:
    // its payload dimensions must exactly match the requested halo rectangle.
    if (m_FullWidth<=0 || m_FullHeight<=0 || tile.haloX<0 || tile.haloY<0 ||
        tile.haloX>=m_FullWidth || tile.haloY>=m_FullHeight ||
        tile.haloWidth<=0 || tile.haloHeight<=0 ||
        tile.haloWidth>m_FullWidth-tile.haloX || tile.haloHeight>m_FullHeight-tile.haloY) return false;
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
        std::shared_ptr<const std::vector<float>> master;
        std::size_t masterFingerprint=0;
        if(source.hdrPixels) {
            master=CropFloatMaster(*source.hdrPixels,m_FullWidth,m_FullHeight,tile);
            if(!master) return false;
            masterFingerprint=StackHash::HashBytes(reinterpret_cast<const unsigned char*>(master->data()),
                master->size()*sizeof(float));
        }
        RenderGraphImagePayload& image =
            m_Graph.nodes[source.nodeIndex].image;
        image.pixels = std::move(cropped);
        image.hdrPixels=std::move(master);
        image.hdrFingerprint=masterFingerprint;
        image.width = tile.haloWidth;
        image.height = tile.haloHeight;
    }
    return m_Topology.IsBoundTo(m_Graph);
}

} // namespace Stack::EditorRenderWorkerTiles
