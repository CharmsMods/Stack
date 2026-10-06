#pragma once

#include "Persistence/RawProjectModel.h"
#include "Project/BracketingProgress.h"
#include "Raw/Bracketing/Processor.h"
#include <atomic>
#include <functional>
#include <mutex>

namespace Stack::Project {

struct BracketingDraftSource {
    std::string frameId, sourceKey;
    std::filesystem::path path;
    RawCaptureCompatibilitySummary metadata;
    std::string error;
    bool inspected = false;
};

struct BracketingMetadataJob {
    std::atomic<bool> done{false};
    std::vector<BracketingDraftSource> sources;
};

struct BracketingJob {
    std::atomic<bool> canceled{false}, done{false};
    std::atomic<double> progress{0};
    std::mutex mutex;
    std::string message;
    Raw::Bracketing::BracketingResult result;
    std::string projectId, setId, recipeIdentity;
    std::uint64_t revision = 0;
    bool restoredFromProject = false;
    std::shared_ptr<ProcessingMailbox> presentation;
};

// Per-project draft, computation and accepted results. No window or selected
// tab is required to edit a recipe or decide whether a worker may publish.
struct BracketingState {
    std::vector<BracketingDraftSource> sources;
    std::shared_ptr<BracketingMetadataJob> metadataJob;
    std::map<std::string, BracketingDraftSource> sourceHistory;
    std::string editedRecipe, projectName = "Bracket", boundDocument;
    std::filesystem::path awaitingProject;
    bool newProject = false, processRequired = false, processAfterImport = false;
    bool savingDraft = false;
    std::function<void(bool)> saveBeforeCloseCompletion;
    std::shared_ptr<const Raw::RawImageData> interactiveRaw;
    std::string projectId, setId, storedRecipe, status;
    std::string completedRecipe;
    std::uint64_t completedRevision = 0;
    Raw::Bracketing::BracketingRecipe recipe;
    std::vector<Raw::Bracketing::BracketingRecipe> undo, redo;
    std::shared_ptr<BracketingJob> job, preparationJob;
    bool preparationRequested = false, preparationFailed = false;
    std::shared_ptr<const Raw::Bracketing::BracketingResult> result;
    // Preserve successfully solved geometry if only the requested canvas size failed.
    std::shared_ptr<const Raw::Bracketing::BracketingAnalysis> panoramaAnalysis;
    Raw::Bracketing::Preview preview, detail;
    struct DetailJob {
        std::atomic<bool> done{false}, canceled{false};
        Raw::Bracketing::Preview preview;
        std::string recipeIdentity, error;
    };
    std::shared_ptr<DetailJob> detailJob;
    struct InspectionJob {
        std::atomic<bool> done{false}, canceled{false};
        std::shared_ptr<const Raw::Bracketing::CapturePreview> image;
        std::vector<std::uint8_t> rgba;
        std::string identity, error;
    };
    std::shared_ptr<InspectionJob> inspectionJob;
    std::shared_ptr<const Raw::Bracketing::CapturePreview> inspectionImage;
    std::vector<std::uint8_t> inspectionPixels;
    std::string inspectionIdentity, inspectionRequested, inspectionError;
    bool publishRequested = false, pending = false, failed = false, previewDirty = false;
    bool gestureActive = false;
    double changedAt = 0;

    ~BracketingState();
    void CancelJobs();
    bool HasInteractivePreview() const {
        return interactiveRaw && (pending || job || previewDirty) && !processRequired && !failed;
    }
};

struct BracketingEditOutcome {
    bool changed = false;
    bool documentChanged = false;
    bool invalidatedInteractiveRaw = false;
};

bool BracketingInputsChanged(const Raw::Bracketing::BracketingRecipe& before,
    const Raw::Bracketing::BracketingRecipe& after);
BracketingEditOutcome CommitBracketingEdit(BracketingState& state,
    RawProjectSnapshot* document, bool interactionActive, double now);
bool CanAdoptBracketingJob(const BracketingState& state, const BracketingJob& job,
    const RawProjectSnapshot& document, const MultiFrameSourceSet& sourceSet);
// Call only after CanAdoptBracketingJob. Moves the full result and shares its
// RAW/analysis buffers; it does not copy full-resolution image data.
bool AdoptBracketingJob(BracketingState& state, BracketingJob& job);
bool ShouldStartPendingBracketing(const BracketingState& state, double now,
    bool interactionActive);
bool RefreshBracketingInteractivePreview(BracketingState& state);
std::shared_ptr<const Raw::RawImageData> MakeBracketingInteractiveRaw(const BracketingState& state);

} // namespace Stack::Project
