#pragma once

#include "Editor/NodeGraph/NodeGraphPayloads.h"
#include "Utils/HashUtils.h"

namespace EditorNodeGraph {

inline std::size_t ImagePreviewFingerprint(const ImagePayload& image) {
    // Imports carry a bounded preview; small images use their original bytes.
    // Cold persisted nodes use their complete encoded PNG until decoded. Never
    // sample a few pixels: rotation/replacement can preserve every such sample.
    const auto& bytes = !image.previewPixels.empty() ? image.previewPixels :
        !image.pixels.empty() ? image.pixels : image.pngBytes;
    if (bytes.empty()) return 0;

    if (image.previewContentFingerprint == 0) {
        const auto content = image.previewPixels.empty() && !image.pixels.empty() &&
            image.pixelsFingerprint != 0 ? image.pixelsFingerprint : StackHash::HashBytes(bytes);
        image.previewContentFingerprint = content == 0 ? 1 : content;
    }
    std::size_t fingerprint = image.previewContentFingerprint;
    const auto mix = [&](int value) {
        StackHash::HashCombine(fingerprint, StackHash::HashValue(value));
    };
    mix(!image.previewPixels.empty() ? 1 : !image.pixels.empty() ? 2 : 3);
    mix(image.width);
    mix(image.height);
    mix(image.channels);
    mix(image.previewWidth);
    mix(image.previewHeight);
    mix(image.previewChannels);
    return fingerprint == 0 ? 1 : fingerprint;
}

} // namespace EditorNodeGraph
