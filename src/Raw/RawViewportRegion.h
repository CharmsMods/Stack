#pragma once
#include "Raw/RawViewportWorkload.h"
#include "Raw/Denoise/RawDenoiseBandSchedule.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Raw {
struct ViewportRegion {
    int fullWidth = 0, fullHeight = 0;
    int x = 0, y = 0, width = 0, height = 0;
    bool Valid() const { return fullWidth > 0 && fullHeight > 0 && width > 0 && height > 0 &&
        x >= 0 && y >= 0 && width <= fullWidth - x && height <= fullHeight - y; }
    bool Partial() const { return Valid() && (x || y || width != fullWidth || height != fullHeight); }
    bool operator==(const ViewportRegion& b) const {
        return fullWidth == b.fullWidth && fullHeight == b.fullHeight && x == b.x && y == b.y &&
            width == b.width && height == b.height;
    }
    bool operator!=(const ViewportRegion& b) const { return !(*this == b); }
    std::size_t Fingerprint() const {
        std::size_t key = 1;
        for (int v : {fullWidth, fullHeight, x, y, width, height})
            Stack::Renderer::RawDevelopmentCache::HashTypedValue(key, v);
        return key;
    }
};
struct ViewportRequest {
    ViewportRegion visible;
    double samplingScale = 1.0;
    std::uint64_t generation = 0;
};
inline ViewportRegion ScaleViewportRegion(const ViewportRegion& native, int width, int height) {
    if (!native.Valid() || width <= 0 || height <= 0) return {};
    const int left = std::clamp(int(std::floor(double(native.x) * width / native.fullWidth)), 0, width - 1);
    const int top = std::clamp(int(std::floor(double(native.y) * height / native.fullHeight)), 0, height - 1);
    const int right = std::clamp(int(std::ceil(double(native.x + native.width) * width / native.fullWidth)), left + 1, width);
    const int bottom = std::clamp(int(std::ceil(double(native.y + native.height) * height / native.fullHeight)), top + 1, height);
    return {width, height, left, top, right - left, bottom - top};
}
inline std::array<ViewportStageRegion, kViewportStageCount> ViewportRegionRequirements(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe, int renderWidth = 1, int renderHeight = 1,
    int sourceWidth = 1, int sourceHeight = 1) {
    std::array<ViewportStageRegion,kViewportStageCount> requirements {};
    for (const auto& module : kViewportModules)
        if (module.regionRequirement) requirements[std::size_t(module.stage)]=
            module.regionRequirement(recipe,renderWidth,renderHeight,sourceWidth,sourceHeight);
    return requirements;
}
inline ViewportStage ViewportFirstRegionalStage(const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    const auto requirements = ViewportRegionRequirements(recipe);
    std::size_t first = 0;
    for (std::size_t i = 0; i < requirements.size(); ++i)
        if ((requirements[i].requirement == ViewportRegionRequirement::FullInput || requirements[i].materializeFullInput)) first = i + 1;
    return static_cast<ViewportStage>(std::clamp(first, std::size_t{2}, kViewportStageCount - 1));
}
}
