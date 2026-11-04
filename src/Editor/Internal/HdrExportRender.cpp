#include "Editor/Internal/HdrExportRender.h"
#include "Hdr/Color.h"
#include "Hdr/GraphView.h"
#include "Renderer/RenderPipeline.h"
#include "Utils/PixelBufferUtils.h"
#include <algorithm>

namespace Stack::EditorRendering {
bool ReadHdrExport(RenderPipeline& pipeline, const RenderGraphSnapshot& graph,
    Hdr::FileFormat format, EditorRenderWorker::Result& result) {
    const auto render = [&](float headroom, bool encodeSrgb) {
        auto rendition = graph;
        if (!Hdr::ConfigureView(rendition, headroom, encodeSrgb)) {
            result.error = "HDR export needs a color image with a supported final View or a declared HDR output.";
            return false;
        }
        pipeline.ExecuteGraph(rendition);
        const auto& failure = pipeline.GetLastGraphExecutionStats().lastSpecializedFailure;
        if (!failure.empty()) {
            result.error = failure;
            return false;
        }
        if (!pipeline.GetOutputTexture() || pipeline.GetLastGraphExecutionStats().allocationFailed ||
            pipeline.IsOutputDenoisePending()) {
            result.error = "The native-resolution export rendition is incomplete.";
            return false;
        }
        return true;
    };
    if (format == Hdr::FileFormat::GainMapJpeg) {
        if (!render(1.f, true)) return false;
        result.pixels = pipeline.GetOutputPixelsTiledPbo(result.width, result.height);
        if (!PixelBuffer::HasCompletePixelBuffer(result.pixels.size(), result.width, result.height, 4)) {
            result.error = "Could not read the SDR base of the gain-map JPEG.";
            return false;
        }
    }
    if (!render(Hdr::MasterHeadroom, false)) return false;
    auto& image = result.hdrExportImage;
    image.rgba = pipeline.GetOutputFloatPixels(image.width, image.height);
    if (!PixelBuffer::HasCompletePixelBuffer(image.rgba.size(), image.width, image.height, 4) ||
        (format == Hdr::FileFormat::GainMapJpeg && (result.width != image.width || result.height != image.height))) {
        result.error = "Could not read the full-resolution HDR rendition.";
        return false;
    }
    const auto stride = static_cast<std::size_t>(image.width) * 4;
    for (int y = 0; y < image.height / 2; ++y) {
        auto row = image.rgba.begin() + y * stride;
        std::swap_ranges(row, row + stride, image.rgba.begin() + (image.height - y - 1) * stride);
    }
    result.width = image.width;
    result.height = image.height;
    return true;
}
}
