#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/Timeline/TimelineAnimation.h"
#include <memory>
#include <string>
#include <vector>

class LayerBase;
namespace Stack::Editor {
// A view-owned edit draft. Only committed RawLayerStackDocument state is used
// by renders and saves. Runtime LayerBase instances serve mask-node controls.
struct RawLayerMaskWorkspace {
    std::string layerId;
    EditorNodeGraph::Graph graph;
    Timeline::TimelineAnimationState animation;
    std::vector<std::shared_ptr<LayerBase>> layers;
    std::uint64_t revision = 0;
    bool dirty = false;
    bool affectsPixels = false;
    int previewOutputNodeId = 0;
};
}
