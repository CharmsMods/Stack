#include "Renderer/RenderTiling.h"
#include "Renderer/GraphViewportPlanner.h"
#include "Renderer/RawPreviewProxy.h"
#include "Raw/RawGraphOperation.h"
#include "Editor/NodeGraph/NodeGraphTypes.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>

namespace {
using Endpoint = RenderGraphViewportEndpoint;
using Stage = RenderGraphViewportStage;
using Stack::Renderer::ImageRegion;
using namespace Stack::NodeMath;

bool PointwiseTone(const nlohmann::json& tone) {
    return tone.is_object() && !tone.value("localBaselineEnabled", false) &&
        !tone.value("autoCalibratePending", false) && tone.value("targetScope", 0) == 0 &&
        !tone.value("foundationAdaptiveAssist", false);
}

bool CanExecuteRegion(const RenderGraphNode& node, const std::string& socket) {
    using K = RenderGraphNodeKind;
    switch (node.kind) {
        case K::TechnicalImage:
        case K::DataMath:
        case K::Lut:
        case K::Mix:
        case K::ChannelSplit:
        case K::ChannelCombine:
        case K::ImageToMask:
        case K::MaskCombine:
        case K::MaskUtility:
        case K::Output:
            return true;
        case K::RawOperation:
            if (socket == "inputImageOut" || !node.rawOperation.enabled) return true;
            switch (node.rawOperation.kind) {
                case Stack::RawRecipe::GraphOperationKind::Exposure:
                case Stack::RawRecipe::GraphOperationKind::Calibration:
                    return true;
                case Stack::RawRecipe::GraphOperationKind::ToneCurve:
                    return node.rawOperation.parameters.value("curveMode",std::string("components"))!="advanced" ||
                        PointwiseTone(node.rawOperation.parameters.at("advanced"));
                case Stack::RawRecipe::GraphOperationKind::LocalEv: {
                    const auto recipe = Stack::RawRecipe::ReadGraphOperation(node.rawOperation);
                    return !Stack::RawRecipe::IsLocalRangeEnabled(recipe) && recipe.localRange.areas.empty();
                }
                case Stack::RawRecipe::GraphOperationKind::ColorWarp:
                    return !Stack::RawRecipe::IsColorWarpEnabled(Stack::RawRecipe::ReadGraphOperation(node.rawOperation).colorWarp);
                case Stack::RawRecipe::GraphOperationKind::DetailContrast:
                    return !Stack::RawRecipe::IsDetailContrastActive(Stack::RawRecipe::ReadGraphOperation(node.rawOperation).detailContrast);
                default: return false;
            }
        case K::Layer: {
            if (!node.layerJson.value("enabled", true)) return true;
            const auto type = node.layerJson.value("type", std::string{});
            // The screen phase uses full-input UVs and logical source pixels.
            // Consumers may crop its complete output, never restart the screen
            // from UV zero inside a regional input.
            if (type=="Halftoning") return false;
            if (type == "RawOutputCrop") {
                const auto recipe = Stack::RawRecipe::DeserializeRecipe(node.layerJson.at("recipe"));
                return !recipe.cropRotation.cropEnabled;
            }
            if (type == "Brightness" || type == "Contrast" || type == "Saturation" || type == "Warmth") {
                // These legacy layers share an optional sharpening pass even
                // when their authored type names only a color adjustment.
                const float sharpening = node.layerJson.value("sharpening", 0.f);
                return std::isfinite(sharpening) && sharpening <= 0.001f;
            }
            return type == "ViewTransform" || (type == "ToneCurve" && PointwiseTone(node.layerJson));
        }
        // These stages retain complete coordinates, neighborhoods or global
        // populations. Consumers may request a crop of their immutable output.
        default: return false;
    }
}

ImageRegion UnionDemand(const ImageRegion& a, const ImageRegion& b) {
    if (!a.Valid()) return b;
    if (!b.Valid()) return a;
    const auto scaled = Stack::Renderer::ScaleImageRegion(b, a.fullWidth, a.fullHeight);
    const int left = std::min(a.x, scaled.x), top = std::min(a.y, scaled.y);
    const int right = std::max(a.x + a.width, scaled.x + scaled.width);
    const int bottom = std::max(a.y + a.height, scaled.y + scaled.height);
    return {a.fullWidth, a.fullHeight, left, top, right - left, bottom - top};
}

SpatialDescriptor Spatial(int width, int height) {
    SpatialDescriptor result;
    if (width <= 0 || height <= 0) return result;
    result.kind = SpatialExtentKind::Finite;
    result.nativeWidth = width; result.nativeHeight = height;
    result.fullWindow = result.dataWindow = {0, 0, width, height};
    return result;
}

bool UsableSpatial(const SpatialDescriptor& spatial) {
    return spatial.kind == SpatialExtentKind::Finite && spatial.fullWindow.width > 0 &&
        spatial.fullWindow.height > 0 && spatial.fullWindow.width <= std::numeric_limits<int>::max() &&
        spatial.fullWindow.height <= std::numeric_limits<int>::max();
}
std::uint64_t SaturatingAdd(std::uint64_t a, std::uint64_t b) {
    return a > std::numeric_limits<std::uint64_t>::max() - b ? std::numeric_limits<std::uint64_t>::max() : a + b;
}
}

RenderGraphViewportPlan RenderTiling::GraphViewportPlanner::Plan(
    int fullWidth, int fullHeight, int previewMaxDimension,
    const Stack::Renderer::ViewportPreviewRequest& request,
    const std::vector<RenderGraphViewportEndpoint>& completeConsumers) {
    RenderGraphViewportPlan plan;
    const auto& graph = m_Graph;
    plan.enabled = request.enabled;
    plan.output = {graph.outputNodeId, graph.outputSocketId};
    if (!request.enabled || request.empty) return plan;
    auto completeRoots = completeConsumers;
    if (graph.rawLayerScopeNodeId > 0)
        completeRoots.push_back({graph.rawLayerScopeNodeId,graph.rawLayerScopeSocketId});
    if (graph.rawWorkspaceLocalRangeTargetSampleRequested ||
        (!graph.rawWorkspaceLocalRangeOverlayMode.empty() && graph.rawWorkspaceLocalRangeOverlayMode != "none"))
        for (const auto& node : graph.nodes) if (node.kind == RenderGraphNodeKind::RawDevelopment)
            completeRoots.push_back({node.nodeId,"imageOut"});
    std::map<int, const RenderGraphNode*> nodes;
    std::map<int, std::vector<const RenderGraphLink*>> inputs;
    for (const auto& node : graph.nodes) nodes.emplace(node.nodeId, &node);
    for (const auto& link : graph.links) inputs[link.toNodeId].push_back(&link);
    std::map<Endpoint, int> states;
    std::vector<Endpoint> order;
    struct Frame { Endpoint endpoint; std::size_t input = 0; };
    const auto visit = [&](const Endpoint& root) {
        std::vector<Frame> pending{{root, 0}};
        if (states[root] == 2) return true;
        states[root] = 1;
        while (!pending.empty()) {
            auto& frame = pending.back();
            const auto found = nodes.find(frame.endpoint.nodeId);
            if (found == nodes.end()) { plan.reason = "Visible-region plan reached a missing node."; return false; }
            const auto& incoming = inputs[frame.endpoint.nodeId];
            bool descended = false;
            while (frame.input < incoming.size()) {
                const auto& link = *incoming[frame.input++];
                if (!Stack::GraphModel::OutputDependsOnInput(found->second->outputDependencies,
                        frame.endpoint.socketId, link.toSocketId)) continue;
                const Endpoint source{link.fromNodeId, link.fromSocketId};
                if (states[source] == 1) { plan.reason = "Visible-region plan reached a cycle."; return false; }
                if (states[source] == 2) continue;
                states[source] = 1; pending.push_back({source, 0}); descended = true; break;
            }
            if (descended) continue;
            states[frame.endpoint] = 2; order.push_back(frame.endpoint); pending.pop_back();
        }
        return true;
    };
    if (!visit(plan.output)) return plan;
    for (const auto& consumer : completeRoots) if (!visit(consumer)) return plan;

    for (const auto& endpoint : order) {
        const auto& node = *nodes.at(endpoint.nodeId);
        Stage stage;
        const auto capability = m_RegionalCapabilities.find(endpoint);
        stage.regionalExecutable = capability != m_RegionalCapabilities.end() ? capability->second :
            m_RegionalCapabilities.emplace(endpoint, CanExecuteRegion(node, endpoint.socketId)).first->second;
        if (node.kind == RenderGraphNodeKind::DataMath && inputs[endpoint.nodeId].empty())
            stage.regionalExecutable = false;
        if (!stage.regionalExecutable) stage.completeReason = "Stage retains its complete input and output contract.";
        if (node.semanticDescriptor.spatial.state == KnowledgeState::Known &&
            UsableSpatial(node.semanticDescriptor.spatial.value)) stage.spatial = node.semanticDescriptor.spatial.value;
        if (!UsableSpatial(stage.spatial)) {
            for (const auto* link : inputs[endpoint.nodeId]) {
                if (!Stack::GraphModel::OutputDependsOnInput(node.outputDependencies, endpoint.socketId, link->toSocketId)) continue;
                const auto upstream = plan.stages.find({link->fromNodeId, link->fromSocketId});
                if (upstream != plan.stages.end() && UsableSpatial(upstream->second.spatial) &&
                    (link->toSocketId == "imageIn" || !UsableSpatial(stage.spatial))) stage.spatial = upstream->second.spatial;
            }
        }
        if (node.kind == RenderGraphNodeKind::Image && node.image.width > 0 && node.image.height > 0)
            stage.spatial = Spatial(node.image.width, node.image.height);
        if (node.kind == RenderGraphNodeKind::Reformat)
            stage.spatial = Stack::NodeMath::ReformatOutputSpatial(stage.spatial, node.reformatSettings);
        if ((node.kind == RenderGraphNodeKind::RawDevelopment || node.kind == RenderGraphNodeKind::RawProjectSourceSet) &&
            node.rawDevelopment.embeddedRawData) {
            int width = Raw::DisplayWidth(node.rawDevelopment.embeddedRawData->metadata);
            int height = Raw::DisplayHeight(node.rawDevelopment.embeddedRawData->metadata);
            const auto& crop = node.rawDevelopment.recipe.cropRotation;
            if (std::abs(crop.rotationDegrees) % 180 == 90) std::swap(width, height);
            if (crop.cropEnabled) {
                const int left = std::clamp(int(std::floor(crop.cropX * width)), 0, width - 1);
                const int top = std::clamp(int(std::floor(crop.cropY * height)), 0, height - 1);
                width = std::clamp(int(std::ceil((crop.cropX + crop.cropWidth) * width)), left + 1, width) - left;
                height = std::clamp(int(std::ceil((crop.cropY + crop.cropHeight) * height)), top + 1, height) - top;
            }
            stage.spatial = Spatial(width, height);
        }
        if (node.kind == RenderGraphNodeKind::Layer && node.layerJson.value("enabled", true) &&
            node.layerJson.value("type", std::string{}) == "RawOutputCrop" && UsableSpatial(stage.spatial)) {
            for (const auto* link : inputs[endpoint.nodeId]) if (link->toSocketId == "imageIn") {
                const auto source = plan.stages.find({link->fromNodeId, link->fromSocketId});
                if (source != plan.stages.end()) stage.spatial = source->second.spatial;
            }
            const auto& crop = Stack::RawRecipe::DeserializeRecipe(node.layerJson.at("recipe")).cropRotation;
            if (crop.cropEnabled) {
                const int width = int(stage.spatial.fullWindow.width), height = int(stage.spatial.fullWindow.height);
                const int left = std::clamp(int(std::floor(crop.cropX * width)), 0, width - 1);
                const int top = std::clamp(int(std::floor(crop.cropY * height)), 0, height - 1);
                stage.spatial = Spatial(
                    std::clamp(int(std::ceil((crop.cropX + crop.cropWidth) * width)), left + 1, width) - left,
                    std::clamp(int(std::ceil((crop.cropY + crop.cropHeight) * height)), top + 1, height) - top);
            }
        }
        if (!UsableSpatial(stage.spatial)) stage.spatial = Spatial(
            node.nativeWidth > 0 ? node.nativeWidth : fullWidth, node.nativeHeight > 0 ? node.nativeHeight : fullHeight);
        if (!UsableSpatial(stage.spatial)) { plan.reason = "Visible-region plan has no finite image geometry."; return plan; }
        if (stage.regionalExecutable) for (const auto* link : inputs[endpoint.nodeId]) {
            if (!Stack::GraphModel::OutputDependsOnInput(node.outputDependencies, endpoint.socketId, link->toSocketId)) continue;
            const auto kind = nodes.at(link->fromNodeId)->kind;
            if (kind == RenderGraphNodeKind::Value || kind == RenderGraphNodeKind::FieldMean ||
                (kind == RenderGraphNodeKind::DataMath && nodes.at(link->fromNodeId)->dataMathSettings.domain == Stack::NodeMath::ArithmeticDomain::Values)) continue;
            const auto& inputSpatial = plan.stages.at({link->fromNodeId,link->fromSocketId}).spatial;
            if (stage.spatial.fullWindow.width != inputSpatial.fullWindow.width ||
                stage.spatial.fullWindow.height != inputSpatial.fullWindow.height) {
                // Legacy normalized mask/field combinations can intentionally
                // differ in extent. Keep that existing complete-frame mapping
                // until this consumer has an explicit per-port spatial map.
                stage.regionalExecutable = false;
                stage.completeReason = "Consumed image windows require a complete normalized extent boundary.";
                break;
            }
        }
        const auto raster = Stack::Renderer::RawPreviewProxy::FitPreviewExtent(
            int(stage.spatial.fullWindow.width), int(stage.spatial.fullWindow.height), previewMaxDimension);
        stage.renderWidth = raster.first; stage.renderHeight = raster.second;
        plan.stages.emplace(endpoint, std::move(stage));
    }
    auto& output = plan.stages.at(plan.output);
    const int outputWidth = int(output.spatial.fullWindow.width), outputHeight = int(output.spatial.fullWindow.height);
    plan.logicalWidth = outputWidth; plan.logicalHeight = outputHeight;
    output.demand = request.visible.Valid()
        ? Stack::Renderer::ScaleImageRegion(request.visible, outputWidth, outputHeight)
        : Stack::Renderer::WholeImageRegion(outputWidth, outputHeight);
    for (const auto& consumer : completeRoots) {
        auto& stage = plan.stages.at(consumer);
        stage.demand = Stack::Renderer::WholeImageRegion(int(stage.spatial.fullWindow.width), int(stage.spatial.fullWindow.height));
    }
    std::uint64_t largestRasterPixels = 0;
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        auto& stage = plan.stages.at(*it);
        if (!stage.demand.Valid()) continue;
        const auto& node = *nodes.at(it->nodeId);
        if (!stage.regionalExecutable) stage.demand = Stack::Renderer::WholeImageRegion(
            int(stage.spatial.fullWindow.width), int(stage.spatial.fullWindow.height));
        for (const auto* link : inputs[it->nodeId]) {
            if (!Stack::GraphModel::OutputDependsOnInput(node.outputDependencies, it->socketId, link->toSocketId)) continue;
            const Endpoint source{link->fromNodeId, link->fromSocketId};
            auto& upstream = plan.stages.at(source);
            ImageRegion inputDemand = Stack::Renderer::WholeImageRegion(int(upstream.spatial.fullWindow.width), int(upstream.spatial.fullWindow.height));
            if (stage.regionalExecutable && stage.spatial.fullWindow.width == upstream.spatial.fullWindow.width &&
                stage.spatial.fullWindow.height == upstream.spatial.fullWindow.height) {
                const Rect requested{upstream.spatial.fullWindow.x + stage.demand.x,
                    upstream.spatial.fullWindow.y + stage.demand.y, stage.demand.width, stage.demand.height};
                const auto mapping = MapPointwiseRegion(MakeFiniteRegion(requested), upstream.spatial);
                if (!mapping.executable) { plan.reason = "Pointwise input region could not be mapped."; return plan; }
                inputDemand = stage.demand;
            }
            stage.inputs.push_back({link->toSocketId, source, inputDemand});
            upstream.demand = UnionDemand(upstream.demand, inputDemand);
        }
        const auto resident = Stack::Renderer::ScaleImageRegion(stage.demand, stage.renderWidth, stage.renderHeight);
        const auto pixels = std::uint64_t(resident.width) * resident.height;
        const auto completePixels = std::uint64_t(stage.spatial.fullWindow.width) * stage.spatial.fullWindow.height;
        largestRasterPixels = std::max(largestRasterPixels, stage.regionalExecutable ? pixels : completePixels);
        if (stage.regionalExecutable) plan.regionalPixels = SaturatingAdd(plan.regionalPixels, pixels);
        else plan.completePixels = SaturatingAdd(plan.completePixels, completePixels);
    }
    plan.valid = true;
    plan.fullInputPixels = plan.completePixels;
    const bool raw = graph.rawLayerBackgroundNodeId > 0 ||
        std::any_of(graph.nodes.begin(), graph.nodes.end(), [](const auto& node) {
            return node.kind == RenderGraphNodeKind::RawSource || node.kind == RenderGraphNodeKind::RawDecode ||
                node.kind == RenderGraphNodeKind::RawDevelop || node.kind == RenderGraphNodeKind::RawDevelopment ||
                node.kind == RenderGraphNodeKind::RawProjectSourceSet || node.kind == RenderGraphNodeKind::RawOperation;
        });
    const std::uint64_t bytesPerPixel = raw ? 16u : 8u;
    // Cropped inputs are cached until execution ends, independently of their
    // endpoint outputs. Count each source/target crop once, as ViewportInput's
    // execution-local cache does, including a complete output's final crop.
    std::uint64_t inputCropPixels = 0;
    std::set<std::tuple<int,std::string,int,int,int,int,int,int>> crops;
    const auto countCrop = [&](const Endpoint& source, const ImageRegion& target) {
        const auto& stage = plan.stages.at(source);
        const auto resident = stage.regionalExecutable ?
            Stack::Renderer::ScaleImageRegion(stage.demand,stage.renderWidth,stage.renderHeight) :
            Stack::Renderer::WholeImageRegion(stage.renderWidth,stage.renderHeight);
        if (!target.Valid() || target == resident) return;
        if (crops.emplace(source.nodeId,source.socketId,target.fullWidth,target.fullHeight,
            target.x,target.y,target.width,target.height).second)
            inputCropPixels = SaturatingAdd(inputCropPixels,std::uint64_t(target.width)*target.height);
    };
    for (const auto& entry : plan.stages) {
        const auto& stage = entry.second;
        if (!stage.regionalExecutable || !stage.demand.Valid()) continue;
        for (const auto& input : stage.inputs)
            countCrop(input.source,Stack::Renderer::ScaleImageRegion(input.demand,stage.renderWidth,stage.renderHeight));
    }
    const auto finalDemand = request.visible.Valid() ?
        Stack::Renderer::ScaleImageRegion(request.visible,outputWidth,outputHeight) :
        Stack::Renderer::WholeImageRegion(outputWidth,outputHeight);
    countCrop(plan.output,Stack::Renderer::ScaleImageRegion(finalDemand,output.renderWidth,output.renderHeight));
    // Retained endpoint outputs coexist, but endpoint execution is serial.
    // Reserve a conservative scratch/input-view/publication working set for
    // the largest operation once; multiplying every retained output by that
    // reserve falsely rejected ordinary native RAW graphs. Keep complete
    // dependency extents conservative and retain engine-specific admission.
    const std::uint64_t scratchSurfaces = raw ? 7u : 4u;
    const auto scratchPixels = largestRasterPixels > std::numeric_limits<std::uint64_t>::max() / scratchSurfaces
        ? std::numeric_limits<std::uint64_t>::max() : largestRasterPixels * scratchSurfaces;
    const auto pixels = SaturatingAdd(SaturatingAdd(plan.completePixels, plan.regionalPixels), scratchPixels);
    const auto outputAndScratchBytes = pixels > std::numeric_limits<std::uint64_t>::max() / bytesPerPixel
        ? std::numeric_limits<std::uint64_t>::max() : pixels * bytesPerPixel;
    // Input sampling can preserve a float32 source even in an ordinary graph.
    const auto cropBytes = inputCropPixels > std::numeric_limits<std::uint64_t>::max() / 16u
        ? std::numeric_limits<std::uint64_t>::max() : inputCropPixels * 16u;
    plan.workingBytes = SaturatingAdd(outputAndScratchBytes,cropBytes);
    plan.fingerprint = 1;
    const auto mix = [&](std::size_t value) { plan.fingerprint ^= value + std::size_t(0x9e3779b97f4a7c15ull) +
        (plan.fingerprint << 6u) + (plan.fingerprint >> 2u); };
    for (const auto& [endpoint, stage] : plan.stages) {
        mix(std::hash<int>{}(endpoint.nodeId)); mix(std::hash<std::string>{}(endpoint.socketId));
        mix(stage.demand.Fingerprint()); mix(stage.renderWidth); mix(stage.renderHeight); mix(stage.regionalExecutable);
    }
    plan.reason = "Complete dependencies and regional pointwise consumers resolved.";
    return plan;
}

RenderGraphViewportPlan RenderTiling::PlanGraphViewport(
    const RenderGraphSnapshot& graph, int fullWidth, int fullHeight, int previewMaxDimension,
    const Stack::Renderer::ViewportPreviewRequest& request,
    const std::vector<RenderGraphViewportEndpoint>& completeConsumers) {
    return GraphViewportPlanner(graph).Plan(fullWidth, fullHeight, previewMaxDimension, request, completeConsumers);
}