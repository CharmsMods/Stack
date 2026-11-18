#include "Editor/EditorRenderWorker.h"
#include "Editor/GraphRenderMemory.h"
#include "Renderer/GpuMemoryBudget.h"
#include "Renderer/GraphResourcePolicy.h"
#include "Renderer/Internal/RenderPipelineGraphSchedule.h"
#include "Renderer/LocalToneGuidePlanning.h"
#include "Renderer/SceneColorGuidePlanning.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <stdexcept>

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
    bool completeProcessingBoundary = false;
    if (result.viewportPreview.enabled) {
        const auto mainPlan = RenderTiling::PlanGraphViewport(snapshot.graph,
            snapshot.width, snapshot.height, snapshot.previewMaxDimension, result.viewportPreview, {}, retainedFloat32);
        if (mainPlan.valid) {
            working = mainPlan.workingBytes;
            if (snapshot.viewportSections) {
                for (const auto& [endpoint,stage] : mainPlan.stages) {
                    if (stage.regionalExecutable) continue;
                    const auto node=std::find_if(snapshot.graph.nodes.begin(),snapshot.graph.nodes.end(),
                        [&](const auto& item) {return item.nodeId==endpoint.nodeId;});
                    if (node!=snapshot.graph.nodes.end() && node->kind==RenderGraphNodeKind::RawOperation)
                        completeProcessingBoundary=true;
                }
            }
            std::uint64_t publishedMain = 0;
            std::uint64_t overviewCopy = 0;
            const auto output = mainPlan.stages.find(mainPlan.output);
            if (output != mainPlan.stages.end()) {
                const auto& stage = output->second;
                const auto resident = Stack::Renderer::ScaleImageRegion(stage.demand,
                    stage.renderWidth, stage.renderHeight);
                publishedMain = targetBytes(resident.width, resident.height, 1);
                if (snapshot.prepareViewportOverview && !resident.Partial()) {
                    const double scale = std::min(1.0, 512.0 / std::max(resident.width,resident.height));
                    // PrepareViewportOverview only copies an existing whole
                    // raster. Regional output never evaluates a hidden graph.
                    // Reserve both presentation renditions conservatively.
                    overviewCopy = targetBytes(std::max(1,int(std::lround(resident.width*scale))),
                        std::max(1,int(std::lround(resident.height*scale))),2);
                }
            }
            // Complete auxiliary evaluations run after publication and may
            // keep the main immutable texture alive while using full inputs.
            if (!snapshot.previews.empty() || !snapshot.developCandidateRenders.empty())
                working = std::max(working, AddBytes(completeWorking, publishedMain));
            working = AddBytes(working,overviewCopy);
        }
    }
    // Published output is immutable and independently leased. Retire the
    // previous execution's output/comparison borrows before cache admission,
    // including ordinary editing frames. Otherwise their obsolete protection
    // can prevent reclaiming the very caches counted as available below.
    pipeline.ClearOutput();
    const auto memory = Stack::Renderer::QueryGpuMemoryBudget();
    const auto budget = Raw::ResolveRawGpuMemoryBudget(memory);
    const auto resident = pipeline.GetGraphResidentCacheBytes();
    const bool budgetKnown = !budget.usedFallback;
    const auto available = budgetKnown ? AddBytes(budget.workingBudgetBytes, resident)
                                      : budget.workingBudgetBytes;
    if (snapshot.rawRenderPurpose == RawRenderPurpose::ViewportRefinement &&
        result.viewportPreview.enabled && working > available) {
        // The unmeasured allowance is total admitted work, not free memory;
        // never increase it by adding existing cache residency back.
        result.telemetry.skipReason = RawPreviewSkipReason::MemoryBudget;
        result.telemetry.fullFrameEstimatedWorkingSetBytes = working;
        result.telemetry.fullFrameRefinementBudgetAllowed = false;
        throw std::runtime_error(budgetKnown
            ? "Viewport refinement working set exceeds GPU headroom including reclaimable graph caches."
            : "Viewport refinement needs more than the 512 MiB conservative GPU allowance; device memory headroom is unavailable.");
    }
    // Device usage already includes accepted/queued presentation textures and
    // other pipelines. Only this pipeline's evictable caches are added back.
    // Repeated native sections share complete-input boundaries. The ordinary
    // 1.25 GiB cap splits into RAW/image pools too small for even one 50 MP
    // float32 boundary, causing whole-image processing for every section.
    // Permit a larger cache only from headroom left AFTER the conservative
    // active working set. Low-memory devices retain the same admission limit.
    const auto ceiling = snapshot.viewportSections && budgetKnown ?
        3ull*1024*1024*1024 : 1280ull*1024*1024;
    const auto allowance = CacheAllowance(budgetKnown,
        budget.workingBudgetBytes, resident, working, ceiling);
    // When a complete downstream operation is required, its finished image
    // is the reusable boundary for subsequent sections. Giving half the small
    // remaining allowance to earlier RAW stages can evict that boundary every
    // time. Prefer the downstream cache; a hit skips those RAW stages too.
    pipeline.SetGraphCacheBudget(allowance,
        completeProcessingBoundary ? 0 : snapshot.rawWorkspace.minimumRawStageCacheBytes,
        rawStages && !completeProcessingBoundary);
    result.telemetry.fullFrameEstimatedWorkingSetBytes = working;
}
