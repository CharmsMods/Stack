#pragma once
#include "ProcessingInternal.h"

namespace Raw::Bracketing {
inline constexpr const char* kHdrColorFusionRevision = "color-supported-hdr-v5-clipped-capture-fallback";
using ColorObservations = std::array<std::vector<Observation>,4>;

// Bayer cells use four sites; reconstructed RGB uses three. Source validity and
// scene values stay per channel, while exposure transitions share color support.
std::array<Pixel,4> BlendColor(const ColorObservations&,const std::vector<double>& requested,
    const BracketingRecipe&,unsigned sites=4);
void GatherColorCell(const std::vector<std::vector<Observation>>& groups,
    std::size_t topLeft,std::size_t rowStride,ColorObservations&);
}
