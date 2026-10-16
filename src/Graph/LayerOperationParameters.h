#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"

class LayerBase;

namespace Stack::GraphModel {

// A layer instance is a runtime/editor adapter. Its authored value is owned by
// the graph node and copied explicitly into a temporary edit or render value.
nlohmann::json DefaultLayerParameters(LayerType type);
nlohmann::json CaptureLayerParameters(const LayerBase& layer);
void PrepareLayerParameterDraft(LayerBase& layer, const nlohmann::json& parameters,
    const std::string& instanceName = {});
bool ValidateLayerParameters(const EditorNodeGraph::Node& node, std::string& error);
nlohmann::json LayerParameterRecords(const EditorNodeGraph::Graph& graph);
void ReindexLayerParameterRecords(EditorNodeGraph::Graph& graph);

} // namespace Stack::GraphModel
