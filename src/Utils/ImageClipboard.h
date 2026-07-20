#pragma once

#include <string>
#include <vector>

namespace ImageClipboard {

struct PublishResult {
    bool dibV5Published = false;
    bool pngPublished = false;
    std::string error;

    bool Succeeded() const { return dibV5Published || pngPublished; }
};

PublishResult PublishRgbaImage(
    void* ownerWindow,
    const std::vector<unsigned char>& topLeftStraightRgba,
    int width,
    int height,
    const std::vector<unsigned char>& pngBytes);

} // namespace ImageClipboard
