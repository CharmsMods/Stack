#pragma once
#include "Raw/RawImageData.h"
#include <array>
#include <functional>
#include <memory>
#include <string>

namespace Raw::Demosaic {
// One instance per RAW pipeline/context. Input and returned texture use native
// sensor row order and camera-linear float values. Caller owns the returned GL
// texture. Compilation state stays here; all intermediate images are temporary.
// AHD alone uses cameraToSrgb for its homogeneity analysis. It does not transform
// the returned image. Construction, use and destruction require the same GL context.
class GpuDemosaic {
public:
    GpuDemosaic();
    ~GpuDemosaic();
    GpuDemosaic(const GpuDemosaic&) = delete;
    GpuDemosaic& operator=(const GpuDemosaic&) = delete;
    unsigned int Reconstruct(unsigned int mosaic, int width, int height,
        CfaPattern pattern, DemosaicMethod method,
        const std::function<bool()>& cancel, std::string& error,
        const std::array<float,9>& cameraToSrgb = {1,0,0,0,1,0,0,0,1});
private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
}
