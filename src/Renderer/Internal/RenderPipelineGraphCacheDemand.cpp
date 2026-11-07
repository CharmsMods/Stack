#include "Renderer/Internal/RenderPipelineGraphExecutionRuntime.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"

namespace Stack::Renderer::GraphExecution {

void GraphExecutionRuntime::PruneCachedDependencies() {
    if (pipeline.m_RawDevelopmentCachePrewarmActive) return;

    // Fingerprint only branches whose identity is independent of the mutable
    // execution canvas. Legacy extent-dependent operations retain their normal
    // topological evaluation; guessing their dimensions here could reuse pixels
    // from another raster. This is preparation per request, never per UI frame.
    std::unordered_set<std::string> stable, cached;
    for (const auto& output : evaluationSchedule.outputs) {
        const auto& node = *nodes.at(output.nodeId);
        using K = RenderGraphNodeKind;
        bool independent = false;
        switch (node.kind) {
        case K::Image:
        case K::RawDevelopment:
        case K::RawProjectSourceSet:
        case K::RawOperation:
        case K::Layer:
        case K::Reformat:
        case K::CustomMask:
        case K::ConstantChannel:
        case K::Value:
            independent = true;
            break;
        case K::TechnicalImage:
            independent = findInputLink(node.nodeId, "imageIn") != nullptr;
            break;
        case K::DataMath:
            independent = IsUniformRenderSocket(executionContext, node.nodeId, output.socketId);
            break;
        default: break;
        }
        if (!independent || graph.unavailableOutputs.count(node.nodeId)) continue;
        const auto inputs = topology.inputsByNode.find(node.nodeId);
        if (inputs != topology.inputsByNode.end()) for (const auto* input : inputs->second) {
            // Some legacy identities hash even inactive bindings. Require all
            // linked identities to be safe before precomputing this one.
            if (!stable.count(MakeNodeSocketKey(input->fromNodeId, input->fromSocketId))) {
                independent = false;
                break;
            }
        }
        if (!independent) continue;
        const auto key = MakeNodeSocketKey(node.nodeId, output.socketId);
        stable.insert(key);
        if (IsUniformRenderSocket(executionContext, node.nodeId, output.socketId)) {
            const auto fingerprint = fingerprintScalar(node.nodeId, output.socketId);
            const auto entry = pipeline.m_GraphScalarCache.find(key);
            if (entry != pipeline.m_GraphScalarCache.end() &&
                entry->second.fingerprint == fingerprint && !entry->second.denoisePending)
                cached.insert(key);
            continue;
        }
        const bool mask = node.kind == K::CustomMask || node.kind == K::ConstantChannel;
        const auto fingerprint = mask ? fingerprintMask(node.nodeId, output.socketId)
                                      : fingerprintImage(node.nodeId, output.socketId);
        // RAW diagnostics must execute even if a later image is already cached.
        if (node.kind == K::RawDevelopment && rawDevelopmentSideEffectsRequested) continue;
        // inputImageOut is a borrowed passthrough and has no independent owner.
        if (node.kind == K::RawOperation && output.socketId == "inputImageOut") continue;
        auto* entry = pipeline.FindGraphCacheEntry(mask ? pipeline.m_GraphMaskCache : pipeline.m_GraphImageCache,
            key, fingerprint);
        if (entry && entry->owned && !entry->denoisePending) cached.insert(key);
    }
    if (cached.empty()) return;

    // An independent legacy branch may inherit the preceding stage's canvas.
    // Keep the entire physical order when such a branch exists: removing a
    // sibling could otherwise change its implicit extent. Image Output is a
    // pure alias whose input restores its extent and is safe to keep as a root.
    for (const auto& output : evaluationSchedule.outputs) {
        if (stable.count(MakeNodeSocketKey(output.nodeId, output.socketId))) continue;
        const auto& node = *nodes.at(output.nodeId);
        const auto* input = findInputLink(output.nodeId, "imageIn");
        if (node.kind == RenderGraphNodeKind::Output && input &&
            !IsScalarRenderSocket(executionContext, input->fromNodeId, input->fromSocketId)) continue;
        return;
    }

    std::vector<ScheduledGraphOutput> pending = evaluationSchedule.requestedOutputs;
    if (referenceSourceNodeId > 0) pending.push_back({referenceSourceNodeId, "imageOut"});
    std::unordered_set<std::string> demanded;
    while (!pending.empty()) {
        auto output = std::move(pending.back());
        pending.pop_back();
        const auto key = MakeNodeSocketKey(output.nodeId, output.socketId);
        if (!demanded.insert(key).second || cached.count(key)) continue;
        const auto fused = pointwiseFusionPlans.find(key);
        if (fused != pointwiseFusionPlans.end()) {
            pending.push_back({fused->second.inputNodeId, fused->second.inputSocketId});
            continue;
        }
        const auto found = nodes.find(output.nodeId);
        const auto inputs = topology.inputsByNode.find(output.nodeId);
        if (found == nodes.end() || inputs == topology.inputsByNode.end()) continue;
        for (const auto* input : inputs->second)
            if (Stack::GraphModel::OutputDependsOnInput(found->second->outputDependencies,
                    output.socketId, input->toSocketId))
                pending.push_back({input->fromNodeId, input->fromSocketId});
    }
    const auto previous = physicalOutputs.size();
    physicalOutputs.erase(std::remove_if(physicalOutputs.begin(), physicalOutputs.end(),
        [&](const auto& output) { return !demanded.count(MakeNodeSocketKey(output.nodeId, output.socketId)); }),
        physicalOutputs.end());
    pipeline.m_LastGraphExecutionStats.cachePrunedOutputs = int(previous - physicalOutputs.size());
}

} // namespace Stack::Renderer::GraphExecution
