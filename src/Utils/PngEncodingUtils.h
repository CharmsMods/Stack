#pragma once

#include "ThirdParty/stb_image_write.h"
#include "Utils/PixelBufferUtils.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace Stack::PngEncoding {
namespace Detail {

struct WriteContext {
    std::vector<unsigned char>* bytes = nullptr;
    bool failed = false;
};

inline void WriteCallback(void* context, void* data, int size) {
    auto* writeContext = static_cast<WriteContext*>(context);
    if (!writeContext || !writeContext->bytes || writeContext->failed ||
        !data || size <= 0) {
        return;
    }

    const auto* begin = static_cast<const unsigned char*>(data);
    try {
        writeContext->bytes->insert(
            writeContext->bytes->end(),
            begin,
            begin + size);
    } catch (const std::bad_alloc&) {
        writeContext->failed = true;
    } catch (const std::length_error&) {
        writeContext->failed = true;
    }
}

} // namespace Detail

inline std::vector<unsigned char> EncodeInterleaved(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    int channels) {
    std::vector<unsigned char> encoded;
    if (!PixelBuffer::HasCompletePixelBuffer(
            pixels.size(), width, height, channels)) {
        return encoded;
    }

    std::size_t rowStride = 0;
    if (!PixelBuffer::TryComputePixelByteCount(
            width, 1, channels, rowStride) ||
        rowStride > static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        return encoded;
    }

    Detail::WriteContext context { &encoded, false };
    const int result = stbi_write_png_to_func(
        Detail::WriteCallback,
        &context,
        width,
        height,
        channels,
        pixels.data(),
        static_cast<int>(rowStride));
    if (result == 0 || context.failed) {
        encoded.clear();
    }
    return encoded;
}

} // namespace Stack::PngEncoding
