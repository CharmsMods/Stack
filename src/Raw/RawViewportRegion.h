#pragma once
#include "Raw/RawViewportWorkload.h"
#include "Raw/Denoise/RawDenoiseBandSchedule.h"
#include "Renderer/ViewportPreviewTypes.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Raw {
using ViewportRegion = Stack::Renderer::ImageRegion;
using ViewportRequest = Stack::Renderer::ViewportPreviewRequest;
inline ViewportRegion ScaleViewportRegion(const ViewportRegion& native, int width, int height) {
    return Stack::Renderer::ScaleImageRegion(native, width, height);
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
