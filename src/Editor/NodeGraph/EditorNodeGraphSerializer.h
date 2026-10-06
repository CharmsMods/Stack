#pragma once

#include "EditorNodeGraph.h"
#include "ThirdParty/json.hpp"
#include <memory>
#include <vector>

namespace EditorNodeGraph {

inline constexpr int kEditorNodeGraphSchemaVersion = 10;

nlohmann::json ExtractLayerArray(const nlohmann::json& pipelineData);
bool IsCurrentGraphPayload(const nlohmann::json& pipelineData);
nlohmann::json SerializeGraphPayload(const nlohmann::json& layerArray, const Graph& graph);
void RemoveGraphLayoutFromPayload(nlohmann::json& pipelineData);
void DeserializeGraphPayload(
    const nlohmann::json& pipelineData,
    Graph& graph,
    int layerCount,
    const std::vector<unsigned char>& fallbackSourcePixels,
    int fallbackSourceWidth,
    int fallbackSourceHeight,
    int fallbackSourceChannels);

} // namespace EditorNodeGraph
