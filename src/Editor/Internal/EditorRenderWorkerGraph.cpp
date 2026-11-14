#include "Editor/EditorRenderWorker.h"
#include "Editor/GraphRenderMemory.h"
#include "Renderer/GpuMemoryBudget.h"
#include "Renderer/GraphResourcePolicy.h"
#include "Renderer/Internal/RenderPipelineGraphSchedule.h"
#include "Renderer/LocalToneGuidePlanning.h"
#include "Renderer/SceneColorGuidePlanning.h"
#include <algorithm>
#include <unordered_set>

void EditorRenderWorker::PrepareGraphResourceBudget(const Snapshot& snapshot,
    RenderPipeline& pipeline, Result& result) {
    if (!snapshot.graphRequest.enabled && !result.viewportPreview.enabled) return;
    using namespace Stack::GraphRendering;
    using namespace Stack::Renderer::GraphExecution;
    std::vector<ScheduledGraphOutput> roots;
    for (const auto& output : snapshot.compositeOutputs)
        roots.push_back({output.outputNodeId, "imageOut"});
    for (const auto& preview : snapshot.previews)
        roots.push_back({preview.sourceNodeId, preview.sourceSocketId});
    if (snapshot.graph.rawLayerScopeNodeId > 0)
        roots.push_back({snapshot.graph.rawLayerScopeNodeId, snapshot.graph.rawLayerScopeSocketId});
    if (snapshot.graph.rawWorkspaceLocalRangeTargetSampleRequested ||
        (!snapshot.graph.rawWorkspaceLocalRangeOverlayMode.empty() &&
         snapshot.graph.rawWorkspaceLocalRangeOverlayMode != "none"))
        for (const auto& node : snapshot.graph.nodes)
            if (node.kind == RenderGraphNodeKind::RawDevelopment) roots.push_back({node.nodeId, "imageOut"});
    const auto schedule = BuildGraphEvaluationSchedule(snapshot.graph,
        {snapshot.graph.outputNodeId, snapshot.graph.outputSocketId}, roots);
    std::unordered_set<int> scheduledNodes;
    if (schedule.valid) for (const auto& output : schedule.outputs) scheduledNodes.insert(output.nodeId);
    Stack::Renderer::GraphResourcePolicy resources;
    int width = std::max(snapshot.width, snapshot.rawWorkspace.fullFrameWidth);
    int height = std::max(snapshot.height, snapshot.rawWorkspace.fullFrameHeight);
    for (const auto& node : snapshot.graph.nodes) {
        if (schedule.valid && !scheduledNodes.count(node.nodeId)) continue;
        resources.Include(node, snapshot.graph.rawLayerBackgroundNodeId > 0);
        width = std::max(width, node.image.width);
        height = std::max(height, node.image.height);
        if (node.kind == RenderGraphNodeKind::Reformat) {
            width = std::max(width, node.nativeWidth > 0 ? node.nativeWidth : node.reformatSettings.width);
            height = std::max(height, node.nativeHeight > 0 ? node.nativeHeight : node.reformatSettings.height);
        }
        if (node.kind == RenderGraphNodeKind::RawSource) {
            width = std::max(width, Raw::DisplayWidth(node.rawSource.metadata));
            height = std::max(height, Raw::DisplayHeight(node.rawSource.metadata));
        }
        if (node.kind == RenderGraphNodeKind::RawDevelopment && node.rawDevelopment.embeddedRawData) {
            width = std::max(width, Raw::DisplayWidth(node.rawDevelopment.embeddedRawData->metadata));
            height = std::max(height, Raw::DisplayHeight(node.rawDevelopment.embeddedRawData->metadata));
        }
    }
    for (const auto& output : snapshot.compositeOutputs) {
        width = std::max(width, output.width);
        height = std::max(height, output.height);
    }
    for (const auto& preview : snapshot.previews) {
        width = std::max(width, preview.width);
        height = std::max(height, preview.height);
    }
    // The executor can retain an image for each scheduled output until the
    // pass ends. Reserve that conservative peak plus ping/pong and publication,
    // rather than applying RAW's fixed seven-surface estimate to a branched graph.
    const auto surfaces = schedule.valid ? schedule.outputs.size() : snapshot.graph.nodes.size();
    const bool retainedFloat32 = pipeline.HasFloat32GraphDependencies();
    const bool rawStages = resources.rawStages;
    const auto targetBytes = [&](int w, int h, std::uint64_t count) {
        const auto bytes = ImageBytes(w, h, count);
        // RAW also retains its existing allowance for internal reconstruction.
        return rawStages || resources.TargetBytesPerPixel(retainedFloat32) == 16u ? AddBytes(bytes, bytes) : bytes;
    };
    std::uint64_t guideWorking=0,colorMaskWorking=0;
    const bool sourceColorGuide=resources.sourceColorGuide;
    for (const auto& node:snapshot.graph.nodes) if (!schedule.valid || scheduledNodes.count(node.nodeId)) {
        guideWorking=std::max(guideWorking,Stack::Renderer::LocalToneGuideWorkingBytes(node,width,height));
        if(sourceColorGuide && Stack::Renderer::NodeMayUseColorMaskGuide(node))
            colorMaskWorking=std::max(colorMaskWorking,Stack::Renderer::SceneColorMaskGuideWorkingBytes(width,height));
    }
    guideWorking=AddBytes(guideWorking,colorMaskWorking);
    const auto completeWorking = AddBytes(targetBytes(width, height, AddBytes(surfaces, rawStages ? 7 : 3)),guideWorking);
    std::uint64_t working = completeWorking;
    if (result.viewportPreview.enabled) {
        const auto mainPlan = RenderTiling::PlanGraphViewport(snapshot.graph,
            snapshot.width, snapshot.height, snapshot.previewMaxDimension, result.viewportPreview, {}, retainedFloat32);
        if (mainPlan.valid) {
            working = mainPlan.workingBytes;
            std::uint64_t publishedMain = 0;
            const auto output = mainPlan.stages.find(mainPlan.output);
            if (output != mainPlan.stages.end()) {
                const auto& stage = output->second;
                const auto resident = Stack::Renderer::ScaleImageRegion(stage.demand,
                    stage.renderWidth, stage.renderHeight);
                publishedMain = targetBytes(resident.width, resident.height, 1);
            }
            // Complete auxiliary evaluations run after publication and may
            // keep the main immutable texture alive while using full inputs.
            if (!snapshot.previews.empty() || !snapshot.developCandidateRenders.empty())
                working = std::max(working, AddBytes(completeWorking, publishedMain));
            if (snapshot.prepareViewportOverview) {
                auto overview = result.viewportPreview;
                overview.visible = {};
                overview.empty = false;
                const auto overviewPlan = RenderTiling::PlanGraphViewport(snapshot.graph,
                    snapshot.width, snapshot.height, 512, overview, {}, retainedFloat32);
                const auto overviewWorking = overviewPlan.valid ? overviewPlan.workingBytes : completeWorking;
                working = std::max(working, AddBytes(overviewWorking, publishedMain));
            }
        }
    }
    const auto memory = Stack::Renderer::QueryGpuMemoryBudget();
    const auto budget = Raw::ResolveRawGpuMemoryBudget(memory);
    const auto resident = pipeline.GetGraphResidentCacheBytes();
    // Device usage already includes accepted/queued presentation textures and
    // other pipelines. Only this pipeline's evictable caches are added back.
    const auto allowance = CacheAllowance(memory.queryAvailable,
        budget.workingBudgetBytes, resident, working);
    pipeline.SetGraphCacheBudget(allowance, snapshot.rawWorkspace.minimumRawStageCacheBytes, rawStages);
    result.telemetry.fullFrameEstimatedWorkingSetBytes = working;
}
