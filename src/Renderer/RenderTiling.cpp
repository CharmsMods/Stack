#include "Renderer/RenderTiling.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <set>
#include <unordered_map>

namespace {

bool IsBasicTileSafeLayerType(const std::string& type) {
    return type == "Brightness" ||
           type == "Contrast" ||
           type == "Saturation" ||
           type == "Warmth";
}

} // namespace

namespace RenderTiling {

ViewportTilingMode ViewportTilingModeFromString(const std::string& value) {
    if (value == "Off") {
        return ViewportTilingMode::Off;
    }
    if (value == "Always") {
        return ViewportTilingMode::Always;
    }
    return ViewportTilingMode::Auto;
}

const char* ViewportTilingModeToString(ViewportTilingMode mode) {
    switch (mode) {
        case ViewportTilingMode::Off: return "Off";
        case ViewportTilingMode::Always: return "Always";
        case ViewportTilingMode::Auto:
        default:
            return "Auto";
    }
}

const char* ViewportTilingModeLabel(ViewportTilingMode mode) {
    switch (mode) {
        case ViewportTilingMode::Off: return "Off";
        case ViewportTilingMode::Always: return "Always";
        case ViewportTilingMode::Auto:
        default:
            return "Auto";
    }
}

ViewportTilingSettings NormalizeSettings(ViewportTilingSettings settings, int maxTextureSize) {
    const int textureLimit = maxTextureSize > 0 ? std::clamp(maxTextureSize, 512, 32768) : 16384;
    settings.tileSize = std::clamp(settings.tileSize, 256, std::min(4096, textureLimit));
    settings.haloPixels = std::clamp(settings.haloPixels, 0, std::min(256, settings.tileSize / 4));
    settings.autoPixelThresholdMegapixels = std::clamp(settings.autoPixelThresholdMegapixels, 1, 512);
    if (settings.mode == ViewportTilingMode::Off) {
        settings.progressive = false;
    }
    return settings;
}

bool ShouldUseTiling(const ViewportTilingSettings& rawSettings, int width, int height, int maxTextureSize) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    const ViewportTilingSettings settings = NormalizeSettings(rawSettings, maxTextureSize);
    if (settings.mode == ViewportTilingMode::Off) {
        return false;
    }
    if (settings.mode == ViewportTilingMode::Always) {
        return width > settings.tileSize || height > settings.tileSize;
    }
    const std::int64_t pixels = static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height);
    const std::int64_t thresholdPixels =
        static_cast<std::int64_t>(settings.autoPixelThresholdMegapixels) * 1000000LL;
    return pixels >= thresholdPixels || width > settings.tileSize || height > settings.tileSize;
}

std::vector<RenderTileRect> PlanTiles(int width, int height, const ViewportTilingSettings& rawSettings) {
    std::vector<RenderTileRect> tiles;
    if (width <= 0 || height <= 0) {
        return tiles;
    }
    const ViewportTilingSettings settings = NormalizeSettings(rawSettings);
    const int tileSize = std::max(1, settings.tileSize);
    const int halo = std::max(0, settings.haloPixels);
    for (int y = 0; y < height; y += tileSize) {
        const int contentH = std::min(tileSize, height - y);
        for (int x = 0; x < width; x += tileSize) {
            const int contentW = std::min(tileSize, width - x);
            RenderTileRect tile;
            tile.x = x;
            tile.y = y;
            tile.width = contentW;
            tile.height = contentH;
            tile.haloX = std::max(0, x - halo);
            tile.haloY = std::max(0, y - halo);
            const int haloMaxX = std::min(width, x + contentW + halo);
            const int haloMaxY = std::min(height, y + contentH + halo);
            tile.haloWidth = std::max(1, haloMaxX - tile.haloX);
            tile.haloHeight = std::max(1, haloMaxY - tile.haloY);
            tiles.push_back(tile);
        }
    }
    return tiles;
}

bool IsGraphTileSafe(const RenderGraphSnapshot& graph, int fullWidth, int fullHeight, std::string* reason) {
    if (fullWidth <= 0 || fullHeight <= 0) {
        if (reason) *reason = "missing source dimensions";
        return false;
    }
    if (graph.nodes.empty() || graph.outputNodeId <= 0) {
        if (reason) *reason = "empty graph";
        return false;
    }

    std::unordered_map<int, const RenderGraphNode*> nodes;
    nodes.reserve(graph.nodes.size());
    for (const RenderGraphNode& node : graph.nodes) {
        nodes[node.nodeId] = &node;
    }
    std::set<int> reachable;
    std::function<void(int)> visit = [&](int nodeId) {
        if (!reachable.insert(nodeId).second) {
            return;
        }
        for (const RenderGraphLink& link : graph.links) {
            if (link.toNodeId == nodeId) {
                visit(link.fromNodeId);
            }
        }
    };
    visit(graph.outputNodeId);

    for (int nodeId : reachable) {
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || !nodeIt->second) {
            if (reason) *reason = "missing reachable node";
            return false;
        }
        const RenderGraphNode& node = *nodeIt->second;
        switch (node.kind) {
            case RenderGraphNodeKind::Image:
                if (!node.image.pixels.empty() &&
                    (node.image.width != fullWidth || node.image.height != fullHeight)) {
                    if (reason) *reason = "image node dimensions do not match the main canvas";
                    return false;
                }
                break;
            case RenderGraphNodeKind::Lut:
            case RenderGraphNodeKind::Output:
            case RenderGraphNodeKind::MaskCombine:
            case RenderGraphNodeKind::MaskUtility:
            case RenderGraphNodeKind::ImageToMask:
            case RenderGraphNodeKind::Mix:
            case RenderGraphNodeKind::ChannelSplit:
            case RenderGraphNodeKind::ChannelCombine:
            case RenderGraphNodeKind::DataMath:
                break;
            case RenderGraphNodeKind::Layer: {
                const std::string type = node.layerJson.value("type", std::string());
                if (!IsBasicTileSafeLayerType(type)) {
                    if (reason) *reason = "layer '" + type + "' is not tile-safe yet";
                    return false;
                }
                break;
            }
            default:
                if (reason) *reason = "node kind is not tile-safe yet";
                return false;
        }
    }

    return true;
}

} // namespace RenderTiling
