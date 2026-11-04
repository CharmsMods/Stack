#pragma once
#include "Renderer/MaskRenderTypes.h"
#include "Renderer/SceneColorGuidance.h"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace Stack::Renderer {
inline bool GraphHasSourceColorGuide(const RenderGraphSnapshot& graph) {
    return std::any_of(graph.nodes.begin(),graph.nodes.end(),[](const auto& node) {
        return node.rawDevelopment.embeddedRawData && bool(node.rawDevelopment.embeddedRawData->sceneColorGuide);
    });
}
inline bool NodeMayUseColorMaskGuide(const RenderGraphNode& node) {
    if(node.kind==RenderGraphNodeKind::MaskGenerator)return node.maskKind==RenderMaskGeneratorKind::PaintedArea;
    if(node.kind==RenderGraphNodeKind::RawOperation)
        return node.rawOperation.enabled && node.rawOperation.kind==RawRecipe::GraphOperationKind::LocalEv;
    return (node.kind==RenderGraphNodeKind::RawDevelopment || node.kind==RenderGraphNodeKind::RawProjectSourceSet) &&
        !node.rawDevelopment.recipe.localRange.areas.empty();
}
inline std::uint64_t SceneColorMaskGuideWorkingBytes(int width,int height) {
    if(width<=0 || height<=0)return 0;
    // Existing native guide/mask memory remains in ordinary RAW admission.
    // Additional optional confidence is 4 bytes/native pixel; a transactional
    // rebuild retains the accepted field until its replacement is ready.
    // Reserve both. Compact agreement
    // work, readback strips and preview confidence together stay below 24 MiB.
    const auto pixels=std::uint64_t(width)*height;
    constexpr auto fixed=24ull*1024*1024+kMaximumPreparedSceneColorGuideBytes;
    return pixels>(std::numeric_limits<std::uint64_t>::max()-fixed)/8
        ? std::numeric_limits<std::uint64_t>::max() : pixels*8+fixed;
}
inline std::uint64_t SceneColorMaskGuideWorkingBytes(const RenderGraphSnapshot& graph,int width,int height) {
    return GraphHasSourceColorGuide(graph) && std::any_of(graph.nodes.begin(),graph.nodes.end(),NodeMayUseColorMaskGuide)
        ? SceneColorMaskGuideWorkingBytes(width,height) : 0;
}
}
