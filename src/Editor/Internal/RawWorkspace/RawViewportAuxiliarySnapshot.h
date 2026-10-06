#pragma once
#include "Editor/EditorRenderWorker.h"
#include "Editor/RawRenderGraphOverlay.h"

namespace Raw {
inline void ConfigureViewportAuxiliarySnapshot(EditorRenderWorker::Snapshot& snapshot,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe, RawRenderPurpose purpose, int edge) {
    snapshot.rawRenderPurpose = purpose;
    snapshot.schedulingSerial = snapshot.generation;
    snapshot.previewMaxDimension = edge;
    snapshot.width = snapshot.rawWorkspace.fullFrameWidth;
    snapshot.height = snapshot.rawWorkspace.fullFrameHeight;
    snapshot.telemetry.interactionActive = false;
    snapshot.rawWorkspace.recipe = recipe;
    snapshot.rawWorkspace.viewport = {};
    snapshot.rawWorkspace.analysisRequested = false;
    snapshot.rawWorkspace.graphScopeStage = RawDevelopmentGraphScopeStage::None;
    snapshot.rawWorkspace.gradingScopeSource = RawDevelopmentGradingScopeSource::None;
    snapshot.rawWorkspace.preciseSolveRequest.reset();
    snapshot.rawWorkspace.cachePrewarmStage.reset();
    snapshot.rawWorkspace.localRangeTargetSampleRequested = false;
    snapshot.rawWorkspace.localRangeTargetPreview = {};
    snapshot.rawWorkspace.localRangeOverlayMode = "none";
    snapshot.rawWorkspace.startPointCandidateRenderRequests.clear();
    snapshot.graph.rawWorkspaceLocalRangeOverlayMode = "none";
    snapshot.graph.rawWorkspaceLocalRangeTargetSampleRequested = false;
    snapshot.graph.rawWorkspaceLocalRangeTargetPreview = {};
    snapshot.developCandidateRenders.clear();
    snapshot.compositeOutputs.clear();
    snapshot.previews.clear();
    Stack::EditorRendering::ApplyRawRecipeOverlayToRenderGraph(snapshot.graph, recipe,
        snapshot.rawWorkspace.managedRawDecodeNodeId, snapshot.rawWorkspace.managedToneCurveNodeId,
        snapshot.rawWorkspace.managedViewTransformNodeId);
}
}
