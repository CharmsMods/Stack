#include "App/Validation/ValidationSuites.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "NodeMath/PointwiseIR.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLLoader.h"
#include "Renderer/RenderPipeline.h"
#include "Utils/SharedPixelBuffer.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

using Stack::NodeMath::PointwiseOperation;
using Stack::NodeMath::PointwisePixel;
using Stack::NodeMath::PointwiseProgram;
using Stack::NodeMath::PointwiseSourceLocation;

class LegacyStateProbeLayer final : public LayerBase {
public:
    json Serialize() const override {
        return json::object();
    }

    void Deserialize(const json&) override {}
    const char* GetDefaultName() const override {
        return "Legacy State Probe";
    }
    const char* GetCategory() const override {
        return "Validation";
    }
    void InitializeGL() override {}
    void Execute(
        unsigned int,
        int,
        int,
        FullscreenQuad&) override {}
    void RenderUI() override {}
};

RenderGraphNode ImageNode(int id) {
    RenderGraphNode node;
    node.nodeId = id;
    node.kind = RenderGraphNodeKind::Image;
    node.definitionId = "stack:graph/image";
    return node;
}

RenderGraphNode DataMathNode(int id, RenderDataMathMode mode, float constantB) {
    RenderGraphNode node;
    node.nodeId = id;
    node.kind = RenderGraphNodeKind::DataMath;
    node.definitionId = "stack:graph/data-math/validation";
    node.dataMathMode = mode;
    node.dataMathSettings.constantA = 0.0f;
    node.dataMathSettings.constantB = constantB;
    return node;
}

RenderGraphNode TechnicalNode(
    int id,
    Stack::NodeMath::TechnicalImageOperation operation) {
    RenderGraphNode node;
    node.nodeId = id;
    node.kind = RenderGraphNodeKind::TechnicalImage;
    node.definitionId = "stack:graph/technical-image/validation";
    node.technicalImageOperation = operation;
    return node;
}

RenderGraphNode OutputNode(int id) {
    RenderGraphNode node;
    node.nodeId = id;
    node.kind = RenderGraphNodeKind::Output;
    node.definitionId = "stack:graph/output";
    return node;
}

RenderGraphLink Link(int fromNode, const char* fromSocket, int toNode, const char* toSocket) {
    RenderGraphLink link;
    link.fromNodeId = fromNode;
    link.fromSocketId = fromSocket;
    link.toNodeId = toNode;
    link.toSocketId = toSocket;
    return link;
}

RenderGraphSnapshot BuildArithmeticGraph(bool forceFanOut, float addValue, float multiplyValue) {
    RenderGraphSnapshot graph;
    graph.outputNodeId = 4;
    graph.outputSocketId = EditorNodeGraph::kImageOutputSocketId;
    graph.executionInspectionEnabled = true;
    graph.nodes = {
        ImageNode(1),
        DataMathNode(2, RenderDataMathMode::Add, addValue),
        DataMathNode(3, RenderDataMathMode::Multiply, multiplyValue),
        OutputNode(4)
    };
    graph.links = {
        Link(1, EditorNodeGraph::kImageOutputSocketId, 2, EditorNodeGraph::kMixInputASocketId),
        Link(2, EditorNodeGraph::kImageOutputSocketId, 3, EditorNodeGraph::kMixInputASocketId),
        Link(3, EditorNodeGraph::kImageOutputSocketId, 4, EditorNodeGraph::kImageInputSocketId)
    };
    if (forceFanOut) {
        graph.nodes.push_back(OutputNode(5));
        graph.links.push_back(Link(
            2,
            EditorNodeGraph::kImageOutputSocketId,
            5,
            EditorNodeGraph::kImageInputSocketId));
    }
    return graph;
}

std::vector<float> ReadTextureFloat(unsigned int texture, int width, int height) {
    if (texture == 0 || width <= 0 || height <= 0) return {};
    GLint previousReadFbo = 0;
    GLint previousDrawFbo = 0;
    GLint previousReadBuffer = 0;
    GLint previousDrawBuffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFbo);
    glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
    glGetIntegerv(GL_DRAW_BUFFER, &previousDrawBuffer);
    const unsigned int fbo = GLHelpers::CreateFBO(texture);
    if (fbo == 0) return {};
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    std::vector<float> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data());
    const GLenum error = glGetError();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFbo));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFbo));
    glReadBuffer(static_cast<GLenum>(previousReadBuffer));
    glDrawBuffer(static_cast<GLenum>(previousDrawBuffer));
    glDeleteFramebuffers(1, &fbo);
    return error == GL_NO_ERROR ? pixels : std::vector<float>{};
}

PointwiseProgram BuildArithmeticReference(float addValue, float multiplyValue) {
    PointwiseProgram program;
    const PointwiseSourceLocation source { 1, "stack:graph/image", {}, "imageOut" };
    const auto input = Stack::NodeMath::AppendPointwiseInput(program, source);
    const PointwiseSourceLocation addSource { 2, "stack:graph/data-math/add", "imageA", "imageOut" };
    const auto add = Stack::NodeMath::AppendPointwiseConstant(
        program, { addValue, addValue, addValue, addValue }, addSource);
    const auto added = Stack::NodeMath::AppendPointwiseOperation(
        program, PointwiseOperation::Add, { input, add }, addSource);
    const PointwiseSourceLocation multiplySource { 3, "stack:graph/data-math/multiply", "imageA", "imageOut" };
    const auto multiply = Stack::NodeMath::AppendPointwiseConstant(
        program, { multiplyValue, multiplyValue, multiplyValue, multiplyValue }, multiplySource);
    program.rootId = Stack::NodeMath::AppendPointwiseOperation(
        program, PointwiseOperation::Multiply, { added, multiply }, multiplySource);
    return program;
}

PointwiseProgram BuildReverseArithmeticReference(float multiplyValue, float addValue) {
    PointwiseProgram program;
    const PointwiseSourceLocation source { 1, "stack:graph/image", {}, "imageOut" };
    const auto input = Stack::NodeMath::AppendPointwiseInput(program, source);
    const PointwiseSourceLocation multiplySource {
        2, "stack:graph/data-math/multiply", "imageA", "imageOut" };
    const auto multiply = Stack::NodeMath::AppendPointwiseConstant(
        program, { multiplyValue, multiplyValue, multiplyValue, multiplyValue }, multiplySource);
    const auto multiplied = Stack::NodeMath::AppendPointwiseOperation(
        program, PointwiseOperation::Multiply, { input, multiply }, multiplySource);
    const PointwiseSourceLocation addSource {
        3, "stack:graph/data-math/add", "imageA", "imageOut" };
    const auto add = Stack::NodeMath::AppendPointwiseConstant(
        program, { addValue, addValue, addValue, addValue }, addSource);
    program.rootId = Stack::NodeMath::AppendPointwiseOperation(
        program, PointwiseOperation::Add, { multiplied, add }, addSource);
    return program;
}

double MaximumDifference(const std::vector<float>& left, const std::vector<float>& right) {
    if (left.size() != right.size() || left.empty()) return std::numeric_limits<double>::infinity();
    double maximum = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        maximum = std::max(maximum, std::abs(static_cast<double>(left[index]) - right[index]));
    }
    return maximum;
}

std::vector<float> EvaluateArithmeticReference(
    const std::vector<unsigned char>& pixels,
    const PointwiseProgram& program) {
    std::vector<float> result;
    result.reserve(pixels.size());
    std::string error;
    for (std::size_t index = 0; index < pixels.size(); index += 4u) {
        PointwisePixel input {
            pixels[index + 0] / 255.0,
            pixels[index + 1] / 255.0,
            pixels[index + 2] / 255.0,
            pixels[index + 3] / 255.0
        };
        PointwisePixel output {};
        if (!Stack::NodeMath::EvaluatePointwiseProgram(program, input, output, error)) return {};
        for (double component : output) result.push_back(static_cast<float>(component));
    }
    return result;
}

bool Check(bool condition, const std::string& message) {
    if (!condition) std::cerr << "Phase 4 validation failed: " << message << '\n';
    return condition;
}

bool ValidateExactDefinitionCacheInvalidation(
    const std::vector<unsigned char>& pixels,
    int width,
    int height) {
    bool ok = true;
    {
        RenderPipeline pipeline;
        pipeline.Initialize();
        pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);

        RenderGraphNode generator;
        generator.nodeId = 1;
        generator.kind = RenderGraphNodeKind::ImageGenerator;
        generator.definitionId = "stack:test/image-generator";
        generator.definitionVersion = "1.0.0";
        generator.definitionHash = "image-v1";
        generator.imageGeneratorKind = RenderImageGeneratorKind::SolidColor;

        RenderGraphSnapshot graph;
        graph.outputNodeId = generator.nodeId;
        graph.outputSocketId = EditorNodeGraph::kImageOutputSocketId;
        graph.executionInspectionEnabled = true;
        graph.nodes = { generator };

        pipeline.ExecuteGraph(graph);
        ok &= Check(
            pipeline.GetLastGraphExecutionStats().imageCacheMisses > 0,
            "first exact-definition image evaluation did not populate the persistent cache");
        pipeline.ExecuteGraph(graph);
        ok &= Check(
            pipeline.GetLastGraphExecutionStats().imageCacheHits > 0,
            "unchanged exact-definition image evaluation did not reuse the persistent cache");

        graph.nodes.front().definitionHash = "image-v2";
        pipeline.ExecuteGraph(graph);
        ok &= Check(
            pipeline.GetLastGraphExecutionStats().imageCacheMisses > 0 &&
                pipeline.GetLastGraphExecutionStats().imageCacheHits == 0,
            "changed exact-definition image identity reused stale cached pixels");
    }

    {
        RenderPipeline pipeline;
        pipeline.Initialize();
        pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);

        RenderGraphNode generator;
        generator.nodeId = 1;
        generator.kind = RenderGraphNodeKind::MaskGenerator;
        generator.definitionId = "stack:test/mask-generator";
        generator.definitionVersion = "1.0.0";
        generator.definitionHash = "mask-v1";
        generator.maskKind = RenderMaskGeneratorKind::Solid;
        generator.maskSettings.value = 0.5f;

        RenderGraphSnapshot graph;
        graph.outputNodeId = generator.nodeId;
        graph.outputSocketId = EditorNodeGraph::kMaskOutputSocketId;
        graph.executionInspectionEnabled = true;
        graph.nodes = { generator };

        pipeline.ExecuteGraph(graph);
        ok &= Check(
            pipeline.GetLastGraphExecutionStats().maskCacheMisses > 0,
            "first exact-definition mask evaluation did not populate the persistent cache");
        pipeline.ExecuteGraph(graph);
        ok &= Check(
            pipeline.GetLastGraphExecutionStats().maskCacheHits > 0,
            "unchanged exact-definition mask evaluation did not reuse the persistent cache");

        graph.nodes.front().definitionHash = "mask-v2";
        pipeline.ExecuteGraph(graph);
        ok &= Check(
            pipeline.GetLastGraphExecutionStats().maskCacheMisses > 0 &&
                pipeline.GetLastGraphExecutionStats().maskCacheHits == 0,
            "changed exact-definition mask identity reused stale cached pixels");
    }
    return ok;
}

bool ValidateTransientTargetPoolBounds(
    const std::vector<unsigned char>& pixels,
    int width,
    int height) {
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);

    RenderGraphSnapshot graph;
    graph.outputNodeId = 5;
    graph.outputSocketId = EditorNodeGraph::kImageOutputSocketId;
    graph.executionInspectionEnabled = true;
    graph.nodes = {
        ImageNode(1),
        ImageNode(2),
        {},
        {},
        DataMathNode(5, RenderDataMathMode::Average, 1.0f)
    };
    graph.nodes[2].nodeId = 3;
    graph.nodes[2].kind = RenderGraphNodeKind::Reformat;
    graph.nodes[2].definitionId = "stack:test/reformat-a";
    graph.nodes[3].nodeId = 4;
    graph.nodes[3].kind = RenderGraphNodeKind::Reformat;
    graph.nodes[3].definitionId = "stack:test/reformat-b";
    graph.links = {
        Link(1, EditorNodeGraph::kImageOutputSocketId, 3, EditorNodeGraph::kImageInputSocketId),
        Link(2, EditorNodeGraph::kImageOutputSocketId, 4, EditorNodeGraph::kImageInputSocketId),
        Link(3, EditorNodeGraph::kImageOutputSocketId, 5, EditorNodeGraph::DataMathInputSocketId(0).c_str()),
        Link(4, EditorNodeGraph::kImageOutputSocketId, 5, EditorNodeGraph::DataMathInputSocketId(1).c_str())
    };

    bool observedEviction = false;
    for (int iteration = 0; iteration < 12; ++iteration) {
        const int outputWidth = 8 + iteration;
        const int outputHeight = 2 + iteration;
        graph.nodes[2].reformatSettings.width = outputWidth;
        graph.nodes[2].reformatSettings.height = outputHeight;
        graph.nodes[3].reformatSettings.width = outputWidth;
        graph.nodes[3].reformatSettings.height = outputHeight;
        pipeline.ExecuteGraph(graph);
        observedEviction |=
            pipeline.GetLastGraphExecutionStats().transientTargetEvictions > 0;
    }

    bool ok = Check(
        observedEviction,
        "multi-resolution transient target pool grew without evicting stale dimensions");
    graph.nodes[4].dataMathSettings.constantA = 0.125f;
    pipeline.ExecuteGraph(graph);
    const GraphExecutionStats reuseStats = pipeline.GetLastGraphExecutionStats();
    ok &= Check(
        reuseStats.transientTargetReuses > 0,
        "transient target trimming discarded the most-recent reusable dimension");
    ok &= Check(
        reuseStats.transientPoolBytes <= reuseStats.transientPoolBudgetBytes,
        "transient target pool remained above its byte budget after execution");
    return ok;
}

bool ValidateStableSourceExtentAcrossReformat(
    const std::vector<unsigned char>& pixels,
    int width,
    int height) {
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);

    RenderGraphNode reformat;
    reformat.nodeId = 2;
    reformat.kind = RenderGraphNodeKind::Reformat;
    reformat.definitionId = "stack:test/stable-source-reformat";
    reformat.reformatSettings.width = 7;
    reformat.reformatSettings.height = 3;

    RenderGraphSnapshot graph;
    graph.outputNodeId = 3;
    graph.outputSocketId = EditorNodeGraph::kImageOutputSocketId;
    graph.executionInspectionEnabled = true;
    graph.nodes = { ImageNode(1), reformat, OutputNode(3) };
    graph.links = {
        Link(1, EditorNodeGraph::kImageOutputSocketId, 2, EditorNodeGraph::kImageInputSocketId),
        Link(2, EditorNodeGraph::kImageOutputSocketId, 3, EditorNodeGraph::kImageInputSocketId)
    };

    pipeline.ExecuteGraph(graph);
    const std::vector<float> first =
        ReadTextureFloat(pipeline.GetOutputTexture(), 7, 3);
    pipeline.ExecuteGraph(graph);
    const GraphExecutionStats directRepeatStats =
        pipeline.GetLastGraphExecutionStats();
    const std::vector<float> directRepeat =
        ReadTextureFloat(pipeline.GetOutputTexture(), 7, 3);

    bool ok = Check(
        pipeline.GetCanvasWidth() == 7 &&
            pipeline.GetCanvasHeight() == 3 &&
            first.size() == 7u * 3u * 4u &&
            MaximumDifference(first, directRepeat) <= 1.0e-6,
        "repeated Reformat execution inherited the prior output extent as its source extent");
    ok &= Check(
        directRepeatStats.imageCacheHits > 0,
        "stable repeated Reformat execution invalidated its unchanged image cache");

    pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);
    pipeline.ExecuteGraph(graph);
    const GraphExecutionStats reloadedStats =
        pipeline.GetLastGraphExecutionStats();
    const std::vector<float> afterUnchangedLoad =
        ReadTextureFloat(pipeline.GetOutputTexture(), 7, 3);
    ok &= Check(
        reloadedStats.imageCacheHits > 0 &&
            MaximumDifference(first, afterUnchangedLoad) <= 1.0e-6,
        "unchanged source reload after Reformat re-uploaded the source or invalidated graph caches");

    int sourceWidth = 0;
    int sourceHeight = 0;
    const std::vector<unsigned char> source =
        pipeline.GetSourcePixels(sourceWidth, sourceHeight);
    int compareWidth = 0;
    int compareHeight = 0;
    const std::vector<unsigned char> compare =
        pipeline.GetCompareSourcePixels(compareWidth, compareHeight);
    ok &= Check(
        sourceWidth == width &&
            sourceHeight == height &&
            source.size() == pixels.size(),
        "source readback used the reformatted output extent");
    ok &= Check(
        compareWidth == width &&
            compareHeight == height &&
            compare.size() == pixels.size(),
        "compare-source readback used the reformatted output extent");
    return ok;
}

bool ValidateInterleavedTextureUploads() {
    GLint originalUnpackAlignment = 4;
    GLint originalUnpackRowLength = 0;
    GLint originalUnpackSkipRows = 0;
    GLint originalUnpackSkipPixels = 0;
    GLint originalUnpackBuffer = 0;
    GLint originalTextureBinding = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &originalUnpackAlignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &originalUnpackRowLength);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &originalUnpackSkipRows);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &originalUnpackSkipPixels);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &originalUnpackBuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &originalTextureBinding);

    unsigned int sentinelTexture = 0;
    unsigned int sentinelUnpackBuffer = 0;
    glGenTextures(1, &sentinelTexture);
    glBindTexture(GL_TEXTURE_2D, sentinelTexture);
    glGenBuffers(1, &sentinelUnpackBuffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, sentinelUnpackBuffer);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 4, nullptr, GL_STATIC_DRAW);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 7);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 2);

    const std::vector<unsigned char> rgPixels {
        10, 20, 30, 40, 50, 60,
        70, 80, 90, 100, 110, 120
    };
    const unsigned int rgTexture =
        GLHelpers::CreateTextureFromPixels(rgPixels.data(), 3, 2, 2);
    GLint restoredTextureBinding = 0;
    GLint restoredUnpackBuffer = 0;
    GLint restoredUnpackRowLength = 0;
    GLint restoredUnpackSkipRows = 0;
    GLint restoredUnpackSkipPixels = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &restoredTextureBinding);
    glGetIntegerv(
        GL_PIXEL_UNPACK_BUFFER_BINDING, &restoredUnpackBuffer);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &restoredUnpackRowLength);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &restoredUnpackSkipRows);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &restoredUnpackSkipPixels);
    const std::vector<float> uploadedRg =
        ReadTextureFloat(rgTexture, 3, 2);
    GLint restoredUnpackAlignment = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &restoredUnpackAlignment);

    bool ok = true;
    ok &= Check(
        rgTexture != 0 && uploadedRg.size() == 24,
        "two-channel texture upload did not produce a readable RGBA texture");
    if (uploadedRg.size() == 24) {
        for (std::size_t pixel = 0; pixel < 6; ++pixel) {
            const std::size_t source = pixel * 2;
            const std::size_t uploaded = pixel * 4;
            ok &= Check(
                std::abs(uploadedRg[uploaded + 0] -
                         rgPixels[source + 0] / 255.0f) <= 1.0e-4f &&
                    std::abs(uploadedRg[uploaded + 1] -
                             rgPixels[source + 1] / 255.0f) <= 1.0e-4f &&
                    std::abs(uploadedRg[uploaded + 2]) <= 1.0e-4f &&
                    std::abs(uploadedRg[uploaded + 3] - 1.0f) <= 1.0e-4f,
                "two-channel texture upload changed RG values or implicit BA defaults");
        }
    }
    ok &= Check(
        restoredUnpackAlignment == 8 &&
            restoredUnpackRowLength == 7 &&
            restoredUnpackSkipRows == 1 &&
            restoredUnpackSkipPixels == 2 &&
            restoredUnpackBuffer ==
                static_cast<GLint>(sentinelUnpackBuffer) &&
            restoredTextureBinding ==
                static_cast<GLint>(sentinelTexture),
        "texture upload did not restore caller texture/unpack state");
    if (rgTexture != 0) {
        glDeleteTextures(1, &rgTexture);
    }

    const std::vector<unsigned char> rgbPixels {
        5, 10, 15, 20, 25, 30, 35, 40, 45,
        50, 55, 60, 65, 70, 75, 80, 85, 90
    };
    const unsigned int rgbTexture =
        GLHelpers::CreateTextureFromPixels(rgbPixels.data(), 3, 2, 3);
    const std::vector<float> uploadedRgb =
        ReadTextureFloat(rgbTexture, 3, 2);
    ok &= Check(
        rgbTexture != 0 && uploadedRgb.size() == 24,
        "odd-row RGB texture upload did not produce a readable RGBA texture");
    if (uploadedRgb.size() == 24) {
        for (std::size_t pixel = 0; pixel < 6; ++pixel) {
            const std::size_t source = pixel * 3;
            const std::size_t uploaded = pixel * 4;
            ok &= Check(
                std::abs(uploadedRgb[uploaded + 0] -
                         rgbPixels[source + 0] / 255.0f) <= 1.0e-4f &&
                    std::abs(uploadedRgb[uploaded + 1] -
                             rgbPixels[source + 1] / 255.0f) <= 1.0e-4f &&
                    std::abs(uploadedRgb[uploaded + 2] -
                             rgbPixels[source + 2] / 255.0f) <= 1.0e-4f &&
                    std::abs(uploadedRgb[uploaded + 3] - 1.0f) <= 1.0e-4f,
                "odd-row RGB texture upload used incorrect row alignment");
        }
    }
    if (rgbTexture != 0) {
        glDeleteTextures(1, &rgbTexture);
    }

    ok &= Check(
        GLHelpers::CreateTextureFromPixels(rgbPixels.data(), 3, 2, 5) == 0,
        "unsupported channel count should fail before creating a texture");
    ok &= Check(
        GLHelpers::CreateEmptyTexture(0, 2) == 0,
        "invalid texture dimensions should fail before allocation");
    glPixelStorei(GL_UNPACK_ALIGNMENT, originalUnpackAlignment);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, originalUnpackRowLength);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, originalUnpackSkipRows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, originalUnpackSkipPixels);
    glBindBuffer(
        GL_PIXEL_UNPACK_BUFFER,
        static_cast<GLuint>(originalUnpackBuffer));
    glBindTexture(
        GL_TEXTURE_2D,
        static_cast<GLuint>(originalTextureBinding));
    if (sentinelUnpackBuffer != 0) {
        glDeleteBuffers(1, &sentinelUnpackBuffer);
    }
    if (sentinelTexture != 0) {
        glDeleteTextures(1, &sentinelTexture);
    }
    return ok;
}

bool ValidateFramebufferStatePreservation() {
    GLint originalPackAlignment = 4;
    GLint originalPackRowLength = 0;
    GLint originalPackSkipRows = 0;
    GLint originalPackSkipPixels = 0;
    GLint originalPackBuffer = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &originalPackAlignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &originalPackRowLength);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &originalPackSkipRows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &originalPackSkipPixels);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &originalPackBuffer);

    const unsigned int readTexture = GLHelpers::CreateEmptyTexture(2, 2);
    const unsigned int drawTexture = GLHelpers::CreateEmptyTexture(2, 2);
    const unsigned int readFbo = GLHelpers::CreateFBO(readTexture);
    const unsigned int drawFbo = GLHelpers::CreateFBO(drawTexture);
    if (readTexture == 0 || drawTexture == 0 ||
        readFbo == 0 || drawFbo == 0) {
        if (readFbo != 0) glDeleteFramebuffers(1, &readFbo);
        if (drawFbo != 0) glDeleteFramebuffers(1, &drawFbo);
        if (readTexture != 0) glDeleteTextures(1, &readTexture);
        if (drawTexture != 0) glDeleteTextures(1, &drawTexture);
        return Check(false, "could not allocate split framebuffer-state fixtures");
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    const unsigned int scratchTexture = GLHelpers::CreateEmptyTexture(1, 1);
    const unsigned int scratchFbo = GLHelpers::CreateFBO(scratchTexture);
    GLint restoredReadFbo = 0;
    GLint restoredDrawFbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &restoredReadFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &restoredDrawFbo);
    bool ok = Check(
        scratchTexture != 0 && scratchFbo != 0 &&
            restoredReadFbo == static_cast<GLint>(readFbo) &&
            restoredDrawFbo == static_cast<GLint>(drawFbo),
        "framebuffer helper collapsed distinct caller read/draw bindings");

    const std::vector<unsigned char> pixels {
        10, 20, 30, 255,
        40, 50, 60, 255,
        70, 80, 90, 255,
        100, 110, 120, 255
    };
    unsigned int sentinelPackBuffer = 0;
    glGenBuffers(1, &sentinelPackBuffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, sentinelPackBuffer);
    glBufferData(GL_PIXEL_PACK_BUFFER, 4, nullptr, GL_STATIC_DRAW);
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glPixelStorei(GL_PACK_ROW_LENGTH, 9);
    glPixelStorei(GL_PACK_SKIP_ROWS, 1);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 2);
    const GLboolean originalScissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean originalDepth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean originalStencil = glIsEnabled(GL_STENCIL_TEST);
    const GLboolean originalBlend = glIsEnabled(GL_BLEND);
    GLint originalViewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, originalViewport);
    glEnable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glViewport(3, 5, 17, 19);
    {
        RenderPipeline pipeline;
        pipeline.Initialize();
        pipeline.LoadSourceFromPixels(pixels.data(), 2, 2, 4);
        RenderGraphSnapshot sourceGraph;
        sourceGraph.outputNodeId = 2;
        sourceGraph.outputSocketId =
            EditorNodeGraph::kImageOutputSocketId;
        sourceGraph.nodes = { ImageNode(1), OutputNode(2) };
        sourceGraph.links = {
            Link(
                1,
                EditorNodeGraph::kImageOutputSocketId,
                2,
                EditorNodeGraph::kImageInputSocketId)
        };
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
        pipeline.ExecuteGraph(sourceGraph);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &restoredReadFbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &restoredDrawFbo);
        ok &= Check(
            restoredReadFbo == static_cast<GLint>(readFbo) &&
                restoredDrawFbo == static_cast<GLint>(drawFbo),
            "graph execution collapsed distinct caller read/draw bindings");
        ok &= Check(
            glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE &&
                glIsEnabled(GL_DEPTH_TEST) == GL_FALSE &&
                glIsEnabled(GL_STENCIL_TEST) == GL_TRUE &&
                glIsEnabled(GL_BLEND) == GL_FALSE,
            "graph execution did not restore caller fixed-function enable state");
        GLint restoredViewport[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_VIEWPORT, restoredViewport);
        ok &= Check(
            restoredViewport[0] == 3 &&
                restoredViewport[1] == 5 &&
                restoredViewport[2] == 17 &&
                restoredViewport[3] == 19,
            "graph execution did not restore the caller viewport");

        std::vector<std::shared_ptr<LayerBase>> legacyLayers {
            nullptr,
            std::make_shared<LegacyStateProbeLayer>()
        };
        pipeline.Execute(legacyLayers);
        GLint restoredReadBuffer = 0;
        GLint restoredDrawBuffer = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &restoredReadFbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &restoredDrawFbo);
        glGetIntegerv(GL_READ_BUFFER, &restoredReadBuffer);
        glGetIntegerv(GL_DRAW_BUFFER, &restoredDrawBuffer);
        glGetIntegerv(GL_VIEWPORT, restoredViewport);
        ok &= Check(
            restoredReadFbo == static_cast<GLint>(readFbo) &&
                restoredDrawFbo == static_cast<GLint>(drawFbo) &&
                restoredReadBuffer == GL_COLOR_ATTACHMENT0 &&
                restoredDrawBuffer == GL_COLOR_ATTACHMENT0,
            "legacy layer execution collapsed distinct caller framebuffer state");
        ok &= Check(
            glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE &&
                glIsEnabled(GL_DEPTH_TEST) == GL_FALSE &&
                glIsEnabled(GL_STENCIL_TEST) == GL_TRUE &&
                glIsEnabled(GL_BLEND) == GL_FALSE,
            "legacy layer execution did not restore caller fixed-function enable state");
        ok &= Check(
            restoredViewport[0] == 3 &&
                restoredViewport[1] == 5 &&
                restoredViewport[2] == 17 &&
                restoredViewport[3] == 19,
            "legacy layer execution did not restore the caller viewport");

        int outputWidth = 0;
        int outputHeight = 0;
        const std::vector<unsigned char> output =
            pipeline.GetOutputPixels(outputWidth, outputHeight);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &restoredReadFbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &restoredDrawFbo);
        GLint restoredPackAlignment = 0;
        GLint restoredPackRowLength = 0;
        GLint restoredPackSkipRows = 0;
        GLint restoredPackSkipPixels = 0;
        GLint restoredPackBuffer = 0;
        glGetIntegerv(GL_PACK_ALIGNMENT, &restoredPackAlignment);
        glGetIntegerv(GL_PACK_ROW_LENGTH, &restoredPackRowLength);
        glGetIntegerv(GL_PACK_SKIP_ROWS, &restoredPackSkipRows);
        glGetIntegerv(GL_PACK_SKIP_PIXELS, &restoredPackSkipPixels);
        glGetIntegerv(
            GL_PIXEL_PACK_BUFFER_BINDING, &restoredPackBuffer);
        ok &= Check(
            output.size() == 16 &&
                outputWidth == 2 &&
                outputHeight == 2,
            "texture readback did not return the expected 2x2 RGBA image");
        ok &= Check(
            restoredReadFbo == static_cast<GLint>(readFbo) &&
                restoredDrawFbo == static_cast<GLint>(drawFbo),
            "texture readback collapsed distinct caller read/draw bindings");
        ok &= Check(
            restoredPackAlignment == 8 &&
                restoredPackRowLength == 9 &&
                restoredPackSkipRows == 1 &&
                restoredPackSkipPixels == 2 &&
                restoredPackBuffer ==
                    static_cast<GLint>(sentinelPackBuffer),
            "texture readback did not restore caller pixel-pack state");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, originalPackAlignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, originalPackRowLength);
    glPixelStorei(GL_PACK_SKIP_ROWS, originalPackSkipRows);
    glPixelStorei(GL_PACK_SKIP_PIXELS, originalPackSkipPixels);
    glBindBuffer(
        GL_PIXEL_PACK_BUFFER,
        static_cast<GLuint>(originalPackBuffer));
    if (originalScissor == GL_TRUE) glEnable(GL_SCISSOR_TEST);
    else glDisable(GL_SCISSOR_TEST);
    if (originalDepth == GL_TRUE) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    if (originalStencil == GL_TRUE) glEnable(GL_STENCIL_TEST);
    else glDisable(GL_STENCIL_TEST);
    if (originalBlend == GL_TRUE) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);
    glViewport(
        originalViewport[0],
        originalViewport[1],
        originalViewport[2],
        originalViewport[3]);
    if (sentinelPackBuffer != 0) {
        glDeleteBuffers(1, &sentinelPackBuffer);
    }
    if (scratchFbo != 0) glDeleteFramebuffers(1, &scratchFbo);
    if (scratchTexture != 0) glDeleteTextures(1, &scratchTexture);
    glDeleteFramebuffers(1, &readFbo);
    glDeleteFramebuffers(1, &drawFbo);
    glDeleteTextures(1, &readTexture);
    glDeleteTextures(1, &drawTexture);
    return ok;
}

bool ValidateLutTextureUploadSafety() {
    constexpr int width = 2;
    constexpr int height = 2;
    const std::vector<unsigned char> pixels {
        32, 64, 96, 255,
        220, 180, 140, 192,
        10, 120, 240, 128,
        250, 30, 80, 64
    };

    RenderGraphNode lutNode;
    lutNode.nodeId = 2;
    lutNode.kind = RenderGraphNodeKind::Lut;
    lutNode.definitionId = "stack:graph/lut/validation";
    constexpr int lutEdge = 17;
    lutNode.lut.lut3D.size = lutEdge;
    lutNode.lut.lut3D.values.reserve(
        static_cast<std::size_t>(lutEdge) *
        static_cast<std::size_t>(lutEdge) *
        static_cast<std::size_t>(lutEdge) * 3u);
    for (int z = 0; z < lutEdge; ++z) {
        for (int y = 0; y < lutEdge; ++y) {
            for (int x = 0; x < lutEdge; ++x) {
                lutNode.lut.lut3D.values.push_back(
                    1.0f - static_cast<float>(x) /
                        static_cast<float>(lutEdge - 1));
                lutNode.lut.lut3D.values.push_back(
                    1.0f - static_cast<float>(y) /
                        static_cast<float>(lutEdge - 1));
                lutNode.lut.lut3D.values.push_back(
                    1.0f - static_cast<float>(z) /
                        static_cast<float>(lutEdge - 1));
            }
        }
    }

    RenderGraphSnapshot graph;
    graph.outputNodeId = 3;
    graph.outputSocketId = EditorNodeGraph::kImageOutputSocketId;
    graph.nodes = { ImageNode(1), lutNode, OutputNode(3) };
    graph.links = {
        Link(
            1, EditorNodeGraph::kImageOutputSocketId,
            2, EditorNodeGraph::kImageInputSocketId),
        Link(
            2, EditorNodeGraph::kImageOutputSocketId,
            3, EditorNodeGraph::kImageInputSocketId)
    };

    GLint originalUnpackBuffer = 0;
    glGetIntegerv(
        GL_PIXEL_UNPACK_BUFFER_BINDING, &originalUnpackBuffer);
    unsigned int sentinelUnpackBuffer = 0;
    glGenBuffers(1, &sentinelUnpackBuffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, sentinelUnpackBuffer);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 4, nullptr, GL_STATIC_DRAW);

    bool ok = true;
    {
        RenderPipeline pipeline;
        pipeline.Initialize();
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        pipeline.LoadSourceFromPixels(
            pixels.data(), width, height, 4);
        glBindBuffer(
            GL_PIXEL_UNPACK_BUFFER, sentinelUnpackBuffer);
        pipeline.ExecuteGraph(graph);
        const std::uint64_t expectedLutTextureBytes =
            static_cast<std::uint64_t>(lutEdge) *
            static_cast<std::uint64_t>(lutEdge) *
            static_cast<std::uint64_t>(lutEdge) * 3u * sizeof(float);
        const std::uint64_t expectedOutputTextureBytes =
            static_cast<std::uint64_t>(width) *
            static_cast<std::uint64_t>(height) * 4u * sizeof(std::uint16_t);
        ok &= Check(
            pipeline.GetLastGraphExecutionStats().persistentCacheBytes >=
                expectedLutTextureBytes + expectedOutputTextureBytes,
            "persistent graph budget omitted the uploaded 3D LUT texture");
        const std::vector<float> inverted =
            ReadTextureFloat(pipeline.GetOutputTexture(), width, height);
        GLint restoredUnpackBuffer = 0;
        glGetIntegerv(
            GL_PIXEL_UNPACK_BUFFER_BINDING, &restoredUnpackBuffer);
        ok &= Check(
            inverted.size() == pixels.size(),
            "valid 3D LUT did not produce a readable output");
        if (inverted.size() == pixels.size()) {
            for (std::size_t pixel = 0;
                 pixel < pixels.size() / 4u;
                 ++pixel) {
                const std::size_t offset = pixel * 4u;
                const bool transformedAsAuthored =
                    std::abs(
                        inverted[offset + 0] -
                        (1.0f - pixels[offset + 0] / 255.0f)) <= 0.003f &&
                    std::abs(
                        inverted[offset + 1] -
                        (1.0f - pixels[offset + 1] / 255.0f)) <= 0.003f &&
                    std::abs(
                        inverted[offset + 2] -
                        (1.0f - pixels[offset + 2] / 255.0f)) <= 0.003f &&
                    std::abs(
                        inverted[offset + 3] -
                        pixels[offset + 3] / 255.0f) <= 0.003f;
                ok &= Check(
                    transformedAsAuthored,
                    "3D LUT upload or interpolation changed its authored transform at pixel " +
                        std::to_string(pixel) + ": got (" +
                        std::to_string(inverted[offset + 0]) + ", " +
                        std::to_string(inverted[offset + 1]) + ", " +
                        std::to_string(inverted[offset + 2]) + ", " +
                        std::to_string(inverted[offset + 3]) + ")");
            }
        }
        ok &= Check(
            restoredUnpackBuffer ==
                static_cast<GLint>(sentinelUnpackBuffer),
            "3D LUT upload did not restore caller pixel-unpack state");

        graph.nodes[1].lut.lut3D.values[0] =
            std::numeric_limits<float>::quiet_NaN();
        pipeline.ExecuteGraph(graph);
        ok &= Check(
            pipeline.GetLastGraphExecutionStats().persistentCacheBytes == 0,
            "malformed LUT replacement retained stale persistent GPU resources");
        const std::vector<float> rejected =
            ReadTextureFloat(pipeline.GetOutputTexture(), width, height);
        ok &= Check(
            rejected.size() == pixels.size(),
            "malformed LUT fallback did not produce a readable output");
        if (rejected.size() == pixels.size()) {
            for (std::size_t index = 0; index < pixels.size(); ++index) {
                ok &= Check(
                    std::abs(
                        rejected[index] -
                        pixels[index] / 255.0f) <= 0.003f,
                    "non-finite LUT values should fail closed to an image passthrough");
            }
        }
    }

    glBindBuffer(
        GL_PIXEL_UNPACK_BUFFER,
        static_cast<GLuint>(originalUnpackBuffer));
    if (sentinelUnpackBuffer != 0) {
        glDeleteBuffers(1, &sentinelUnpackBuffer);
    }
    return ok;
}

bool ValidateGraphExceptionBoundary(
    const std::vector<unsigned char>& pixels,
    int width,
    int height) {
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);

    RenderGraphNode malformedLayer;
    malformedLayer.nodeId = 2;
    malformedLayer.kind = RenderGraphNodeKind::Layer;
    malformedLayer.definitionId = "stack:graph/layer/malformed-validation";
    malformedLayer.layerJson = {
        { "type", 42 }
    };

    RenderGraphSnapshot graph;
    graph.outputNodeId = 3;
    graph.outputSocketId = EditorNodeGraph::kImageOutputSocketId;
    graph.nodes = { ImageNode(1), std::move(malformedLayer), OutputNode(3) };
    graph.links = {
        Link(1, EditorNodeGraph::kImageOutputSocketId, 2, EditorNodeGraph::kImageInputSocketId),
        Link(2, EditorNodeGraph::kImageOutputSocketId, 3, EditorNodeGraph::kImageInputSocketId)
    };

    pipeline.ExecuteGraph(graph);
    const GraphExecutionStats& stats =
        pipeline.GetLastGraphExecutionStats();
    bool ok = Check(
        pipeline.GetOutputTexture() == 0,
        "malformed graph data published an unprocessed or partial output");
    ok &= Check(
        !stats.allocationFailed &&
            stats.lastSpecializedFailureNodeId == graph.outputNodeId &&
            !stats.lastSpecializedFailure.empty(),
        "malformed graph data did not report a contained execution failure");
    return ok;
}

bool RunValidationWithContext() {
    const std::vector<unsigned char> pixels {
        64, 32, 16, 128,
        255, 128, 0, 64,
        0, 255, 200, 255,
        10, 20, 30, 0
    };
    constexpr int width = 4;
    constexpr int height = 1;
    constexpr float addValue = 0.25f;
    constexpr float multiplyValue = 2.0f;
    constexpr double rgba16fTolerance = 2.5e-3;

    bool ok = ValidateInterleavedTextureUploads();
    ok &= ValidateFramebufferStatePreservation();
    ok &= ValidateLutTextureUploadSafety();
    ok &= ValidateExactDefinitionCacheInvalidation(pixels, width, height);
    ok &= ValidateTransientTargetPoolBounds(pixels, width, height);
    ok &= ValidateStableSourceExtentAcrossReformat(pixels, width, height);
    ok &= ValidateGraphExceptionBoundary(pixels, width, height);
    std::vector<float> fusedPixels;
    GraphExecutionStats fusedStats;
    {
        RenderPipeline pipeline;
        pipeline.Initialize();
        pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);
        RenderGraphSnapshot graph = BuildArithmeticGraph(false, addValue, multiplyValue);
        pipeline.ExecuteGraph(graph);
        fusedPixels = ReadTextureFloat(pipeline.GetOutputTexture(), width, height);
        fusedStats = pipeline.GetLastGraphExecutionStats();
        ok &= Check(fusedStats.fusedPointwiseGroups == 1, "linear arithmetic chain did not fuse");
        ok &= Check(fusedStats.fusedPointwiseNodes == 2 && fusedStats.avoidedPointwisePasses == 1,
            "fused pass accounting is incorrect");
        ok &= Check(fusedStats.pointwiseProgramCacheMisses == 1 &&
            !fusedStats.pointwiseGroups.empty() &&
            fusedStats.pointwiseGroups.front().authoredNodeIds == std::vector<int>({ 2, 3 }),
            "fused execution inspection does not retain authored order/source IDs");

        graph.nodes[1].dataMathSettings.constantB = 0.5f;
        pipeline.ExecuteGraph(graph);
        const GraphExecutionStats editedStats = pipeline.GetLastGraphExecutionStats();
        ok &= Check(editedStats.pointwiseProgramCacheHits == 1,
            "parameter edit did not reuse the structural generated-program cache");

        graph.outputNodeId = 2;
        graph.outputSocketId = EditorNodeGraph::kImageOutputSocketId;
        pipeline.ExecuteGraph(graph);
        ok &= Check(pipeline.GetLastGraphExecutionStats().fusedPointwiseGroups == 0,
            "requested intermediate preview was not materialized as its own boundary");
    }

    const PointwiseProgram arithmeticReference = BuildArithmeticReference(addValue, multiplyValue);
    const std::vector<float> cpuReference = EvaluateArithmeticReference(pixels, arithmeticReference);
    ok &= Check(MaximumDifference(cpuReference, fusedPixels) <= rgba16fTolerance,
        "CPU reference and fused live GPU output exceed RGBA16F tolerance");

    std::vector<float> reverseFusedPixels;
    {
        RenderPipeline pipeline;
        pipeline.Initialize();
        pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);
        RenderGraphSnapshot reverse = BuildArithmeticGraph(false, addValue, multiplyValue);
        reverse.nodes[1].dataMathMode = RenderDataMathMode::Multiply;
        reverse.nodes[1].dataMathSettings.constantB = multiplyValue;
        reverse.nodes[2].dataMathMode = RenderDataMathMode::Add;
        reverse.nodes[2].dataMathSettings.constantB = addValue;
        pipeline.ExecuteGraph(reverse);
        reverseFusedPixels = ReadTextureFloat(pipeline.GetOutputTexture(), width, height);
        ok &= Check(pipeline.GetLastGraphExecutionStats().fusedPointwiseGroups == 1,
            "Multiply -> Add did not execute as an ordered fused program");
    }
    const PointwiseProgram reverseReference = BuildReverseArithmeticReference(multiplyValue, addValue);
    const std::vector<float> reverseCpuReference = EvaluateArithmeticReference(pixels, reverseReference);
    ok &= Check(MaximumDifference(reverseCpuReference, reverseFusedPixels) <= rgba16fTolerance,
        "reverse-order CPU reference and fused live GPU output exceed RGBA16F tolerance");
    ok &= Check(MaximumDifference(fusedPixels, reverseFusedPixels) > 0.1,
        "Add -> Multiply and Multiply -> Add were not observably distinct after fusion");

    std::vector<float> unfusedPixels;
    GraphExecutionStats unfusedStats;
    {
        RenderPipeline pipeline;
        pipeline.Initialize();
        pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);
        pipeline.ExecuteGraph(BuildArithmeticGraph(true, addValue, multiplyValue));
        unfusedPixels = ReadTextureFloat(pipeline.GetOutputTexture(), width, height);
        unfusedStats = pipeline.GetLastGraphExecutionStats();
    }
    ok &= Check(unfusedStats.fusedPointwiseGroups == 0,
        "fan-out did not create the conservative v1 materialization barrier");
    ok &= Check(MaximumDifference(cpuReference, unfusedPixels) <= rgba16fTolerance,
        "CPU reference and unfused live GPU output exceed RGBA16F tolerance");
    ok &= Check(MaximumDifference(fusedPixels, unfusedPixels) <= rgba16fTolerance,
        "fused and unfused live GPU outputs disagree");
    ok &= Check(fusedStats.persistentCacheBytes < unfusedStats.persistentCacheBytes,
        "representative fusion did not reduce persistent materialized bytes");

    {
        RenderPipeline pipeline;
        pipeline.Initialize();
        pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);
        RenderGraphSnapshot alpha;
        alpha.outputNodeId = 4;
        alpha.outputSocketId = EditorNodeGraph::kImageOutputSocketId;
        alpha.nodes = {
            ImageNode(1),
            TechnicalNode(2, Stack::NodeMath::TechnicalImageOperation::Premultiply),
            TechnicalNode(3, Stack::NodeMath::TechnicalImageOperation::Unpremultiply),
            OutputNode(4)
        };
        alpha.links = {
            Link(1, EditorNodeGraph::kImageOutputSocketId, 2, EditorNodeGraph::kImageInputSocketId),
            Link(2, EditorNodeGraph::kImageOutputSocketId, 3, EditorNodeGraph::kImageInputSocketId),
            Link(3, EditorNodeGraph::kImageOutputSocketId, 4, EditorNodeGraph::kImageInputSocketId)
        };
        pipeline.ExecuteGraph(alpha);
        const std::vector<float> alphaPixels = ReadTextureFloat(pipeline.GetOutputTexture(), width, height);
        ok &= Check(pipeline.GetLastGraphExecutionStats().fusedPointwiseGroups == 1,
            "premultiply/unpremultiply chain did not fuse");
        ok &= Check(alphaPixels.size() == pixels.size() &&
            std::abs(alphaPixels[0] - pixels[0] / 255.0f) <= rgba16fTolerance &&
            std::abs(alphaPixels[12]) <= rgba16fTolerance &&
            std::abs(alphaPixels[13]) <= rgba16fTolerance &&
            std::abs(alphaPixels[14]) <= rgba16fTolerance,
            "fused alpha guard does not match the Phase 2 contract");
    }

    {
        RenderPipeline pipeline;
        pipeline.Initialize();
        pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);
        RenderGraphSnapshot failure = BuildArithmeticGraph(false, addValue, multiplyValue);
        failure.nodes[1].dataMathSettings.constantB = std::numeric_limits<float>::quiet_NaN();
        pipeline.ExecuteGraph(failure);
        const GraphExecutionStats stats = pipeline.GetLastGraphExecutionStats();
        ok &= Check(stats.pointwiseFallbacks >= 1 && !stats.lastPointwiseFailure.empty(),
            "invalid uniform did not produce a source-mapped fusion fallback");
        ok &= Check(std::find(
                stats.lastPointwiseFailureNodeIds.begin(),
                stats.lastPointwiseFailureNodeIds.end(),
                2) != stats.lastPointwiseFailureNodeIds.end(),
            "fusion fallback does not identify the authored failing node");
    }

    {
        RenderPipeline pipeline;
        pipeline.Initialize();
        pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);
        RenderGraphSnapshot average;
        average.outputNodeId = 10;
        average.outputSocketId = EditorNodeGraph::kImageOutputSocketId;
        average.nodes = {
            ImageNode(1), ImageNode(2), ImageNode(3),
            DataMathNode(9, RenderDataMathMode::Average, 1.0f), OutputNode(10)
        };
        average.links = {
            Link(1, EditorNodeGraph::kImageOutputSocketId, 9, EditorNodeGraph::DataMathInputSocketId(0).c_str()),
            Link(2, EditorNodeGraph::kImageOutputSocketId, 9, EditorNodeGraph::DataMathInputSocketId(1).c_str()),
            Link(3, EditorNodeGraph::kImageOutputSocketId, 9, EditorNodeGraph::DataMathInputSocketId(2).c_str()),
            Link(9, EditorNodeGraph::kImageOutputSocketId, 10, EditorNodeGraph::kImageInputSocketId)
        };
        pipeline.ExecuteGraph(average);
        ok &= Check(pipeline.GetLastGraphExecutionStats().transientTargetAllocations == 2,
            "first multipass average did not allocate the bounded two-target transient pool");
        average.nodes[3].dataMathSettings.constantA = 0.125f;
        pipeline.ExecuteGraph(average);
        ok &= Check(pipeline.GetLastGraphExecutionStats().transientTargetReuses >= 2,
            "subsequent multipass evaluation did not reuse transient targets");
    }

    if (ok) {
        std::cout << "Phase 4 live validation passed: CPU, unfused GPU, fused GPU, "
                     "authored order, alpha, previews, failures, cache, and targets.\n";
        std::cout << "Fused bytes: " << fusedStats.persistentCacheBytes
                  << "; unfused bytes: " << unfusedStats.persistentCacheBytes << "\n";
    }
    return ok;
}

} // namespace

bool ValidateNodeMathPhase4Integration() {
    if (!glfwInit()) {
        std::cerr << "Phase 4 validation failed: glfwInit failed.\n";
        return false;
    }
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(64, 64, "Stack Phase 4 Validation", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Phase 4 validation failed: hidden OpenGL context creation failed.\n";
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) {
        std::cerr << "Phase 4 validation failed: OpenGL function loading failed.\n";
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return false;
    }
    const bool result = RunValidationWithContext();
    glfwMakeContextCurrent(nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}

} // namespace Stack::Validation
