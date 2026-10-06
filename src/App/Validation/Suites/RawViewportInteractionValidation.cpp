#include "App/Validation/Suites/EditorRenderWorkerPreviewValidation.h"
#include "App/Validation/Suites/RawViewportValidationFixture.h"
#include "Editor/RawRenderService.h"
#include "Editor/RawRenderPlanning.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

namespace Stack::Validation {
namespace {
using Clock = std::chrono::steady_clock;
using Service = EditorRendering::RawRenderService;
using Snapshot = EditorRenderWorker::Snapshot;
using Result = EditorRenderWorker::Result;

Snapshot MakeInteractionSnapshot(int width, int height) {
    Snapshot snapshot;
    snapshot.width = snapshot.rawWorkspace.fullFrameWidth = width;
    snapshot.height = snapshot.rawWorkspace.fullFrameHeight = height;
    snapshot.channels = 4;
    snapshot.outputConnected = true;
    snapshot.rawWorkspace.sourceKey = "validation-first-interaction";
    snapshot.rawWorkspace.sourceHash = 1;
    snapshot.rawWorkspace.hasRecipe = true;
    snapshot.rawWorkspace.recipe = RawRecipe::MakeDefaultRecipe("viewport-region-validation");
    snapshot.rawWorkspace.recipe.rgbDenoise.enabled = false;
    snapshot.rawWorkspace.recipe.finishTone.layerJson["localBaselineEnabled"] = false;
    snapshot.rawWorkspace.recipe.finishTone.layerJson["foundationAdaptiveAssist"] = false;
    snapshot.rawWorkspace.editStage = Raw::ViewportStage::RawPlacement;
    snapshot.rawWorkspace.gpuWorkingBudgetBytes = 4ull * 1024 * 1024 * 1024;
    snapshot.rawWorkspace.gpuCacheBudgetBytes = EditorRendering::ResolveRawRenderCacheBudgetBytes(
        snapshot.rawWorkspace.gpuWorkingBudgetBytes,
        EditorRendering::EstimateRawRenderWorkingSetBytes(width,height,0));
    snapshot.rawWorkspace.minimumRawStageCacheBytes = static_cast<std::uint64_t>(width) * height * 8;
    snapshot.telemetry.interactionActive = true;
    snapshot.telemetry.gestureId = 1;
    RenderGraphNode source;
    source.nodeId = 1;
    source.kind = RenderGraphNodeKind::RawDevelopment;
    auto raw = MakeViewportValidationRaw(width,height);
    // The real decoder supplies an immutable identity. Without one this
    // synthetic 100 MB buffer exercises the fallback full-byte hash on each
    // upload, which is a different workload from a loaded RAW document.
    raw->contentIdentity = "first-interaction-fixture:" + std::to_string(width) + "x" + std::to_string(height);
    raw->contentIdentityHash = std::hash<std::string>{}(raw->contentIdentity);
    source.rawDevelopment.embeddedRawData = std::move(raw);
    snapshot.graph.nodes.push_back(source);
    RenderGraphNode output;
    output.nodeId = 2;
    output.kind = RenderGraphNodeKind::Output;
    snapshot.graph.nodes.push_back(output);
    snapshot.graph.links.push_back({1,"imageOut",2,"imageIn"});
    snapshot.graph.outputNodeId = 2;
    snapshot.graph.outputSocketId = "imageOut";
    return snapshot;
}

bool Submit(Service& service, Service::ClientId client, Snapshot& snapshot) {
    snapshot.generation = EditorRenderScheduling::NextGlobalGeneration();
    snapshot.schedulingSerial = snapshot.generation;
    snapshot.rawWorkspace.recipeRevision = snapshot.generation;
    snapshot.graph.nodes[0].rawDevelopment.recipe = snapshot.rawWorkspace.recipe;
    snapshot.telemetry.snapshotReadyAt = Clock::now();
    return service.Submit(client,snapshot);
}

bool Wait(Service& service, Service::ClientId client, std::uint64_t generation, Result* completed = nullptr) {
    const auto deadline = Clock::now() + std::chrono::seconds(15);
    Result result;
    while (Clock::now() < deadline) {
        while (service.TryConsumeCompleted(client,result)) {
            if (result.generation == generation) {
                const bool success = result.success;
                if (completed) *completed = std::move(result);
                return success;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool ValidateStalledDrag(Service& service, Service::ClientId client, EditorRenderWorker& worker, int fps) {
    auto snapshot = MakeInteractionSnapshot(512,384);
    snapshot.telemetry.targetFps = fps;
    snapshot.rawWorkspace.recipe.preToneExposureEv = 0.1f;
    if (!Submit(service,client,snapshot)) return false;
    // Queue a bounded external GL task without pumping the service. This
    // reproduces a real queue stall while pointer values continue arriving.
    std::atomic<bool> started = false;
    auto blocker = std::async(std::launch::async,[&] {
        std::string error;
        return service.ExecuteOpenGlTaskBlocking([&](std::string&) {
            started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(450));
            return true;
        },error);
    });
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!started && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    int deadlineSkips = 0, visible = 0, submittedDuringStall = 0;
    std::uint64_t displayed = 0;
    const auto dragStart = Clock::now();
    auto nextInput = dragStart;
    double firstAfterStallMs = -1;
    while (Clock::now() - dragStart < std::chrono::milliseconds(800)) {
        if (Clock::now() >= nextInput) {
            snapshot.rawWorkspace.recipe.preToneExposureEv += 0.001f;
            if (!Submit(service,client,snapshot)) return false;
            if (Clock::now() - dragStart < std::chrono::milliseconds(450)) ++submittedDuringStall;
            nextInput = Clock::now() + std::chrono::milliseconds(16);
        }
        Result result;
        while (service.TryConsumeCompleted(client,result)) {
            deadlineSkips += result.telemetry.overloadSkipped ? 1 : 0;
            const bool intermediate = EditorRenderScheduling::IsRawGestureIntermediate(
                result.telemetry.interactionActive,true,result.telemetry.gestureId,1);
            if (result.success && EditorRendering::ShouldAdoptRawPresentation(result.generation,
                result.rawWorkspace.recipeRevision,snapshot.generation,snapshot.rawWorkspace.recipeRevision,
                displayed,intermediate)) {
                displayed = result.generation;
                ++visible;
                const double elapsed = std::chrono::duration<double,std::milli>(Clock::now()-dragStart).count();
                if (elapsed >= 450 && firstAfterStallMs < 0) firstAfterStallMs = elapsed - 450;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool blockerOk = blocker.get();
    snapshot.telemetry.interactionActive = false;
    const bool final = Submit(service,client,snapshot) && Wait(service,client,snapshot.generation);
    std::cout << "RAW continuous input, " << fps << " FPS, 450 ms queue stall: " << visible
        << " visible frames, " << deadlineSkips << " deadline cancellations; first after stall "
        << firstAfterStallMs << " ms\n";
    return started && blockerOk && final && visible > 1 && deadlineSkips == 0 && submittedDuringStall > 10 &&
        firstAfterStallMs >= 0 && firstAfterStallMs < 300;
}

bool ValidateBackgroundPreemption(Service& service, Service::ClientId client) {
    auto snapshot = MakeInteractionSnapshot(2048,1536);
    snapshot.rawWorkspace.recipe.rgbDenoise.enabled = true;
    snapshot.rawWorkspace.recipe.rgbDenoise.lumaMap.baseMultiplier = 0.25f;
    snapshot.rawRenderPurpose = RawRenderPurpose::ViewportRefinement;
    snapshot.telemetry.interactionActive = false;
    if (!Submit(service,client,snapshot)) return false;
    const auto nativeGeneration = snapshot.generation;
    snapshot.previewMaxDimension = 512;
    snapshot.rawWorkspace.recipe.preToneExposureEv = 1;
    snapshot.rawRenderPurpose = RawRenderPurpose::InteractivePresentation;
    snapshot.telemetry.interactionActive = true;
    if (!Submit(service,client,snapshot)) return false;
    bool interrupted = false;
    Result result;
    while (service.TryConsumeCompleted(client,result))
        if (result.generation == nativeGeneration && result.telemetry.superseded) interrupted = true;
    const bool foreground = Wait(service,client,snapshot.generation);
    std::cout << "RAW native refinement yields to the first adjustment: " << interrupted << "\n";
    return interrupted && foreground;
}

bool ValidateLargeFirstEdits(Service& service, Service::ClientId client) {
    auto snapshot = MakeInteractionSnapshot(5784,8672);
    // Prepare the source, then exercise actual first edits at native size,
    // including a first edit that interrupts a private calibration pipeline.
    snapshot.rawRenderPurpose = RawRenderPurpose::ViewportRefinement;
    snapshot.telemetry.interactionActive = false;
    if (!Submit(service,client,snapshot) || !Wait(service,client,snapshot.generation)) return false;
    for (int fps : {5,10}) for (bool zoomed : {false,true}) {
        snapshot.telemetry.targetFps = fps;
        snapshot.rawWorkspace.viewport = zoomed
            ? Raw::ViewportRequest{{5784,8672,1700,2500,1600,1800},1.0,2}
            : Raw::ViewportRequest{{5784,8672,0,0,5784,8672},1.0,1};
        for (int edit = 0; edit < 4; ++edit) {
            snapshot.rawRenderPurpose = RawRenderPurpose::InteractivePresentation;
            snapshot.telemetry.interactionActive = true;
            ++snapshot.telemetry.gestureId;
            if (edit == 0) snapshot.rawWorkspace.recipe.preToneExposureEv += 0.1f;
            if (edit == 1) {
                snapshot.rawWorkspace.recipe.finishTone.layerJson["points"] = nlohmann::json::array({
                    {{"x",0.f},{"y",0.f}},{{"x",0.5f},{"y",0.55f+fps*0.001f}},{{"x",1.f},{"y",1.f}}});
                snapshot.rawWorkspace.recipe.finishTone.layerJson["preparedPoints"] = snapshot.rawWorkspace.recipe.finishTone.layerJson["points"];
            }
            if (edit == 2) {
                snapshot.rawWorkspace.recipe.colorWarp.enabled = true;
                RawRecipe::RawColorWarpPin pin;
                pin.id = "first-edit"; pin.targetA = 0.02f + fps*0.001f;
                snapshot.rawWorkspace.recipe.colorWarp.pins = {pin};
            }
            if (edit == 3) snapshot.rawWorkspace.recipe.viewTransform.layerJson["exposure"] = 0.1f + fps*0.01f;
            const auto begin = Clock::now();
            if (!Submit(service,client,snapshot) || !Wait(service,client,snapshot.generation)) return false;
            const double elapsed = std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
            std::cout << "RAW 50 MP first edit " << edit << ", " << fps << " FPS, "
                << (zoomed ? "zoomed" : "fit") << ": " << elapsed << " ms\n";
        }
        snapshot.rawRenderPurpose = RawRenderPurpose::ViewportCalibration;
        snapshot.telemetry.interactionActive = false;
        snapshot.rawWorkspace.calibrationFirstUse = true;
        snapshot.previewMaxDimension = 2048;
        if (!Submit(service,client,snapshot)) return false;
        const auto startDeadline = Clock::now() + std::chrono::seconds(1);
        while (Clock::now() < startDeadline && service.GetProgressFor(client).label.find("Measuring RAW") == std::string::npos)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        snapshot.rawRenderPurpose = RawRenderPurpose::InteractivePresentation;
        snapshot.previewMaxDimension = 0;
        snapshot.telemetry.interactionActive = true;
        snapshot.rawWorkspace.recipe.preToneExposureEv += 0.1f;
        const auto begin = Clock::now();
        Result completed;
        if (!Submit(service,client,snapshot) || !Wait(service,client,snapshot.generation,&completed)) return false;
        std::cout << "RAW 50 MP first edit during calibration, " << fps << " FPS, "
            << (zoomed ? "zoomed" : "fit") << ": "
            << std::chrono::duration<double,std::milli>(Clock::now()-begin).count()
            << " ms; queue/worker/main " << completed.telemetry.queueWaitMs << "/"
            << completed.telemetry.workerTotalMs << "/" << completed.mainRenderMs
            << "; preprocess " << completed.mainGraphStats.rawGpuPreprocessDispatches << "\n";
    }
    return true;
}
}

bool ValidateRawViewportInteraction(GLFWwindow* sharedWindow) {
    auto& service = Service::Get();
    const auto client = service.Acquire(sharedWindow);
    if (!client) return false;
    const bool success = ValidateStalledDrag(service,client,service.m_Worker,5) &&
        ValidateStalledDrag(service,client,service.m_Worker,10) &&
        ValidateBackgroundPreemption(service,client) && ValidateLargeFirstEdits(service,client);
    service.Release(client);
    if (!success) std::cerr << "RAW first-interaction validation failed.\n";
    return success;
}
}
