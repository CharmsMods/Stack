#pragma once

#include "Editor/EditorModuleTypes.h"
#include "ThirdParty/json.hpp"
#include <string>
#include <utility>
#include <vector>

namespace Stack::EditorPersistence {

struct CompositePersistenceState {
    std::vector<Stack::EditorModuleTypes::PersistedCompositeSceneEntry> entries;
    std::vector<int> zOrder;
    Stack::EditorModuleTypes::CompositeExportSettings exportSettings;
};

inline Stack::EditorModuleTypes::CompositeExportBoundsMode ExportBoundsModeFromToken(const std::string& token) {
    return token == "custom"
        ? Stack::EditorModuleTypes::CompositeExportBoundsMode::Custom
        : Stack::EditorModuleTypes::CompositeExportBoundsMode::Auto;
}

inline Stack::EditorModuleTypes::CompositeExportBackgroundMode ExportBackgroundModeFromToken(const std::string& token) {
    return token == "solid"
        ? Stack::EditorModuleTypes::CompositeExportBackgroundMode::Solid
        : Stack::EditorModuleTypes::CompositeExportBackgroundMode::Transparent;
}

inline Stack::EditorModuleTypes::CompositeExportAspectPreset ExportAspectPresetFromToken(const std::string& token) {
    if (token == "4:3") return Stack::EditorModuleTypes::CompositeExportAspectPreset::Ratio4x3;
    if (token == "3:2") return Stack::EditorModuleTypes::CompositeExportAspectPreset::Ratio3x2;
    if (token == "16:9") return Stack::EditorModuleTypes::CompositeExportAspectPreset::Ratio16x9;
    if (token == "9:16") return Stack::EditorModuleTypes::CompositeExportAspectPreset::Ratio9x16;
    if (token == "2:3") return Stack::EditorModuleTypes::CompositeExportAspectPreset::Ratio2x3;
    if (token == "5:4") return Stack::EditorModuleTypes::CompositeExportAspectPreset::Ratio5x4;
    if (token == "21:9") return Stack::EditorModuleTypes::CompositeExportAspectPreset::Ratio21x9;
    if (token == "custom") return Stack::EditorModuleTypes::CompositeExportAspectPreset::Custom;
    return Stack::EditorModuleTypes::CompositeExportAspectPreset::Ratio1x1;
}

// Decode into an isolated candidate. Callers publish only after every authored
// field has been read, and load preflight uses the exact same parser.
inline CompositePersistenceState ReadCompositePersistence(const nlohmann::json& pipelineData) {
    CompositePersistenceState candidate;
    if (!pipelineData.is_object()) {
        return candidate;
    }

    const nlohmann::json composite = pipelineData.value("editorComposite", nlohmann::json::object());
    if (!composite.is_object()) {
        return candidate;
    }

    const nlohmann::json sceneItems = composite.value("sceneItems", nlohmann::json::array());
    if (sceneItems.is_array()) {
        for (const auto& item : sceneItems) {
            if (!item.is_object()) {
                continue;
            }
            Stack::EditorModuleTypes::PersistedCompositeSceneEntry entry;
            entry.outputNodeId = item.value("outputNodeId", -1);
            const nlohmann::json position = item.value("position", nlohmann::json::array());
            const nlohmann::json scale = item.value("scale", nlohmann::json::array());
            if (position.is_array() && position.size() >= 2) {
                entry.position.x = position[0].get<float>();
                entry.position.y = position[1].get<float>();
            }
            if (scale.is_array() && scale.size() >= 2) {
                entry.scale.x = scale[0].get<float>();
                entry.scale.y = scale[1].get<float>();
            }
            entry.rotation = item.value("rotation", 0.0f);
            entry.visible = item.value("visible", true);
            entry.locked = item.value("locked", false);
            if (entry.outputNodeId > 0) {
                candidate.entries.push_back(std::move(entry));
            }
        }
    }

    const nlohmann::json zOrder = composite.value("zOrder", nlohmann::json::array());
    if (zOrder.is_array()) {
        for (const auto& value : zOrder) {
            if (value.is_number_integer()) {
                candidate.zOrder.push_back(value.get<int>());
            }
        }
    }

    const nlohmann::json exportSettings = composite.value("exportSettings", nlohmann::json::object());
    if (exportSettings.is_object()) {
        candidate.exportSettings.boundsMode = ExportBoundsModeFromToken(exportSettings.value("boundsMode", std::string("auto")));
        candidate.exportSettings.backgroundMode = ExportBackgroundModeFromToken(exportSettings.value("backgroundMode", std::string("transparent")));
        const nlohmann::json backgroundColor = exportSettings.value("backgroundColor", nlohmann::json::array());
        for (int i = 0; i < 4; ++i) {
            if (backgroundColor.is_array() && backgroundColor.size() > static_cast<size_t>(i)) {
                candidate.exportSettings.backgroundColor[i] = backgroundColor[i].get<float>();
            }
        }
        candidate.exportSettings.customX = exportSettings.value("customX", candidate.exportSettings.customX);
        candidate.exportSettings.customY = exportSettings.value("customY", candidate.exportSettings.customY);
        candidate.exportSettings.customWidth = exportSettings.value("customWidth", candidate.exportSettings.customWidth);
        candidate.exportSettings.customHeight = exportSettings.value("customHeight", candidate.exportSettings.customHeight);
        candidate.exportSettings.aspectPreset = ExportAspectPresetFromToken(exportSettings.value("aspectPreset", std::string("1:1")));
        candidate.exportSettings.customAspectRatio = exportSettings.value("customAspectRatio", candidate.exportSettings.customAspectRatio);
        candidate.exportSettings.outputWidth = exportSettings.value("outputWidth", candidate.exportSettings.outputWidth);
        candidate.exportSettings.outputHeight = exportSettings.value("outputHeight", candidate.exportSettings.outputHeight);
    }
    return candidate;
}

} // namespace Stack::EditorPersistence
