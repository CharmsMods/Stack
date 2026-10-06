#pragma once

#include "Raw/MultiFrameDenoise/LocalMotion.h"
#include "Raw/MultiFrameDenoise/SharedBurst.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Raw::Mfd {

struct GpuSharedBurstFrameView {
    const std::vector<float>* normalized = nullptr;
    const std::vector<float>* comparisonGain = nullptr;
    const std::vector<std::uint8_t>* sampleFlags = nullptr;
    const std::vector<float>* explicitVariance = nullptr;
    std::array<SiteNoiseProfile, 4> noise {};
    std::array<double, 4> usableCodeSpanDn {};
    NoiseModelQuality noiseQuality = NoiseModelQuality::Unavailable;
    CfaPattern cfaPattern = CfaPattern::Unknown;
};

struct GpuSharedBurstAlternateView {
    GpuSharedBurstFrameView frame;
    const LocalMotionGrid* motion = nullptr;
    const std::vector<float>* reliability = nullptr;
    PixelExtent reliabilityExtent;
    double exposureScale = 1.0;
    double exposureScaleVariance = 0.0;
    double trustAttenuation = 1.0;
};

struct GpuSharedBurstRequest {
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t tileRawPixels = 512u;
    Parameters parameters;
    SharedBurstSettings settings;
    GpuSharedBurstFrameView reference;
    std::vector<GpuSharedBurstAlternateView> alternates;
    std::function<bool()> shouldCancel;
    std::function<void(double)> reportProgress;
};

struct GpuSharedBurstDiagnostics {
    std::string deviceIdentity;
    std::uint32_t dispatchedTileCount = 0u;
};

struct GpuSharedBurstOutput {
    std::vector<float> normalizedMosaic;
    std::vector<FusionPixelDiagnostics> diagnostics;
    GpuSharedBurstDiagnostics gpu;
};

// Requires a current OpenGL 4.3 core context. Unsupported requests fail
// without partial output so the caller can run the CPU reference path.
bool FuseSharedBurstOpenGl(
    const GpuSharedBurstRequest& request,
    GpuSharedBurstOutput& output,
    std::string& error);

} // namespace Raw::Mfd
