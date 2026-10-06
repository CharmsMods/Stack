#include "Project/GraphSnapshotBuilder.h"
#include "Project/GraphSnapshotConversions.h"
#include <algorithm>

namespace Stack::Project {
using namespace GraphSnapshotInternal;

std::vector<std::shared_ptr<LayerBase>> BuildGraphRenderLayers(const EditorNodeGraph::Graph& graph, const std::vector<std::shared_ptr<LayerBase>>& layers) {
    std::vector<std::shared_ptr<LayerBase>> renderLayers;
    for (int index : graph.GetRenderLayerIndexPath()) {
        if (index >= 0 && index < static_cast<int>(layers.size())) {
            renderLayers.push_back(layers[index]);
        }
    }
    return renderLayers;
}

std::vector<RenderLayerStep> BuildGraphRenderSteps(const EditorNodeGraph::Graph& graph, const std::vector<std::shared_ptr<LayerBase>>& layers) {
    std::vector<RenderLayerStep> steps;
    for (int nodeId : graph.GetRenderLayerNodePath()) {
        const EditorNodeGraph::Node* node = graph.FindNode(nodeId);
        if (!node || node->kind != EditorNodeGraph::NodeKind::Layer ||
            node->layerIndex < 0 || node->layerIndex >= static_cast<int>(layers.size())) {
            continue;
        }

        RenderLayerStep step;
        step.layer = layers[node->layerIndex];
        if (const EditorNodeGraph::Link* maskLink = graph.FindAnyInputLink(node->id, EditorNodeGraph::kMaskInputSocketId)) {
            const EditorNodeGraph::Node* maskNode = graph.FindNode(maskLink->fromNodeId);
            if (maskNode && maskNode->kind == EditorNodeGraph::NodeKind::MaskGenerator) {
                step.maskNodeId = maskNode->id;
            }
        }
        steps.push_back(std::move(step));
    }
    return steps;
}

std::vector<RenderMaskSource> BuildGraphRenderMasks(const EditorNodeGraph::Graph& graph, const std::vector<std::shared_ptr<LayerBase>>& layers) {
    std::vector<int> usedMaskNodeIds;
    for (const RenderLayerStep& step : BuildGraphRenderSteps(graph, layers)) {
        if (step.maskNodeId > 0 &&
            std::find(usedMaskNodeIds.begin(), usedMaskNodeIds.end(), step.maskNodeId) == usedMaskNodeIds.end()) {
            usedMaskNodeIds.push_back(step.maskNodeId);
        }
    }

    std::vector<RenderMaskSource> masks;
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::MaskGenerator) {
            continue;
        }
        if (std::find(usedMaskNodeIds.begin(), usedMaskNodeIds.end(), node.id) == usedMaskNodeIds.end()) {
            continue;
        }

        RenderMaskSource mask;
        mask.nodeId = node.id;
        mask.kind = ToRenderMaskKind(node.maskKind);
        mask.settings = ToRenderMaskSettings(node.maskSettings);
        masks.push_back(mask);
    }
    return masks;
}

} // namespace Stack::Project
