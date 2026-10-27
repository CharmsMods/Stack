#include "BracketingDetail.h"

namespace Stack::Project {
namespace {
bool ResolveDetail(const BracketingState& state, std::uint64_t inputRevision,
    BracketingDetailMode& mode, Raw::Bracketing::BracketingRecipe& recipe,
    std::string& error) {
    if (!state.result || !state.result->raw) {
        error = "No completed bracket is available for native detail.";
        return false;
    }
    if (!Raw::Bracketing::Deserialize(nlohmann::json::parse(state.completedRecipe, nullptr, false),
        recipe, error)) return false;
    mode = BracketingDetailMode::Completed;
    const auto& raw = *state.result->raw;
    // An engine upgrade may attach newly prepared sources to an older result.
    // Preparation alone is not an edit, nor an instruction to replace its pixels.
    const bool changed = inputRevision != state.completedRevision ||
        state.storedRecipe != state.completedRecipe;
    if (changed && state.HasInteractivePreview() && state.result->analysis &&
        !state.result->panorama && !raw.reconstructedCameraRgb &&
        raw.metadata.pixelLayout == Raw::RawPixelLayout::MosaicBayer &&
        recipe.reconstruction == Raw::Bracketing::ReconstructionMode::Standard &&
        state.recipe.reconstruction == Raw::Bracketing::ReconstructionMode::Standard &&
        !BracketingInputsChanged(recipe, state.recipe)) {
        mode = BracketingDetailMode::Proposed;
        recipe = state.recipe;
    }
    return true;
}
} // namespace

bool CaptureBracketingDetail(const BracketingState& state, std::uint64_t inputRevision,
    BracketingState::DetailJob& job, Raw::Bracketing::BracketingRecipe& recipe,
    std::string& error) {
    if (!ResolveDetail(state, inputRevision, job.mode, recipe, error)) return false;
    job.source = state.result;
    job.inputRevision = inputRevision;
    job.completedRevision = state.completedRevision;
    job.previewRevision = state.previewRevision;
    job.recipeIdentity = state.storedRecipe;
    job.completedRecipeIdentity = state.completedRecipe;
    return true;
}

bool CanAdoptBracketingDetail(const BracketingState& state,
    const BracketingState::DetailJob& job, std::uint64_t inputRevision) {
    if (job.canceled || job.source != state.result || job.inputRevision != inputRevision ||
        job.completedRevision != state.completedRevision || job.previewRevision != state.previewRevision ||
        job.recipeIdentity != state.storedRecipe || job.completedRecipeIdentity != state.completedRecipe)
        return false;
    BracketingDetailMode mode;
    Raw::Bracketing::BracketingRecipe recipe;
    std::string error;
    return ResolveDetail(state, inputRevision, mode, recipe, error) && mode == job.mode;
}

} // namespace Stack::Project
