#pragma once
#include "Raw/RawImageData.h"
#include <functional>
#include <string>
#include <vector>

namespace Raw::Demosaic {
// Native, active-area CFA samples after source normalization, mosaic denoise,
// and white balance. Row zero is the top; CFA phase is relative to this area.
// Output is interleaved scene-linear camera RGB, with no display transform,
// clipping to [0,1], integer quantization, orientation or downstream edits.
bool Reconstruct(const std::vector<float>& mosaic, int width, int height,
    CfaPattern pattern, DemosaicMethod method, std::vector<float>& rgb,
    std::string& error, const std::function<bool()>& shouldCancel = {},
    const std::array<float,9>& cameraToSrgb = {1,0,0,0,1,0,0,0,1});
} // namespace Raw::Demosaic
