#pragma once

#include "Raw/RawImageData.h"

#include <array>

namespace Raw::Denoise {

// Var(component | Y) = shotScale * max(Y, 0) + readNoiseVariance.
// Components are scene-linear luma, B-Y, and R-Y in that order.
struct RawRgbNoiseModel {
    bool profiled = false;
    std::array<float, 3> shotScale {};
    std::array<float, 3> readNoiseVariance {};
};

RawRgbNoiseModel BuildRawRgbNoiseModel(
    const RawImageData& raw,
    const RawDevelopSettings& settings);

} // namespace Raw::Denoise
