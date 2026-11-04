#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"

namespace EditorNodeGraphDefinitions {
namespace {
using namespace EditorNodeGraph;
using namespace Stack::NodeMath;

std::string LeafReason(const Node& node, const LiveNodeDefinition& definition) {
    switch (node.kind) {
        case NodeKind::Value: return "A typed constant is an authored value, with no lower-level graph.";
        case NodeKind::Image: case NodeKind::RawSource: case NodeKind::RawDevelopment:
        case NodeKind::RawDecode: case NodeKind::RawDevelop: case NodeKind::RawProjectFrame:
        case NodeKind::MultiFrameDenoise: case NodeKind::MultiFrameHdr: case NodeKind::RawProjectSourceSet:
            return "Source preparation is an algorithmic boundary; no equivalent lower-level graph is defined.";
        case NodeKind::Output: case NodeKind::Preview: case NodeKind::Scope: case NodeKind::Composite:
            return "This node routes or inspects a graph result; it has no processing recipe to decompile.";
        case NodeKind::DataMath:
            return "This arithmetic operation is a primitive; no lower-level canonical graph is defined.";
        case NodeKind::ChannelSplit: case NodeKind::ChannelCombine: case NodeKind::ConstantChannel:
            return "Channel extraction, assembly, and extent realization are graph primitives.";
        case NodeKind::FieldMean:
            return "Field Mean is a spatial reduction algorithm, not a pointwise arithmetic recipe.";
        case NodeKind::Layer:
            if (node.layerType == LayerType::GaussianBlur)
                return "Gaussian convolution is an algorithmic leaf; reusable directional filters are not defined.";
            return "This image algorithm has no registered exact decomposition, including its settings and masking.";
        default: return definition.decompositionReason;
    }
}
} // namespace

NodeDecompositionCapability GetNodeDecompositionCapability(const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) {
    const auto resolved=ResolveNodeDefinition(graph,node);
    if (!resolved) return {Inspectability::OpaqueSpecialized,false,"The exact node definition is unresolved."};
    if (node.kind==NodeKind::Layer && node.layerType==LayerType::Brightness)
        return {Inspectability::TransparentGraph,true,{}};
    if (resolved.compound) {
        const auto& definition=*resolved.compound;
        const auto classification=definition.definitionClass == CompoundDefinitionClass::TransparentGraph
            ? Inspectability::TransparentGraph
            : definition.definitionClass == CompoundDefinitionClass::GraphDefinedOptimizedEquivalent
                ? Inspectability::GraphDefinedOptimizedEquivalent : Inspectability::OpaqueSpecialized;
        const bool available=definition.unpackable && definition.definitionClass != CompoundDefinitionClass::OpaqueSpecialized;
        return {classification,available,available ? std::string{} : "This definition has no editable lower-level graph."};
    }
    const auto& definition=*resolved.builtin;
    return {definition.inspectability,false,LeafReason(node,definition)};
}
} // namespace EditorNodeGraphDefinitions
