#include "NodeMath/GeometryMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Stack::NodeMath {
namespace {

std::int64_t ScaledExtent(int value, double scale) {
    if (value <= 0 || !std::isfinite(scale) || scale <= 0.0) return 0;
    const double scaled = std::round(static_cast<double>(value) * scale);
    if (scaled < 1.0 || scaled > static_cast<double>(kMaximumReformatDimension)) return 0;
    return static_cast<std::int64_t>(scaled);
}

float FetchClamp(
    const std::vector<float>& input,
    int width,
    int height,
    int x,
    int y,
    int channel) {
    x = std::clamp(x, 0, width - 1);
    y = std::clamp(y, 0, height - 1);
    return input[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                  static_cast<std::size_t>(x)) * 4u + static_cast<std::size_t>(channel)];
}

} // namespace

bool operator==(const ReformatSettings& left, const ReformatSettings& right) {
    return left.width == right.width && left.height == right.height &&
        left.filter == right.filter && left.border == right.border;
}

std::vector<ContractIssue> ValidateReformatSettings(const ReformatSettings& settings) {
    std::vector<ContractIssue> issues;
    if (settings.width <= 0 || settings.width > kMaximumReformatDimension) {
        issues.push_back({ "width", "Reformat width must be between 1 and 32768 pixels." });
    }
    if (settings.height <= 0 || settings.height > kMaximumReformatDimension) {
        issues.push_back({ "height", "Reformat height must be between 1 and 32768 pixels." });
    }
    if (settings.filter != ReconstructionFilter::Nearest &&
        settings.filter != ReconstructionFilter::Linear) {
        issues.push_back({ "filter", "The first Reformat contract supports only Nearest or Linear reconstruction." });
    }
    if (settings.border != BorderPolicy::Clamp) {
        issues.push_back({ "border", "The first Reformat contract requires an explicit Clamp border." });
    }
    return issues;
}

SpatialDescriptor ReformatOutputSpatial(
    const SpatialDescriptor& inputSpatial,
    const ReformatSettings& settings,
    const RenderScale& renderScale) {
    SpatialDescriptor result;
    if (!ValidateReformatSettings(settings).empty() ||
        !ValidateRenderScale(renderScale).empty()) {
        return result;
    }
    const std::int64_t width = ScaledExtent(settings.width, renderScale.x);
    const std::int64_t height = ScaledExtent(settings.height, renderScale.y);
    if (width <= 0 || height <= 0) return result;
    result.kind = SpatialExtentKind::Finite;
    const std::int64_t originX = inputSpatial.kind == SpatialExtentKind::Finite
        ? inputSpatial.fullWindow.x : 0;
    const std::int64_t originY = inputSpatial.kind == SpatialExtentKind::Finite
        ? inputSpatial.fullWindow.y : 0;
    result.fullWindow = { originX, originY, width, height };
    result.dataWindow = result.fullWindow;
    result.rasterOrigin = inputSpatial.rasterOrigin;
    result.pixelAspect = inputSpatial.pixelAspect > 0.0
        ? inputSpatial.pixelAspect : 1.0;
    return result;
}

RegionMapping MapReformatRegion(
    const RenderRegion& outputRequest,
    const SpatialDescriptor& inputSpatial,
    const ReformatSettings& settings,
    const RenderScale& renderScale) {
    RegionMapping mapping;
    mapping.output = outputRequest;
    mapping.border = settings.border;
    mapping.issues = ValidateReformatSettings(settings);
    const auto scaleIssues = ValidateRenderScale(renderScale);
    mapping.issues.insert(mapping.issues.end(), scaleIssues.begin(), scaleIssues.end());
    if (inputSpatial.kind != SpatialExtentKind::Finite ||
        inputSpatial.fullWindow.width <= 0 || inputSpatial.fullWindow.height <= 0) {
        mapping.issues.push_back({ "inputSpatial", "Reformat requires a finite input extent." });
    }
    const SpatialDescriptor outputSpatial =
        ReformatOutputSpatial(inputSpatial, settings, renderScale);
    if (outputSpatial.kind != SpatialExtentKind::Finite) {
        mapping.executable = false;
        return mapping;
    }
    const RenderRegion resolvedOutput = ResolveRegionAgainstSpatial(outputRequest, outputSpatial);
    mapping.output = resolvedOutput;
    if (resolvedOutput.kind == RenderRegionKind::Empty || IsRectEmpty(resolvedOutput.bounds)) {
        mapping.input = MakeFiniteRegion({}, resolvedOutput.channels.first, resolvedOutput.channels.count);
        mapping.executable = mapping.issues.empty();
        return mapping;
    }

    const double inputWidth = static_cast<double>(inputSpatial.fullWindow.width);
    const double inputHeight = static_cast<double>(inputSpatial.fullWindow.height);
    const double outputWidth = static_cast<double>(outputSpatial.fullWindow.width);
    const double outputHeight = static_cast<double>(outputSpatial.fullWindow.height);
    const double localMinX = static_cast<double>(resolvedOutput.bounds.x - outputSpatial.fullWindow.x);
    const double localMinY = static_cast<double>(resolvedOutput.bounds.y - outputSpatial.fullWindow.y);
    const double localMaxX = localMinX + static_cast<double>(resolvedOutput.bounds.width);
    const double localMaxY = localMinY + static_cast<double>(resolvedOutput.bounds.height);
    const double footprint = settings.filter == ReconstructionFilter::Linear ? 1.0 : 0.5;
    const std::int64_t minX = inputSpatial.fullWindow.x + static_cast<std::int64_t>(
        std::floor(localMinX * inputWidth / outputWidth - footprint));
    const std::int64_t minY = inputSpatial.fullWindow.y + static_cast<std::int64_t>(
        std::floor(localMinY * inputHeight / outputHeight - footprint));
    const std::int64_t maxX = inputSpatial.fullWindow.x + static_cast<std::int64_t>(
        std::ceil(localMaxX * inputWidth / outputWidth + footprint));
    const std::int64_t maxY = inputSpatial.fullWindow.y + static_cast<std::int64_t>(
        std::ceil(localMaxY * inputHeight / outputHeight + footprint));
    const Rect requestedInput { minX, minY, std::max<std::int64_t>(0, maxX - minX),
        std::max<std::int64_t>(0, maxY - minY) };
    const Rect clipped = IntersectRects(requestedInput, inputSpatial.dataWindow);
    mapping.input = MakeFiniteRegion(
        clipped, resolvedOutput.channels.first, resolvedOutput.channels.count);
    mapping.samplesOutsideDataWindow = !ContainsRect(inputSpatial.dataWindow, requestedInput);
    mapping.support = settings.filter == ReconstructionFilter::Linear
        ? NeighborhoodSupport{ 1, 1, 1, 1 }
        : NeighborhoodSupport{};
    mapping.executable = mapping.issues.empty();
    return mapping;
}

double NormalizedPixelCenter(std::int64_t pixelIndex, std::int64_t extent) {
    if (pixelIndex < 0 || extent <= 0 || pixelIndex >= extent) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return (static_cast<double>(pixelIndex) + 0.5) / static_cast<double>(extent);
}

std::vector<float> ReformatRgbaReference(
    const std::vector<float>& input,
    int inputWidth,
    int inputHeight,
    const ReformatSettings& settings) {
    if (inputWidth <= 0 || inputHeight <= 0 ||
        input.size() != static_cast<std::size_t>(inputWidth) *
            static_cast<std::size_t>(inputHeight) * 4u ||
        !ValidateReformatSettings(settings).empty()) {
        return {};
    }
    std::vector<float> output(
        static_cast<std::size_t>(settings.width) *
        static_cast<std::size_t>(settings.height) * 4u, 0.0f);
    for (int y = 0; y < settings.height; ++y) {
        const double sourceY = NormalizedPixelCenter(y, settings.height) *
            static_cast<double>(inputHeight) - 0.5;
        for (int x = 0; x < settings.width; ++x) {
            const double sourceX = NormalizedPixelCenter(x, settings.width) *
                static_cast<double>(inputWidth) - 0.5;
            for (int c = 0; c < 4; ++c) {
                float value = 0.0f;
                if (settings.filter == ReconstructionFilter::Nearest) {
                    value = FetchClamp(input, inputWidth, inputHeight,
                        static_cast<int>(std::floor(sourceX + 0.5)),
                        static_cast<int>(std::floor(sourceY + 0.5)), c);
                } else {
                    const int x0 = static_cast<int>(std::floor(sourceX));
                    const int y0 = static_cast<int>(std::floor(sourceY));
                    const float tx = static_cast<float>(sourceX - static_cast<double>(x0));
                    const float ty = static_cast<float>(sourceY - static_cast<double>(y0));
                    const float a = FetchClamp(input, inputWidth, inputHeight, x0, y0, c);
                    const float b = FetchClamp(input, inputWidth, inputHeight, x0 + 1, y0, c);
                    const float d = FetchClamp(input, inputWidth, inputHeight, x0, y0 + 1, c);
                    const float e = FetchClamp(input, inputWidth, inputHeight, x0 + 1, y0 + 1, c);
                    value = (a + (b - a) * tx) +
                        ((d + (e - d) * tx) - (a + (b - a) * tx)) * ty;
                }
                output[(static_cast<std::size_t>(y) * static_cast<std::size_t>(settings.width) +
                        static_cast<std::size_t>(x)) * 4u + static_cast<std::size_t>(c)] = value;
            }
        }
    }
    return output;
}

} // namespace Stack::NodeMath
