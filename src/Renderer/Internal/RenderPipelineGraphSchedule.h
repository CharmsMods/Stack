#pragma once

#include "Renderer/MaskRenderTypes.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Stack::Renderer::GraphExecution {

// Immutable lookup data bound to one exact RenderGraphSnapshot instance.
// The render graph's node/link vectors must not be structurally modified while
// this index is in use. Output selection and node payload values may change.
struct GraphTopologyIndex {
    using NodeLookup =
        std::unordered_map<int, const RenderGraphNode*>;
    using InputLinkLookup =
        std::unordered_map<
            int,
            std::unordered_map<std::string_view, const RenderGraphLink*>>;
    using InputsByNodeLookup =
        std::unordered_map<int, std::vector<const RenderGraphLink*>>;
    using OutputUseCountLookup =
        std::unordered_map<std::string, int>;

    bool IsBoundTo(const RenderGraphSnapshot& graph) const noexcept;

    const RenderGraphSnapshot* graph = nullptr;
    const RenderGraphNode* nodeData = nullptr;
    const RenderGraphLink* linkData = nullptr;
    std::size_t nodeCount = 0;
    std::size_t linkCount = 0;
    bool valid = false;
    std::string error;
    NodeLookup nodes;
    InputLinkLookup inputLinks;
    InputsByNodeLookup inputsByNode;
    OutputUseCountLookup outputUseCounts;
};

struct ScheduledGraphOutput {
    int nodeId = -1;
    std::string socketId;
};

struct GraphEvaluationSchedule {
    bool valid = false;
    std::string error;
    std::vector<ScheduledGraphOutput> outputs;
};

GraphTopologyIndex BuildGraphTopologyIndex(
    const RenderGraphSnapshot& graph);

GraphEvaluationSchedule BuildGraphEvaluationSchedule(
    const RenderGraphSnapshot& graph,
    const ScheduledGraphOutput& root,
    const std::vector<ScheduledGraphOutput>& extraRoots = {});

GraphEvaluationSchedule BuildGraphEvaluationSchedule(
    const RenderGraphSnapshot& graph,
    const GraphTopologyIndex& topology,
    const ScheduledGraphOutput& root,
    const std::vector<ScheduledGraphOutput>& extraRoots = {});

} // namespace Stack::Renderer::GraphExecution
