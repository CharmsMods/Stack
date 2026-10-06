#pragma once

#include "Project/RawLayerStack.h"
#include "Renderer/MaskRenderTypes.h"

namespace Stack::Project {

Stack::NodeMath::ValueDescriptor RawLayerSceneDescriptor(Raw::RawWorkingSpace space);

// Lower RAW-owned settings and mask workspaces to one executable dependency
// graph. The authored graphs remain unchanged. The source node retains its
// public identity as the completed stack's unmapped output.
bool LowerRawLayerStack(
    RenderGraphSnapshot& snapshot, const RawLayerStackState& state,
    int backgroundNodeId, std::uint64_t revision, std::string& error, int frame = 0, int duration = 120, int framesPerSecond = 30,
    const EditorNodeGraph::Graph* composition = nullptr);

// Apply to a private preview snapshot. Image thumbnails include the shared
// viewing transform; mask thumbnails read exactly the numerical attachment.
bool IsRawLayerOutputAvailable(const RenderGraphSnapshot& graph, std::string& error);

bool ConfigureRawLayerOutputPreview(RenderGraphSnapshot& graph, int nodeId,
    const std::string& socket, std::string& error);
bool ConfigureRawLayerThumbnail(RenderGraphSnapshot& graph, const std::string& layerId,
    const std::string& maskId, std::string& error);

} // namespace Stack::Project
