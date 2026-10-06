#include "Project/RawLayerStack.h"
#include "Graph/GraphDocumentRules.h"

#include <cmath>
#include <limits>
#include <unordered_set>

namespace Stack::Project {
bool ValidateRawLayerStack(const RawLayerStackState& state, std::string& error,
    const EditorNodeGraph::Graph* composition, int sourceNodeId) {
    using Role = GraphModel::NodeRole;
    error.clear();
    const auto fail = [&](std::string message) { error = std::move(message); return false; };
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
    std::unordered_set<std::string> identities;
    GraphModel::Endpoint previous = technical;
    for (const auto* layer : RawLayersInOrder(state)) {
        if (layer->id.empty() || !identities.insert(layer->id).second)
            return fail("RAW layers must have distinct stable identities.");
        if (!std::isfinite(layer->opacity) || layer->opacity < 0 || layer->opacity > 1)
            return fail("Invalid layer opacity.");
        if (!layer->processingSettings.is_array()) return fail("Invalid operation settings.");
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
        document.dependencies.push_back({previous, currentImage});
        document.valueBindings.push_back({previous,currentImage});
        document.valueBindings.push_back({unedited,RawLayerEndpoint(*layer,*original)});
        document.valueBindings.push_back({RawLayerEndpoint(*layer,*result,"imageIn"),blended});
        document.valueBindings.push_back({blended,RawLayerEndpoint(*layer,*result)});
        document.dependencies.push_back({unedited, RawLayerEndpoint(*layer, *original)});
        document.dependencies.push_back({currentImage, blended});
        document.dependencies.push_back({RawLayerEndpoint(*layer, *result, "imageIn"), blended});
        for (const auto& node : layer->graph.GetNodes()) {
            if (node.kind == EditorNodeGraph::NodeKind::Layer &&
                (node.layerIndex < 0 || static_cast<std::size_t>(node.layerIndex) >= layer->processingSettings.size()))
                return fail("An effect node has no saved operation settings.");
            if (node.kind == EditorNodeGraph::NodeKind::RawOperation) {
                try {
                    const auto settings = RawRecipe::ReadGraphOperation(node.rawOperation);
                    if (!std::isfinite(settings.preToneExposureEv) || std::abs(settings.preToneExposureEv) > 32)
                        return fail("Exposure is outside the supported EV range.");
                } catch (const std::exception& exception) { return fail(exception.what()); }
            }
            if (node.role == Role::Reference) {
                if (node.reference.Empty()) return fail("A graph reference has no stable target identity.");
                const auto outputPort = node.kind == EditorNodeGraph::NodeKind::MaskGenerator ? "maskOut" : "imageOut";
                document.dependencies.push_back({node.reference, RawLayerEndpoint(*layer, node, outputPort)});
            }
            if (node.kind == EditorNodeGraph::NodeKind::Output)
                document.dependencies.push_back({node.role == Role::LayerResult ? blended : RawLayerEndpoint(*layer, node, "imageIn"), RawLayerEndpoint(*layer, node)});
        }
        for (const auto& output : layer->maskOutputs) {
            if (output.id.empty() || output.nodeUuid.empty() || !identities.insert(output.id).second) return fail("Published mask identities must be distinct.");
            const auto* node = FindRawPublishedNode(*layer, output);
            if (node && (node->kind != EditorNodeGraph::NodeKind::Output || !node->outputSettings.maskOutput))
                return fail("A mask publication must identify a Mask Output.");
        }
        if (layer->layerMask) {
            const auto* owner = FindRawAdjustmentLayer(state, layer->layerMask->layerId);
            const auto* publication = FindRawMaskOutput(state, *layer->layerMask);
            const auto* node = owner && publication ? FindRawPublishedNode(*owner, *publication) : nullptr;
            if (node) document.dependencies.push_back({RawLayerEndpoint(*owner, *node), blended});
            // An unresolved attached mask is a valid draft with empty coverage.
        }
        previous = blended;
    }
    if (source) {
        document.dependencies.push_back({previous, {"project", source->instanceUuid, "imageOut"}});
        document.valueBindings.push_back({previous, {"project", source->instanceUuid, "imageOut"}});
    }
    const auto analysis = GraphModel::AnalyzeDocument(document);
    if (!analysis.valid) return fail(analysis.errors.front());
    return true;
}
} // namespace Stack::Project
