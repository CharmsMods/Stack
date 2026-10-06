#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"

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
