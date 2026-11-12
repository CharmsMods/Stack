#include "Editor/NodeGraph/GraphCatalogInsertion.h"
#include "Editor/Internal/EditorModuleDevelopDefaults.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Editor/LayerRegistry.h"
#include "Editor/Layers/LayerBase.h"
#include <algorithm>

namespace Stack::GraphModel {
namespace {
using namespace EditorNodeGraph;
using EditorNodeGraphDefinitions::NodeCatalogEntry;

Node* CreateNode(Graph& graph, const NodeCatalogEntry& entry, Vec2 position, const Node* upstream, std::string& error) {
    if (entry.kind == NodeKind::Compound) {
        const auto* definition = EditorNodeGraphDefinitions::FindShippedCompoundTemplate(std::size_t(entry.value));
        if (!definition) return nullptr;
        const auto& templates = EditorNodeGraphDefinitions::GetShippedCompoundTemplates();
        const auto closure = NodeMath::CollectCompoundDependencyClosure(templates, {definition->identity}, &error);
        if (closure.empty()) return nullptr;
        for (const auto& reference : closure) {
            const auto* dependency = NodeMath::FindExactCompoundDefinition(templates, reference);
            if (!dependency || !graph.AddCompoundDefinition(*dependency, &error)) return nullptr;
        }
        return graph.AddCompoundNode(definition->identity, position);
    }
    if (entry.kind == NodeKind::Layer) {
        const auto type = static_cast<LayerType>(entry.value);
        if (type == LayerType::ToneCurve)
            return graph.AddRawOperationNode(Stack::RawRecipe::GraphOperationKind::ToneCurve, position);
        auto layer = LayerRegistry::CreateLayer(type);
        if (!layer) return nullptr;
        int next = 0, count = 0;
        for (const auto& node : graph.GetNodes()) if (node.kind == NodeKind::Layer) {
            next = std::max(next, node.layerIndex + 1);
            if (node.layerType == type) ++count;
        }
        if (count) layer->SetInstanceName(std::string(layer->GetDefaultName()) + " (" + std::to_string(count + 1) + ")");
        auto* node = graph.AddLayerNode(type, next, position);
        if (node) {
            node->layerParameters = layer->Serialize();
            node->layerParameters["visible"] = layer->IsVisible();
            node->title = layer->GetName();
        }
        return node;
    }
    if (entry.kind == NodeKind::Output) return graph.AddOutputNode(position);
    if (entry.kind == NodeKind::Preview) return graph.AddPreviewNode(position);
    if (entry.kind == NodeKind::Mix) return graph.AddMixNode(position);
    if (entry.kind == NodeKind::RawDevelopment) {
        RawDevelopmentPayload payload;
        payload.recipe = Stack::RawRecipe::MakeDefaultRecipe({});
        return graph.AddRawDevelopmentNode(std::move(payload), position);
    }
    if (entry.kind == NodeKind::RawDecode || entry.kind == NodeKind::RawDevelop) {
        using namespace Stack::Editor::DevelopDefaults;
        Raw::RawDevelopSettings settings;
        if (upstream && upstream->kind == NodeKind::RawSource)
            settings = BuildRawDevelopSettingsFromMetadata(upstream->rawSource.metadata);
        if (entry.kind == NodeKind::RawDecode) {
            RawDecodePayload payload;
            payload.settings = settings;
            return graph.AddRawDecodeNode(std::move(payload), position);
        }
        RawDevelopPayload payload;
        payload.settings = settings;
        payload.integratedToneLayerJson = BuildDefaultIntegratedToneLayerJson();
        return graph.AddRawDevelopNode(std::move(payload), position);
    }
    if (entry.kind == NodeKind::MaskTexture) return graph.AddMaskTextureNode(static_cast<MaskGeneratorKind>(entry.value), position);
    if (entry.kind == NodeKind::ImageGenerator) return graph.AddImageGeneratorNode(static_cast<ImageGeneratorKind>(entry.value), position);
    if (entry.kind == NodeKind::Lut) return graph.AddLutNode({}, position);
    return graph.AddDefaultOperationNode(EditorNodeGraphDefinitions::BuildPrototypeNode(entry), position);
}
}

std::optional<CatalogInsertion> PrepareCatalogInsertion(const Graph& source, const CatalogInsertionTarget& target,
    const NodeCatalogEntry& entry, Vec2 position, std::string& error, const GraphOutputContext* published) {
    error.clear();
    const auto* anchor = source.FindNode(target.nodeId);
    SocketDefinition socket;
    if (!anchor || !source.FindSocket(target.nodeId, target.socket, &socket) || socket.direction != target.direction) {
        error = "The connection target changed.";
        return std::nullopt;
    }
    const bool fromOutput = target.direction == SocketDirection::Output;
    const Link* old = nullptr;
    if (target.wire) {
        const auto& wire = *target.wire;
        old = source.FindAnyInputLink(wire.toNodeId, wire.toSocketId);
        if (!old || old->fromNodeId != wire.fromNodeId || old->fromSocketId != wire.fromSocketId ||
            (fromOutput ? old->fromNodeId != target.nodeId || old->fromSocketId != target.socket
                        : old->toNodeId != target.nodeId || old->toSocketId != target.socket)) {
            error = "The wire changed.";
            return std::nullopt;
        }
        if (old->ownership == Link::Ownership::ManagedSourceBinding) {
            error = "Stack manages this source connection.";
            return std::nullopt;
        }
    } else if (!fromOutput && source.FindAnyInputLink(target.nodeId, target.socket)) {
        error = "Choose the existing input wire.";
        return std::nullopt;
    }
    auto candidate = source.CloneForAnalysis();
    const auto* upstream = old ? source.FindNode(old->fromNodeId) : fromOutput ? anchor : nullptr;
    auto* node = CreateNode(candidate, entry, position, upstream, error);
    if (!node) {
        if (error.empty()) error = "This node cannot be created here.";
        return std::nullopt;
    }
    const int id = node->id;
    node->rawLayerOwnerId = anchor->rawLayerOwnerId;
    if (old) {
        if (const auto* downstream = source.FindNode(old->toNodeId); downstream && !downstream->rawLayerOwnerId.empty())
            node->rawLayerOwnerId = downstream->rawLayerOwnerId;
        candidate.RemoveLink(old->fromNodeId, old->fromSocketId, old->toNodeId, old->toSocketId);
    }
    const auto sockets = candidate.GetSockets(*candidate.FindNode(id), false);
    std::vector<std::string> inputs, outputs;
    for (const auto& port : sockets) {
        if (port.direction == SocketDirection::Input) inputs.push_back(port.id);
        else outputs.push_back(port.id);
    }
    if (!old && fromOutput) outputs = {{}};
    if (!old && !fromOutput) inputs = {{}};
    for (const auto& input : inputs) for (const auto& output : outputs) {
        if (old && !EditorNodeGraphDefinitions::OutputDependsOnInput(candidate, *candidate.FindNode(id), output, input)) continue;
        auto attempt = candidate.CloneForAnalysis();
        const auto connect = [&](int from, const std::string& out, int to, const std::string& in) {
            std::optional<ScopedGraphOutputAnalysis> analysis;
            if (published) analysis.emplace(attempt, *published);
            if (!attempt.TryConnectSockets(from, out, to, in, &error)) return false;
            if (attempt.HasLink(from, out, to, in)) return true;
            error = "This node would redirect the selected connection.";
            return false;
        };
        if (!input.empty() && !connect(old ? old->fromNodeId : target.nodeId,
                old ? old->fromSocketId : target.socket, id, input)) continue;
        if (!output.empty() && !connect(id, output, old ? old->toNodeId : target.nodeId,
                old ? old->toSocketId : target.socket)) continue;
        error.clear();
        return CatalogInsertion{std::move(attempt), id, input, output};
    }
    if (error.empty()) error = "This node cannot carry the selected connection.";
    return std::nullopt;
}
}
