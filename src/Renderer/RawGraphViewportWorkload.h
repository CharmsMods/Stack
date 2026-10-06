#pragma once

#include "Raw/RawViewportWorkload.h"
#include "Renderer/MaskRenderTypes.h"

namespace Stack::Renderer {

// Graph timings describe the complete executed request. Keep slider values
// out of the cost identity so successive values can teach the same workload.
// Topology, operation count and algorithm choices still separate workloads.
inline std::array<std::size_t,Raw::kViewportStageCount> BuildRawGraphViewportWorkloadKeys(
    const Raw::ViewportModules::Recipe& recipe, const RenderGraphSnapshot& graph, int changingNodeId = 0) {
    using namespace RawDevelopmentCache;
    auto keys = Raw::ViewportWorkloadKeys(recipe);
    std::size_t shape = 1;
    HashTypedValue(shape, std::string("complete-raw-layer-graph-v1"));
    HashTypedValue(shape, graph.outputNodeId);
    HashTypedValue(shape, graph.outputSocketId);
    HashTypedValue(shape, changingNodeId);
    for (const auto& node : graph.nodes) {
        HashTypedValue(shape, node.nodeId);
        HashTypedValue(shape, static_cast<int>(node.kind));
        HashTypedValue(shape, node.definitionId);
        HashTypedValue(shape, node.definitionVersion);
        HashTypedValue(shape, node.definitionHash);
        if (node.kind == RenderGraphNodeKind::RawOperation) {
            HashTypedValue(shape, static_cast<int>(node.rawOperation.kind));
            HashTypedValue(shape, node.rawOperation.enabled);
            const auto settings = RawRecipe::ReadGraphOperation(node.rawOperation);
            for (auto cost : Raw::ViewportWorkloadKeys(settings)) HashTypedValue(shape, cost);
            for (const auto& gradient : settings.evGradients) {
                HashTypedValue(shape, gradient.mask.enabled);
                HashTypedValue(shape, RawRecipe::IsLocalRangeEnabled(gradient.curve));
                HashTypedValue(shape, gradient.curve.points.size());
            }
        } else if (node.kind == RenderGraphNodeKind::Layer) {
            HashTypedJson(shape, node.layerJson);
        } else if (node.kind == RenderGraphNodeKind::MaskGenerator) {
            HashTypedValue(shape, static_cast<int>(node.maskKind));
            HashTypedValue(shape, node.rawCoverage.value("strokes", nlohmann::json::array()).size());
        }
    }
    for (const auto& link : graph.links) {
        HashTypedValue(shape, link.fromNodeId);
        HashTypedValue(shape, link.fromSocketId);
        HashTypedValue(shape, link.toNodeId);
        HashTypedValue(shape, link.toSocketId);
    }
    for (std::size_t i = 0; i < keys.size(); ++i) {
        HashTypedValue(keys[i], shape);
        HashTypedValue(keys[i], i);
    }
    return keys;
}

} // namespace Stack::Renderer
