#include "Project/RawLayerSourceTransactions.h"

namespace Stack::Project {
namespace {
RawProjectSnapshot SourceSettingsCandidate(const RawProjectSnapshot& source) {
    RawProjectSnapshot candidate;
    candidate.projectId = source.projectId;
    candidate.sourceSets = source.sourceSets;
    candidate.multiFrameGraph = source.multiFrameGraph;
    candidate.activeSourceSetId = source.activeSourceSetId;
    candidate.mfdInputRevision = source.mfdInputRevision;
    candidate.hdrInputRevision = source.hdrInputRevision;
    candidate.postRecipeRevision = source.postRecipeRevision;
    // The format tag selects source-only serialization. Graphs, images and
    // thumbnails are not copied just to prepare a source-settings edit.
    candidate.pipelineData["rawLayerStack"] = nlohmann::json::object();
    return candidate;
}

void CommitSourceSettings(RawProjectSnapshot& snapshot, RawProjectSnapshot candidate) {
    snapshot.sourceSets = std::move(candidate.sourceSets);
    snapshot.mfdInputRevision = candidate.mfdInputRevision;
    snapshot.hdrInputRevision = candidate.hdrInputRevision;
    snapshot.postRecipeRevision = candidate.postRecipeRevision;
}

bool RestoreDocument(RawLayerStackDocument& document, RawLayerHistoryAction action,
    RawRecipe::RawDevelopmentRecipe& source, const EditorNodeGraph::Graph* composition,
    int sourceNodeId, std::string& error) {
    if (action == RawLayerHistoryAction::CancelGesture) return document.CancelGesture(&source);
    return action == RawLayerHistoryAction::Undo
        ? document.Undo(&error,composition,sourceNodeId,&source)
        : document.Redo(&error,composition,sourceNodeId,&source);
}
}

RawLayerSourceUpdate ApplyMergedRawLayerSourceEdit(RawLayerStackDocument& document,
    RawProjectSnapshot& snapshot, const std::string& sourceSetId,
    const RawRecipe::RawDevelopmentRecipe& recipe, const EditorNodeGraph::Graph* composition,
    int sourceNodeId) {
    RawLayerSourceUpdate result;
    result.sourceSetId = sourceSetId;
    RawProjectEditRecipeBinding binding;
    if (!ResolveRawProjectEditRecipe(snapshot,sourceSetId,binding,&result.errorMessage) || !binding.multiFrameResult) {
        if (result.errorMessage.empty()) result.errorMessage = "The merged source is unavailable.";
        return result;
    }
    auto prepared = RawRecipe::BuildWorkspaceSourceRecipe(recipe);
    prepared.source = binding.recipe.source;
    auto candidate = SourceSettingsCandidate(snapshot);
    const auto update = StoreRawProjectEditRecipe(candidate,binding,prepared);
    static_cast<RawProjectEditRecipeUpdate&>(result) = update;
    if (!update.success || !update.changed) return result;
    if (!document.ApplySourceEdit(document.State(),binding.recipe,prepared,result.errorMessage,
            composition,sourceNodeId,sourceSetId)) {
        result.success = false;
        return result;
    }
    CommitSourceSettings(snapshot,std::move(candidate));
    return result;
}

RawLayerSourceUpdate RestoreRawLayerHistory(RawLayerStackDocument& document,
    RawLayerHistoryAction action, RawRecipe::RawDevelopmentRecipe& singleSource,
    RawProjectSnapshot* snapshot, const EditorNodeGraph::Graph* composition, int sourceNodeId) {
    if (action != RawLayerHistoryAction::CancelGesture) document.EndGesture();
    RawLayerSourceUpdate result;
    const auto* source = document.PendingSourceHistory(action);
    if (!source || source->sourceSetId.empty()) {
        const auto before = RawRecipe::SerializeWorkspaceSourceRecipe(singleSource);
        result.success = RestoreDocument(document,action,singleSource,composition,sourceNodeId,result.errorMessage);
        result.changed = result.success;
        result.postMergeChanged = before != RawRecipe::SerializeWorkspaceSourceRecipe(singleSource);
        return result;
    }
    result.sourceSetId = source->sourceSetId;
    if (!snapshot) { result.errorMessage = "The merged source project is unavailable."; return result; }
    RawProjectEditRecipeBinding binding;
    if (!ResolveRawProjectEditRecipe(*snapshot,source->sourceSetId,binding,&result.errorMessage) || !binding.multiFrameResult)
        return result;
    auto candidate = SourceSettingsCandidate(*snapshot);
    const auto update = StoreRawProjectEditRecipe(candidate,binding,source->recipe);
    static_cast<RawProjectEditRecipeUpdate&>(result) = update;
    if (!update.success) return result;
    if (!RestoreDocument(document,action,binding.recipe,composition,sourceNodeId,result.errorMessage)) {
        result.success = false;
        return result;
    }
    result.changed = true;
    CommitSourceSettings(*snapshot,std::move(candidate));
    return result;
}

} // namespace Stack::Project
