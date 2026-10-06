#pragma once
#include "Persistence/RawProjectEditPipeline.h"

namespace Stack::EditorRendering {

inline bool ReadMergedRenderingRecipe(const Stack::Project::RawProjectSnapshot& snapshot,
    const std::string& sourceIdentity, std::uint64_t sourceHash,
    Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    Stack::Project::RawProjectEditRecipeBinding binding;
    if (!Stack::Project::ResolveRawProjectEditRecipe(snapshot,snapshot.activeSourceSetId,binding)) return false;
    recipe = Stack::RawRecipe::BuildWorkspaceSourceRecipe(binding.recipe);
    recipe.source.sourcePath = sourceIdentity;
    recipe.source.relativePathKey = sourceIdentity;
    recipe.source.fingerprint = std::to_string(sourceHash);
    return true;
}

} // namespace Stack::EditorRendering
