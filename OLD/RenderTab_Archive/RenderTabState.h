#pragma once

#include "PanelRegistry.h"
#include <array>
#include <filesystem>

class RenderTabState {
public:
    RenderTabState();

    void Initialize();
    void Shutdown();

    bool IsPanelOpen(RenderPanelId id) const;
    void SetPanelOpen(RenderPanelId id, bool isOpen);

    bool IsToolbarVisible() const { return m_ShowToolbar; }
    void SetToolbarVisible(bool visible);

    bool WasDefaultLayoutApplied() const { return m_DefaultLayoutApplied; }
    void SetDefaultLayoutApplied(bool applied);

    void SaveIfDirty();
    void MarkDirty() { m_Dirty = true; }

    const std::filesystem::path& GetStateFilePath() const { return m_StateFilePath; }

private:
    void ResetToDefaults();
    void Load();
    void Save();

    std::array<bool, static_cast<std::size_t>(RenderPanelId::Count)> m_PanelOpen {};
    bool m_ShowToolbar = true;
    bool m_DefaultLayoutApplied = false;
    bool m_Dirty = false;
    bool m_Initialized = false;
    std::filesystem::path m_StateFilePath;
};
