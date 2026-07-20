#pragma once

#include "NodeMath/ContractTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Stack::NodeMath {

enum class RenderRegionKind {
    Empty,
    Finite,
    Full,
    Global
};

struct ChannelSpan {
    std::uint32_t first = 0;
    std::uint32_t count = 4;
};

bool operator==(const ChannelSpan& left, const ChannelSpan& right);

struct RenderRegion {
    RenderRegionKind kind = RenderRegionKind::Empty;
    Rect bounds;
    ChannelSpan channels;
};

bool operator==(const RenderRegion& left, const RenderRegion& right);

struct RenderScale {
    double x = 1.0;
    double y = 1.0;
};

bool operator==(const RenderScale& left, const RenderScale& right);

struct NeighborhoodSupport {
    std::int64_t left = 0;
    std::int64_t right = 0;
    std::int64_t bottom = 0;
    std::int64_t top = 0;
};

bool operator==(const NeighborhoodSupport& left, const NeighborhoodSupport& right);

struct RegionMapping {
    RenderRegion output;
    RenderRegion input;
    NeighborhoodSupport support;
    BorderPolicy border = BorderPolicy::Transparent;
    bool samplesOutsideDataWindow = false;
    bool requiresFullInput = false;
    bool executable = true;
    std::vector<ContractIssue> issues;
};

bool IsRectEmpty(const Rect& value);
bool ContainsRect(const Rect& outer, const Rect& inner);
Rect IntersectRects(const Rect& left, const Rect& right);
Rect ExpandRect(const Rect& value, const NeighborhoodSupport& support);

RenderRegion MakeFiniteRegion(
    const Rect& bounds,
    std::uint32_t firstChannel = 0,
    std::uint32_t channelCount = 4);
RenderRegion MakeFullRegion(
    std::uint32_t firstChannel = 0,
    std::uint32_t channelCount = 4);
RenderRegion ResolveRegionAgainstSpatial(
    const RenderRegion& request,
    const SpatialDescriptor& spatial);

std::vector<ContractIssue> ValidateRenderScale(const RenderScale& scale);
std::vector<ContractIssue> ValidateRenderRegion(const RenderRegion& region);

std::int64_t GaussianBlurRadiusAtScale(double amount, double scale);
NeighborhoodSupport GaussianBlurSupport(double amount, const RenderScale& scale);

RegionMapping MapPointwiseRegion(
    const RenderRegion& outputRequest,
    const SpatialDescriptor& inputSpatial);
RegionMapping MapNeighborhoodRegion(
    const RenderRegion& outputRequest,
    const SpatialDescriptor& inputSpatial,
    const NeighborhoodSupport& support,
    BorderPolicy border);
RegionMapping MapReductionRegion(
    const RenderRegion& outputRequest,
    const SpatialDescriptor& inputSpatial);

} // namespace Stack::NodeMath
