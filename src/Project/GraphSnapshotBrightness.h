#pragma once
#include "Project/GraphSnapshotBuilder.h"

namespace Stack::Project::GraphSnapshotInternal {
bool LowerBrightnessRecipes(const EditorNodeGraph::Graph& sourceGraph, const GraphSnapshotInputs& inputs,
                            const Timeline::FrameEvaluationContext& frameContext,
                            EditorNodeGraph::Graph& brightnessGraph,
                            EditorNodeGraph::CompoundExpansionResult& expansion,
                            EditorNodeGraph::GraphOutputDescriptions& authoredWireIssues,
                            EditorNodeGraph::GraphOutputDescriptions& blockedResults, int& resolvedStageId);
}
