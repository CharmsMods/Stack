#pragma once

#include <array>
#include <cstddef>

enum class RenderPanelId : std::size_t {
    Viewport = 0,
    Outliner,
    Inspector,
    Settings,
    RenderManager,
    Statistics,
    Console,
    AovDebug,
    AssetBrowser,
    Count
};

struct RenderPanelDefinition {
    RenderPanelId id;
    const char* storageKey;
    const char* windowTitle;
    const char* toolbarLabel;
    bool defaultOpen;
};

namespace RenderPanelRegistry {

const std::array<RenderPanelDefinition, static_cast<std::size_t>(RenderPanelId::Count)>& GetDefinitions();
const RenderPanelDefinition& GetDefinition(RenderPanelId id);
std::size_t ToIndex(RenderPanelId id);

} // namespace RenderPanelRegistry
