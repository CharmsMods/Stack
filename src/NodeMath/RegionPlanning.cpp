#include "NodeMath/RegionPlanning.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Stack::NodeMath {
namespace {

std::int64_t SaturatingAdd(std::int64_t left, std::int64_t right) {
    if (right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) {
        return std::numeric_limits<std::int64_t>::max();
    }
    if (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return left + right;
}

std::int64_t RectMaximum(std::int64_t origin, std::int64_t size) {
    return SaturatingAdd(origin, std::max<std::int64_t>(0, size));
}

bool SupportIsValid(const NeighborhoodSupport& support) {
    return support.left >= 0 && support.right >= 0 &&
        support.bottom >= 0 && support.top >= 0;
}

RegionMapping InvalidMapping(
    const RenderRegion& request,
    const std::string& field,
    const std::string& message) {
    RegionMapping result;
    result.output = request;
    result.executable = false;
    result.issues.push_back({ field, message });
    return result;
}

} // namespace

bool operator==(const ChannelSpan& left, const ChannelSpan& right) {
    return left.first == right.first && left.count == right.count;
}

bool operator==(const RenderRegion& left, const RenderRegion& right) {
    return left.kind == right.kind && left.bounds == right.bounds &&
        left.channels == right.channels;
}

bool operator==(const RenderScale& left, const RenderScale& right) {
    return left.x == right.x && left.y == right.y;
}

bool operator==(const NeighborhoodSupport& left, const NeighborhoodSupport& right) {
    return left.left == right.left && left.right == right.right &&
        left.bottom == right.bottom && left.top == right.top;
}

bool IsRectEmpty(const Rect& value) {
    return value.width <= 0 || value.height <= 0;
}

bool ContainsRect(const Rect& outer, const Rect& inner) {
    if (IsRectEmpty(inner)) return true;
    if (IsRectEmpty(outer)) return false;
    return inner.x >= outer.x && inner.y >= outer.y &&
        RectMaximum(inner.x, inner.width) <= RectMaximum(outer.x, outer.width) &&
        RectMaximum(inner.y, inner.height) <= RectMaximum(outer.y, outer.height);
}

Rect IntersectRects(const Rect& left, const Rect& right) {
    const std::int64_t minimumX = std::max(left.x, right.x);
    const std::int64_t minimumY = std::max(left.y, right.y);
    const std::int64_t maximumX = std::min(
        RectMaximum(left.x, left.width), RectMaximum(right.x, right.width));
    const std::int64_t maximumY = std::min(
        RectMaximum(left.y, left.height), RectMaximum(right.y, right.height));
    if (maximumX <= minimumX || maximumY <= minimumY) {
        return { minimumX, minimumY, 0, 0 };
    }
    return { minimumX, minimumY, maximumX - minimumX, maximumY - minimumY };
}

Rect ExpandRect(const Rect& value, const NeighborhoodSupport& support) {
    if (IsRectEmpty(value) || !SupportIsValid(support)) return value;
    const std::int64_t x = SaturatingAdd(value.x, -support.left);
    const std::int64_t y = SaturatingAdd(value.y, -support.bottom);
    const std::int64_t maximumX = SaturatingAdd(
        RectMaximum(value.x, value.width), support.right);
    const std::int64_t maximumY = SaturatingAdd(
        RectMaximum(value.y, value.height), support.top);
    return {
        x,
        y,
        std::max<std::int64_t>(0, maximumX - x),
        std::max<std::int64_t>(0, maximumY - y)
    };
}

RenderRegion MakeFiniteRegion(
    const Rect& bounds,
    std::uint32_t firstChannel,
    std::uint32_t channelCount) {
    RenderRegion result;
    result.kind = IsRectEmpty(bounds) ? RenderRegionKind::Empty : RenderRegionKind::Finite;
    result.bounds = bounds;
    result.channels = { firstChannel, channelCount };
    return result;
}

RenderRegion MakeFullRegion(
    std::uint32_t firstChannel,
    std::uint32_t channelCount) {
    RenderRegion result;
    result.kind = RenderRegionKind::Full;
    result.channels = { firstChannel, channelCount };
    return result;
}

RenderRegion ResolveRegionAgainstSpatial(
    const RenderRegion& request,
    const SpatialDescriptor& spatial) {
    RenderRegion result = request;
    if (spatial.kind == SpatialExtentKind::Empty || request.kind == RenderRegionKind::Empty) {
        result.kind = RenderRegionKind::Empty;
        result.bounds = {};
        return result;
    }
    if (request.kind == RenderRegionKind::Full || request.kind == RenderRegionKind::Global) {
        result.kind = RenderRegionKind::Finite;
        result.bounds = spatial.dataWindow;
        return result;
    }
    if (request.kind == RenderRegionKind::Finite) {
        result.bounds = IntersectRects(request.bounds, spatial.fullWindow);
        if (IsRectEmpty(result.bounds)) result.kind = RenderRegionKind::Empty;
    }
    return result;
}

std::vector<ContractIssue> ValidateRenderScale(const RenderScale& scale) {
    std::vector<ContractIssue> issues;
    if (!std::isfinite(scale.x) || !std::isfinite(scale.y) ||
        scale.x <= 0.0 || scale.y <= 0.0) {
        issues.push_back({ "renderScale", "render scale must be finite and positive in both axes" });
    }
    return issues;
}

std::vector<ContractIssue> ValidateRenderRegion(const RenderRegion& region) {
    std::vector<ContractIssue> issues;
    if (region.channels.count == 0 ||
        region.channels.first > std::numeric_limits<std::uint32_t>::max() - region.channels.count) {
        issues.push_back({ "channels", "render region channel span must be nonempty and finite" });
    }
    if (region.kind == RenderRegionKind::Finite && IsRectEmpty(region.bounds)) {
        issues.push_back({ "region", "finite render region must have positive width and height" });
    }
    if (region.kind != RenderRegionKind::Finite && !IsRectEmpty(region.bounds)) {
        issues.push_back({ "region", "only a finite render region may carry explicit bounds" });
    }
    return issues;
}

std::int64_t GaussianBlurRadiusAtScale(double amount, double scale) {
    if (!std::isfinite(amount) || !std::isfinite(scale) || scale <= 0.0) return -1;
    const double scaled = std::max(1.0, amount * scale);
    if (scaled >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        return -1;
    }
    return static_cast<std::int64_t>(scaled);
}

NeighborhoodSupport GaussianBlurSupport(double amount, const RenderScale& scale) {
    const std::int64_t radiusX = GaussianBlurRadiusAtScale(amount, scale.x);
    const std::int64_t radiusY = GaussianBlurRadiusAtScale(amount, scale.y);
    if (radiusX < 0 || radiusY < 0) return { -1, -1, -1, -1 };
    return { radiusX, radiusX, radiusY, radiusY };
}

RegionMapping MapPointwiseRegion(
    const RenderRegion& outputRequest,
    const SpatialDescriptor& inputSpatial) {
    const std::vector<ContractIssue> requestIssues = ValidateRenderRegion(outputRequest);
    if (!requestIssues.empty()) {
        RegionMapping result;
        result.output = outputRequest;
        result.executable = false;
        result.issues = requestIssues;
        return result;
    }
    RegionMapping result;
    result.output = ResolveRegionAgainstSpatial(outputRequest, inputSpatial);
    result.input = result.output;
    return result;
}

RegionMapping MapNeighborhoodRegion(
    const RenderRegion& outputRequest,
    const SpatialDescriptor& inputSpatial,
    const NeighborhoodSupport& support,
    BorderPolicy border) {
    if (!SupportIsValid(support)) {
        return InvalidMapping(outputRequest, "support", "neighborhood support must be nonnegative");
    }
    RegionMapping result = MapPointwiseRegion(outputRequest, inputSpatial);
    result.support = support;
    result.border = border;
    if (!result.executable || result.output.kind == RenderRegionKind::Empty) return result;
    const Rect expanded = ExpandRect(result.output.bounds, support);
    result.input = MakeFiniteRegion(
        IntersectRects(expanded, inputSpatial.dataWindow),
        outputRequest.channels.first,
        outputRequest.channels.count);
    result.samplesOutsideDataWindow = !ContainsRect(inputSpatial.dataWindow, expanded);
    return result;
}

RegionMapping MapReductionRegion(
    const RenderRegion& outputRequest,
    const SpatialDescriptor& inputSpatial) {
    RegionMapping result = MapPointwiseRegion(outputRequest, inputSpatial);
    if (!result.executable) return result;
    result.requiresFullInput = true;
    result.input.kind = inputSpatial.kind == SpatialExtentKind::Empty
        ? RenderRegionKind::Empty : RenderRegionKind::Finite;
    result.input.bounds = inputSpatial.kind == SpatialExtentKind::Empty
        ? Rect{} : inputSpatial.dataWindow;
    return result;
}

} // namespace Stack::NodeMath
