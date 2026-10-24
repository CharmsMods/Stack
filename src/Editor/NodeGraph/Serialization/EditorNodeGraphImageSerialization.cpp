#include "EditorNodeGraphImageSerialization.h"

#include "Library/LibraryManager.h"
#include "ThirdParty/stb_image.h"
#include "Utils/PixelBufferUtils.h"
#include "Utils/PngEncodingUtils.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace EditorNodeGraph {
namespace {

std::vector<unsigned char> EncodePng(const std::vector<unsigned char>& pixels, int width, int height, int channels) {
    return Stack::PngEncoding::EncodeInterleaved(
        pixels, width, height, channels);
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

    if (!Stack::PixelBuffer::HasCompletePixelBuffer(
            bottomLeftPixels.size(), width, height, channels)) {
        return {};
    }
    std::vector<unsigned char> topLeftPixels;
    try {
        if (channels < 3) {
            // These are packed GL_RED/GL_RG numerical values. PNG's two-channel
            // gray/alpha interpretation would move G into alpha on reopen.
            std::size_t storageSize = 0;
            if (!Stack::PixelBuffer::TryComputePixelByteCount(width, height, 4, storageSize)) return {};
            topLeftPixels.resize(storageSize);
            for (std::size_t i = 0, source = 0; i < storageSize; i += 4, source += channels) {
                topLeftPixels[i] = bottomLeftPixels[source];
                topLeftPixels[i + 1] = channels == 2 ? bottomLeftPixels[source + 1] : 0;
                topLeftPixels[i + 2] = 0;
                topLeftPixels[i + 3] = 255;
            }
        } else topLeftPixels = bottomLeftPixels;
    } catch (const std::bad_alloc&) {
        return {};
    } catch (const std::length_error&) {
        return {};
    }
    const int storageChannels = channels < 3 ? 4 : channels;
    LibraryManager::FlipImageRowsInPlace(topLeftPixels, width, height, storageChannels);
    return EncodePng(topLeftPixels, width, height, storageChannels);
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
    outChannels = 0;

    if (!Stack::PixelBuffer::IsSupportedInterleavedChannelCount(channels) ||
        !Stack::PixelBuffer::HasCompletePixelBuffer(
            bottomLeftPixels.size(), width, height, channels)) {
        return;
    }
    const int safeChannels = channels;
    const int safeMaxDimension = std::max(1, maxDimension);
    const int largestDimension = std::max(width, height);
    if (largestDimension <= safeMaxDimension) {
        return;
    }

    outWidth = std::max(1, static_cast<int>(
        (static_cast<long long>(width) * safeMaxDimension + largestDimension / 2) / largestDimension));
    outHeight = std::max(1, static_cast<int>(
        (static_cast<long long>(height) * safeMaxDimension + largestDimension / 2) / largestDimension));
    outChannels = safeChannels;
    std::size_t outputByteCount = 0;
    if (!Stack::PixelBuffer::TryComputePixelByteCount(
            outWidth, outHeight, safeChannels, outputByteCount)) {
        outWidth = 0;
        outHeight = 0;
        outChannels = 0;
        return;
    }
    try {
        outPixels.resize(outputByteCount);
    } catch (const std::bad_alloc&) {
        outPixels.clear();
        outWidth = 0;
        outHeight = 0;
        outChannels = 0;
        return;
    } catch (const std::length_error&) {
        outPixels.clear();
        outWidth = 0;
        outHeight = 0;
        outChannels = 0;
        return;
    }

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
    if (pngBytes.empty() ||
        pngBytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    // The editor stores image-node pixels in the same bottom-left-oriented layout
    // used by the render pipeline's GL uploads. Keep saved image payloads aligned
    // with fresh imports so reopening a project cannot flip the source image.
    stbi_set_flip_vertically_on_load_thread(1);
    int width = 0;
    int height = 0;
    int channels = 0;
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(pngBytes.data(), static_cast<int>(pngBytes.size()),
            &width, &height, &channels, 4),
        stbi_image_free);
    if (!pixels || width <= 0 || height <= 0) {
        return false;
    }

    try {
        // Do not expose a half-decoded payload if copying bytes or inspecting
        // metadata fails. The input may also alias one of payload's vectors.
        std::vector<unsigned char> decodedPixels;
        if (!Stack::PixelBuffer::CopyInterleavedPixels(
                pixels.get(), width, height, 4, decodedPixels)) {
            return false;
        }
        pixels.reset();
        std::vector<unsigned char> encodedBytes(pngBytes);
        auto metadata = Stack::NodeMath::InspectSourceColorMetadata(
            encodedBytes, width, height, channels, Stack::NodeMath::LogicalPrecision::UInt8,
            payload.sourcePath.empty() ? payload.label : payload.sourcePath);

        // Every potentially throwing operation precedes publication. Leave the
        // payload's source/managed identity and pending-operation fields intact.
        static_assert(std::is_nothrow_move_assignable_v<Stack::NodeMath::SourceColorMetadata>);
        payload.pngBytes.swap(encodedBytes);
        payload.pixels.swap(decodedPixels);
        payload.sourceColorMetadata = std::move(metadata);
        payload.width = width;
        payload.height = height;
        payload.channels = 4;
        payload.originalChannels = channels;
        InvalidateImagePayloadRuntime(payload);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
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
