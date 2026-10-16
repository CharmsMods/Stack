#include "BracketingDiagnosticInputs.h"
#include "Async/TaskSystem.h"
#include "BurstEngine/Client.h"
#include "Project/BracketingState.h"
#include "Raw/Bracketing/ResultSerialization.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace Stack::Validation {
// Bounded real-file check of the shipping client boundary. All outputs belong
// to a fresh validation directory; source captures are only read.
bool ValidateBurstWorker(const std::filesystem::path& inputs, const std::filesystem::path& output,
    bool combiningCancellationOnly) {
    using namespace Raw::Bracketing;
    const auto require = [](bool ok, const std::string& message) {
        if (!ok) throw std::runtime_error(message);
    };
    Async::TaskSystem::Get().Initialize();
    bool success = false;
    try {
        require(!std::filesystem::exists(output), "Choose a new validation output directory.");
        std::filesystem::create_directories(output);
        ProcessingRequest request;
        std::string error;
        require(LoadBracketingDiagnosticFolder(inputs, request, error), error);
        require(request.sources.size() >= 2 && request.sources.size() <= 8,
            "This focused check requires two to eight real RAW captures.");
        const auto memory = Raw::Mfd::ResolveMfdProcessingMemoryBudget(0, Raw::Mfd::QueryPhysicalMemorySnapshot());
        require(memory.valid, memory.message);
        request.memoryBudgetBytes = memory.budgetBytes;
        request.automaticMemoryBudget = true;
        request.workerCount = ResolveBracketingWorkerCount(memory.budgetBytes);
        request.removeCacheOnRelease = true;
        request.cacheDirectory = output / "cancel-cache";
        std::atomic<bool> cancel{false};
        std::atomic<unsigned> progressEvents{0}, presentationEvents{0};
        request.shouldCancel = [&] { return cancel.load(); };
        std::chrono::steady_clock::time_point canceledAt;
        request.reportProgress = [&](double, const std::string& stage) {
            if ((!combiningCancellationOnly || stage == "Combining exposure groups") && !cancel.exchange(true))
                canceledAt = std::chrono::steady_clock::now();
        };
        const auto canceled = BurstEngine::Process(request);
        require(cancel && canceled.status == BracketingResult::Status::Canceled,
            "The worker did not report cooperative cancellation.");
        if (combiningCancellationOnly) {
            // The public progress event follows a completed tile, while other
            // tile workers can still be dispatching or waiting for GPU memory.
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - canceledAt).count();
            const nlohmann::json report = {{"passed", true}, {"stage", "Combining exposure groups"},
                {"cancellationSeconds", seconds}, {"sourceCount", request.sources.size()}};
            std::ofstream(output / "burst-cancellation-check.json") << report.dump(2);
            std::cout << "Burst combining cancellation passed in " << seconds << " seconds.\n";
            Async::TaskSystem::Get().Shutdown();
            return true;
        }
        cancel = false;
        request.cacheDirectory = output / "process-cache";
        request.reportProgress = [&](double fraction, const std::string& stage) {
            if (progressEvents.fetch_add(1) % 12 == 0)
                std::cout << "Burst worker: " << int(fraction * 100) << "% " << stage << '\n' << std::flush;
        };
        request.reportPresentation = [&](const ProcessingProgress&) { ++presentationEvents; };
        const auto start = std::chrono::steady_clock::now();
        auto result = BurstEngine::Process(request);
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        require(result.status == BracketingResult::Status::Completed, result.message);
        require(result.raw && result.raw->normalizedMosaicBuffer && result.analysis &&
            result.analysis->sessionLease && !result.analysis->prepared,
            "The result did not retain a remote processing session.");
        require(progressEvents > 0 && presentationEvents > 0, "Progress was not delivered to Stack.");

        Project::RawProjectSnapshot document;
        document.projectId = "burst-worker-validation";
        document.hdrInputRevision = 1;
        Project::MultiFrameSourceSet set;
        set.sourceSetId = "real-captures";
        set.settings["bracketing"] = Serialize(request.recipe);
        Project::BracketingState state;
        state.recipe = request.recipe;
        state.editedRecipe = state.storedRecipe = Serialize(request.recipe).dump();
        Project::BracketingJob job;
        job.projectId = document.projectId;
        job.setId = set.sourceSetId;
        job.revision = document.hdrInputRevision;
        job.recipeIdentity = state.storedRecipe;
        job.result = std::move(result);
        require(Project::CanAdoptBracketingJob(state, job, document, set), "Stack rejected its current job.");
        require(Project::AdoptBracketingJob(state, job), "Stack did not adopt the completed measurement.");

        state.pending = state.publishRequested = state.previewDirty = true;
        const auto previewStart = std::chrono::steady_clock::now();
        Project::RefreshBracketingInteractivePreview(state);
        const auto previewDeadline = previewStart + std::chrono::seconds(60);
        while (state.previewJob && std::chrono::steady_clock::now() < previewDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            Project::RefreshBracketingInteractivePreview(state);
        }
        require(!state.previewJob && state.interactiveRaw, "Retained asynchronous preview failed: " + state.status);
        const auto previewSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - previewStart).count();
        request.analysis = state.result->analysis;
        const auto detail = BurstEngine::RenderResultDetail(request, *state.result,
            state.result->raw->metadata.visibleWidth / 2, state.result->raw->metadata.visibleHeight / 2, 192);
        require(detail.width && !detail.resultRgb.empty(), "Retained native detail failed.");
        const auto inspection = BurstEngine::RenderInspectionImage(request, *state.result,
            InspectionImageKind::Capture, request.sources.front().frameId, 0, 256);
        require(inspection && inspection->width, "Retained capture inspection failed.");

        const auto artifact = output / "result.bracket";
        require(Serialization::WriteResultFile(artifact, *state.result), "Could not write the result artifact.");
        BracketingResult restored;
        std::ifstream stream(artifact, std::ios::binary);
        Serialization::ReadResult(stream, std::filesystem::file_size(artifact), request.recipe.groups.size(),
            restored, output / "restored-inspection");
        require(restored.raw && restored.raw->normalizedMosaicBuffer &&
            *restored.raw->normalizedMosaicBuffer == *state.result->raw->normalizedMosaicBuffer,
            "Measurement samples changed during persistence round-trip.");
        auto restoredRequest = request;
        restoredRequest.analysis.reset();
        restoredRequest.preparationOnly = true;
        const auto prepared = BurstEngine::Process(restoredRequest);
        require(prepared.status == BracketingResult::Status::Completed && prepared.analysis,
            "Saved-result source preparation failed: " + prepared.message);
        restoredRequest.analysis = prepared.analysis;
        restored.analysis = prepared.analysis;
        const auto savedInspection = BurstEngine::RenderInspectionImage(restoredRequest, restored,
            InspectionImageKind::Result, {}, 0, 256);
        require(savedInspection && savedInspection->width, "Saved-result inspection import failed.");
        const auto restoredPreview = BurstEngine::ReblendPreview(restored.preview, request.recipe,
            *prepared.analysis, &error);
        require(restoredPreview.width, "Saved-result overview reuse failed: " + error);
        for (const auto& source : request.sources) {
            const auto current = RawEvidence::ComputeSourceIdentity(source.path);
            require(current.valid && current.sha256 == source.sha256 && current.byteSize == source.bytes,
                "An original capture changed.");
        }
        nlohmann::json report = {
            {"passed", true}, {"sourceCount", request.sources.size()}, {"processingSeconds", elapsed},
            {"retainedPreviewSeconds", previewSeconds}, {"progressEvents", progressEvents.load()},
            {"presentationEvents", presentationEvents.load()}, {"workerCount", request.workerCount},
            {"recipeIdentity", Identity(request.recipe)}, {"analysisIdentity", state.result->analysis->identity},
            {"stageSeconds", state.result->analysis->stageSeconds}, {"originalsUnchanged", true},
            {"measurementRoundTripExact", true}, {"stackResultAdopted", true},
            {"cancellationPassed", true}, {"inspectionPassed", true}, {"savedResultImportPassed", true}
        };
        std::ofstream(output / "burst-worker-check.json") << report.dump(2);
        std::cout << "Burst worker check passed: " << (output / "burst-worker-check.json") << '\n';
        success = true;
    } catch (const std::exception& error) {
        std::cerr << "Burst worker check failed: " << error.what() << '\n';
    }
    Async::TaskSystem::Get().Shutdown();
    return success;
}
}
