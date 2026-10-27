#pragma once
#include <cstdint>
#include <vector>

namespace Raw::Bracketing {
// Categorical photometric evidence, separate from interpolated motion and
// confidence. A passing cell is necessary, not sufficient, for correspondence.
// This transient crop uses the reliability owner's fixed REFERENCE raw grid.
struct CaptureSupportMask {
    enum class Mode : std::uint8_t { NotRequired, AllRejected, Raster };
    Mode mode=Mode::NotRequired;
    std::uint32_t width=0,height=0,cellStepRawPixels=0;
    std::uint64_t originRawX=0,originRawY=0,rawWidth=0,rawHeight=0;
    std::vector<float> cells; // exactly 0 (reject) or 1 (no categorical veto)
};
}
