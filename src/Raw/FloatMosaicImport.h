#pragma once
#include "RawImageData.h"
#include <functional>

namespace Raw {
// Copies LibRaw's unquantized floating-point CFA using the shared per-site
// black/white calibration. Gain maps, white balance and color remain downstream.
bool ImportFloatDngMosaic(const float* samples,int stride,RawImageData&,
    const std::function<bool()>& shouldCancel);
}
