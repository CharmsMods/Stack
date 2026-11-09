#pragma once
#include "Color.h"
#include <cstdint>

namespace Stack::Hdr {
// Immutable, embedded output policy. No file IO, mutable document state, image
// analysis, or display-dependent parameters. CPU and GPU use the same table.
inline constexpr int SdrAppearanceEdge = 129;
const std::uint16_t* SdrAppearanceTable();
Rgb SdrAppearance(Rgb hdr709);
}
