#include "Graph/LayerOperationParameters.h"
#include "Editor/LayerRegistry.h"
#include "Editor/Layers/LayerBase.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Stack::GraphModel {
namespace {
bool FiniteJson(const nlohmann::json& value) {
    if (value.is_number_float()) return std::isfinite(value.get<double>());
    if (value.is_structured()) for (const auto& child:value)
        if (!FiniteJson(child)) return false;
    return true;
}
}

nlohmann::json DefaultLayerParameters(LayerType type) {
    const auto layer=LayerRegistry::CreateLayer(type);
    if (!layer) throw std::runtime_error("This effect has no installed parameter definition.");
    return CaptureLayerParameters(*layer);
}

nlohmann::json CaptureLayerParameters(const LayerBase& layer) {
    auto parameters=layer.Serialize();
    parameters["visible"]=layer.IsVisible();
    return parameters;
}

void PrepareLayerParameterDraft(LayerBase& layer, const nlohmann::json& parameters,
    const std::string& instanceName) {
    layer.Deserialize(parameters);
    layer.SetVisible(parameters.value("visible",true));
    if (!instanceName.empty()) layer.SetInstanceName(instanceName);
}

bool ValidateLayerParameters(const EditorNodeGraph::Node& node, std::string& error) {
    const auto* definition=LayerRegistry::GetDescriptor(node.layerType);
    if (!definition || node.typeId!=definition->typeId || !node.layerParameters.is_object() ||
        node.layerParameters.value("type",std::string{})!=node.typeId) {
        error="The effect has no parameters for its exact current definition."; return false;
    }
    if (!FiniteJson(node.layerParameters)) {
        error="Effect parameters must be finite."; return false;
    }
    return true;
}

nlohmann::json LayerParameterRecords(const EditorNodeGraph::Graph& graph) {
    const auto count=std::count_if(graph.GetNodes().begin(),graph.GetNodes().end(),
        [](const auto& node){return node.kind==EditorNodeGraph::NodeKind::Layer;});
    auto records=nlohmann::json::array();
    for (std::ptrdiff_t i=0; i<count; ++i) records.push_back(nullptr);
    for (const auto& node:graph.GetNodes()) {
        if (node.kind!=EditorNodeGraph::NodeKind::Layer) continue;
        std::string error;
        if (!ValidateLayerParameters(node,error)) throw std::runtime_error(error);
        if (node.layerIndex<0 || node.layerIndex>=count || !records[node.layerIndex].is_null())
            throw std::runtime_error("Effect adapter indices must be unique and contiguous.");
        records[node.layerIndex]=node.layerParameters;
    }
    return records;
}

void ReindexLayerParameterRecords(EditorNodeGraph::Graph& graph) {
    int index=0;
    for (auto& node:graph.GetNodes())
        if (node.kind==EditorNodeGraph::NodeKind::Layer) node.layerIndex=index++;
}
} // namespace Stack::GraphModel
