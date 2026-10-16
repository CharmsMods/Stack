#pragma once

#include "Project/RawLayerStack.h"
#include <cstdint>
#include <limits>

namespace Stack::Editor {

// A single decoded draft for the selected operation. Callers take value
// copies before editing. The project remains the owner of authored settings.
class RawOperationRecipeCache {
public:
    const RawRecipe::RawDevelopmentRecipe& Read(const RawRecipe::GraphOperation& operation,
        const std::string& uuid, const std::string& document, std::uint64_t revision) {
        if (documentId != document || !layerId.empty() || operationUuid != uuid ||
            graphRevision != revision || !directOperation || directOperation->kind != operation.kind ||
            directOperation->parameters != operation.parameters) {
            recipe = RawRecipe::ReadGraphOperation(operation);
            documentId = document;
            layerId.clear();
            operationUuid = uuid;
            graphRevision = revision;
            directOperation = operation;
        }
        return recipe;
    }
    const RawRecipe::RawDevelopmentRecipe& Read(const Project::RawAdjustmentLayer& layer,
        const std::string& uuid, const std::string& document, std::uint64_t revision) {
        if (documentId != document || layerId != layer.id || operationUuid != uuid || graphRevision != revision) {
            auto next = Project::ReadRawLayerOperation(layer,uuid);
            recipe = std::move(next);
            directOperation.reset();
            documentId = document;
            layerId = layer.id;
            operationUuid = uuid;
            graphRevision = revision;
        }
        return recipe;
    }
private:
    std::string documentId, layerId, operationUuid;
    std::uint64_t graphRevision = std::numeric_limits<std::uint64_t>::max();
    RawRecipe::RawDevelopmentRecipe recipe;
    std::optional<RawRecipe::GraphOperation> directOperation;
};

} // namespace Stack::Editor
