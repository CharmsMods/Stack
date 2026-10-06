#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <vector>

namespace Stack::RawRecipe {

struct ImageGuidePixel { float a = 0, b = 0, sceneEv = 0; };

// Top-down, developed scene-linear reference before authored exposure gains.
// The revision identifies source content, development settings and orientation.
struct ImageGuide {
    int width = 0, height = 0;
    std::size_t revision = 0;
    std::size_t recipeFingerprint = 0;
    std::vector<ImageGuidePixel> pixels;
    bool Valid() const {
        return width > 0 && height > 0 && pixels.size() == std::size_t(width) * height;
    }
};

// Shared by Color Warp region reach and the bounded Zones brush. Keep the
// caller's scales explicit: color affinity and spatial boundaries differ.
template<class Pixel>
float ImageGuideColorDistance(const Pixel& a, const Pixel& b, float scale) {
    return std::hypot(a.a - b.a, a.b - b.b) / std::max(.001f, scale);
}
template<class Pixel>
float ImageGuideBrightnessDistance(const Pixel& a, const Pixel& b, float scale) {
    return std::abs(a.sceneEv - b.sceneEv) / std::max(.001f, scale);
}
template<class Pixel>
float ImageGuideEdgeDistance(const Pixel& a, const Pixel& b, float colorScale, float evScale) {
    return ImageGuideColorDistance(a,b,colorScale) + ImageGuideBrightnessDistance(a,b,evScale);
}

// coverage is top-down. It is an upper bound, including its original feather.
// Only pixels connected to the first seed inside that footprint can survive.
// Returns false on cancellation/invalid input; callers must discard the result.
bool FollowImageEdges(const ImageGuide& guide, std::vector<float>& coverage,
    int seedX, int seedY, float radiusPixels, float sensitivity,
    const std::function<bool()>& cancelled = {});

} // namespace Stack::RawRecipe
