#pragma once

#include "NodeMath/NodeDefinition.h"
#include "NodeMath/RegionPlanning.h"
#include "NodeMath/SpecializedPlanning.h"
#include "Renderer/MaskRenderTypes.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

enum class ViewportTilingMode {
    Off,
    Auto,
    Always
};

struct ViewportTilingSettings {
    ViewportTilingMode mode = ViewportTilingMode::Off;
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

struct RenderGraphRegionStage {
    int nodeId = -1;
    std::string definitionId;
    Stack::NodeMath::CapabilityClass capability =
        Stack::NodeMath::CapabilityClass::SpecializedExternal;
    Stack::NodeMath::NeighborhoodSupport support;
    Stack::NodeMath::BorderPolicy border = Stack::NodeMath::BorderPolicy::Transparent;
    Stack::NodeMath::SpecializedStageKind specializedKind =
        Stack::NodeMath::SpecializedStageKind::None;
    Stack::NodeMath::RegionRequirement regionRequirement =
        Stack::NodeMath::RegionRequirement::MappedRoi;
    Stack::NodeMath::RenderScalePolicy scalePolicy =
        Stack::NodeMath::RenderScalePolicy::ExactRequested;
    Stack::NodeMath::CancellationPolicy cancellation =
        Stack::NodeMath::CancellationPolicy::BetweenTiles;
    Stack::NodeMath::SpatialDescriptor outputSpatial;
};

struct RenderGraphRegionPlan {
    bool valid = false;
    bool tileable = false;
    bool requiresFullFrame = false;
    int requiredHaloX = 0;
    int requiredHaloY = 0;
    Stack::NodeMath::RenderScale renderScale;
    Stack::NodeMath::SpatialDescriptor outputSpatial;
    std::vector<RenderGraphRegionStage> stages;
    std::string reason;
};

enum class TileIterationStatus {
    Completed,
    Canceled,
    Failed
};

struct TileIterationResult {
    TileIterationStatus status = TileIterationStatus::Completed;
    std::size_t completedTiles = 0;
};

namespace RenderTiling {

ViewportTilingMode ViewportTilingModeFromString(const std::string& value);
const char* ViewportTilingModeToString(ViewportTilingMode mode);
const char* ViewportTilingModeLabel(ViewportTilingMode mode);

ViewportTilingSettings NormalizeSettings(ViewportTilingSettings settings, int maxTextureSize = 0);
bool ShouldUseTiling(const ViewportTilingSettings& settings, int width, int height, int maxTextureSize = 0);
std::vector<RenderTileRect> PlanTiles(int width, int height, const ViewportTilingSettings& settings);
std::vector<RenderTileRect> PlanTiles(
    int width,
    int height,
    const ViewportTilingSettings& settings,
    int requiredHaloX,
    int requiredHaloY);
RenderGraphRegionPlan PlanGraphRegions(
    const RenderGraphSnapshot& graph,
    int fullWidth,
    int fullHeight,
    Stack::NodeMath::RenderScale renderScale = {});
bool IsGraphTileSafe(const RenderGraphSnapshot& graph, int fullWidth, int fullHeight, std::string* reason = nullptr);
TileIterationResult IterateTiles(
    const std::vector<RenderTileRect>& tiles,
    const std::function<bool()>& shouldCancel,
    const std::function<bool(const RenderTileRect&, std::size_t)>& renderTile);

} // namespace RenderTiling
