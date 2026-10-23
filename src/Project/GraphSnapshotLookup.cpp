#include "Project/GraphSnapshotLookup.h"

#include "NodeMath/FirstClassValue.h"
#include "NodeMath/DescriptorSerialization.h"

#include <cmath>
#include <variant>

namespace Stack::Project::GraphSnapshotInternal {

Lookup::Lookup(const EditorNodeGraph::Graph& graph) {
    const std::vector<EditorNodeGraph::Node>& nodes = graph.GetNodes();
    const std::vector<EditorNodeGraph::Link>& links = graph.GetLinks();
    m_NodeById.reserve(nodes.size());
    m_InputLinksByNode.reserve(nodes.size());
    for (const EditorNodeGraph::Node& node : nodes) {
        m_NodeById.emplace(node.id, &node);
    }
    for (const EditorNodeGraph::Link& link : links) {
        m_InputLinksByNode[link.toNodeId].push_back(&link);
    }
}

const EditorNodeGraph::Node* Lookup::FindNode(int nodeId) const {
    const auto found = m_NodeById.find(nodeId);
    return found == m_NodeById.end() ? nullptr : found->second;
}

bool Lookup::IsAnalysisLink(const EditorNodeGraph::Link& link) const {
    const EditorNodeGraph::Node* destination = FindNode(link.toNodeId);
    return destination &&
        ((destination->kind == EditorNodeGraph::NodeKind::Scope &&
          link.toSocketId == EditorNodeGraph::kScopeInputSocketId) ||
         (destination->kind == EditorNodeGraph::NodeKind::Preview &&
          link.toSocketId == EditorNodeGraph::kPreviewInputSocketId));
}

bool Lookup::TryResolveUniformScalarInput(
    int nodeId,
    const std::string& socketId,
    double& value) const {
    const auto incoming = m_InputLinksByNode.find(nodeId);
    if (incoming == m_InputLinksByNode.end()) {
        return false;
    }

    const EditorNodeGraph::Link* input = nullptr;
    for (const EditorNodeGraph::Link* link : incoming->second) {
        if (link->toSocketId == socketId) {
            input = link;
            break;
        }
    }
    if (!input ||
        input->fromSocketId != EditorNodeGraph::kValueOutputSocketId) {
        return false;
    }

    const EditorNodeGraph::Node* source = FindNode(input->fromNodeId);
    if (!source || source->kind != EditorNodeGraph::NodeKind::Value) {
        return false;
    }
    const Stack::NodeMath::FirstClassValue& typedValue = source->value.value;
    if (typedValue.logicalType != Stack::NodeMath::LogicalValueType::Scalar ||
        typedValue.storage != Stack::NodeMath::ValueStorageClass::Uniform ||
        typedValue.availability != Stack::NodeMath::ValueAvailability::Known) {
        return false;
    }
    const double* scalar = std::get_if<double>(&typedValue.payload);
    if (!scalar || !std::isfinite(*scalar)) {
        return false;
    }
    value = *scalar;
    return true;
}

} // namespace Stack::Project::GraphSnapshotInternal

namespace Stack::Project {

std::optional<RenderGraphSnapshot::OutputAlias> ResolveGraphSnapshotOutput(
    const RenderGraphSnapshot& snapshot, const GraphModel::Endpoint& authored) {
    for (const auto& alias : snapshot.authoredOutputAliases)
        if (alias.authored.graphId == authored.graphId &&
            alias.authored.nodeUuid == authored.nodeUuid &&
            alias.authored.portId == authored.portId) return alias;
    for (const auto& node : snapshot.nodes)
        if (node.authoredGraphId == authored.graphId &&
            node.authoredNodeUuid == authored.nodeUuid)
            return RenderGraphSnapshot::OutputAlias{authored, node.nodeId, authored.portId};
    return std::nullopt;
}

void AppendGraphSnapshotMetadata(RenderGraphSnapshot& destination,
    const RenderGraphSnapshot& source, const std::unordered_map<int, int>& nodeIds,
    const std::string& graphId) {
    for (auto alias : source.authoredOutputAliases) {
        const auto mapped = nodeIds.find(alias.nodeId);
        if (mapped == nodeIds.end()) continue;
        alias.nodeId = mapped->second;
        destination.authoredOutputAliases.push_back(std::move(alias));
    }
    for (const auto& [nodeId, reason] : source.unavailableOutputs) {
        const auto mapped = nodeIds.find(nodeId);
        if (mapped != nodeIds.end()) destination.unavailableOutputs[mapped->second] = reason;
    }
    for (auto diagnostic : source.semanticDiagnostics) {
        diagnostic.authoredSourceIdentity = graphId + "/" + diagnostic.authoredSourceIdentity;
        diagnostic.affectedIdentity = graphId + "/" + diagnostic.affectedIdentity;
        diagnostic.semanticFingerprint = NodeMath::Sha256ContentIdentity(
            graphId + "/" + diagnostic.semanticFingerprint + "/" + diagnostic.message);
        destination.semanticDiagnostics.push_back(std::move(diagnostic));
    }
    destination.semanticFingerprint += ":" + graphId + ":" + source.semanticFingerprint;
}

} // namespace Stack::Project
