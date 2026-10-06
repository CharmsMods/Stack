#include "App/Validation/Suites/RawViewportValidationFixture.h"
#include "App/Validation/Suites/EditorRenderWorkerPreviewValidation.h"

#include "Editor/EditorRenderWorker.h"
#include "Raw/RawViewportCalibration.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Renderer/GLLoader.h"
#include "Renderer/RawGraphViewportWorkload.h"
#include "Utils/SharedPixelBuffer.h"

#include <chrono>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

namespace Stack::Validation {
namespace {

RenderGraphNode PreviewNode(
    int nodeId,
    RenderGraphNodeKind kind,
    const char* definitionId) {
    RenderGraphNode node;
    node.nodeId = nodeId;
    node.kind = kind;
    node.definitionId = definitionId;
    node.definitionVersion = 1;
    node.definitionHash = definitionId;
    node.requestRevision = 1;
    return node;
}

EditorRenderWorker::PreviewRequest DirectPreviewRequest(
    int previewNodeId) {
    EditorRenderWorker::PreviewRequest request;
    request.previewNodeId = previewNodeId;
    request.sourceNodeId = 1;
    request.sourceSocketId =
        EditorNodeGraph::kImageOutputSocketId;
    request.directSourceOutput = true;
    request.dirtyGeneration = 1;
    return request;
}

bool WaitForResult(
    EditorRenderWorker& worker,
    EditorRenderWorker::Result& result) {
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (worker.TryConsumeCompleted(result)) {
            return true;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(2));
    }
    return false;
}

void ReleaseSharedResultResources(
    EditorRenderWorker::Result& result) {
    if (result.outputTexture.readyFence) {
        glDeleteSync(result.outputTexture.readyFence);
        result.outputTexture.readyFence = nullptr;
    }
    if (result.outputTexture.texture != 0) {
        glDeleteTextures(
            1,
            &result.outputTexture.texture);
        result.outputTexture.texture = 0;
    }
    if (result.outputTiles.readyFence) {
        glDeleteSync(result.outputTiles.readyFence);
        result.outputTiles.readyFence = nullptr;
    }
    for (EditorRenderWorker::SharedTextureTile& tile :
         result.outputTiles.tiles) {
        if (tile.texture != 0) {
            glDeleteTextures(1, &tile.texture);
            tile.texture = 0;
        }
    }
}

} // namespace

bool ValidateEditorRenderWorkerPreviewBatch(GLFWwindow* sharedWindow) {
    constexpr int kWidth = 4;
    constexpr int kHeight = 4;
    std::vector<unsigned char> source(
        static_cast<std::size_t>(kWidth) *
        static_cast<std::size_t>(kHeight) * 4u);
    for (int pixel = 0; pixel < kWidth * kHeight; ++pixel) {
        const std::size_t offset =
            static_cast<std::size_t>(pixel) * 4u;
        source[offset + 0u] =
            static_cast<unsigned char>(15 + pixel * 7);
        source[offset + 1u] =
            static_cast<unsigned char>(30 + pixel * 5);
        source[offset + 2u] =
            static_cast<unsigned char>(45 + pixel * 3);
        source[offset + 3u] = 255;
    }

    EditorRenderWorker::Snapshot snapshot;
    snapshot.generation = 1;
    snapshot.width = kWidth;
    snapshot.height = kHeight;
    snapshot.channels = 4;
    snapshot.sourcePixels =
        MakeSharedPixelBufferOwned(std::move(source));
    snapshot.outputConnected = false;
    snapshot.graph.nodes = {
        PreviewNode(
            1,
            RenderGraphNodeKind::Image,
            "validation:worker-preview/source"),
        PreviewNode(
            2,
            RenderGraphNodeKind::ChannelSplit,
            "validation:worker-preview/split")
    };
    snapshot.graph.links.push_back(RenderGraphLink{
        1,
        EditorNodeGraph::kImageOutputSocketId,
        2,
        EditorNodeGraph::kImageInputSocketId
    });

    snapshot.previews.push_back(DirectPreviewRequest(101));
    EditorRenderWorker::PreviewRequest frequencyRequest;
    frequencyRequest.previewNodeId = 102;
    frequencyRequest.sourceNodeId = 2;
    frequencyRequest.sourceSocketId = "r";
    frequencyRequest.frequencySpectrumInput = true;
    frequencyRequest.dirtyGeneration = 1;
    snapshot.previews.push_back(std::move(frequencyRequest));
    snapshot.previews.push_back(DirectPreviewRequest(103));

    EditorRenderWorker worker;
    if (!worker.Initialize(sharedWindow) ||
        !worker.Submit(std::move(snapshot))) {
        worker.Shutdown();
        std::cerr
            << "Editor render-worker preview validation failed: "
               "worker initialization or submission failed.\n";
        return false;
    }

    EditorRenderWorker::Result result;
    const bool received = WaitForResult(worker, result);
    worker.Shutdown();

    const bool complete =
        received &&
        result.success &&
        result.previews.size() == 3u;
    const bool directOutputsMatch =
        complete &&
        result.previews[0].success &&
        result.previews[2].success &&
        result.previews[0].width == kWidth &&
        result.previews[0].height == kHeight &&
        result.previews[2].width == kWidth &&
        result.previews[2].height == kHeight &&
        result.previews[0].pixels ==
            result.previews[2].pixels;
    const bool frequencyOutputValid =
        complete &&
        result.previews[1].success &&
        result.previews[1].width == kWidth &&
        result.previews[1].height == kHeight &&
        !result.previews[1].pixels.empty();
    if (!complete ||
        !directOutputsMatch ||
        !frequencyOutputValid) {
        std::cerr
            << "Editor render-worker preview validation failed: "
               "direct/frequency/direct batch did not remain complete "
               "and pixel-stable.\n";
        return false;
    }

    std::cout
        << "Editor render-worker preview validation passed: one graph "
           "copy and reusable topology survived temporary frequency "
           "expansion with pixel-identical direct outputs.\n";
    return true;
}

bool ValidateEditorRenderWorkerTileBatch(
    GLFWwindow* sharedWindow) {
    constexpr int kWidth = 300;
    constexpr int kHeight = 180;
    std::vector<unsigned char> source(
        static_cast<std::size_t>(kWidth) *
        static_cast<std::size_t>(kHeight) * 4u);
    for (int pixel = 0; pixel < kWidth * kHeight; ++pixel) {
        const std::size_t offset =
            static_cast<std::size_t>(pixel) * 4u;
        source[offset + 0u] =
            static_cast<unsigned char>((pixel * 3) & 0xff);
        source[offset + 1u] =
            static_cast<unsigned char>((pixel * 5) & 0xff);
        source[offset + 2u] =
            static_cast<unsigned char>((pixel * 7) & 0xff);
        source[offset + 3u] = 255;
    }
    const SharedPixelBuffer sharedSource =
        MakeSharedPixelBufferOwned(std::move(source));

    RenderGraphNode sourceNode = PreviewNode(
        1,
        RenderGraphNodeKind::Image,
        "validation:worker-tiles/source");
    sourceNode.image.pixels = sharedSource;
    sourceNode.image.width = kWidth;
    sourceNode.image.height = kHeight;
    sourceNode.image.channels = 4;
    RenderGraphNode outputNode = PreviewNode(
        2,
        RenderGraphNodeKind::Output,
        "validation:worker-tiles/output");

    EditorRenderWorker::Snapshot snapshot;
    snapshot.generation = 2;
    snapshot.width = kWidth;
    snapshot.height = kHeight;
    snapshot.channels = 4;
    snapshot.sourcePixels = sharedSource;
    snapshot.outputConnected = true;
    snapshot.viewportTiling.mode =
        ViewportTilingMode::Always;
    snapshot.viewportTiling.tileSize = 128;
    snapshot.graph.outputNodeId = 2;
    snapshot.graph.outputSocketId =
        EditorNodeGraph::kImageOutputSocketId;
    snapshot.graph.nodes.push_back(
        std::move(sourceNode));
    snapshot.graph.nodes.push_back(
        std::move(outputNode));
    snapshot.graph.links.push_back(RenderGraphLink{
        1,
        EditorNodeGraph::kImageOutputSocketId,
        2,
        EditorNodeGraph::kImageInputSocketId
    });

    EditorRenderWorker worker;
    if (!worker.Initialize(sharedWindow) ||
        !worker.Submit(std::move(snapshot))) {
        worker.Shutdown();
        std::cerr
            << "Editor render-worker tile validation failed: "
               "worker initialization or submission failed.\n";
        return false;
    }

    EditorRenderWorker::Result result;
    const bool received = WaitForResult(worker, result);
    worker.Shutdown();

    bool fenceReady = false;
    if (received && result.outputTiles.readyFence) {
        const GLenum waitResult = glClientWaitSync(
            result.outputTiles.readyFence,
            GL_SYNC_FLUSH_COMMANDS_BIT,
            1000000000ull);
        fenceReady =
            waitResult == GL_ALREADY_SIGNALED ||
            waitResult == GL_CONDITION_SATISFIED;
    }

    std::size_t coveredPixels = 0;
    bool texturesValid = true;
    for (const EditorRenderWorker::SharedTextureTile& tile :
         result.outputTiles.tiles) {
        coveredPixels +=
            static_cast<std::size_t>(tile.width) *
            static_cast<std::size_t>(tile.height);
        texturesValid &=
            tile.texture != 0 &&
            glIsTexture(tile.texture) == GL_TRUE &&
            tile.width > 0 &&
            tile.height > 0 &&
            tile.haloWidth >= tile.width &&
            tile.haloHeight >= tile.height;
    }

    const bool valid =
        received &&
        result.success &&
        result.mainRegionPlanAvailable &&
        result.mainRegionPlanTileable &&
        result.outputTiles.tiled &&
        result.outputTiles.complete &&
        result.outputTiles.fullWidth == kWidth &&
        result.outputTiles.fullHeight == kHeight &&
        result.outputTiles.tiles.size() > 1u &&
        coveredPixels ==
            static_cast<std::size_t>(kWidth) *
            static_cast<std::size_t>(kHeight) &&
        fenceReady &&
        texturesValid;
    ReleaseSharedResultResources(result);
    if (!valid) {
        std::cerr
            << "Editor render-worker tile validation failed: "
               "the shared tiled result was incomplete, uncovered, "
               "unsynchronized, or invalid.\n";
        return false;
    }

    std::cout
        << "Editor render-worker tile validation passed: one graph "
           "copy/topology produced a complete synchronized multi-tile "
           "shared output.\n";
    return true;
}

bool ValidateTransactionalOutputUpload() {
    const std::vector<unsigned char> firstPixels = {
        10, 20, 30, 255,
        40, 50, 60, 255,
        70, 80, 90, 255,
        100, 110, 120, 255
    };
    const std::vector<unsigned char> secondPixels = {
        120, 80, 40, 255
    };

    RenderPipeline pipeline;
    pipeline.Initialize();
    const bool firstUploaded =
        pipeline.UploadOutputFromPixels(
            firstPixels.data(),
            2,
            2,
            4);
    const unsigned int firstTexture =
        pipeline.GetOutputTexture();
    const bool rejectedInvalidReplacement =
        !pipeline.UploadOutputFromPixels(
            secondPixels.data(),
            1,
            1,
            5);
    const bool preservedAfterFailure =
        firstTexture != 0 &&
        pipeline.GetOutputTexture() == firstTexture &&
        pipeline.GetCanvasWidth() == 2 &&
        pipeline.GetCanvasHeight() == 2 &&
        glIsTexture(firstTexture) == GL_TRUE;
    const bool secondUploaded =
        pipeline.UploadOutputFromPixels(
            secondPixels.data(),
            1,
            1,
            4);
    const unsigned int secondTexture =
        pipeline.GetOutputTexture();
    const bool replacementCommitted =
        secondTexture != 0 &&
        secondTexture != firstTexture &&
        pipeline.GetCanvasWidth() == 1 &&
        pipeline.GetCanvasHeight() == 1 &&
        glIsTexture(secondTexture) == GL_TRUE &&
        glIsTexture(firstTexture) == GL_FALSE;
    pipeline.ClearOutput();

    if (!firstUploaded ||
        !rejectedInvalidReplacement ||
        !preservedAfterFailure ||
        !secondUploaded ||
        !replacementCommitted) {
        std::cerr
            << "Transactional output-upload validation failed: "
               "a failed replacement did not preserve the prior "
               "texture or a valid replacement did not commit.\n";
        return false;
    }

    std::cout
        << "Transactional output-upload validation passed: failed "
           "replacement preserved the prior texture and valid "
           "replacement committed atomically.\n";
    return true;
}

bool ValidateRawViewportCalibration(GLFWwindow* sharedWindow) {
    EditorRenderWorker::Snapshot snapshot;
    snapshot.generation = 1;
    snapshot.width = 512; snapshot.height = 384;
    snapshot.channels = 4;
    snapshot.outputConnected = true;
    snapshot.rawRenderPurpose = RawRenderPurpose::ViewportCalibration;
    snapshot.previewMaxDimension = 512;
    snapshot.rawWorkspace.sourceKey = "validation-calibration";
    snapshot.rawWorkspace.fullFrameWidth = 512; snapshot.rawWorkspace.fullFrameHeight = 384;
    snapshot.rawWorkspace.hasRecipe = true;
    snapshot.rawWorkspace.analysisRequested = false;
    snapshot.rawWorkspace.recipe = RawRecipe::MakeDefaultRecipe("viewport-region-validation");
    auto source = PreviewNode(1, RenderGraphNodeKind::RawDevelopment, "validation:calibration/source");
    source.rawDevelopment.embeddedRawData = MakeViewportValidationRaw();
    source.rawDevelopment.recipe = snapshot.rawWorkspace.recipe;
    snapshot.graph.nodes.push_back(std::move(source));
    snapshot.graph.nodes.push_back(PreviewNode(2, RenderGraphNodeKind::Output, "validation:calibration/output"));
    snapshot.graph.links.push_back(RenderGraphLink { 1, EditorNodeGraph::kImageOutputSocketId,
        2, EditorNodeGraph::kImageInputSocketId });
    snapshot.graph.outputNodeId = 2;
    snapshot.graph.outputSocketId = EditorNodeGraph::kImageOutputSocketId;
    EditorRenderWorker worker;
    if (!worker.Initialize(sharedWindow) || !worker.Submit(snapshot)) return false;
    EditorRenderWorker::Result result;
    const bool measured = WaitForResult(worker, result) && result.success &&
        result.calibration.renderMs > 0.0 && result.calibration.startupMs > 0.0 &&
        result.calibration.native && result.mainGraphStats.rawStageCacheMisses > 0 &&
        result.outputTexture.texture == 0 && result.pixels.empty();
    snapshot.generation = 2;
    const bool queued = worker.Submit(snapshot);
    snapshot.generation = 3;
    snapshot.rawRenderPurpose = RawRenderPurpose::InteractivePresentation;
    const bool submitted = worker.Submit(snapshot);
    result = {};
    const bool foreground = WaitForResult(worker, result) && result.generation == 3 && result.success;
    ReleaseSharedResultResources(result);
    // A small visible patch of a camera-sized source must reuse its full
    // upstream dependency across repeated EV changes at a fixed resolution.
    snapshot.rawWorkspace.recipe.source.sourcePath = "viewport-large-validation";
    snapshot.width = snapshot.rawWorkspace.fullFrameWidth = 5496;
    snapshot.height = snapshot.rawWorkspace.fullFrameHeight = 3672;
    snapshot.rawWorkspace.recipe.rgbDenoise.enabled = false;
    snapshot.rawWorkspace.recipe.finishTone.layerJson["localBaselineEnabled"] = false;
    snapshot.rawWorkspace.recipe.finishTone.layerJson["foundationAdaptiveAssist"] = false;
    snapshot.graph.nodes[0].rawDevelopment.embeddedRawData = MakeViewportValidationRaw(5496,3672);
    snapshot.rawWorkspace.viewport = {{5496,3672,2200,1500,256,256},1.0,1};
    snapshot.telemetry.interactionActive = true;
    snapshot.rawWorkspace.editStage = Raw::ViewportStage::RawPlacement;
    snapshot.rawWorkspace.preferredCacheInputStage = Raw::ViewportStage::NeutralPlacement;
    snapshot.rawWorkspace.gpuWorkingBudgetBytes = 2ull*1024*1024*1024;
    snapshot.rawWorkspace.gpuCacheBudgetBytes = 1024ull*1024*1024;
    for (bool zoomed : {true,false}) {
    snapshot.rawWorkspace.viewport = zoomed ? Raw::ViewportRequest{{5496,3672,2200,1500,256,256},1.0,1}
        : Raw::ViewportRequest{{5496,3672,0,0,5496,3672},1.0,2};
    for (int edge : {512,2048,5496}) {
        snapshot.previewMaxDimension = edge;
        double total = 0, minimum = 1e9, maximum = 0;
        Raw::ViewportStageCosts costs {};
        Raw::ViewportTimingBank observedBank;
        double predictionError=0; int predictions=0;
        for (int frame = 0; frame < 8; ++frame) {
            ++snapshot.generation;
            snapshot.rawWorkspace.recipe.preToneExposureEv = 0.25f + frame * 0.01f;
            snapshot.graph.nodes[0].rawDevelopment.recipe = snapshot.rawWorkspace.recipe;
            if (!worker.Submit(snapshot) || !WaitForResult(worker,result) || !result.success) return false;
            const auto expected = Raw::ScaleViewportRegion(snapshot.rawWorkspace.viewport.visible,
                result.rawWorkspace.viewportRegion.fullWidth, result.rawWorkspace.viewportRegion.fullHeight);
            if (zoomed && (!expected.Valid() || result.outputTexture.width != expected.width || result.outputTexture.height != expected.height)) {
                std::cerr << "RAW worker ignored the requested visible area.\n";
                return false;
            }
            EditorRenderWorker::Result evidence;
            bool hasTiming=false;
            const auto timingDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while (!hasTiming && std::chrono::steady_clock::now()<timingDeadline) {
                while (worker.TryConsumeViewportTiming(evidence))
                    if (evidence.generation==snapshot.generation) { hasTiming=true; break; }
                if (!hasTiming) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (!hasTiming || evidence.telemetry.completedServiceMs<=0 || evidence.telemetry.gpuServiceMs<=0 ||
                evidence.outputTexture.texture || evidence.timingOutputWidth!=result.outputTexture.width) {
                std::cerr << "RAW asynchronous timing lost its completion, dimensions or numeric-only ownership.\n";
                return false;
            }
            if (frame > 0) {
                Raw::ViewportCalibrationSample observation;
                observation.edge=edge;
                observation.renderMs=observation.startupMs=evidence.telemetry.completedServiceMs;
                observation.stages=evidence.stageCosts;
                observation.firstMeasuredStage=evidence.firstMeasuredStage;
                for (std::size_t i=0;i<std::min(observation.firstMeasuredStage,observation.stages.size());++i) observation.stages[i]=0;
                observation.changingStage=std::size_t(snapshot.rawWorkspace.editStage);
                bool known=false;
                const auto first=Raw::ViewportStage(observation.firstMeasuredStage);
                const double predicted=observedBank.Cost(evidence.workloadKeys,edge,first,false,&known,1.0,8,observation.changingStage);
                if (known && predicted>0) { predictionError+=std::abs(predicted-observation.renderMs)/observation.renderMs; ++predictions; }
                observedBank.Record(evidence.workloadKeys,observation);
                total += evidence.telemetry.completedServiceMs;
                minimum = std::min(minimum,evidence.telemetry.completedServiceMs);
                maximum = std::max(maximum,evidence.telemetry.completedServiceMs);
                for (std::size_t i = 0; i < costs.size(); ++i) costs[i] += evidence.stageCosts[i]/7;
                if (result.mainGraphStats.rawGpuPreprocessDispatches || result.mainGraphStats.rawStageCacheHits == 0) {
                    std::cerr << "RAW repeated EV upstream reuse check failed: edge " << edge << ", frame " << frame
                        << ", zoomed " << zoomed << ", GPU preprocess dispatches " << result.mainGraphStats.rawGpuPreprocessDispatches
                        << ", RAW stage hits " << result.mainGraphStats.rawStageCacheHits
                        << ", image hits " << result.mainGraphStats.imageCacheHits
                        << ", image misses " << result.mainGraphStats.imageCacheMisses << ".\n";
                    return false;
                }
            }
            ReleaseSharedResultResources(result);
        }
        std::cout << "RAW repeated EV, source 5496x3672, " << (zoomed ? "visible 256x256" : "whole image") << ", edge " << edge
            << ": completed service mean/min/max " << total/7 << "/" << minimum << "/" << maximum << " ms; stages";
        for (double cost : costs) std::cout << " " << cost;
        std::cout << "; recent prediction mean absolute error " << (predictions ? predictionError*100/predictions : 0) << "%\n";
    }
    }
    RenderGraphNode downstream;
    downstream.nodeId=3; downstream.kind=RenderGraphNodeKind::TechnicalImage;
    downstream.technicalImageOperation=Stack::NodeMath::TechnicalImageOperation::Exposure;
    downstream.technicalExposureValue=0.25f;
    snapshot.graph.nodes.push_back(downstream);
    snapshot.graph.links={{1,"imageOut",3,"imageIn"},{3,"imageOut",2,"imageIn"}};
    snapshot.rawWorkspace.viewport.visible={};
    snapshot.previewMaxDimension=512;
    ++snapshot.generation;
    if (!worker.Submit(snapshot) || !WaitForResult(worker,result) || !result.success ||
        result.rawWorkspace.viewportRegion.Partial() || result.outputTexture.width!=512) {
        std::cerr << "Graph-backed RAW presentation did not retain its full output extent.\n";
        return false;
    }
    ReleaseSharedResultResources(result);
    std::cout << "Graph-backed RAW complete-output render passed.\n";
    // Repeat the same checks through the unified operation adapter. The
    // technical source recipe stays neutral while Exposure owns its setting.
    snapshot.graph.nodes[0].rawDevelopment.recipe=RawRecipe::BuildTechnicalSourceRecipe(snapshot.rawWorkspace.recipe);
    snapshot.rawWorkspace.recipe=snapshot.graph.nodes[0].rawDevelopment.recipe;
    auto& graphExposure=snapshot.graph.nodes.back();
    graphExposure.kind=RenderGraphNodeKind::RawOperation;
    graphExposure.rawOperation=RawRecipe::MakeGraphOperation(RawRecipe::GraphOperationKind::Exposure);
    snapshot.graph.rawLayerBackgroundNodeId=1;
    snapshot.rawWorkspace.preferredCacheInputStage=Raw::ViewportStage::RawPlacement;
    for (int edge : {512,1024,5496}) {
        snapshot.previewMaxDimension=edge;
        double completeMs=0;
        for (int frame=0;frame<4;++frame) {
            ++snapshot.generation;
            graphExposure.rawOperation.parameters["ev"]=.5f+frame*.05f;
            snapshot.rawWorkspace.graphWorkloadKeys=Renderer::BuildRawGraphViewportWorkloadKeys(
                snapshot.rawWorkspace.recipe,snapshot.graph,3);
            if (!worker.Submit(snapshot) || !WaitForResult(worker,result) || !result.success ||
                result.outputTexture.width!=edge || result.rawWorkspace.viewportRegion.Partial() ||
                (frame>0 && result.mainGraphStats.rawGpuPreprocessDispatches)) {
                std::cerr << "Unified Exposure did not reuse the prepared source, edge " << edge
                    << ", frame " << frame << ", preprocessing dispatches "
                    << result.mainGraphStats.rawGpuPreprocessDispatches << "\n";
                return false;
            }
            EditorRenderWorker::Result evidence;
            bool ready=false;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while (!ready && std::chrono::steady_clock::now()<deadline) {
                while (worker.TryConsumeViewportTiming(evidence))
                    if (evidence.generation==snapshot.generation) {ready=true;break;}
                if (!ready) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (!ready || evidence.workloadKeys!=snapshot.rawWorkspace.graphWorkloadKeys ||
                evidence.telemetry.completedServiceMs<=0) {
                std::cerr << "Unified graph completion lost its complete workload timing.\n";
                return false;
            }
            if (frame>0) completeMs+=evidence.telemetry.completedServiceMs;
            ReleaseSharedResultResources(result);
        }
        std::cout << "Unified Exposure, source 5496x3672, edge " << edge
            << ": completed service mean " << completeMs/3 << " ms.\n";
    }
    worker.Shutdown();
    if (!measured || !queued || !submitted || !foreground) {
        std::cerr << "RAW viewport calibration validation failed.\n";
        return false;
    }
    std::cout << "RAW viewport calibration GPU validation passed.\n";
    return ValidateRawViewportInteraction(sharedWindow);
}

} // namespace Stack::Validation
