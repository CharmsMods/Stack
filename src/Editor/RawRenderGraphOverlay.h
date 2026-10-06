#pragma once

#include "Raw/RawDevelopmentRecipe.h"
#include "Renderer/MaskRenderTypes.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"

namespace Stack::EditorRendering {

inline std::size_t BuildRawPresentationFingerprint(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const RenderGraphSnapshot& graph) {
    using namespace Stack::Renderer::RawDevelopmentCache;
    auto fingerprint = BuildStageFingerprints(recipe, 0).postOutputCrop;
    if (graph.rawLayerBackgroundNodeId > 0) {
        // Layer edits leave the Background recipe unchanged. Lowering adds
        // their processing revision to this identity, independent of layout or preview
        // resolution, so retained native pixels match the complete image.
        HashTypedValue(fingerprint, graph.semanticFingerprint);
        HashTypedValue(fingerprint, graph.outputNodeId);
        HashTypedValue(fingerprint, graph.outputSocketId);
    }
    return fingerprint;
}

inline void ApplyRawRecipeOverlayToRenderGraph(
    RenderGraphSnapshot& graph,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    int managedRawDecodeNodeId,
    int managedToneCurveNodeId,
    int managedViewTransformNodeId) {
    for (RenderGraphNode& node : graph.nodes) {
        if (graph.rawLayerBackgroundNodeId > 0) {
            if (node.nodeId == graph.rawLayerBackgroundNodeId) {
                node.rawDevelopment.recipe = Stack::RawRecipe::BuildTechnicalSourceRecipe(recipe);
            } else if (node.nodeId == graph.rawLayerViewNodeId) {
                node.layerJson = recipe.viewTransform.layerJson;
                node.layerJson["type"] = "ViewTransform";
                node.layerJson["inputWorkingSpace"] = Stack::RawRecipe::WorkingSpaceStableString(recipe.technical.workingSpace);
            }
            continue;
        }
        if (node.kind == RenderGraphNodeKind::RawDevelopment ||
            node.kind == RenderGraphNodeKind::RawProjectSourceSet) {
            node.rawDevelopment.recipe = recipe;
        }
        if (node.nodeId == managedRawDecodeNodeId &&
            node.kind == RenderGraphNodeKind::RawDecode) {
            node.rawDecode.settings =
                Stack::RawRecipe::ToRawDevelopSettings(recipe);
        } else if (node.nodeId == managedToneCurveNodeId &&
                   node.kind == RenderGraphNodeKind::Layer) {
            node.layerJson = recipe.finishTone.layerJson;
        } else if (node.nodeId == managedViewTransformNodeId &&
                   node.kind == RenderGraphNodeKind::Layer) {
            node.layerJson = recipe.viewTransform.layerJson;
        }
    }
}

} // namespace Stack::EditorRendering
