#pragma once
#include "Raw/HslSelections.h"

namespace Stack::RawRecipe {

// A batch owns its immutable recipe so deferred visualization work cannot
// observe later control edits. Masks still use the authored pin indices.
struct PreparedColorWarpEvaluation {
    struct Pin {
        std::size_t index=0;
        float spatialSupport=0;
        std::array<float,2> movement{};
    };
    RawColorWarpRecipe recipe;
    ColorWarpHslCoefficients globalHsl{};
    std::vector<HslSelectionEvaluation> selections;
    std::vector<Pin> pins;
    bool hasEffect=false;
};

// The input follows ApplyPreparedColorWarp's already-sanitized recipe contract.
// Prepare once for a batch, then keep the value unchanged until the batch ends.
PreparedColorWarpEvaluation PrepareColorWarpEvaluation(const RawColorWarpRecipe& recipe);
std::array<float,3> ApplyPreparedColorWarp(
    const PreparedColorWarpEvaluation& prepared,
    const std::array<float,3>& sceneRgb,
    Raw::RawWorkingSpace workingSpace,
    const std::vector<RawColorWarpMaskSample>* masks=nullptr,
    std::array<float,2> imagePosition={.5f,.5f});

}
