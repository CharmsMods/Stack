#pragma once

#include "Persistence/RawProjectModel.h"
#include "Raw/RawDevelopmentRecipe.h"

#include <filesystem>
#include <string>

namespace Stack::Project {

struct RawProjectEditRecipeBinding {
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    std::string sourceSetId;
    std::string sourceLabel;
    std::string sourceKind = "single-raw";
    std::string viewTransformPlacement = "internal";
    bool multiFrameResult = false;
    bool hdrResult = false;
};

struct RawProjectEditRecipeUpdate {
    bool success = false;
    bool changed = false;
    bool preMergeChanged = false;
    bool postMergeChanged = false;
    std::string errorMessage;
};

// Resolves the single editable RAW recipe exposed by RAW Lab. Multi-frame MFD
// projects are assembled from their persisted pre-merge and post-merge recipe
// pieces here, rather than making each UI/copy/save path know those keys.
bool ResolveRawProjectEditRecipe(
    const RawProjectSnapshot& snapshot,
    const std::string& requestedSourceSetId,
    RawProjectEditRecipeBinding& binding,
    std::string* errorMessage = nullptr);

// Stores an edited RAW Lab recipe back into the authoritative snapshot. This
// performs the inverse MFD pre/post split and maintains the corresponding
// revision counters. The caller remains responsible for assigning the next
// project dirty revision and committing the snapshot transactionally.
RawProjectEditRecipeUpdate StoreRawProjectEditRecipe(
    RawProjectSnapshot& snapshot,
    const RawProjectEditRecipeBinding& binding,
    const Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe);

// Keeps compact single-RAW graph payload state synchronized with
// rawWorkspaceData when an offline/batch edit updates a snapshot.
bool StoreSingleRawRecipeInPipeline(
    nlohmann::json& pipelineData,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    std::string* errorMessage = nullptr);

// Rebinds a single-image recipe to the copy of its immutable managed asset in
// a newly-created directory bundle. Relocation is storage bookkeeping, so it
// does not advance the authored recipe revision.
bool RebaseSingleRawRecipeToManagedAsset(
    RawProjectSnapshot& snapshot,
    const std::filesystem::path& projectRoot,
    std::string* errorMessage = nullptr);

} // namespace Stack::Project
