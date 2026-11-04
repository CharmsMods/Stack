#pragma once
#include "Persistence/ProjectStore.h"
#include "Renderer/MaskRenderTypes.h"

namespace Stack::GalleryPresets {
// Comparison presets deliberately substitute the chosen sample at all source
// slots. A project cover binds only its original RAW or adopted result.
RenderGraphSnapshot PrepareGraph(Project::ProjectStoreOpenResult& opened,
    const std::shared_ptr<const Raw::RawImageData>& image, bool comparison, bool savedBracket = false);
}
