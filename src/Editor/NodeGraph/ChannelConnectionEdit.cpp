#include "Editor/NodeGraph/ChannelConnectionEdit.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include <algorithm>

namespace EditorNodeGraph {
bool DescribeRgbAdapter(const Graph& graph, const Node& target, const std::string& input,
                        GraphOutputDescription& source) {
    using Type = Stack::NodeMath::LogicalValueType;
    if (!IsSingleChannelValue(source.descriptor.logicalType)) return false;
    const auto port = EditorNodeGraphDefinitions::GetLivePortContract(graph, target, input);
    if (!port) return false;
    const auto& types = port->port.acceptedLogicalTypes;
    if (std::find(types.begin(), types.end(), Type::ColorImage) == types.end() ||
        std::any_of(types.begin(), types.end(), IsSingleChannelValue)) return false;

    static const Node adapter = [] { Node node; node.kind = NodeKind::ChannelCombine; return node; }();
    const OutputRules::Inputs components{{"r", &source}, {"g", &source}, {"b", &source}};
    source = OutputRules::Describe(adapter, kImageOutputSocketId, components, Type::ColorImage, {});
    return true;
}

bool ConnectAuthoredSockets(Graph& candidate, int from, const std::string& output,
                            int to, const std::string& input, std::string* error,
                            int* adapterNode, const GraphOutputContext* published) {
    if (adapterNode) *adapterNode = 0;
    bool combine = false;
    {
        std::optional<ScopedGraphOutputAnalysis> scope;
        if (published) scope.emplace(candidate, *published);
        if (!candidate.CanConnectSockets(from, output, to, input, nullptr, error, false, &combine)) return false;
        if (!combine) return candidate.TryConnectSockets(from, output, to, input, error);
    }

    // Repeating the drop onto an already replicated input must not leave an orphan node.
    if (const auto* current = candidate.FindInputLink(to, input)) {
        const auto* existing = candidate.FindNode(current->fromNodeId);
        if (existing && existing->kind == NodeKind::ChannelCombine && current->fromSocketId == kImageOutputSocketId &&
            !candidate.FindAnyInputLink(existing->id, "a")) {
            const auto matches = [&](const char* component) {
                const auto* wire = candidate.FindAnyInputLink(existing->id, component);
                return wire && wire->fromNodeId == from && wire->fromSocketId == output;
            };
            if (matches("r") && matches("g") && matches("b")) {
                if (adapterNode) *adapterNode = existing->id;
                return true;
            }
        }
    }
    const auto* source = candidate.FindNode(from);
    const auto* destination = candidate.FindNode(to);
    const auto owner = source->rawLayerOwnerId.empty() ? destination->rawLayerOwnerId : source->rawLayerOwnerId;
    const Vec2 position{(source->position.x + destination->position.x) * 0.5f,
                        (source->position.y + destination->position.y) * 0.5f};
    auto* adapter = candidate.AddChannelCombineNode(position);
    adapter->rawLayerOwnerId = owner;
    const int id = adapter->id;
    // An authored reference can have more precise source metadata than its
    // local projection. Borrow it afresh for each edge of the evolving graph.
    const auto wire = [&](int producer, const std::string& port, int consumer, const std::string& socket) {
        std::optional<ScopedGraphOutputAnalysis> scope;
        if (published) scope.emplace(candidate, *published);
        return candidate.TryConnectSockets(producer, port, consumer, socket, error);
    };
    for (const auto* component : {"r", "g", "b"})
        if (!wire(from, output, id, component)) return false;
    if (!wire(id, kImageOutputSocketId, to, input)) return false;
    if (adapterNode) *adapterNode = id;
    return true;
}
}
