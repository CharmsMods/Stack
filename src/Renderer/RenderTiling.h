#pragma once

#include "Renderer/MaskRenderTypes.h"
#include <string>
#include <vector>

enum class ViewportTilingMode {
    Off,
    Auto,
    Always
};

struct ViewportTilingSettings {
    ViewportTilingMode mode = ViewportTilingMode::Auto;
    int tileSize = 1024;
    int haloPixels = 0;
    int autoPixelThresholdMegapixels = 32;
    bool progressive = true;
    bool debugOverlay = false;
};

struct RenderTileRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int haloX = 0;
    int haloY = 0;
    int haloWidth = 0;
    int haloHeight = 0;
};

namespace RenderTiling {

ViewportTilingMode ViewportTilingModeFromString(const std::string& value);
const char* ViewportTilingModeToString(ViewportTilingMode mode);
const char* ViewportTilingModeLabel(ViewportTilingMode mode);

ViewportTilingSettings NormalizeSettings(ViewportTilingSettings settings, int maxTextureSize = 0);
bool ShouldUseTiling(const ViewportTilingSettings& settings, int width, int height, int maxTextureSize = 0);
std::vector<RenderTileRect> PlanTiles(int width, int height, const ViewportTilingSettings& settings);
bool IsGraphTileSafe(const RenderGraphSnapshot& graph, int fullWidth, int fullHeight, std::string* reason = nullptr);

} // namespace RenderTiling
