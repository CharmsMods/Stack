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
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

using Stack::NodeMath::PointwiseOperation;
using Stack::NodeMath::PointwisePixel;
using Stack::NodeMath::PointwiseProgram;
using Stack::NodeMath::PointwiseSourceLocation;

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
    GLint previousFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    const unsigned int fbo = GLHelpers::CreateFBO(texture);
    if (fbo == 0) return {};
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    std::vector<float> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data());
    const GLenum error = glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
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

    bool ok = true;
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
