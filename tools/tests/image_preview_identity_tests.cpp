#include "Editor/NodeGraph/ImagePreviewIdentity.h"
#include "Utils/PixelBufferUtils.h"

#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

EditorNodeGraph::ImagePayload Image() {
    EditorNodeGraph::ImagePayload image;
    image.width = image.height = 4;
    image.pixels.assign(4 * 4 * 4, 0);
    for (std::size_t i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = 255;
    image.pixels[(2 * 4 + 1) * 4 + 1] = 255;
    return image;
}

void RequireSameOldSamples(const std::vector<unsigned char>& a,
    const std::vector<unsigned char>& b) {
    Require(a.size() == b.size() && a.front() == b.front() &&
        a[a.size() / 2] == b[b.size() / 2] && a.back() == b.back(),
        "Fixture must preserve every byte sampled by the old preview key");
}
}

int main() try {
    using namespace EditorNodeGraph;
    auto original = Image();
    original.pixelsFingerprint = StackHash::HashBytes(original.pixels);
    const auto first = ImagePreviewFingerprint(original);
    Require(first != 0 && original.previewContentFingerprint != 0,
        "Preview identity was not retained");
    const auto retainedDigest = original.previewContentFingerprint;
    for (int i = 0; i < 8; ++i)
        Require(ImagePreviewFingerprint(original) == first &&
            original.previewContentFingerprint == retainedDigest,
            "Unchanged redraw lost its cached content identity");

    auto rotated = original;
    rotated.pixels = Stack::PixelBuffer::RotateInterleavedQuarterTurnsClockwise(
        original.pixels, original.width, original.height, original.channels,
        1, rotated.width, rotated.height);
    Require(rotated.pixels != original.pixels, "Rotation fixture did not change pixels");
    RequireSameOldSamples(original.pixels, rotated.pixels);
    InvalidateImagePayloadRuntime(rotated);
    Require(rotated.previewContentFingerprint == 0,
        "Pixel mutation retained the previous preview identity");
    Require(ImagePreviewFingerprint(rotated) != first,
        "Square rotation reused the previous thumbnail identity");

    auto replacement = Image();
    replacement.pixels[5] = 180;
    RequireSameOldSamples(original.pixels, replacement.pixels);
    Require(ImagePreviewFingerprint(replacement) != first,
        "Same-size replacement reused the previous thumbnail identity");

    // Whole-payload undo/redo restores the identity belonging to those bytes.
    auto restored = original;
    Require(ImagePreviewFingerprint(restored) == first,
        "Restoring an original payload retained the edited thumbnail identity");
    restored = rotated;
    Require(ImagePreviewFingerprint(restored) == ImagePreviewFingerprint(rotated),
        "Restoring the rotated payload lost its thumbnail identity");

    // Analysis and async-import history reduction construct metadata-only
    // payloads, then restore their separately owned bytes. A fresh payload must
    // not inherit the digest of an unrelated current UI texture.
    ImagePayload reduced;
    reduced.width = original.width;
    reduced.height = original.height;
    reduced.channels = original.channels;
    Require(ImagePreviewFingerprint(reduced) == 0 && reduced.previewContentFingerprint == 0,
        "An empty reduced payload cached an identity before restoration");
    reduced.pixels = replacement.pixels;
    Require(ImagePreviewFingerprint(reduced) == ImagePreviewFingerprint(replacement),
        "Restored history pixels received a stale preview identity");

    auto preview = Image();
    preview.previewPixels = preview.pixels;
    preview.previewWidth = preview.previewHeight = 4;
    const auto previewIdentity = ImagePreviewFingerprint(preview);
    auto changedPreview = preview;
    InvalidateImagePayloadRuntime(changedPreview);
    changedPreview.previewPixels = replacement.pixels;
    changedPreview.previewWidth = changedPreview.previewHeight = 4;
    Require(ImagePreviewFingerprint(changedPreview) != previewIdentity,
        "Bounded preview content was not included in its identity");
    const auto changedPreviewIdentity = ImagePreviewFingerprint(changedPreview);
    changedPreview.previewChannels = 3;
    Require(ImagePreviewFingerprint(changedPreview) != changedPreviewIdentity,
        "Preview layout was not included in its identity");

    // The cold PNG path hashes every encoded byte, not merely file size and
    // three samples. Payload replacement is how a different cold node arrives.
    ImagePayload cold;
    cold.width = cold.height = 4;
    cold.pngBytes = {137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 1};
    const auto coldIdentity = ImagePreviewFingerprint(cold);
    ImagePayload otherCold;
    otherCold.width = otherCold.height = 4;
    otherCold.pngBytes = cold.pngBytes;
    otherCold.pngBytes[4] ^= 1;
    RequireSameOldSamples(cold.pngBytes, otherCold.pngBytes);
    Require(ImagePreviewFingerprint(otherCold) != coldIdentity,
        "Cold encoded payload replacement reused a stale decode identity");
    cold.pixels = original.pixels;
    InvalidateImagePayloadRuntime(cold);
    Require(ImagePreviewFingerprint(cold) == first,
        "Decoded payload retained its encoded-content identity");

    std::cout << "Image preview identity: rotation, replacement, history, cold bytes and reuse passed.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
