#include "App/Validation/Suites/EditorRenderWorkerPreviewValidation.h"

#include "Editor/EditorRenderWorker.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Renderer/GLLoader.h"
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

} // namespace Stack::Validation
