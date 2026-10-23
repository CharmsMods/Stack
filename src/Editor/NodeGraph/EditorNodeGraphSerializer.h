#pragma once

#include "EditorNodeGraph.h"
#include "ThirdParty/json.hpp"
#include <memory>
#include <vector>

namespace EditorNodeGraph {

inline constexpr int kEditorNodeGraphSchemaVersion = 16;

nlohmann::json ExtractLayerArray(const nlohmann::json& pipelineData);
bool IsCurrentGraphPayload(const nlohmann::json& pipelineData);
// Comparison callers may omit mask bytes after comparing their immutable raster
// values separately. Persistence always uses the default complete payload.
nlohmann::json SerializeGraphPayload(const nlohmann::json& layerArray, const Graph& graph, bool includeMaskRaster = true);
void RemoveGraphLayoutFromPayload(nlohmann::json& pipelineData);
// Reads the current model atomically; incompatible or malformed data throws.
void DeserializeGraphPayload(
    const nlohmann::json& pipelineData,
    Graph& graph,
    int layerCount);

} // namespace EditorNodeGraph
