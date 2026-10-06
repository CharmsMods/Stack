#pragma once

#include "NodeMath/ContractTypes.h"

#include <string>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace EditorNodeGraph {
class Graph;
struct Node;

struct GraphOutputDescription {
    Stack::NodeMath::ValueDescriptor descriptor;
    // Identity of the component whose meaning survives this operation.
    // Empty for generated or mixed channels; roles distinguish neutral from unknown.
    std::string componentOrigin;
    std::string componentRole;
    std::vector<Stack::NodeMath::Diagnostic> diagnostics;
};

using GraphOutputDescriptions = std::unordered_map<std::string, GraphOutputDescription>;

struct GraphOutputContext {
    // A document owner resolves graph-qualified sources and publications.
    std::function<std::optional<GraphOutputDescription>(const Node&, const std::string&)> resolveOutput;
    // Authoritative source facts supplied by the render request, never by a consumer.
    std::unordered_map<std::string, Stack::NodeMath::ValueDescriptor> sourceDescriptors;
    Stack::NodeMath::SemanticField<Stack::NodeMath::SpatialDescriptor> canvasSpatial =
        Stack::NodeMath::SemanticField<Stack::NodeMath::SpatialDescriptor>::Unknown();
};

std::string GraphOutputIdentity(int nodeId, const std::string& socketId);
bool IsSingleChannelValue(Stack::NodeMath::LogicalValueType type);
std::string OutputChannelColor(const GraphOutputDescription& output);
GraphOutputDescription DescribeGraphOutput(
    const Graph& graph, int nodeId, const std::string& socketId,
    const GraphOutputContext& context = {});
GraphOutputDescriptions DescribeGraphOutputs(
    const Graph& graph, const GraphOutputContext& context = {});

namespace OutputRules {
using Inputs = std::unordered_map<std::string, const GraphOutputDescription*>;
GraphOutputDescription Describe(
    const Node& node, const std::string& socketId, const Inputs& inputs,
    Stack::NodeMath::LogicalValueType declaredType, const GraphOutputContext& context);
}
} // namespace EditorNodeGraph
