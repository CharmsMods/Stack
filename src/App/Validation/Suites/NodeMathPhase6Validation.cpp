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
#include "Renderer/Frequency/GpuFft.h"
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

bool ValidateGpuFftAgainstCpuReference() {
    constexpr int sourceWidth = 7;
    constexpr int sourceHeight = 5;
    constexpr int paddedWidth = 8;
    constexpr int paddedHeight = 8;
    constexpr int originX = 0;
    constexpr int originY = 1;
    std::vector<float> source(
        static_cast<std::size_t>(sourceWidth) *
        static_cast<std::size_t>(sourceHeight) * 4u,
        1.0f);
    for (int y = 0; y < sourceHeight; ++y) {
        for (int x = 0; x < sourceWidth; ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * sourceWidth + x) * 4u;
            const float value =
                (static_cast<float>((x * 11 + y * 7) % 19) - 9.0f) * 0.625f;
            source[index + 0] = value;
            source[index + 1] = -value * 0.5f;
            source[index + 2] = value * 1.75f;
        }
    }

    unsigned int sourceTexture = 0;
    glGenTextures(1, &sourceTexture);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA16F,
        sourceWidth, sourceHeight, 0,
        GL_RGBA, GL_FLOAT, source.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (sourceTexture == 0 || glGetError() != GL_NO_ERROR) return false;

    const auto wrapIndex = [](int value, int size) {
        const int result = value % size;
        return result < 0 ? result + size : result;
    };
    const auto mirrorIndex = [&](int value, int size) {
        const int period = 2 * (size - 1);
        const int folded = wrapIndex(value, period);
        return folded < size ? folded : period - folded;
    };

    Stack::Renderer::Frequency::GpuFft fft;
    bool ok =
        Stack::Renderer::Frequency::GpuFft::NextPowerOfTwo(1) == 1 &&
        Stack::Renderer::Frequency::GpuFft::NextPowerOfTwo(7) == 8 &&
        Stack::Renderer::Frequency::GpuFft::NextPowerOfTwo(
            std::numeric_limits<int>::max()) == 0 &&
        fft.Forward(
            sourceTexture,
            sourceWidth,
            sourceHeight,
            sourceWidth,
            paddedHeight,
            originX,
            originY,
            Stack::Renderer::Frequency::FftEdgePolicy::Mirror) == 0;
    for (Stack::Renderer::Frequency::FftEdgePolicy policy : {
             Stack::Renderer::Frequency::FftEdgePolicy::Mirror,
             Stack::Renderer::Frequency::FftEdgePolicy::Wrap,
             Stack::Renderer::Frequency::FftEdgePolicy::ZeroPad}) {
        std::vector<double> packed(
            static_cast<std::size_t>(paddedWidth) * paddedHeight, 0.0);
        for (int y = 0; y < paddedHeight; ++y) {
            for (int x = 0; x < paddedWidth; ++x) {
                int sourceX = x - originX;
                int sourceY = y - originY;
                const bool inside =
                    sourceX >= 0 && sourceX < sourceWidth &&
                    sourceY >= 0 && sourceY < sourceHeight;
                if (!inside &&
                    policy == Stack::Renderer::Frequency::FftEdgePolicy::ZeroPad)
                    continue;
                if (!inside &&
                    policy == Stack::Renderer::Frequency::FftEdgePolicy::Mirror) {
                    sourceX = mirrorIndex(sourceX, sourceWidth);
                    sourceY = mirrorIndex(sourceY, sourceHeight);
                } else if (!inside) {
                    sourceX = wrapIndex(sourceX, sourceWidth);
                    sourceY = wrapIndex(sourceY, sourceHeight);
                }
                packed[static_cast<std::size_t>(y) * paddedWidth + x] =
                    source[(static_cast<std::size_t>(sourceY) * sourceWidth +
                            sourceX) * 4u];
            }
        }

        std::vector<std::complex<double>> reference(
            static_cast<std::size_t>(paddedWidth) * paddedHeight);
        constexpr double twoPi = 6.28318530717958647692;
        for (int ky = 0; ky < paddedHeight; ++ky) {
            for (int kx = 0; kx < paddedWidth; ++kx) {
                std::complex<double> sum;
                for (int y = 0; y < paddedHeight; ++y) {
                    for (int x = 0; x < paddedWidth; ++x) {
                        const double angle = -twoPi * (
                            static_cast<double>(kx * x) / paddedWidth +
                            static_cast<double>(ky * y) / paddedHeight);
                        sum += packed[static_cast<std::size_t>(y) * paddedWidth + x] *
                            std::complex<double>(std::cos(angle), std::sin(angle));
                    }
                }
                reference[static_cast<std::size_t>(ky) * paddedWidth + kx] = sum;
            }
        }

        unsigned int spectrum = 0;
        if (policy == Stack::Renderer::Frequency::FftEdgePolicy::Mirror) {
            struct ImageBindingState {
                GLint texture = 0;
                GLint level = 0;
                GLint layered = GL_FALSE;
                GLint layer = 0;
                GLint access = GL_READ_ONLY;
                GLint format = GL_RGBA16F;
            };
            const auto readImageBinding = [](GLuint unit) {
                ImageBindingState state;
                glGetIntegeri_v(GL_IMAGE_BINDING_NAME, unit, &state.texture);
                glGetIntegeri_v(GL_IMAGE_BINDING_LEVEL, unit, &state.level);
                glGetIntegeri_v(GL_IMAGE_BINDING_LAYERED, unit, &state.layered);
                glGetIntegeri_v(GL_IMAGE_BINDING_LAYER, unit, &state.layer);
                glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS, unit, &state.access);
                glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT, unit, &state.format);
                return state;
            };
            const auto sameImageBinding = [](
                const ImageBindingState& left,
                const ImageBindingState& right) {
                return left.texture == right.texture &&
                    left.level == right.level &&
                    left.layered == right.layered &&
                    left.layer == right.layer &&
                    left.access == right.access &&
                    left.format == right.format;
            };
            const auto restoreImageBinding = [](
                GLuint unit,
                const ImageBindingState& state) {
                const GLuint texture =
                    state.texture > 0 &&
                        glIsTexture(static_cast<GLuint>(state.texture))
                    ? static_cast<GLuint>(state.texture)
                    : 0;
                glBindImageTexture(
                    unit,
                    texture,
                    state.level,
                    static_cast<GLboolean>(state.layered),
                    state.layer,
                    static_cast<GLenum>(state.access),
                    state.format != 0
                        ? static_cast<GLenum>(state.format)
                        : GL_RGBA16F);
            };
            const Stack::Renderer::GLState::FramebufferState savedState;
            const ImageBindingState savedImage0 = readImageBinding(0);
            const ImageBindingState savedImage1 = readImageBinding(1);
            const unsigned int readProbe = GLHelpers::CreateFBO(sourceTexture);
            const unsigned int drawProbe = GLHelpers::CreateFBO(sourceTexture);
            const unsigned int textureProbe =
                GLHelpers::CreateEmptyTexture(1, 1);
            GLint savedProgram = 0;
            GLint savedActiveTexture = GL_TEXTURE0;
            GLint savedTexture0 = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &savedProgram);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &savedActiveTexture);
            glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedTexture0);
            if (readProbe == 0 || drawProbe == 0 || textureProbe == 0) {
                ok = false;
            } else {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, readProbe);
                glReadBuffer(GL_COLOR_ATTACHMENT0);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawProbe);
                glDrawBuffer(GL_COLOR_ATTACHMENT0);
                glBindTexture(GL_TEXTURE_2D, textureProbe);
                glBindImageTexture(
                    0, textureProbe, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
                glBindImageTexture(
                    1, textureProbe, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
                const ImageBindingState expectedImage0 = readImageBinding(0);
                const ImageBindingState expectedImage1 = readImageBinding(1);
                glActiveTexture(GL_TEXTURE3);
                glUseProgram(0);
                spectrum = fft.Forward(
                    sourceTexture,
                    sourceWidth,
                    sourceHeight,
                    paddedWidth,
                    paddedHeight,
                    originX,
                    originY,
                    policy);
                GLint restoredRead = 0;
                GLint restoredDraw = 0;
                GLint restoredProgram = -1;
                GLint restoredActiveTexture = 0;
                GLint restoredTexture0 = 0;
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &restoredRead);
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &restoredDraw);
                glGetIntegerv(GL_CURRENT_PROGRAM, &restoredProgram);
                glGetIntegerv(GL_ACTIVE_TEXTURE, &restoredActiveTexture);
                glActiveTexture(GL_TEXTURE0);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &restoredTexture0);
                const ImageBindingState restoredImage0 = readImageBinding(0);
                const ImageBindingState restoredImage1 = readImageBinding(1);
                ok &= restoredRead == static_cast<GLint>(readProbe) &&
                    restoredDraw == static_cast<GLint>(drawProbe) &&
                    restoredProgram == 0 &&
                    restoredActiveTexture == GL_TEXTURE3 &&
                    restoredTexture0 == static_cast<GLint>(textureProbe) &&
                    sameImageBinding(restoredImage0, expectedImage0) &&
                    sameImageBinding(restoredImage1, expectedImage1);
            }
            savedState.Restore();
            restoreImageBinding(0, savedImage0);
            restoreImageBinding(1, savedImage1);
            glUseProgram(static_cast<GLuint>(savedProgram));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(
                GL_TEXTURE_2D,
                static_cast<GLuint>(savedTexture0));
            glActiveTexture(static_cast<GLenum>(savedActiveTexture));
            if (readProbe != 0) glDeleteFramebuffers(1, &readProbe);
            if (drawProbe != 0) glDeleteFramebuffers(1, &drawProbe);
            if (textureProbe != 0) glDeleteTextures(1, &textureProbe);
        } else {
            spectrum = fft.Forward(
                sourceTexture,
                sourceWidth,
                sourceHeight,
                paddedWidth,
                paddedHeight,
                originX,
                originY,
                policy);
        }
        const std::vector<float> measured =
            ReadPhase6ComplexTexture(spectrum, paddedWidth, paddedHeight);
        double maximumError = 0.0;
        double maximumReference = 1.0;
        if (measured.size() != reference.size() * 2u) {
            ok = false;
        } else {
            for (std::size_t index = 0; index < reference.size(); ++index) {
                maximumError = std::max(
                    maximumError,
                    std::abs(
                        static_cast<double>(measured[index * 2u]) -
                        reference[index].real()));
                maximumError = std::max(
                    maximumError,
                    std::abs(
                        static_cast<double>(measured[index * 2u + 1u]) -
                        reference[index].imag()));
                maximumReference = std::max(
                    maximumReference, std::abs(reference[index]));
            }
            const double relativeError =
                maximumError / maximumReference;
            if (relativeError >= 0.0125) {
                std::cerr
                    << "Phase 6 FFT reference mismatch for edge policy "
                    << static_cast<int>(policy)
                    << ": max error " << maximumError
                    << ", relative error " << relativeError
                    << ", measured DC "
                    << (measured.empty() ? 0.0f : measured[0])
                    << ", reference DC " << reference[0].real()
                    << ".\n";
                ok = false;
            }
        }
        const unsigned int reconstructed = fft.Inverse(
            spectrum, paddedWidth, paddedHeight);
        const std::vector<float> reconstructedSamples =
            ReadPhase6ComplexTexture(
                reconstructed, paddedWidth, paddedHeight);
        double inverseError = 0.0;
        if (reconstructedSamples.size() != packed.size() * 2u) {
            ok = false;
        } else {
            for (std::size_t index = 0; index < packed.size(); ++index) {
                inverseError = std::max(
                    inverseError,
                    std::abs(
                        static_cast<double>(
                            reconstructedSamples[index * 2u]) -
                        packed[index]));
            }
            if (inverseError >= 0.0125) {
                std::cerr
                    << "Phase 6 inverse FFT mismatch for edge policy "
                    << static_cast<int>(policy)
                    << ": max error " << inverseError << ".\n";
                ok = false;
            }
        }
        if (reconstructed != 0)
            glDeleteTextures(1, &reconstructed);
        if (spectrum != 0) glDeleteTextures(1, &spectrum);
    }
    glDeleteTextures(1, &sourceTexture);
    return ok;
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
        saved, restored, 0, {}, 0, 0, 4);
    const EditorNodeGraph::Node* restoredReformat = restored.FindNode(reformatId);
    if (!restoredReformat || restoredReformat->kind != EditorNodeGraph::NodeKind::Reformat ||
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

bool BuildAuthoredFrequencyRoundTrip(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    RenderGraphSnapshot& snapshot,
    std::string& error) {
    EditorNodeGraph::Graph graph;
    graph.Clear();
    EditorNodeGraph::ImagePayload source;
    source.label = "Phase 6 Frequency Source";
    source.width = width;
    source.height = height;
    source.channels = 4;
    source.originalChannels = 4;
    source.pixels = pixels;
    const int sourceId = graph.AddImageNode(std::move(source), { 0.0f, 0.0f })->id;
    const int splitId = graph.AddChannelSplitNode({ 220.0f, 0.0f })->id;
    const int fftId = graph.AddFrequencyFftNode({ 440.0f, 0.0f })->id;
    const int separateId =
        graph.AddSpectrumSeparateNode({ 660.0f, 0.0f })->id;
    const int recombineId =
        graph.AddSpectrumRecombineNode({ 880.0f, 0.0f })->id;
    const int ifftId = graph.AddFrequencyIfftNode({ 1100.0f, 0.0f })->id;
    const int outputId = graph.AddOutputNode({ 1320.0f, 0.0f }, true)->id;
    if (!graph.TryConnectSockets(sourceId, EditorNodeGraph::kImageOutputSocketId,
            splitId, EditorNodeGraph::kImageInputSocketId, &error) ||
        !graph.TryConnectSockets(splitId, "r",
            fftId, EditorNodeGraph::kChannelInputSocketId, &error) ||
        !graph.TryConnectSockets(fftId, EditorNodeGraph::kSpectrumOutputSocketId,
            separateId, EditorNodeGraph::kSpectrumInputSocketId, &error) ||
        !graph.TryConnectSockets(
            separateId, EditorNodeGraph::kSpectrumMagnitudeOutputSocketId,
            recombineId, EditorNodeGraph::kSpectrumMagnitudeInputSocketId,
            &error) ||
        !graph.TryConnectSockets(
            separateId, EditorNodeGraph::kSpectrumPhaseOutputSocketId,
            recombineId, EditorNodeGraph::kSpectrumPhaseInputSocketId,
            &error) ||
        !graph.TryConnectSockets(
            recombineId, EditorNodeGraph::kSpectrumOutputSocketId,
            ifftId, EditorNodeGraph::kSpectrumInputSocketId, &error) ||
        !graph.TryConnectSockets(ifftId, EditorNodeGraph::kChannelOutputSocketId,
            outputId, EditorNodeGraph::kImageInputSocketId, &error)) {
        return false;
    }
    EditorModule editor;
    editor.GetNodeGraph() = std::move(graph);
    snapshot = editor.BuildGraphSnapshot();
    snapshot.executionInspectionEnabled = true;
    return snapshot.outputNodeId == outputId && !snapshot.nodes.empty();
}

bool ValidateFrequencyEditorActions() {
    EditorModule editor;
    EditorNodeGraph::Graph& graph = editor.GetNodeGraph();
    graph.Clear();
    const int splitId = graph.AddChannelSplitNode({ 0.0f, 0.0f })->id;
    EditorNodeGraph::Node* filter = graph.AddFrequencyFilterNode(
        EditorNodeGraph::FrequencyFilterMode::BandStop, { 240.0f, 0.0f });
    const int filterId = filter->id;
    filter->frequencyFilterSettings.strength = 0.63f;
    filter->frequencyFilterSettings.edgePolicy =
        EditorNodeGraph::FrequencyEdgePolicy::Wrap;
    filter->frequencyFilterSettings.localResponse.lowCutoff = 0.08f;
    filter->frequencyFilterSettings.localResponse.highCutoff = 0.22f;
    const int outputId = graph.AddOutputNode({ 520.0f, 0.0f }, true)->id;
    EditorNodeGraph::ImagePayload unrelatedPayload;
    unrelatedPayload.label = "Frequency history payload sentinel";
    unrelatedPayload.width = 16;
    unrelatedPayload.height = 16;
    unrelatedPayload.channels = 4;
    unrelatedPayload.pixels.resize(16u * 16u * 4u, 127u);
    const int unrelatedImageId = graph.AddImageNode(
        std::move(unrelatedPayload),
        { 0.0f, 240.0f })->id;
    const unsigned char* unrelatedPixelsBefore =
        graph.FindNode(unrelatedImageId)->image.pixels.data();
    std::string error;
    bool ok = graph.TryConnectSockets(
        splitId, "r", filterId, EditorNodeGraph::kChannelInputSocketId, &error);
    ok = ok && graph.TryConnectSockets(
        filterId,
        EditorNodeGraph::kChannelOutputSocketId,
        outputId,
        EditorNodeGraph::kImageInputSocketId,
        &error);
    ok = ok && editor.SetFrequencyParameterExposed(
        filterId, EditorNodeGraph::kStrengthParameterId, true);
    ok = Phase6Check(
        ok, error.empty() ? "could not author the Frequency Filter action fixture" : error);

    const std::size_t originalNodeCount = graph.GetNodes().size();
    ok &= Phase6Check(
        editor.ExtractFrequencyResponseNode(filterId, &error),
        error.empty() ? "Extract Response Node failed" : error);
    const EditorNodeGraph::Link* extractedLink = graph.FindAnyInputLink(
        filterId, EditorNodeGraph::kFrequencyResponseInputSocketId);
    const EditorNodeGraph::Node* extracted = extractedLink == nullptr
        ? nullptr : graph.FindNode(extractedLink->fromNodeId);
    ok &= Phase6Check(
        graph.GetNodes().size() == originalNodeCount + 1 &&
        extracted != nullptr &&
        extracted->kind == EditorNodeGraph::NodeKind::FrequencyResponse &&
        std::abs(extracted->frequencyResponseSettings.lowCutoff - 0.08f) < 1.0e-6f &&
        std::abs(extracted->frequencyResponseSettings.highCutoff - 0.22f) < 1.0e-6f,
        "Extract Response Node did not preserve the local response settings");
    ok &= Phase6Check(
        editor.UndoFrequencyGraphAction() &&
        graph.GetNodes().size() == originalNodeCount &&
        graph.FindAnyInputLink(
            filterId, EditorNodeGraph::kFrequencyResponseInputSocketId) == nullptr,
        "Extract Response Node was not a single undoable graph edit");
    ok &= Phase6Check(
        editor.RedoFrequencyGraphAction() &&
        graph.FindAnyInputLink(
            filterId, EditorNodeGraph::kFrequencyResponseInputSocketId) != nullptr,
        "Extract Response Node could not be redone");
    ok &= Phase6Check(
        editor.UndoFrequencyGraphAction(),
        "the action fixture could not return to its pre-extraction graph");

    error.clear();
    ok &= Phase6Check(
        editor.ExpandFrequencyFilterNode(filterId, &error),
        error.empty() ? "Expand to Advanced Nodes failed" : error);
    const auto countKind = [&](EditorNodeGraph::NodeKind kind) {
        return static_cast<int>(std::count_if(
            graph.GetNodes().begin(), graph.GetNodes().end(),
            [&](const EditorNodeGraph::Node& node) { return node.kind == kind; }));
    };
    const EditorNodeGraph::Node* apply = nullptr;
    const EditorNodeGraph::Node* inverse = nullptr;
    const EditorNodeGraph::Node* transform = nullptr;
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::ApplyFrequencyResponse) apply = &node;
        if (node.kind == EditorNodeGraph::NodeKind::FrequencyIfft) inverse = &node;
        if (node.kind == EditorNodeGraph::NodeKind::FrequencyFft) transform = &node;
    }
    const bool strengthExposed = apply != nullptr &&
        std::find(
            apply->exposedParameterIds.begin(),
            apply->exposedParameterIds.end(),
            EditorNodeGraph::kStrengthParameterId) !=
            apply->exposedParameterIds.end();
    const EditorNodeGraph::Link* advancedInput = transform == nullptr
        ? nullptr : graph.FindAnyInputLink(
            transform->id, EditorNodeGraph::kChannelInputSocketId);
    const EditorNodeGraph::Link* preservedOutput =
        graph.FindAnyInputLink(
            outputId,
            EditorNodeGraph::kImageInputSocketId);
    ok &= Phase6Check(
        graph.FindNode(filterId) == nullptr &&
        countKind(EditorNodeGraph::NodeKind::FrequencyFft) == 1 &&
        countKind(EditorNodeGraph::NodeKind::FrequencyResponse) == 1 &&
        countKind(EditorNodeGraph::NodeKind::ApplyFrequencyResponse) == 1 &&
        countKind(EditorNodeGraph::NodeKind::FrequencyIfft) == 1 &&
        advancedInput != nullptr && advancedInput->fromNodeId == splitId &&
        inverse != nullptr && preservedOutput != nullptr &&
        preservedOutput->fromNodeId == inverse->id &&
        strengthExposed &&
        std::abs(apply->applyFrequencyResponseSettings.strength - 0.63f) < 1.0e-6f,
        "Expand to Advanced Nodes did not preserve connections, settings, or exposure");
    ok &= Phase6Check(
        editor.UndoFrequencyGraphAction() &&
        graph.FindNode(filterId) != nullptr &&
        graph.GetNodes().size() == originalNodeCount,
        "Expand to Advanced Nodes was not a single undoable graph edit");
    ok &= Phase6Check(
        editor.RedoFrequencyGraphAction() &&
        graph.FindNode(filterId) == nullptr &&
        countKind(EditorNodeGraph::NodeKind::ApplyFrequencyResponse) == 1 &&
        graph.FindNode(unrelatedImageId) != nullptr &&
        graph.FindNode(unrelatedImageId)->image.pixels.data() ==
            unrelatedPixelsBefore,
        "Expand redo failed or frequency history replaced an unrelated image payload");
    const int laterEditId = graph.AddValueNode(
        Stack::NodeMath::MakeUniformScalar(0.25),
        { 760.0f, 220.0f })->id;
    ok &= Phase6Check(
        editor.CanUndoFrequencyGraphAction() &&
        !editor.UndoFrequencyGraphAction() &&
        !editor.CanUndoFrequencyGraphAction() &&
        !editor.CanRedoFrequencyGraphAction() &&
        graph.FindNode(laterEditId) != nullptr,
        "a newer structural edit should invalidate stale frequency history without being overwritten");

    EditorModule rollbackEditor;
    EditorNodeGraph::Graph& rollbackGraph =
        rollbackEditor.GetNodeGraph();
    rollbackGraph.Clear();
    const int rollbackSplitId =
        rollbackGraph.AddChannelSplitNode(
            { 0.0f, 0.0f })->id;
    const int rollbackFilterId =
        rollbackGraph.AddFrequencyFilterNode(
            EditorNodeGraph::FrequencyFilterMode::LowPass,
            { 240.0f, 0.0f })->id;
    const int rollbackTargetId =
        rollbackGraph.AddOutputNode(
            { 520.0f, 0.0f },
            true)->id;
    error.clear();
    ok &= Phase6Check(
        rollbackGraph.TryConnectSockets(
            rollbackSplitId,
            "r",
            rollbackFilterId,
            EditorNodeGraph::kChannelInputSocketId,
            &error) &&
        rollbackGraph.TryConnectSockets(
            rollbackFilterId,
            EditorNodeGraph::kChannelOutputSocketId,
            rollbackTargetId,
            EditorNodeGraph::kImageInputSocketId,
            &error),
        "advanced frequency rollback fixture could not connect");
    auto& rollbackNodes = rollbackGraph.EditNodes();
    const auto invalidRollbackTarget = std::find_if(
        rollbackNodes.begin(),
        rollbackNodes.end(),
        [rollbackTargetId](const auto& node) {
            return node.id == rollbackTargetId;
        });
    const bool foundRollbackTarget =
        invalidRollbackTarget != rollbackNodes.end();
    if (foundRollbackTarget) {
        invalidRollbackTarget->kind =
            EditorNodeGraph::NodeKind::Value;
    }
    const std::size_t rollbackNodeCount =
        rollbackGraph.GetNodes().size();
    const std::size_t rollbackLinkCount =
        rollbackGraph.GetLinks().size();
    const int rollbackNextNodeId =
        rollbackGraph.GetNextNodeId();
    error.clear();
    ok &= Phase6Check(
        foundRollbackTarget &&
        !rollbackEditor.ExpandFrequencyFilterNode(
            rollbackFilterId,
            &error) &&
        rollbackGraph.GetNodes().size() ==
            rollbackNodeCount &&
        rollbackGraph.GetLinks().size() ==
            rollbackLinkCount &&
        rollbackGraph.GetNextNodeId() ==
            rollbackNextNodeId &&
        rollbackGraph.FindNode(rollbackFilterId) != nullptr &&
        rollbackGraph.FindAnyInputLink(
            rollbackFilterId,
            EditorNodeGraph::kChannelInputSocketId) != nullptr &&
        rollbackGraph.FindAnyInputLink(
            rollbackTargetId,
            EditorNodeGraph::kImageInputSocketId) != nullptr,
        "failed advanced frequency rewiring should restore the exact filter, links, and node-ID state");
    return ok;
}

bool ValidateSpectrumAnalyzerFixture() {
    constexpr int width = 8;
    constexpr int height = 8;
    std::vector<unsigned char> source(
        static_cast<std::size_t>(width * height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double wave =
                std::cos(2.0 * 3.14159265358979323846 *
                    0.25 * static_cast<double>(x));
            const unsigned char value = static_cast<unsigned char>(
                std::clamp(std::lround(128.0 + 100.0 * wave), 0l, 255l));
            const std::size_t offset =
                (static_cast<std::size_t>(y) * width +
                 static_cast<std::size_t>(x)) * 4u;
            source[offset] = value;
            source[offset + 1u] = value;
            source[offset + 2u] = value;
            source[offset + 3u] = 255;
        }
    }

    EditorNodeGraph::Graph graph;
    EditorNodeGraph::ImagePayload image;
    image.width = width;
    image.height = height;
    image.channels = 4;
    image.originalChannels = 4;
    image.pixels = source;
    const int sourceId = graph.AddImageNode(
        std::move(image), { 0.0f, 0.0f })->id;
    const int splitId = graph.AddChannelSplitNode({ 180.0f, 0.0f })->id;
    const int fftId = graph.AddFrequencyFftNode({ 360.0f, 0.0f })->id;
    const int responseId =
        graph.AddFrequencyResponseNode({ 540.0f, 160.0f })->id;
    const int analyzerId =
        graph.AddSpectrumAnalyzerNode(
            EditorNodeGraph::SpectrumAnalyzerMode::RadialEnergy,
            { 540.0f, -160.0f })->id;
    const int applyId =
        graph.AddApplyFrequencyResponseNode({ 560.0f, 0.0f })->id;
    const int inverseId =
        graph.AddFrequencyIfftNode({ 760.0f, 0.0f })->id;
    const int outputId =
        graph.AddOutputNode({ 940.0f, 0.0f }, true)->id;
    EditorNodeGraph::Node* analyzer = graph.FindNode(analyzerId);
    analyzer->spectrumAnalyzerSettings.innerRadius = 0.24f;
    analyzer->spectrumAnalyzerSettings.outerRadius = 0.26f;
    analyzer->spectrumAnalyzerSettings.excludeDc = true;
    graph.SetParameterExposed(
        applyId, EditorNodeGraph::kStrengthParameterId, true);
    std::string error;
    bool authored =
        graph.TryConnectSockets(
            sourceId, EditorNodeGraph::kImageOutputSocketId,
            splitId, EditorNodeGraph::kImageInputSocketId, &error) &&
        graph.TryConnectSockets(
            splitId, "r", fftId,
            EditorNodeGraph::kChannelInputSocketId, &error) &&
        graph.TryConnectSockets(
            fftId, EditorNodeGraph::kSpectrumOutputSocketId,
            analyzerId, EditorNodeGraph::kSpectrumInputSocketId, &error) &&
        graph.TryConnectSockets(
            fftId, EditorNodeGraph::kSpectrumOutputSocketId,
            applyId, EditorNodeGraph::kSpectrumInputSocketId, &error) &&
        graph.TryConnectSockets(
            responseId,
            EditorNodeGraph::kFrequencyResponseOutputSocketId,
            applyId,
            EditorNodeGraph::kFrequencyResponseInputSocketId, &error) &&
        graph.TryConnectSockets(
            analyzerId, EditorNodeGraph::kBandPowerOutputSocketId,
            applyId,
            EditorNodeGraph::ParameterInputSocketId(
                EditorNodeGraph::kStrengthParameterId),
            &error) &&
        graph.TryConnectSockets(
            applyId, EditorNodeGraph::kSpectrumOutputSocketId,
            inverseId, EditorNodeGraph::kSpectrumInputSocketId, &error) &&
        graph.TryConnectSockets(
            inverseId, EditorNodeGraph::kChannelOutputSocketId,
            outputId, EditorNodeGraph::kImageInputSocketId, &error);
    bool ok = Phase6Check(
        authored,
        error.empty() ? "could not author the deterministic Spectrum Analyzer fixture" : error);
    if (!authored) return false;

    EditorModule editor;
    editor.GetNodeGraph() = std::move(graph);
    RenderGraphSnapshot snapshot = editor.BuildGraphSnapshot();
    snapshot.executionInspectionEnabled = true;
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.LoadSourceFromPixels(source.data(), width, height, 4);
    pipeline.ExecuteGraph(snapshot);
    RenderSpectrumAnalysis analysis;
    ok &= Phase6Check(
        pipeline.GetCachedSpectrumAnalysis(analyzerId, analysis),
        "Spectrum Analyzer did not publish its typed analysis result");
    const int expectedBin = static_cast<int>(
        std::floor(0.25 / 0.70710678 * 255.0));
    const float direction = std::abs(analysis.peakDirectionDegrees);
    ok &= Phase6Check(
        analysis.valid &&
        analysis.radialPower[static_cast<std::size_t>(expectedBin)] > 0.0f &&
        analysis.bandPower > 0.999f &&
        std::abs(analysis.peakFrequency - 0.25f) < 1.0e-5f &&
        (direction < 1.0e-3f ||
         std::abs(direction - 180.0f) < 1.0e-3f),
        "Spectrum Analyzer curve, band fraction, or peak measurement "
        "did not match the deterministic quarter-cycle fixture");

    const std::size_t originalAnalysisFingerprint = analysis.fingerprint;
    const auto analyzerSnapshotIt = std::find_if(
        snapshot.nodes.begin(),
        snapshot.nodes.end(),
        [analyzerId](const RenderGraphNode& node) {
            return node.nodeId == analyzerId;
        });
    if (analyzerSnapshotIt != snapshot.nodes.end()) {
        analyzerSnapshotIt->definitionVersion += "-replacement";
        pipeline.ExecuteGraph(snapshot);
        RenderSpectrumAnalysis replacementAnalysis;
        ok &= Phase6Check(
            pipeline.GetCachedSpectrumAnalysis(
                analyzerId, replacementAnalysis) &&
                replacementAnalysis.fingerprint !=
                    originalAnalysisFingerprint,
            "Spectrum Analyzer reused persistent analysis after an "
            "exact-definition version change");
    } else {
        ok &= Phase6Check(
            false,
            "Spectrum Analyzer was missing from its render snapshot");
    }
    return ok;
}

struct FrequencyFilterFixtureResult {
    std::vector<float> pixels;
    std::string specializedFailure;
};

FrequencyFilterFixtureResult RenderFrequencyFilterFixture(
    const std::vector<unsigned char>& source,
    int width,
    int height,
    const EditorNodeGraph::FrequencyFilterSettings& settings) {
    EditorNodeGraph::Graph graph;
    EditorNodeGraph::ImagePayload image;
    image.width = width;
    image.height = height;
    image.channels = 4;
    image.originalChannels = 4;
    image.pixels = source;
    const int sourceId =
        graph.AddImageNode(std::move(image), { 0.0f, 0.0f })->id;
    const int splitId =
        graph.AddChannelSplitNode({ 180.0f, 0.0f })->id;
    EditorNodeGraph::Node* filter = graph.AddFrequencyFilterNode(
        settings.localResponse.mode, { 360.0f, 0.0f });
    filter->frequencyFilterSettings = settings;
    const int filterId = filter->id;
    const int outputId =
        graph.AddOutputNode({ 580.0f, 0.0f }, true)->id;
    if (!graph.TryConnectSockets(
            sourceId, EditorNodeGraph::kImageOutputSocketId,
            splitId, EditorNodeGraph::kImageInputSocketId) ||
        !graph.TryConnectSockets(
            splitId, "r", filterId,
            EditorNodeGraph::kChannelInputSocketId) ||
        !graph.TryConnectSockets(
            filterId, EditorNodeGraph::kChannelOutputSocketId,
            outputId, EditorNodeGraph::kImageInputSocketId)) {
        return {};
    }
    EditorModule editor;
    editor.GetNodeGraph() = std::move(graph);
    RenderGraphSnapshot snapshot = editor.BuildGraphSnapshot();
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.LoadSourceFromPixels(source.data(), width, height, 4);
    pipeline.ExecuteGraph(snapshot);
    FrequencyFilterFixtureResult result;
    result.pixels = ReadPhase6Texture(
        pipeline.GetOutputTexture(), width, height);
    result.specializedFailure =
        pipeline.GetLastGraphExecutionStats().lastSpecializedFailure;
    return result;
}

bool ValidateFrequencyResponsesAndStrength() {
    constexpr int width = 16;
    constexpr int height = 8;
    std::vector<unsigned char> source(
        static_cast<std::size_t>(width * height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double wave = std::cos(
                2.0 * 3.14159265358979323846 *
                0.25 * static_cast<double>(x));
            const unsigned char value = static_cast<unsigned char>(
                std::clamp(std::lround(128.0 + 100.0 * wave), 0l, 255l));
            const std::size_t offset =
                (static_cast<std::size_t>(y) * width +
                 static_cast<std::size_t>(x)) * 4u;
            source[offset] = value;
            source[offset + 1u] = value;
            source[offset + 2u] = value;
            source[offset + 3u] = 255;
        }
    }
    std::vector<float> reference(
        static_cast<std::size_t>(width * height) * 4u, 1.0f);
    for (std::size_t offset = 0; offset < reference.size();
         offset += 4u) {
        const float value =
            static_cast<float>(source[offset]) / 255.0f;
        reference[offset] = value;
        reference[offset + 1u] = value;
        reference[offset + 2u] = value;
    }

    EditorNodeGraph::FrequencyFilterSettings settings;
    settings.localResponse.mode =
        EditorNodeGraph::FrequencyFilterMode::LowPass;
    settings.localResponse.lowCutoff = 0.08f;
    settings.localResponse.transitionWidth = 0.02f;
    settings.strength = 0.0f;
    FrequencyFilterFixtureResult bypass =
        RenderFrequencyFilterFixture(source, width, height, settings);
    bool ok = Phase6Check(
        !bypass.pixels.empty() &&
        MaximumDifference(bypass.pixels, reference) <= 5.0e-4 &&
        bypass.specializedFailure.empty(),
        "Frequency Filter strength 0 was not an exact no-FFT bypass");

    settings.strength = 1.0f;
    for (EditorNodeGraph::FrequencyTransitionProfile profile : {
             EditorNodeGraph::FrequencyTransitionProfile::Smooth,
             EditorNodeGraph::FrequencyTransitionProfile::Gaussian,
             EditorNodeGraph::FrequencyTransitionProfile::Butterworth,
             EditorNodeGraph::FrequencyTransitionProfile::Hard }) {
        settings.localResponse.profile = profile;
        FrequencyFilterFixtureResult filtered =
            RenderFrequencyFilterFixture(source, width, height, settings);
        float minimum = std::numeric_limits<float>::max();
        float maximum = std::numeric_limits<float>::lowest();
        for (std::size_t offset = 0; offset < filtered.pixels.size();
             offset += 4u) {
            minimum = std::min(minimum, filtered.pixels[offset]);
            maximum = std::max(maximum, filtered.pixels[offset]);
        }
        ok &= Phase6Check(
            !filtered.pixels.empty() &&
            maximum - minimum < 0.04f &&
            filtered.specializedFailure.empty(),
            "a Frequency Response transition profile did not suppress "
            "the deterministic out-of-band signal or preserve Hermitian output");
    }

    std::vector<unsigned char> notchSource = source;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double wave =
                55.0 * std::cos(
                    2.0 * 3.14159265358979323846 *
                    0.125 * static_cast<double>(x)) +
                55.0 * std::cos(
                    2.0 * 3.14159265358979323846 *
                    0.25 * static_cast<double>(x));
            const unsigned char value = static_cast<unsigned char>(
                std::clamp(std::lround(128.0 + wave), 0l, 255l));
            const std::size_t offset =
                (static_cast<std::size_t>(y) * width +
                 static_cast<std::size_t>(x)) * 4u;
            notchSource[offset] = value;
            notchSource[offset + 1u] = value;
            notchSource[offset + 2u] = value;
        }
    }
    settings.localResponse.mode =
        EditorNodeGraph::FrequencyFilterMode::NotchReject;
    settings.localResponse.profile =
        EditorNodeGraph::FrequencyTransitionProfile::Hard;
    settings.localResponse.notches = {
        { "fixture-notch-a", 0.125f, 0.0f, 0.01f },
        { "fixture-notch-b", 0.25f, 0.0f, 0.01f }
    };
    FrequencyFilterFixtureResult notched =
        RenderFrequencyFilterFixture(
            notchSource, width, height, settings);
    float notchMinimum = std::numeric_limits<float>::max();
    float notchMaximum = std::numeric_limits<float>::lowest();
    for (std::size_t offset = 0; offset < notched.pixels.size();
         offset += 4u) {
        notchMinimum = std::min(notchMinimum, notched.pixels[offset]);
        notchMaximum = std::max(notchMaximum, notched.pixels[offset]);
    }
    ok &= Phase6Check(
        !notched.pixels.empty() &&
        notchMaximum - notchMinimum < 0.04f &&
        notched.specializedFailure.empty(),
        "multiple mirrored notch pairs did not reject both deterministic "
        "frequencies while preserving a real inverse Channel");

    EditorNodeGraph::FrequencyFilterSettings malformed = settings;
    malformed.localResponse.mode =
        EditorNodeGraph::FrequencyFilterMode::LowPass;
    malformed.localResponse.profile =
        EditorNodeGraph::FrequencyTransitionProfile::Smooth;
    malformed.localResponse.lowCutoff = 9.0f;
    malformed.localResponse.highCutoff = -4.0f;
    malformed.localResponse.transitionWidth = 8.0f;
    malformed.localResponse.butterworthOrder = 99.0f;
    malformed.localResponse.notches = {
        { "out-of-domain", 8.0f, 720.0f, 4.0f }
    };
    EditorNodeGraph::FrequencyFilterSettings canonical = malformed;
    canonical.localResponse.lowCutoff = 0.0f;
    canonical.localResponse.highCutoff = 0.5f;
    canonical.localResponse.transitionWidth = 0.5f;
    canonical.localResponse.butterworthOrder = 12.0f;
    canonical.localResponse.notches = {
        { "out-of-domain", 0.5f, 180.0f, 0.25f }
    };
    const FrequencyFilterFixtureResult malformedResult =
        RenderFrequencyFilterFixture(
            source, width, height, malformed);
    const FrequencyFilterFixtureResult canonicalResult =
        RenderFrequencyFilterFixture(
            source, width, height, canonical);
    ok &= Phase6Check(
        !malformedResult.pixels.empty() &&
        !canonicalResult.pixels.empty() &&
        MaximumDifference(
            malformedResult.pixels,
            canonicalResult.pixels) <= 1.0e-6 &&
        malformedResult.specializedFailure.empty(),
        "a local Frequency Filter bypassed the canonical Response parameter "
        "domain used by an extracted Response node");
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

bool RunPhase6SpecializedValidation() {
    constexpr int width = 7;
    constexpr int height = 5;
    constexpr double tolerance = 7.5e-3;
    std::vector<unsigned char> source(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned char value = static_cast<unsigned char>((x * 29 + y * 41) & 255);
            const std::size_t index =
                (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4u;
            source[index + 0] = value;
            source[index + 1] = value;
            source[index + 2] = value;
            source[index + 3] = 255;
        }
    }
    RenderGraphSnapshot graph;
    std::string error;
    bool ok = Phase6Check(BuildAuthoredFrequencyRoundTrip(
        source, width, height, graph, error),
        error.empty() ? "the authored FFT/IFFT graph did not lower" : error);
    ok &= ValidateFrequencyEditorActions();
    ok &= ValidateSpectrumAnalyzerFixture();
    ok &= ValidateFrequencyResponsesAndStrength();
    ok &= Phase6Check(
        ValidateGpuFftAgainstCpuReference(),
        "odd, non-power-of-two, signed HDR GPU FFT results diverged from the CPU DFT reference");
    const RenderGraphRegionPlan plan = RenderTiling::PlanGraphRegions(graph, width, height);
    const auto isTypedTransform = [](const auto& stage, NodeMath::SpecializedStageKind kind) {
        return stage.specializedKind == kind &&
            stage.capability == NodeMath::CapabilityClass::MultipassIterative &&
            stage.regionRequirement == NodeMath::RegionRequirement::FullFrame;
    };
    const int forwardStages = static_cast<int>(std::count_if(
        plan.stages.begin(), plan.stages.end(), [&](const auto& stage) {
            return isTypedTransform(stage, NodeMath::SpecializedStageKind::FrequencyTransform);
        }));
    const int inverseStages = static_cast<int>(std::count_if(
        plan.stages.begin(), plan.stages.end(), [&](const auto& stage) {
            return isTypedTransform(stage, NodeMath::SpecializedStageKind::FrequencyInverseTransform);
        }));
    ok &= Phase6Check(
        plan.valid && plan.requiresFullFrame && !plan.tileable &&
            forwardStages == 1 && inverseStages == 1,
        "FFT and IFFT were not planned as distinct typed full-frame multipass stages");

    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.LoadSourceFromPixels(source.data(), width, height, 4);
    pipeline.ExecuteGraph(graph);
    const std::vector<float> rendered = ReadPhase6Texture(
        pipeline.GetOutputTexture(), width, height);
    std::vector<float> reference(rendered.size(), 1.0f);
    for (std::size_t index = 0; index < reference.size(); index += 4) {
        const float value = static_cast<float>(source[index]) / 255.0f;
        reference[index + 0] = value;
        reference[index + 1] = value;
        reference[index + 2] = value;
        reference[index + 3] = 1.0f;
    }
    const double difference = MaximumDifference(rendered, reference);
    const GraphExecutionStats stats = pipeline.GetLastGraphExecutionStats();
    ok &= Phase6Check(
        !rendered.empty() && difference <= tolerance &&
        stats.lastSpecializedFailure.empty() &&
        stats.frequencyCacheHits > 0 &&
        stats.frequencyCacheMisses > 0,
        "the representative specialized chain failed, bypassed its typed "
        "frequency cache, or differed by " + std::to_string(difference));

    RenderGraphSnapshot previewGraph = graph;
    const auto splitIt = std::find_if(
        previewGraph.nodes.begin(),
        previewGraph.nodes.end(),
        [](const RenderGraphNode& node) {
            return node.kind == RenderGraphNodeKind::ChannelSplit;
        });
    const int previewSplitId =
        splitIt != previewGraph.nodes.end() ? splitIt->nodeId : -1;
    constexpr int transientTransformId = -200001;
    constexpr int transientViewId = -200002;
    RenderGraphNode transientTransform;
    transientTransform.nodeId = transientTransformId;
    transientTransform.kind = RenderGraphNodeKind::FrequencyFft;
    transientTransform.frequencyFftSettings.edgePolicy =
        RenderFrequencyEdgePolicy::Mirror;
    RenderGraphNode transientView;
    transientView.nodeId = transientViewId;
    transientView.kind = RenderGraphNodeKind::SpectrumView;
    transientView.spectrumViewSettings.mode =
        RenderSpectrumViewMode::Magnitude;
    transientView.spectrumViewSettings.centerDc = true;
    previewGraph.nodes.push_back(std::move(transientTransform));
    previewGraph.nodes.push_back(std::move(transientView));
    if (previewSplitId > 0) {
        previewGraph.links.push_back(RenderGraphLink{
            previewSplitId,
            "r",
            transientTransformId,
            EditorNodeGraph::kChannelInputSocketId
        });
    }
    previewGraph.links.push_back(RenderGraphLink{
        transientTransformId,
        EditorNodeGraph::kSpectrumOutputSocketId,
        transientViewId,
        EditorNodeGraph::kSpectrumInputSocketId
    });
    previewGraph.outputNodeId = transientViewId;
    previewGraph.outputSocketId = EditorNodeGraph::kImageOutputSocketId;
    const RenderGraphRegionPlan previewPlan =
        RenderTiling::PlanGraphRegions(previewGraph, width, height);
    pipeline.ExecuteGraph(previewGraph);
    int previewWidth = 0;
    int previewHeight = 0;
    const std::vector<unsigned char> previewPixels =
        pipeline.GetPreviewPixels(previewWidth, previewHeight, 512);
    ok &= Phase6Check(
        previewSplitId > 0 &&
        previewPlan.valid &&
        !previewPixels.empty() &&
        previewWidth == width &&
        previewHeight == height &&
        pipeline.GetLastGraphExecutionStats().lastSpecializedFailure.empty(),
        "the Frequency Filter spectrum preview rejected its negative "
        "transient transform/view node IDs or produced no pixels");
    if (ok) {
        std::cout
            << "Phase 6 specialized validation passed: authored FFT -> "
               "Separate -> Recombine -> IFFT round-trip, response profiles, "
               "mirrored notches, analyzer, transient preview, and editor "
               "actions (max difference "
            << difference << ").\n";
    }
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
    ok &= RunPhase6SpecializedValidation();
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
                     "reduction, typed consumers, and specialized frequency execution share "
                     "the planner/diagnostic framework (Gaussian max difference "
                  << maximumDifference << ").\n";
    }
    return ok;
}

} // namespace

bool ValidateNodeMathPhase6Integration(bool calibrationOnly, bool transactionsOnly) {
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
    if (calibrationOnly) {
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
