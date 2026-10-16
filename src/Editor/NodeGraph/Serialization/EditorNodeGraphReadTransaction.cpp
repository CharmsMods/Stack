#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "EditorNodeGraphCurrentReader.h"
#include "Graph/LayerOperationParameters.h"
#include <stdexcept>

namespace EditorNodeGraph {
void DeserializeGraphPayload(const nlohmann::json& payload, Graph& graph, int layerCount) {
    if (!payload.contains("nodeGraph") || !IsCurrentGraphPayload(payload))
        throw std::runtime_error("Unsupported graph model generation: saved " +
            std::to_string(payload.value("nodeGraph",nlohmann::json::object()).value("version",0)) +
            ", expected " + std::to_string(kEditorNodeGraphSchemaVersion) + ".");
    Graph candidate;
    Serialization::ReadCurrentGraphPayload(payload, candidate, layerCount);
    Stack::GraphModel::LayerParameterRecords(candidate);
    graph = std::move(candidate);
}
}
