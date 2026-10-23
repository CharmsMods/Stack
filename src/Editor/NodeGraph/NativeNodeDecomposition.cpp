#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"

namespace EditorNodeGraphDefinitions {
namespace {
using namespace EditorNodeGraph;
using namespace Stack::NodeMath;

constexpr const char* kMaskInvertRecipe = "stack:native-recipe/mask-invert";
constexpr const char* kMaskInvertInputPort = "mask-in";
constexpr const char* kMaskInvertOutputPort = "mask-out";

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
    if (definition.canonicalRecipeId != kMaskInvertRecipe)
        return {definition.inspectability,false,LeafReason(node,definition)};
    if (node.role != Stack::GraphModel::NodeRole::Ordinary)
        return {definition.inspectability,false,"This node owns a protected graph boundary."};
    // Refuse an unrecognized boundary rather than deleting a driver or tap.
    bool unsupportedPort=false;
    graph.ForEachIncomingLink(node.id,[&](const auto& link) {
        unsupportedPort |= link.toSocketId!=kMaskUtilityInputSocketId;
    });
    graph.ForEachOutgoingLink(node.id,[&](const auto& link) {
        unsupportedPort |= link.fromSocketId!=kMaskOutputSocketId;
    });
    if (unsupportedPort)
        return {definition.inspectability,false,"A connected port has no binding in this native recipe; the node and wires remain unchanged."};
    return {definition.inspectability,true,{}};
}

bool BuildNativeNodeDecomposition(const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node, Stack::NodeMath::CompoundDefinition& definition,
    std::string* error, std::vector<NativeDecompositionPortBinding>* portBindings) {
    const auto capability=GetNodeDecompositionCapability(graph,node);
    const auto resolved=ResolveNodeDefinition(graph,node);
    if (!capability.canDecompile || !resolved.builtin || resolved.builtin->canonicalRecipeId!=kMaskInvertRecipe) {
        if (error) *error=capability.reason.empty() ? "This node has no native decomposition recipe." : capability.reason;
        return false;
    }
    Graph internal;
    internal.Clear();internal.SetAllowNoOutput(true);
    const bool enabled=node.maskUtilitySettings.enabled;
    auto* operation=internal.AddDataMathNode(enabled ? DataMathMode::Subtract : DataMathMode::Multiply,{});
    operation->instanceUuid="6d61736b-696e-4765-8274-000000000001";
    operation->dataMathSettings.domain=ArithmeticDomain::Channel;
    operation->dataMathSettings.constantA=1.f;
    operation->dataMathSettings.constantB=1.f;
    operation->title=enabled ? "Subtract channel from one" : "Pass channel unchanged";
    CompoundDefinition recipe;
    recipe.definitionUuid=enabled ? "6d61736b-696e-4765-8274-000000000010" : "6d61736b-696e-4765-8274-000000000011";
    recipe.identity.id=std::string(kMaskInvertRecipe)+(enabled ? "/enabled" : "/bypassed");
    recipe.identity.version={1,0,0};
    recipe.label="Invert Mask";
    recipe.description=enabled ? "Exact native Invert Mask: 1 - channel, preserving signed and HDR values."
        : "Exact bypassed native Invert Mask: channel multiplied by one.";
    recipe.ports.push_back({kMaskInvertInputPort,"Channel",PortDirection::Input,
        LogicalValueType::Channel,false,operation->instanceUuid,enabled ? "imageB" : "imageA"});
    recipe.ports.push_back({kMaskInvertOutputPort,"Result",PortDirection::Output,
        LogicalValueType::Channel,false,operation->instanceUuid,"imageOut"});
    recipe.lifecycleNotes="Native enabled state is materialized as real arithmetic. This recipe does not clamp the input or output. Repacking keeps subsequent arithmetic edits.";
    recipe.canonicalGraph=SerializeGraphPayload(nlohmann::json::array(),internal);
    RefreshCompoundDefinitionContentHash(recipe);
    definition=std::move(recipe);
    if (portBindings) *portBindings={
        {PortDirection::Input,kMaskUtilityInputSocketId,kMaskInvertInputPort},
        {PortDirection::Output,kMaskOutputSocketId,kMaskInvertOutputPort}};
    if (error) error->clear();
    return true;
}
} // namespace EditorNodeGraphDefinitions
