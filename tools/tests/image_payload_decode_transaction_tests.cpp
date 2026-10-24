#include "Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

// Scope allocation failures to this tiny, single-threaded decode. stb uses
// malloc internally; these failures exercise the C++ pixel/PNG/metadata staging
// that follows the actual decoder without adding production injection hooks.
namespace {
bool trackAllocations = false;
bool allocationFailed = false;
std::size_t allocationCount = 0;
std::size_t failingAllocation = std::numeric_limits<std::size_t>::max();
}

void* operator new(std::size_t size) {
    if (trackAllocations && allocationCount++ == failingAllocation) {
        trackAllocations = false;
        allocationFailed = true;
        throw std::bad_alloc();
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
using EditorNodeGraph::ImagePayload;

int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

const std::vector<unsigned char>& TinyPng() {
    // Synthetic 2x2 RGBA PNG with an sRGB chunk. Top row red/green, bottom
    // row blue/yellow, so the bottom-left decoded layout is unambiguous.
    static const std::vector<unsigned char> png = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d,
        0x24, 0x00, 0x00, 0x00, 0x01, 0x73, 0x52, 0x47,
        0x42, 0x00, 0xae, 0xce, 0x1c, 0xe9, 0x00, 0x00,
        0x00, 0x14, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c,
        0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x0c, 0x81,
        0x34, 0x10, 0x30, 0xfc, 0x07, 0x00, 0x47, 0xca,
        0x08, 0xf8, 0x8b, 0x4e, 0x43, 0x85, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42,
        0x60, 0x82
    };
    return png;
}

ImagePayload ExistingImage() {
    ImagePayload image;
    image.label = "accepted image";
    image.sourcePath = "synthetic/accepted-source.png";
    image.managedAssetId = "retained-managed-identity";
    image.projectAssetPath = "Assets/retained.png";
    image.pngBytes = { 1, 2, 3 };
    image.pixels = { 21, 22, 23, 24 };
    image.previewPixels = { 31, 32, 33, 34 };
    image.width = image.height = image.previewWidth = image.previewHeight = 1;
    image.originalChannels = image.previewChannels = 3;
    image.sourceColorMetadata.label = "accepted color metadata";
    image.sourceColorMetadata.dependencyIdentity = "accepted color identity";
    image.sourceColorMetadata.retainedPayload = { 4, 5, 6 };
    image.sourceColorMetadata.issues.push_back({ "accepted", "retained issue" });
    image.sourceColorMetadata.descriptor.provenance =
        Stack::NodeMath::SemanticField<Stack::NodeMath::ProvenanceDescriptor>::Known({
            Stack::NodeMath::ProvenanceKind::Assigned, "accepted source", "accepted operation" });
    image.sharedPixels = std::make_shared<const std::vector<unsigned char>>(image.pixels);
    image.pixelsFingerprint = 123;
    image.previewContentFingerprint = 456;
    image.isLoading = image.isEmbedding = true;
    image.importRequestId = 42;
    image.embeddingRequestId = 43;
    return image;
}

bool SameOtherFields(const ImagePayload& a, const ImagePayload& b) {
    return a.label == b.label && a.sourcePath == b.sourcePath &&
        a.managedAssetId == b.managedAssetId && a.projectAssetPath == b.projectAssetPath &&
        a.isLoading == b.isLoading && a.isEmbedding == b.isEmbedding &&
        a.importRequestId == b.importRequestId && a.embeddingRequestId == b.embeddingRequestId;
}

bool SamePayload(const ImagePayload& a, const ImagePayload& b) {
    const auto& ma = a.sourceColorMetadata;
    const auto& mb = b.sourceColorMetadata;
    if (ma.issues.size() != mb.issues.size()) return false;
    for (std::size_t i = 0; i < ma.issues.size(); ++i)
        if (ma.issues[i].field != mb.issues[i].field ||
            ma.issues[i].message != mb.issues[i].message) return false;
    return SameOtherFields(a, b) && a.pngBytes == b.pngBytes && a.pixels == b.pixels &&
        a.previewPixels == b.previewPixels && a.width == b.width && a.height == b.height &&
        a.channels == b.channels && a.originalChannels == b.originalChannels &&
        a.previewWidth == b.previewWidth && a.previewHeight == b.previewHeight &&
        a.previewChannels == b.previewChannels && a.sharedPixels == b.sharedPixels &&
        a.pixelsFingerprint == b.pixelsFingerprint &&
        a.previewContentFingerprint == b.previewContentFingerprint &&
        ma.schemaVersion == mb.schemaVersion && ma.payloadKind == mb.payloadKind &&
        ma.label == mb.label && ma.dependencyIdentity == mb.dependencyIdentity &&
        ma.retainedPayload == mb.retainedPayload && ma.descriptor == mb.descriptor;
}

void CheckDecoded(const ImagePayload& image, const ImagePayload& before) {
    const std::vector<unsigned char> expected = {
        0, 0, 255, 255, 255, 255, 0, 255,
        255, 0, 0, 255, 0, 255, 0, 255
    };
    Check(image.pixels == expected && image.pngBytes == TinyPng(),
        "Successful decode must publish both encoded and bottom-left decoded bytes");
    Check(image.width == 2 && image.height == 2 && image.channels == 4 && image.originalChannels == 4,
        "Successful decode must publish dimensions and channels");
    Check(!image.sharedPixels && image.pixelsFingerprint == 0 &&
        image.previewContentFingerprint == 0 && image.previewPixels.empty() &&
        image.previewWidth == 0 && image.previewHeight == 0 && image.previewChannels == 4,
        "Successful decode must invalidate all previous pixel/preview runtime state");
    Check(image.sourceColorMetadata.label == "PNG sRGB" &&
        image.sourceColorMetadata.payloadKind == Stack::NodeMath::EmbeddedColorPayloadKind::PngColorSignal &&
        image.sourceColorMetadata.retainedPayload == std::vector<unsigned char>{0} &&
        image.sourceColorMetadata.issues.empty(),
        "Successful decode must replace metadata with the inspected PNG signal");
    Check(image.sourceColorMetadata.descriptor.provenance.value.sourceIdentity ==
        (before.sourcePath.empty() ? before.label : before.sourcePath),
        "Successful metadata inspection must retain the caller's source identity");
    Check(SameOtherFields(image, before), "Decode must preserve unrelated payload fields");
}

bool TrackedDecode(ImagePayload& image, std::size_t failAt, bool& threw) {
    allocationCount = 0;
    allocationFailed = false;
    failingAllocation = failAt;
    trackAllocations = true;
    bool decoded = false;
    try {
        decoded = EditorNodeGraph::DecodeImagePayloadPngBytes(TinyPng(), image);
    } catch (...) {
        threw = true;
    }
    trackAllocations = false;
    return decoded;
}
}

int main() {
    using EditorNodeGraph::DecodeImagePayloadPngBytes;
    const auto before = ExistingImage();
    auto decoded = before;
    Check(DecodeImagePayloadPngBytes(TinyPng(), decoded), "Tiny PNG must decode successfully");
    CheckDecoded(decoded, before);

    for (const auto& malformed : { std::vector<unsigned char>{},
            std::vector<unsigned char>{0x89, 'P', 'N', 'G', 0, 1, 2} }) {
        auto image = before;
        Check(!DecodeImagePayloadPngBytes(malformed, image), "Malformed PNG must be rejected");
        Check(SamePayload(image, before), "Malformed rejection must preserve the entire payload");
    }

    auto alias = before;
    alias.pngBytes = TinyPng();
    Check(DecodeImagePayloadPngBytes(alias.pngBytes, alias), "Aliased encoded PNG must decode");
    CheckDecoded(alias, before);
    alias = before;
    alias.pixels = TinyPng();
    Check(DecodeImagePayloadPngBytes(alias.pixels, alias), "Aliased pixel vector input must decode");
    CheckDecoded(alias, before);

    // Measure only this tiny decode, then reject each of its C++ allocations in
    // turn. This covers pixel copying, retained encoded bytes and metadata work.
    auto counted = before;
    bool threw = false;
    Check(TrackedDecode(counted, std::numeric_limits<std::size_t>::max(), threw) && !threw,
        "Allocation-counting decode must succeed");
    const std::size_t count = allocationCount;
    Check(count >= 3 && count < 128, "Tiny decode allocation coverage must remain bounded");
    if (count >= 128) return 1;
    for (std::size_t index = 0; index < count; ++index) {
        auto image = before;
        threw = false;
        const bool accepted = TrackedDecode(image, index, threw);
        Check(allocationFailed, "Each planned allocation failure must actually execute");
        Check(!accepted && !threw, "Allocation failure must return false without escaping");
        Check(SamePayload(image, before), "Allocation failure must preserve the entire accepted payload");
    }
    std::cout << "Image PNG decode transaction: " << count << " forced allocation failures, "
        << "malformed rejection, aliases and success; " << failures << " failed checks.\n";
    return failures == 0 ? 0 : 1;
}
