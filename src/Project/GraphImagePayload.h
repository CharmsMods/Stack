#pragma once

#include "Editor/NodeGraph/NodeGraphModelTypes.h"
#include "Renderer/MaskRenderTypes.h"

namespace Stack::Project {

// Reuses the payload's immutable buffer cache. Legacy owned pixel vectors
// are copied only when that cache is first materialized, as before.
SharedPixelBuffer EnsureSharedImagePixels(const EditorNodeGraph::ImagePayload& payload);
RenderGraphImagePayload BuildRenderImagePayload(const EditorNodeGraph::ImagePayload& payload);

} // namespace Stack::Project
