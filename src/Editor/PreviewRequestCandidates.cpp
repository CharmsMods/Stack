#include "Editor/PreviewRequestCandidates.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include <utility>

namespace Stack::Editor {
bool PreviewRequestCandidates::Refresh(const EditorNodeGraph::Graph& graph,
    const std::string& document, std::uint64_t documentRevision, std::uint64_t renderRevision) {
    const auto& nodes = graph.GetNodes();
    if (m_Graph == &graph && m_NodeData == nodes.data() && m_NodeCount == nodes.size() &&
        m_Document == document && m_StructureRevision == graph.GetStructureRevision() &&
        m_DocumentRevision == documentRevision && m_RenderRevision == renderRevision) return false;

    // Publish identity only after membership construction succeeds, so an
    // allocation failure cannot turn an incomplete list into a cache hit.
    std::vector<int> candidates;
    for (const auto& node : nodes) {
        if (node.kind == EditorNodeGraph::NodeKind::Preview ||
            node.kind == EditorNodeGraph::NodeKind::RawDetailAutoMask ||
            node.kind == EditorNodeGraph::NodeKind::Scope) candidates.push_back(node.id);
    }
    std::string candidateDocument = document;
    m_Nodes = std::move(candidates);
    m_Document = std::move(candidateDocument);
    m_Graph = &graph; m_NodeData = nodes.data(); m_NodeCount = nodes.size();
    m_StructureRevision = graph.GetStructureRevision();
    m_DocumentRevision = documentRevision; m_RenderRevision = renderRevision;
    ++m_Generation;
    return true;
}
} // namespace Stack::Editor
