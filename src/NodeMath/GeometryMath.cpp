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
        left.filter == right.filter && left.border == right.border &&
        left.canvasRatio == right.canvasRatio && left.customCanvasAspect == right.customCanvasAspect && left.visibleAspect == right.visibleAspect &&
        left.sideBars == right.sideBars && left.topBottomBars == right.topBottomBars &&
        left.sourceScale == right.sourceScale && left.sourceX == right.sourceX && left.sourceY == right.sourceY;
}

ReformatSettings ResolveFramingCanvas(const ReformatSettings& settings, int inputWidth, int inputHeight) {
    ReformatSettings result = settings;
    const double ratios[] = {0.0, inputHeight > 0 ? double(inputWidth) / inputHeight : 0.0,
        16.0 / 9.0, 3.0 / 2.0, 4.0 / 3.0, 1.0, 9.0 / 16.0, settings.customCanvasAspect};
    if (settings.canvasRatio > 0 && settings.canvasRatio < 8 && ratios[settings.canvasRatio] > 0.0) {
        const double ratio = ratios[settings.canvasRatio];
        // Bound resolution while preserving the chosen ratio to integer-pixel precision.
        result.width = std::clamp(result.width, 1, std::max(1, int(std::min(double(kMaximumReformatDimension), std::floor(kMaximumReformatDimension * ratio)))));
        result.height = std::clamp(int(std::round(result.width / ratio)), 1, kMaximumReformatDimension);
    }
    return result;
}

FramingMapping ResolveFramingMapping(const ReformatSettings& authored, int inputWidth, int inputHeight) {
    const auto settings = ResolveFramingCanvas(authored, inputWidth, inputHeight);
    FramingMapping result;
    if (inputWidth <= 0 || inputHeight <= 0) return result;
    const double aspect = double(settings.width) / settings.height;
    if (settings.visibleAspect > 0.0f) {
        if (settings.visibleAspect > aspect) result.halfHeight = 0.5 * aspect / settings.visibleAspect;
        else result.halfWidth = 0.5 * settings.visibleAspect / aspect;
    }
    result.halfWidth = std::min(result.halfWidth, 0.5 - settings.sideBars);
    result.halfHeight = std::min(result.halfHeight, 0.5 - settings.topBottomBars);
    const double visibleWidth = settings.width * result.halfWidth * 2.0;
    const double visibleHeight = settings.height * result.halfHeight * 2.0;
    const double scale = std::max(visibleWidth / inputWidth, visibleHeight / inputHeight) * settings.sourceScale;
    result.sampleWidth = settings.width / (inputWidth * scale);
    result.sampleHeight = settings.height / (inputHeight * scale);
    const double slackX = std::max(0.0, 0.5 - result.halfWidth * result.sampleWidth);
    const double slackY = std::max(0.0, 0.5 - result.halfHeight * result.sampleHeight);
    // Translation moves the image, hence inverse sampling moves oppositely.
    result.centerX -= settings.sourceX * slackX;
    result.centerY -= settings.sourceY * slackY;
    return result;
}

std::vector<ContractIssue> ValidateReformatSettings(const ReformatSettings& settings) {
    std::vector<ContractIssue> issues;
    if (settings.canvasRatio < 0 || settings.canvasRatio > 7 ||
        !std::isfinite(settings.customCanvasAspect) || settings.customCanvasAspect < 0.01f || settings.customCanvasAspect > 100.0f ||
        !std::isfinite(settings.visibleAspect) || settings.visibleAspect < 0.0f || settings.visibleAspect > 100.0f ||
        !std::isfinite(settings.sideBars) || settings.sideBars < 0.0f || settings.sideBars > 0.45f ||
        !std::isfinite(settings.topBottomBars) || settings.topBottomBars < 0.0f || settings.topBottomBars > 0.45f ||
        !std::isfinite(settings.sourceScale) || settings.sourceScale < 1.0f || settings.sourceScale > 8.0f ||
        !std::isfinite(settings.sourceX) || std::abs(settings.sourceX) > 1.0f ||
        !std::isfinite(settings.sourceY) || std::abs(settings.sourceY) > 1.0f)
        issues.push_back({ "framing", "Framing requires finite ratios, symmetric bars, cover scale 1..8 and translation -1..1." });
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
    const auto canvas = ResolveFramingCanvas(settings, int(inputSpatial.fullWindow.width), int(inputSpatial.fullWindow.height));
    const std::int64_t width = ScaledExtent(canvas.width, renderScale.x);
    const std::int64_t height = ScaledExtent(canvas.height, renderScale.y);
    if (width <= 0 || height <= 0) return result;
    result.kind = SpatialExtentKind::Finite;
    const std::int64_t originX = inputSpatial.kind == SpatialExtentKind::Finite
        ? inputSpatial.fullWindow.x : 0;
    const std::int64_t originY = inputSpatial.kind == SpatialExtentKind::Finite
        ? inputSpatial.fullWindow.y : 0;
    result.fullWindow = { originX, originY, width, height };
    result.dataWindow = result.fullWindow;
    result.nativeWidth = canvas.width;
    result.nativeHeight = canvas.height;
    const auto framing = ResolveFramingMapping(canvas, int(inputSpatial.fullWindow.width), int(inputSpatial.fullWindow.height));
    if (inputSpatial.fullWindow.width > 0 && inputSpatial.fullWindow.height > 0) {
        const auto& t = inputSpatial.sourceTransform;
        const double x = framing.centerX - 0.5 * framing.sampleWidth;
        const double y = framing.centerY - 0.5 * framing.sampleHeight;
        result.sourceTransform = {t[0]*framing.sampleWidth,t[1]*framing.sampleHeight,t[0]*x+t[1]*y+t[2],
            t[3]*framing.sampleWidth,t[4]*framing.sampleHeight,t[3]*x+t[4]*y+t[5],
            t[6]*framing.sampleWidth,t[7]*framing.sampleHeight,t[6]*x+t[7]*y+t[8]};
    }
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

    // Framing keeps complete upstream pixels available for later reframing.
    mapping.input = MakeFiniteRegion(inputSpatial.dataWindow,
        resolvedOutput.channels.first, resolvedOutput.channels.count);
    mapping.samplesOutsideDataWindow = false;
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
    const ReformatSettings& authored) {
    const auto settings = ResolveFramingCanvas(authored, inputWidth, inputHeight);
    if (inputWidth <= 0 || inputHeight <= 0 ||
        input.size() != static_cast<std::size_t>(inputWidth) *
            static_cast<std::size_t>(inputHeight) * 4u ||
        !ValidateReformatSettings(settings).empty()) {
        return {};
    }
    std::vector<float> output(
        static_cast<std::size_t>(settings.width) *
        static_cast<std::size_t>(settings.height) * 4u, 0.0f);
    const auto framing = ResolveFramingMapping(settings, inputWidth, inputHeight);
    for (int y = 0; y < settings.height; ++y) {
        const double sourceY = (framing.centerY + (NormalizedPixelCenter(y, settings.height) - 0.5) * framing.sampleHeight) *
            static_cast<double>(inputHeight) - 0.5;
        for (int x = 0; x < settings.width; ++x) {
            const double sourceX = (framing.centerX + (NormalizedPixelCenter(x, settings.width) - 0.5) * framing.sampleWidth) *
                static_cast<double>(inputWidth) - 0.5;
            for (int c = 0; c < 4; ++c) {
                const bool bar = std::abs(NormalizedPixelCenter(x, settings.width) - 0.5) > framing.halfWidth ||
                    std::abs(NormalizedPixelCenter(y, settings.height) - 0.5) > framing.halfHeight;
                float value = 0.0f;
                if (bar) {
                    output[(std::size_t(y) * settings.width + x) * 4u + c] = c == 3 ? 1.0f : 0.0f;
                    continue;
                }
                if (settings.filter == ReconstructionFilter::Nearest) {
                    value = FetchClamp(input, inputWidth, inputHeight,
                        static_cast<int>(std::floor(sourceX + 0.5)),
                        static_cast<int>(std::floor(sourceY + 0.5)), c);
                } else {
                    const int x0 = static_cast<int>(std::floor(sourceX));
                    const int y0 = static_cast<int>(std::floor(sourceY));
                    const float tx = static_cast<float>(sourceX - static_cast<double>(x0));
                    const float ty = static_cast<float>(sourceY - static_cast<double>(y0));
                    const auto interpolate = [&](int channel, bool premultiply) {
                        const auto fetch = [&](int px, int py) {
                            float sample = FetchClamp(input, inputWidth, inputHeight, px, py, channel);
                            if (premultiply) sample *= FetchClamp(input, inputWidth, inputHeight, px, py, 3);
                            return sample;
                        };
                        const float a = fetch(x0,y0), b = fetch(x0+1,y0);
                        const float d = fetch(x0,y0+1), e = fetch(x0+1,y0+1);
                        return (a+(b-a)*tx)+((d+(e-d)*tx)-(a+(b-a)*tx))*ty;
                    };
                    value = interpolate(c,c<3);
                    if (c<3) {
                        const float alpha = interpolate(3,false);
                        value = alpha>0.0f ? value/alpha : 0.0f;
                    }
                }
                output[(static_cast<std::size_t>(y) * static_cast<std::size_t>(settings.width) +
                        static_cast<std::size_t>(x)) * 4u + static_cast<std::size_t>(c)] = value;
            }
        }
    }
    return output;
}

} // namespace Stack::NodeMath
