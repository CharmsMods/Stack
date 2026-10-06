#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include <unordered_set>

std::pair<int,int> RenderPipeline::ResolveGraphNativeExtent(
    const Stack::Renderer::GraphExecution::GraphExecutionContext& context,
    int nodeId, const std::string& socketId) const {
    std::unordered_set<std::string> visited;
    std::vector<std::pair<int,std::string>> pending{{nodeId,socketId}};
    while (!pending.empty()) {
        const auto endpoint = std::move(pending.back()); pending.pop_back();
        const auto key = std::to_string(endpoint.first) + "/" + endpoint.second;
        if (!visited.insert(key).second) continue;
        const auto found = context.nodes.find(endpoint.first);
        if (found == context.nodes.end()) continue;
        const auto& node = *found->second;
        if (node.nativeWidth > 0 && node.nativeHeight > 0) return {node.nativeWidth,node.nativeHeight};
        if (node.kind == RenderGraphNodeKind::RawDevelopment || node.kind == RenderGraphNodeKind::RawProjectSourceSet) {
            auto source = node.rawDevelopment.embeddedRawData;
            if (!source) {
                const auto stored = m_RawSharedSourceData.find(Stack::Renderer::RawDevelopmentCache::BuildSourceDataIdentity(node.rawDevelopment.recipe.source));
                if (stored != m_RawSharedSourceData.end()) source = stored->second;
            }
            if (source) {
                int width = Raw::DisplayWidth(source->metadata), height = Raw::DisplayHeight(source->metadata);
                if (std::abs(node.rawDevelopment.recipe.cropRotation.rotationDegrees) % 180 == 90) std::swap(width,height);
                return {width,height};
            }
        }
        const auto inputs = context.inputLinks.find(node.nodeId);
        if (inputs == context.inputLinks.end()) continue;
        // Primary image geometry wins over masks and scalar controls.
        const RenderGraphLink* primary = context.FindInputLink(node.nodeId,"imageIn");
        if (node.kind == RenderGraphNodeKind::RawOperation && endpoint.second == "measurementImageOut")
            if (const auto* reference = context.FindInputLink(node.nodeId,"referenceIn")) primary = reference;
        if (primary) { pending.push_back({primary->fromNodeId,primary->fromSocketId}); continue; }
        for (const auto& input : inputs->second) {
            const auto& link = *input.second;
            if (Stack::GraphModel::OutputDependsOnInput(node.outputDependencies,endpoint.second,link.toSocketId) &&
                link.toSocketId != "maskIn" && link.toSocketId != "factor" && link.toSocketId.rfind("param:",0) != 0)
                pending.push_back({link.fromNodeId,link.fromSocketId});
        }
    }
    return {m_Width,m_Height};
}
