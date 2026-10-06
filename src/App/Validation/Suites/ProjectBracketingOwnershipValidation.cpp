#include "ProjectBracketingOwnershipValidation.h"
#include "Project/BracketingState.h"
#include "Persistence/BracketingProject.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void InitializeBracket(Project::BracketingState& state, Project::RawProjectSnapshot& document,
    const char* projectId) {
    state.projectId = document.projectId = projectId;
    state.setId = document.activeSourceSetId = "captures";
    state.recipe.originFrameId = "a";
    state.recipe.groups = {{"group-a", "A", true, {{"a"}}}, {"group-b", "B", true, {{"b"}}}};
    state.recipe.knots = Raw::Bracketing::EqualCurves(2);
    state.storedRecipe = state.editedRecipe = Raw::Bracketing::Serialize(state.recipe).dump();
    Project::MultiFrameSourceSet set;
    set.sourceSetId = state.setId;
    set.settings["bracketing"] = Raw::Bracketing::Serialize(state.recipe);
    for (const auto* id : {"a", "b"}) {
        Project::SourceSetFrame frame;
        frame.frameId = id;
        set.frames.push_back(std::move(frame));
    }
    document.sourceSets.push_back(std::move(set));
}
}

void ValidateProjectBracketingOwnership() {
    Project::RawProjectSnapshot exposures;
    Project::MultiFrameSourceSet captures;
    for (unsigned i = 0; i < 3; ++i) {
        Project::EmbeddedAssetRecord asset;
        asset.assetId = std::to_string(i);
        Project::RawCaptureCompatibilitySummary metadata;
        metadata.exposureTimeSeconds = i == 2 ? 4 : 1;
        metadata.isoSpeed = 100;
        metadata.apertureFNumber = i == 0 ? 4 : 8;
        asset.captureMetadataSummary = Project::SerializeRawCaptureCompatibilitySummary(metadata);
        exposures.embeddedAssets.push_back(asset);
        Project::SourceSetFrame frame;
        frame.frameId = frame.assetId = asset.assetId;
        captures.frames.push_back(frame);
    }
    const auto grouped = Project::SuggestBracketingGroups(exposures, captures);
    Require(grouped.groups.size() == 2 && grouped.groups[0].frames.size() == 1 &&
        grouped.groups[1].frames.size() == 2 && grouped.groups[0].frames[0].id == "1" &&
        std::abs(grouped.groups[0].frames[0].relativeEv + 2) < 1e-9,
        "Bracket grouping ignored aperture or separated equivalent exposures.");
    Project::BracketingState a, b;
    Project::RawProjectSnapshot documentA, documentB;
    InitializeBracket(a, documentA, "project-a");
    InitializeBracket(b, documentB, "project-b");
    const auto unchangedB = b.storedRecipe;
    const auto originalA = a.recipe;
    a.job = std::make_shared<Project::BracketingJob>();
    b.job = std::make_shared<Project::BracketingJob>();
    a.recipe.automatic = false;
    const auto edit = Project::CommitBracketingEdit(a, &documentA, false, 10.0);
    Require(edit.changed && edit.documentChanged && !a.processRequired && a.pending &&
        a.undo.size() == 1 && a.job->canceled,
        "Bracket curve edit did not invalidate only its own job and retain its own history.");
    Require(Raw::Bracketing::Serialize(a.undo.back()) == Raw::Bracketing::Serialize(originalA),
        "Bracket undo did not retain the prior recipe.");
    Require(b.storedRecipe == unchangedB && b.editedRecipe == unchangedB && b.undo.empty() &&
        b.redo.empty() && !b.job->canceled && documentB.hdrInputRevision == 0 &&
        documentB.sourceSets.front().settings["bracketing"].dump() == unchangedB,
        "A bracket edit changed another project's recipe, document, history or job.");
    a.job.reset();
    Require(!Project::ShouldStartPendingBracketing(a, 10.1, false) &&
        !Project::ShouldStartPendingBracketing(a, 11.0, true) &&
        Project::ShouldStartPendingBracketing(a, 11.0, false),
        "Bracket pending processing ignored the explicit quiet period or interaction state.");
    a.recipe.alignmentMode = Raw::Bracketing::AlignmentMode::FixedCoordinates;
    const auto inputEdit = Project::CommitBracketingEdit(a, &documentA, false, 12.0);
    Require(inputEdit.changed && a.processRequired && !a.pending &&
        documentA.sourceSets.front().settings.contains("bracketingDraft") &&
        !documentB.sourceSets.front().settings.contains("bracketingDraft"),
        "Bracket input changes did not remain an isolated unprocessed draft.");

    Project::BracketingJob completed;
    completed.projectId = documentB.projectId;
    completed.setId = b.setId;
    completed.revision = documentB.hdrInputRevision;
    completed.recipeIdentity = b.storedRecipe;
    const auto acceptable = [&] {
        return Project::CanAdoptBracketingJob(b, completed, documentB, documentB.sourceSets.front());
    };
    Require(acceptable(), "Current bracket job was rejected.");
    completed.projectId = documentA.projectId;
    Require(!acceptable(), "A bracket job from another project passed publication.");
    completed.projectId = documentB.projectId;
    completed.setId = "other-set";
    Require(!acceptable(), "A bracket job from another capture set passed publication.");
    completed.setId = b.setId;
    ++completed.revision;
    Require(!acceptable(), "A bracket job with a stale input revision passed publication.");
    completed.revision = documentB.hdrInputRevision;
    completed.recipeIdentity += "changed";
    Require(!acceptable(), "A bracket job with a stale recipe passed publication.");
    completed.recipeIdentity = b.storedRecipe;
    completed.canceled = true;
    Require(!acceptable(), "A canceled bracket job passed publication.");
    completed.canceled = false;
    b.processRequired = true;
    Require(!acceptable(), "An unprocessed bracket draft accepted a computed result.");
    completed.restoredFromProject = true;
    Require(acceptable(), "A saved result could not reopen alongside its saved input draft.");

    Project::ProcessingMailbox mailboxA(11), mailboxB(22);
    Raw::Bracketing::ProcessingProgress event;
    event.generation = 11;
    event.stage = Raw::Bracketing::ProcessingStage::Preparing;
    mailboxA.Publish(event);
    mailboxB.Publish(event);
    Require(mailboxA.Read() && !mailboxB.Read(), "Progress crossed bracket generation ownership.");
    event.generation = 22;
    event.stage = Raw::Bracketing::ProcessingStage::Blend;
    mailboxB.Publish(event);
    a.job = std::make_shared<Project::BracketingJob>();
    a.CancelJobs();
    Require(a.job->canceled && !b.job->canceled && mailboxA.Read()->progress.stage == Raw::Bracketing::ProcessingStage::Preparing &&
        mailboxB.Read()->progress.stage == Raw::Bracketing::ProcessingStage::Blend,
        "Canceling one bracket changed another bracket's job or progress.");
    std::cout << "PASS independent bracket edits/history, result publication and processing progress\n";
}
}
