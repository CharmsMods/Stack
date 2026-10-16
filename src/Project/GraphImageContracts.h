#pragma once
#include "Renderer/MaskRenderTypes.h"
#include <optional>

namespace Stack::Project {
// RAW operation math supports these two explicit scene RGB bases. Both the
// renderer and its editing interface must interpret incoming pixels alike.
std::optional<Raw::RawWorkingSpace> RawOperationInputWorkingSpace(
    const NodeMath::ValueDescriptor& input);
// Rebind after graph expansion and source/reference resolution. These facts
// belong to each image value, not to a selected editor or Background lookup.
void BindGraphImageContracts(RenderGraphSnapshot& snapshot);
}
