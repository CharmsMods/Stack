#pragma once

#include "NodeMath/RegionPlanning.h"

#include <cstddef>
#include <vector>

namespace Stack::NodeMath {

inline constexpr int kReformatAlgorithmVersion = 2;
inline constexpr int kMaximumReformatDimension = 32768;

struct ReformatSettings {
    int width = 1920;
    int height = 1080;
    ReconstructionFilter filter = ReconstructionFilter::Linear;
    BorderPolicy border = BorderPolicy::Clamp;
    // Custom uses exact width/height. Other ratios derive height from width.
    int canvasRatio = 0; // Exact dimensions, Original, 16:9, 3:2, 4:3, Square, Portrait 9:16, Custom ratio
    float customCanvasAspect = 16.0f / 9.0f;
    float visibleAspect = 0.0f; // zero disables aspect bars
    float sideBars = 0.0f; // symmetric fraction on each edge
    float topBottomBars = 0.0f;
    float sourceScale = 1.0f; // relative to cover, never exposes empty canvas
    float sourceX = 0.0f; // -1..1 across available source slack
    float sourceY = 0.0f;
};

ReformatSettings ResolveFramingCanvas(const ReformatSettings& settings, int inputWidth, int inputHeight);
struct FramingMapping {
    double halfWidth = 0.5, halfHeight = 0.5;
    double sampleWidth = 1.0, sampleHeight = 1.0;
    double centerX = 0.5, centerY = 0.5;
};
FramingMapping ResolveFramingMapping(const ReformatSettings& settings, int inputWidth, int inputHeight);

bool operator==(const ReformatSettings& left, const ReformatSettings& right);

std::vector<ContractIssue> ValidateReformatSettings(const ReformatSettings& settings);
SpatialDescriptor ReformatOutputSpatial(
    const SpatialDescriptor& inputSpatial,
    const ReformatSettings& settings,
    const RenderScale& renderScale = {});
RegionMapping MapReformatRegion(
    const RenderRegion& outputRequest,
    const SpatialDescriptor& inputSpatial,
    const ReformatSettings& settings,
    const RenderScale& renderScale = {});

// Normalized pixel-center position. This is the scale-independent coordinate
// used by both full-resolution and proxy executions.
double NormalizedPixelCenter(std::int64_t pixelIndex, std::int64_t extent);

// CPU reference used by generated verification. Input and output are RGBA in
// row-major order with a bottom-left raster origin, matching the live graph.
std::vector<float> ReformatRgbaReference(
    const std::vector<float>& input,
    int inputWidth,
    int inputHeight,
    const ReformatSettings& settings);

} // namespace Stack::NodeMath
