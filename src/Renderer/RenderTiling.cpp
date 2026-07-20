#include "Renderer/RenderTiling.h"
#include "NodeMath/GeometryMath.h"
#include "Editor/NodeGraph/NodeGraphTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <set>
#include <unordered_set>
#include <unordered_map>

namespace {

bool IsBasicTileSafeLayerType(const std::string& type) {
    return type == "Brightness" ||
           type == "Contrast" ||
           type == "Saturation" ||
           type == "Warmth";
}

bool IsNeighborhoodLayerType(const std::string& type) {
    return type == "GaussianBlur" || type == "BoxBlur";
}

struct Locality {
    bool recognized = false;
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
    std::string reason;
};

Locality ClassifyNode(
    const RenderGraphNode& node,
    const Stack::NodeMath::RenderScale& scale,
    int fullWidth,
    int fullHeight) {
    using Stack::NodeMath::BorderPolicy;
    using Stack::NodeMath::CapabilityClass;
    Locality result;
    result.recognized = true;
    result.capability = CapabilityClass::Pointwise;
    switch (node.kind) {
        case RenderGraphNodeKind::Image:
            return result;
        case RenderGraphNodeKind::Lut:
        case RenderGraphNodeKind::Output:
        case RenderGraphNodeKind::MaskCombine:
        case RenderGraphNodeKind::MaskUtility:
        case RenderGraphNodeKind::ImageToMask:
        case RenderGraphNodeKind::Mix:
        case RenderGraphNodeKind::ChannelSplit:
        case RenderGraphNodeKind::ChannelCombine:
        case RenderGraphNodeKind::DataMath:
        case RenderGraphNodeKind::TechnicalImage:
            return result;
        case RenderGraphNodeKind::Reformat:
            result.recognized = false;
            result.capability = CapabilityClass::SampleResample;
            result.border = node.reformatSettings.border;
            result.regionRequirement = Stack::NodeMath::RegionRequirement::FullFrame;
            result.cancellation = Stack::NodeMath::CancellationPolicy::BetweenStages;
            result.reason = "Reformat uses inverse pixel-center sampling and currently materializes a full-frame extent boundary";
            return result;
        case RenderGraphNodeKind::FieldMean:
            result.recognized = false;
            result.capability = CapabilityClass::Reduction;
            result.reason = "Field Mean requires the complete scalar-field population";
            return result;
        case RenderGraphNodeKind::RawSource:
        case RenderGraphNodeKind::RawDecode:
        case RenderGraphNodeKind::RawDevelopment:
        case RenderGraphNodeKind::RawDevelop: {
            const auto kind = node.kind == RenderGraphNodeKind::RawSource ||
                    node.kind == RenderGraphNodeKind::RawDecode
                ? Stack::NodeMath::SpecializedStageKind::RawDecode
                : Stack::NodeMath::SpecializedStageKind::RawDevelopment;
            const auto specialized = Stack::NodeMath::PlanSpecializedStage(kind);
            result.recognized = false;
            result.capability = specialized.capability;
            result.specializedKind = kind;
            result.regionRequirement = specialized.regionRequirement;
            result.scalePolicy = specialized.scalePolicy;
            result.cancellation = specialized.cancellation;
            result.reason = specialized.reason;
            return result;
        }
        case RenderGraphNodeKind::RawDetailAutoMask:
        case RenderGraphNodeKind::RawDetailFusion: {
            const auto kind = Stack::NodeMath::SpecializedStageKind::RawDevelopment;
            const auto specialized = Stack::NodeMath::PlanSpecializedStage(kind);
            result.recognized = false;
            result.capability = specialized.capability;
            result.specializedKind = kind;
            result.regionRequirement = specialized.regionRequirement;
            result.scalePolicy = specialized.scalePolicy;
            result.cancellation = specialized.cancellation;
            result.reason = "RAW detail processing remains an opaque RAW-development stage.";
            return result;
        }
        case RenderGraphNodeKind::RawNeuralDenoise: {
            const auto kind = Stack::NodeMath::SpecializedStageKind::NeuralOrExternal;
            const auto specialized = Stack::NodeMath::PlanSpecializedStage(kind);
            result.recognized = false;
            result.capability = specialized.capability;
            result.specializedKind = kind;
            result.regionRequirement = specialized.regionRequirement;
            result.scalePolicy = specialized.scalePolicy;
            result.cancellation = specialized.cancellation;
            result.reason = specialized.reason;
            return result;
        }
        case RenderGraphNodeKind::HdrMerge:
        case RenderGraphNodeKind::Mfsr: {
            const auto kind = Stack::NodeMath::SpecializedStageKind::MultiFrameMerge;
            const auto specialized = Stack::NodeMath::PlanSpecializedStage(kind);
            result.recognized = false;
            result.capability = specialized.capability;
            result.specializedKind = kind;
            result.regionRequirement = specialized.regionRequirement;
            result.scalePolicy = specialized.scalePolicy;
            result.cancellation = specialized.cancellation;
            result.reason = specialized.reason;
            return result;
        }
        case RenderGraphNodeKind::FrequencyFft: {
            const auto kind = Stack::NodeMath::SpecializedStageKind::FrequencyTransform;
            const auto specialized = Stack::NodeMath::PlanSpecializedStage(kind);
            result.recognized = false;
            result.capability = specialized.capability;
            result.specializedKind = kind;
            result.regionRequirement = specialized.regionRequirement;
            result.scalePolicy = specialized.scalePolicy;
            result.cancellation = specialized.cancellation;
            result.reason = specialized.reason;
            return result;
        }
        case RenderGraphNodeKind::FrequencyIfft: {
            const auto kind = Stack::NodeMath::SpecializedStageKind::FrequencyInverseTransform;
            const auto specialized = Stack::NodeMath::PlanSpecializedStage(kind);
            result.recognized = false;
            result.capability = specialized.capability;
            result.specializedKind = kind;
            result.regionRequirement = specialized.regionRequirement;
            result.scalePolicy = specialized.scalePolicy;
            result.cancellation = specialized.cancellation;
            result.reason = specialized.reason;
            return result;
        }
        case RenderGraphNodeKind::SpectrumView:
        case RenderGraphNodeKind::FrequencyMask:
        case RenderGraphNodeKind::SpectrumMath:
        case RenderGraphNodeKind::MagnitudePhase:
        case RenderGraphNodeKind::SpectrumAnalyzer: {
            const auto kind = Stack::NodeMath::SpecializedStageKind::FrequencyOperation;
            const auto specialized = Stack::NodeMath::PlanSpecializedStage(kind);
            result.recognized = false;
            result.capability = specialized.capability;
            result.specializedKind = kind;
            result.regionRequirement = specialized.regionRequirement;
            result.scalePolicy = specialized.scalePolicy;
            result.cancellation = specialized.cancellation;
            result.reason = specialized.reason;
            return result;
        }
        case RenderGraphNodeKind::Layer: {
            const std::string type = node.layerJson.value("type", std::string());
            if (IsBasicTileSafeLayerType(type)) return result;
            if (!IsNeighborhoodLayerType(type)) {
                result.recognized = false;
                result.reason = "layer '" + type + "' has no trusted region mapping";
                return result;
            }
            const double amount = node.layerJson.value("amount", 2.0);
            result.capability = CapabilityClass::Neighborhood;
            result.support = Stack::NodeMath::GaussianBlurSupport(amount, scale);
            result.border = BorderPolicy::Clamp;
            constexpr std::int64_t kMaximumLiveTileSupport = 256;
            if (result.support.left < 0 || result.support.right < 0 ||
                result.support.bottom < 0 || result.support.top < 0 ||
                result.support.left > kMaximumLiveTileSupport ||
                result.support.bottom > kMaximumLiveTileSupport) {
                result.recognized = false;
                result.reason = "layer '" + type + "' has invalid or excessive neighborhood support";
            }
            return result;
        }
        default:
            result.recognized = false;
            result.reason = "reachable node kind requires full-frame or specialized execution";
            return result;
    }
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
    return PlanTiles(width, height, rawSettings, 0, 0);
}

std::vector<RenderTileRect> PlanTiles(
    int width,
    int height,
    const ViewportTilingSettings& rawSettings,
    int requiredHaloX,
    int requiredHaloY) {
    std::vector<RenderTileRect> tiles;
    if (width <= 0 || height <= 0 || requiredHaloX < 0 || requiredHaloY < 0) {
        return tiles;
    }
    const ViewportTilingSettings settings = NormalizeSettings(rawSettings);
    const int haloX = std::max(settings.haloPixels, requiredHaloX);
    const int haloY = std::max(settings.haloPixels, requiredHaloY);
    const int contentTileWidth = settings.tileSize - haloX * 2;
    const int contentTileHeight = settings.tileSize - haloY * 2;
    if (contentTileWidth <= 0 || contentTileHeight <= 0) return tiles;
    for (int y = 0; y < height; y += contentTileHeight) {
        const int contentH = std::min(contentTileHeight, height - y);
        for (int x = 0; x < width; x += contentTileWidth) {
            const int contentW = std::min(contentTileWidth, width - x);
            RenderTileRect tile;
            tile.x = x;
            tile.y = y;
            tile.width = contentW;
            tile.height = contentH;
            tile.haloX = std::max(0, x - haloX);
            tile.haloY = std::max(0, y - haloY);
            const int haloMaxX = std::min(width, x + contentW + haloX);
            const int haloMaxY = std::min(height, y + contentH + haloY);
            tile.haloWidth = std::max(1, haloMaxX - tile.haloX);
            tile.haloHeight = std::max(1, haloMaxY - tile.haloY);
            tiles.push_back(tile);
        }
    }
    return tiles;
}

RenderGraphRegionPlan PlanGraphRegions(
    const RenderGraphSnapshot& graph,
    int fullWidth,
    int fullHeight,
    Stack::NodeMath::RenderScale renderScale) {
    RenderGraphRegionPlan plan;
    plan.renderScale = renderScale;
    const std::vector<Stack::NodeMath::ContractIssue> scaleIssues =
        Stack::NodeMath::ValidateRenderScale(renderScale);
    if (!scaleIssues.empty()) {
        plan.reason = scaleIssues.front().message;
        return plan;
    }
    if (fullWidth <= 0 || fullHeight <= 0) {
        plan.reason = "missing source dimensions";
        return plan;
    }
    if (graph.nodes.empty() || graph.outputNodeId <= 0) {
        plan.reason = "empty graph";
        return plan;
    }

    std::unordered_map<int, const RenderGraphNode*> nodes;
    nodes.reserve(graph.nodes.size());
    for (const RenderGraphNode& node : graph.nodes) {
        nodes[node.nodeId] = &node;
    }
    bool extentFailure = false;
    std::string extentFailureReason;
    std::unordered_map<int, Stack::NodeMath::SpatialDescriptor> spatialMemo;
    std::unordered_set<int> spatialVisiting;
    const auto fallbackSpatial = [&]() {
        Stack::NodeMath::SpatialDescriptor spatial;
        spatial.kind = Stack::NodeMath::SpatialExtentKind::Finite;
        spatial.fullWindow = { 0, 0, fullWidth, fullHeight };
        spatial.dataWindow = spatial.fullWindow;
        spatial.rasterOrigin = Stack::NodeMath::RasterOrigin::BottomLeft;
        spatial.pixelAspect = 1.0;
        return spatial;
    };
    std::function<Stack::NodeMath::SpatialDescriptor(int)> resolveSpatial =
        [&](int nodeId) -> Stack::NodeMath::SpatialDescriptor {
        if (const auto memo = spatialMemo.find(nodeId); memo != spatialMemo.end()) {
            return memo->second;
        }
        if (!spatialVisiting.insert(nodeId).second) {
            extentFailure = true;
            extentFailureReason = "reachable graph contains a cycle while propagating extents";
            return {};
        }
        const auto found = nodes.find(nodeId);
        if (found == nodes.end() || found->second == nullptr) {
            extentFailure = true;
            extentFailureReason = "extent propagation reached a missing node";
            spatialVisiting.erase(nodeId);
            return {};
        }
        const RenderGraphNode& node = *found->second;
        Stack::NodeMath::SpatialDescriptor result;
        if (node.kind == RenderGraphNodeKind::Image &&
            node.image.width > 0 && node.image.height > 0) {
            result = fallbackSpatial();
            result.fullWindow.width = node.image.width;
            result.fullWindow.height = node.image.height;
            result.dataWindow = result.fullWindow;
        } else {
            std::vector<Stack::NodeMath::SpatialDescriptor> inputs;
            for (const RenderGraphLink& link : graph.links) {
                if (link.toNodeId != nodeId ||
                    link.toSocketId == EditorNodeGraph::kExposureValueInputSocketId) {
                    continue;
                }
                const auto source = nodes.find(link.fromNodeId);
                if (source != nodes.end() && source->second &&
                    source->second->kind == RenderGraphNodeKind::FieldMean) {
                    continue;
                }
                const auto inputSpatial = resolveSpatial(link.fromNodeId);
                if (inputSpatial.kind == Stack::NodeMath::SpatialExtentKind::Finite) {
                    inputs.push_back(inputSpatial);
                }
            }
            result = inputs.empty() ? fallbackSpatial() : inputs.front();
            if (node.kind != RenderGraphNodeKind::Reformat) {
                for (std::size_t index = 1; index < inputs.size(); ++index) {
                    if (!(inputs[index] == inputs.front())) {
                        extentFailure = true;
                        extentFailureReason =
                            "node " + std::to_string(nodeId) +
                            " received mismatched image extents; add an explicit Reformat before combining them";
                        break;
                    }
                }
            }
            if (node.kind == RenderGraphNodeKind::Reformat) {
                const auto issues = Stack::NodeMath::ValidateReformatSettings(
                    node.reformatSettings);
                if (!issues.empty()) {
                    extentFailure = true;
                    extentFailureReason = issues.front().message;
                    result = {};
                } else {
                    result = Stack::NodeMath::ReformatOutputSpatial(
                        result, node.reformatSettings, renderScale);
                }
            }
        }
        spatialVisiting.erase(nodeId);
        spatialMemo[nodeId] = result;
        return result;
    };
    const Stack::NodeMath::SpatialDescriptor resolvedOutputSpatial =
        resolveSpatial(graph.outputNodeId);
    std::unordered_map<int, std::pair<int, int>> memo;
    std::unordered_set<int> visiting;
    std::unordered_set<int> staged;
    bool structuralFailure = false;
    bool fullFrameBoundary = false;
    std::string failureReason;
    std::function<std::pair<int, int>(int)> requiredHalo = [&](int nodeId) {
        const auto memoIt = memo.find(nodeId);
        if (memoIt != memo.end()) return memoIt->second;
        if (!visiting.insert(nodeId).second) {
            structuralFailure = true;
            failureReason = "reachable graph contains a cycle";
            return std::pair<int, int>{ 0, 0 };
        }
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || !nodeIt->second) {
            structuralFailure = true;
            failureReason = "missing reachable node";
            visiting.erase(nodeId);
            return std::pair<int, int>{ 0, 0 };
        }
        const RenderGraphNode& node = *nodeIt->second;
        const Locality locality = ClassifyNode(node, renderScale, fullWidth, fullHeight);
        if (staged.insert(nodeId).second) {
            RenderGraphRegionStage stage;
            stage.nodeId = node.nodeId;
            stage.definitionId = node.definitionId;
            stage.capability = locality.capability;
            stage.support = locality.support;
            stage.border = locality.border;
            stage.specializedKind = locality.specializedKind;
            stage.regionRequirement = locality.regionRequirement;
            stage.scalePolicy = locality.scalePolicy;
            stage.cancellation = locality.cancellation;
            stage.outputSpatial = resolveSpatial(nodeId);
            plan.stages.push_back(std::move(stage));
        }
        if (!locality.recognized) {
            fullFrameBoundary = true;
            if (failureReason.empty()) failureReason = locality.reason;
            for (const RenderGraphLink& link : graph.links) {
                if (link.toNodeId == nodeId) {
                    (void)requiredHalo(link.fromNodeId);
                }
            }
            visiting.erase(nodeId);
            return std::pair<int, int>{ 0, 0 };
        }
        int upstreamX = 0;
        int upstreamY = 0;
        for (const RenderGraphLink& link : graph.links) {
            if (link.toNodeId != nodeId) continue;
            const auto inputHalo = requiredHalo(link.fromNodeId);
            upstreamX = std::max(upstreamX, inputHalo.first);
            upstreamY = std::max(upstreamY, inputHalo.second);
        }
        visiting.erase(nodeId);
        const int localX = static_cast<int>(std::max(locality.support.left, locality.support.right));
        const int localY = static_cast<int>(std::max(locality.support.bottom, locality.support.top));
        const std::pair<int, int> result {
            upstreamX > std::numeric_limits<int>::max() - localX
                ? std::numeric_limits<int>::max() : upstreamX + localX,
            upstreamY > std::numeric_limits<int>::max() - localY
                ? std::numeric_limits<int>::max() : upstreamY + localY
        };
        memo[nodeId] = result;
        return result;
    };

    const auto halo = requiredHalo(graph.outputNodeId);
    plan.valid = !structuralFailure && !extentFailure &&
        resolvedOutputSpatial.kind == Stack::NodeMath::SpatialExtentKind::Finite;
    plan.tileable = plan.valid && !fullFrameBoundary;
    plan.requiresFullFrame = plan.valid && fullFrameBoundary;
    plan.requiredHaloX = plan.tileable ? halo.first : 0;
    plan.requiredHaloY = plan.tileable ? halo.second : 0;
    if (extentFailure && !extentFailureReason.empty()) failureReason = extentFailureReason;
    plan.reason = plan.tileable
        ? "region mappings resolved"
        : (failureReason.empty() ? "region plan is invalid" : failureReason);
    plan.outputSpatial = resolvedOutputSpatial;
    return plan;
}

bool IsGraphTileSafe(const RenderGraphSnapshot& graph, int fullWidth, int fullHeight, std::string* reason) {
    const RenderGraphRegionPlan plan = PlanGraphRegions(graph, fullWidth, fullHeight);
    if (reason) *reason = plan.reason;
    return plan.valid && plan.tileable;
}

TileIterationResult IterateTiles(
    const std::vector<RenderTileRect>& tiles,
    const std::function<bool()>& shouldCancel,
    const std::function<bool(const RenderTileRect&, std::size_t)>& renderTile) {
    TileIterationResult result;
    if (!renderTile) {
        result.status = TileIterationStatus::Failed;
        return result;
    }
    for (std::size_t index = 0; index < tiles.size(); ++index) {
        if (shouldCancel && shouldCancel()) {
            result.status = TileIterationStatus::Canceled;
            return result;
        }
        if (!renderTile(tiles[index], index)) {
            result.status = TileIterationStatus::Failed;
            return result;
        }
        ++result.completedTiles;
    }
    result.status = TileIterationStatus::Completed;
    return result;
}

} // namespace RenderTiling
