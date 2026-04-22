#include "PanelRegistry.h"

namespace {

constexpr std::array<RenderPanelDefinition, static_cast<std::size_t>(RenderPanelId::Count)> kDefinitions = {{
    { RenderPanelId::Viewport,      "viewport",       "Render Viewport",       "Viewport",       true  },
    { RenderPanelId::Outliner,      "outliner",       "Render Outliner",       "Outliner",       true  },
    { RenderPanelId::Inspector,     "inspector",      "Render Inspector",      "Inspector",      true  },
    { RenderPanelId::Settings,      "settings",       "Render Settings",       "Settings",       true  },
    { RenderPanelId::RenderManager, "render_manager", "Render Manager",        "Render Manager", true  },
    { RenderPanelId::Statistics,    "statistics",     "Render Statistics",     "Statistics",     false },
    { RenderPanelId::Console,       "console",        "Render Console",        "Console",        false },
    { RenderPanelId::AovDebug,      "aov_debug",      "Render AOV / Debug",    "AOV/Debug",      false },
    { RenderPanelId::AssetBrowser,  "asset_browser",  "Render Asset Browser",  "Asset Browser",  false }
}};

} // namespace

namespace RenderPanelRegistry {

const std::array<RenderPanelDefinition, static_cast<std::size_t>(RenderPanelId::Count)>& GetDefinitions() {
    return kDefinitions;
}

const RenderPanelDefinition& GetDefinition(RenderPanelId id) {
    return kDefinitions[static_cast<std::size_t>(id)];
}

std::size_t ToIndex(RenderPanelId id) {
    return static_cast<std::size_t>(id);
}

} // namespace RenderPanelRegistry
