#pragma once

#include "Utils/HashUtils.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

struct SharedPixelBuffer {
    std::shared_ptr<const std::vector<unsigned char>> bytes;
    std::size_t fingerprint = 0;

    bool empty() const {
        return !bytes || bytes->empty();
    }

    const unsigned char* data() const {
        return empty() ? nullptr : bytes->data();
    }

    std::size_t size() const {
        return bytes ? bytes->size() : 0;
    }
};

inline SharedPixelBuffer MakeSharedPixelBufferAlias(
    std::shared_ptr<const std::vector<unsigned char>> bytes,
    std::size_t fingerprint = 0) {
    if (!bytes || bytes->empty()) {
        return {};
    }

    SharedPixelBuffer buffer;
    buffer.bytes = std::move(bytes);
    buffer.fingerprint =
        fingerprint != 0 ? fingerprint : StackHash::HashBytes(*buffer.bytes);
    return buffer;
}

inline SharedPixelBuffer MakeSharedPixelBufferCopy(
    const std::vector<unsigned char>& pixels,
    std::size_t fingerprint = 0) {
    if (pixels.empty()) {
        return {};
    }
    return MakeSharedPixelBufferAlias(
        std::make_shared<std::vector<unsigned char>>(pixels),
        fingerprint);
}

inline SharedPixelBuffer MakeSharedPixelBufferOwned(
    std::vector<unsigned char> pixels,
    std::size_t fingerprint = 0) {
    if (pixels.empty()) {
        return {};
    }
    return MakeSharedPixelBufferAlias(
        std::make_shared<std::vector<unsigned char>>(std::move(pixels)),
        fingerprint);
}
