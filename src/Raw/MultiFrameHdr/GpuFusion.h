#pragma once

#include "Raw/MultiFrameHdr/Contracts.h"
#include "Raw/MultiFrameDenoise/NoiseModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Raw::Hdr {

// Non-owning, immutable view of one fully prepared HDR measurement. These
// views are valid for the duration of FuseOpenGl only.
struct GpuFusionFrameView {
    const std::vector<float>* normalized = nullptr;
    const std::vector<float>* gain = nullptr;
    const std::vector<std::uint8_t>* flags = nullptr;
    const std::vector<std::uint8_t>* clippedNeighborCount = nullptr;
    const std::vector<float>* explicitVariance = nullptr;
    std::array<Raw::Mfd::SiteNoiseProfile, 4> noise {};
    float exposureRelativeToAnchor = 1.0f;
    float exposureUncertaintyEv = 0.0f;
    float noiseVarianceInflation = 1.0f;
    float translationX = 0.0f;
    float translationY = 0.0f;
    bool exposureVerified = false;
    bool lowConfidence = false;
};

struct GpuFusionRequest {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    CfaPattern cfaPattern = CfaPattern::Unknown;
    std::uint32_t referenceFrame = 0;
    std::uint32_t tileRawPixels = 512;
    Parameters parameters;
    std::vector<GpuFusionFrameView> frames;
    std::function<bool()> shouldCancel;
    std::function<void(double)> reportProgress;
};

struct GpuFusionDiagnostics {
    std::string deviceIdentity;
    std::vector<double> contribution;
    std::vector<std::array<double, 3>> contributionByRange;
    std::array<double, 3> rangePixelCount {};
    double effectiveSampleSum = 0.0;
    std::uint64_t finitePixelCount = 0;
    std::uint64_t referenceFallbackPixelCount = 0;
    std::uint64_t highlightSafeHandoffPixelCount = 0;
    std::uint64_t recoveredHighlightPixelCount = 0;
    std::uint32_t dispatchedTileCount = 0;
};

struct GpuFusionOutput {
    std::vector<float> virtualAnchorMosaic;
    std::vector<float> varianceProxy;
    std::vector<float> mergeConfidence;
    std::vector<float> effectiveSampleCount;
    std::vector<float> recoveredHeadroomStops;
    std::vector<std::uint8_t> validityMask;
    std::vector<std::uint8_t> ownerFrame;
    std::vector<std::uint8_t> flags;
    GpuFusionDiagnostics diagnostics;
};

// Requires a current OpenGL 4.3 core context. The function owns and releases
// all temporary GL objects before it returns. False never publishes a partial
// output and is safe to follow with the CPU reference implementation.
bool FuseOpenGl(
    const GpuFusionRequest& request,
    GpuFusionOutput& output,
    std::string& error);

} // namespace Raw::Hdr
