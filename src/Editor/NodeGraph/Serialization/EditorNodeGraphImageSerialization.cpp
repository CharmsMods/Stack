#include "EditorNodeGraphImageSerialization.h"

#include "Library/LibraryManager.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"

#include <algorithm>

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
    stbi_image_free(pixels);
    return true;
}

std::vector<unsigned char> ReadBinaryJsonBytes(const nlohmann::json& value) {
    if (!value.is_binary()) {
        return {};
    }

    const auto& binaryValue = value.get_binary();
    return std::vector<unsigned char>(binaryValue.begin(), binaryValue.end());
}

} // namespace EditorNodeGraph
