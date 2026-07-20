#pragma once

#include "NodeMath/RegionPlanning.h"

#include <cstddef>
#include <vector>

namespace Stack::NodeMath {

inline constexpr int kReformatAlgorithmVersion = 1;
inline constexpr int kMaximumReformatDimension = 32768;

struct ReformatSettings {
    int width = 1920;
    int height = 1080;
    ReconstructionFilter filter = ReconstructionFilter::Linear;
    BorderPolicy border = BorderPolicy::Clamp;
};

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
