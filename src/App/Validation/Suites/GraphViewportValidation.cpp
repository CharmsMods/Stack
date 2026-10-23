#include "App/Validation/Suites/GraphRenderingValidation.h"
#include "App/Validation/Suites/RawViewportValidationFixture.h"
#include "Editor/EditorRenderWorker.h"
#include "Project/GraphImageContracts.h"
#include "Raw/RawGraphOperation.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/RenderTiling.h"
#include "Renderer/GLStateGuards.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace Stack::Validation {
namespace {
using namespace Stack::Renderer;

void RequireViewport(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

RenderGraphSnapshot ViewportGraph() {
    constexpr int width = 96, height = 64;
    std::vector<unsigned char> pixels(width * height * 4);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto i = std::size_t(y * width + x) * 4;
        pixels[i] = static_cast<unsigned char>((x * 7 + y * 3) % 251);
        pixels[i+1] = static_cast<unsigned char>((x * 2 + y * 11) % 239);
        pixels[i+2] = 90; pixels[i+3] = 255;
    }
    RenderGraphSnapshot graph;
    graph.nodes.resize(4);
    for (int i = 0; i < 4; ++i) graph.nodes[i].nodeId = i + 1;
    graph.nodes[0].kind = RenderGraphNodeKind::Image;
    graph.nodes[0].image = {MakeSharedPixelBufferOwned(std::move(pixels)), width, height, 4};
    graph.nodes[1].kind = RenderGraphNodeKind::Layer;
    graph.nodes[1].layerJson = {{"type", "GaussianBlur"}, {"amount", 3.f}};
    graph.nodes[2].kind = RenderGraphNodeKind::TechnicalImage;
    graph.nodes[2].technicalExposureValue = 0.25f;
    graph.nodes[3].kind = RenderGraphNodeKind::Output;
    graph.links = {{1,"imageOut",2,"imageIn"}, {2,"imageOut",3,"imageIn"}, {3,"imageOut",4,"imageIn"}};
    graph.outputNodeId = 4; graph.outputSocketId = "imageOut";
    return graph;
}

void CompareViewportCrop(RenderPipeline& pipeline, const RenderGraphSnapshot& graph,
    const std::vector<float>& full, int fullWidth, int fullHeight, const ImageRegion& requested,
    int logicalWidth, int logicalHeight) {
    ViewportPreviewRequest request;
    request.enabled = true; request.visible = requested;
    pipeline.SetGraphViewportRequest(request);
    pipeline.ExecuteGraph(graph);
    int width = 0, height = 0;
    const auto cropped = pipeline.GetOutputFloatPixels(width, height);
    const auto geometry = pipeline.GetGraphViewportGeometry();
    const auto expected = ScaleImageRegion(requested, fullWidth, fullHeight);
    RequireViewport(geometry.Valid() && geometry.logicalWidth == logicalWidth && geometry.logicalHeight == logicalHeight &&
        geometry.resident == expected && width == expected.width && height == expected.height,
        "Viewport output lost its logical extent, origin, coverage or sampling grid");
    RequireViewport(cropped.size() == std::size_t(width) * height * 4,
        "Viewport readback did not return its resident pixels");
    // Float readbacks use the OpenGL bottom-left row order. Requests use
    // top-left image corners; preserve that conversion exactly once.
    const int bottom = fullHeight - expected.y - expected.height;
    float maximumError = 0;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) for (int c = 0; c < 4; ++c) {
        const float a = cropped[(std::size_t(y) * width + x) * 4 + c];
        const float b = full[(std::size_t(bottom + y) * fullWidth + expected.x + x) * 4 + c];
        RequireViewport(std::isfinite(a) && std::isfinite(b), "Viewport parity produced a non-finite sample");
        maximumError = std::max(maximumError, std::abs(a-b));
    }
    if (maximumError > 0.002f) {
        std::cerr << "Viewport crop maximum float error: " << maximumError << '\n';
        throw std::runtime_error("Regional pixels differ from the complete render on the same sampling lattice");
    }
}

void ValidateViewportGraph(RenderGraphSnapshot graph, int logicalWidth, int logicalHeight, bool cacheOrigins) {
    Project::BindGraphImageContracts(graph);
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.Resize(logicalWidth, logicalHeight);
    pipeline.SetRawDevelopmentAnalysisEnabled(false);
    pipeline.SetRawDevelopmentViewportValidationEnabled(false);
    pipeline.SetRawRgbDenoiseAsyncEnabled(false);
    pipeline.SetGraphCacheBudget(64ull * 1024 * 1024);
    for (const int edge : {0, logicalWidth / 2}) {
        pipeline.SetPreviewMaxDimension(edge);
        ViewportPreviewRequest complete; complete.enabled = true;
        pipeline.SetGraphViewportRequest(complete);
        pipeline.ExecuteGraph(graph);
        int width = 0, height = 0;
        const auto full = pipeline.GetOutputFloatPixels(width, height);
        RequireViewport(!full.empty() && pipeline.GetGraphViewportGeometry().Valid(), "Complete viewport reference is unavailable");
        const ImageRegion visible{logicalWidth,logicalHeight,logicalWidth/5,logicalHeight/5,
            logicalWidth/3,logicalHeight/3};
        const auto plan = RenderTiling::PlanGraphViewportRegions(graph, logicalWidth, logicalHeight, complete, edge);
        RequireViewport(plan.valid && plan.logicalWidth == logicalWidth && plan.logicalHeight == logicalHeight,
            "Executable viewport plan disagrees with the logical output extent");
        CompareViewportCrop(pipeline, graph, full, width, height, visible, logicalWidth, logicalHeight);
        RequireViewport(pipeline.GetLastGraphExecutionStats().viewportRegionalStages > 0,
            "Visible viewport executed no regional shader stage");
        for (const auto& reduction : pipeline.GetLastGraphExecutionStats().reductions)
            RequireViewport(reduction.sampleCount == std::uint64_t(width) * height,
                "Visible cropping changed the full-grid reduction population");
        if (cacheOrigins) {
            RequireViewport(pipeline.WasGraphImageCacheHit(2,"imageOut"), "Panning reran a complete blur dependency");
            auto moved = visible; moved.x += logicalWidth/8; moved.y += logicalHeight/8;
            CompareViewportCrop(pipeline, graph, full, width, height, moved, logicalWidth, logicalHeight);
            RequireViewport(!pipeline.WasGraphImageCacheHit(3,"imageOut"), "Same-sized pan reused pixels from a different origin");
            CompareViewportCrop(pipeline, graph, full, width, height, visible, logicalWidth, logicalHeight);
            RequireViewport(pipeline.WasGraphImageCacheHit(3,"imageOut"), "Returning to a cached viewport lost its immutable origin");
        }
    }
    ViewportPreviewRequest empty; empty.enabled = true; empty.empty = true;
    pipeline.SetGraphViewportRequest(empty);
    pipeline.ExecuteGraph(graph);
    RequireViewport(!pipeline.GetOutputTexture() && !pipeline.GetGraphViewportGeometry().Valid(),
        "Empty visible demand computed or published a complete image");
}

void ValidateViewportPlans() {
    auto graph = ViewportGraph();
    ViewportPreviewRequest request; request.enabled = true; request.visible = {96,64,19,13,32,21};
    const auto plan = RenderTiling::PlanGraphViewportRegions(graph,96,64,request,48);
    RequireViewport(plan.valid && plan.stages.at({2,"imageOut"}).demand == WholeImageRegion(96,64) &&
        !plan.stages.at({2,"imageOut"}).regionalExecutable && plan.stages.at({3,"imageOut"}).demand == request.visible,
        "Blur must retain complete dependencies while its pointwise suffix renders visible demand");
    RequireViewport(plan.workingBytes > 0 && plan.regionalPixels < plan.completePixels,
        "Viewport resource estimate omitted complete dependencies or regional work");
    auto controlled = graph;
    RenderGraphNode value; value.nodeId = 5; value.kind = RenderGraphNodeKind::Value; value.scalarValue = .25;
    RenderGraphNode arithmetic; arithmetic.nodeId = 6; arithmetic.kind = RenderGraphNodeKind::DataMath;
    arithmetic.dataMathSettings.domain = NodeMath::ArithmeticDomain::Values;
    arithmetic.dataMathMode = RenderDataMathMode::Multiply; arithmetic.dataMathSettings.constantB = 2;
    controlled.nodes.push_back(value); controlled.nodes.push_back(arithmetic);
    controlled.links.push_back({5,"valueOut",6,"imageA"});
    controlled.links.push_back({6,"imageOut",3,"evIn"});
    const auto controlPlan = RenderTiling::PlanGraphViewportRegions(controlled,96,64,request,48);
    RequireViewport(controlPlan.valid && controlPlan.stages.count({5,"valueOut"}) &&
        controlPlan.stages.count({6,"imageOut"}) && controlPlan.completePixels == plan.completePixels &&
        controlPlan.regionalPixels == plan.regionalPixels && controlPlan.workingBytes == plan.workingBytes,
        "Uniform Value/arithmetic controls were charged as image outputs or cropped textures");
    auto reduced = graph;
    RenderGraphNode field; field.nodeId = 5; field.kind = RenderGraphNodeKind::ImageToMask;
    reduced.nodes.push_back(field); reduced.links.push_back({2,"imageOut",5,"imageIn"});
    const auto fullFieldPlan = RenderTiling::PlanGraphViewport(reduced,96,64,48,request,{{5,"maskOut"}});
    RenderGraphNode mean; mean.nodeId = 6; mean.kind = RenderGraphNodeKind::FieldMean;
    reduced.nodes.push_back(mean); reduced.links.push_back({5,"maskOut",6,"fieldIn"});
    reduced.links.push_back({6,"valueOut",3,"evIn"});
    const auto reductionPlan = RenderTiling::PlanGraphViewportRegions(reduced,96,64,request,48);
    RequireViewport(reductionPlan.valid && reductionPlan.stages.at({5,"maskOut"}).demand == WholeImageRegion(96,64) &&
        reductionPlan.completePixels == fullFieldPlan.completePixels &&
        reductionPlan.regionalPixels == fullFieldPlan.regionalPixels &&
        reductionPlan.workingBytes == fullFieldPlan.workingBytes && reductionPlan.workingBytes > plan.workingBytes,
        "Scalar reduction accounting omitted its complete field dependency or charged its value as a texture");
    auto scoped = graph; scoped.rawLayerScopeNodeId = 3; scoped.rawLayerScopeSocketId = "imageOut";
    const auto scopePlan = RenderTiling::PlanGraphViewportRegions(scoped,96,64,request,48);
    RequireViewport(scopePlan.stages.at({3,"imageOut"}).demand == WholeImageRegion(96,64) &&
        scopePlan.workingBytes > plan.workingBytes,
        "Authoritative scope consumers must join complete dependencies in the admission plan");
    graph.nodes[2].kind = RenderGraphNodeKind::Layer;
    graph.nodes[2].layerJson = {{"type","Brightness"},{"sharpening",0.2f}};
    RequireViewport(!RenderTiling::PlanGraphViewportRegions(graph,96,64,request,48).stages.at({3,"imageOut"}).regionalExecutable,
        "Legacy adjustment sharpening must remain a complete neighborhood boundary");
    graph.nodes[1].kind = RenderGraphNodeKind::RawOperation;
    graph.nodes[1].rawOperation = RawRecipe::MakeGraphOperation(RawRecipe::GraphOperationKind::LocalEv);
    RequireViewport(RenderTiling::PlanGraphViewportRegions(graph,96,64,request,48).stages.at({2,"imageOut"}).regionalExecutable,
        "Inactive authored RAW operations must preserve the regional pointwise suffix");
    auto local = RawRecipe::ReadGraphOperation(graph.nodes[1].rawOperation);
    local.localRange.enabled = true; local.localRange.points = {{-8,0},{0,0.3f},{6,0}};
    RawRecipe::WriteGraphOperation(graph.nodes[1].rawOperation,local);
    RequireViewport(!RenderTiling::PlanGraphViewportRegions(graph,96,64,request,48).stages.at({2,"imageOut"}).regionalExecutable,
        "Active Local EV must retain full coordinates and populations");
}
}

bool ValidateGraphViewportRendering() {
    ValidateViewportPlans();
    ValidateViewportGraph(ViewportGraph(),96,64,true);

    auto masked = ViewportGraph();
    masked.nodes[2].kind = RenderGraphNodeKind::Layer;
    masked.nodes[2].layerJson = {{"type","Brightness"},{"brightness",0.13f}};
    RenderGraphNode gradient; gradient.nodeId = 5; gradient.kind = RenderGraphNodeKind::MaskGenerator;
    gradient.maskKind = RenderMaskGeneratorKind::LinearGradient; gradient.maskCanvasSourceNodeId = 1;
    RenderGraphNode inverse; inverse.nodeId = 6; inverse.kind = RenderGraphNodeKind::MaskUtility;
    masked.nodes.push_back(gradient); masked.nodes.push_back(inverse);
    masked.links.push_back({5,"maskOut",6,"maskIn"}); masked.links.push_back({6,"maskOut",3,"maskIn"});
    ValidateViewportGraph(masked,96,64,false);

    auto reduced = ViewportGraph();
    RenderGraphNode field; field.nodeId = 5; field.kind = RenderGraphNodeKind::ImageToMask;
    RenderGraphNode mean; mean.nodeId = 6; mean.kind = RenderGraphNodeKind::FieldMean;
    reduced.nodes.push_back(field); reduced.nodes.push_back(mean);
    reduced.links.push_back({2,"imageOut",5,"imageIn"}); reduced.links.push_back({5,"maskOut",6,"fieldIn"});
    reduced.links.push_back({6,"valueOut",3,"evIn"});
    ValidateViewportGraph(reduced,96,64,false);

    auto cropped = ViewportGraph();
    auto cropRecipe = RawRecipe::MakeDefaultRecipe({});
    cropRecipe.cropRotation.cropEnabled = true; cropRecipe.cropRotation.cropX = 0.125f;
    cropRecipe.cropRotation.cropY = 0.25f; cropRecipe.cropRotation.cropWidth = 0.75f;
    cropRecipe.cropRotation.cropHeight = 0.5f;
    cropped.nodes[1].layerJson = {{"type","RawOutputCrop"},{"recipe",RawRecipe::SerializeRecipe(cropRecipe)}};
    Project::BindGraphImageContracts(cropped);
    RequireViewport(cropped.outputDescriptor.spatial.value.fullWindow.width == 72 &&
        cropped.outputDescriptor.spatial.value.fullWindow.height == 32,
        "Output crop semantic contract retained uncropped logical dimensions");
    ValidateViewportGraph(cropped,72,32,false);

    auto raw = ViewportGraph();
    raw.nodes[0].kind = RenderGraphNodeKind::RawDevelopment;
    raw.nodes[0].rawDevelopment.embeddedRawData = MakeViewportValidationRaw(96,64);
    raw.nodes[0].rawDevelopment.recipe = RawRecipe::MakeDefaultRecipe("viewport-region-validation");
    raw.nodes[0].rawDevelopment.recipe.rgbDenoise.enabled = false;
    raw.nodes[0].semanticDescriptor.color = NodeMath::SemanticField<NodeMath::ColorIdentity>::Known({"srgb-d65",{}});
    raw.nodes[0].semanticDescriptor.transfer = NodeMath::SemanticField<NodeMath::TransferDescriptor>::Known({NodeMath::TransferKind::Linear});
    raw.nodes[0].semanticDescriptor.reference = NodeMath::SemanticField<NodeMath::ReferenceState>::Known(NodeMath::ReferenceState::Scene);
    raw.nodes[1].kind = RenderGraphNodeKind::RawOperation;
    raw.nodes[1].rawOperation = RawRecipe::MakeGraphOperation(RawRecipe::GraphOperationKind::Exposure);
    auto exposure = RawRecipe::ReadGraphOperation(raw.nodes[1].rawOperation);
    exposure.preToneExposureEv = 0.4f;
    RawRecipe::WriteGraphOperation(raw.nodes[1].rawOperation,exposure);
    ValidateViewportGraph(raw,96,64,false);
    std::cout << "Graph/RAW visible-area planning, native/half-grid parity, mask/reduction boundaries, crop extents and pan cache origins passed.\n";
    return true;
}

namespace {

EditorRenderWorker::Result WaitViewportWorker(EditorRenderWorker& worker, bool timing = false) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    EditorRenderWorker::Result result;
    while (std::chrono::steady_clock::now() < deadline) {
        if (timing ? worker.TryConsumeViewportTiming(result) : worker.TryConsumeCompleted(result)) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error(timing ? "Viewport worker completion timing did not arrive" : "Viewport worker result did not arrive");
}

std::vector<float> ReadViewportTexture(const EditorRenderWorker::SharedTextureResult& image) {
    RequireViewport(image.texture && image.lease && image.lease.Texture() == image.texture && image.readyFence,
        "Viewport worker publication lacks an immutable lease or readiness fence");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    GLenum status = GL_TIMEOUT_EXPIRED;
    while (status == GL_TIMEOUT_EXPIRED && std::chrono::steady_clock::now() < deadline)
        status = glClientWaitSync(image.readyFence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000);
    RequireViewport(status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED,
        "Viewport worker publication fence failed to become ready");
    std::vector<float> pixels(std::size_t(image.width) * image.height * 4);
    const GLState::TextureBinding texture(GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D);
    const GLState::PixelPackState pack;
    pack.ConfigureTightCpuReadback();
    glBindTexture(GL_TEXTURE_2D, image.texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
    pack.Restore(); texture.Restore();
    RequireViewport(glGetError() == GL_NO_ERROR, "Viewport worker shared texture readback failed");
    return pixels;
}

void RequireViewportPixels(const std::vector<float>& actual, int width, int height,
    const std::vector<float>& expected, int fullWidth, int fullHeight, const ImageRegion& region) {
    RequireViewport(actual.size() == std::size_t(width) * height * 4 &&
        expected.size() == std::size_t(fullWidth) * fullHeight * 4 &&
        width == region.width && height == region.height, "Viewport worker parity has inconsistent dimensions");
    const int bottom = fullHeight - region.y - region.height;
    float error = 0;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) for (int c = 0; c < 4; ++c) {
        const float a = actual[(std::size_t(y) * width + x) * 4 + c];
        const float b = expected[(std::size_t(bottom + y) * fullWidth + region.x + x) * 4 + c];
        RequireViewport(std::isfinite(a) && std::isfinite(b), "Viewport worker publication contains non-finite pixels");
        error = std::max(error, std::abs(a - b));
    }
    RequireViewport(error <= 0.002f, "Viewport worker publication differs from its matching complete reference");
}

} // namespace

bool ValidateGraphViewportWorker(GLFWwindow* sharedWindow) {
    constexpr int width = 96, height = 64;
    auto graph = ViewportGraph();
    const auto source = graph.nodes[0].image.pixels;
    // Reuse the existing fixture through the supported implicit-source path.
    // The small preview installs a different fallback source before overview.
    graph.nodes[0].image.pixels = {};
    Project::BindGraphImageContracts(graph);
    RenderPipeline reference;
    reference.Initialize();
    reference.SetRawDevelopmentAnalysisEnabled(false);
    reference.SetRawDevelopmentViewportValidationEnabled(false);
    reference.LoadSourceFromSharedPixels(source, width, height, 4);
    ViewportPreviewRequest complete; complete.enabled = true;
    reference.SetGraphViewportRequest(complete);
    reference.ExecuteGraph(graph);
    int nativeWidth = 0, nativeHeight = 0;
    const auto native = reference.GetOutputFloatPixels(nativeWidth, nativeHeight);
    RequireViewport(nativeWidth == width && nativeHeight == height, "Viewport worker reference lost native dimensions");
    reference.SetPreviewMaxDimension(48);
    reference.ExecuteGraph(graph);
    int sampleWidth = 0, sampleHeight = 0;
    const auto proxy = reference.GetOutputFloatPixels(sampleWidth, sampleHeight);

    EditorRenderWorker worker;
    RequireViewport(worker.Initialize(sharedWindow), "Viewport worker could not initialize its shared context");
    EditorRenderWorker::Snapshot snapshot;
    snapshot.ownerId = 91; snapshot.generation = 1; snapshot.schedulingSerial = 1;
    snapshot.graphRequest = {true, 4, 0, false, 1, 9123, 1};
    snapshot.graph = graph; snapshot.sourcePixels = source;
    snapshot.width = width; snapshot.height = height; snapshot.channels = 4;
    snapshot.outputConnected = true; snapshot.previewMaxDimension = 48;
    snapshot.viewportTiling.mode = ViewportTilingMode::Off;
    snapshot.rawWorkspace.analysisRequested = false;
    snapshot.viewportPreview = {{width, height, 19, 13, 32, 21}, 0.5, 7, true, false};
    snapshot.viewportViewId = 2; snapshot.viewportContentIdentity = 77;
    snapshot.viewportExecutionIdentity = 88; snapshot.prepareViewportOverview = true;
    EditorRenderWorker::PreviewRequest preview;
    preview.previewNodeId = 5; preview.sourceNodeId = 1; preview.sourceSocketId = "imageOut";
    preview.directSourceOutput = true; preview.dirtyGeneration = 1;
    preview.width = 8; preview.height = 8; preview.channels = 4;
    preview.sourcePixels = MakeSharedPixelBufferOwned(std::vector<unsigned char>(source.bytes->begin(), source.bytes->begin() + 8 * 8 * 4));
    snapshot.previews = {preview};
    RequireViewport(worker.Submit(snapshot), "Viewport worker detail request was rejected");
    auto first = WaitViewportWorker(worker);
    const auto expected = ScaleImageRegion(snapshot.viewportPreview.visible, sampleWidth, sampleHeight);
    RequireViewport(first.success && first.viewportPreview.enabled && first.viewportViewId == 2 &&
        first.viewportContentIdentity == 77 && first.viewportExecutionIdentity == 88 && first.viewportPreview.generation == 7,
        "Viewport worker lost its enabled request or delivery identities");
    RequireViewport(first.viewportPreviewGeometry.Valid() && first.viewportPreviewGeometry.logicalWidth == width &&
        first.viewportPreviewGeometry.logicalHeight == height && first.viewportPreviewGeometry.resident == expected &&
        first.rawWorkspace.expectedNativeOutputWidth == width && first.rawWorkspace.expectedNativeOutputHeight == height &&
        first.mainGraphStats.viewportRegionalStages > 0 && first.previews.size() == 1 && first.previews.front().success,
        "An ordinary preview replaced the regional main plan or its logical canvas");
    RequireViewportPixels(ReadViewportTexture(first.outputTexture), first.outputTexture.width, first.outputTexture.height,
        proxy, sampleWidth, sampleHeight, expected);
    RequireViewport(first.viewportOverview.width == width && first.viewportOverview.height == height &&
        std::max(first.viewportOverview.width, first.viewportOverview.height) <= 512,
        "Viewport worker did not publish matching bounded complete coverage");
    RequireViewportPixels(ReadViewportTexture(first.viewportOverview), width, height,
        native, width, height, WholeImageRegion(width, height));
    auto timing = WaitViewportWorker(worker, true);
    RequireViewport(timing.timingOnly && timing.viewportViewId == first.viewportViewId &&
        timing.viewportContentIdentity == first.viewportContentIdentity && timing.viewportExecutionIdentity == first.viewportExecutionIdentity &&
        timing.viewportPreviewGeometry.resident == expected && std::isfinite(timing.telemetry.completedServiceMs) &&
        timing.telemetry.completedServiceMs > 0 && timing.telemetry.completedServiceMs + 1e-9 >= first.telemetry.workerTotalMs,
        "Viewport completion timing lost its geometry, identity or complete request cost");

    snapshot.generation = snapshot.schedulingSerial = snapshot.graphRequest.revision = 2;
    snapshot.rawRenderPurpose = RawRenderPurpose::ViewportRefinement;
    snapshot.previewMaxDimension = 0; snapshot.viewportPreview.samplingScale = 1;
    snapshot.prepareViewportOverview = false; snapshot.previews.clear();
    RequireViewport(worker.Submit(snapshot), "Viewport worker native refinement request was rejected");
    auto refined = WaitViewportWorker(worker);
    RequireViewport(refined.success && refined.viewportViewId == first.viewportViewId &&
        refined.viewportContentIdentity == first.viewportContentIdentity && refined.viewportPreviewGeometry.NativeSamples() &&
        refined.viewportPreviewGeometry.resident == snapshot.viewportPreview.visible,
        "Native refinement lost its resident coverage or per-view content identity");
    RequireViewportPixels(ReadViewportTexture(refined.outputTexture), refined.outputTexture.width, refined.outputTexture.height,
        native, width, height, snapshot.viewportPreview.visible);
    worker.Shutdown();
    RequireViewport(glIsTexture(first.outputTexture.texture) && glIsTexture(first.viewportOverview.texture),
        "Worker shutdown destroyed immutable published detail or overview coverage");
    std::cout << "Viewport worker cropped/proxy delivery, preview coexistence, matching overview, timing identity and native refinement passed.\n";
    return true;
}
}
