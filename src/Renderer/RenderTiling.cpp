#include "Renderer/RenderTiling.h"
#include "NodeMath/GeometryMath.h"
#include "Editor/NodeGraph/NodeGraphTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
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

using RenderNodeIndex =
    std::unordered_map<int, const RenderGraphNode*>;
using IncomingLinkIndex =
    std::unordered_map<int, std::vector<const RenderGraphLink*>>;

using OutputEndpoint = std::pair<int, std::string>;

struct ReachableGraphOrder {
    bool valid = false;
    std::vector<OutputEndpoint> preorder;
    std::vector<OutputEndpoint> postorder;
    std::string reason;
};

template <typename IncludeLink>
ReachableGraphOrder BuildReachableGraphOrder(
    const OutputEndpoint& root,
    const RenderNodeIndex& nodes,
    const IncomingLinkIndex& incomingLinks,
    IncludeLink&& includeLink,
    const char* cycleReason,
    const char* missingNodeReason) {
    struct Frame {
        OutputEndpoint output;
        std::size_t nextInput = 0;
    };

    ReachableGraphOrder result;
    result.preorder.reserve(nodes.size());
    result.postorder.reserve(nodes.size());
    std::map<OutputEndpoint, std::uint8_t> states;
    std::vector<Frame> pending;
    pending.reserve(nodes.size());

    if (nodes.find(root.first) == nodes.end()) {
        result.reason = missingNodeReason;
        return result;
    }
    states.emplace(root, 1u);
    result.preorder.push_back(root);
    pending.push_back({ root, 0 });

    while (!pending.empty()) {
        Frame& frame = pending.back();
        const auto incoming = incomingLinks.find(frame.output.first);
        const std::vector<const RenderGraphLink*>* inputs =
            incoming == incomingLinks.end() ? nullptr : &incoming->second;

        bool descended = false;
        while (inputs && frame.nextInput < inputs->size()) {
            const RenderGraphLink& link = *(*inputs)[frame.nextInput++];
            if (!includeLink(link) || !Stack::GraphModel::OutputDependsOnInput(
                    nodes.at(frame.output.first)->outputDependencies, frame.output.second, link.toSocketId)) {
                continue;
            }
            if (nodes.find(link.fromNodeId) == nodes.end()) {
                result.reason = missingNodeReason;
                return result;
            }
            const OutputEndpoint upstream{link.fromNodeId, link.fromSocketId};
            const auto state = states.find(upstream);
            if (state != states.end()) {
                if (state->second == 1u) {
                    result.reason = cycleReason;
                    return result;
                }
                continue;
            }
            states.emplace(upstream, 1u);
            result.preorder.push_back(upstream);
            pending.push_back({ upstream, 0 });
            descended = true;
            break;
        }
        if (descended) {
            continue;
        }

        states[frame.output] = 2u;
        result.postorder.push_back(frame.output);
        pending.pop_back();
    }

    result.valid = true;
    return result;
}

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
        case RenderGraphNodeKind::ConstantChannel:
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
        case RenderGraphNodeKind::RawProjectSourceSet:
            result.recognized = false;
            result.capability = CapabilityClass::SpecializedExternal;
            result.specializedKind =
                Stack::NodeMath::SpecializedStageKind::RawDevelopment;
            result.regionRequirement = Stack::NodeMath::RegionRequirement::FullFrame;
            result.cancellation = Stack::NodeMath::CancellationPolicy::BetweenStages;
            result.reason = node.rawProjectSourceSet.resultAvailable
                ? "The adopted multi-frame Bayer result is developed through the ordinary full-frame RAW pipeline."
                : "The multi-frame RAW result is not available yet.";
            return result;
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
        case RenderGraphNodeKind::FrequencyFilter:
        case RenderGraphNodeKind::FrequencyResponse:
        case RenderGraphNodeKind::ApplyFrequencyResponse:
        case RenderGraphNodeKind::CombineSpectra:
        case RenderGraphNodeKind::SpectrumSeparate:
        case RenderGraphNodeKind::SpectrumRecombine:
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
    if (graph.nodes.empty()) {
        plan.reason = "empty graph";
        return plan;
    }

    RenderNodeIndex nodes;
    nodes.reserve(graph.nodes.size());
    for (const RenderGraphNode& node : graph.nodes) {
        nodes[node.nodeId] = &node;
    }
    if (nodes.find(graph.outputNodeId) == nodes.end()) {
        plan.reason = "missing output node";
        return plan;
    }

    IncomingLinkIndex incomingLinks;
    incomingLinks.reserve(nodes.size());
    for (const RenderGraphLink& link : graph.links) {
        incomingLinks[link.toNodeId].push_back(&link);
    }

    bool extentFailure = false;
    std::string extentFailureReason;
    std::map<OutputEndpoint, Stack::NodeMath::SpatialDescriptor> spatialMemo;
    const OutputEndpoint root{graph.outputNodeId, graph.outputSocketId.empty() ? "imageOut" : graph.outputSocketId};
    const auto fallbackSpatial = [&]() {
        Stack::NodeMath::SpatialDescriptor spatial;
        spatial.kind = Stack::NodeMath::SpatialExtentKind::Finite;
        spatial.fullWindow = { 0, 0, fullWidth, fullHeight };
        spatial.dataWindow = spatial.fullWindow;
        spatial.rasterOrigin = Stack::NodeMath::RasterOrigin::BottomLeft;
        spatial.pixelAspect = 1.0;
        return spatial;
    };

    const auto isSpatialDependency = [&](const RenderGraphLink& link) {
        const auto destination = nodes.find(link.toNodeId);
        if (destination != nodes.end() && destination->second &&
            destination->second->kind == RenderGraphNodeKind::Image &&
            destination->second->image.width > 0 &&
            destination->second->image.height > 0) {
            return false;
        }
        if (link.toSocketId == EditorNodeGraph::kExposureValueInputSocketId) {
            return false;
        }
        const auto source = nodes.find(link.fromNodeId);
        return source == nodes.end() || !source->second ||
            source->second->kind != RenderGraphNodeKind::FieldMean;
    };
    const ReachableGraphOrder spatialOrder = BuildReachableGraphOrder(
        root,
        nodes,
        incomingLinks,
        isSpatialDependency,
        "reachable graph contains a cycle while propagating extents",
        "extent propagation reached a missing node");
    if (!spatialOrder.valid) {
        extentFailure = true;
        extentFailureReason = spatialOrder.reason;
    }

    for (const auto& endpoint : spatialOrder.postorder) {
        const int nodeId = endpoint.first;
        const RenderGraphNode& node = *nodes.at(nodeId);
        Stack::NodeMath::SpatialDescriptor result;
        if (node.kind == RenderGraphNodeKind::Image &&
            node.image.width > 0 && node.image.height > 0) {
            result = fallbackSpatial();
            result.fullWindow.width = node.image.width;
            result.fullWindow.height = node.image.height;
            result.dataWindow = result.fullWindow;
        } else {
            bool hasFiniteInput = false;
            Stack::NodeMath::SpatialDescriptor firstInput;
            const auto incoming = incomingLinks.find(nodeId);
            if (incoming != incomingLinks.end()) {
                for (const RenderGraphLink* link : incoming->second) {
                    if (!link || !isSpatialDependency(*link) ||
                        !Stack::GraphModel::OutputDependsOnInput(node.outputDependencies, endpoint.second, link->toSocketId)) {
                        continue;
                    }
                    const auto inputSpatial = spatialMemo.find({link->fromNodeId, link->fromSocketId});
                    if (inputSpatial != spatialMemo.end() &&
                        inputSpatial->second.kind ==
                            Stack::NodeMath::SpatialExtentKind::Finite) {
                        if (!hasFiniteInput) {
                            firstInput = inputSpatial->second;
                            hasFiniteInput = true;
                        } else if (node.kind != RenderGraphNodeKind::Reformat &&
                                   !(inputSpatial->second == firstInput)) {
                            extentFailure = true;
                            extentFailureReason =
                                "node " + std::to_string(nodeId) +
                                " received mismatched image extents; add an explicit Reformat before combining them";
                        }
                    }
                }
            }
            result = hasFiniteInput ? firstInput : fallbackSpatial();
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
        spatialMemo.emplace(endpoint, std::move(result));
    }
    const auto outputSpatial = spatialMemo.find(root);
    const Stack::NodeMath::SpatialDescriptor resolvedOutputSpatial =
        outputSpatial == spatialMemo.end()
            ? Stack::NodeMath::SpatialDescriptor{}
            : outputSpatial->second;

    bool structuralFailure = false;
    bool fullFrameBoundary = false;
    std::string failureReason;
    const ReachableGraphOrder haloOrder = BuildReachableGraphOrder(
        root,
        nodes,
        incomingLinks,
        [](const RenderGraphLink&) { return true; },
        "reachable graph contains a cycle",
        "missing reachable node");
    if (!haloOrder.valid) {
        structuralFailure = true;
        failureReason = haloOrder.reason;
    }

    std::unordered_map<int, Locality> localities;
    localities.reserve(haloOrder.preorder.size());
    plan.stages.reserve(haloOrder.preorder.size());
    for (const auto& endpoint : haloOrder.preorder) {
        const int nodeId = endpoint.first;
        if (localities.count(nodeId)) continue;
        const RenderGraphNode& node = *nodes.at(nodeId);
        Locality locality =
            ClassifyNode(node, renderScale, fullWidth, fullHeight);
        if (!locality.recognized) {
            fullFrameBoundary = true;
            if (failureReason.empty()) {
                failureReason = locality.reason;
            }
        }
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
        const auto spatial = spatialMemo.find(endpoint);
        if (spatial != spatialMemo.end()) {
            stage.outputSpatial = spatial->second;
        }
        plan.stages.push_back(std::move(stage));
        localities.emplace(nodeId, std::move(locality));
    }

    std::map<OutputEndpoint, std::pair<int, int>> haloMemo;
    for (const auto& endpoint : haloOrder.postorder) {
        const int nodeId = endpoint.first;
        const Locality& locality = localities.at(nodeId);
        if (!locality.recognized) {
            haloMemo.emplace(endpoint, std::pair<int, int>{ 0, 0 });
            continue;
        }
        int upstreamX = 0;
        int upstreamY = 0;
        const auto incoming = incomingLinks.find(nodeId);
        if (incoming != incomingLinks.end()) {
            for (const RenderGraphLink* link : incoming->second) {
                if (!link || !Stack::GraphModel::OutputDependsOnInput(
                        nodes.at(nodeId)->outputDependencies, endpoint.second, link->toSocketId)) {
                    continue;
                }
                const auto inputHalo = haloMemo.find({link->fromNodeId, link->fromSocketId});
                if (inputHalo == haloMemo.end()) {
                    structuralFailure = true;
                    if (failureReason.empty()) {
                        failureReason = "missing reachable node";
                    }
                    continue;
                }
                upstreamX = std::max(upstreamX, inputHalo->second.first);
                upstreamY = std::max(upstreamY, inputHalo->second.second);
            }
        }
        const int localX = static_cast<int>(
            std::max(locality.support.left, locality.support.right));
        const int localY = static_cast<int>(
            std::max(locality.support.bottom, locality.support.top));
        haloMemo.emplace(endpoint, std::pair<int, int>{
            upstreamX > std::numeric_limits<int>::max() - localX
                ? std::numeric_limits<int>::max() : upstreamX + localX,
            upstreamY > std::numeric_limits<int>::max() - localY
                ? std::numeric_limits<int>::max() : upstreamY + localY
        });
    }
    const auto outputHalo = haloMemo.find(root);
    const std::pair<int, int> halo =
        outputHalo == haloMemo.end()
            ? std::pair<int, int>{ 0, 0 }
            : outputHalo->second;
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
