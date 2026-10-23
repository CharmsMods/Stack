#include "Renderer/Internal/RenderPipelineGraphExecutionRuntime.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"

namespace Stack::Renderer::GraphExecution {

void GraphExecutionRuntime::PreparePhysicalSchedule() {
    std::unordered_set<std::string> materializedOutputs;
    for (const auto& root : evaluationSchedule.requestedOutputs)
        materializedOutputs.insert(MakeNodeSocketKey(root.nodeId, root.socketId));

    std::unordered_set<std::string> virtualOutputs;
    // Claim the largest downstream groups first. Previously each upstream
    // operation had already rendered by the time its consumer discovered a
    // fusion opportunity, so progressively larger prefixes were recomputed.
    for (auto output = evaluationSchedule.outputs.rbegin(); output != evaluationSchedule.outputs.rend(); ++output) {
        const auto key = MakeNodeSocketKey(output->nodeId, output->socketId);
        if (virtualOutputs.count(key)) continue;
        const auto found = nodes.find(output->nodeId);
        if (found == nodes.end() || !found->second) continue;
        const auto kind = found->second->kind;
        if (kind != RenderGraphNodeKind::DataMath && kind != RenderGraphNodeKind::TechnicalImage) continue;
        auto plan = pipeline.BuildPointwiseFusionPlan(executionContext, output->nodeId, output->socketId,
            pointwiseFusionDisabledNodes, materializedOutputs);
        if (!plan.valid) continue;
        for (const auto id : plan.authoredNodeIds)
            if (id != output->nodeId) virtualOutputs.insert(MakeNodeSocketKey(id, EditorNodeGraph::kImageOutputSocketId));
        pointwiseFusionPlans.emplace(key, std::move(plan));
    }
    physicalOutputs.reserve(evaluationSchedule.outputs.size());
    for (const auto& output : evaluationSchedule.outputs)
        if (!virtualOutputs.count(MakeNodeSocketKey(output.nodeId, output.socketId))) physicalOutputs.push_back(output);
}

} // namespace Stack::Renderer::GraphExecution
