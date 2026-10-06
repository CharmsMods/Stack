#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include "Graph/OutputDependencies.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Stack::GraphModel {

struct GraphView {
    std::string id;
    const EditorNodeGraph::Graph* graph = nullptr;
};

struct DependencyEdge { Endpoint source; Endpoint destination; };

struct DocumentView {
    std::vector<GraphView> graphs;
    // Host-owned source, publication and composition dependencies. No editor
    // or active-project lookup is permitted during analysis.
    std::vector<DependencyEdge> dependencies;
    // Value aliases differ from dependencies: a blend depends on its mask,
    // but carries the image descriptor, never the mask's descriptor.
    std::vector<DependencyEdge> valueBindings;
    std::vector<std::pair<Endpoint, Stack::NodeMath::ValueDescriptor>> sources;
};

struct GraphAnalysis {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<Endpoint> cycle;
    struct Draft { Endpoint input; std::string message; };
    std::vector<Draft> drafts;
};

struct EditProposal {
    std::uint64_t sourceRevision = 0;
    EditorNodeGraph::Graph candidate;
    GraphAnalysis analysis;
    std::vector<Endpoint> affectedOutputs;
};

GraphAnalysis AnalyzeDocument(const DocumentView& document);
EditProposal ProposeEdit(const EditorNodeGraph::Graph& source,
    std::uint64_t revision,
    const std::function<void(EditorNodeGraph::Graph&)>& edit);
EditProposal ProposeInsertion(const EditorNodeGraph::Graph& source, std::uint64_t revision,
    int nodeId, const EditorNodeGraph::Link& link);
EditProposal ProposeSerialMove(const EditorNodeGraph::Graph& source, std::uint64_t revision, int nodeId, bool forward);
bool ApplyEdit(EditorNodeGraph::Graph& graph, std::uint64_t revision,
    EditProposal proposal, std::string& error);

// Call only after validation, immediately before replacing the live graph.
// Keep unchanged embedded allocations with their authored node identities.
void RetainImageStorage(EditorNodeGraph::Graph& candidate, EditorNodeGraph::Graph& previous);

// A dragged endpoint belongs to graph. A missing endpoint is general Add,
// which permits independent nodes with currently unconnected inputs.
std::vector<EditorNodeGraphDefinitions::NodeCatalogEntry> QueryAvailableNodes(
    const EditorNodeGraph::Graph& graph,
    const std::optional<std::pair<int, std::string>>& dragged = std::nullopt,
    EditorNodeGraphDefinitions::LiveGraphRole role = EditorNodeGraphDefinitions::LiveGraphRole::Composition,
    const std::function<bool(const EditorNodeGraph::Graph&)>& accepts = {});

} // namespace Stack::GraphModel
