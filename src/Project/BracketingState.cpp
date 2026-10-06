#include "BracketingState.h"
#include <algorithm>

namespace Stack::Project {

BracketingState::~BracketingState() { CancelJobs(); }

void BracketingState::CancelJobs() {
    if (job) job->canceled = true;
    if (preparationJob) preparationJob->canceled = true;
    if (detailJob) detailJob->canceled = true;
    if (inspectionJob) inspectionJob->canceled = true;
}

bool BracketingInputsChanged(const Raw::Bracketing::BracketingRecipe& before,
    const Raw::Bracketing::BracketingRecipe& after) {
    auto left = Raw::Bracketing::Serialize(before), right = Raw::Bracketing::Serialize(after);
    // Curves and display names reuse prepared measurements.
    for (auto* value : {&left, &right}) {
        value->erase("knots");
        value->erase("automatic");
        for (auto& group : (*value)["groups"]) {
            group.erase("name");
            group.erase("colorSlot");
        }
    }
    return left != right;
}

BracketingEditOutcome CommitBracketingEdit(BracketingState& state,
    RawProjectSnapshot* document, bool interactionActive, double now) {
    BracketingEditOutcome outcome;
    for (const auto& source : state.sources) state.sourceHistory[source.frameId] = source;
    Raw::Bracketing::ConstrainEnabledCurves(state.recipe);
    const auto& before = state.editedRecipe.empty() ? state.storedRecipe : state.editedRecipe;
    const auto edited = Raw::Bracketing::Serialize(state.recipe).dump();
    if (edited == before) return outcome;
    outcome.changed = true;
    Raw::Bracketing::BracketingRecipe previous;
    std::string error;
    bool inputs = state.newProject;
    if (!before.empty() && Raw::Bracketing::Deserialize(json::parse(before), previous, error, true)) {
        inputs |= BracketingInputsChanged(previous, state.recipe);
        if (!state.gestureActive) {
            state.undo.push_back(std::move(previous));
            if (state.undo.size() > 100) state.undo.erase(state.undo.begin());
        }
    }
    state.gestureActive = interactionActive;
    state.editedRecipe = edited;
    state.processRequired |= inputs;
    if (state.processRequired && state.interactiveRaw) {
        state.interactiveRaw.reset();
        outcome.invalidatedInteractiveRaw = true;
    }
    if (state.job) state.job->canceled = true;
    if (state.preparationJob) state.preparationJob->canceled = true;
    if (state.detailJob) state.detailJob->canceled = true;
    state.failed = false;
    state.changedAt = now;
    state.pending = !state.processRequired;
    state.publishRequested = state.pending;
    state.previewDirty = state.pending;
    state.status = state.processRequired
        ? "Changes pending. Press Process to update the result."
        : "Updating contribution blend...";
    if (!document || state.newProject) return outcome;
    auto* set = FindSourceSet(*document, state.setId);
    if (!set) return outcome;
    if (!state.processRequired) {
        set->settings["algorithmVersion"] = Raw::Bracketing::RecipeVersion;
        set->settings["bracketing"] = Raw::Bracketing::Serialize(state.recipe);
        state.storedRecipe = state.editedRecipe;
        ++document->hdrInputRevision;
    } else {
        const auto managedFrame = [&](const std::string& id) {
            return std::any_of(set->frames.begin(), set->frames.end(),
                [&](const auto& frame) { return frame.frameId == id; });
        };
        bool managed = true;
        for (const auto& group : state.recipe.groups)
            for (const auto& frame : group.frames) managed &= managedFrame(frame.id);
        for (const auto& source : state.sources) managed &= managedFrame(source.frameId);
        if (managed && !state.recipe.groups.empty() && Raw::Bracketing::Validate(state.recipe, error, true)) {
            set->settings["bracketingDraft"] = Raw::Bracketing::Serialize(state.recipe);
            set->settings["bracketingDraftSelection"] = json::array();
            for (const auto& source : state.sources)
                set->settings["bracketingDraftSelection"].push_back(source.frameId);
        }
    }
    outcome.documentChanged = true;
    return outcome;
}

bool CanAdoptBracketingJob(const BracketingState& state, const BracketingJob& job,
    const RawProjectSnapshot& document, const MultiFrameSourceSet& sourceSet) {
    const auto stored = sourceSet.settings.find("bracketing");
    return (!state.processRequired || job.restoredFromProject) && !job.canceled &&
        job.projectId == document.projectId && job.setId == sourceSet.sourceSetId &&
        job.revision == document.hdrInputRevision && stored != sourceSet.settings.end() &&
        job.recipeIdentity == stored->dump();
}

bool AdoptBracketingJob(BracketingState& state, BracketingJob& job) {
    if(job.result.analysis&&job.result.analysis->panorama)state.panoramaAnalysis=job.result.analysis;
    state.status = job.result.message;
    state.failed = job.result.status != Raw::Bracketing::BracketingResult::Status::Completed || !job.result.raw;
    if (state.failed) {
        if (state.status.empty()) state.status = "Bracketing did not produce a RAW result.";
        return false;
    }
    state.preparationFailed = false;
    state.result = std::make_shared<Raw::Bracketing::BracketingResult>(std::move(job.result));
    state.completedRecipe = job.recipeIdentity;
    state.completedRevision = job.revision;
    state.preview = state.result->preview;
    state.previewDirty = false;
    state.pending = false;
    return true;
}

bool ShouldStartPendingBracketing(const BracketingState& state, double now, bool interactionActive) {
    return state.publishRequested && state.pending && !state.processRequired &&
        !state.job && !state.failed && now - state.changedAt > .4 && !interactionActive;
}

bool RefreshBracketingInteractivePreview(BracketingState& state) {
    if(state.recipe.reconstruction==Raw::Bracketing::ReconstructionMode::Panorama)return false;
    if (!state.previewDirty || state.failed || (!state.pending && !state.publishRequested) ||
        state.processRequired || !state.result || !state.result->analysis) return false;
    if (state.preview.samples.size() == static_cast<std::size_t>(state.preview.width) * state.preview.height * 4 * state.recipe.groups.size())
        state.preview = Raw::Bracketing::ReblendPreview(state.preview, state.recipe, *state.result->analysis);
    state.previewDirty = false;
    state.interactiveRaw = MakeBracketingInteractiveRaw(state);
    return bool(state.interactiveRaw);
}

} // namespace Stack::Project
