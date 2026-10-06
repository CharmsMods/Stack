#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Stack::Project { struct LoadedProjectData; }

enum class ProjectLoadPhase {
    None, LibraryFadeOut, SpinnerFadeIn, WaitForEditorReady,
    SpinnerFadeOut, EditorReveal
};

// Presentation and deferred apply belong to the workspace receiving the load.
// The shell only draws the currently visible owner's transition.
struct ProjectLoadTransition {
    bool AcceptsCompletion(std::uint64_t completedGeneration) const {
        return phase != ProjectLoadPhase::None && generation == completedGeneration;
    }

    void Reset() {
        const auto nextGeneration = generation + 1;
        *this = {};
        generation = nextGeneration;
    }

    ProjectLoadPhase phase = ProjectLoadPhase::None;
    std::uint64_t generation = 0;
    double phaseStartTime = 0.0;
    double spinnerStartTime = 0.0;
    int phasePresentedFrames = 0;
    std::string projectFileName;
    bool decodeReady = false;
    bool decodeSucceeded = false;
    bool applySucceeded = false;
    bool dismissLibraryPreviewsPending = false;
    bool loadRequested = false;
    bool firstRenderReady = false;
    bool nodeBrowserThumbnailsReady = false;
    double startedAt = 0.0;
    double decodeRequestedAt = 0.0;
    double decodeReadyAt = 0.0;
    double applyStartedAt = 0.0;
    double applyFinishedAt = 0.0;
    double firstRenderReadyAt = 0.0;
    double thumbnailsReadyAt = 0.0;
    double readyToRevealAt = 0.0;
    std::shared_ptr<Stack::Project::LoadedProjectData> decodedProject;
    std::shared_ptr<void> ownerLease;
    std::vector<std::string> trace;
};
