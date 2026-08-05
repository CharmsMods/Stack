#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Stack::PixelBuffer {

inline constexpr std::size_t kMaximumTransparentPlaceholderBytes =
    512ull * 1024ull * 1024ull;

inline bool TryComputePixelElementCount(
    int width,
    int height,
    int channels,
    std::size_t& outElementCount) {
    outElementCount = 0;
    if (width <= 0 || height <= 0 || channels <= 0) {
        return false;
    }

    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    const std::size_t c = static_cast<std::size_t>(channels);
    if (w > std::numeric_limits<std::size_t>::max() / h) {
        return false;
    }
    const std::size_t pixelCount = w * h;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / c) {
        return false;
    }
    outElementCount = pixelCount * c;
    return true;
}

inline bool TryComputePixelByteCount(
    int width,
    int height,
    int channels,
    std::size_t& outByteCount) {
    return TryComputePixelElementCount(
        width,
        height,
        channels,
        outByteCount);
}

inline bool IsSupportedInterleavedChannelCount(int channels) {
    return channels >= 1 && channels <= 4;
}

inline bool HasCompletePixelBuffer(
    std::size_t availableBytes,
    int width,
    int height,
    int channels) {
    std::size_t requiredBytes = 0;
    return IsSupportedInterleavedChannelCount(channels) &&
        TryComputePixelByteCount(width, height, channels, requiredBytes) &&
        availableBytes >= requiredBytes;
}

inline bool CopyInterleavedPixels(
    const unsigned char* source,
    int width,
    int height,
    int channels,
    std::vector<unsigned char>& destination) {
    std::size_t byteCount = 0;
    if (!source ||
        !IsSupportedInterleavedChannelCount(channels) ||
        !TryComputePixelByteCount(width, height, channels, byteCount)) {
        destination.clear();
        return false;
    }
    try {
        std::vector<unsigned char> copy(source, source + byteCount);
        destination = std::move(copy);
        return true;
    } catch (const std::bad_alloc&) {
        destination.clear();
        return false;
    } catch (const std::length_error&) {
        destination.clear();
        return false;
    }
}

inline std::vector<unsigned char> BuildTransparentRgbaPixels(
    int width,
    int height,
    std::size_t maximumBytes = kMaximumTransparentPlaceholderBytes) {
    std::size_t byteCount = 0;
    if (!TryComputePixelByteCount(width, height, 4, byteCount) ||
        byteCount > maximumBytes) {
        return {};
    }
    try {
        return std::vector<unsigned char>(byteCount, 0u);
    } catch (const std::bad_alloc&) {
        return {};
    } catch (const std::length_error&) {
        return {};
    }
}

inline bool FlipInterleavedRowsInPlace(
    std::vector<unsigned char>& pixels,
    int width,
    int height,
    int channels) {
    if (!HasCompletePixelBuffer(
            pixels.size(), width, height, channels)) {
        return false;
    }
    if (height <= 1) {
        return true;
    }

    std::size_t rowByteCount = 0;
    if (!TryComputePixelByteCount(
            width, 1, channels, rowByteCount)) {
        return false;
    }
    std::vector<unsigned char> scratch;
    try {
        scratch.resize(rowByteCount);
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }

    for (int y = 0; y < height / 2; ++y) {
        unsigned char* top =
            pixels.data() + static_cast<std::size_t>(y) * rowByteCount;
        unsigned char* bottom =
            pixels.data() +
            static_cast<std::size_t>(height - 1 - y) * rowByteCount;
        std::memcpy(scratch.data(), top, rowByteCount);
        std::memcpy(top, bottom, rowByteCount);
        std::memcpy(bottom, scratch.data(), rowByteCount);
    }
    return true;
}

inline std::vector<unsigned char> RotateInterleavedQuarterTurnsClockwise(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    int channels,
    int quarterTurnsClockwise,
    int& outWidth,
    int& outHeight) {
    outWidth = 0;
    outHeight = 0;
    if (!HasCompletePixelBuffer(
            pixels.size(), width, height, channels)) {
        return {};
    }

    int turns = quarterTurnsClockwise % 4;
    if (turns < 0) {
        turns += 4;
    }
    outWidth = (turns == 1 || turns == 3) ? height : width;
    outHeight = (turns == 1 || turns == 3) ? width : height;

    std::size_t outputByteCount = 0;
    if (!TryComputePixelByteCount(
            outWidth, outHeight, channels, outputByteCount)) {
        outWidth = 0;
        outHeight = 0;
        return {};
    }

    std::vector<unsigned char> rotated;
    try {
        rotated.resize(outputByteCount);
    } catch (const std::bad_alloc&) {
        outWidth = 0;
        outHeight = 0;
        return {};
    } catch (const std::length_error&) {
        outWidth = 0;
        outHeight = 0;
        return {};
    }

    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            int sourceX = x;
            int sourceY = y;
            if (turns == 1) {
                sourceX = width - 1 - y;
                sourceY = x;
            } else if (turns == 2) {
                sourceX = width - 1 - x;
                sourceY = height - 1 - y;
            } else if (turns == 3) {
                sourceX = y;
                sourceY = height - 1 - x;
            }
            const std::size_t sourceIndex =
                (static_cast<std::size_t>(sourceY) *
                     static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(sourceX)) *
                static_cast<std::size_t>(channels);
            const std::size_t destinationIndex =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(outWidth) +
                 static_cast<std::size_t>(x)) *
                static_cast<std::size_t>(channels);
            std::copy_n(
                pixels.data() + sourceIndex,
                channels,
                rotated.data() + destinationIndex);
        }
    }
    return rotated;
}

} // namespace Stack::PixelBuffer
