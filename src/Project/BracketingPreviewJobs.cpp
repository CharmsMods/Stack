#include "BracketingState.h"
#include "Async/TaskSystem.h"
#include "BurstEngine/Client.h"

namespace Stack::Project {

bool RefreshBracketingInteractivePreview(BracketingState& state) {
    bool changed = false;
    if (state.previewJob && state.previewJob->done.load()) {
        auto job = std::move(state.previewJob);
        if (!job->canceled && job->revision == state.previewRevision &&
            job->source == state.result && !state.processRequired && !state.failed &&
            (state.pending || state.publishRequested)) {
            if (!job->preview.width) {
                state.status = job->error.empty() ? "Could not update the burst preview." : job->error;
            } else {
                state.preview = std::move(job->preview);
                state.interactiveRaw = MakeBracketingInteractiveRaw(state);
                changed = bool(state.interactiveRaw);
            }
        }
    }

    if (state.recipe.reconstruction == Raw::Bracketing::ReconstructionMode::Panorama ||
        !state.previewDirty || state.failed || (!state.pending && !state.publishRequested) ||
        state.processRequired || !state.result || !state.result->analysis) return changed;

    // A newer edit leaves previewDirty set while the canceled request drains.
    // Only the newest recipe is submitted next, with no queue per slider event.
    if (state.previewJob) return changed;
    const auto& sourcePreview = state.result->preview;
    if (sourcePreview.samples.size() != static_cast<std::size_t>(sourcePreview.width) *
        sourcePreview.height * 4 * state.recipe.groups.size()) {
        state.previewDirty = false;
        return changed;
    }

    auto job = std::make_shared<BracketingState::PreviewJob>();
    job->revision = state.previewRevision;
    job->source = state.result;
    const auto recipe = state.recipe;
    state.previewJob = job;
    state.previewDirty = false;
    if (!Async::TaskSystem::Get().SubmitHighPriority("Updating burst preview", [job, recipe] {
        try {
            if (!job->canceled) {
                job->preview = Stack::BurstEngine::ReblendPreview(
                    job->source->preview, recipe, *job->source->analysis, &job->error,
                    [job] { return job->canceled.load(); });
            }
        } catch (const std::exception& error) {
            job->error = error.what();
        } catch (...) {
            job->error = "The burst preview request failed.";
        }
        job->done = true;
    })) {
        state.previewJob.reset();
        state.status = "Could not schedule the burst preview.";
    }
    return changed;
}

} // namespace Stack::Project
