#include "Project/RawLayerStack.h"

namespace Stack::Project {

GraphModel::Endpoint RawPreparedSourceEndpoint() {
    return {kRawPreparedSourceGraph, "technical", "imageOut"};
}

std::optional<GraphModel::Endpoint> RawLayerInputEndpoint(
    const RawLayerStackState& state, const std::string& layerId) {
    const auto* selected = FindRawAdjustmentLayer(state, layerId);
    if (!selected) return std::nullopt;
    if (state.explicitRouting) return selected->imageInput;
    auto input = RawPreparedSourceEndpoint();
    for (const auto* layer : RawLayersInOrder(state)) {
        if (layer == selected) return input;
        const auto* end = FindRawRole(*layer, GraphModel::NodeRole::LayerResult);
        if (!end) return std::nullopt;
        input = RawLayerEndpoint(*layer, *end);
    }
    return std::nullopt;
}

std::optional<GraphModel::Endpoint> RawMainOutputEndpoint(const RawLayerStackState& state) {
    if (state.explicitRouting) return state.imageOutput;
    const auto* last = state.layers.empty() ? &state.background : &state.layers.back();
    const auto* end = FindRawRole(*last, GraphModel::NodeRole::LayerResult);
    return end ? std::optional<GraphModel::Endpoint>(RawLayerEndpoint(*last, *end)) : std::nullopt;
}

} // namespace Stack::Project
