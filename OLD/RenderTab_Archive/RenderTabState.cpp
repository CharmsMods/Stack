#include "RenderTabState.h"

#include "ThirdParty/json.hpp"
#include <fstream>

namespace {

using json = nlohmann::json;

constexpr int kRenderTabStateVersion = 1;

} // namespace

RenderTabState::RenderTabState() {
    ResetToDefaults();
}

void RenderTabState::Initialize() {
    if (m_Initialized) {
        return;
    }

    ResetToDefaults();
    m_StateFilePath = std::filesystem::current_path() / "Render" / "render_tab_state.json";
    Load();
    m_Initialized = true;
}

void RenderTabState::Shutdown() {
    SaveIfDirty();
}

bool RenderTabState::IsPanelOpen(RenderPanelId id) const {
    return m_PanelOpen[RenderPanelRegistry::ToIndex(id)];
}

void RenderTabState::SetPanelOpen(RenderPanelId id, bool isOpen) {
    const std::size_t index = RenderPanelRegistry::ToIndex(id);
    if (m_PanelOpen[index] == isOpen) {
        return;
    }

    m_PanelOpen[index] = isOpen;
    m_Dirty = true;
}

void RenderTabState::SetToolbarVisible(bool visible) {
    if (m_ShowToolbar == visible) {
        return;
    }

    m_ShowToolbar = visible;
    m_Dirty = true;
}

void RenderTabState::SetDefaultLayoutApplied(bool applied) {
    if (m_DefaultLayoutApplied == applied) {
        return;
    }

    m_DefaultLayoutApplied = applied;
    m_Dirty = true;
}

void RenderTabState::SaveIfDirty() {
    if (!m_Initialized || !m_Dirty) {
        return;
    }

    Save();
}

void RenderTabState::ResetToDefaults() {
    for (const RenderPanelDefinition& definition : RenderPanelRegistry::GetDefinitions()) {
        m_PanelOpen[RenderPanelRegistry::ToIndex(definition.id)] = definition.defaultOpen;
    }

    m_ShowToolbar = true;
    m_DefaultLayoutApplied = false;
    m_Dirty = false;
}

void RenderTabState::Load() {
    if (m_StateFilePath.empty() || !std::filesystem::exists(m_StateFilePath)) {
        return;
    }

    try {
        std::ifstream file(m_StateFilePath, std::ios::binary);
        if (!file.is_open()) {
            return;
        }

        json root = json::parse(file);
        if (!root.is_object()) {
            return;
        }

        m_ShowToolbar = root.value("showToolbar", true);
        m_DefaultLayoutApplied = root.value("defaultLayoutApplied", false);

        const json panels = root.value("panels", json::object());
        if (panels.is_object()) {
            for (const RenderPanelDefinition& definition : RenderPanelRegistry::GetDefinitions()) {
                const bool defaultOpen = definition.defaultOpen;
                m_PanelOpen[RenderPanelRegistry::ToIndex(definition.id)] =
                    panels.value(definition.storageKey, defaultOpen);
            }
        }

        m_Dirty = false;
    } catch (...) {
        ResetToDefaults();
    }
}

void RenderTabState::Save() {
    try {
        if (!m_StateFilePath.empty() && m_StateFilePath.has_parent_path()) {
            std::filesystem::create_directories(m_StateFilePath.parent_path());
        }

        json panels = json::object();
        for (const RenderPanelDefinition& definition : RenderPanelRegistry::GetDefinitions()) {
            panels[definition.storageKey] = m_PanelOpen[RenderPanelRegistry::ToIndex(definition.id)];
        }

        json root = json::object();
        root["version"] = kRenderTabStateVersion;
        root["showToolbar"] = m_ShowToolbar;
        root["defaultLayoutApplied"] = m_DefaultLayoutApplied;
        root["panels"] = std::move(panels);

        std::ofstream file(m_StateFilePath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return;
        }

        file << root.dump(2);
        m_Dirty = false;
    } catch (...) {
    }
}
