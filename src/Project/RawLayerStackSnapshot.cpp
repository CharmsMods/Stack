#include "Project/RawLayerStackSnapshot.h"

#include "Project/GraphSnapshotBuilder.h"
#include "Project/GraphImageContracts.h"
#include "NodeMath/DescriptorSerialization.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace Stack::Project {
namespace {
using namespace Stack::NodeMath;
struct Socket { int node = 0; std::string socket = "imageOut"; };
std::string MaskKey(const RawMaskReference& mask) { return mask.layerId + "/" + mask.outputId; }
} // namespace

Stack::NodeMath::ValueDescriptor RawLayerSceneDescriptor(Raw::RawWorkingSpace space) {
    using namespace Stack::NodeMath;
    auto descriptor = MakeUnknownDescriptor(LogicalValueType::ColorImage);
    descriptor.color = SemanticField<ColorIdentity>::Known({
        space == Raw::RawWorkingSpace::LinearRec2020D65 ? "rec2020-d65" : "srgb-d65", {}, ColorRelation::Standard});
    descriptor.transfer = SemanticField<TransferDescriptor>::Known({TransferKind::Linear, 0.0, {}});
    descriptor.reference = SemanticField<ReferenceState>::Known(ReferenceState::Scene);
    descriptor.alpha = SemanticField<AlphaMode>::Known(AlphaMode::Opaque);
    descriptor.precision = SemanticField<LogicalPrecision>::Known(LogicalPrecision::Float32);
    return descriptor;
}

bool LowerRawLayerStack(RenderGraphSnapshot& snapshot, const RawLayerStackState& state,
    int backgroundNodeId, std::uint64_t revision, std::string& error, int frame, int duration, int framesPerSecond, const EditorNodeGraph::Graph* composition) {
    if (!ValidateRawLayerStack(state, error, composition, backgroundNodeId)) return false;
    auto candidate = snapshot;
    const auto backgroundIt = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
        [&](const auto& node) { return node.nodeId == backgroundNodeId; });
    if (backgroundIt == candidate.nodes.end() || (backgroundIt->kind != RenderGraphNodeKind::RawDevelopment && backgroundIt->kind != RenderGraphNodeKind::RawProjectSourceSet)) {
        error = "The RAW layer stack has no developed Background source.";
        return false;
    }
    try {
        std::unordered_set<int> allocated;
        for (const auto& node : candidate.nodes) allocated.insert(node.nodeId);
        const auto stableAllocate = [&](const std::string& key) {
            std::uint32_t hash = 2166136261u;
            for (unsigned char c : key) hash = (hash ^ c) * 16777619u;
            int id = 1024 + static_cast<int>(hash % 1000000000u);
            while (!allocated.insert(id).second) ++id;
            return id;
        };
        const auto source = *backgroundIt;
        const auto recipe = source.rawDevelopment.recipe;
        const auto scene = RawLayerSceneDescriptor(recipe.technical.workingSpace);
        const auto sceneIdentity = DescriptorContentIdentity(scene);
        const int base = stableAllocate("raw/technical");
        const int original = stableAllocate("raw/original");
        candidate.rawLayerBackgroundNodeId = base;
        backgroundIt->nodeId = base;
        backgroundIt->rawDevelopment.recipe = RawRecipe::BuildTechnicalSourceRecipe(recipe);
        backgroundIt->semanticDescriptor = scene;
        backgroundIt->semanticDescriptorIdentity = sceneIdentity;
        auto unedited = source;
        unedited.nodeId = original;
        unedited.rawProjectSourceSet.postRecipeRevision = 0;
        unedited.rawDevelopment.recipe = RawRecipe::BuildUneditedSourceRecipe(recipe);
        unedited.semanticDescriptor = scene;
        unedited.semanticDescriptorIdentity = sceneIdentity;
        candidate.nodes.push_back(std::move(unedited));
        // Both source developments consume the same immutable sensor/merge input.
        // Keep input links when the outer RAW node uses a graph-provided source.
        const auto sourceLinks = candidate.links;
        for (auto& connection : candidate.links)
            if (connection.toNodeId == backgroundNodeId) connection.toNodeId = base;
        for (auto connection : sourceLinks) if (connection.toNodeId == backgroundNodeId) {
            connection.toNodeId = original; candidate.links.push_back(std::move(connection));
        }
        const auto addNode = [&](RenderGraphNode node) {
            node.requestRevision = revision;
            if (node.semanticDescriptor.logicalType == LogicalValueType::Invalid) {
                node.semanticDescriptor = scene;
                node.semanticDescriptorIdentity = sceneIdentity;
            }
            candidate.nodes.push_back(std::move(node));
        };
        const auto link = [&](Socket from, int target, std::string port) {
            RenderGraphLink value;
            value.fromNodeId = from.node; value.fromSocketId = std::move(from.socket);
            value.toNodeId = target; value.toSocketId = std::move(port);
            candidate.links.push_back(std::move(value));
        };
        const auto pass = [&](int id, Socket input) {
            RenderGraphNode node;
            node.nodeId = id; node.kind = RenderGraphNodeKind::Output;
            addNode(std::move(node));
            if (input.node > 0) link(std::move(input), id, "imageIn");
        };
        const int emptyMask = stableAllocate("raw/empty-mask");
        RenderGraphNode zero;
        zero.nodeId = emptyMask; zero.kind = RenderGraphNodeKind::MaskGenerator;
        zero.maskKind = RenderMaskGeneratorKind::Solid; zero.maskSettings.value = 0.f;
        zero.semanticDescriptor = MakeUnknownDescriptor(LogicalValueType::ScalarField);
        addNode(std::move(zero));
        link({base}, emptyMask, EditorNodeGraph::kMatchExtentInputSocketId);

        struct LayerCapture {
            const RawAdjustmentLayer* layer;
            RenderGraphSnapshot snapshot;
            std::unordered_map<int, int> ids;
            int incoming = 0;
            int result = 0;
        };
        std::vector<LayerCapture> captures;
        std::unordered_set<int> typedAliases;
        std::unordered_map<std::string, Socket> endpoints;
        const auto endpointKey = [](const GraphModel::Endpoint& endpoint) {
            return endpoint.graphId + "/" + endpoint.nodeUuid + "/" + endpoint.portId;
        };
        if (composition) for (const auto& node : composition->GetNodes()) {
            if (node.kind == EditorNodeGraph::NodeKind::Output &&
                node.outputSettings.publishedType != LogicalValueType::Invalid &&
                node.outputSettings.publishedType != LogicalValueType::ColorImage &&
                !EditorNodeGraph::IsSingleChannelValue(node.outputSettings.publishedType))
                typedAliases.insert(node.id);
            if (node.kind == EditorNodeGraph::NodeKind::Output)
                endpoints[endpointKey({"project",node.instanceUuid,"imageOut"})] = {node.id,"imageOut"};
            for (const auto& socket : composition->GetSockets(node, false))
                if (socket.direction == EditorNodeGraph::SocketDirection::Output)
                    endpoints[endpointKey({"project", node.instanceUuid, socket.id})] = {node.id, socket.id};
        }
        for (const auto& alias : candidate.authoredOutputAliases)
            endpoints[endpointKey(alias.authored)] = {alias.nodeId,alias.socketId};
        const auto isTypedAlias = [](LogicalValueType type) {
            return type != LogicalValueType::Invalid && type != LogicalValueType::ColorImage &&
                !EditorNodeGraph::IsSingleChannelValue(type);
        };
        for (const auto& node : candidate.nodes)
            if (isTypedAlias(node.publishedType)) typedAliases.insert(node.nodeId);
        const std::vector<std::shared_ptr<LayerBase>> noLayers;
        const std::unordered_map<int, std::uint64_t> noDirtyNodes;
        const std::shared_ptr<const Raw::RawImageData> noRaw;
        const std::vector<Timeline::AnimatableParameterTarget> noLiveTargets;
        int previous = base;
        for (const auto* layer : RawLayersInOrder(state)) {
            GraphSnapshotInputs input{layer->graph, noLayers, layer->animation, noDirtyNodes,
                recipe, noRaw, noLiveTargets};
            input.layerSettings = &layer->processingSettings;
            input.graphId = layer->id;
            input.frame = {frame, duration, framesPerSecond};
            EditorNodeGraph::GraphOutputContext sources;
            for (const auto& node : layer->graph.GetNodes())
                if (node.role == GraphModel::NodeRole::OriginalImage ||
                    node.role == GraphModel::NodeRole::CurrentImage ||
                    (node.role == GraphModel::NodeRole::Reference && node.kind == EditorNodeGraph::NodeKind::Image))
                    sources.sourceDescriptors[EditorNodeGraph::GraphOutputIdentity(node.id, "imageOut")] = scene;
            input.outputContextOverrides = &sources;
            LayerCapture capture{layer, BuildGraphSnapshot(input).snapshot, {}, previous, stableAllocate(layer->id + "/result")};
            for (const auto& node : capture.snapshot.nodes) {
                const auto* authored = layer->graph.FindNode(node.nodeId);
                capture.ids.emplace(node.nodeId, stableAllocate(layer->id + "/" +
                    (authored ? authored->instanceUuid : "$expanded-" + std::to_string(node.nodeId))));
                if (isTypedAlias(node.publishedType)) typedAliases.insert(capture.ids.at(node.nodeId));
            }
            for (const auto& node : layer->graph.GetNodes()) {
                const auto id = capture.ids.find(node.id);
                if (id == capture.ids.end()) continue;
                const auto type = node.role == GraphModel::NodeRole::Reference ? node.referenceType : node.outputSettings.publishedType;
                if (type != LogicalValueType::Invalid && type != LogicalValueType::ColorImage &&
                    !EditorNodeGraph::IsSingleChannelValue(type)) typedAliases.insert(id->second);
                for (const auto& socket : layer->graph.GetSockets(node, false))
                    if (socket.direction == EditorNodeGraph::SocketDirection::Output) endpoints[endpointKey(RawLayerEndpoint(*layer, node, socket.id))] = {id->second, socket.id};
                if (node.kind == EditorNodeGraph::NodeKind::Output)
                    endpoints[endpointKey(RawLayerEndpoint(*layer, node))] = {node.role == GraphModel::NodeRole::LayerResult ? capture.result : id->second};
            }
            for (const auto& alias : capture.snapshot.authoredOutputAliases)
                endpoints[endpointKey(alias.authored)] = {capture.ids.at(alias.nodeId),alias.socketId};
            candidate.rawLayerMaskNodeIds[layer->id] = capture.ids;
            candidate.rawLayerStageNodeIds[layer->id + "/input"] = previous;
            candidate.rawLayerStageNodeIds[layer->id + "/output"] = capture.result;
            previous = capture.result;
            captures.push_back(std::move(capture));
        }
        std::unordered_map<std::string, Socket> masks;
        for (const auto& capture : captures) {
            const auto& layer = *capture.layer;
            const auto complete = layer.graph.GetConnectedOutputNodeIds();
            for (const auto& output : layer.maskOutputs) {
                Socket value{emptyMask, "maskOut"};
                const auto* node = FindRawPublishedNode(layer, output);
                const auto input = std::find_if(capture.snapshot.links.begin(),capture.snapshot.links.end(),
                    [&](const auto& link) { return node && link.toNodeId == node->id && link.toSocketId == "imageIn"; });
                if (node && node->outputEnabled && input != capture.snapshot.links.end() && capture.ids.count(input->fromNodeId) &&
                    std::find(complete.begin(), complete.end(), node->id) != complete.end())
                    value = {capture.ids.at(input->fromNodeId), input->fromSocketId};
                masks[MaskKey({layer.id, output.id})] = value;
                candidate.rawLayerMaskOutputs[MaskKey({layer.id, output.id})] = {value.node, value.socket};
                if (node) endpoints[endpointKey(RawLayerEndpoint(layer, *node))] = value;
            }
        }
        for (auto& capture : captures) {
            const auto& layer = *capture.layer;
            const auto completed = layer.graph.GetConnectedOutputNodeIds();
            for (const auto& node : layer.graph.GetNodes()) {
                if (node.kind == EditorNodeGraph::NodeKind::Output && capture.ids.count(node.id) &&
                    std::find(completed.begin(), completed.end(), node.id) == completed.end())
                    candidate.unavailableOutputs[capture.ids.at(node.id)] = layer.name + " / " + node.title + ": connect its required image inputs.";
            }
            for (auto& node : capture.snapshot.nodes) {
                const auto* authored = layer.graph.FindNode(node.nodeId);
                const int localId = node.nodeId;
                node.nodeId = capture.ids.at(node.nodeId);
                if (node.role == GraphModel::NodeRole::OriginalImage) pass(node.nodeId, {original});
                else if (node.role == GraphModel::NodeRole::CurrentImage) pass(node.nodeId, {capture.incoming});
                else if (node.role == GraphModel::NodeRole::Reference) {
                    const auto found = endpoints.find(endpointKey(node.reference));
                    const bool mask = node.kind == RenderGraphNodeKind::MaskGenerator;
                    if (!mask && found == endpoints.end())
                        candidate.unavailableOutputs[node.nodeId] = layer.name + " / " + (authored ? authored->title : "Compound reference") + ": the referenced producer is unavailable.";
                    pass(node.nodeId, found != endpoints.end() ? found->second :
                        mask ? Socket{emptyMask, "maskOut"} : Socket{});
                } else {
                    if (node.kind == RenderGraphNodeKind::MaskGenerator &&
                        std::none_of(capture.snapshot.links.begin(),capture.snapshot.links.end(),[&](const auto& connection) {
                            return connection.toNodeId == localId && connection.toSocketId == EditorNodeGraph::kMatchExtentInputSocketId;
                        }))
                        link({base}, node.nodeId, EditorNodeGraph::kMatchExtentInputSocketId);
                    if (node.kind == RenderGraphNodeKind::RawOperation || node.kind == RenderGraphNodeKind::MaskGenerator) {
                        node.rawWorkingSpace = recipe.technical.workingSpace;
                        node.nativeWidth = source.rawDevelopment.embeddedRawData ? Raw::DisplayWidth(source.rawDevelopment.embeddedRawData->metadata) : 0;
                        node.nativeHeight = source.rawDevelopment.embeddedRawData ? Raw::DisplayHeight(source.rawDevelopment.embeddedRawData->metadata) : 0;
                    }
                    addNode(std::move(node));
                }
            }
            for (auto connection : capture.snapshot.links) {
                connection.fromNodeId = capture.ids.at(connection.fromNodeId);
                connection.toNodeId = capture.ids.at(connection.toNodeId);
                candidate.links.push_back(std::move(connection));
            }
            const auto* result = FindRawRole(layer, GraphModel::NodeRole::LayerResult);
            if (!layer.enabled || layer.opacity == 0.f) pass(capture.result, {capture.incoming});
            else if (!layer.layerMask && layer.opacity == 1.f) pass(capture.result, {capture.ids.at(result->id)});
            else {
                RenderGraphNode blend;
                blend.nodeId = capture.result; blend.kind = RenderGraphNodeKind::Mix;
                blend.mixFactor = layer.opacity; blend.mixBlendMode = RenderMixBlendMode::Normal;
                addNode(std::move(blend));
                link({capture.incoming}, capture.result, "imageA");
                link({capture.ids.at(result->id)}, capture.result, "imageB");
                if (layer.layerMask) {
                    const auto mask = masks.find(MaskKey(*layer.layerMask));
                    link(mask == masks.end() ? Socket{emptyMask, "maskOut"} : mask->second, capture.result, "factor");
                }
            }
        }
        std::vector<RenderGraphNode> outerReferences;
        for (const auto& node : candidate.nodes)
            if (node.role == GraphModel::NodeRole::Reference) outerReferences.push_back(node);
        for (const auto& authored : outerReferences) {
            const auto resolved = endpoints.find(endpointKey(authored.reference));
            const bool mask = authored.kind == RenderGraphNodeKind::MaskGenerator;
            candidate.nodes.erase(std::remove_if(candidate.nodes.begin(), candidate.nodes.end(),
                [&](const auto& node) { return node.nodeId == authored.nodeId; }), candidate.nodes.end());
            if (!mask && resolved == endpoints.end())
                candidate.unavailableOutputs[authored.nodeId] = "The referenced producer is unavailable.";
            pass(authored.nodeId, resolved != endpoints.end() ? resolved->second : mask ? Socket{emptyMask,"maskOut"} : Socket{});
        }
        // Typed publications are value aliases, not image conversions. Resolve
        // them before the existing scalar/frequency consumers are scheduled.
        std::unordered_map<int,Socket> aliasInputs;
        for (const auto& connection : candidate.links)
            if (typedAliases.count(connection.toNodeId) && connection.toSocketId == "imageIn")
                aliasInputs[connection.toNodeId] = {connection.fromNodeId,connection.fromSocketId};
        for (auto& connection : candidate.links) {
            std::unordered_set<int> visited;
            while (typedAliases.count(connection.fromNodeId) && aliasInputs.count(connection.fromNodeId) && visited.insert(connection.fromNodeId).second) {
                const auto source = aliasInputs.at(connection.fromNodeId);
                connection.fromNodeId = source.node; connection.fromSocketId = source.socket;
            }
        }
        pass(backgroundNodeId, {previous});
        // The finished RAW stack remains scene-linear for outer composition.
        // The presentation transform runs once at the requested final output.
        {
            RenderGraphNode view;
            view.nodeId = stableAllocate("raw/presentation");
            const int viewId = view.nodeId;
            candidate.rawLayerViewNodeId = viewId;
            view.kind = RenderGraphNodeKind::Layer;
            view.layerJson = recipe.viewTransform.layerJson;
            view.layerJson["type"] = "ViewTransform";
            view.layerJson["inputWorkingSpace"] = RawRecipe::WorkingSpaceStableString(recipe.technical.workingSpace);
            auto displayed = RawLayerSceneDescriptor(Raw::RawWorkingSpace::LinearSrgbD65);
            if (RawRecipe::IsViewTransformEnabled(recipe)) {
                displayed.reference = SemanticField<ReferenceState>::Known(ReferenceState::Display);
                displayed.transfer = SemanticField<TransferDescriptor>::Known({
                    recipe.viewTransform.layerJson.value("encodeSrgbOutput", false) ? TransferKind::Srgb : TransferKind::Linear,
                    0.0, {}});
            } else displayed = scene;
            view.semanticDescriptor = displayed;
            view.semanticDescriptorIdentity = DescriptorContentIdentity(displayed);
            candidate.outputDescriptor = displayed;
            candidate.outputDescriptorIdentity = view.semanticDescriptorIdentity;
            addNode(std::move(view));
            link({candidate.outputNodeId, candidate.outputSocketId.empty() ? "imageOut" : candidate.outputSocketId}, viewId, "imageIn");
            candidate.outputNodeId = viewId;
            candidate.outputSocketId = "imageOut";
        }
        if (recipe.cropRotation.cropEnabled) {
            RenderGraphNode crop;
            crop.nodeId = stableAllocate("raw/final-crop");
            const int cropId = crop.nodeId;
            crop.kind = RenderGraphNodeKind::Layer;
            crop.layerJson = {{"type", "RawOutputCrop"}, {"recipe", RawRecipe::SerializeRecipe(recipe)}};
            crop.semanticDescriptor = candidate.outputDescriptor;
            crop.semanticDescriptorIdentity = candidate.outputDescriptorIdentity;
            addNode(std::move(crop));
            link({candidate.outputNodeId, "imageOut"}, cropId, "imageIn");
            candidate.outputNodeId = cropId;
        }
        BindGraphImageContracts(candidate);
        candidate.semanticFingerprint += ":raw-layer-graphs:" + std::to_string(revision);
        snapshot = std::move(candidate);
        return true;
    } catch (const std::exception& exception) {
        error = std::string("Cannot build RAW layer rendering: ") + exception.what();
        return false;
    }
}
bool IsRawLayerOutputAvailable(const RenderGraphSnapshot& graph, std::string& error) {
    if (graph.outputNodeId <= 0) { error = "Connect an image output."; return false; }
    std::unordered_map<int, const RenderGraphNode*> nodes;
    std::unordered_map<int, std::vector<const RenderGraphLink*>> inputs;
    for (const auto& node : graph.nodes) nodes[node.nodeId] = &node;
    for (const auto& link : graph.links) inputs[link.toNodeId].push_back(&link);
    std::vector<Socket> pending{{graph.outputNodeId, graph.outputSocketId.empty() ? "imageOut" : graph.outputSocketId}};
    std::unordered_set<std::string> visited;
    while (!pending.empty()) {
        const auto endpoint = pending.back(); pending.pop_back();
        if (!visited.insert(std::to_string(endpoint.node) + "/" + endpoint.socket).second) continue;
        const auto unavailable = graph.unavailableOutputs.find(endpoint.node);
        if (unavailable != graph.unavailableOutputs.end()) { error = unavailable->second; return false; }
        const auto node = nodes.find(endpoint.node);
        if (node == nodes.end()) { error = "The requested image producer is missing."; return false; }
        for (const auto* link : inputs[endpoint.node]) {
            // Missing mask coverage is defined as empty. It does not fabricate
            // a photo image, and it must not make an unused mask draft fatal.
            if (link->toSocketId == "maskIn" || link->toSocketId == "factor" ||
                link->toSocketId.rfind("gradient:", 0) == 0 || link->toSocketId.rfind("area:", 0) == 0) continue;
            if (GraphModel::OutputDependsOnInput(node->second->outputDependencies, endpoint.socket, link->toSocketId))
                pending.push_back({link->fromNodeId, link->fromSocketId});
        }
    }
    error.clear(); return true;
}

bool ConfigureRawLayerOutputPreview(RenderGraphSnapshot& graph, int nodeId,
    const std::string& socket, std::string& error) {
    const auto node = std::find_if(graph.nodes.begin(),graph.nodes.end(),[&](const auto& n) { return n.nodeId == nodeId; });
    if (node == graph.nodes.end()) { error = "The requested output is unavailable."; return false; }
    graph.outputNodeId = nodeId; graph.outputSocketId = socket;
    graph.outputDescriptor = node->semanticDescriptor;
    graph.outputDescriptorIdentity = node->semanticDescriptorIdentity;
    const auto& descriptor = node->semanticDescriptor;
    if (descriptor.logicalType == LogicalValueType::ColorImage && descriptor.reference.state == KnowledgeState::Known &&
        descriptor.reference.value == ReferenceState::Scene) {
        const auto input = std::find_if(graph.links.begin(),graph.links.end(),[&](const auto& link) {
            return link.toNodeId == graph.rawLayerViewNodeId && link.toSocketId == "imageIn";
        });
        if (input == graph.links.end()) { error = "The project View is unavailable."; return false; }
        input->fromNodeId = nodeId; input->fromSocketId = socket;
        graph.outputNodeId = graph.rawLayerViewNodeId; graph.outputSocketId = "imageOut";
        BindGraphImageContracts(graph);
    }
    return IsRawLayerOutputAvailable(graph,error);
}

bool ConfigureRawLayerThumbnail(RenderGraphSnapshot& graph, const std::string& layerId,
    const std::string& maskId, std::string& error) {
    graph.rawLayerScopeNodeId = 0;
    if (!maskId.empty()) {
        const auto mask = graph.rawLayerMaskOutputs.find(layerId + "/" + maskId);
        if (mask == graph.rawLayerMaskOutputs.end()) {
            error = "Mask output is unavailable.";
            return false;
        }
        graph.outputNodeId = mask->second.first;
        graph.outputSocketId = mask->second.second;
        return true;
    }
    // A Background-only document already has its normal viewing path.
    if (layerId == kRawBackgroundId && graph.rawLayerStageNodeIds.empty()) return true;
    const auto stage = graph.rawLayerStageNodeIds.find(layerId + "/output");
    auto viewInput = std::find_if(graph.links.begin(), graph.links.end(), [&](const auto& link) {
        return link.toNodeId == graph.rawLayerViewNodeId && link.toSocketId == "imageIn";
    });
    if (stage == graph.rawLayerStageNodeIds.end() || viewInput == graph.links.end()) {
        error = "Layer output is unavailable.";
        return false;
    }
    // Reuse the project's view settings on this preview copy only. Never bake
    // the displayed thumbnail back into the authored image or mask pipeline.
    viewInput->fromNodeId = stage->second;
    viewInput->fromSocketId = "imageOut";
    graph.outputNodeId = graph.rawLayerViewNodeId;
    graph.outputSocketId = "imageOut";
    return IsRawLayerOutputAvailable(graph, error);
}
} // namespace Stack::Project
