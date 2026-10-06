#pragma once

#include "Project/RawLayerStack.h"
#include "Persistence/RawProjectEditPipeline.h"

namespace Stack::Project {

struct RawLayerSourceUpdate : RawProjectEditRecipeUpdate {
    std::string sourceSetId;
};

// These transactions prepare source settings separately from embedded assets
// and commit source settings together with the layer document's history.
RawLayerSourceUpdate ApplyMergedRawLayerSourceEdit(RawLayerStackDocument& document,
    RawProjectSnapshot& snapshot, const std::string& sourceSetId,
    const RawRecipe::RawDevelopmentRecipe& recipe, const EditorNodeGraph::Graph* composition,
    int sourceNodeId);

RawLayerSourceUpdate RestoreRawLayerHistory(RawLayerStackDocument& document,
    RawLayerHistoryAction action, RawRecipe::RawDevelopmentRecipe& singleSource,
    RawProjectSnapshot* snapshot, const EditorNodeGraph::Graph* composition, int sourceNodeId);

} // namespace Stack::Project
