#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace EditorNodeGraph { class Graph; struct Node; }
namespace Stack::Editor {

// Membership is derived from the owning document. Eligibility, completion and
// retry state remain in the editor scheduler and are checked on every request.
class PreviewRequestCandidates {
public:
    bool Refresh(const EditorNodeGraph::Graph& graph, const std::string& document,
        std::uint64_t documentRevision, std::uint64_t renderRevision);
    const std::vector<int>& Nodes() const { return m_Nodes; }
    std::uint64_t Generation() const { return m_Generation; }
private:
    const EditorNodeGraph::Graph* m_Graph = nullptr;
    const EditorNodeGraph::Node* m_NodeData = nullptr;
    std::size_t m_NodeCount = 0;
    std::string m_Document;
    std::uint64_t m_StructureRevision = 0, m_DocumentRevision = 0, m_RenderRevision = 0;
    std::uint64_t m_Generation = 0;
    std::vector<int> m_Nodes;
};
} // namespace Stack::Editor
