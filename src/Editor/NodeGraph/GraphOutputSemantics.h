#pragma once

#include "NodeMath/ContractTypes.h"

#include <string>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace EditorNodeGraph {
class Graph;
struct Node;
struct CompoundExpansionResult;

// During lowering, connection checks inspect the current partial graph instead
// of recursively lowering the entire graph again for each reconstructed wire.
class ScopedCompoundLoweringDescription {
public:
    ScopedCompoundLoweringDescription();
    ~ScopedCompoundLoweringDescription();
private:
    bool previous;
};

struct GraphOutputDescription {
    Stack::NodeMath::ValueDescriptor descriptor;
    // Identity of the component whose meaning survives this operation.
    // Empty for generated or mixed channels; roles distinguish neutral from unknown.
    std::string componentOrigin;
    std::string componentRole;
    // Assembly provenance, not a claim that a numeric channel is color.
    Stack::NodeMath::SemanticField<Stack::NodeMath::ColorIdentity> channelBasis =
        Stack::NodeMath::SemanticField<Stack::NodeMath::ColorIdentity>::Unknown();
    Stack::NodeMath::SemanticField<Stack::NodeMath::TransferDescriptor> channelTransfer =
        Stack::NodeMath::SemanticField<Stack::NodeMath::TransferDescriptor>::Unknown();
    Stack::NodeMath::SemanticField<Stack::NodeMath::ReferenceState> channelReference =
        Stack::NodeMath::SemanticField<Stack::NodeMath::ReferenceState>::Unknown();
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
// Publish authored compound ports from an already-described execution graph.
// No second lowering or source resolution is needed.
void BindCompoundOutputDescriptions(
    GraphOutputDescriptions& outputs, const CompoundExpansionResult& expansion);

// Batch semantic queries during one read-only model operation. The borrowed
// graph must not be edited until this scope ends; nothing survives the scope.
// Drawing uses the document owner's revisioned cache instead.
class ScopedGraphOutputAnalysis {
public:
    explicit ScopedGraphOutputAnalysis(const Graph& graph);
    // Read-only presentation queries may borrow the owner's current output
    // descriptions. The resolver must describe this exact graph revision.
    ScopedGraphOutputAnalysis(const Graph& graph, GraphOutputContext context);
    ~ScopedGraphOutputAnalysis();
    ScopedGraphOutputAnalysis(const ScopedGraphOutputAnalysis&) = delete;
    ScopedGraphOutputAnalysis& operator=(const ScopedGraphOutputAnalysis&) = delete;
private:
    struct State;
    std::unique_ptr<State> state;
    friend GraphOutputDescription DescribeGraphOutput(
        const Graph&, int, const std::string&, const GraphOutputContext&);
};

namespace OutputRules {
using Inputs = std::unordered_map<std::string, const GraphOutputDescription*>;
GraphOutputDescription ExtractChannel(const Node& node, const std::string& socketId,
    const std::string& component, const GraphOutputDescription* image, bool opaqueAlpha = false);
GraphOutputDescription Describe(
    const Node& node, const std::string& socketId, const Inputs& inputs,
    Stack::NodeMath::LogicalValueType declaredType, const GraphOutputContext& context);
}
} // namespace EditorNodeGraph
