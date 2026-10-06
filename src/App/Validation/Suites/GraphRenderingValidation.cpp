#include "App/Validation/Suites/GraphRenderingValidation.h"
#include "App/Validation/Suites/RawCandidateOwnershipValidation.h"
#include "Editor/EditorRenderWorker.h"
#include "Editor/RawRenderService.h"
#include "Editor/UI/GraphFrameTransition.h"
#include "Renderer/GLHelpers.h"
#include "App/Validation/Suites/EditorRenderWorkerPreviewValidation.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/ScopedGLObjects.h"
#include <GLFW/glfw3.h>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace Stack::Validation {
namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

RenderGraphSnapshot MakeGraph(int width, int height) {
    std::vector<unsigned char> pixels(std::size_t(width) * height * 4);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = static_cast<unsigned char>((i / 4) % 251);
        pixels[i+1] = static_cast<unsigned char>((i / 4 / width) % 239);
        pixels[i+2] = 90; pixels[i+3] = 255;
    }
    RenderGraphSnapshot graph;
    graph.outputNodeId = 4;
    graph.outputSocketId = "imageOut";
    graph.nodes.resize(4);
    for (int i = 0; i < 4; ++i) graph.nodes[i].nodeId = i + 1;
    auto& source = graph.nodes[0];
    source.kind = RenderGraphNodeKind::Image;
    source.image = {MakeSharedPixelBufferOwned(std::move(pixels)), width, height, 4};
    auto& blur = graph.nodes[1];
    blur.kind = RenderGraphNodeKind::Layer;
    blur.layerJson = {{"type", "GaussianBlur"}, {"amount", 24.0f}};
    auto& exposure = graph.nodes[2];
    exposure.kind = RenderGraphNodeKind::TechnicalImage;
    exposure.technicalImageOperation = NodeMath::TechnicalImageOperation::Exposure;
    exposure.technicalExposureValue = 0.25f;
    graph.nodes[3].kind = RenderGraphNodeKind::Output;
    graph.links = {{1, "imageOut", 2, "imageIn"}, {2, "imageOut", 3, "imageIn"}, {3, "imageOut", 4, "imageIn"}};
    return graph;
}

void ValidateNativeCache(int width, int height) {
    auto graph = MakeGraph(width, height);
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.SetPreviewMaxDimension(0);
    pipeline.SetGraphCacheBudget(2ull * 1024 * 1024 * 1024, 0, false);
    const auto& source = graph.nodes[0].image;
    pipeline.LoadSourceFromSharedPixels(source.pixels, width, height, 4);
    auto render = [&] {
        const auto begin = std::chrono::steady_clock::now();
        pipeline.ExecuteGraph(graph);
        glFinish();
        Require(pipeline.GetOutputTexture() != 0, "Native graph did not produce an image");
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-begin).count();
    };
    const auto cold = render();
    Require(!pipeline.WasGraphImageCacheHit(2, "imageOut"), "Cold blur unexpectedly reused cache");
    graph.nodes[2].technicalExposureValue = 0.75f;
    const auto downstream = render();
    Require(pipeline.WasGraphImageCacheHit(2, "imageOut"), "Downstream edit reran Gaussian blur");
    int outWidth = 0, outHeight = 0;
    auto pixels = pipeline.GetOutputPixels(outWidth, outHeight);
    Require(outWidth == width && outHeight == height, "Graph silently changed resolution");

    // A cold renderer at the same settings must agree with the cached result.
    RenderPipeline reference;
    reference.Initialize();
    reference.SetPreviewMaxDimension(0);
    reference.LoadSourceFromSharedPixels(source.pixels, width, height, 4);
    reference.ExecuteGraph(graph);
    auto expected = reference.GetOutputPixels(outWidth, outHeight);
    Require(pixels == expected, "Cached native render differs from fresh native render");
    reference.Clear();
    pixels.clear(); expected.clear();

    // Rewrapping immutable bytes is not a source edit.
    auto rewrapped = MakeSharedPixelBufferCopy(*source.pixels.bytes, source.pixels.fingerprint);
    pipeline.LoadSourceFromSharedPixels(rewrapped, width, height, 4);
    render();
    Require(pipeline.WasGraphImageCacheHit(2, "imageOut"), "Equivalent source ownership invalidated blur");
    graph.nodes[1].layerJson["amount"] = 28.0f;
    render();
    Require(!pipeline.WasGraphImageCacheHit(2, "imageOut"), "Blur parameter edit reused stale output");
    auto changedPixels = *source.pixels.bytes;
    changedPixels[0] ^= 127;
    graph.nodes[0].image.pixels = MakeSharedPixelBufferOwned(std::move(changedPixels));
    render();
    Require(!pipeline.WasGraphImageCacheHit(2, "imageOut"), "Source edit reused stale blur");
    const auto nativeOutput = pipeline.GetOutputTexture();
    pipeline.SetGraphCacheBudget(0, 0, false);
    Require(glIsTexture(nativeOutput), "Cache eviction deleted the active output");
    std::cout << "Native Graph " << width << 'x' << height << ": cold blur " << cold
        << " ms, downstream edit " << downstream << " ms; cache parity passed.\n";
}

EditorRenderWorker::Result WaitResult(EditorRenderWorker& worker) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    EditorRenderWorker::Result result;
    while (std::chrono::steady_clock::now() < deadline) {
        if (worker.TryConsumeCompleted(result)) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("Graph worker failed to finish pending work");
}

void ValidateWorker(GLFWwindow* window) {
    EditorRenderWorker worker;
    Require(worker.Initialize(window), "Graph worker initialization failed");
    EditorRenderWorker::Snapshot snapshot;
    snapshot.graph = MakeGraph(1024, 512);
    snapshot.sourcePixels = snapshot.graph.nodes[0].image.pixels;
    snapshot.width = 1024; snapshot.height = 512;
    snapshot.outputConnected = true;
    snapshot.viewportTiling.mode = ViewportTilingMode::Off;
    snapshot.graphRequest = {true, 4, 0, false, 1, 123, 1};
    snapshot.generation = 1;
    Require(worker.Submit(snapshot), "Initial worker submission failed");
    auto first = WaitResult(worker);
    Require(first.success && first.outputTexture.texture, "Worker did not publish its native image");
    const auto retained = first.outputTexture.texture;

    // Block the GL task queue, then enqueue a burst. Only the newest pending
    // snapshot should remain. This does not depend on driver rendering speed.
    std::atomic<bool> entered {false}, release {false};
    auto blocker = std::async(std::launch::async, [&] {
        std::string error;
        return worker.ExecuteOpenGlTaskBlocking([&](std::string&) {
            entered = true;
            const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while (!release && std::chrono::steady_clock::now()<deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return true;
        }, error);
    });
    const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while (!entered && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Require(entered, "Worker task did not start");
    for (int revision=2; revision<=50; ++revision) {
        snapshot.generation = snapshot.graphRequest.revision = revision;
        snapshot.graph.nodes[2].technicalExposureValue = float(revision)/100;
        Require(worker.Submit(snapshot), "Interactive submission failed");
    }
    release = true;
    Require(blocker.get(), "Worker GL task failed");
    auto latest = WaitResult(worker);
    Require(latest.success && latest.generation == 50 && latest.graphRequest.revision == 50,
        "Graph worker did not coalesce the newest request");
    Require(glIsTexture(retained), "New render destroyed the retained presentation");
    Require(latest.outputTexture.width == 1024 && latest.outputTexture.height == 512, "Worker changed native resolution");
    snapshot.generation = 51;
    snapshot.graph.outputNodeId = 9999;
    Require(worker.Submit(snapshot), "Invalid-graph submission failed");
    auto failure = WaitResult(worker);
    Require(!failure.success && glIsTexture(retained), "Failure destroyed the accepted presentation");
    Require(failure.error == "The selected graph output is missing from the render snapshot.",
        "Worker discarded the graph's actual failure reason");
    snapshot.generation = 52;
    snapshot.graph.outputNodeId = 4;
    Require(worker.Submit(snapshot), "Shutdown test submission failed");
    worker.CancelActiveAndPending();
    snapshot.generation = snapshot.graphRequest.revision = 53;
    snapshot.graphRequest.sourceIdentity++;
    Require(worker.Submit(snapshot), "Submission after cancellation failed");
    auto recovered = WaitResult(worker);
    Require(recovered.success && recovered.generation == 53, "Worker failed to recover after cancellation");
    // Keep editing while the active GPU render runs. We must receive useful
    // completed frames before the gesture ends, not only after submissions stop.
    std::uint64_t lastSeen = 53;
    int intermediateFrames = 0;
    for (std::uint64_t revision = 100; revision < 300; ++revision) {
        snapshot.generation = snapshot.graphRequest.revision = revision;
        snapshot.graph.nodes[1].layerJson["amount"] = 24.0f + float(revision % 12);
        Require(worker.Submit(snapshot), "Continuous-edit submission failed");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        EditorRenderWorker::Result frame;
        while (worker.TryConsumeCompleted(frame)) {
            Require(frame.success && frame.generation > lastSeen,
                "Continuous edits published an invalid or older frame");
            lastSeen = frame.generation;
            ++intermediateFrames;
            frame.outputTexture.Reset();
        }
    }
    Require(intermediateFrames > 0, "Continuous edits starved Graph presentation");
    while (lastSeen < 299) {
        auto frame = WaitResult(worker);
        Require(frame.success && frame.generation > lastSeen, "Latest edit did not finish");
        lastSeen = frame.generation;
        frame.outputTexture.Reset();
    }
    snapshot.generation = snapshot.graphRequest.revision = 300;
    snapshot.outputConnected = false;
    EditorRenderWorker::PreviewRequest scope;
    scope.previewNodeId = 5; scope.sourceNodeId = 3;
    scope.sourceSocketId = "imageOut"; scope.directSourceOutput = true;
    scope.scopeAnalysis = true; scope.dirtyGeneration = 300;
    scope.sourcePixels = snapshot.sourcePixels;
    scope.width = 1024; scope.height = 512;
    snapshot.previews = {scope};
    Require(worker.Submit(snapshot), "Scope submission failed");
    auto scopeResult = WaitResult(worker);
    Require(scopeResult.previews.size() == 1 && scopeResult.previews[0].success &&
        scopeResult.previews[0].scopeData && scopeResult.previews[0].width <= 256 &&
        !scopeResult.previews[0].scopeData->VectorPoints.empty(),
        "Worker did not prepare scope analysis");
    snapshot.previews.clear();
    snapshot.generation = snapshot.graphRequest.revision = 301;
    snapshot.outputConnected = true;
    Require(worker.Submit(snapshot), "Pre-switch submission failed");
    snapshot.generation = snapshot.graphRequest.revision = 302;
    snapshot.graphRequest.outputNodeId = 3;
    snapshot.graph.outputNodeId = 3;
    worker.UpdateGraphContext(snapshot.graphRequest);
    Require(worker.Submit(snapshot), "Output-switch submission failed");
    auto switched = WaitResult(worker);
    while (switched.generation < 302) switched = WaitResult(worker);
    Require(switched.success && switched.graphRequest.outputNodeId == 3 &&
        switched.outputTexture.width == 1024, "Worker did not finish the selected output");
    switched.outputTexture.Reset();
    snapshot.graph.outputNodeId = snapshot.graphRequest.outputNodeId = 4;
    snapshot.generation = snapshot.graphRequest.revision = 303;
    snapshot.outputConnected = false;
    snapshot.sourcePixels = {};
    snapshot.graph.nodes[0].kind = RenderGraphNodeKind::ImageGenerator;
    snapshot.graph.nodes[0].image = {};
    EditorRenderWorker::CompositeOutputRequest canvas;
    canvas.outputNodeId = 4; canvas.sourceNodeId = 1;
    canvas.width = 1024; canvas.height = 512;
    canvas.preparePixels = true; canvas.keepFullFrame = true;
    snapshot.compositeOutputs = {canvas};
    Require(worker.Submit(snapshot), "Generator canvas submission failed");
    auto composite = WaitResult(worker);
    Require(composite.success && composite.compositeOutputs.size() == 1 &&
        composite.compositeOutputs[0].success && composite.compositeOutputs[0].pixelsPrepared &&
        composite.compositeOutputs[0].width == 1024,
        "Worker could not prepare a generator canvas without CPU source pixels");

    std::cout << "Graph continuous editing published " << intermediateFrames << " intermediate frames.\n";
    worker.Shutdown();
    Require(glIsTexture(retained), "Worker shutdown destroyed a published texture");
    first.outputTexture.Reset(); latest.outputTexture.Reset(); recovered.outputTexture.Reset();
    std::cout << "Graph worker coalescing, cancellation, failure retention and shutdown passed.\n";
}
void ValidateOwnerDelivery(GLFWwindow* window) {
    EditorRenderWorker worker;
    Require(worker.Initialize(window), "Owner worker initialization failed");
    EditorRenderWorker::Snapshot snapshot;
    snapshot.width = 1024; snapshot.height = 512;
    snapshot.viewportTiling.mode = ViewportTilingMode::Off;
    snapshot.graphRequest = {true, 4, 0, false, 1, 123, 1};
    // Leave both owners' results queued to exercise worker-level delivery,
    // before the shared service has a chance to drain either result.
    snapshot.graph = MakeGraph(1024, 512);
    snapshot.sourcePixels = snapshot.graph.nodes[0].image.pixels;
    snapshot.outputConnected = true;
    snapshot.compositeOutputs.clear();
    const auto waitIdle = [&] {
        const auto finishBy = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (worker.HasPendingOrBusyForShutdown() && std::chrono::steady_clock::now() < finishBy)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Require(!worker.HasPendingOrBusyForShutdown(), "Owner render did not finish");
    };
    snapshot.ownerId = 41;
    snapshot.generation = snapshot.graphRequest.revision = 1001;
    Require(worker.Submit(snapshot), "First owner submission failed");
    waitIdle();
    snapshot.ownerId = 42;
    snapshot.generation = snapshot.graphRequest.revision = 1;
    Require(worker.Submit(snapshot), "Second owner submission failed");
    waitIdle();
    auto ownerA = WaitResult(worker);
    auto ownerB = WaitResult(worker);
    Require(ownerA.success && ownerA.ownerId == 41 && ownerB.success && ownerB.ownerId == 42 &&
        ownerB.generation == 1, "Worker lost an owner's result or mixed independent revisions");
    ownerA.outputTexture.Reset();
    ownerB.outputTexture.Reset();
    snapshot.generation = snapshot.graphRequest.revision = 2;
    Require(worker.Submit(snapshot), "Surviving owner submission failed");
    worker.InvalidateOwnerSnapshotsBefore(41, 1002);
    worker.ReleaseOwner(41);
    auto survivingOwner = WaitResult(worker);
    Require(survivingOwner.success && survivingOwner.ownerId == 42 && survivingOwner.generation == 2,
        "Closing one owner canceled another owner's render");
    survivingOwner.outputTexture.Reset();
    worker.Shutdown();
    std::cout << "Independent worker owner delivery and cancellation passed.\n";

    auto& service = Stack::EditorRendering::RawRenderService::Get();
    struct ClientGuard {
        Stack::EditorRendering::RawRenderService& service;
        Stack::EditorRendering::RawRenderService::ClientId id;
        ~ClientGuard() { if (id) service.Release(id); }
    } first {service, service.Acquire(window)}, second {service, service.Acquire(window)};
    Require(first.id && second.id && first.id != second.id, "Independent service clients unavailable");
    {
        std::promise<void> taskStarted;
        auto started = taskStarted.get_future();
        std::promise<void> releaseTask;
        auto released = releaseTask.get_future().share();
        auto compute = std::async(std::launch::async, [&] {
            std::string error;
            return service.ExecuteOpenGlTaskBlocking([&](std::string& taskError) {
                taskStarted.set_value();
                // Bound a regression's wait so a lock stall fails this check
                // instead of hanging the validation process.
                if (released.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
                    taskError = "Client changes waited for unrelated compute.";
                    return false;
                }
                return true;
            }, error);
        });
        Require(started.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
            "Shared service compute task did not start");
        ClientGuard transient {service, service.Acquire(window)};
        Require(transient.id != 0, "Could not acquire a client during unrelated compute");
        service.Release(transient.id);
        service.Release(transient.id);
        transient.id = 0;
        releaseTask.set_value();
        Require(compute.get(), "Client acquire or release blocked on unrelated compute");
        std::cout << "Shared service client changes did not wait for unrelated compute.\n";
    }
    const auto waitClient = [&](auto id, std::uint64_t generation) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        EditorRenderWorker::Result result;
        while (std::chrono::steady_clock::now() < deadline) {
            if (service.TryConsumeCompleted(id, result)) {
                Require(result.success && result.ownerId == id && result.generation == generation,
                    "Shared service mixed independent project results");
                result.outputTexture.Reset();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw std::runtime_error("Shared service owner did not finish");
    };
    snapshot.generation = snapshot.graphRequest.revision = 1001;
    Require(service.Submit(first.id, snapshot), "First service owner submission failed");
    waitClient(first.id, 1001);
    snapshot.generation = snapshot.graphRequest.revision = 1;
    Require(service.Submit(second.id, snapshot), "Second service owner submission failed");
    service.InvalidateSnapshotsBefore(first.id, 1002);
    service.Release(first.id);
    first.id = 0;
    waitClient(second.id, 1);
    std::cout << "Shared service client delivery and release isolation passed.\n";
}
void ValidateGraphThroughRawService(GLFWwindow* window) {
    auto& service = Stack::EditorRendering::RawRenderService::Get();
    const auto client = service.Acquire(window);
    Require(client != 0, "Shared render service unavailable");
    struct ClientGuard {
        Stack::EditorRendering::RawRenderService& service;
        Stack::EditorRendering::RawRenderService::ClientId client;
        ~ClientGuard() { service.Release(client); }
    } guard {service, client};
    EditorRenderWorker::Snapshot snapshot;
    snapshot.graph = MakeGraph(1024, 512);
    snapshot.sourcePixels = snapshot.graph.nodes[0].image.pixels;
    snapshot.width = 1024; snapshot.height = 512;
    snapshot.outputConnected = true;
    snapshot.viewportTiling.mode = ViewportTilingMode::Off;
    snapshot.rawWorkspace.sourceKey = "graph-service-validation";
    snapshot.rawWorkspace.sourceHash = 123;
    snapshot.graphRequest = {true, 4, 0, false, 1, 123, 1};
    std::uint64_t accepted = 0;
    int framesDuringEdits = 0;
    auto consume = [&] {
        EditorRenderWorker::Result result;
        while (service.TryConsumeCompleted(client, result)) {
            if (!result.success) std::cerr << "Shared Graph failure: " << result.error << '\n';
            Require(result.success && result.graphRequest.enabled &&
                result.generation > accepted && result.outputTexture.width == 1024 &&
                result.outputTexture.height == 512,
                "Shared service published an obsolete or non-native Graph frame");
            accepted = result.generation;
            result.outputTexture.Reset();
            ++framesDuringEdits;
        }
    };
    for (std::uint64_t revision = 1; revision <= 100; ++revision) {
        snapshot.generation = snapshot.graphRequest.revision = revision;
        snapshot.graph.nodes[1].layerJson["amount"] = 24.0f + float(revision % 12);
        Require(service.Submit(client, snapshot), "Shared Graph submission failed");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        consume();
    }
    Require(framesDuringEdits > 0, "Shared service starved Graph presentation");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (accepted < 100 && std::chrono::steady_clock::now() < deadline) {
        consume();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(accepted == 100, "Shared service lost the final Graph edit");
    std::cout << "RAW-backed Graph service kept native resolution and forward progress.\n";
}

} // namespace

bool ValidateGraphRendering(bool projectOwnershipOnly) {
    std::cout << std::unitbuf;
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(64, 64, "Graph rendering validation", nullptr, nullptr);
    bool passed = false;
    if (window) {
        glfwMakeContextCurrent(window);
        try {
            Require(LoadGLFunctions(), "OpenGL loading failed");
            if (projectOwnershipOnly) {
                ValidateOwnerDelivery(window);
                RenderPipeline pipeline;
                pipeline.Initialize();
                Require(pipeline.ValidateRawNativeDenoiseHandoffForTesting(),
                    "Project denoise ownership validation failed");
                ValidateRawCandidateOwnership();
            } else {
            Require(ValidateRawViewportTransitions(), "Frame blending shader validation failed");
            Require(ValidateRawViewportPresentation(), "Raw viewport presentation validation failed");
            {
                GraphFrameTransition transition;
                Stack::GraphRendering::FramePresentation frame {{true,4,0,false,1,123,1},2,2,false,1.0};
                Require(!transition.Prepare(frame,200), "First frame should not blend");
                frame.completedAt = 1.2;
                Require(transition.Prepare(frame,200), "Slow completion should blend");
                const auto oldTexture = GLHelpers::CreateEmptyTexture(2,2);
                transition.RetainOwned(oldTexture,2,2);
                transition.Validate(frame.identity,1.25);
                Require(glIsTexture(oldTexture), "Transition released its previous frame too early");
                transition.Validate(frame.identity,1.4);
                Require(!glIsTexture(oldTexture), "Finished transition retained its previous frame");
                frame.completedAt = 1.5;
                Require(transition.Prepare(frame,200), "Subsequent completion should blend");
                const auto interruptedTexture = GLHelpers::CreateEmptyTexture(2,2);
                transition.RetainOwned(interruptedTexture,2,2);
                frame.completedAt = 1.55;
                Require(transition.Prepare(frame,100), "Newest completion was delayed by an old blend");
                Require(!glIsTexture(interruptedTexture), "Interrupted blend retained an obsolete frame");
                const auto switchedTexture = GLHelpers::CreateEmptyTexture(2,2);
                transition.RetainOwned(switchedTexture,2,2);
                frame.identity.outputNodeId++;
                transition.Validate(frame.identity,1.56);
                Require(!glIsTexture(switchedTexture), "Output switch retained a transition from another output");
                transition.Shutdown();
            }
            ValidateNativeCache(1024, 512);
            ValidateNativeCache(7680, 4320);
            ValidateWorker(window);
            ValidateOwnerDelivery(window);
            ValidateGraphThroughRawService(window);
            }
            passed = true;
        } catch (const std::exception& error) {
            std::cerr << "Graph rendering validation failed: " << error.what() << '\n';
        }
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window);
    }
    glfwTerminate();
    return passed;
}
} // namespace Stack::Validation
