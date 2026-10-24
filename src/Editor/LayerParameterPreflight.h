#pragma once

#include "Graph/LayerOperationParameters.h"
#include "Editor/Layers/LayerBase.h"
#include <stdexcept>

namespace Stack::EditorPersistence {
// Use the installed decoder, not a parallel parameter schema. Fresh adapters
// have no GPU resources: never InitializeGL or Execute during load preflight.
// Discard the draft instead of serializing it back into the authored document,
// so existing defaults/clamping do not become a project migration.
template<class Factory>
void PreflightLayerParameters(const nlohmann::json& records, Factory createLayer) {
    for (const auto& record:records) {
        auto draft=createLayer(record.value("type",std::string{}));
        if (!draft) throw std::runtime_error("Saved effect has no current canonical definition.");
        Stack::GraphModel::PrepareLayerParameterDraft(*draft,record);
    }
}
} // namespace Stack::EditorPersistence
