#pragma once
#include "Renderer/MaskRenderTypes.h"

namespace Stack::Project {
// Rebind after graph expansion and source/reference resolution. These facts
// belong to each image value, not to a selected editor or Background lookup.
void BindGraphImageContracts(RenderGraphSnapshot& snapshot);
}
