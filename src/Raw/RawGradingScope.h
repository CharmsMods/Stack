#pragma once

#include "Raw/RawImageData.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class RawDevelopmentGradingScopeSource {
    None = 0,
    NeutralScene = 1,
    DisplayCandidate = 2
};

struct RawGradingScopePoint {
    float x = 0.0f;
    float y = 0.0f;
    std::array<float, 4> color {};
};

struct RawGradingScopeVisualization {
    RawDevelopmentGradingScopeSource source = RawDevelopmentGradingScopeSource::None;
    std::string sourceKey;
    std::uint64_t generation = 0;
    static constexpr int kHistogramBins = 256;
    static constexpr int kVectorscopeResolution = 192;
    static constexpr int kParadeColumns = 192;
    static constexpr int kParadeRows = 128;
    std::array<float, kHistogramBins> histogram {};
    std::vector<RawGradingScopePoint> vectorscopePoints;
    std::vector<RawGradingScopePoint> paradePoints;
};

struct RawDevelopmentGradingScopeReadback {
    bool valid = false;
    RawDevelopmentGradingScopeSource source =
        RawDevelopmentGradingScopeSource::None;
    std::string sourceKey;
    std::string measurementDomain;
    Raw::RawWorkingSpace workingSpace = Raw::RawWorkingSpace::LinearSrgbD65;
    bool sceneLinear = false;
    bool encodedSrgb = false;
    std::uint64_t generation = 0;
    int width = 0;
    int height = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    std::vector<float> pixels;
    std::vector<float> coverage; // Empty means fully opaque.
    std::shared_ptr<const RawGradingScopeVisualization> visualization;
};

namespace Raw {

// Histogram floats followed by RGBA8 vectorscope/parade bins. Empty bins are zero.
inline constexpr std::size_t kGradingScopePackedValueCount =
    RawGradingScopeVisualization::kHistogramBins +
    RawGradingScopeVisualization::kVectorscopeResolution * RawGradingScopeVisualization::kVectorscopeResolution +
    3 * RawGradingScopeVisualization::kParadeColumns * RawGradingScopeVisualization::kParadeRows;

std::shared_ptr<const RawGradingScopeVisualization> BuildGradingScopeVisualizationFromPacked(
    const RawDevelopmentGradingScopeReadback& readback, const std::vector<std::uint32_t>& packed);

// Called by the readback owner once per completed packet. Consumers share the
// immutable plot data; the UI never converts or bins source pixels.
std::shared_ptr<const RawGradingScopeVisualization> BuildGradingScopeVisualization(
    const RawDevelopmentGradingScopeReadback& readback);

} // namespace Raw
