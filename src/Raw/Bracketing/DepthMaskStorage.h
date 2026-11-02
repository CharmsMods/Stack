#pragma once
#include "Raw/DepthMask.h"
#include <zlib.h>

namespace Raw::Bracketing::Serialization {
inline DepthMask::Bytes CompressCoverage(const DepthMask& mask) {
    const auto pixels=mask.Read();
    uLongf size=compressBound(static_cast<uLong>(pixels->size()));
    DepthMask::Bytes compressed(size);
    if (compress2(compressed.data(),&size,pixels->data(),static_cast<uLong>(pixels->size()),Z_BEST_SPEED)!=Z_OK)
        throw std::runtime_error("Could not compress bracket mask coverage.");
    compressed.resize(size);
    return compressed;
}
inline void SetCompressedCoverage(DepthMask& mask,DepthMask::Bytes compressed) {
    const auto size=static_cast<uLongf>(mask.w*mask.h);
    auto stored=std::make_shared<const DepthMask::Bytes>(std::move(compressed));
    mask.readPixels=[size,stored] {
        DepthMask::Bytes pixels(size);
        auto length=size;
        if (uncompress(pixels.data(),&length,stored->data(),static_cast<uLong>(stored->size()))!=Z_OK || length!=size)
            throw std::runtime_error("Invalid compressed bracket mask coverage.");
        return pixels;
    };
}
}
