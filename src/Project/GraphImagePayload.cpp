#include "Project/GraphImagePayload.h"
#include "Utils/HashUtils.h"
#include "Utils/PixelBufferUtils.h"

namespace Stack::Project {
SharedPixelBuffer EnsureSharedImagePixels(const EditorNodeGraph::ImagePayload& payload) {
    const auto payloadIsComplete = [&](std::size_t availableBytes) {
        return Stack::PixelBuffer::HasCompletePixelBuffer(
            availableBytes,
            payload.width,
            payload.height,
            payload.channels);
    };
    if (payload.width <= 0 || payload.height <= 0 ||
        !Stack::PixelBuffer::IsSupportedInterleavedChannelCount(payload.channels)) {
        payload.sharedPixels.reset();
        payload.pixelsFingerprint = 0;
        return {};
    }

    if (payload.pixels.empty()) {
        if (!payload.sharedPixels || payload.sharedPixels->empty() ||
            !payloadIsComplete(payload.sharedPixels->size())) {
            payload.sharedPixels.reset();
            payload.pixelsFingerprint = 0;
            return {};
        }
        if (payload.pixelsFingerprint == 0) {
            payload.pixelsFingerprint = StackHash::HashBytes(*payload.sharedPixels);
        }
        return MakeSharedPixelBufferAlias(payload.sharedPixels, payload.pixelsFingerprint);
    }

    if (!payloadIsComplete(payload.pixels.size())) {
        payload.sharedPixels.reset();
        payload.pixelsFingerprint = 0;
        return {};
    }
    if (!payload.sharedPixels || payload.sharedPixels->size() != payload.pixels.size()) {
        payload.pixelsFingerprint = StackHash::HashBytes(payload.pixels);
        payload.sharedPixels = std::make_shared<std::vector<unsigned char>>(payload.pixels);
    } else if (payload.pixelsFingerprint == 0) {
        payload.pixelsFingerprint = StackHash::HashBytes(*payload.sharedPixels);
    }

    return MakeSharedPixelBufferAlias(payload.sharedPixels, payload.pixelsFingerprint);
}

RenderGraphImagePayload BuildRenderImagePayload(const EditorNodeGraph::ImagePayload& payload) {
    RenderGraphImagePayload renderImage;
    renderImage.pixels = EnsureSharedImagePixels(payload);
    renderImage.width = payload.width;
    renderImage.height = payload.height;
    renderImage.channels = payload.channels;
    renderImage.sourceDescriptor = payload.sourceColorMetadata.descriptor;
    return renderImage;
}

} // namespace Stack::Project
