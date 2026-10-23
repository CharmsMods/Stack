#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Renderer/MaskRenderTypes.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Stack::Project::GraphSnapshotInternal {

// Immutable indices used while lowering an authored graph into a render
// snapshot. They avoid repeated full-vector searches without placing pointer
// caches on the mutable editor graph itself.
class Lookup {
public:
    explicit Lookup(const EditorNodeGraph::Graph& graph);

    const EditorNodeGraph::Node* FindNode(int nodeId) const;
    bool IsAnalysisLink(const EditorNodeGraph::Link& link) const;
    bool TryResolveUniformScalarInput(
        int nodeId,
        const std::string& socketId,
        double& value) const;

private:
    std::unordered_map<int, const EditorNodeGraph::Node*> m_NodeById;
    std::unordered_map<int, std::vector<const EditorNodeGraph::Link*>>
        m_InputLinksByNode;
};

} // namespace Stack::Project::GraphSnapshotInternal

namespace Stack::Project {

// Resolve the complete endpoint: public compound ports need not lower to an
// imageOut socket, and authored integer IDs are not execution identities.
std::optional<RenderGraphSnapshot::OutputAlias> ResolveGraphSnapshotOutput(
    const RenderGraphSnapshot& snapshot, const GraphModel::Endpoint& authored);

// Composition remaps metadata together with executable node identities. The
// caller owns node/link routing; this operation preserves aliases and failures.
void AppendGraphSnapshotMetadata(RenderGraphSnapshot& destination,
    const RenderGraphSnapshot& source, const std::unordered_map<int, int>& nodeIds,
    const std::string& graphId);

} // namespace Stack::Project
