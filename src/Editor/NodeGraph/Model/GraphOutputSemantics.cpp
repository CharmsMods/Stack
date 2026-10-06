#include "Editor/NodeGraph/GraphOutputSemantics.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/SocketPresentation.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"

#include <algorithm>
#include <unordered_set>

namespace EditorNodeGraph {
using namespace Stack::NodeMath;

std::string GraphOutputIdentity(int nodeId, const std::string& socketId) {
    return "output-" + std::to_string(nodeId) + "-" + socketId;
}

bool IsSingleChannelValue(LogicalValueType type) {
    return type == LogicalValueType::Channel || type == LogicalValueType::Mask ||
        type == LogicalValueType::ScalarField;
}

std::string OutputChannelColor(const GraphOutputDescription& output) {
    if (!IsSingleChannelValue(output.descriptor.logicalType) ||
        output.descriptor.channels.state != KnowledgeState::Known ||
        output.descriptor.channels.value.roles.size() != 1) return {};
    std::string role = output.descriptor.channels.value.roles.front();
    std::transform(role.begin(), role.end(), role.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (role == "r" || role == "red") return "r";
    if (role == "g" || role == "green") return "g";
    if (role == "b" || role == "blue") return "b";
    if (role == "a" || role == "alpha") return "a";
    return {};
}

namespace {
// Expansion validates reconstructed links. Those checks describe the partially
// expanded graph directly; only the outer query expands compound boundaries.
thread_local bool expandingCompounds = false;
struct ExpansionScope {
    ExpansionScope() { expandingCompounds = true; }
    ~ExpansionScope() { expandingCompounds = false; }
};
bool HasCompounds(const Graph& graph) {
    return std::any_of(graph.GetNodes().begin(), graph.GetNodes().end(), [](const Node& node) {
        return node.kind == NodeKind::Compound;
    });
}
std::vector<SocketDefinition> OutputSockets(const Graph& graph, const Node& node) {
    // LUT input visibility consults channel identity. Its output declaration
    // does not depend on that visibility, so avoid re-entering the analyzer.
    if (node.kind == NodeKind::Lut) {
        SocketDefinition socket;
        socket.id = kImageOutputSocketId;
        socket.logicalType = LogicalValueType::ColorImage;
        socket.direction = SocketDirection::Output;
        return { socket };
    }
    auto sockets = graph.GetSockets(node, false);
    sockets.erase(std::remove_if(sockets.begin(), sockets.end(), [](const auto& socket) {
        return socket.direction != SocketDirection::Output;
    }), sockets.end());
    // Output is an inspection boundary, but still has a described result.
    if (node.kind == NodeKind::Output || node.kind == NodeKind::Preview || node.kind == NodeKind::Scope) {
        SocketDefinition socket;
        socket.id = kImageOutputSocketId;
        socket.logicalType = LogicalValueType::Invalid;
        socket.direction = SocketDirection::Output;
        sockets.push_back(socket);
    }
    return sockets;
}

class Analyzer {
public:
    Analyzer(const Graph& graph, const GraphOutputContext& context) : graph(graph), context(context) {}

    void Visit(int root) {
        const Node* rootNode = graph.FindNode(root);
        if (!rootNode) return;
        struct Frame { int nodeId; std::string socketId; bool expanded = false; };
        for (const auto& rootSocket : OutputSockets(graph, *rootNode)) {
            std::vector<Frame> pending{{root, rootSocket.id}};
            while (!pending.empty()) {
                // Copy before pushing dependencies: vector growth invalidates references.
                const Frame frame = pending.back();
                const auto key = GraphOutputIdentity(frame.nodeId, frame.socketId);
                if (complete.count(key)) { pending.pop_back(); continue; }
                const Node* node = graph.FindNode(frame.nodeId);
                if (!node) { pending.pop_back(); continue; }
                if (!frame.expanded) {
                    if (context.resolveOutput) {
                        if (auto resolved = context.resolveOutput(*node, frame.socketId)) {
                            outputs[key] = std::move(*resolved);
                            complete.insert(key); pending.pop_back(); continue;
                        }
                    }
                    pending.back().expanded = true;
                    visiting.insert(key);
                    graph.ForEachIncomingLink(node->id, [&](const Link& link) {
                        if (!EditorNodeGraphDefinitions::OutputDependsOnInput(graph, *node, frame.socketId, link.toSocketId)) return;
                        const auto inputKey = GraphOutputIdentity(link.fromNodeId, link.fromSocketId);
                        if (!complete.count(inputKey) && !visiting.count(inputKey))
                            pending.push_back({link.fromNodeId, link.fromSocketId});
                    });
                    continue;
                }
                OutputRules::Inputs inputs;
                graph.ForEachIncomingLink(node->id, [&](const Link& link) {
                    if (!EditorNodeGraphDefinitions::OutputDependsOnInput(graph, *node, frame.socketId, link.toSocketId)) return;
                    const auto input = outputs.find(GraphOutputIdentity(link.fromNodeId, link.fromSocketId));
                    if (input != outputs.end()) inputs[link.toSocketId] = &input->second;
                });
                auto sockets = OutputSockets(graph, *node);
                const auto socket = std::find_if(sockets.begin(), sockets.end(), [&](const auto& value) { return value.id == frame.socketId; });
                if (socket != sockets.end())
                    outputs[key] = OutputRules::Describe(*node, frame.socketId, inputs, socket->logicalType, context);
                visiting.erase(key);
                complete.insert(key);
                pending.pop_back();
            }
        }
    }

    const Graph& graph;
    const GraphOutputContext& context;
    GraphOutputDescriptions outputs;
    std::unordered_set<std::string> complete;
    std::unordered_set<std::string> visiting;
};
}

GraphOutputDescription DescribeGraphOutput(
    const Graph& graph, int nodeId, const std::string& socketId, const GraphOutputContext& context) {
    if (!expandingCompounds && HasCompounds(graph)) {
        const auto outputs = DescribeGraphOutputs(graph, context);
        const auto output = outputs.find(GraphOutputIdentity(nodeId, socketId));
        return output == outputs.end() ? GraphOutputDescription{} : output->second;
    }
    Analyzer analyzer(graph, context);
    analyzer.Visit(nodeId);
    const auto output = analyzer.outputs.find(GraphOutputIdentity(nodeId, socketId));
    return output == analyzer.outputs.end() ? GraphOutputDescription{} : output->second;
}

GraphOutputDescriptions DescribeGraphOutputs(const Graph& graph, const GraphOutputContext& context) {
    if (!expandingCompounds && HasCompounds(graph)) {
        ExpansionScope scope;
        Graph expanded;
        CompoundExpansionResult expansion;
        if (graph.ExpandAllCompoundNodes(expanded, &expansion)) {
            Analyzer analyzer(expanded, context);
            for (const Node& node : expanded.GetNodes()) analyzer.Visit(node.id);
            for (const auto& binding : expansion.outputBindings) {
                const auto output = analyzer.outputs.find(GraphOutputIdentity(binding.expandedNodeId, binding.expandedSocketId));
                if (output != analyzer.outputs.end()) {
                    const auto description = output->second;
                    analyzer.outputs[GraphOutputIdentity(binding.authoredNodeId, binding.authoredSocketId)] = description;
                }
            }
            return std::move(analyzer.outputs);
        }
    }
    Analyzer analyzer(graph, context);
    for (const Node& node : graph.GetNodes()) analyzer.Visit(node.id);
    return std::move(analyzer.outputs);
}
} // namespace EditorNodeGraph
