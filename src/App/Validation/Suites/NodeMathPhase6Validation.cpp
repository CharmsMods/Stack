#include "App/Validation/Suites/ProjectGraphSnapshotValidation.h"
#include "App/Validation/ValidationSuites.h"
#include "App/Validation/Suites/EditorRenderWorkerPreviewValidation.h"
#include "App/Validation/Suites/NodeMathMultiTreeValidation.h"
#include "Async/TaskSystem.h"

#include "Editor/EditorModule.h"
#include "NodeMath/TechnicalImageMath.h"
#include "NodeMath/GeometryMath.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLLoader.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/RenderTiling.h"
#include "Utils/SharedPixelBuffer.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

#ifndef GL_RG
#define GL_RG 0x8227
#endif

bool Phase6Check(bool condition, const std::string& message) {
    if (!condition) std::cerr << "Phase 6 validation failed: " << message << '\n';
    return condition;
}

std::vector<unsigned char> GeneratePhase6Pixels(int width, int height) {
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) * 4u;
            pixels[index + 0] = static_cast<unsigned char>((x * 17 + y * 3) & 255);
            pixels[index + 1] = static_cast<unsigned char>((x * 5 + y * 29) & 255);
            pixels[index + 2] = static_cast<unsigned char>(((x ^ y) * 11) & 255);
            pixels[index + 3] = static_cast<unsigned char>(32 + ((x * 7 + y * 13) % 224));
        }
    }
    return pixels;
}

std::vector<unsigned char> CropPhase6Pixels(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    const RenderTileRect& tile) {
    if (width <= 0 || height <= 0 || tile.haloWidth <= 0 || tile.haloHeight <= 0) return {};
    std::vector<unsigned char> result(
        static_cast<std::size_t>(tile.haloWidth) *
        static_cast<std::size_t>(tile.haloHeight) * 4u);
    for (int y = 0; y < tile.haloHeight; ++y) {
        const int sourceY = tile.haloY + y;
        for (int x = 0; x < tile.haloWidth; ++x) {
            const int sourceX = tile.haloX + x;
            const std::size_t sourceIndex =
                (static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(sourceX)) * 4u;
            const std::size_t destinationIndex =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(tile.haloWidth) +
                 static_cast<std::size_t>(x)) * 4u;
            std::copy_n(pixels.data() + sourceIndex, 4, result.data() + destinationIndex);
        }
    }
    return result;
}

RenderGraphSnapshot BuildPhase6Graph(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    float blurAmount) {
    RenderGraphSnapshot graph;
    graph.outputNodeId = 4;

    RenderGraphNode source;
    source.nodeId = 1;
    source.kind = RenderGraphNodeKind::Image;
    source.definitionId = "stack:graph/image";
    source.image.pixels = MakeSharedPixelBufferCopy(pixels);
    source.image.width = width;
    source.image.height = height;
    source.image.channels = 4;

    RenderGraphNode blur;
    blur.nodeId = 2;
    blur.kind = RenderGraphNodeKind::Layer;
    blur.definitionId = "stack:layer/gaussianblur";
    blur.layerJson = {
        { "type", "GaussianBlur" },
        { "amount", blurAmount }
    };

    RenderGraphNode exposure;
    exposure.nodeId = 3;
    exposure.kind = RenderGraphNodeKind::TechnicalImage;
    exposure.definitionId = "stack:technical/exposure";
    exposure.technicalImageOperation = NodeMath::TechnicalImageOperation::Exposure;
    exposure.technicalExposureValue = 0.5f;

    RenderGraphNode output;
    output.nodeId = 4;
    output.kind = RenderGraphNodeKind::Output;
    output.definitionId = "stack:graph/output";

    graph.nodes = { std::move(source), std::move(blur), std::move(exposure), std::move(output) };
    graph.links = {
        { 1, "imageOut", 2, "imageIn" },
        { 2, "imageOut", 3, "imageIn" },
        { 3, "imageOut", 4, "imageIn" }
    };
    return graph;
}

std::vector<unsigned char> GeneratePhase6BPixels(int width, int height) {
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) * 4u;
            pixels[index + 0] = ((x + y * width) & 1) == 0 ? 0 : 255;
            pixels[index + 1] = 64;
            pixels[index + 2] = 192;
            pixels[index + 3] = 255;
        }
    }
    return pixels;
}

bool BuildAuthoredPhase6BGraph(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    bool measuredExposure,
    RenderGraphSnapshot& snapshot,
    std::string& error) {
    EditorNodeGraph::Graph graph;
    graph.Clear();
    EditorNodeGraph::ImagePayload source;
    source.label = "Phase 6B Authored Source";
    source.width = width;
    source.height = height;
    source.channels = 4;
    source.originalChannels = 4;
    source.pixels = pixels;
    const int sourceId = graph.AddImageNode(std::move(source), { 0.0f, 0.0f })->id;
    const int splitId = measuredExposure
        ? graph.AddChannelSplitNode({ 260.0f, 80.0f })->id : -1;
    const int meanId = measuredExposure
        ? graph.AddFieldMeanNode({ 520.0f, 160.0f })->id : -1;
    const int constantId = !measuredExposure
        ? graph.AddValueNode(NodeMath::MakeUniformScalar(0.5), { 520.0f, 160.0f })->id : -1;
    const int exposureId = graph.AddTechnicalImageNode(
        NodeMath::TechnicalImageOperation::Exposure, { 780.0f, 0.0f })->id;
    const int outputId = graph.AddOutputNode({ 1040.0f, 0.0f }, true)->id;
    if (!graph.TryConnectSockets(
            sourceId, EditorNodeGraph::kImageOutputSocketId,
            exposureId, EditorNodeGraph::kImageInputSocketId, &error) ||
        !graph.TryConnectSockets(
            exposureId, EditorNodeGraph::kImageOutputSocketId,
            outputId, EditorNodeGraph::kImageInputSocketId, &error)) {
        return false;
    }
    if (measuredExposure) {
        if (!graph.TryConnectSockets(
                sourceId, EditorNodeGraph::kImageOutputSocketId,
                splitId, EditorNodeGraph::kImageInputSocketId, &error) ||
            !graph.TryConnectSockets(
                splitId, "r", meanId, EditorNodeGraph::kReductionFieldInputSocketId, &error) ||
            !graph.TryConnectSockets(
                meanId, EditorNodeGraph::kValueOutputSocketId,
                exposureId, EditorNodeGraph::kExposureValueInputSocketId, &error)) {
            return false;
        }
    } else if (!graph.TryConnectSockets(
            constantId, EditorNodeGraph::kValueOutputSocketId,
            exposureId, EditorNodeGraph::kExposureValueInputSocketId, &error)) {
        return false;
    }
    if (!graph.IsOutputConnected() || !graph.Validate().valid) {
        error = "The authored Field Mean graph is not a valid connected output.";
        return false;
    }
    EditorModule editor;
    editor.GetNodeGraph() = std::move(graph);
    snapshot = editor.BuildGraphSnapshot();
    snapshot.executionInspectionEnabled = true;
    return snapshot.outputNodeId == outputId && !snapshot.nodes.empty();
}

std::vector<float> ReadPhase6Texture(unsigned int texture, int width, int height) {
    if (texture == 0 || width <= 0 || height <= 0) return {};
    GLint previousFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    const unsigned int fbo = GLHelpers::CreateFBO(texture);
    if (fbo == 0) return {};
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    std::vector<float> result(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, result.data());
    const GLenum error = glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
    glDeleteFramebuffers(1, &fbo);
    return error == GL_NO_ERROR ? result : std::vector<float>{};
}

std::vector<float> RenderPhase6Graph(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    float blurAmount) {
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);
    pipeline.ExecuteGraph(BuildPhase6Graph(pixels, width, height, blurAmount));
    return ReadPhase6Texture(pipeline.GetOutputTexture(), width, height);
}

double MaximumDifference(
    const std::vector<float>& left,
    const std::vector<float>& right) {
    if (left.size() != right.size() || left.empty()) {
        return std::numeric_limits<double>::infinity();
    }
    double maximum = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        maximum = std::max(maximum,
            std::abs(static_cast<double>(left[index]) - static_cast<double>(right[index])));
    }
    return maximum;
}

std::vector<float> ReadPhase6ComplexTexture(
    unsigned int texture,
    int width,
    int height) {
    if (texture == 0 || width <= 0 || height <= 0) return {};
    GLint previousFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    const unsigned int fbo = GLHelpers::CreateFBO(texture);
    if (fbo == 0) return {};
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    std::vector<float> result(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) * 2u);
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, 0, width, height, GL_RG, GL_FLOAT, result.data());
    const GLenum error = glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
    glDeleteFramebuffers(1, &fbo);
    return error == GL_NO_ERROR ? result : std::vector<float>{};
}



bool RunPhase6BReductionValidation() {
    constexpr int width = 8;
    constexpr int height = 4;
    constexpr double rgba16fTolerance = 2.5e-3;
    const std::vector<unsigned char> source = GeneratePhase6BPixels(width, height);
    RenderGraphSnapshot measuredGraph;
    std::string authoredError;
    bool ok = Phase6Check(BuildAuthoredPhase6BGraph(
        source, width, height, true, measuredGraph, authoredError),
        authoredError.empty()
            ? "the authored Field Mean graph did not lower to a renderer snapshot"
            : authoredError);
    const RenderGraphRegionPlan plan = RenderTiling::PlanGraphRegions(
        measuredGraph, width, height);
    ok &= Phase6Check(
        plan.valid && plan.requiresFullFrame && !plan.tileable &&
        std::any_of(plan.stages.begin(), plan.stages.end(), [](const auto& stage) {
            return stage.capability == NodeMath::CapabilityClass::Reduction &&
                stage.definitionId == "stack:analysis/field-mean";
        }),
        "Field Mean was not planned as an explicit full-frame Reduction stage");

    RenderPipeline measuredPipeline;
    measuredPipeline.Initialize();
    measuredPipeline.LoadSourceFromPixels(source.data(), width, height, 4);
    measuredPipeline.ExecuteGraph(measuredGraph);
    const std::vector<float> measured = ReadPhase6Texture(
        measuredPipeline.GetOutputTexture(), width, height);
    const GraphExecutionStats firstStats = measuredPipeline.GetLastGraphExecutionStats();
    ok &= Phase6Check(!measured.empty(),
        "the authored Field Mean to Exposure graph produced no output");
    ok &= Phase6Check(
        firstStats.reductionPasses == 1 &&
        firstStats.reductionCacheMisses == 1 &&
        firstStats.reductions.size() == 1 &&
        std::abs(firstStats.reductions.front().value - 0.5) <= 1e-12 &&
        firstStats.reductions.front().sampleCount ==
            static_cast<std::uint64_t>(width * height),
        "Field Mean did not report the exact 0.5 full-population result");

    RenderGraphSnapshot cacheProbe = measuredGraph;
    for (RenderGraphNode& node : cacheProbe.nodes) {
        if (node.kind == RenderGraphNodeKind::TechnicalImage &&
            node.technicalImageOperation == NodeMath::TechnicalImageOperation::Exposure) {
            node.semanticDescriptorIdentity += ":downstream-cache-probe";
        }
    }
    measuredPipeline.ExecuteGraph(cacheProbe);
    const std::vector<float> cacheProbeOutput = ReadPhase6Texture(
        measuredPipeline.GetOutputTexture(), width, height);
    const GraphExecutionStats cacheStats = measuredPipeline.GetLastGraphExecutionStats();
    ok &= Phase6Check(
        !cacheProbeOutput.empty() && cacheStats.reductionPasses == 0 &&
        cacheStats.reductionCacheHits >= 1 &&
        !cacheStats.reductions.empty() && cacheStats.reductions.front().cacheHit,
        "an unchanged Field Mean input was not reused after a downstream cache invalidation");

    RenderPipeline constantPipeline;
    constantPipeline.Initialize();
    constantPipeline.LoadSourceFromPixels(source.data(), width, height, 4);
    RenderGraphSnapshot constantGraph;
    authoredError.clear();
    ok &= Phase6Check(BuildAuthoredPhase6BGraph(
        source, width, height, false, constantGraph, authoredError),
        authoredError.empty()
            ? "the authored constant reference did not lower to a renderer snapshot"
            : authoredError);
    constantPipeline.ExecuteGraph(constantGraph);
    const std::vector<float> constant = ReadPhase6Texture(
        constantPipeline.GetOutputTexture(), width, height);
    const double difference = MaximumDifference(measured, constant);
    ok &= Phase6Check(
        !constant.empty() && difference <= rgba16fTolerance,
        "Field Mean-driven Exposure differs from constant EV 0.5 by " +
            std::to_string(difference));
    if (ok) {
        std::cout << "Phase 6B reduction validation passed: Field Mean = 0.5 from "
                  << (width * height) << " samples, persistent cache reused, "
                  << "constant-EV max difference " << difference << ".\n";
    }
    return ok;
}

bool BuildAuthoredReformatGraph(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    int outputWidth,
    int outputHeight,
    NodeMath::ReconstructionFilter filter,
    RenderGraphSnapshot& snapshot,
    std::string& error) {
    EditorNodeGraph::Graph graph;
    graph.Clear();
    EditorNodeGraph::ImagePayload source;
    source.label = "Phase 6 Geometry Source";
    source.width = width;
    source.height = height;
    source.channels = 4;
    source.originalChannels = 4;
    source.pixels = pixels;
    const int sourceId = graph.AddImageNode(std::move(source), { 0.0f, 0.0f })->id;
    EditorNodeGraph::Node* reformat = graph.AddReformatNode({ 280.0f, 0.0f });
    if (!reformat) {
        error = "Could not create the authored Reformat node.";
        return false;
    }
    reformat->reformatSettings.width = outputWidth;
    reformat->reformatSettings.height = outputHeight;
    reformat->reformatSettings.filter = filter;
    reformat->reformatSettings.border = NodeMath::BorderPolicy::Clamp;
    reformat->reformatSettings.canvasRatio = 7;
    reformat->reformatSettings.customCanvasAspect = float(outputWidth) / outputHeight;
    reformat->reformatSettings.visibleAspect = 2.39f;
    reformat->reformatSettings.sideBars = 0.03f;
    reformat->reformatSettings.topBottomBars = 0.06f;
    reformat->reformatSettings.sourceScale = 1.2f;
    reformat->reformatSettings.sourceX = 0.3f;
    reformat->reformatSettings.sourceY = -0.4f;
    const auto authoredFraming = reformat->reformatSettings;
    const int reformatId = reformat->id;
    const int exposureId = graph.AddTechnicalImageNode(
        NodeMath::TechnicalImageOperation::Exposure, { 560.0f, 0.0f })->id;
    if (EditorNodeGraph::Node* exposure = graph.FindNode(exposureId)) {
        exposure->technicalImageSettings.exposureValue = 0.25f;
    }
    const int outputId = graph.AddOutputNode({ 840.0f, 0.0f }, true)->id;
    if (!graph.TryConnectSockets(
            sourceId, EditorNodeGraph::kImageOutputSocketId,
            reformatId, EditorNodeGraph::kImageInputSocketId, &error)) {
        if (error.empty()) error = "Image -> Reformat connection was rejected.";
        return false;
    }
    if (!graph.TryConnectSockets(
            reformatId, EditorNodeGraph::kImageOutputSocketId,
            exposureId, EditorNodeGraph::kImageInputSocketId, &error)) {
        if (error.empty()) error = "Reformat -> Exposure connection was rejected.";
        return false;
    }
    if (!graph.TryConnectSockets(
            exposureId, EditorNodeGraph::kImageOutputSocketId,
            outputId, EditorNodeGraph::kImageInputSocketId, &error)) {
        if (error.empty()) error = "Exposure -> Output connection was rejected.";
        return false;
    }
    const EditorNodeGraph::ValidationResult validation = graph.Validate();
    if (!graph.IsOutputConnected() || !validation.valid) {
        error = "The authored Reformat chain is not a valid connected output";
        for (const std::string& message : validation.messages) {
            error += ": " + message;
        }
        return false;
    }

    const nlohmann::json saved = EditorNodeGraph::SerializeGraphPayload(
        nlohmann::json::array(), graph);
    EditorNodeGraph::Graph restored;
    EditorNodeGraph::DeserializeGraphPayload(
        saved, restored, 0);
    const EditorNodeGraph::Node* restoredReformat = restored.FindNode(reformatId);
    if (!restoredReformat || restoredReformat->kind != EditorNodeGraph::NodeKind::Reformat ||
        !(restoredReformat->reformatSettings == authoredFraming) ||
        restoredReformat->reformatSettings.width != outputWidth ||
        restoredReformat->reformatSettings.height != outputHeight ||
        restoredReformat->reformatSettings.filter != filter ||
        !restoredReformat->definitionResolved) {
        error = "Reformat settings or exact definition identity did not survive project round-trip.";
        return false;
    }
    EditorModule editor;
    editor.GetNodeGraph() = std::move(restored);
    snapshot = editor.BuildGraphSnapshot();
    snapshot.executionInspectionEnabled = true;
    if (snapshot.outputNodeId != outputId || snapshot.nodes.empty()) {
        error = "Restored Reformat graph lowered with output " +
            std::to_string(snapshot.outputNodeId) + " instead of " +
            std::to_string(outputId) + " and " +
            std::to_string(snapshot.nodes.size()) + " renderer nodes.";
        return false;
    }
    return true;
}

bool RunPhase6GeometryValidation() {
    constexpr int width = 4;
    constexpr int height = 3;
    constexpr int outputWidth = 7;
    constexpr int outputHeight = 5;
    constexpr double rgba16fTolerance = 2.5e-3;
    const std::vector<unsigned char> source = GeneratePhase6Pixels(width, height);
    RenderGraphSnapshot graph;
    std::string error;
    const bool built = BuildAuthoredReformatGraph(
        source, width, height, outputWidth, outputHeight,
        NodeMath::ReconstructionFilter::Linear, graph, error);
    bool ok = Phase6Check(built,
        error.empty() ? "the authored Reformat graph did not lower" : error);
    if (!built) return false;
    const RenderGraphRegionPlan plan = RenderTiling::PlanGraphRegions(graph, width, height);
    ok &= Phase6Check(
        plan.valid && plan.requiresFullFrame && !plan.tileable &&
        plan.outputSpatial.fullWindow == NodeMath::Rect{ 0, 0, outputWidth, outputHeight } &&
        std::any_of(plan.stages.begin(), plan.stages.end(), [](const auto& stage) {
            return stage.capability == NodeMath::CapabilityClass::SampleResample &&
                stage.definitionId == "stack:geometry/reformat" &&
                stage.border == NodeMath::BorderPolicy::Clamp;
        }),
        "Reformat was not planned as an explicit extent-changing Sample/Resample boundary");
    ok &= Phase6Check(
        graph.outputDescriptor.spatial.state == NodeMath::KnowledgeState::Known &&
        graph.outputDescriptor.spatial.value.fullWindow ==
            NodeMath::Rect{ 0, 0, outputWidth, outputHeight },
        "Reformat extent did not propagate through Exposure to the direct output descriptor");

    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.LoadSourceFromPixels(source.data(), width, height, 4);
    pipeline.ExecuteGraph(graph);
    const std::vector<float> rendered = ReadPhase6Texture(
        pipeline.GetOutputTexture(), outputWidth, outputHeight);
    ok &= Phase6Check(
        !rendered.empty() && pipeline.GetCanvasWidth() == outputWidth &&
        pipeline.GetCanvasHeight() == outputHeight,
        "the live authored Reformat chain did not publish its declared output extent");

    std::vector<float> normalized(source.size());
    std::transform(source.begin(), source.end(), normalized.begin(), [](unsigned char value) {
        return static_cast<float>(value) / 255.0f;
    });
    NodeMath::ReformatSettings referenceSettings;
    referenceSettings.width = outputWidth;
    referenceSettings.height = outputHeight;
    referenceSettings.filter = NodeMath::ReconstructionFilter::Linear;
    referenceSettings.border = NodeMath::BorderPolicy::Clamp;
    referenceSettings.canvasRatio = 7;
    referenceSettings.customCanvasAspect = float(outputWidth) / outputHeight;
    referenceSettings.visibleAspect = 2.39f;
    referenceSettings.sideBars = 0.03f;
    referenceSettings.topBottomBars = 0.06f;
    referenceSettings.sourceScale = 1.2f;
    referenceSettings.sourceX = 0.3f;
    referenceSettings.sourceY = -0.4f;
    std::vector<float> reference = NodeMath::ReformatRgbaReference(
        normalized, width, height, referenceSettings);
    const float exposureScale = std::exp2(0.25f);
    for (std::size_t index = 0; index + 3 < reference.size(); index += 4) {
        reference[index + 0] *= exposureScale;
        reference[index + 1] *= exposureScale;
        reference[index + 2] *= exposureScale;
    }
    const double difference = MaximumDifference(rendered, reference);
    ok &= Phase6Check(
        difference <= rgba16fTolerance,
        "live Reformat plus downstream Exposure differs from the CPU reference by " +
            std::to_string(difference));

    int previewWidth = 0;
    int previewHeight = 0;
    const auto preview = pipeline.GetPreviewPixels(previewWidth, previewHeight, 3);
    int scopeWidth = 0;
    int scopeHeight = 0;
    const auto scopes = pipeline.GetScopesPixels(scopeWidth, scopeHeight);
    int exportWidth = 0;
    int exportHeight = 0;
    const auto exported = pipeline.GetOutputPixels(exportWidth, exportHeight);
    int fullPreviewWidth=0, fullPreviewHeight=0;
    const auto fullPreview=pipeline.GetPreviewPixels(fullPreviewWidth,fullPreviewHeight,0);
    bool consumersMatch=fullPreview.size()==exported.size() && fullPreviewWidth==exportWidth && fullPreviewHeight==exportHeight;
    if(consumersMatch) for(int y=0;y<exportHeight;++y) for(int x=0;x<exportWidth*4;++x)
        consumersMatch &= fullPreview[std::size_t(y)*exportWidth*4+x] == exported[std::size_t(exportHeight-1-y)*exportWidth*4+x];
    ok &= Phase6Check(consumersMatch,"Framing preview and export pixels differ after accounting for readback row orientation");

    const GraphExecutionStats consumerStats = pipeline.GetLastGraphExecutionStats();
    ok &= Phase6Check(
        !preview.empty() && previewWidth == 3 && previewHeight == 2 &&
        !scopes.empty() && scopeWidth == outputWidth && scopeHeight == outputHeight &&
        !exported.empty() && exportWidth == outputWidth && exportHeight == outputHeight &&
        consumerStats.specializedBoundaries.size() >= 3 &&
        std::none_of(
            consumerStats.specializedBoundaries.begin(),
            consumerStats.specializedBoundaries.end(),
            [](const auto& boundary) { return boundary.changesGraphResult; }),
        "preview, scope, and export did not execute through typed non-mutating consumer boundaries");
    if (ok) {
        std::cout << "Phase 6 geometry validation passed: 4x3 -> 7x5 linear Reformat, "
                  << "project round-trip, downstream Exposure, and typed consumers "
                  << "(max difference " << difference << ").\n";
    }
    return ok;
}

bool RunPhase6BranchedExtentValidation() {
    constexpr int referenceWidth = 4;
    constexpr int referenceHeight = 3;
    constexpr int alternateWidth = 7;
    constexpr int alternateHeight = 5;
    constexpr int maskWidth = 2;
    constexpr int maskHeight = 2;

    RenderGraphSnapshot graph;
    graph.outputNodeId = 7;
    graph.executionInspectionEnabled = true;

    const auto makeImageNode = [](int nodeId, int width, int height) {
        RenderGraphNode node;
        node.nodeId = nodeId;
        node.kind = RenderGraphNodeKind::Image;
        node.definitionId = "stack:graph/image";
        node.image.width = width;
        node.image.height = height;
        node.image.channels = 4;
        node.image.pixels = MakeSharedPixelBufferCopy(
            GeneratePhase6Pixels(width, height));
        return node;
    };

    RenderGraphNode mask;
    mask.nodeId = 4;
    mask.kind = RenderGraphNodeKind::ImageToMask;
    mask.definitionId = "stack:mask/luminance";

    RenderGraphNode mix;
    mix.nodeId = 5;
    mix.kind = RenderGraphNodeKind::Mix;
    mix.definitionId = "stack:graph/mix";

    RenderGraphNode layer;
    layer.nodeId = 6;
    layer.kind = RenderGraphNodeKind::Layer;
    layer.definitionId = "stack:layer/brightness";
    layer.layerJson = {
        { "type", "Brightness" },
        { "brightness", 0.15f }
    };

    RenderGraphNode output;
    output.nodeId = 7;
    output.kind = RenderGraphNodeKind::Output;
    output.definitionId = "stack:graph/output";

    RenderGraphNode alternateReformat;
    alternateReformat.nodeId = 8;
    alternateReformat.kind = RenderGraphNodeKind::Reformat;
    alternateReformat.definitionId = "stack:geometry/reformat";
    alternateReformat.reformatSettings.width = referenceWidth;
    alternateReformat.reformatSettings.height = referenceHeight;

    RenderGraphNode maskReformat = alternateReformat;
    maskReformat.nodeId = 9;

    graph.nodes = {
        makeImageNode(1, referenceWidth, referenceHeight),
        makeImageNode(2, alternateWidth, alternateHeight),
        makeImageNode(3, maskWidth, maskHeight),
        std::move(mask),
        std::move(mix),
        std::move(layer),
        std::move(output),
        std::move(alternateReformat),
        std::move(maskReformat)
    };
    graph.links = {
        { 1, "imageOut", 5, "imageA" },
        { 2, "imageOut", 8, "imageIn" },
        { 8, "imageOut", 5, "imageB" },
        { 3, "imageOut", 4, "imageIn" },
        { 4, "maskOut", 9, "imageIn" },
        { 9, "imageOut", 5, "factor" },
        { 5, "imageOut", 6, "imageIn" },
        { 9, "imageOut", 6, "maskIn" },
        { 6, "imageOut", 7, "imageIn" }
    };

    RenderPipeline pipeline;
    pipeline.Initialize();
    const std::vector<unsigned char> fallback =
        GeneratePhase6Pixels(referenceWidth, referenceHeight);
    pipeline.LoadSourceFromPixels(
        fallback.data(), referenceWidth, referenceHeight, 4);
    const RenderGraphRegionPlan branchedPlan =
        RenderTiling::PlanGraphRegions(graph, referenceWidth, referenceHeight);
    bool ok = Phase6Check(
        branchedPlan.valid,
        "explicit Reformat nodes did not make the differently sized image and mask branches compatible: " +
            branchedPlan.reason);
    pipeline.ExecuteGraph(graph);
    std::vector<float> first = ReadPhase6Texture(
        pipeline.GetOutputTexture(), referenceWidth, referenceHeight);
    if (pipeline.GetOutputTexture() == 0) {
        for (const std::pair<int, const char*>& probe : {
                 std::pair<int, const char*>{ 1, "imageOut" },
                 std::pair<int, const char*>{ 2, "imageOut" },
                 std::pair<int, const char*>{ 4, "maskOut" },
                 std::pair<int, const char*>{ 5, "imageOut" },
                 std::pair<int, const char*>{ 6, "imageOut" } }) {
            RenderGraphSnapshot probeGraph = graph;
            probeGraph.outputNodeId = probe.first;
            probeGraph.outputSocketId = probe.second;
            RenderPipeline probePipeline;
            probePipeline.Initialize();
            probePipeline.LoadSourceFromPixels(
                fallback.data(), referenceWidth, referenceHeight, 4);
            probePipeline.ExecuteGraph(probeGraph);
            std::cerr << "Phase 6 branched extent probe node " << probe.first
                      << " produced texture " << probePipeline.GetOutputTexture()
                      << " at " << probePipeline.GetCanvasWidth() << 'x'
                      << probePipeline.GetCanvasHeight() << ".\n";
        }
    }
    ok &= Phase6Check(
        !first.empty() &&
        pipeline.GetCanvasWidth() == referenceWidth &&
        pipeline.GetCanvasHeight() == referenceHeight,
        "a differently sized B branch and factor/mask branch changed the Mix A reference canvas to " +
            std::to_string(pipeline.GetCanvasWidth()) + "x" +
            std::to_string(pipeline.GetCanvasHeight()) + " with output texture " +
            std::to_string(pipeline.GetOutputTexture()) + " and " +
            std::to_string(first.size()) + " readback samples");

    pipeline.ExecuteGraph(graph);
    std::vector<float> cached = ReadPhase6Texture(
        pipeline.GetOutputTexture(), referenceWidth, referenceHeight);
    ok &= Phase6Check(
        !cached.empty() &&
        pipeline.GetCanvasWidth() == referenceWidth &&
        pipeline.GetCanvasHeight() == referenceHeight &&
        MaximumDifference(first, cached) <= 1.0e-6,
        "branched extent selection changed after graph-cache reuse (published " +
            std::to_string(pipeline.GetCanvasWidth()) + "x" +
            std::to_string(pipeline.GetCanvasHeight()) + ", output texture " +
            std::to_string(pipeline.GetOutputTexture()) + ", " +
            std::to_string(cached.size()) + " readback samples)");
    if (ok) {
        std::cout << "Phase 6 branched extent validation passed: Mix A retained its "
                  << referenceWidth << 'x' << referenceHeight
                  << " canvas after explicit Reformat of 7x5 image and 2x2 mask branches.\n";
    }
    return ok;
}

bool RunPhase6ConnectedMaskFailureValidation() {
    constexpr int width = 4;
    constexpr int height = 3;
    const std::vector<unsigned char> source = GeneratePhase6Pixels(width, height);

    RenderGraphNode image;
    image.nodeId = 1;
    image.kind = RenderGraphNodeKind::Image;
    image.definitionId = "stack:graph/image";
    image.image.width = width;
    image.image.height = height;
    image.image.channels = 4;
    image.image.pixels = MakeSharedPixelBufferCopy(source);

    RenderGraphNode unavailableMask;
    unavailableMask.nodeId = 2;
    unavailableMask.kind = RenderGraphNodeKind::MaskUtility;
    unavailableMask.definitionId = "stack:mask/levels";

    RenderGraphNode layer;
    layer.nodeId = 3;
    layer.kind = RenderGraphNodeKind::Layer;
    layer.definitionId = "stack:layer/brightness";
    layer.layerJson = {
        { "type", "Brightness" },
        { "brightness", 0.5f }
    };

    RenderGraphNode output;
    output.nodeId = 4;
    output.kind = RenderGraphNodeKind::Output;
    output.definitionId = "stack:graph/output";

    RenderGraphSnapshot graph;
    graph.nodes = {
        std::move(image),
        std::move(unavailableMask),
        std::move(layer),
        std::move(output)
    };
    graph.links = {
        { 1, "imageOut", 3, "imageIn" },
        { 2, "maskOut", 3, "maskIn" },
        { 3, "imageOut", 4, "imageIn" }
    };
    graph.outputNodeId = 4;
    graph.outputSocketId = "imageOut";

    const RenderGraphRegionPlan plan =
        RenderTiling::PlanGraphRegions(graph, width, height);
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.LoadSourceFromPixels(source.data(), width, height, 4);
    pipeline.ExecuteGraph(graph);
    const std::vector<float> first =
        ReadPhase6Texture(pipeline.GetOutputTexture(), width, height);

    std::vector<float> reference(first.size(), 1.0f);
    for (std::size_t index = 0; index < reference.size(); index += 4u) {
        reference[index + 0u] =
            static_cast<float>(source[index + 0u]) / 255.0f;
        reference[index + 1u] =
            static_cast<float>(source[index + 1u]) / 255.0f;
        reference[index + 2u] =
            static_cast<float>(source[index + 2u]) / 255.0f;
        reference[index + 3u] =
            static_cast<float>(source[index + 3u]) / 255.0f;
    }

    bool ok = Phase6Check(
        plan.valid &&
        !first.empty() &&
        MaximumDifference(first, reference) <= 2.5e-3,
        "a Layer whose connected mask could not materialize applied its effect "
        "unmasked instead of preserving the input");
    pipeline.ExecuteGraph(graph);
    const std::vector<float> cached =
        ReadPhase6Texture(pipeline.GetOutputTexture(), width, height);
    ok &= Phase6Check(
        !cached.empty() &&
        MaximumDifference(first, cached) <= 1.0e-6,
        "connected-mask failure fallback changed after graph-cache reuse");
    if (ok) {
        std::cout
            << "Phase 6 connected-mask failure validation passed: an unavailable "
               "authored mask preserves the input instead of applying the effect globally.\n";
    }
    return ok;
}

bool RunPhase6CustomMaskPolygonValidation() {
    constexpr int width = 5;
    constexpr int height = 5;

    RenderCustomMaskObject triangle;
    triangle.id = 1;
    triangle.type = RenderCustomMaskObjectType::Polygon;
    triangle.operation = RenderCustomMaskOperation::Add;
    triangle.points = {
        { 0.1f, 0.1f },
        { 0.9f, 0.1f },
        { 0.5f, 0.9f }
    };
    RenderCustomMaskObject disabledIntersect;
    disabledIntersect.id = 2;
    disabledIntersect.type = RenderCustomMaskObjectType::Rectangle;
    disabledIntersect.operation =
        RenderCustomMaskOperation::Intersect;
    disabledIntersect.points = {
        { 0.0f, 0.0f },
        { 1.0f, 1.0f }
    };
    disabledIntersect.enabled = false;

    RenderGraphNode customMask;
    customMask.nodeId = 1;
    customMask.kind = RenderGraphNodeKind::CustomMask;
    customMask.definitionId = "stack:mask/custom";
    customMask.customMask.width = width;
    customMask.customMask.height = height;
    customMask.customMask.objects = {
        std::move(triangle),
        std::move(disabledIntersect)
    };

    RenderGraphSnapshot graph;
    graph.nodes = { std::move(customMask) };
    graph.outputNodeId = 1;
    graph.outputSocketId = "maskOut";

    const std::vector<unsigned char> source =
        GeneratePhase6Pixels(width, height);
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.LoadSourceFromPixels(source.data(), width, height, 4);
    pipeline.ExecuteGraph(graph);
    const std::vector<float> rendered =
        ReadPhase6Texture(pipeline.GetOutputTexture(), width, height);
    const auto redAt = [&](int x, int y) {
        return rendered[
            (static_cast<std::size_t>(y) *
                 static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)) *
            4u];
    };

    const bool ok = Phase6Check(
        rendered.size() ==
                static_cast<std::size_t>(width * height * 4) &&
        redAt(2, 2) >= 0.99f &&
        redAt(0, 0) <= 0.01f &&
        redAt(4, 4) <= 0.01f,
        "custom-mask polygon ray casting rejected its center or filled an "
        "outside corner when traversing a descending edge, or a disabled "
        "Intersect object modified the result");
    if (ok) {
        std::cout
            << "Phase 6 custom-mask polygon validation passed: ascending and "
               "descending edges classify the center and corners correctly.\n";
    }
    return ok;
}







bool RunPhase7ConstantChannelValidation() {
    constexpr int width = 7;
    constexpr int height = 5;
    constexpr float value = 0.375f;
    constexpr double tolerance = 2.5e-3;
    const std::vector<unsigned char> pixels =
        GeneratePhase6Pixels(width, height);

    RenderGraphNode source;
    source.nodeId = 1;
    source.kind = RenderGraphNodeKind::Image;
    source.definitionId = "stack:graph/image";
    source.image.pixels =
        MakeSharedPixelBufferCopy(pixels);
    source.image.width = width;
    source.image.height = height;
    source.image.channels = 4;

    RenderGraphNode split;
    split.nodeId = 2;
    split.kind = RenderGraphNodeKind::ChannelSplit;
    split.definitionId = "stack:graph/channel-split";

    RenderGraphNode constant;
    constant.nodeId = 3;
    constant.kind = RenderGraphNodeKind::ConstantChannel;
    constant.definitionId =
        "stack:graph/constant-channel";
    constant.constantChannelValue = value;

    RenderGraphNode output;
    output.nodeId = 4;
    output.kind = RenderGraphNodeKind::Output;
    output.definitionId = "stack:graph/output";
    output.outputChannelViewMode =
        NodeMath::OutputChannelViewMode::Neutral;

    RenderGraphSnapshot graph;
    graph.outputNodeId = output.nodeId;
    graph.outputSocketId =
        EditorNodeGraph::kImageInputSocketId;
    graph.executionInspectionEnabled = true;
    graph.nodes = {
        source,
        split,
        constant,
        output
    };
    graph.links = {
        {
            source.nodeId,
            EditorNodeGraph::kImageOutputSocketId,
            split.nodeId,
            EditorNodeGraph::kImageInputSocketId
        },
        {
            split.nodeId,
            "r",
            constant.nodeId,
            EditorNodeGraph::kMatchExtentInputSocketId
        },
        {
            constant.nodeId,
            EditorNodeGraph::kChannelOutputSocketId,
            output.nodeId,
            EditorNodeGraph::kImageInputSocketId
        }
    };

    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.LoadSourceFromPixels(
        pixels.data(),
        width,
        height,
        4);
    pipeline.ExecuteGraph(graph);
    const std::vector<float> rendered =
        ReadPhase6Texture(
            pipeline.GetOutputTexture(),
            width,
            height);
    bool ok = Phase6Check(
        !rendered.empty() &&
            pipeline.GetCanvasWidth() == width &&
            pipeline.GetCanvasHeight() == height,
        "Constant Channel did not materialize at its exact Match Extent dimensions");
    for (std::size_t index = 0;
         ok && index + 3u < rendered.size();
         index += 4u) {
        ok &= Phase6Check(
            std::abs(rendered[index + 0u] - value) <=
                    tolerance &&
                std::abs(rendered[index + 1u] - value) <=
                    tolerance &&
                std::abs(rendered[index + 2u] - value) <=
                    tolerance &&
                std::abs(rendered[index + 3u] - 1.0f) <=
                    tolerance,
            "Constant Channel samples were not exact neutral inspection values with opaque presentation alpha");
    }
    ok &= Phase6Check(
        pipeline.GetLastGraphExecutionStats()
                .lastSpecializedFailureNodeId < 0,
        "resolved Constant Channel published an unexpected execution failure");

    RenderGraphSnapshot unresolved = graph;
    unresolved.links.erase(
        unresolved.links.begin() + 1);
    RenderPipeline unresolvedPipeline;
    unresolvedPipeline.Initialize();
    unresolvedPipeline.LoadSourceFromPixels(
        pixels.data(),
        width,
        height,
        4);
    unresolvedPipeline.ExecuteGraph(unresolved);
    const GraphExecutionStats& failure =
        unresolvedPipeline.GetLastGraphExecutionStats();
    ok &= Phase6Check(
        unresolvedPipeline.GetOutputTexture() == 0 &&
            failure.lastSpecializedFailureNodeId ==
                constant.nodeId &&
            failure.lastSpecializedFailure.find(
                "Match Extent") != std::string::npos,
        "unresolved Constant Channel should fail typed execution without inventing a canvas");
    return ok;
}



bool RunPhase6ValidationWithContext() {
    constexpr int width = 530;
    constexpr int height = 270;
    constexpr float blurAmount = 3.9f;
    constexpr double rgba16fTolerance = 2.5e-3;
    const std::vector<unsigned char> source = GeneratePhase6Pixels(width, height);
    const RenderGraphSnapshot fullGraph = BuildPhase6Graph(source, width, height, blurAmount);
    const RenderGraphRegionPlan regionPlan = RenderTiling::PlanGraphRegions(
        fullGraph, width, height);
    bool ok = true;
    ok &= Phase6Check(
        regionPlan.valid && regionPlan.tileable &&
        regionPlan.requiredHaloX == 3 && regionPlan.requiredHaloY == 3,
        "the live Gaussian chain did not derive its exact three-pixel halo");

    const std::vector<float> full = RenderPhase6Graph(source, width, height, blurAmount);
    ok &= Phase6Check(!full.empty(), "full-frame Gaussian fixture produced no float output");

    ViewportTilingSettings settings;
    settings.mode = ViewportTilingMode::Always;
    settings.tileSize = 256;
    settings.haloPixels = 0;
    const std::vector<RenderTileRect> tiles = RenderTiling::PlanTiles(
        width,
        height,
        settings,
        regionPlan.requiredHaloX,
        regionPlan.requiredHaloY);
    ok &= Phase6Check(tiles.size() > 1, "the live equivalence fixture did not produce multiple tiles");

    std::vector<float> tiled(full.size(), 0.0f);
    const TileIterationResult iteration = RenderTiling::IterateTiles(
        tiles,
        {},
        [&](const RenderTileRect& tile, std::size_t) {
            const std::vector<unsigned char> tileSource =
                CropPhase6Pixels(source, width, height, tile);
            const std::vector<float> tileOutput = RenderPhase6Graph(
                tileSource, tile.haloWidth, tile.haloHeight, blurAmount);
            if (tileOutput.size() !=
                static_cast<std::size_t>(tile.haloWidth) *
                static_cast<std::size_t>(tile.haloHeight) * 4u) {
                return false;
            }
            for (int y = 0; y < tile.height; ++y) {
                const int localY = tile.y - tile.haloY + y;
                const int destinationY = tile.y + y;
                for (int x = 0; x < tile.width; ++x) {
                    const int localX = tile.x - tile.haloX + x;
                    const int destinationX = tile.x + x;
                    const std::size_t sourceIndex =
                        (static_cast<std::size_t>(localY) *
                         static_cast<std::size_t>(tile.haloWidth) +
                         static_cast<std::size_t>(localX)) * 4u;
                    const std::size_t destinationIndex =
                        (static_cast<std::size_t>(destinationY) *
                         static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(destinationX)) * 4u;
                    std::copy_n(
                        tileOutput.data() + sourceIndex,
                        4,
                        tiled.data() + destinationIndex);
                }
            }
            return true;
        });
    ok &= Phase6Check(
        iteration.status == TileIterationStatus::Completed &&
        iteration.completedTiles == tiles.size(),
        "the live tiled Gaussian fixture did not complete every planned tile");
    const double maximumDifference = MaximumDifference(full, tiled);
    ok &= Phase6Check(
        maximumDifference <= rgba16fTolerance,
        "full-frame and tiled Gaussian outputs differ by " +
            std::to_string(maximumDifference) +
            ", above the RGBA16F tolerance");
    ok &= RunPhase6BReductionValidation();
    ok &= RunPhase6GeometryValidation();
    ok &= RunPhase6BranchedExtentValidation();
    ok &= RunPhase6ConnectedMaskFailureValidation();
    ok &= RunPhase6CustomMaskPolygonValidation();
    ok &= RunPhase7ConstantChannelValidation();
    ok &= ValidateNodeMathMultiTreeWithCurrentContext();
    ok &= ValidateEditorRenderWorkerPreviewBatch(
        glfwGetCurrentContext());
    ok &= ValidateEditorRenderWorkerTileBatch(
        glfwGetCurrentContext());
    ok &= ValidateTransactionalOutputUpload();
    ok &= ValidateRawViewportCalibration(glfwGetCurrentContext());
    ok &= ValidateEditorGraphTransactions();
    if (ok) {
        std::cout << "Phase 6 live validation passed: pointwise, geometry, neighborhood, "
                     "reduction and typed consumers share "
                     "the planner/diagnostic framework (Gaussian max difference "
                  << maximumDifference << ").\n";
    }
    return ok;
}

} // namespace

bool ValidateNodeMathPhase6Integration(bool calibrationOnly, bool transactionsOnly, bool framingOnly) {
    if (!glfwInit()) {
        std::cerr << "Phase 6 validation failed: glfwInit failed.\n";
        return false;
    }
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(64, 64, "Stack Phase 6 Validation", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Phase 6 validation failed: hidden OpenGL context creation failed.\n";
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) {
        std::cerr << "Phase 6 validation failed: OpenGL function loading failed.\n";
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return false;
    }
    if (transactionsOnly) ValidateProjectGraphSnapshotBuilder();
    bool result = false;
    if (framingOnly) {
        result = RunPhase6GeometryValidation();
        result &= RunPhase6BranchedExtentValidation();
        if (result) std::cout << "Framing geometry, serialization, preview/export and branch extent checks passed.\n";
    }
    else if (calibrationOnly) {
        result = ValidateRawViewportCalibration(window);
        // Worker lifetime checks may change the current context. Direct
        // pipeline checks must explicitly restore their owning context.
        glfwMakeContextCurrent(window);
        if (!result) std::cerr << "RAW viewport worker validation failed.\n";
        if (result) {
            result = ValidateRawViewportRegions();
            if (!result) std::cerr << "RAW viewport region validation failed.\n";
        }
        if (result) {
            result = ValidateRawViewportTransitions();
            if (!result) std::cerr << "RAW viewport transition validation failed.\n";
        }
    } else result = transactionsOnly ? ValidateEditorGraphTransactions() : RunPhase6ValidationWithContext();
    // Save checks can start shared catalog work as well as project-owned jobs.
    // Stop that work while store registries and the GL context still exist.
    Async::TaskSystem::Get().Shutdown();
    glfwMakeContextCurrent(nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}

} // namespace Stack::Validation
