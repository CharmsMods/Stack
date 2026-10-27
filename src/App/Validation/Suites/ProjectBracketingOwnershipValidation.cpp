#include "ProjectBracketingOwnershipValidation.h"
#include "Project/BracketingState.h"
#include "Project/BracketingDetail.h"
#include "Editor/Bracketing/BracketingPreviewAvailability.h"
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

void ValidateNativeDetailOwnership() {
    // Full-quality inspection publishes dimensions separately from display RGBA.
    Raw::Bracketing::CapturePreview image;image.width=4;image.height=3;
    Require(Editor::HasBracketingOriginalPixels(&image,true,4*3*4),
        "A dimension-only full-quality original lost its separately stored RGBA pixels.");
    Require(!Editor::HasBracketingOriginalPixels(&image,true,0),
        "Missing full-quality RGBA passed availability.");
    image.width=0;
    Require(!Editor::HasBracketingOriginalPixels(&image,true,0),
        "An empty native image passed availability.");
    image.width=4;image.rgb.assign(4*3*3,.2f);
    Require(Editor::HasBracketingOriginalPixels(&image,false,0),
        "Ordinary prepared RGB pixels became unavailable.");
    image.rgb.pop_back();
    Require(!Editor::HasBracketingOriginalPixels(&image,false,0),
        "A truncated prepared RGB plane passed availability.");
    Raw::Bracketing::BracketingResult historical;
    historical.raw=std::make_shared<Raw::RawImageData>();
    historical.raw->metadata.visibleWidth=4;historical.raw->metadata.visibleHeight=3;
    Require(!Editor::HasSavedBracketingDiagnostics(historical),
        "Absent historical diagnostics were claimed available.");
    auto maps=std::make_shared<Raw::RawImageData::MultiFrameMeasurementSidecars>();
    maps->validity=maps->clipping=maps->localRejection=maps->fallbackReason=
        std::make_shared<const std::vector<std::uint8_t>>(12,0);
    historical.raw->multiFrameMeasurementSidecars=maps;
    Require(Editor::HasSavedBracketingDiagnostics(historical),
        "Complete historical diagnostics were lost.");
    maps->clipping.reset();
    Require(!Editor::HasSavedBracketingDiagnostics(historical),
        "Partial historical diagnostics fabricated unrepresented conditions.");
    using Project::BracketingDetailMode;
    Project::BracketingState state;
    Project::RawProjectSnapshot document;
    InitializeBracket(state, document, "native-detail");
    auto result = std::make_shared<Raw::Bracketing::BracketingResult>();
    result->raw = std::make_shared<Raw::RawImageData>();
    result->raw->metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    result->analysis = std::make_shared<Raw::Bracketing::BracketingAnalysis>();
    state.result = result;
    state.completedRecipe = state.storedRecipe;
    state.completedRevision = document.hdrInputRevision;
    Raw::Bracketing::BracketingRecipe requestRecipe;
    std::string error;
    Project::BracketingState::DetailJob unchanged;
    Require(Project::CaptureBracketingDetail(state, document.hdrInputRevision, unchanged, requestRecipe, error) &&
        unchanged.mode == BracketingDetailMode::Completed &&
        Project::CanAdoptBracketingDetail(state, unchanged, document.hdrInputRevision),
        "An unchanged saved bracket did not retain authoritative native-detail ownership.");

    // Fresh preparation, even with the same saved recipe, replaces the lease.
    // A previously submitted detail must not cross that result-owner boundary.
    state.result = std::make_shared<Raw::Bracketing::BracketingResult>(*result);
    Require(!Project::CanAdoptBracketingDetail(state, unchanged, document.hdrInputRevision),
        "A native detail crossed a replaced result/preparation owner.");
    state.result = result;
    state.job = std::make_shared<Project::BracketingJob>();
    Project::BracketingState::DetailJob reprocess;
    Require(Project::CaptureBracketingDetail(state, document.hdrInputRevision, reprocess, requestRecipe, error) &&
        reprocess.mode == BracketingDetailMode::Completed,
        "A pending explicit reprocess silently substituted newly prepared pixels.");
    state.job.reset();

    state.recipe.automatic = false;
    Project::CommitBracketingEdit(state, &document, false, 1.0);
    Project::BracketingState::DetailJob awaitingPreview;
    Require(Project::CaptureBracketingDetail(state, document.hdrInputRevision, awaitingPreview, requestRecipe, error) &&
        awaitingPreview.mode == BracketingDetailMode::Completed && requestRecipe.automatic,
        "Detail borrowed a proposed recipe before an interactive preview owned the view.");
    state.interactiveRaw = std::make_shared<Raw::RawImageData>();
    Project::BracketingState::DetailJob proposed;
    Require(Project::CaptureBracketingDetail(state, document.hdrInputRevision, proposed, requestRecipe, error) &&
        proposed.mode == BracketingDetailMode::Proposed && !requestRecipe.automatic &&
        Project::CanAdoptBracketingDetail(state, proposed, document.hdrInputRevision) &&
        !Project::CanAdoptBracketingDetail(state, awaitingPreview, document.hdrInputRevision),
        "Live Standard contribution detail did not follow its interactive owner.");
    state.pending = state.previewDirty = false;
    Require(!Project::CanAdoptBracketingDetail(state, proposed, document.hdrInputRevision),
        "A proposed native detail survived loss of its interactive owner.");
    state.pending = true;

    proposed.canceled = true;
    Require(!Project::CanAdoptBracketingDetail(state, proposed, document.hdrInputRevision),
        "A canceled native detail passed adoption.");
    proposed.canceled = false;
    Require(!Project::CanAdoptBracketingDetail(state, proposed, document.hdrInputRevision + 1),
        "A native detail crossed an input revision.");
    ++state.completedRevision;
    Require(!Project::CanAdoptBracketingDetail(state, proposed, document.hdrInputRevision),
        "A native detail crossed a completed revision.");
    --state.completedRevision;
    ++state.previewRevision;
    Require(!Project::CanAdoptBracketingDetail(state, proposed, document.hdrInputRevision),
        "A native detail crossed a preview revision.");
    --state.previewRevision;
    const auto currentRecipe = state.storedRecipe;
    state.storedRecipe += " ";
    Require(!Project::CanAdoptBracketingDetail(state, proposed, document.hdrInputRevision),
        "A native detail crossed a current recipe stamp.");
    state.storedRecipe = currentRecipe;
    state.completedRecipe += " ";
    Require(!Project::CanAdoptBracketingDetail(state, proposed, document.hdrInputRevision),
        "A native detail crossed a completed recipe stamp.");
    state.completedRecipe.pop_back();

    // Undo may restore the same recipe while a newer revision's interactive
    // preview is still active. It remains explicitly proposed until completion.
    Raw::Bracketing::Deserialize(nlohmann::json::parse(state.completedRecipe), state.recipe, error);
    state.storedRecipe = state.completedRecipe;
    Project::BracketingState::DetailJob undone;
    Require(Project::CaptureBracketingDetail(state, document.hdrInputRevision, undone, requestRecipe, error) &&
        undone.mode == BracketingDetailMode::Proposed,
        "An interactive undo lost its newer-revision detail ownership.");

    const auto completedOnly = [&] {
        Project::BracketingState::DetailJob candidate;
        return Project::CaptureBracketingDetail(state, document.hdrInputRevision, candidate, requestRecipe, error) &&
            candidate.mode == BracketingDetailMode::Completed && requestRecipe.automatic &&
            requestRecipe.reconstruction == Raw::Bracketing::ReconstructionMode::Standard;
    };
    state.processRequired = true;
    Require(completedOnly(), "An unprocessed input draft acquired proposed native detail.");
    state.processRequired = false;
    state.failed = true;
    Require(completedOnly(), "A failed interactive owner acquired proposed native detail.");
    state.failed = false;
    state.recipe.alignmentMode = Raw::Bracketing::AlignmentMode::FixedCoordinates;
    Require(completedOnly(), "Incompatible prepared inputs acquired proposed native detail.");
    state.recipe.alignmentMode = Raw::Bracketing::AlignmentMode::AutomaticLocal;
    for (auto mode : {Raw::Bracketing::ReconstructionMode::SuperResolution1x,
        Raw::Bracketing::ReconstructionMode::SuperResolution2x, Raw::Bracketing::ReconstructionMode::Panorama}) {
        state.recipe.reconstruction = mode;
        Require(completedOnly(), "Non-Standard draft changed completed native-detail ownership.");
    }
    state.recipe.reconstruction = Raw::Bracketing::ReconstructionMode::Standard;
    result->raw->reconstructedCameraRgb = true;
    Require(completedOnly(), "Completed camera-RGB pixels acquired Bayer proposal ownership.");
    result->raw->reconstructedCameraRgb = false;
    state.completedRecipe = "not a recipe";
    Project::BracketingState::DetailJob invalid;
    Require(!Project::CaptureBracketingDetail(state, document.hdrInputRevision, invalid, requestRecipe, error),
        "Invalid completed provenance silently borrowed the proposed recipe.");
}
}

void ValidateProjectBracketingOwnership() {
    ValidateNativeDetailOwnership();
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
    std::cout << "PASS independent bracket edits/history, result publication, native-detail ownership and processing progress\n";
}
}
