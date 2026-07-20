#include "EditorNodeGraphImageSerialization.h"

#include "Library/LibraryManager.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace EditorNodeGraph {
namespace {

void PngWriteCallback(void* context, void* data, int size) {
    auto* bytes = static_cast<std::vector<unsigned char>*>(context);
    const auto* begin = static_cast<unsigned char*>(data);
    bytes->insert(bytes->end(), begin, begin + size);
}

std::vector<unsigned char> EncodePng(const std::vector<unsigned char>& pixels, int width, int height, int channels) {
    std::vector<unsigned char> pngBytes;
    if (pixels.empty() || width <= 0 || height <= 0) {
        return pngBytes;
    }

    const int safeChannels = std::max(1, channels);
    stbi_write_png_to_func(PngWriteCallback, &pngBytes, width, height, safeChannels, pixels.data(), width * safeChannels);
    return pngBytes;
}

} // namespace

std::vector<unsigned char> EncodeImagePayloadPngForStorage(
    const std::vector<unsigned char>& bottomLeftPixels,
    int width,
    int height,
    int channels) {
    if (bottomLeftPixels.empty() || width <= 0 || height <= 0) {
        return {};
    }

    std::vector<unsigned char> topLeftPixels = bottomLeftPixels;
    LibraryManager::FlipImageRowsInPlace(topLeftPixels, width, height, std::max(1, channels));
    return EncodePng(topLeftPixels, width, height, channels);
}

void BuildImagePayloadPreview(
    const std::vector<unsigned char>& bottomLeftPixels,
    int width,
    int height,
    int channels,
    std::vector<unsigned char>& outPixels,
    int& outWidth,
    int& outHeight,
    int& outChannels,
    int maxDimension) {
    outPixels.clear();
    outWidth = 0;
    outHeight = 0;
    outChannels = std::max(1, channels);

    const int safeChannels = std::max(1, channels);
    const int safeMaxDimension = std::max(1, maxDimension);
    const int largestDimension = std::max(width, height);
    const std::size_t requiredBytes =
        static_cast<std::size_t>(std::max(0, width)) *
        static_cast<std::size_t>(std::max(0, height)) *
        static_cast<std::size_t>(safeChannels);
    if (bottomLeftPixels.size() < requiredBytes || width <= 0 || height <= 0 ||
        largestDimension <= safeMaxDimension) {
        return;
    }

    outWidth = std::max(1, static_cast<int>(
        (static_cast<long long>(width) * safeMaxDimension + largestDimension / 2) / largestDimension));
    outHeight = std::max(1, static_cast<int>(
        (static_cast<long long>(height) * safeMaxDimension + largestDimension / 2) / largestDimension));
    outChannels = safeChannels;
    outPixels.resize(
        static_cast<std::size_t>(outWidth) *
        static_cast<std::size_t>(outHeight) *
        static_cast<std::size_t>(safeChannels));

    // A nearest-neighbour sample is intentional here: this is only a bounded
    // graph thumbnail, so it must be cheap and must preserve alpha exactly.
    for (int y = 0; y < outHeight; ++y) {
        const int sourceY = std::min(
            height - 1,
            static_cast<int>((static_cast<long long>(y) * height + height / 2) / outHeight));
        for (int x = 0; x < outWidth; ++x) {
            const int sourceX = std::min(
                width - 1,
                static_cast<int>((static_cast<long long>(x) * width + width / 2) / outWidth));
            const std::size_t sourceOffset =
                (static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(sourceX)) * static_cast<std::size_t>(safeChannels);
            const std::size_t destinationOffset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(outWidth) +
                 static_cast<std::size_t>(x)) * static_cast<std::size_t>(safeChannels);
            std::copy_n(
                bottomLeftPixels.data() + sourceOffset,
                safeChannels,
                outPixels.data() + destinationOffset);
        }
    }
}

bool DecodeImagePayloadPngBytes(const std::vector<unsigned char>& pngBytes, ImagePayload& payload) {
    if (pngBytes.empty()) {
        return false;
    }

    // The editor stores image-node pixels in the same bottom-left-oriented layout
    // used by the render pipeline's GL uploads. Keep saved image payloads aligned
    // with fresh imports so reopening a project cannot flip the source image.
    stbi_set_flip_vertically_on_load_thread(1);
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(pngBytes.data(), static_cast<int>(pngBytes.size()), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }

    payload.pngBytes = pngBytes;
    payload.pixels.assign(pixels, pixels + (width * height * 4));
    payload.width = width;
    payload.height = height;
    payload.channels = 4;
    payload.originalChannels = channels;
    payload.sourceColorMetadata = Stack::NodeMath::InspectSourceColorMetadata(
        pngBytes, width, height, channels, Stack::NodeMath::LogicalPrecision::UInt8,
        payload.sourcePath.empty() ? payload.label : payload.sourcePath);
    stbi_image_free(pixels);
    return true;
}

Stack::NodeMath::SourceColorMetadata InspectImageFileColorMetadata(
    const std::string& path,
    int width,
    int height,
    int originalChannels) {
    std::vector<unsigned char> encoded;
    std::ifstream stream(path, std::ios::binary);
    if (stream) {
        encoded.assign(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>());
    }
    return Stack::NodeMath::InspectSourceColorMetadata(
        encoded, width, height, originalChannels,
        Stack::NodeMath::LogicalPrecision::UInt8, path);
}

std::vector<unsigned char> ReadBinaryJsonBytes(const nlohmann::json& value) {
    if (!value.is_binary()) {
        return {};
    }

    const auto& binaryValue = value.get_binary();
    return std::vector<unsigned char>(binaryValue.begin(), binaryValue.end());
}

} // namespace EditorNodeGraph
