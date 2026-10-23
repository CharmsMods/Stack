#include "Project/RawLayerStack.h"
#include "Editor/NodeGraph/ChannelOutputs.h"
#include "Graph/LayerOperationParameters.h"
#include "Graph/GraphDocumentRules.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Stack::Project {
bool ValidateRawLayerStack(const RawLayerStackState& state, std::string& error,
    const EditorNodeGraph::Graph* composition, int sourceNodeId) {
    using Role = GraphModel::NodeRole;
    error.clear();
    if (!Assets::ValidateLibrary(state.assets, error)) return false;
    const auto fail = [&](std::string message) { error = std::move(message); return false; };
    for(const auto& position:{state.sourceCanvasPosition,state.outputCanvasPosition})
        if(position && (!std::isfinite(position->x) || !std::isfinite(position->y)))return fail("Invalid RAW canvas anchor position.");
    if (!state.nextId || state.nextId == std::numeric_limits<std::uint64_t>::max())
        return fail("Invalid RAW layer identity counter.");
    if (state.background.id != kRawBackgroundId) return fail("The Background identity is invalid.");
    GraphModel::DocumentView document;
    if (composition) document.graphs.push_back({"project", composition});
    const auto* source = composition ? composition->FindNode(sourceNodeId) : nullptr;
    const GraphModel::Endpoint technical{"$raw-source", "technical", "imageOut"};
    const GraphModel::Endpoint unedited{"$raw-source", "original", "imageOut"};
    if (source) {
        for (const auto& socket : composition->GetSockets(*source, false)) {
            if (socket.direction != EditorNodeGraph::SocketDirection::Input) continue;
            const GraphModel::Endpoint input{"project", source->instanceUuid, socket.id};
            document.dependencies.push_back({input, technical});
            document.dependencies.push_back({input, unedited});
        }
    }
    using namespace NodeMath;
    auto scene = MakeUnknownDescriptor(LogicalValueType::ColorImage);
    scene.transfer = SemanticField<TransferDescriptor>::Known({TransferKind::Linear, 0, {}});
    scene.reference = SemanticField<ReferenceState>::Known(ReferenceState::Scene);
    scene.alpha = SemanticField<AlphaMode>::Known(AlphaMode::Opaque);
    scene.precision = SemanticField<LogicalPrecision>::Known(LogicalPrecision::Float32);
    document.sources.push_back({technical,scene});
    document.sources.push_back({unedited,scene});
    EditorNodeGraph::Node technicalNode;technicalNode.id=1;technicalNode.channelOutputs=state.sourceChannelOutputs;
    EditorNodeGraph::SocketDefinition imagePort{"imageOut",1,EditorNodeGraph::SocketDirection::Output,EditorNodeGraph::SocketType::Image,"Image"};
    imagePort.logicalType=LogicalValueType::ColorImage;
    std::vector<EditorNodeGraph::SocketDefinition> sourcePorts{imagePort};
    for(const auto& output:state.sourceChannelOutputs)sourcePorts.push_back(EditorNodeGraph::ChannelOutputSocket(1,output));
    if(!EditorNodeGraph::ValidateChannelOutputs(technicalNode,sourcePorts,error))return false;
    EditorNodeGraph::GraphOutputDescription technicalDescription;technicalDescription.descriptor=scene;
    for(const auto& output:state.sourceChannelOutputs) {
        auto endpoint=technical;endpoint.portId=output.id;
        document.sources.push_back({endpoint,EditorNodeGraph::OutputRules::ExtractChannel(technicalNode,output.id,
            output.component,&technicalDescription,output.missingAlphaIsOpaque).descriptor});
        document.dependencies.push_back({technical,endpoint});
    }
    std::unordered_set<std::string> identities;
    const auto validRoute = [&](const std::optional<GraphModel::Endpoint>& route) {
        if (!route || *route == technical) return true;
        if (route->Empty() || route->graphId.empty()) return false;
        const auto* owner = FindRawAdjustmentLayer(state, route->graphId);
        if (!owner) return false;
        EditorNodeGraph::ScopedGraphOutputAnalysis descriptions(owner->graph);
        const auto producer = std::find_if(owner->graph.GetNodes().begin(),owner->graph.GetNodes().end(),
            [&](const auto& node) { return node.instanceUuid == route->nodeUuid; });
        EditorNodeGraph::SocketDefinition socket;
        if (producer == owner->graph.GetNodes().end() ||
            !owner->graph.FindSocket(producer->id,route->portId,&socket) ||
            socket.direction != EditorNodeGraph::SocketDirection::Output ||
            socket.type != EditorNodeGraph::SocketType::Image ||
            owner->graph.IsScalarSocketStream(producer->id,route->portId)) return false;
        const auto* end = FindRawRole(*owner, Role::LayerResult);
        std::string contractError;
        return end && EditorNodeGraphDefinitions::ValidateInputDescriptor(owner->graph,*end,"imageIn",
            EditorNodeGraph::DescribeGraphOutput(owner->graph,producer->id,route->portId).descriptor,contractError);
    };
    if (state.explicitRouting && !validRoute(state.imageOutput))
        return fail("The main image output requires a scene-linear color image result.");
    for (const auto* layer : RawLayersInOrder(state)) {
        if (layer->id.empty() || !identities.insert(layer->id).second)
            return fail("RAW layers must have distinct stable identities.");
        if (!std::isfinite(layer->opacity) || layer->opacity < 0 || layer->opacity > 1)
            return fail("Invalid layer opacity.");
        if (layer->canvasOrigin && (!std::isfinite(layer->canvasOrigin->x) || !std::isfinite(layer->canvasOrigin->y)))
            return fail("Invalid layer canvas position.");
        const auto* original = FindRawRole(*layer, Role::OriginalImage);
        const auto* current = FindRawRole(*layer, Role::CurrentImage);
        const auto* result = FindRawRole(*layer, Role::LayerResult);
        if (!original || !current || !result) return fail("A layer requires its Original, Current location and Layer result nodes.");
        if (original->kind != EditorNodeGraph::NodeKind::Image || current->kind != EditorNodeGraph::NodeKind::Image ||
            result->kind != EditorNodeGraph::NodeKind::Output || result->outputSettings.maskOutput ||
            result->outputSettings.publishedType != LogicalValueType::Invalid)
            return fail("The layer source or result has an invalid type.");
        for (auto role : {Role::OriginalImage, Role::CurrentImage, Role::LayerResult}) {
            int count = 0;
            for (const auto& node : layer->graph.GetNodes()) if (node.role == role) ++count;
            if (count != 1) return fail("A layer has duplicate protected source or result nodes.");
        }
        for (const auto& track : layer->animation.tracks) {
            if (track.target.graphId != layer->id || track.target.nodeUuid.empty() || track.target.parameterId.empty())
                return fail("An animation target must identify its owning graph and node instance.");
            int previousFrame = -1;
            for (const auto& key : track.keyframes) {
                if (key.frame <= previousFrame || !std::isfinite(key.value)) return fail("Invalid animation keyframes.");
                previousFrame = key.frame;
            }
        }
        document.graphs.push_back({layer->id, &layer->graph});
        const auto currentImage = RawLayerEndpoint(*layer, *current);
        const GraphModel::Endpoint blended{layer->id, "$layer-blend", "imageOut"};
        const auto input = RawLayerInputEndpoint(state, layer->id);
        if (!validRoute(input)) return fail("A layer Start requires a scene-linear color image result.");
        if (input) {
            document.dependencies.push_back({*input, currentImage});
            document.valueBindings.push_back({*input,currentImage});
        }
        document.valueBindings.push_back({unedited,RawLayerEndpoint(*layer,*original)});
        document.valueBindings.push_back({RawLayerEndpoint(*layer,*result,"imageIn"),blended});
        document.valueBindings.push_back({blended,RawLayerEndpoint(*layer,*result)});
        document.dependencies.push_back({unedited, RawLayerEndpoint(*layer, *original)});
        document.dependencies.push_back({currentImage, blended});
        document.dependencies.push_back({RawLayerEndpoint(*layer, *result, "imageIn"), blended});
        for (const auto& node : layer->graph.GetNodes()) {
            if (node.kind == EditorNodeGraph::NodeKind::Layer && !GraphModel::ValidateLayerParameters(node,error))
                return false;
            if (node.kind == EditorNodeGraph::NodeKind::RawOperation) {
                try {
                    const auto settings = RawRecipe::ReadGraphOperation(node.rawOperation);
                    if (!std::isfinite(settings.preToneExposureEv) || std::abs(settings.preToneExposureEv) > 32)
                        return fail("Exposure is outside the supported EV range.");
                } catch (const std::exception& exception) { return fail(exception.what()); }
            }
            if (node.kind == EditorNodeGraph::NodeKind::MaskTexture &&
                (node.rawCoverage.value("canvas",std::string{}) != "project" ||
                 (node.maskKind != EditorNodeGraph::MaskGeneratorKind::LinearGradient &&
                  node.maskKind != EditorNodeGraph::MaskGeneratorKind::RadialGradient &&
                  node.maskKind != EditorNodeGraph::MaskGeneratorKind::Square)))
                return fail("A live mask texture requires a project canvas and an editable shape.");
            if (node.role == Role::Reference) {
                if (node.reference.Empty()) return fail("A graph reference has no stable target identity.");
                const auto outputPort = node.kind == EditorNodeGraph::NodeKind::MaskGenerator ? "maskOut" : "imageOut";
                document.dependencies.push_back({node.reference, RawLayerEndpoint(*layer, node, outputPort)});
            }
            if (node.kind == EditorNodeGraph::NodeKind::Output)
                document.dependencies.push_back({node.role == Role::LayerResult ? blended : RawLayerEndpoint(*layer, node, "imageIn"), RawLayerEndpoint(*layer, node)});
            if (node.kind == EditorNodeGraph::NodeKind::MaskGenerator && node.rawCoverage.value("canvas",std::string{}) == "project" &&
                layer->graph.FindInputLink(node.id,"matchExtent"))
                return fail("A project-canvas mask is an independent source and cannot receive an image input.");
        }
        for (const auto& output : layer->maskOutputs) {
            if (output.id.empty() || output.nodeUuid.empty() || !identities.insert(output.id).second) return fail("Published mask identities must be distinct.");
            const auto* node = FindRawPublishedNode(*layer, output);
            if (node && !(node->kind == EditorNodeGraph::NodeKind::Output && node->outputSettings.maskOutput) &&
                !((node->kind == EditorNodeGraph::NodeKind::MaskGenerator || node->kind == EditorNodeGraph::NodeKind::MaskTexture || node->kind == EditorNodeGraph::NodeKind::CustomMask) && node->role == Role::Ordinary &&
                    node->rawCoverage.value("canvas",std::string{}) == "project"))
                return fail("A mask must identify a mask output or an independent shape source.");
        }
        if (layer->layerMask) {
            const auto* owner = FindRawAdjustmentLayer(state, layer->layerMask->layerId);
            const auto* publication = FindRawMaskOutput(state, *layer->layerMask);
            const auto* node = owner && publication ? FindRawPublishedNode(*owner, *publication) : nullptr;
            if (node) document.dependencies.push_back({RawMaskEndpoint(*owner, *node), blended});
            // An unresolved optional mask leaves the receiving operation unrestricted.
        }
    }
    if (source) if (const auto output = RawMainOutputEndpoint(state)) {
        document.dependencies.push_back({*output, {"project", source->instanceUuid, "imageOut"}});
        document.valueBindings.push_back({*output, {"project", source->instanceUuid, "imageOut"}});
    }
    const auto analysis = GraphModel::AnalyzeDocument(document);
    if (!analysis.valid) return fail(analysis.errors.front());
    return true;
}
} // namespace Stack::Project
