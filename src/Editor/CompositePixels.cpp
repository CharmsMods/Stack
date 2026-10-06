#include "Editor/CompositePixels.h"
#include "Utils/PixelBufferUtils.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <new>
namespace Stack::EditorRendering {
bool PrepareCompositePixels(
    std::vector<unsigned char> source,
    int width,
    int height,
    int padding,
    bool keepFullFrame,
    PreparedCompositePixels& output) {
    std::size_t sourceBytes = 0;
    if (!Stack::PixelBuffer::TryComputePixelByteCount(
            width, height, 4, sourceBytes) ||
        source.size() != sourceBytes) {
        return false;
    }

    if (keepFullFrame) {
        output.pixels = std::move(source);
        output.width = width;
        output.height = height;
        return true;
    }

    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < height; ++y) {
        const std::size_t row =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width) * 4u;
        for (int x = 0; x < width; ++x) {
            const std::size_t alpha =
                row + static_cast<std::size_t>(x) * 4u + 3u;
            if (source[alpha] == 0) {
                continue;
            }
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }

    if (maxX < minX || maxY < minY) {
        output.pixels = std::move(source);
        output.width = width;
        output.height = height;
        return true;
    }

    const int safePadding = std::max(0, padding);
    minX = std::max(0, minX - safePadding);
    minY = std::max(0, minY - safePadding);
    maxX = std::min(width - 1, maxX + safePadding);
    maxY = std::min(height - 1, maxY + safePadding);
    const int croppedWidth = maxX - minX + 1;
    const int croppedHeight = maxY - minY + 1;
    if (croppedWidth == width && croppedHeight == height) {
        output.pixels = std::move(source);
        output.width = width;
        output.height = height;
        return true;
    }

    std::size_t croppedBytes = 0;
    std::size_t croppedRowBytes = 0;
    std::size_t sourceRowBytes = 0;
    if (!Stack::PixelBuffer::TryComputePixelByteCount(
            croppedWidth, croppedHeight, 4, croppedBytes) ||
        !Stack::PixelBuffer::TryComputePixelByteCount(
            croppedWidth, 1, 4, croppedRowBytes) ||
        !Stack::PixelBuffer::TryComputePixelByteCount(
            width, 1, 4, sourceRowBytes)) {
        return false;
    }

    std::vector<unsigned char> cropped;
    try {
        cropped.resize(croppedBytes);
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    for (int y = 0; y < croppedHeight; ++y) {
        const std::size_t sourceOffset =
            static_cast<std::size_t>(minY + y) * sourceRowBytes +
            static_cast<std::size_t>(minX) * 4u;
        const std::size_t destinationOffset =
            static_cast<std::size_t>(y) * croppedRowBytes;
        std::memcpy(
            cropped.data() + destinationOffset,
            source.data() + sourceOffset,
            croppedRowBytes);
    }

    output.pixels = std::move(cropped);
    output.width = croppedWidth;
    output.height = croppedHeight;
    return true;
}

}
